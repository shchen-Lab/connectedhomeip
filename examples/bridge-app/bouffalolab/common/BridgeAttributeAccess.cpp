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

#include "BridgeAppInternal.h"
#include "BridgeDevice.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/util/attribute-storage.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ZclString.h>

#include <cstring>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace {

constexpr uint16_t kBridgedDeviceBasicInformationClusterRevision = 2;
constexpr uint16_t kOnOffClusterRevision                         = 6;
constexpr uint16_t kLevelControlClusterRevision                  = 6;
constexpr uint16_t kColorControlClusterRevision                  = 7;
constexpr uint8_t kMinLevel                                      = 1;
constexpr uint8_t kMaxLevel                                      = 254;
constexpr uint16_t kColorTempMinMireds                           = 154;
constexpr uint16_t kColorTempMaxMireds                           = 454;
constexpr uint32_t kOnOffFeatureLighting                         = 0x00000001;
constexpr uint32_t kLevelControlFeatureOnOff                     = 0x00000001;
constexpr uint32_t kLevelControlFeatureLighting                  = 0x00000002;
constexpr uint32_t kLevelControlLightingFeatureMap               = kLevelControlFeatureOnOff | kLevelControlFeatureLighting;

Protocols::InteractionModel::Status CopyScalar(uint8_t * buffer, uint16_t maxReadLength, const void * value, size_t valueSize)
{
    VerifyOrReturnError(maxReadLength == valueSize, Protocols::InteractionModel::Status::Failure);
    std::memcpy(buffer, value, valueSize);
    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status CopyUint8(uint8_t * buffer, uint16_t maxReadLength, uint8_t value)
{
    return CopyScalar(buffer, maxReadLength, &value, sizeof(value));
}

Protocols::InteractionModel::Status CopyUint16(uint8_t * buffer, uint16_t maxReadLength, uint16_t value)
{
    return CopyScalar(buffer, maxReadLength, &value, sizeof(value));
}

Protocols::InteractionModel::Status CopyUint32(uint8_t * buffer, uint16_t maxReadLength, uint32_t value)
{
    return CopyScalar(buffer, maxReadLength, &value, sizeof(value));
}

uint16_t ReadUint16(const uint8_t * buffer)
{
    uint16_t value;
    std::memcpy(&value, buffer, sizeof(value));
    return value;
}

Protocols::InteractionModel::Status CopyZclString(uint8_t * buffer, uint16_t maxReadLength, const char * value,
                                                  uint16_t expectedLength)
{
    VerifyOrReturnError(maxReadLength == expectedLength, Protocols::InteractionModel::Status::Failure);
    MutableByteSpan zclSpan(buffer, maxReadLength);
    MakeZclCharString(zclSpan, value);
    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleReadBridgedDeviceBasicAttribute(BridgeDevice * device, AttributeId attributeId,
                                                                          uint8_t * buffer, uint16_t maxReadLength)
{
    using namespace BridgedDeviceBasicInformation::Attributes;

    if (attributeId == Reachable::Id)
    {
        return CopyUint8(buffer, maxReadLength, device->IsReachable() ? 1 : 0);
    }
    if (attributeId == NodeLabel::Id)
    {
        return CopyZclString(buffer, maxReadLength, device->GetName(), BridgeDevice::kNameSize);
    }
    if (attributeId == UniqueID::Id)
    {
        return CopyZclString(buffer, maxReadLength, device->GetUniqueId(), BridgeDevice::kUniqueIdSize);
    }
    if (attributeId == FeatureMap::Id)
    {
        return CopyUint32(buffer, maxReadLength, 0);
    }
    if (attributeId == ClusterRevision::Id)
    {
        return CopyUint16(buffer, maxReadLength, kBridgedDeviceBasicInformationClusterRevision);
    }
    return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status HandleReadOnOffAttribute(BridgeDevice * device, AttributeId attributeId, uint8_t * buffer,
                                                             uint16_t maxReadLength)
{
    if (attributeId == OnOff::Attributes::OnOff::Id)
    {
        return CopyUint8(buffer, maxReadLength, device->IsOn() ? 1 : 0);
    }
    if (attributeId == OnOff::Attributes::FeatureMap::Id)
    {
        return CopyUint32(buffer, maxReadLength, device->IsLighting() ? kOnOffFeatureLighting : 0);
    }
    if (attributeId == OnOff::Attributes::ClusterRevision::Id)
    {
        return CopyUint16(buffer, maxReadLength, kOnOffClusterRevision);
    }
    return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status HandleReadLevelControlAttribute(BridgeDevice * device, AttributeId attributeId,
                                                                    uint8_t * buffer, uint16_t maxReadLength)
{
    VerifyOrReturnError(device->HasLevel(), Protocols::InteractionModel::Status::Failure);
    using namespace LevelControl::Attributes;

    if (attributeId == CurrentLevel::Id)
    {
        return CopyUint8(buffer, maxReadLength, device->GetLevel());
    }
    if (attributeId == MinLevel::Id)
    {
        return CopyUint8(buffer, maxReadLength, kMinLevel);
    }
    if (attributeId == MaxLevel::Id)
    {
        return CopyUint8(buffer, maxReadLength, kMaxLevel);
    }
    if (attributeId == Options::Id || attributeId == FeatureMap::Id)
    {
        return attributeId == Options::Id ? CopyUint8(buffer, maxReadLength, 0)
                                          : CopyUint32(buffer, maxReadLength, kLevelControlLightingFeatureMap);
    }
    if (attributeId == OnLevel::Id)
    {
        return CopyUint8(buffer, maxReadLength, device->GetOnLevel());
    }
    if (attributeId == ClusterRevision::Id)
    {
        return CopyUint16(buffer, maxReadLength, kLevelControlClusterRevision);
    }
    return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status HandleReadColorControlAttribute(BridgeDevice * device, AttributeId attributeId,
                                                                    uint8_t * buffer, uint16_t maxReadLength)
{
    VerifyOrReturnError(device->HasColor(), Protocols::InteractionModel::Status::Failure);
    using namespace ColorControl::Attributes;

    if (attributeId == CurrentHue::Id && device->HasHueSaturation())
    {
        return CopyUint8(buffer, maxReadLength, device->GetHue());
    }
    if (attributeId == CurrentSaturation::Id && device->HasHueSaturation())
    {
        return CopyUint8(buffer, maxReadLength, device->GetSaturation());
    }
    if (attributeId == RemainingTime::Id)
    {
        return CopyUint16(buffer, maxReadLength, 0);
    }
    if (attributeId == CurrentX::Id && device->HasXY())
    {
        return CopyUint16(buffer, maxReadLength, device->GetCurrentX());
    }
    if (attributeId == CurrentY::Id && device->HasXY())
    {
        return CopyUint16(buffer, maxReadLength, device->GetCurrentY());
    }
    if (attributeId == ColorTemperatureMireds::Id && device->HasColorTemperature())
    {
        return CopyUint16(buffer, maxReadLength, device->GetColorTemperatureMireds());
    }
    if (attributeId == ColorMode::Id)
    {
        return CopyUint8(buffer, maxReadLength, device->GetColorMode());
    }
    if (attributeId == EnhancedColorMode::Id)
    {
        return CopyUint8(buffer, maxReadLength, device->GetEnhancedColorMode());
    }
    if (attributeId == Options::Id || attributeId == NumberOfPrimaries::Id)
    {
        return CopyUint8(buffer, maxReadLength, 0);
    }
    if (attributeId == ColorCapabilities::Id)
    {
        return CopyUint16(buffer, maxReadLength, static_cast<uint16_t>(device->GetColorFeatures()));
    }
    if ((attributeId == ColorTempPhysicalMinMireds::Id || attributeId == CoupleColorTempToLevelMinMireds::Id) &&
        device->HasColorTemperature())
    {
        return CopyUint16(buffer, maxReadLength, kColorTempMinMireds);
    }
    if (attributeId == ColorTempPhysicalMaxMireds::Id && device->HasColorTemperature())
    {
        return CopyUint16(buffer, maxReadLength, kColorTempMaxMireds);
    }
    if (attributeId == StartUpColorTemperatureMireds::Id && device->HasColorTemperature())
    {
        return CopyUint16(buffer, maxReadLength, device->GetStartUpColorTemperatureMireds());
    }
    if (attributeId == FeatureMap::Id)
    {
        return CopyUint32(buffer, maxReadLength, device->GetColorFeatures());
    }
    if (attributeId == ClusterRevision::Id)
    {
        return CopyUint16(buffer, maxReadLength, kColorControlClusterRevision);
    }
    return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status HandleWriteOnOffAttribute(BridgeDevice * device, AttributeId attributeId, uint8_t * buffer)
{
    VerifyOrReturnError(attributeId == OnOff::Attributes::OnOff::Id && device->IsReachable(),
                        Protocols::InteractionModel::Status::Failure);
    device->SetOnOff(*buffer == 1);
    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleWriteLevelControlAttribute(BridgeDevice * device, AttributeId attributeId,
                                                                     uint8_t * buffer)
{
    VerifyOrReturnError(device->HasLevel() && device->IsReachable(), Protocols::InteractionModel::Status::Failure);
    if (attributeId == LevelControl::Attributes::CurrentLevel::Id)
    {
        device->SetLevel(*buffer);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == LevelControl::Attributes::OnLevel::Id)
    {
        device->SetOnLevel(*buffer);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == LevelControl::Attributes::Options::Id)
    {
        return Protocols::InteractionModel::Status::Success;
    }
    return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status HandleWriteColorControlAttribute(BridgeDevice * device, AttributeId attributeId,
                                                                     uint8_t * buffer)
{
    VerifyOrReturnError(device->HasColor() && device->IsReachable(), Protocols::InteractionModel::Status::Failure);

    if (attributeId == ColorControl::Attributes::CurrentHue::Id && device->HasHueSaturation())
    {
        device->SetHue(*buffer);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::CurrentSaturation::Id && device->HasHueSaturation())
    {
        device->SetSaturation(*buffer);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::CurrentX::Id && device->HasXY())
    {
        device->SetCurrentX(ReadUint16(buffer));
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::CurrentY::Id && device->HasXY())
    {
        device->SetCurrentY(ReadUint16(buffer));
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::ColorTemperatureMireds::Id && device->HasColorTemperature())
    {
        device->SetColorTemperatureMireds(ReadUint16(buffer));
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::StartUpColorTemperatureMireds::Id && device->HasColorTemperature())
    {
        device->SetStartUpColorTemperatureMireds(ReadUint16(buffer));
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::ColorMode::Id || attributeId == ColorControl::Attributes::EnhancedColorMode::Id ||
        attributeId == ColorControl::Attributes::Options::Id || attributeId == ColorControl::Attributes::RemainingTime::Id)
    {
        return Protocols::InteractionModel::Status::Success;
    }
    return Protocols::InteractionModel::Status::Failure;
}

} // namespace

Protocols::InteractionModel::Status emberAfExternalAttributeReadCallback(EndpointId endpoint, ClusterId clusterId,
                                                                         const EmberAfAttributeMetadata * attributeMetadata,
                                                                         uint8_t * buffer, uint16_t maxReadLength)
{
    BridgeDevice * device = FindBridgeDevice(endpoint);
    VerifyOrReturnError(device != nullptr, Protocols::InteractionModel::Status::Failure);

    if (clusterId == BridgedDeviceBasicInformation::Id)
    {
        return HandleReadBridgedDeviceBasicAttribute(device, attributeMetadata->attributeId, buffer, maxReadLength);
    }
    if (clusterId == OnOff::Id)
    {
        return HandleReadOnOffAttribute(device, attributeMetadata->attributeId, buffer, maxReadLength);
    }
    if (clusterId == LevelControl::Id)
    {
        return HandleReadLevelControlAttribute(device, attributeMetadata->attributeId, buffer, maxReadLength);
    }
    if (clusterId == ColorControl::Id)
    {
        return HandleReadColorControlAttribute(device, attributeMetadata->attributeId, buffer, maxReadLength);
    }
    return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status emberAfExternalAttributeWriteCallback(EndpointId endpoint, ClusterId clusterId,
                                                                          const EmberAfAttributeMetadata * attributeMetadata,
                                                                          uint8_t * buffer)
{
    BridgeDevice * device = FindBridgeDevice(endpoint);
    VerifyOrReturnError(device != nullptr, Protocols::InteractionModel::Status::Failure);

    if (clusterId == OnOff::Id)
    {
        return HandleWriteOnOffAttribute(device, attributeMetadata->attributeId, buffer);
    }
    if (clusterId == LevelControl::Id)
    {
        return HandleWriteLevelControlAttribute(device, attributeMetadata->attributeId, buffer);
    }
    if (clusterId == ColorControl::Id)
    {
        return HandleWriteColorControlAttribute(device, attributeMetadata->attributeId, buffer);
    }
    return Protocols::InteractionModel::Status::Failure;
}
