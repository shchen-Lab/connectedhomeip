#include "BridgeUartRuntime.h"
#include "BridgeApp.h"
#include "BridgeAppInternal.h"
#include "BridgeDevice.h"
#include "BridgeUartLifecycle.h"
#include "LightUartBridge.h"

#include <FreeRTOS.h>
#include <atomic>
#include <crypto/RandUtils.h>
#include <cstdio>
#include <cstring>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/KeyValueStoreManager.h>
#include <platform/LockTracker.h>
#include <queue.h>
#include <system/SystemClock.h>

using namespace chip;
using namespace chip::DeviceLayer;
using chip::Protocols::InteractionModel::Status;

namespace {
constexpr unsigned kCapacity      = 32;
constexpr size_t kPayloadCapacity = 18;
constexpr char kStoreKey[]        = "bridge-uart-v2";
constexpr size_t kStoreSize       = 12 + kCapacity * 12;
uint32_t U32(const uint8_t * p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint16_t U16(const uint8_t * p)
{
    return uint16_t(p[0]) | uint16_t(p[1]) << 8;
}
void Put(uint8_t * p, uint32_t v, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        p[i] = uint8_t(v >> (8 * i));
}
uint32_t Now()
{
    return uint32_t(System::SystemClock().GetMonotonicMilliseconds64().count());
}

struct Record
{
    uint32_t id = 0, binding = 0, version = 0;
    uint16_t endpoint = 0, mask = 0;
    uint8_t type = 0, persistentState = 0; // 1=PENDING_ADD, 2=committed, 3=PENDING_REMOVE
    bool bound = false, online = false, synced = false, listedRemoved = false, needsBind = false;
    uint8_t values[4]      = {};
    uint16_t versionFields = 0;
};
Record records[kCapacity];
uint32_t counter = 0, boot = 0, session = 0, transaction = 0, lastHeartbeat = 0, nextProbe = 0;
uint16_t sequence = 1, negotiated = 1024;
uint8_t heartbeatMisses = 0;
bool storageFailed = false, initialized = false;
BridgeUartLifecycle lifecycle;
struct Packet
{
    lu_frame_t frame                  = {};
    uint8_t payload[kPayloadCapacity] = {};
};
struct CachedResponse
{
    bool valid = false;
    Packet request;
    Packet response;
};
CachedResponse responseCache[8];
unsigned responseCursor = 0;
bool cacheResponse      = false;
bool haveBusyRequest    = false;
Packet busyRequest;
struct Pending
{
    bool active = false;
    Packet packet;
    uint32_t sent   = 0;
    uint8_t retries = 0;
} pending;
app::CommandHandler::Handle commandHandle;
app::ConcreteCommandPath commandPath;

StaticQueue_t queueControl;
uint8_t queueStorage[40 * sizeof(Packet)];
QueueHandle_t queueHandle = nullptr;
std::atomic<bool> drainScheduled{ false }, overflow{ false };

Record * Find(uint32_t id)
{
    for (auto & r : records)
        if (r.id == id && id != 0)
            return &r;
    return nullptr;
}
Record * FindEndpoint(uint16_t endpoint)
{
    for (auto & r : records)
        if (r.id && r.endpoint == endpoint)
            return &r;
    return nullptr;
}
uint32_t Cap(uint8_t type)
{
    return type == 0x10 ? 0x13 : type == 0x11 ? 0x17 : 0;
}
bool ValidStateFlags(uint8_t flags, uint32_t version)
{
    return (flags & 0xF8) == 0 && ((flags & 1) || flags == 0) && (!(flags & 4) || version != 0);
}
void Reachable(Record & r, bool value)
{
    if (auto * d = FindBridgeDevice(r.endpoint))
        d->SetReachable(value);
}
CHIP_ERROR Save()
{
    uint8_t bytes[kStoreSize] = {};
    Put(bytes, 1, 4);
    Put(bytes + 4, counter, 4);
    for (unsigned i = 0; i < kCapacity; ++i)
    {
        uint8_t * p    = bytes + 8 + 12 * i;
        const auto & r = records[i];
        // Do not persist an incomplete new record before allocating its binding.
        if (!r.binding)
            continue;
        Put(p, r.id, 4);
        Put(p + 4, r.binding, 4);
        Put(p + 8, r.endpoint, 2);
        p[10] = r.type;
        p[11] = r.persistentState;
    }
    Put(bytes + kStoreSize - 4, lu_crc32_iso_hdlc(bytes, kStoreSize - 4), 4);
    CHIP_ERROR err = PersistedStorage::KeyValueStoreMgr().Put(kStoreKey, bytes, sizeof(bytes));
    if (err != CHIP_NO_ERROR)
    {
        storageFailed = true;
        for (auto & r : records)
            if (r.id)
            {
                r.online = r.synced = false;
                Reachable(r, false);
            }
    }
    return err;
}
CHIP_ERROR AllocateBinding(Record & r)
{
    VerifyOrReturnError(!storageFailed && counter != UINT32_MAX, CHIP_ERROR_PERSISTED_STORAGE_FAILED);
    // Persist the high watermark before using the new binding.
    ++counter;
    ReturnErrorOnFailure(Save());
    r.binding         = counter;
    r.persistentState = 1;
    return Save();
}
CHIP_ERROR CreateEndpoint(Record & r, bool restore)
{
    char name[32], unique[32];
    std::snprintf(name, sizeof(name), "UART-%08lx", static_cast<unsigned long>(r.id));
    std::snprintf(unique, sizeof(unique), "bridge-uart-%08lx", static_cast<unsigned long>(r.id));
    DynamicBridgeDeviceParams params{ r.type == 0x10 ? DynamicBridgeDeviceType::kColorTemperatureLight
                                                     : DynamicBridgeDeviceType::kExtendedColorLight,
                                      name, "", unique };
    if (restore)
        params.requestedEndpoint = r.endpoint;
    return AddDynamicBridgeDeviceLocked(params, r.endpoint);
}
CHIP_ERROR Restore()
{
    uint8_t bytes[kStoreSize] = {};
    size_t size               = 0;
    CHIP_ERROR err            = PersistedStorage::KeyValueStoreMgr().Get(kStoreKey, bytes, sizeof(bytes), &size);
    if (err == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND)
        return CHIP_NO_ERROR;
    ReturnErrorOnFailure(err);
    VerifyOrReturnError(size == sizeof(bytes) && U32(bytes) == 1 && U32(bytes + size - 4) == lu_crc32_iso_hdlc(bytes, size - 4),
                        CHIP_ERROR_INTEGRITY_CHECK_FAILED);
    counter = U32(bytes + 4);
    // Validate the entire table before exposing any endpoint.
    for (unsigned i = 0; i < kCapacity; ++i)
    {
        const uint8_t * p = bytes + 8 + 12 * i;
        auto & r          = records[i];
        r.id              = U32(p);
        r.binding         = U32(p + 4);
        r.endpoint        = U16(p + 8);
        r.type            = p[10];
        r.persistentState = p[11];
        if (!r.id)
        {
            VerifyOrReturnError(r.binding == 0 && r.endpoint == 0 && r.type == 0 && r.persistentState == 0,
                                CHIP_ERROR_INTEGRITY_CHECK_FAILED);
            continue;
        }
        VerifyOrReturnError(Cap(r.type) && r.endpoint > 1 && r.endpoint != kInvalidEndpointId && r.binding &&
                                r.binding <= counter && r.persistentState >= 1 && r.persistentState <= 3,
                            CHIP_ERROR_INTEGRITY_CHECK_FAILED);
        for (unsigned j = 0; j < i; ++j)
            VerifyOrReturnError(records[j].id != r.id && (!records[j].id || records[j].endpoint != r.endpoint),
                                CHIP_ERROR_INTEGRITY_CHECK_FAILED);
    }
    bool removed = false;
    for (auto & r : records)
    {
        if (!r.id)
            continue;
        if (r.persistentState == 3)
        {
            r       = {};
            removed = true;
            continue;
        }
        ReturnErrorOnFailure(CreateEndpoint(r, true));
    }
    return removed ? Save() : CHIP_NO_ERROR;
}
void FinishCommand(Status status)
{
    if (auto * handler = commandHandle.Get())
        handler->AddStatus(commandPath, status);
    commandHandle.Release();
}
CHIP_ERROR Send(Packet & p)
{
    uint8_t bytes[LU_MIN_FRAME_SIZE + kPayloadCapacity];
    size_t size = 0;
    auto & f    = p.frame;
    VerifyOrReturnError(f.payload_len <= kPayloadCapacity, CHIP_ERROR_INVALID_ARGUMENT);
    lu_status_t result = lu_pack_frame(f.type, f.flags, f.seq, f.session_id, f.uart_device_id, f.endpoint, f.binding_version,
                                       f.cluster, f.id, p.payload, f.payload_len, bytes, sizeof(bytes), &size);
    VerifyOrReturnError(result == LU_OK, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(LightUartBridgeWrite(bytes, uint16_t(size)) == int16_t(size), CHIP_ERROR_WRITE_FAILED);
    ChipLogProgress(Zcl, "UART v2 TX type=%02x seq=%u dev=%lu ep=%u bind=%lu", f.type, f.seq,
                    static_cast<unsigned long>(f.uart_device_id), f.endpoint, static_cast<unsigned long>(f.binding_version));
    return CHIP_NO_ERROR;
}
CHIP_ERROR Start(uint8_t type, Record * r, const uint8_t * bytes, uint16_t size, uint32_t cluster = 0, uint32_t id = 0)
{
    VerifyOrReturnError(!pending.active && size <= negotiated && size <= kPayloadCapacity, CHIP_ERROR_BUSY);
    Packet p;
    p.frame.type  = type;
    p.frame.flags = LU_FLAG_ACK_REQUIRED;
    p.frame.seq   = sequence++;
    if (!sequence)
        sequence = 1;
    p.frame.session_id = type == LU_MSG_HELLO_REQUEST ? 0 : session;
    if (r)
    {
        p.frame.uart_device_id  = r->id;
        p.frame.endpoint        = r->endpoint;
        p.frame.binding_version = r->binding;
    }
    p.frame.cluster     = cluster;
    p.frame.id          = id;
    p.frame.payload_len = size;
    if (size)
        std::memcpy(p.payload, bytes, size);
    ReturnErrorOnFailure(Send(p));
    pending = { true, p, Now(), 0 };
    return CHIP_NO_ERROR;
}
void Reply(const Packet & request, uint8_t type, uint8_t status, const uint8_t * extra = nullptr, uint16_t size = 0)
{
    Packet p            = request;
    p.frame.type        = type;
    p.frame.flags       = LU_FLAG_RESPONSE | (status ? LU_FLAG_ERROR : 0);
    p.frame.payload_len = status ? 1 : size + 1;
    p.payload[0]        = status;
    if (!status && size)
        std::memcpy(p.payload + 1, extra, size);
    if (cacheResponse && status == LU_STATUS_BUSY)
    {
        busyRequest     = request;
        haveBusyRequest = true;
    }
    if (cacheResponse && status != LU_STATUS_BUSY)
    {
        haveBusyRequest               = false;
        responseCache[responseCursor] = { true, request, p };
        responseCursor                = (responseCursor + 1) % 8;
    }
    if (Send(p) != CHIP_NO_ERROR)
        ChipLogError(Zcl, "UART v2 response write failed");
}
void Disconnect()
{
    FinishCommand(Status::Failure);
    pending.active = false;
    session        = 0;
    for (auto & cached : responseCache)
        cached.valid = false;
    haveBusyRequest = false;
    for (auto & r : records)
    {
        if (!r.id)
            continue;
        Reachable(r, false);
        r.bound = r.online = r.synced = r.needsBind = false;
        r.listedRemoved                             = false;
        r.version                                   = 0;
        r.versionFields                             = 0;
    }
    lifecycle.Reset(boot);
    nextProbe = Now() + 5000;
}
void Hello()
{
    uint8_t bytes[14] = { 2, 0 };
    Put(bytes + 2, boot, 4);
    Put(bytes + 10, 1024, 2);
    Put(bytes + 12, 1059, 2);
    if (Start(LU_MSG_HELLO_REQUEST, nullptr, bytes, sizeof(bytes)) != CHIP_NO_ERROR)
        nextProbe = Now() + 5000;
}
void RequestList()
{
    if (++transaction == 0)
        ++transaction;
    uint8_t bytes[12] = {};
    Put(bytes, transaction, 4);
    Put(bytes + 4, lifecycle.GetDeviceListVersion(), 4);
    if (Start(LU_MSG_DEVICE_LIST_REQUEST, nullptr, bytes, sizeof(bytes)) != CHIP_NO_ERROR)
        Disconnect();
}
void NextBind()
{
    if (pending.active || storageFailed)
        return;
    for (auto & r : records)
    {
        if (!r.id || !r.needsBind)
            continue;
        r.needsBind       = false;
        uint8_t bytes[10] = {};
        bytes[0]          = r.type;
        Put(bytes + 1, Cap(r.type), 4);
        bytes[5] = 1;
        if (Start(LU_MSG_DEVICE_BIND_REQUEST, &r, bytes, sizeof(bytes)) != CHIP_NO_ERROR)
            Disconnect();
        return;
    }
    lifecycle.BindingsComplete();
}
uint8_t Add(uint32_t id, uint8_t type, uint32_t capabilities, uint8_t flags, uint32_t version, Record *& out)
{
    out = Find(id);
    if (!id || !ValidStateFlags(flags, version) || !(flags & 1))
        return LU_STATUS_INVALID_ARGUMENT;
    if (out && (out->type != type || Cap(out->type) != capabilities))
        return LU_STATUS_DEVICE_ID_CONFLICT;
    if (!Cap(type) || Cap(type) != capabilities)
        return LU_STATUS_UNSUPPORTED_DEVICE_TYPE;
    if (storageFailed)
        return LU_STATUS_INTERNAL_ERROR;
    if (!out)
    {
        for (auto & r : records)
            if (!r.id)
            {
                out = &r;
                break;
            }
        if (!out)
            return LU_STATUS_ENDPOINT_EXHAUSTED;
        out->id   = id;
        out->type = type;
        if (CreateEndpoint(*out, false) != CHIP_NO_ERROR)
        {
            *out = {};
            out  = nullptr;
            return LU_STATUS_INTERNAL_ERROR;
        }
        if (AllocateBinding(*out) != CHIP_NO_ERROR)
        {
            Reachable(*out, false);
            return LU_STATUS_INTERNAL_ERROR;
        }
    }
    if (!out->bound)
    {
        out->online        = flags & 2;
        out->listedRemoved = false;
        out->needsBind     = true;
    }
    return LU_STATUS_OK;
}
bool Matching(const lu_frame_t & f)
{
    const auto & q = pending.packet.frame;
    return pending.active && f.seq == q.seq && f.uart_device_id == q.uart_device_id && f.endpoint == q.endpoint &&
        f.binding_version == q.binding_version && f.cluster == q.cluster && f.id == q.id &&
        (q.type == LU_MSG_HELLO_REQUEST ? f.session_id != 0 : f.session_id == q.session_id) &&
        ((q.type == LU_MSG_DOWN_INVOKE_COMMAND && f.type == LU_MSG_DOWN_COMMAND_RESPONSE) ||
         (q.type == LU_MSG_DOWN_READ_ATTRIBUTE && f.type == LU_MSG_DOWN_READ_ATTRIBUTE_RESPONSE) ||
         ((q.type == LU_MSG_HELLO_REQUEST || q.type == LU_MSG_DEVICE_BIND_REQUEST || q.type == LU_MSG_HEARTBEAT ||
           q.type == LU_MSG_DEVICE_STATE_REQUEST) &&
          f.type == q.type + 1));
}
uint8_t CheckBinding(const lu_frame_t & f, Record * r)
{
    if (!r)
        return LU_STATUS_UNKNOWN_DEVICE;
    if (f.session_id != session || !session || f.endpoint != r->endpoint || f.binding_version != r->binding)
        return LU_STATUS_BINDING_MISMATCH;
    if (!r->bound)
        return LU_STATUS_BUSY;
    return LU_STATUS_OK;
}
uint8_t State(Packet & p, bool snapshot)
{
    auto & f       = p.frame;
    Record * r     = Find(f.uart_device_id);
    uint8_t status = CheckBinding(f, r);
    if (status)
        return status;
    if (!r->online)
        return LU_STATUS_DEVICE_OFFLINE;
    if (!snapshot && !r->synced)
        return LU_STATUS_STATE_UNAVAILABLE;
    if (f.payload_len != (snapshot ? 10 : (f.cluster == 0x300 && f.id == 7 ? 6 : 5)))
        return LU_STATUS_INVALID_ARGUMENT;
    uint32_t version = U32(p.payload);
    if (!version || (r->version && static_cast<int32_t>(version - r->version) < 0))
        return LU_STATUS_BAD_STATE_VERSION;
    uint16_t mask = snapshot              ? U16(p.payload + 4)
        : f.cluster == 6 && f.id == 0     ? 1
        : f.cluster == 8 && f.id == 0     ? 2
        : f.cluster == 0x300 && f.id == 0 ? 4
        : f.cluster == 0x300 && f.id == 1 ? 8
        : f.cluster == 0x300 && f.id == 7 ? 0x40
                                          : 0;
    if (snapshot && (f.cluster || f.id || (mask != 0x43 && !(r->type == 0x11 && mask == 0x0F))))
        return LU_STATUS_INVALID_ARGUMENT;
    if (!mask || (!snapshot && (mask & r->mask) != mask))
        return LU_STATUS_UNSUPPORTED_ATTRIBUTE;
    if (snapshot && r->synced && version == r->version && mask != r->mask)
        return LU_STATUS_BAD_STATE_VERSION;
    uint8_t values[4];
    std::memcpy(values, r->values, sizeof(values));
    const uint8_t * input = p.payload + (snapshot ? 6 : 4);
    for (unsigned bit = 0; bit <= 6; ++bit)
    {
        if (!(mask & (1u << bit)))
            continue;
        unsigned index  = bit == 6 ? 2 : bit;
        unsigned length = bit == 6 ? 2 : 1;
        uint16_t value  = length == 2 ? U16(input) : *input;
        if ((bit == 0 && value > 1) || (bit == 1 && (value < 1 || value > 254)) || ((bit == 2 || bit == 3) && value > 254) ||
            (bit == 6 && (value < 154 || value > 454)))
            return LU_STATUS_INVALID_ARGUMENT;
        if (version == r->version && (r->versionFields & (1u << bit)) && std::memcmp(r->values + index, input, length) != 0)
            return LU_STATUS_BAD_STATE_VERSION;
        std::memcpy(values + index, input, length);
        input += length;
    }
    auto * d = FindBridgeDevice(r->endpoint);
    if (!d)
        return LU_STATUS_INTERNAL_ERROR;
    if (version != r->version)
        r->versionFields = 0;
    r->version = version;
    r->versionFields |= mask;
    std::memcpy(r->values, values, sizeof(values));
    if (snapshot)
    {
        r->mask   = mask;
        r->synced = true;
    }
    if (mask & 1)
        d->SetOnOff(values[0] != 0);
    if (mask & 2)
        d->SetLevel(values[1]);
    if (mask & 0x0C)
        d->SetHueSaturationState(values[2], values[3]);
    if (mask & 0x40)
        d->SetColorTemperatureMireds(U16(values + 2));
    if (snapshot)
        Reachable(*r, true);
    return LU_STATUS_OK;
}
void ProcessImpl(Packet & p)
{
    auto & f  = p.frame;
    f.payload = p.payload;
    if (f.type == LU_MSG_HELLO_RESPONSE)
    {
        if (!Matching(f) || f.payload_len != 17 || p.payload[0] || p.payload[1] != 2 || p.payload[2] || U32(p.payload + 3) ||
            U16(p.payload + 7) < 18 || U16(p.payload + 7) > 1024 || !U32(p.payload + 9))
            return;
        if (!lifecycle.AcceptHelloResponse(f.seq, pending.packet.frame.seq, f.session_id, 0, U32(p.payload + 9)))
            return;
        session         = f.session_id;
        negotiated      = U16(p.payload + 7);
        pending.active  = false;
        heartbeatMisses = 0;
        lastHeartbeat   = Now();
        RequestList();
        return;
    }
    if (!session || f.session_id != session || f.payload_len > negotiated)
        return;
    if (f.type >= LU_MSG_DEVICE_LIST_BEGIN && f.type <= LU_MSG_DEVICE_LIST_END)
    {
        if (!pending.active || pending.packet.frame.type != LU_MSG_DEVICE_LIST_REQUEST || f.uart_device_id || f.endpoint ||
            f.binding_version || f.cluster || f.id || f.payload_len < 4 || U32(p.payload) != transaction)
            return;
        bool valid = false;
        if (f.type == LU_MSG_DEVICE_LIST_BEGIN && f.payload_len == 16)
            valid =
                lifecycle.BeginList(transaction, U32(p.payload + 4), U16(p.payload + 8), U16(p.payload + 10), U32(p.payload + 12));
        if (f.type == LU_MSG_DEVICE_LIST_ENTRY && f.payload_len == 18)
            valid = lifecycle.AddListEntry(
                transaction, { U32(p.payload + 4), p.payload[8], U32(p.payload + 9), p.payload[13], U32(p.payload + 14) });
        if (f.type == LU_MSG_DEVICE_LIST_END && f.payload_len == 11)
        {
            valid = lifecycle.FinishList(transaction, U16(p.payload + 4), U32(p.payload + 6), p.payload[10]);
            if (valid)
            {
                pending.active = false;
                for (unsigned i = 0; i < lifecycle.GetListCount(); ++i)
                {
                    const auto & e = lifecycle.GetListEntries()[i];
                    Record * r     = Find(e.deviceId);
                    bool existing  = r != nullptr;
                    if (!(e.stateFlags & 1))
                    {
                        if (r)
                            r->listedRemoved = true;
                        continue;
                    }
                    uint8_t status = Add(e.deviceId, e.deviceType, e.capabilityFlags, e.stateFlags, e.stateVersion, r);
                    if (!status && existing && AllocateBinding(*r) != CHIP_NO_ERROR)
                        status = LU_STATUS_INTERNAL_ERROR;
                    if (status)
                    {
                        if (r)
                            r->needsBind = false;
                        ChipLogError(Zcl, "UART list device=%lu status=%u", static_cast<unsigned long>(e.deviceId), status);
                    }
                }
                NextBind();
            }
        }
        if (!valid)
            ChipLogError(Zcl, "UART list rejected type=%02x", f.type);
        return;
    }
    if (f.type == LU_MSG_UP_STATE_SNAPSHOT)
    {
        uint8_t status = State(p, true), applied[4] = {};
        Record * r = Find(f.uart_device_id);
        if (r)
            Put(applied, r->version, 4);
        Reply(p, LU_MSG_STATE_SNAPSHOT_RESPONSE, status, applied, 4);
        return;
    }
    if (f.type == LU_MSG_UP_ATTRIBUTE_REPORT)
    {
        uint8_t status = State(p, false);
        if (status)
            ChipLogError(Zcl, "UART report rejected status=%u ep=%u", status, f.endpoint);
        return;
    }
    if (Matching(f))
    {
        if (!f.payload_len)
            return;
        uint8_t status = p.payload[0];
        if (status > LU_STATUS_STATE_UNAVAILABLE || bool(f.flags & LU_FLAG_ERROR) != bool(status) || (status && f.payload_len != 1))
            return;
        if (status == LU_STATUS_BUSY)
        {
            // Keep the original deadline so repeated BUSY cannot stall forever.
            return;
        }
        if (f.type == LU_MSG_DEVICE_BIND_RESPONSE)
        {
            if (!status && (f.payload_len != 7 || U16(p.payload + 1) != f.endpoint || U32(p.payload + 3) != f.binding_version))
                return;
            Record * r = Find(f.uart_device_id);
            if (r && !status)
            {
                r->persistentState = 2;
                if (Save() == CHIP_NO_ERROR)
                    r->bound = true;
            }
        }
        else if (f.type == LU_MSG_DOWN_COMMAND_RESPONSE)
        {
            if (!status && (f.payload_len != 6 || p.payload[5] != 0))
                return;
            FinishCommand(!status                                       ? Status::Success
                              : status == LU_STATUS_UNSUPPORTED_COMMAND ? Status::UnsupportedCommand
                              : status == LU_STATUS_INVALID_ARGUMENT    ? Status::ConstraintError
                                                                        : Status::Failure);
        }
        else if (f.type == LU_MSG_DOWN_READ_ATTRIBUTE_RESPONSE)
        {
            if (!status)
            {
                if (f.payload_len != (f.cluster == 0x300 && f.id == 7 ? 7 : 6))
                    return;
                Packet report = p;
                --report.frame.payload_len;
                std::memmove(report.payload, report.payload + 1, report.frame.payload_len);
                State(report, false);
            }
        }
        else if (f.type == LU_MSG_HEARTBEAT_RESPONSE)
        {
            if (!status && f.payload_len != 9)
                return;
            if (!status)
                heartbeatMisses = 0;
            else
                ++heartbeatMisses;
            if (heartbeatMisses >= 3 || (!status && U32(p.payload + 5) != lifecycle.GetDeviceListVersion()))
            {
                Disconnect();
                nextProbe = Now();
                return;
            }
        }
        pending.active = false;
        NextBind();
        return;
    }
    Record * r = Find(f.uart_device_id);
    if (f.cluster || f.id)
        return;
    if (f.type == LU_MSG_DEVICE_ADD_NOTIFY)
    {
        uint8_t status = LU_STATUS_INVALID_ARGUMENT;
        if (f.payload_len == 13 && !f.endpoint && !f.binding_version && p.payload[10] <= 2)
            status = Add(f.uart_device_id, p.payload[0], U32(p.payload + 1), p.payload[9], U32(p.payload + 5), r);
        uint8_t extra[6] = {};
        if (!status)
        {
            Put(extra, r->endpoint, 2);
            Put(extra + 2, r->binding, 4);
        }
        Reply(p, LU_MSG_DEVICE_ADD_RESPONSE, status, extra, sizeof(extra));
        NextBind();
        return;
    }
    if (f.type == LU_MSG_DEVICE_REMOVE_NOTIFY)
    {
        uint8_t status = LU_STATUS_OK;
        if (f.payload_len != 8 || p.payload[0] > 2 || p.payload[5])
            status = LU_STATUS_INVALID_ARGUMENT;
        else if (r)
        {
            bool zero = !f.endpoint && !f.binding_version;
            if ((zero && r->bound && !r->listedRemoved) ||
                (!zero && (f.endpoint != r->endpoint || f.binding_version != r->binding)))
                status = LU_STATUS_BINDING_MISMATCH;
            else
            {
                r->persistentState = 3;
                if (Save() != CHIP_NO_ERROR)
                    status = LU_STATUS_INTERNAL_ERROR;
                else
                {
                    Reachable(*r, false);
                    if (pending.active && pending.packet.frame.uart_device_id == r->id)
                    {
                        FinishCommand(Status::Failure);
                        pending.active = false;
                    }
                    if (RemoveDynamicBridgeDeviceLocked(r->endpoint) != CHIP_NO_ERROR)
                        status = LU_STATUS_INTERNAL_ERROR;
                    else
                    {
                        *r = {};
                        if (Save() != CHIP_NO_ERROR)
                            status = LU_STATUS_INTERNAL_ERROR;
                    }
                }
            }
        }
        Reply(p, LU_MSG_DEVICE_REMOVE_RESPONSE, status);
        return;
    }
    if (f.type == LU_MSG_DEVICE_ONLINE_NOTIFY || f.type == LU_MSG_DEVICE_OFFLINE_NOTIFY)
    {
        bool online    = f.type == LU_MSG_DEVICE_ONLINE_NOTIFY;
        uint8_t status = CheckBinding(f, r);
        if (!status &&
            (f.payload_len != 8 || p.payload[0] > (online ? 2 : 3) || !ValidStateFlags(p.payload[5], U32(p.payload + 1)) ||
             (p.payload[5] & 3) != (online ? 3 : 1)))
            status = LU_STATUS_INVALID_ARGUMENT;
        if (!status && r->version && static_cast<int32_t>(U32(p.payload + 1) - r->version) < 0)
            status = LU_STATUS_BAD_STATE_VERSION;
        if (!status)
        {
            uint32_t eventVersion = U32(p.payload + 1);
            if (eventVersion != r->version)
            {
                r->version       = eventVersion;
                r->versionFields = 0;
            }
            r->online = online;
            r->synced = false;
            Reachable(*r, false);
            if (pending.active && pending.packet.frame.uart_device_id == r->id)
            {
                FinishCommand(Status::Failure);
                pending.active = false;
            }
        }
        Reply(p, uint8_t(f.type + 1), status);
    }
}
bool SameRequest(const Packet & a, const Packet & b)
{
    const auto & x = a.frame;
    const auto & y = b.frame;
    return x.type == y.type && x.seq == y.seq && x.session_id == y.session_id && x.uart_device_id == y.uart_device_id &&
        x.endpoint == y.endpoint && x.binding_version == y.binding_version && x.cluster == y.cluster && x.id == y.id &&
        (x.flags & 0x1Fu) == (y.flags & 0x1Fu) && x.payload_len == y.payload_len &&
        std::memcmp(a.payload, b.payload, x.payload_len) == 0;
}
void Process(Packet & p)
{
    const auto & f    = p.frame;
    bool notification = f.type == LU_MSG_DEVICE_ADD_NOTIFY || f.type == LU_MSG_DEVICE_REMOVE_NOTIFY ||
        f.type == LU_MSG_DEVICE_ONLINE_NOTIFY || f.type == LU_MSG_DEVICE_OFFLINE_NOTIFY || f.type == LU_MSG_UP_STATE_SNAPSHOT;
    bool valid = f.payload_len <= kPayloadCapacity && lu_validate_frame_semantics(&f) == LU_OK;
    if (!notification)
    {
        if (valid)
            ProcessImpl(p);
        return;
    }
    if (!session || f.session_id != session)
        return;
    if (storageFailed)
    {
        Reply(p, uint8_t(f.type + 1), LU_STATUS_INTERNAL_ERROR);
        return;
    }
    if (!valid || f.cluster || f.id || !f.uart_device_id)
    {
        Reply(p, uint8_t(f.type + 1), LU_STATUS_INVALID_ARGUMENT);
        return;
    }
    if (f.type != LU_MSG_DEVICE_ADD_NOTIFY && f.type != LU_MSG_DEVICE_REMOVE_NOTIFY)
    {
        uint8_t status = CheckBinding(f, Find(f.uart_device_id));
        if (status && status != LU_STATUS_BUSY)
        {
            Reply(p, uint8_t(f.type + 1), status);
            return;
        }
    }
    if (f.flags & LU_FLAG_RETRANSMISSION)
    {
        for (auto & cached : responseCache)
        {
            const auto & q = cached.request.frame;
            if (!cached.valid || q.session_id != f.session_id || q.seq != f.seq || q.type != f.type ||
                q.uart_device_id != f.uart_device_id)
                continue;
            if (!SameRequest(p, cached.request))
                Reply(p, uint8_t(f.type + 1), LU_STATUS_INVALID_ARGUMENT);
            else if (Send(cached.response) != CHIP_NO_ERROR)
                ChipLogError(Zcl, "UART cached response write failed");
            return;
        }
        if (!haveBusyRequest || !SameRequest(p, busyRequest))
        {
            Reply(p, uint8_t(f.type + 1), LU_STATUS_BUSY);
            return;
        }
    }
    // New requests may reuse sequence after wrap; expire matching older results.
    for (auto & cached : responseCache)
        if (cached.valid && cached.request.frame.type == f.type && cached.request.frame.seq == f.seq &&
            cached.request.frame.uart_device_id == f.uart_device_id)
            cached.valid = false;
    if (lifecycle.GetPhase() == BridgeUartLifecycle::Phase::Listing)
    {
        busyRequest     = p;
        haveBusyRequest = true;
        Reply(p, uint8_t(f.type + 1), LU_STATUS_BUSY);
        return;
    }
    cacheResponse = true;
    ProcessImpl(p);
    cacheResponse = false;
}
void Drain(intptr_t)
{
    for (;;)
    {
        if (overflow.exchange(false))
        {
            Disconnect();
            ChipLogError(Zcl, "UART event queue overflow; resynchronizing");
        }
        Packet p;
        while (xQueueReceive(queueHandle, &p, 0) == pdPASS)
            Process(p);
        drainScheduled = false;
        if (!uxQueueMessagesWaiting(queueHandle) || drainScheduled.exchange(true))
            break;
    }
}
void Timer(System::Layer *, void *)
{
    // Recover a failed ScheduleWork even when no more UART frames arrive.
    if (!drainScheduled.exchange(true))
        Drain(0);
    uint32_t now = Now();
    if (pending.active)
    {
        uint8_t type     = pending.packet.frame.type;
        uint32_t timeout = type == LU_MSG_DEVICE_LIST_REQUEST ? 5000 : 500;
        if (uint32_t(now - pending.sent) >= timeout)
        {
            if (type == LU_MSG_HEARTBEAT)
            {
                pending.active = false;
                if (++heartbeatMisses >= 3)
                    Disconnect();
            }
            else if (pending.retries++ < 3)
            {
                pending.packet.frame.flags |= LU_FLAG_RETRANSMISSION;
                if (Send(pending.packet) != CHIP_NO_ERROR)
                    Disconnect();
                else
                    pending.sent = Now();
            }
            else
                Disconnect();
        }
    }
    if (!pending.active && !storageFailed)
    {
        if (!session)
        {
            if (static_cast<int32_t>(now - nextProbe) >= 0)
                Hello();
        }
        else if (lifecycle.GetPhase() != BridgeUartLifecycle::Phase::Listing && uint32_t(now - lastHeartbeat) >= 5000)
        {
            uint8_t bytes[9] = {};
            Put(bytes, now / 1000, 4);
            Put(bytes + 4, session, 4);
            lastHeartbeat = now;
            if (Start(LU_MSG_HEARTBEAT, nullptr, bytes, sizeof(bytes)) != CHIP_NO_ERROR)
                Disconnect();
        }
        else
            NextBind();
    }
    if (SystemLayer().StartTimer(System::Clock::Milliseconds32(50), Timer, nullptr) != CHIP_NO_ERROR)
    {
        Disconnect();
        ChipLogError(Zcl, "UART timer failed");
    }
}
} // namespace

CHIP_ERROR InitBridgeUartRuntime()
{
    assertChipStackLockedByCurrentThread();
    if (initialized)
        return CHIP_NO_ERROR;
    queueHandle = xQueueCreateStatic(40, sizeof(Packet), queueStorage, &queueControl);
    VerifyOrReturnError(queueHandle != nullptr, CHIP_ERROR_NO_MEMORY);
    ReturnErrorOnFailure(Restore());
    boot = Crypto::GetRandU32();
    if (!boot)
        boot = 1;
    lifecycle.Reset(boot);
    ReturnErrorOnFailure(SystemLayer().StartTimer(System::Clock::Milliseconds32(50), Timer, nullptr));
    initialized = true;
    Hello();
    return CHIP_NO_ERROR;
}
CHIP_ERROR BridgeUartPostFrame(const lu_frame_t & frame)
{
    VerifyOrReturnError(queueHandle != nullptr, CHIP_ERROR_INCORRECT_STATE);
    Packet p;
    p.frame         = frame;
    p.frame.payload = nullptr;
    size_t copySize = frame.payload_len < kPayloadCapacity ? frame.payload_len : kPayloadCapacity;
    if (copySize)
        std::memcpy(p.payload, frame.payload, copySize);
    if (xQueueSend(queueHandle, &p, 0) != pdPASS)
    {
        overflow = true;
        return CHIP_ERROR_NO_MEMORY;
    }
    if (!drainScheduled.exchange(true))
    {
        CHIP_ERROR err = PlatformMgr().ScheduleWork(Drain, 0);
        if (err != CHIP_NO_ERROR)
        {
            drainScheduled = false;
            overflow       = true;
            return err;
        }
    }
    return CHIP_NO_ERROR;
}
CHIP_ERROR BridgeUartRequest(uint16_t endpoint, uint32_t cluster, uint32_t id, const uint8_t * payload, uint16_t size, bool read)
{
    assertChipStackLockedByCurrentThread();
    auto * r = FindEndpoint(endpoint);
    VerifyOrReturnError(r && r->bound && r->online && r->synced && !storageFailed, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!pending.active && uint32_t(Now() - lastHeartbeat) < 5000, CHIP_ERROR_BUSY);
    // Do not send unsupported XY on the MCU wire.
    VerifyOrReturnError(!(cluster == 0x300 && (read ? id == 3 || id == 4 : id >= 7 && id <= 9)), CHIP_ERROR_INVALID_ARGUMENT);
    if (read)
    {
        bool supported =
            ((cluster == 6 || cluster == 8) && id == 0) || (cluster == 0x300 && (id == 7 || (r->type == 0x11 && id <= 1)));
        VerifyOrReturnError(supported, CHIP_ERROR_INVALID_ARGUMENT);
    }
    if (!read && cluster == 6 && (id == 1 || (id == 2 && r->values[0] == 0)))
    {
        auto * device = FindBridgeDevice(endpoint);
        if (device && device->GetOnLevel() != 0xFF)
        {
            uint8_t level = device->GetOnLevel();
            VerifyOrReturnError(level >= 1 && level <= 254, CHIP_ERROR_INVALID_ARGUMENT);
            uint8_t target[6] = { 0, level, 0, 0, 0, 0 };
            return Start(LU_MSG_DOWN_INVOKE_COMMAND, r, target, sizeof(target), 8, 4);
        }
    }
    return Start(read ? LU_MSG_DOWN_READ_ATTRIBUTE : LU_MSG_DOWN_INVOKE_COMMAND, r, payload, size, cluster, id);
}
void BridgeUartHoldCommand(app::CommandHandler & handler, const app::ConcreteCommandPath & path)
{
    assertChipStackLockedByCurrentThread();
    commandPath   = path;
    commandHandle = app::CommandHandler::Handle(&handler);
}
