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

#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>

#include <cstdint>

enum class DynamicBridgeDeviceType : uint8_t
{
    kOnOffLight,
    kDimmableLight,
    kColorTemperatureLight,
    kHueSaturationLight,
    kExtendedColorLight,
    kOnOffPlugInUnit,
};

struct DynamicBridgeDeviceParams
{
    DynamicBridgeDeviceType type;
    const char * name;
    const char * location;
    const char * uniqueId;
};

CHIP_ERROR InitBridgeApp();

// These APIs synchronously update Matter's dynamic endpoint table. The caller must hold the CHIP stack lock.
CHIP_ERROR AddDynamicBridgeDeviceLocked(const DynamicBridgeDeviceParams & params, chip::EndpointId & endpoint);
CHIP_ERROR RemoveDynamicBridgeDeviceLocked(chip::EndpointId endpoint);
