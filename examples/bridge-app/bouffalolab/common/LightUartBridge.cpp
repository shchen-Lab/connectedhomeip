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

#include "LightUartAttribute.h"
#include "LightUartPort.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

#include <FreeRTOS.h>
#include <task.h>

using namespace chip;

namespace {

constexpr uint16_t kLightUartRxTaskStackSize   = 4096;
constexpr UBaseType_t kLightUartRxTaskPriority = 2;
constexpr TickType_t kLightUartRxIdleDelay     = pdMS_TO_TICKS(10);
constexpr size_t kLightUartRxFrameBufferSize   = LU_MIN_FRAME_SIZE + LU_MAX_PAYLOAD_SIZE;
constexpr size_t kUartLogBytesMax              = 96;

TaskHandle_t sLightUartRxTaskHandle;
StackType_t sLightUartRxTaskStack[kLightUartRxTaskStackSize / sizeof(StackType_t)];
StaticTask_t sLightUartRxTaskStruct;
uint8_t sLightUartRxFrameBuffer[kLightUartRxFrameBufferSize];
uint16_t sNextSeq = 1;

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
        if (LightUartHandleAttributeReport(frame) != CHIP_NO_ERROR)
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

void LightUartRxTask(void *)
{
    size_t len       = 0;
    uint8_t * buffer = sLightUartRxFrameBuffer;

    while (true)
    {
        uint8_t byte = 0;
        int16_t read = LightUartBridgeRead(&byte, 1);
        if (read <= 0)
        {
            LightUartPortWaitForRxData(kLightUartRxIdleDelay);
            continue;
        }

        LightUartPortLogRxDropsIfChanged();

        if (len >= sizeof(sLightUartRxFrameBuffer))
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

CHIP_ERROR InitLightUartBridge()
{
    VerifyOrReturnError(sLightUartRxTaskHandle == nullptr, CHIP_NO_ERROR);

    ReturnErrorOnFailure(InitLightUartPort());

    sLightUartRxTaskHandle = xTaskCreateStatic(LightUartRxTask, "light_uart", MATTER_ARRAY_SIZE(sLightUartRxTaskStack), nullptr,
                                               kLightUartRxTaskPriority, sLightUartRxTaskStack, &sLightUartRxTaskStruct);
    VerifyOrReturnError(sLightUartRxTaskHandle != nullptr, CHIP_ERROR_NO_MEMORY);
    LightUartPortSetRxTaskHandle(sLightUartRxTaskHandle);

    return CHIP_NO_ERROR;
}

CHIP_ERROR LightUartBridgeSendCommand(uint16_t endpoint, uint32_t clusterId, uint32_t commandId, const uint8_t * payload,
                                      uint16_t payloadLen)
{
    uint8_t frame[LU_MIN_FRAME_SIZE + LU_MAX_PAYLOAD_SIZE];
    size_t frameLen = 0;

    VerifyOrReturnError(payloadLen <= LU_MAX_PAYLOAD_SIZE, CHIP_ERROR_INVALID_ARGUMENT);
    uint16_t seq = NextSeq();
    ReturnErrorOnFailure(StatusToChipError(lu_pack_frame(LU_MSG_DOWN_INVOKE_COMMAND, 0, seq, endpoint, clusterId, commandId,
                                                         payload, payloadLen, frame, sizeof(frame), &frameLen)));

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
    ReturnErrorOnFailure(StatusToChipError(lu_pack_frame(LU_MSG_DOWN_READ_ATTRIBUTE, 0, seq, endpoint, clusterId, attributeId,
                                                         nullptr, 0, frame, sizeof(frame), &frameLen)));

    ChipLogProgress(
        Zcl, "UART TX read-attr seq=%u ep=%u cluster=" ChipLogFormatMEI " attr=" ChipLogFormatMEI " frameLen=%u crc=0x%04x",
        static_cast<unsigned>(seq), static_cast<unsigned>(endpoint), ChipLogValueMEI(clusterId), ChipLogValueMEI(attributeId),
        static_cast<unsigned>(frameLen), static_cast<unsigned>(GetFrameCrcForLog(frame, 0)));
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
