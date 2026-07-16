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

#include "LightUartAttribute.h"

#include "BridgeAppInternal.h"
#include "BridgeDevice.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <cstring>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::DeviceLayer;

namespace {

constexpr size_t kMaxAttributeValueSize = sizeof(uint16_t);

struct PendingAttributeReport
{
    uint16_t sequence;
    EndpointId endpoint;
    ClusterId clusterId;
    AttributeId attributeId;
    uint16_t valueLen;
    uint8_t value[kMaxAttributeValueSize];
};

bool GetBool(const PendingAttributeReport & report, bool & value)
{
    VerifyOrReturnError(report.valueLen == 1 && report.value[0] <= 1, false);
    value = report.value[0] != 0;
    return true;
}

bool GetU8(const PendingAttributeReport & report, uint8_t & value)
{
    VerifyOrReturnError(report.valueLen == 1, false);
    value = report.value[0];
    return true;
}

bool GetU16(const PendingAttributeReport & report, uint16_t & value)
{
    VerifyOrReturnError(report.valueLen == 2, false);
    value = static_cast<uint16_t>(report.value[0] | (static_cast<uint16_t>(report.value[1]) << 8));
    return true;
}

CHIP_ERROR ApplyColorAttributeValue(const PendingAttributeReport & report, BridgeDevice & device)
{
    uint8_t u8Value   = 0;
    uint16_t u16Value = 0;

    switch (report.attributeId)
    {
    // Hue/Saturation reports select HS mode; the setter updates ColorMode and EnhancedColorMode.
    case ColorControl::Attributes::CurrentHue::Id:
        VerifyOrReturnError(device.HasHueSaturation() && GetU8(report, u8Value), CHIP_ERROR_INVALID_ARGUMENT);
        device.SetHue(u8Value);
        return CHIP_NO_ERROR;
    case ColorControl::Attributes::CurrentSaturation::Id:
        VerifyOrReturnError(device.HasHueSaturation() && GetU8(report, u8Value), CHIP_ERROR_INVALID_ARGUMENT);
        device.SetSaturation(u8Value);
        return CHIP_NO_ERROR;
    case ColorControl::Attributes::CurrentX::Id:
        VerifyOrReturnError(device.HasXY() && GetU16(report, u16Value), CHIP_ERROR_INVALID_ARGUMENT);
        device.SetCurrentX(u16Value);
        return CHIP_NO_ERROR;
    case ColorControl::Attributes::CurrentY::Id:
        VerifyOrReturnError(device.HasXY() && GetU16(report, u16Value), CHIP_ERROR_INVALID_ARGUMENT);
        device.SetCurrentY(u16Value);
        return CHIP_NO_ERROR;
    // Color temperature reports select Color Temperature mode.
    case ColorControl::Attributes::ColorTemperatureMireds::Id:
        VerifyOrReturnError(device.HasColorTemperature() && GetU16(report, u16Value), CHIP_ERROR_INVALID_ARGUMENT);
        device.SetColorTemperatureMireds(u16Value);
        return CHIP_NO_ERROR;
    default:
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
}

CHIP_ERROR ApplyAttributeValue(const PendingAttributeReport & report, BridgeDevice & device)
{
    bool boolValue  = false;
    uint8_t u8Value = 0;

    switch (report.clusterId)
    {
    case OnOff::Id:
        VerifyOrReturnError(report.attributeId == OnOff::Attributes::OnOff::Id && GetBool(report, boolValue),
                            CHIP_ERROR_INVALID_ARGUMENT);
        device.SetOnOff(boolValue);
        return CHIP_NO_ERROR;
    case LevelControl::Id:
        VerifyOrReturnError(device.HasLevel() && report.attributeId == LevelControl::Attributes::CurrentLevel::Id &&
                                GetU8(report, u8Value),
                            CHIP_ERROR_INVALID_ARGUMENT);
        device.SetLevel(u8Value);
        return CHIP_NO_ERROR;
    case ColorControl::Id:
        return ApplyColorAttributeValue(report, device);
    default:
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
}

void ApplyAttributeReport(intptr_t arg)
{
    PendingAttributeReport * report = reinterpret_cast<PendingAttributeReport *>(arg);
    VerifyOrReturn(report != nullptr);

    BridgeDevice * device = FindBridgeDevice(report->endpoint);
    if (device == nullptr)
    {
        ChipLogError(Zcl, "Rejected bridge UART report for unknown endpoint: seq=%u ep=%u", static_cast<unsigned>(report->sequence),
                     static_cast<unsigned>(report->endpoint));
        Platform::Delete(report);
        return;
    }

    CHIP_ERROR err = ApplyAttributeValue(*report, *device);
    if (err == CHIP_NO_ERROR)
    {
        ChipLogProgress(Zcl, "Applied bridge UART report: seq=%u ep=%u cluster=" ChipLogFormatMEI " attr=" ChipLogFormatMEI,
                        static_cast<unsigned>(report->sequence), static_cast<unsigned>(report->endpoint),
                        ChipLogValueMEI(report->clusterId), ChipLogValueMEI(report->attributeId));
    }
    else
    {
        ChipLogError(Zcl, "Rejected bridge UART report: seq=%u ep=%u cluster=" ChipLogFormatMEI " attr=" ChipLogFormatMEI " len=%u",
                     static_cast<unsigned>(report->sequence), static_cast<unsigned>(report->endpoint),
                     ChipLogValueMEI(report->clusterId), ChipLogValueMEI(report->attributeId),
                     static_cast<unsigned>(report->valueLen));
    }

    Platform::Delete(report);
}

} // namespace

CHIP_ERROR LightUartHandleAttributeReport(const lu_frame_t & frame)
{
    ChipLogProgress(
        Zcl, "Received bridge UART report: seq=%u ep=%u cluster=" ChipLogFormatMEI " attr=" ChipLogFormatMEI " flags=0x%02x len=%u",
        static_cast<unsigned>(frame.seq), static_cast<unsigned>(frame.endpoint), ChipLogValueMEI(frame.cluster),
        ChipLogValueMEI(frame.id), static_cast<unsigned>(frame.flags), static_cast<unsigned>(frame.payload_len));

    if ((frame.flags & LU_FLAG_ERROR) != 0)
    {
        ChipLogError(Zcl, "Rejected bridge UART error report: seq=%u ep=%u", static_cast<unsigned>(frame.seq),
                     static_cast<unsigned>(frame.endpoint));
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    if ((frame.flags & LU_FLAG_NULL_VALUE) != 0)
    {
        ChipLogError(Zcl, "Rejected unsupported bridge UART null report: seq=%u ep=%u", static_cast<unsigned>(frame.seq),
                     static_cast<unsigned>(frame.endpoint));
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    if (frame.payload_len > kMaxAttributeValueSize || (frame.payload == nullptr && frame.payload_len != 0))
    {
        ChipLogError(Zcl, "Rejected bridge UART report payload: seq=%u ep=%u len=%u", static_cast<unsigned>(frame.seq),
                     static_cast<unsigned>(frame.endpoint), static_cast<unsigned>(frame.payload_len));
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    PendingAttributeReport * report = Platform::New<PendingAttributeReport>();
    VerifyOrReturnError(report != nullptr, CHIP_ERROR_NO_MEMORY);

    report->sequence    = frame.seq;
    report->endpoint    = static_cast<EndpointId>(frame.endpoint);
    report->clusterId   = frame.cluster;
    report->attributeId = frame.id;
    report->valueLen    = frame.payload_len;
    if (frame.payload_len != 0)
    {
        std::memcpy(report->value, frame.payload, frame.payload_len);
    }

    CHIP_ERROR err = PlatformMgr().ScheduleWork(ApplyAttributeReport, reinterpret_cast<intptr_t>(report));
    if (err != CHIP_NO_ERROR)
    {
        Platform::Delete(report);
    }
    return err;
}
