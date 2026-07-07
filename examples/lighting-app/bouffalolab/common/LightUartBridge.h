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

#pragma once

#include "LightUartProtocol.h"

#include <lib/core/CHIPError.h>
#include <stdint.h>

CHIP_ERROR InitLightUartBridge();
CHIP_ERROR LightUartBridgeSendCommand(uint16_t endpoint, uint32_t clusterId, uint32_t commandId, const uint8_t * payload,
                                      uint16_t payloadLen);
CHIP_ERROR LightUartBridgeSendReadAttribute(uint16_t endpoint, uint32_t clusterId, uint32_t attributeId);
CHIP_ERROR LightUartBridgeProcessFrame(const uint8_t * frame, uint16_t frameLen);

extern "C" int16_t LightUartBridgeWrite(const uint8_t * data, uint16_t len);
extern "C" int16_t LightUartBridgeRead(uint8_t * data, uint16_t maxLen);

