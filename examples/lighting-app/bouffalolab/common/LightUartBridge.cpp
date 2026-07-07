/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "LightUartBridge.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <clusters/ColorControl/ClusterId.h>
#include <clusters/Identify/ClusterId.h>
#include <clusters/LevelControl/ClusterId.h>
#include <clusters/OnOff/ClusterId.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <FreeRTOS.h>
#include <task.h>

#include <cstring>

#if CHIP_DEVICE_LAYER_TARGET_BFLB
extern "C" {
#include <bflb_core.h>
#include <bflb_uart.h>
#include <board.h>
}
#else
#include <uart.h>
#endif

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::DeviceLayer;

namespace {

constexpr uint16_t kRxStackSize    = 2048;
constexpr UBaseType_t kRxPriority  = 2;
constexpr TickType_t kRxIdleDelay  = pdMS_TO_TICKS(10);

TaskHandle_t sRxTaskHandle;
uint16_t sNextSeq = 1;
#if CHIP_DEVICE_LAYER_TARGET_BFLB
struct bflb_device_s * sLightUart;
#endif
StackType_t sRxStack[kRxStackSize / sizeof(StackType_t)];
StaticTask_t sRxTaskStruct;

struct PendingAttributeReport
{
    uint16_t endpoint;
    uint32_t clusterId;
    uint32_t attributeId;
    uint8_t valueType;
    uint8_t valueFlags;
    uint16_t valueLen;
    uint8_t value[4];
};

uint16_t NextSeq()
{
    uint16_t seq = sNextSeq++;
    if (sNextSeq == 0)
    {
        sNextSeq = 1;
    }
    return seq;
}

CHIP_ERROR StatusToChipError(lu_status_t status)
{
    return status == LU_OK ? CHIP_NO_ERROR : CHIP_ERROR_INTERNAL;
}

constexpr size_t kUartLogBytesMax = 96;

void LogHexBuffer(const char * label, const uint8_t * data, size_t len)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    char text[kUartLogBytesMax * 3 + 1];
    size_t dumpLen = len < kUartLogBytesMax ? len : kUartLogBytesMax;
    size_t pos     = 0;

    if (data == nullptr && len != 0)
    {
        ChipLogError(Zcl, "%s len=%u data=null", label, static_cast<unsigned>(len));
        return;
    }

    for (size_t i = 0; i < dumpLen; ++i)
    {
        if (i != 0)
        {
            text[pos++] = ' ';
        }
        text[pos++] = kHex[(data[i] >> 4) & 0x0F];
        text[pos++] = kHex[data[i] & 0x0F];
    }
    text[pos] = '\0';

    ChipLogProgress(Zcl, "%s len=%u%s %s", label, static_cast<unsigned>(len), len > dumpLen ? " truncated" : "", text);
}

uint16_t GetFrameCrcForLog(const uint8_t * frame, uint16_t payloadLen)
{
    return static_cast<uint16_t>(frame[19u + payloadLen] | (static_cast<uint16_t>(frame[20u + payloadLen]) << 8));
}

#if CHIP_DEVICE_LAYER_TARGET_BFLB
#ifndef LIGHT_UART_DEVICE_NAME
#define LIGHT_UART_DEVICE_NAME DEFAULT_TEST_UART
#endif

#ifndef LIGHT_UART_BAUDRATE
#define LIGHT_UART_BAUDRATE 2000000
#endif

CHIP_ERROR InitLightUartPort()
{
    if (sLightUart != nullptr)
    {
        return CHIP_NO_ERROR;
    }

    board_uartx_gpio_init();

    sLightUart = bflb_device_get_by_name(LIGHT_UART_DEVICE_NAME);
    VerifyOrReturnError(sLightUart != nullptr, CHIP_ERROR_INTERNAL);

    struct bflb_uart_config_s cfg = {};
    cfg.baudrate           = LIGHT_UART_BAUDRATE;
    cfg.data_bits          = UART_DATA_BITS_8;
    cfg.stop_bits          = UART_STOP_BITS_1;
    cfg.parity             = UART_PARITY_NONE;
    cfg.flow_ctrl          = 0;
    cfg.tx_fifo_threshold  = 7;
    cfg.rx_fifo_threshold  = 7;
    cfg.bit_order          = UART_LSB_FIRST;
    bflb_uart_init(sLightUart, &cfg);

    ChipLogProgress(Zcl, "Light UART initialized on %s baud=%u", LIGHT_UART_DEVICE_NAME,
                    static_cast<unsigned>(LIGHT_UART_BAUDRATE));
    return CHIP_NO_ERROR;
}
#endif

bool AttrIsNull(const PendingAttributeReport & report)
{
    return (report.valueFlags & LU_VALUE_FLAG_NULL) != 0;
}

bool GetBool(const PendingAttributeReport & report, bool & value)
{
    VerifyOrReturnError(!AttrIsNull(report), false);
    VerifyOrReturnError(report.valueType == LU_VT_BOOL && report.valueLen == 1, false);
    value = report.value[0] != 0;
    return true;
}

bool GetU8(const PendingAttributeReport & report, uint8_t expectedType, uint8_t & value)
{
    VerifyOrReturnError(!AttrIsNull(report), false);
    VerifyOrReturnError(report.valueType == expectedType && report.valueLen == 1, false);
    value = report.value[0];
    return true;
}

bool GetU16(const PendingAttributeReport & report, uint8_t expectedType, uint16_t & value)
{
    VerifyOrReturnError(!AttrIsNull(report), false);
    VerifyOrReturnError(report.valueType == expectedType && report.valueLen == 2, false);
    value = static_cast<uint16_t>(report.value[0] | (static_cast<uint16_t>(report.value[1]) << 8));
    return true;
}

Protocols::InteractionModel::Status ApplyAttributeValue(const PendingAttributeReport & report, EndpointId endpoint)
{
    bool boolValue    = false;
    uint8_t u8Value   = 0;
    uint16_t u16Value = 0;

    switch (report.clusterId)
    {
    case OnOff::Id:
        if (report.attributeId == OnOff::Attributes::OnOff::Id && GetBool(report, boolValue))
        {
            return OnOff::Attributes::OnOff::Set(endpoint, boolValue);
        }
        break;
    case LevelControl::Id:
        if (report.attributeId == LevelControl::Attributes::CurrentLevel::Id)
        {
            DataModel::Nullable<uint8_t> level;
            if (AttrIsNull(report))
            {
                level.SetNull();
                return LevelControl::Attributes::CurrentLevel::Set(endpoint, level);
            }
            if (GetU8(report, LU_VT_U8, u8Value))
            {
                level.SetNonNull(u8Value);
                return LevelControl::Attributes::CurrentLevel::Set(endpoint, level);
            }
        }
        break;
    case ColorControl::Id:
        if (report.attributeId == ColorControl::Attributes::CurrentHue::Id && GetU8(report, LU_VT_U8, u8Value))
        {
            return ColorControl::Attributes::CurrentHue::Set(endpoint, u8Value);
        }
        if (report.attributeId == ColorControl::Attributes::CurrentSaturation::Id && GetU8(report, LU_VT_U8, u8Value))
        {
            return ColorControl::Attributes::CurrentSaturation::Set(endpoint, u8Value);
        }
        if (report.attributeId == ColorControl::Attributes::CurrentX::Id && GetU16(report, LU_VT_U16, u16Value))
        {
            return ColorControl::Attributes::CurrentX::Set(endpoint, u16Value);
        }
        if (report.attributeId == ColorControl::Attributes::CurrentY::Id && GetU16(report, LU_VT_U16, u16Value))
        {
            return ColorControl::Attributes::CurrentY::Set(endpoint, u16Value);
        }
        if (report.attributeId == ColorControl::Attributes::ColorTemperatureMireds::Id &&
            GetU16(report, LU_VT_U16, u16Value))
        {
            return ColorControl::Attributes::ColorTemperatureMireds::Set(endpoint, u16Value);
        }
        if (report.attributeId == ColorControl::Attributes::ColorMode::Id && GetU8(report, LU_VT_ENUM8, u8Value))
        {
            return ColorControl::Attributes::ColorMode::Set(endpoint, static_cast<ColorControl::ColorModeEnum>(u8Value));
        }
        if (report.attributeId == ColorControl::Attributes::EnhancedCurrentHue::Id && GetU16(report, LU_VT_U16, u16Value))
        {
            return ColorControl::Attributes::EnhancedCurrentHue::Set(endpoint, u16Value);
        }
        if (report.attributeId == ColorControl::Attributes::EnhancedColorMode::Id && GetU8(report, LU_VT_ENUM8, u8Value))
        {
            return ColorControl::Attributes::EnhancedColorMode::Set(endpoint,
                                                                    static_cast<ColorControl::EnhancedColorModeEnum>(u8Value));
        }
        if (report.attributeId == ColorControl::Attributes::ColorLoopActive::Id && GetU8(report, LU_VT_U8, u8Value))
        {
            return ColorControl::Attributes::ColorLoopActive::Set(endpoint, u8Value);
        }
        if (report.attributeId == ColorControl::Attributes::ColorLoopDirection::Id && GetU8(report, LU_VT_U8, u8Value))
        {
            return ColorControl::Attributes::ColorLoopDirection::Set(endpoint, u8Value);
        }
        break;
    default:
        break;
    }

    return Protocols::InteractionModel::Status::UnsupportedAttribute;
}

void ApplyAttributeReport(intptr_t arg)
{
    PendingAttributeReport * report = reinterpret_cast<PendingAttributeReport *>(arg);
    VerifyOrReturn(report != nullptr);

    EndpointId endpoint = static_cast<EndpointId>(report->endpoint);
    uint16_t u16Value   = 0;
    bool statusLogged    = false;
    Protocols::InteractionModel::Status status = Protocols::InteractionModel::Status::UnsupportedAttribute;

    if (report->clusterId == Identify::Id && report->attributeId == Identify::Attributes::IdentifyTime::Id &&
        GetU16(*report, LU_VT_U16, u16Value))
    {
        ChipLogError(Zcl, "UART IdentifyTime report ignored ep %u value %u: no Matter attribute setter",
                     static_cast<unsigned>(endpoint), static_cast<unsigned>(u16Value));
        statusLogged = true;
    }
    else
    {
        status = ApplyAttributeValue(*report, endpoint);
    }

    if (!statusLogged && status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(Zcl,
                     "UART attribute report not applied ep %u cluster " ChipLogFormatMEI
                     " attr " ChipLogFormatMEI " type 0x%02x flags 0x%02x len %u status 0x%02x",
                     static_cast<unsigned>(endpoint), ChipLogValueMEI(report->clusterId), ChipLogValueMEI(report->attributeId),
                     static_cast<unsigned>(report->valueType), static_cast<unsigned>(report->valueFlags),
                     static_cast<unsigned>(report->valueLen), static_cast<unsigned>(status));
    }

    chip::Platform::MemoryFree(report);
}

CHIP_ERROR HandleAttributeReport(const lu_frame_t & frame)
{
    lu_attr_value_t attr;
    ReturnErrorOnFailure(StatusToChipError(lu_unpack_attr(frame.payload, frame.payload_len, &attr)));

    if (attr.value_len > sizeof(PendingAttributeReport::value))
    {
        ChipLogError(Zcl, "UART attribute value too large: %u", static_cast<unsigned>(attr.value_len));
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    PendingAttributeReport * report =
        static_cast<PendingAttributeReport *>(chip::Platform::MemoryAlloc(sizeof(PendingAttributeReport)));
    VerifyOrReturnError(report != nullptr, CHIP_ERROR_NO_MEMORY);

    report->endpoint    = frame.endpoint;
    report->clusterId   = frame.cluster;
    report->attributeId = frame.id;
    report->valueType   = attr.value_type;
    report->valueFlags  = attr.value_flags;
    report->valueLen    = attr.value_len;
    memset(report->value, 0, sizeof(report->value));
    if (attr.value_len != 0)
    {
        memcpy(report->value, attr.value, attr.value_len);
    }

    CHIP_ERROR err = PlatformMgr().ScheduleWork(ApplyAttributeReport, reinterpret_cast<intptr_t>(report));
    if (err != CHIP_NO_ERROR)
    {
        chip::Platform::MemoryFree(report);
    }
    return err;
}

CHIP_ERROR HandleFrame(const uint8_t * frameBytes, size_t frameLen)
{
    lu_frame_t frame;
    lu_status_t status = lu_unpack_frame(frameBytes, frameLen, &frame);
    if (status != LU_OK)
    {
        ChipLogError(Zcl, "UART light frame parse failed: %d", static_cast<int>(status));
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    switch (frame.type)
    {
    case LU_MSG_UP_ATTRIBUTE_REPORT:
        if (HandleAttributeReport(frame) != CHIP_NO_ERROR)
        {
            ChipLogError(Zcl, "UART light attribute report rejected");
            return CHIP_ERROR_INTERNAL;
        }
        break;
    default:
        ChipLogError(Zcl, "Unsupported UART light frame type 0x%02x", frame.type);
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    return CHIP_NO_ERROR;
}

void ResetRxBufferKeepingTrailingSof(uint8_t * buffer, size_t & len)
{
    while (len > 0u)
    {
        --len;
        if (buffer[len] == LU_SOF0)
        {
            buffer[0] = LU_SOF0;
            len       = 1u;
            return;
        }
    }
}

void StoreRxByte(uint8_t * buffer, size_t & len, uint8_t byte)
{
    if (len == 0 && byte != LU_SOF0)
    {
        return;
    }
    if (len == 1 && byte != LU_SOF1)
    {
        len = (byte == LU_SOF0) ? 1u : 0u;
        return;
    }

    buffer[len++] = byte;
}

void RxTask(void *)
{
    uint8_t buffer[LU_MIN_FRAME_SIZE + LU_MAX_PAYLOAD_SIZE];
    size_t len = 0;

    while (true)
    {
        uint8_t byte = 0;
        int16_t read = LightUartBridgeRead(&byte, 1);
        if (read <= 0)
        {
            vTaskDelay(kRxIdleDelay);
            continue;
        }

        if (len >= sizeof(buffer))
        {
            len = 0;
        }

        StoreRxByte(buffer, len, byte);

        if (len >= LU_HEADER_SIZE)
        {
            uint16_t payloadLen = static_cast<uint16_t>(buffer[17] | (static_cast<uint16_t>(buffer[18]) << 8));
            size_t frameLen     = LU_MIN_FRAME_SIZE + payloadLen;
            if (payloadLen > LU_MAX_PAYLOAD_SIZE)
            {
                ChipLogError(Zcl, "UART light frame payload too large: %u", static_cast<unsigned>(payloadLen));
                ResetRxBufferKeepingTrailingSof(buffer, len);
                continue;
            }
            if (len == frameLen)
            {
                if (HandleFrame(buffer, frameLen) == CHIP_NO_ERROR)
                {
                    len = 0;
                }
                else
                {
                    ResetRxBufferKeepingTrailingSof(buffer, len);
                }
            }
        }
    }
}

} // namespace

extern "C" int16_t __attribute__((weak)) LightUartBridgeWrite(const uint8_t * data, uint16_t len)
{
#if CHIP_DEVICE_LAYER_TARGET_BFLB
    if (sLightUart == nullptr)
    {
        return -1;
    }
    int ret = bflb_uart_put_block(sLightUart, const_cast<uint8_t *>(data), len);
    return ret == 0 ? static_cast<int16_t>(len) : -1;
#else
    return uartWrite(reinterpret_cast<const char *>(data), len);
#endif
}

extern "C" int16_t __attribute__((weak)) LightUartBridgeRead(uint8_t * data, uint16_t maxLen)
{
#if CHIP_DEVICE_LAYER_TARGET_BFLB
    if (sLightUart == nullptr || !bflb_uart_rxavailable(sLightUart))
    {
        return 0;
    }
    return static_cast<int16_t>(bflb_uart_get(sLightUart, data, maxLen));
#else
    return uartRead(reinterpret_cast<char *>(data), maxLen);
#endif
}

CHIP_ERROR InitLightUartBridge()
{
    VerifyOrReturnError(sRxTaskHandle == nullptr, CHIP_NO_ERROR);

#if CHIP_DEVICE_LAYER_TARGET_BFLB
    ReturnErrorOnFailure(InitLightUartPort());
#endif

    sRxTaskHandle = xTaskCreateStatic(RxTask, "light_uart", MATTER_ARRAY_SIZE(sRxStack), nullptr, kRxPriority, sRxStack, &sRxTaskStruct);
    VerifyOrReturnError(sRxTaskHandle != nullptr, CHIP_ERROR_NO_MEMORY);

    return CHIP_NO_ERROR;
}

CHIP_ERROR LightUartBridgeSendCommand(uint16_t endpoint, uint32_t clusterId, uint32_t commandId, const uint8_t * payload,
                                      uint16_t payloadLen)
{
    uint8_t frame[LU_MIN_FRAME_SIZE + LU_MAX_PAYLOAD_SIZE];
    size_t frameLen = 0;

    VerifyOrReturnError(payloadLen <= LU_MAX_PAYLOAD_SIZE, CHIP_ERROR_INVALID_ARGUMENT);
    uint16_t seq = NextSeq();
    ReturnErrorOnFailure(StatusToChipError(lu_pack_frame(LU_MSG_DOWN_INVOKE_COMMAND, 0, seq, endpoint, clusterId,
                                                         commandId, payload, payloadLen, frame, sizeof(frame), &frameLen)));

    ChipLogProgress(Zcl,
                    "UART TX command seq=%u ep=%u cluster=" ChipLogFormatMEI " cmd=" ChipLogFormatMEI
                    " payloadLen=%u frameLen=%u crc=0x%04x",
                    static_cast<unsigned>(seq), static_cast<unsigned>(endpoint), ChipLogValueMEI(clusterId),
                    ChipLogValueMEI(commandId), static_cast<unsigned>(payloadLen), static_cast<unsigned>(frameLen),
                    static_cast<unsigned>(GetFrameCrcForLog(frame, payloadLen)));
    LogHexBuffer("UART TX command payload", payload, payloadLen);
    LogHexBuffer("UART TX command frame", frame, frameLen);

    int16_t written = LightUartBridgeWrite(frame, static_cast<uint16_t>(frameLen));
    VerifyOrReturnError(written == static_cast<int16_t>(frameLen), CHIP_ERROR_WRITE_FAILED);
    return CHIP_NO_ERROR;
}

CHIP_ERROR LightUartBridgeSendReadAttribute(uint16_t endpoint, uint32_t clusterId, uint32_t attributeId)
{
    uint8_t frame[LU_MIN_FRAME_SIZE];
    size_t frameLen = 0;

    uint16_t seq = NextSeq();
    ReturnErrorOnFailure(StatusToChipError(lu_pack_frame(LU_MSG_DOWN_READ_ATTRIBUTE, 0, seq, endpoint, clusterId,
                                                         attributeId, nullptr, 0, frame, sizeof(frame), &frameLen)));

    ChipLogProgress(Zcl,
                    "UART TX read-attr seq=%u ep=%u cluster=" ChipLogFormatMEI " attr=" ChipLogFormatMEI
                    " frameLen=%u crc=0x%04x",
                    static_cast<unsigned>(seq), static_cast<unsigned>(endpoint), ChipLogValueMEI(clusterId),
                    ChipLogValueMEI(attributeId), static_cast<unsigned>(frameLen),
                    static_cast<unsigned>(GetFrameCrcForLog(frame, 0)));
    LogHexBuffer("UART TX read-attr frame", frame, frameLen);

    int16_t written = LightUartBridgeWrite(frame, static_cast<uint16_t>(frameLen));
    VerifyOrReturnError(written == static_cast<int16_t>(frameLen), CHIP_ERROR_WRITE_FAILED);
    return CHIP_NO_ERROR;
}

CHIP_ERROR LightUartBridgeProcessFrame(const uint8_t * frame, uint16_t frameLen)
{
    VerifyOrReturnError(frame != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    return HandleFrame(frame, frameLen);
}

