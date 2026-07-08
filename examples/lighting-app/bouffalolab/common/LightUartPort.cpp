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

#include "LightUartPort.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

#if CHIP_DEVICE_LAYER_TARGET_BFLB
extern "C" {
#include <bflb_clock.h>
#include <bflb_core.h>
#include <bflb_dma.h>
#include <bflb_gpio.h>
#include <bflb_uart.h>
#include <board.h>
#include <ring_buffer.h>
}
#else
#include <uart.h>
#endif

using namespace chip;

namespace {

#if CHIP_DEVICE_LAYER_TARGET_BFLB
#ifndef LIGHT_UART_DEVICE_NAME
#define LIGHT_UART_DEVICE_NAME "uart1"
#endif

#ifndef LIGHT_UART_BAUDRATE
#define LIGHT_UART_BAUDRATE 2000000
#endif

#ifndef LIGHT_UART_DMA_RX_DEVICE_NAME
#if defined(BL618DG)
#define LIGHT_UART_DMA_RX_DEVICE_NAME "dma1_ch0"
#else
#define LIGHT_UART_DMA_RX_DEVICE_NAME "dma0_ch2"
#endif
#endif

#if defined(BL616CL)
#ifndef LIGHT_UART_TX_PIN
#define LIGHT_UART_TX_PIN GPIO_PIN_6
#endif
#ifndef LIGHT_UART_RX_PIN
#define LIGHT_UART_RX_PIN GPIO_PIN_7
#endif
#ifndef LIGHT_UART_CTS_PIN
#define LIGHT_UART_CTS_PIN GPIO_PIN_8
#endif
#ifndef LIGHT_UART_RTS_PIN
#define LIGHT_UART_RTS_PIN GPIO_PIN_9
#endif
#endif

#ifndef LIGHT_UART_BOOT_PROBE
#define LIGHT_UART_BOOT_PROBE 0
#endif

constexpr size_t kLightUartRxDmaBufferSize  = 2048;
constexpr size_t kLightUartRxRingBufferSize = 2048;
constexpr size_t kLightUartRxDmaLliPoolSize = 20;

struct bflb_device_s * sLightUart;
struct bflb_device_s * sLightUartRxDma;
struct bflb_rx_cycle_dma sLightUartRxCycleDma;
struct bflb_dma_channel_lli_pool_s sLightUartRxDmaLliPool[kLightUartRxDmaLliPoolSize];
Ring_Buffer_Type sLightUartRxRing;
TaskHandle_t sLightUartRxTaskHandle;
bool sLightUartRxDmaReady;
volatile uint32_t sLightUartRxDroppedBytes;

uint8_t ATTR_NOCACHE_NOINIT_RAM_SECTION sLightUartRxDmaBuffer[kLightUartRxDmaBufferSize];
uint8_t sLightUartRxRingBuffer[kLightUartRxRingBufferSize];

void NotifyLightUartRxTaskFromIsr()
{
    if (sLightUartRxTaskHandle == nullptr)
    {
        return;
    }

    BaseType_t higherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(sLightUartRxTaskHandle, &higherPriorityTaskWoken);
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

void CopyLightUartRxDmaDataToRing(uint8_t * data, uint32_t len)
{
    uint32_t written = Ring_Buffer_Write(&sLightUartRxRing, data, len);
    if (written != len)
    {
        sLightUartRxDroppedBytes += len - written;
    }
}

void LightUartIrqHandler(int irq, void * arg)
{
    (void) irq;
    (void) arg;

    uint32_t intstatus = bflb_uart_get_intstatus(sLightUart);
    if ((intstatus & UART_INTSTS_RTO) != 0)
    {
        bflb_uart_int_clear(sLightUart, UART_INTCLR_RTO);
        bflb_rx_cycle_dma_process(&sLightUartRxCycleDma, false);
        NotifyLightUartRxTaskFromIsr();
    }
}

void LightUartRxDmaIrqHandler(void * arg)
{
    (void) arg;

    bflb_rx_cycle_dma_process(&sLightUartRxCycleDma, true);
    NotifyLightUartRxTaskFromIsr();
}

void InitLightUartGpio(struct bflb_device_s * uart)
{
#if defined(BL616CL)
    struct bflb_device_s * gpio = bflb_device_get_by_name("gpio");
    VerifyOrReturn(gpio != nullptr && uart != nullptr);

    bflb_gpio_uart_init(gpio, LIGHT_UART_TX_PIN, GPIO_UART_FUNC_UART0_TX + 4 * uart->idx);
    bflb_gpio_uart_init(gpio, LIGHT_UART_RX_PIN, GPIO_UART_FUNC_UART0_RX + 4 * uart->idx);
    bflb_gpio_uart_init(gpio, LIGHT_UART_CTS_PIN, GPIO_UART_FUNC_UART0_CTS + 4 * uart->idx);
    bflb_gpio_uart_init(gpio, LIGHT_UART_RTS_PIN, GPIO_UART_FUNC_UART0_RTS + 4 * uart->idx);
#else
    board_uartx_gpio_init();
#endif
}

void SendLightUartBootProbe()
{
#if LIGHT_UART_BOOT_PROBE
    static const uint8_t kBootProbe[] = { 0x55, 0x55, 0x55, 0x55, '\r', '\n', 'L', 'I', 'G', 'H', 'T', '_',
                                          'U',  'A',  'R',  'T',  '_',  'B',  'O', 'O', 'T', '\r', '\n' };
    int ret = bflb_uart_put_block(sLightUart, const_cast<uint8_t *>(kBootProbe), sizeof(kBootProbe));
    ChipLogProgress(Zcl, "Light UART boot probe wrote %d/%u bytes", ret == 0 ? static_cast<int>(sizeof(kBootProbe)) : ret,
                    static_cast<unsigned>(sizeof(kBootProbe)));
#endif
}

CHIP_ERROR InitLightUartRxDma()
{
    if (sLightUartRxDmaReady)
    {
        return CHIP_NO_ERROR;
    }

    sLightUartRxDma = bflb_device_get_by_name(LIGHT_UART_DMA_RX_DEVICE_NAME);
    VerifyOrReturnError(sLightUartRxDma != nullptr, CHIP_ERROR_INTERNAL);

    Ring_Buffer_Init(&sLightUartRxRing, sLightUartRxRingBuffer, sizeof(sLightUartRxRingBuffer), nullptr, nullptr);
    sLightUartRxDroppedBytes = 0;

#if !defined(BL618DG)
    PERIPHERAL_CLOCK_DMA0_ENABLE();
#endif

    struct bflb_dma_channel_config_s rxCfg = {};
    rxCfg.direction       = DMA_PERIPH_TO_MEMORY;
    rxCfg.src_req         = DMA_REQUEST_UART0_RX + 2 * sLightUart->idx;
    rxCfg.dst_req         = DMA_REQUEST_NONE;
    rxCfg.src_addr_inc    = DMA_ADDR_INCREMENT_DISABLE;
    rxCfg.dst_addr_inc    = DMA_ADDR_INCREMENT_ENABLE;
    rxCfg.src_burst_count = DMA_BURST_INCR1;
    rxCfg.dst_burst_count = DMA_BURST_INCR1;
    rxCfg.src_width       = DMA_DATA_WIDTH_8BIT;
    rxCfg.dst_width       = DMA_DATA_WIDTH_8BIT;

    bflb_dma_channel_init(sLightUartRxDma, &rxCfg);
    bflb_dma_channel_irq_attach(sLightUartRxDma, LightUartRxDmaIrqHandler, nullptr);

    int ret = bflb_rx_cycle_dma_init(&sLightUartRxCycleDma, sLightUartRxDma, sLightUartRxDmaLliPool,
                                     MATTER_ARRAY_SIZE(sLightUartRxDmaLliPool), sLightUart->reg_base + 0x8C,
                                     sLightUartRxDmaBuffer, sizeof(sLightUartRxDmaBuffer),
                                     CopyLightUartRxDmaDataToRing);
    VerifyOrReturnError(ret >= 0, CHIP_ERROR_INTERNAL);

    bflb_dma_channel_start(sLightUartRxDma);
    sLightUartRxDmaReady = true;

    ChipLogProgress(Zcl, "Light UART RX DMA initialized channel=%s dmaBuf=%u ringBuf=%u", LIGHT_UART_DMA_RX_DEVICE_NAME,
                    static_cast<unsigned>(sizeof(sLightUartRxDmaBuffer)), static_cast<unsigned>(sizeof(sLightUartRxRingBuffer)));
    return CHIP_NO_ERROR;
}
#endif

} // namespace

void LightUartPortSetRxTaskHandle(TaskHandle_t taskHandle)
{
#if CHIP_DEVICE_LAYER_TARGET_BFLB
    sLightUartRxTaskHandle = taskHandle;
#else
    (void) taskHandle;
#endif
}

void LightUartPortWaitForRxData(TickType_t idleDelay)
{
#if CHIP_DEVICE_LAYER_TARGET_BFLB
    ulTaskNotifyTake(pdTRUE, idleDelay);
#else
    vTaskDelay(idleDelay);
#endif
}

void LightUartPortLogRxDropsIfChanged()
{
#if CHIP_DEVICE_LAYER_TARGET_BFLB
    static uint32_t lastDropped = 0;
    uint32_t dropped = sLightUartRxDroppedBytes;
    if (dropped != lastDropped)
    {
        ChipLogError(Zcl, "Light UART RX ring dropped %u bytes total", static_cast<unsigned>(dropped));
        lastDropped = dropped;
    }
#endif
}

CHIP_ERROR InitLightUartPort()
{
#if CHIP_DEVICE_LAYER_TARGET_BFLB
    if (sLightUart != nullptr)
    {
        return CHIP_NO_ERROR;
    }

    sLightUart = bflb_device_get_by_name(LIGHT_UART_DEVICE_NAME);
    VerifyOrReturnError(sLightUart != nullptr, CHIP_ERROR_INTERNAL);

    InitLightUartGpio(sLightUart);

    struct bflb_uart_config_s cfg = {};
    cfg.baudrate           = LIGHT_UART_BAUDRATE;
    cfg.data_bits          = UART_DATA_BITS_8;
    cfg.stop_bits          = UART_STOP_BITS_1;
    cfg.parity             = UART_PARITY_NONE;
    cfg.flow_ctrl          = 0;
    cfg.tx_fifo_threshold  = 7;
    cfg.rx_fifo_threshold  = 0;
    cfg.bit_order          = UART_LSB_FIRST;
    bflb_uart_init(sLightUart, &cfg);
    bflb_uart_feature_control(sLightUart, UART_CMD_SET_RTO_VALUE, 0x80);
    bflb_uart_link_rxdma(sLightUart, true);
    ReturnErrorOnFailure(InitLightUartRxDma());
    bflb_irq_attach(sLightUart->irq_num, LightUartIrqHandler, nullptr);
    bflb_irq_enable(sLightUart->irq_num);
    SendLightUartBootProbe();

#if defined(BL616CL)
    ChipLogProgress(Zcl, "Light UART initialized on %s baud=%u pins tx=%u rx=%u cts=%u rts=%u", LIGHT_UART_DEVICE_NAME,
                    static_cast<unsigned>(LIGHT_UART_BAUDRATE), static_cast<unsigned>(LIGHT_UART_TX_PIN),
                    static_cast<unsigned>(LIGHT_UART_RX_PIN), static_cast<unsigned>(LIGHT_UART_CTS_PIN),
                    static_cast<unsigned>(LIGHT_UART_RTS_PIN));
#else
    ChipLogProgress(Zcl, "Light UART initialized on %s baud=%u", LIGHT_UART_DEVICE_NAME,
                    static_cast<unsigned>(LIGHT_UART_BAUDRATE));
#endif
#endif
    return CHIP_NO_ERROR;
}

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
    if (sLightUart == nullptr || !sLightUartRxDmaReady || data == nullptr || maxLen == 0)
    {
        return 0;
    }

    uint32_t available = Ring_Buffer_Get_Length(&sLightUartRxRing);
    if (available == 0)
    {
        return 0;
    }

    uint32_t readLen = available < maxLen ? available : maxLen;
    return static_cast<int16_t>(Ring_Buffer_Read(&sLightUartRxRing, data, readLen));
#else
    return uartRead(reinterpret_cast<char *>(data), maxLen);
#endif
}
