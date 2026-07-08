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

#include <app-common/zap-generated/attributes/Accessors.h>
#include <clusters/ColorControl/ClusterId.h>
#include <clusters/Identify/ClusterId.h>
#include <clusters/LevelControl/ClusterId.h>
#include <clusters/OnOff/ClusterId.h>
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

constexpr size_t kUartLogBytesMax = 96;

struct PendingAttributeReport
{
    uint16_t endpoint;
    uint32_t clusterId;
    uint32_t attributeId;
    bool isNull;
    uint16_t valueLen;
    uint8_t value[4];
};

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

bool AttrIsNull(const PendingAttributeReport & report)
{
    return report.isNull;
}

bool GetBool(const PendingAttributeReport & report, bool & value)
{
    VerifyOrReturnError(!AttrIsNull(report), false);
    VerifyOrReturnError(report.valueLen == 1, false);
    value = report.value[0] != 0;
    return true;
}

bool GetU8(const PendingAttributeReport & report, uint8_t & value)
{
    VerifyOrReturnError(!AttrIsNull(report), false);
    VerifyOrReturnError(report.valueLen == 1, false);
    value = report.value[0];
    return true;
}

bool GetU16(const PendingAttributeReport & report, uint16_t & value)
{
    VerifyOrReturnError(!AttrIsNull(report), false);
    VerifyOrReturnError(report.valueLen == 2, false);
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
    case Clusters::OnOff::Id:
        if (report.attributeId == Clusters::OnOff::Attributes::OnOff::Id && GetBool(report, boolValue))
        {
            return Clusters::OnOff::Attributes::OnOff::Set(endpoint, boolValue);
        }
        break;
    case Clusters::LevelControl::Id:
        if (report.attributeId == Clusters::LevelControl::Attributes::CurrentLevel::Id)
        {
            DataModel::Nullable<uint8_t> level;
            if (AttrIsNull(report))
            {
                level.SetNull();
                return Clusters::LevelControl::Attributes::CurrentLevel::Set(endpoint, level);
            }
            if (GetU8(report, u8Value))
            {
                level.SetNonNull(u8Value);
                return Clusters::LevelControl::Attributes::CurrentLevel::Set(endpoint, level);
            }
        }
        break;
    case Clusters::ColorControl::Id:
        if (report.attributeId == Clusters::ColorControl::Attributes::CurrentHue::Id && GetU8(report, u8Value))
        {
            return Clusters::ColorControl::Attributes::CurrentHue::Set(endpoint, u8Value);
        }
        if (report.attributeId == Clusters::ColorControl::Attributes::CurrentSaturation::Id && GetU8(report, u8Value))
        {
            return Clusters::ColorControl::Attributes::CurrentSaturation::Set(endpoint, u8Value);
        }
        if (report.attributeId == Clusters::ColorControl::Attributes::CurrentX::Id && GetU16(report, u16Value))
        {
            return Clusters::ColorControl::Attributes::CurrentX::Set(endpoint, u16Value);
        }
        if (report.attributeId == Clusters::ColorControl::Attributes::CurrentY::Id && GetU16(report, u16Value))
        {
            return Clusters::ColorControl::Attributes::CurrentY::Set(endpoint, u16Value);
        }
        if (report.attributeId == Clusters::ColorControl::Attributes::ColorTemperatureMireds::Id &&
            GetU16(report, u16Value))
        {
            return Clusters::ColorControl::Attributes::ColorTemperatureMireds::Set(endpoint, u16Value);
        }
        if (report.attributeId == Clusters::ColorControl::Attributes::ColorMode::Id && GetU8(report, u8Value))
        {
            return Clusters::ColorControl::Attributes::ColorMode::Set(
                endpoint, static_cast<Clusters::ColorControl::ColorModeEnum>(u8Value));
        }
        if (report.attributeId == Clusters::ColorControl::Attributes::EnhancedCurrentHue::Id && GetU16(report, u16Value))
        {
            return Clusters::ColorControl::Attributes::EnhancedCurrentHue::Set(endpoint, u16Value);
        }
        if (report.attributeId == Clusters::ColorControl::Attributes::EnhancedColorMode::Id && GetU8(report, u8Value))
        {
            return Clusters::ColorControl::Attributes::EnhancedColorMode::Set(
                endpoint, static_cast<Clusters::ColorControl::EnhancedColorModeEnum>(u8Value));
        }
        if (report.attributeId == Clusters::ColorControl::Attributes::ColorLoopActive::Id && GetU8(report, u8Value))
        {
            return Clusters::ColorControl::Attributes::ColorLoopActive::Set(endpoint, u8Value);
        }
        if (report.attributeId == Clusters::ColorControl::Attributes::ColorLoopDirection::Id && GetU8(report, u8Value))
        {
            return Clusters::ColorControl::Attributes::ColorLoopDirection::Set(endpoint, u8Value);
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

    if (report->clusterId == Clusters::Identify::Id &&
        report->attributeId == Clusters::Identify::Attributes::IdentifyTime::Id && GetU16(*report, u16Value))
    {
        ChipLogError(Zcl, "UART IdentifyTime report ignored ep %u value %u: no Matter attribute setter",
                     static_cast<unsigned>(endpoint), static_cast<unsigned>(u16Value));
        statusLogged = true;
    }
    else
    {
        status = ApplyAttributeValue(*report, endpoint);
    }

    if (!statusLogged && status == Protocols::InteractionModel::Status::Success)
    {
        ChipLogProgress(Zcl,
                        "UART attribute report applied ep %u cluster " ChipLogFormatMEI
                        " attr " ChipLogFormatMEI " null=%u len %u",
                        static_cast<unsigned>(endpoint), ChipLogValueMEI(report->clusterId), ChipLogValueMEI(report->attributeId),
                        static_cast<unsigned>(report->isNull), static_cast<unsigned>(report->valueLen));
    }
    else if (!statusLogged)
    {
        ChipLogError(Zcl,
                     "UART attribute report not applied ep %u cluster " ChipLogFormatMEI
                     " attr " ChipLogFormatMEI " null=%u len %u status 0x%02x",
                     static_cast<unsigned>(endpoint), ChipLogValueMEI(report->clusterId), ChipLogValueMEI(report->attributeId),
                     static_cast<unsigned>(report->isNull), static_cast<unsigned>(report->valueLen), static_cast<unsigned>(status));
    }

    chip::Platform::MemoryFree(report);
}

} // namespace

CHIP_ERROR LightUartHandleAttributeReport(const lu_frame_t & frame)
{
    if ((frame.flags & LU_FLAG_ERROR) != 0)
    {
        ChipLogError(Zcl, "UART attribute report error ep %u cluster " ChipLogFormatMEI " attr " ChipLogFormatMEI,
                     static_cast<unsigned>(frame.endpoint), ChipLogValueMEI(frame.cluster), ChipLogValueMEI(frame.id));
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    const bool isNull = (frame.flags & LU_FLAG_NULL_VALUE) != 0;
    if (isNull && frame.payload_len != 0)
    {
        ChipLogError(Zcl, "UART null attribute report has payload: %u", static_cast<unsigned>(frame.payload_len));
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    if (frame.payload_len > sizeof(PendingAttributeReport::value))
    {
        ChipLogError(Zcl, "UART attribute value too large: %u", static_cast<unsigned>(frame.payload_len));
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    ChipLogProgress(Zcl,
                    "UART RX attr-report seq=%u ep=%u cluster=" ChipLogFormatMEI
                    " attr=" ChipLogFormatMEI " flags=0x%02x len=%u",
                    static_cast<unsigned>(frame.seq), static_cast<unsigned>(frame.endpoint), ChipLogValueMEI(frame.cluster),
                    ChipLogValueMEI(frame.id), static_cast<unsigned>(frame.flags), static_cast<unsigned>(frame.payload_len));
    LogHexBuffer("UART RX attr payload", frame.payload, frame.payload_len);

    PendingAttributeReport * report =
        static_cast<PendingAttributeReport *>(chip::Platform::MemoryAlloc(sizeof(PendingAttributeReport)));
    VerifyOrReturnError(report != nullptr, CHIP_ERROR_NO_MEMORY);

    report->endpoint    = frame.endpoint;
    report->clusterId   = frame.cluster;
    report->attributeId = frame.id;
    report->isNull      = isNull;
    report->valueLen    = frame.payload_len;
    memset(report->value, 0, sizeof(report->value));
    if (frame.payload_len != 0)
    {
        memcpy(report->value, frame.payload, frame.payload_len);
    }

    CHIP_ERROR err = PlatformMgr().ScheduleWork(ApplyAttributeReport, reinterpret_cast<intptr_t>(report));
    if (err != CHIP_NO_ERROR)
    {
        chip::Platform::MemoryFree(report);
    }
    return err;
}
