#include "LightUartBridge.h"
#include "BridgeUartRuntime.h"
#include "LightUartPort.h"
#include <FreeRTOS.h>
#include <cstring>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <task.h>

using namespace chip;
namespace {
constexpr size_t kBufferSize = LU_MIN_FRAME_SIZE + LU_MAX_PAYLOAD_SIZE;
TaskHandle_t rxTask;
StaticTask_t taskStorage;
StackType_t taskStack[4096 / sizeof(StackType_t)];
uint8_t buffer[kBufferSize];

// Only discard the failed candidate start; preserve the next complete SOF and
// its suffix. Never retain the same expired candidate indefinitely.
void Resync(size_t & length)
{
    for (size_t i = 1; i < length; ++i)
    {
        if (buffer[i] == LU_SOF0 && (i + 1 == length || buffer[i + 1] == LU_SOF1))
        {
            std::memmove(buffer, buffer + i, length - i);
            length -= i;
            return;
        }
    }
    length = 0;
}
void Consume(size_t & length, size_t count)
{
    std::memmove(buffer, buffer + count, length - count);
    length -= count;
}
void RxTask(void *)
{
    size_t length    = 0;
    TickType_t start = 0;
    for (;;)
    {
        if (length && xTaskGetTickCount() - start >= pdMS_TO_TICKS(100))
        {
            ChipLogError(Zcl, "UART half-frame timeout: %u bytes", unsigned(length));
            Resync(length);
            start = xTaskGetTickCount();
        }
        uint8_t byte;
        if (LightUartBridgeRead(&byte, 1) <= 0)
        {
            LightUartPortWaitForRxData(pdMS_TO_TICKS(10));
            continue;
        }
        LightUartPortLogRxDropsIfChanged();
        if (!length)
            start = xTaskGetTickCount();
        if (length == sizeof(buffer))
            Resync(length);
        buffer[length++] = byte;
        while (length)
        {
            if (buffer[0] != LU_SOF0 || (length >= 2 && buffer[1] != LU_SOF1))
            {
                Resync(length);
                start = xTaskGetTickCount();
                continue;
            }
            if (length < LU_HEADER_SIZE)
                break;
            size_t payload = size_t(buffer[29]) | size_t(buffer[30]) << 8;
            if (buffer[2] != LU_VERSION || payload > LU_MAX_PAYLOAD_SIZE)
            {
                Resync(length);
                start = xTaskGetTickCount();
                continue;
            }
            size_t total = LU_MIN_FRAME_SIZE + payload;
            if (length < total)
                break;
            lu_frame_t frame;
            lu_status_t status = lu_unpack_frame(buffer, total, &frame);
            if (status == LU_OK)
            {
                CHIP_ERROR err = BridgeUartPostFrame(frame);
                if (err != CHIP_NO_ERROR)
                    ChipLogError(Zcl, "UART event rejected: %" CHIP_ERROR_FORMAT, err.Format());
                Consume(length, total);
            }
            else if (status == LU_ERR_BAD_PAYLOAD || status == LU_ERR_RESERVED_BITS)
            {
                ChipLogError(Zcl, "UART semantic rejection: %u", unsigned(status));
                // Header/payload are available after successful CRC validation.
                // The Matter-thread dispatcher replies to invalid known requests.
                (void) BridgeUartPostFrame(frame);
                Consume(length, total);
            }
            else
            {
                ChipLogError(Zcl, "UART wire rejection: %u", unsigned(status));
                Resync(length);
            }
            start = xTaskGetTickCount();
        }
    }
}
} // namespace

CHIP_ERROR InitLightUartBridge()
{
    if (rxTask)
        return CHIP_NO_ERROR;
    ReturnErrorOnFailure(InitLightUartPort());
    rxTask = xTaskCreateStatic(RxTask, "light_uart", sizeof(taskStack) / sizeof(taskStack[0]), nullptr, 2, taskStack, &taskStorage);
    VerifyOrReturnError(rxTask, CHIP_ERROR_NO_MEMORY);
    LightUartPortSetRxTaskHandle(rxTask);
    return CHIP_NO_ERROR;
}
CHIP_ERROR LightUartBridgeProcessFrame(const uint8_t * bytes, uint16_t length)
{
    lu_frame_t frame;
    VerifyOrReturnError(lu_unpack_frame(bytes, length, &frame) == LU_OK, CHIP_ERROR_INVALID_ARGUMENT);
    return BridgeUartPostFrame(frame);
}
CHIP_ERROR LightUartBridgeSendCommand(uint16_t endpoint, uint32_t cluster, uint32_t id, const uint8_t * payload, uint16_t size)
{
    return BridgeUartRequest(endpoint, cluster, id, payload, size);
}
CHIP_ERROR LightUartBridgeSendReadAttribute(uint16_t endpoint, uint32_t cluster, uint32_t id)
{
    return BridgeUartRequest(endpoint, cluster, id, nullptr, 0, true);
}
