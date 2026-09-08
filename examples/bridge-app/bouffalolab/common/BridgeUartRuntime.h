#pragma once

#include "LightUartProtocol.h"
#include <app/CommandHandler.h>
#include <lib/core/CHIPError.h>

// All entry points except PostFrame run on the Matter thread/under its stack lock.
CHIP_ERROR InitBridgeUartRuntime();
CHIP_ERROR BridgeUartPostFrame(const lu_frame_t & frame);
CHIP_ERROR BridgeUartRequest(uint16_t endpoint, uint32_t cluster, uint32_t id, const uint8_t * payload, uint16_t size,
                             bool read = false);
void BridgeUartHoldCommand(chip::app::CommandHandler & handler, const chip::app::ConcreteCommandPath & path);
