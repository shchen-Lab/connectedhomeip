#include "BridgeUartLifecycle.h"

#include <stddef.h>

namespace {

bool IsResponseFor(uint8_t requestType, uint8_t responseType)
{
    switch (requestType)
    {
    case 0x30:
        return responseType == 0x31;
    case 0x32:
        return responseType == 0x35;
    case 0x10:
        return responseType == 0x12;
    case 0x11:
        return responseType == 0x13;
    case 0x3A:
        return responseType == 0x3B;
    case 0x42:
        return responseType == 0x43;
    case 0x44:
        return responseType == 0x45;
    default:
        return false;
    }
}

} // namespace

void BridgeUartLifecycle::Reset(uint32_t bridgeBootId)
{
    mBridgeBootId      = bridgeBootId == 0 ? 1 : bridgeBootId;
    mSessionId         = 0;
    mDeviceListVersion = 0;
    mListTransactionId = 0;
    mListChecksum      = 0;
    mListStarted       = false;
    mListInvalid       = false;
    mListCountExpected = 0;
    mListCount         = 0;
    mPending           = {};
    mRetries           = 0;
    mHeartbeatMisses   = 0;
    mPhase             = Phase::Hello;
}

bool BridgeUartLifecycle::AcceptHelloResponse(uint16_t sequence, uint16_t expectedSequence, uint32_t sessionId, uint8_t status,
                                              uint32_t deviceListVersion)
{
    if (mPhase != Phase::Hello || sequence != expectedSequence || sessionId == 0 || status != 0)
    {
        return false;
    }

    mSessionId         = sessionId;
    mDeviceListVersion = deviceListVersion;
    mPending           = {};
    mRetries           = 0;
    mPhase             = Phase::Listing;
    return true;
}

bool BridgeUartLifecycle::BeginList(uint32_t transactionId, uint32_t deviceListVersion, uint16_t entryCount, uint16_t entrySize,
                                    uint32_t checksum)
{
    if (mPhase != Phase::Listing || transactionId == 0 || entrySize != 18 || entryCount > kMaxDevices)
    {
        return false;
    }

    mListTransactionId = transactionId;
    mDeviceListVersion = deviceListVersion;
    mListCountExpected = entryCount;
    mListChecksum      = checksum;
    mListCount         = 0;
    mComputedChecksum  = 0xFFFFFFFFu;
    mListStarted       = true;
    mListInvalid       = false;
    return true;
}

void BridgeUartLifecycle::BeginListRefresh()
{
    mListTransactionId = 0;
    mListChecksum      = 0;
    mListStarted       = false;
    mListInvalid       = false;
    mListCountExpected = 0;
    mListCount         = 0;
    mPending           = {};
    mRetries           = 0;
    mPhase             = Phase::Listing;
}

bool BridgeUartLifecycle::AddListEntry(uint32_t transactionId, const ListEntry & entry)
{
    if (mPhase != Phase::Listing || !mListStarted || mListInvalid || transactionId != mListTransactionId ||
        mListCount >= mListCountExpected || entry.deviceId == 0 ||
        (mListCount != 0 && entry.deviceId <= mEntries[mListCount - 1].deviceId) || (entry.stateFlags & 0xF8u) != 0 ||
        ((entry.stateFlags & 1u) == 0 && entry.stateFlags != 0) || ((entry.stateFlags & 4u) != 0 && entry.stateVersion == 0))
    {
        mListInvalid = true;
        return false;
    }
    for (uint16_t i = 0; i < mListCount; ++i)
    {
        if (mEntries[i].deviceId == entry.deviceId)
        {
            return false;
        }
    }
    // CRC covers the wire ENTRY payload, including transactionId, not struct padding.
    uint8_t bytes[18] = {};
    auto put          = [&](unsigned offset, uint32_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
        {
            bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
        }
    };
    put(0, transactionId, 4);
    put(4, entry.deviceId, 4);
    put(8, entry.deviceType, 1);
    put(9, entry.capabilityFlags, 4);
    put(13, entry.stateFlags, 1);
    put(14, entry.stateVersion, 4);
    for (uint8_t byte : bytes)
    {
        mComputedChecksum ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            mComputedChecksum = (mComputedChecksum >> 1) ^ ((mComputedChecksum & 1u) ? 0xEDB88320u : 0u);
        }
    }
    mEntries[mListCount++] = entry;
    return true;
}

bool BridgeUartLifecycle::FinishList(uint32_t transactionId, uint16_t receivedCount, uint32_t checksum, uint8_t status)
{
    if (mPhase != Phase::Listing || !mListStarted || mListInvalid || transactionId != mListTransactionId || status != 0 ||
        receivedCount != mListCountExpected || receivedCount != mListCount || checksum != mListChecksum ||
        checksum != (mComputedChecksum ^ 0xFFFFFFFFu))
    {
        mListCount = 0;
        return false;
    }
    mListStarted = false;
    mPending     = {};
    mPhase       = Phase::Binding;
    return true;
}

bool BridgeUartLifecycle::BeginRequest(uint8_t messageType, uint16_t sequence, uint32_t nowMs)
{
    if (mPending.active || sequence == 0)
    {
        return false;
    }
    mPending = { true, messageType, sequence, nowMs };
    mRetries = 0;
    return true;
}

bool BridgeUartLifecycle::CompleteResponse(uint8_t messageType, uint16_t sequence, uint8_t status)
{
    if (!mPending.active || !IsResponseFor(mPending.type, messageType) || mPending.sequence != sequence)
    {
        return false;
    }
    mPending = {};
    mRetries = 0;
    if (mPhase == Phase::Offline && status == 0)
    {
        mPhase = Phase::Hello;
    }
    return true;
}

BridgeUartLifecycle::PollResult BridgeUartLifecycle::Poll(uint32_t nowMs)
{
    if (!mPending.active || static_cast<uint32_t>(nowMs - mPending.sentAtMs) < kResponseTimeoutMs)
    {
        return PollResult::None;
    }
    if (mRetries < kMaxRetries)
    {
        ++mRetries;
        mPending.sentAtMs = nowMs;
        return PollResult::Retry;
    }
    mPending = {};
    mPhase   = Phase::Offline;
    return PollResult::Offline;
}

void BridgeUartLifecycle::HeartbeatResponse(uint32_t nowMs)
{
    (void) nowMs;
    mHeartbeatMisses = 0;
    if (mPhase == Phase::Offline)
    {
        mPhase = Phase::Hello;
    }
}
