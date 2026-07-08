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

#include "BridgeApp.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <app/reporting/reporting.h>
#include <app/util/attribute-storage-null-handling.h>
#include <app/util/attribute-storage.h>
#include <app/util/endpoint-config-api.h>
#include <bridged-actions-stub.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/ZclString.h>
#include <platform/CHIPDeviceLayer.h>

#include <cstring>
#include <memory>

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::app::Clusters;
using namespace ::chip::DeviceLayer;

namespace {

class BridgeDevice
{
public:
    static constexpr size_t kNameSize     = 32;
    static constexpr size_t kLocationSize = 32;

    enum Changed_t
    {
        kChanged_Reachable = 0x01,
        kChanged_State     = 0x02,
        kChanged_Location  = 0x04,
        kChanged_Name      = 0x08,
        kChanged_Level     = 0x10,
        kChanged_Color     = 0x20,
    };

    using ChangeCallback = void (*)(BridgeDevice * device, Changed_t changeMask);

    BridgeDevice(const char * name, const char * location, const char * uniqueId, bool isLighting, bool hasLevel, bool hasColor)
    {
        Platform::CopyString(mName, sizeof(mName), name);
        Platform::CopyString(mLocation, sizeof(mLocation), location);
        Platform::CopyString(mUniqueId, sizeof(mUniqueId), uniqueId);
        mIsLighting = isLighting;
        mHasLevel = hasLevel;
        mHasColor = hasColor;
    }

    bool IsOn() const { return mOn; }
    bool IsReachable() const { return mReachable; }
    bool IsLighting() const { return mIsLighting; }
    bool HasLevel() const { return mHasLevel; }
    bool HasColor() const { return mHasColor; }
    uint8_t GetLevel() const { return mLevel; }
    uint8_t GetOnLevel() const { return mOnLevel; }
    uint8_t GetHue() const { return mHue; }
    uint8_t GetSaturation() const { return mSaturation; }
    uint8_t GetColorMode() const { return mColorMode; }
    uint8_t GetEnhancedColorMode() const { return mEnhancedColorMode; }
    uint16_t GetColorTemperatureMireds() const { return mColorTemperatureMireds; }
    uint16_t GetStartUpColorTemperatureMireds() const { return mStartUpColorTemperatureMireds; }
    const char * GetName() const { return mName; }
    const char * GetUniqueId() const { return mUniqueId; }
    EndpointId GetEndpointId() const { return mEndpointId; }

    void SetEndpointId(EndpointId endpoint) { mEndpointId = endpoint; }
    void SetChangeCallback(ChangeCallback callback) { mChangeCallback = callback; }

    void SetOnOff(bool on)
    {
        bool changed = (mOn != on);
        mOn          = on;
        ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: %s", mName, mOn ? "ON" : "OFF");
        NotifyIfChanged(changed, kChanged_State);
    }

    void SetReachable(bool reachable)
    {
        bool changed = (mReachable != reachable);
        mReachable   = reachable;
        ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: %s", mName, mReachable ? "ONLINE" : "OFFLINE");
        NotifyIfChanged(changed, kChanged_Reachable);
    }

    void SetLevel(uint8_t level)
    {
        VerifyOrReturn(mHasLevel);
        bool changed = (mLevel != level);
        mLevel       = level;
        ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: level %u", mName, mLevel);
        NotifyIfChanged(changed, kChanged_Level);
    }

    void SetOnLevel(uint8_t level)
    {
        VerifyOrReturn(mHasLevel);
        mOnLevel = level;
        ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: on-level %u", mName, mOnLevel);
    }

    void SetHue(uint8_t hue)
    {
        VerifyOrReturn(mHasColor);
        bool changed = (mHue != hue);
        mHue               = hue;
        mColorMode         = 0;
        mEnhancedColorMode = 0;
        ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: hue %u", mName, mHue);
        NotifyIfChanged(changed, kChanged_Color);
    }

    void SetSaturation(uint8_t saturation)
    {
        VerifyOrReturn(mHasColor);
        bool changed = (mSaturation != saturation);
        mSaturation        = saturation;
        mColorMode         = 0;
        mEnhancedColorMode = 0;
        ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: saturation %u", mName, mSaturation);
        NotifyIfChanged(changed, kChanged_Color);
    }

    void SetColorTemperatureMireds(uint16_t temperatureMireds)
    {
        VerifyOrReturn(mHasColor);
        bool changed            = (mColorTemperatureMireds != temperatureMireds);
        mColorTemperatureMireds = temperatureMireds;
        mColorMode              = 2;
        mEnhancedColorMode      = 2;
        ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: color temperature %u mireds", mName, mColorTemperatureMireds);
        NotifyIfChanged(changed, kChanged_Color);
    }

    void SetStartUpColorTemperatureMireds(uint16_t temperatureMireds)
    {
        VerifyOrReturn(mHasColor);
        mStartUpColorTemperatureMireds = temperatureMireds;
        ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: startup color temperature %u mireds", mName,
                        mStartUpColorTemperatureMireds);
    }

private:
    void NotifyIfChanged(bool changed, Changed_t change)
    {
        if (changed && mChangeCallback != nullptr)
        {
            mChangeCallback(this, change);
        }
    }

    bool mOn                         = false;
    bool mReachable                  = false;
    bool mIsLighting                 = false;
    bool mHasLevel                   = false;
    bool mHasColor                   = false;
    uint8_t mLevel                   = 128;
    uint8_t mOnLevel                 = NumericAttributeTraits<uint8_t>::kNullValue;
    uint8_t mHue                     = 0;
    uint8_t mSaturation              = 0;
    uint16_t mColorTemperatureMireds        = 250;
    uint16_t mStartUpColorTemperatureMireds = NumericAttributeTraits<uint16_t>::kNullValue;
    uint8_t mColorMode               = 0;
    uint8_t mEnhancedColorMode       = 0;
    char mName[kNameSize]            = {};
    char mLocation[kLocationSize]    = {};
    char mUniqueId[kNameSize]        = {};
    EndpointId mEndpointId           = kInvalidEndpointId;
    ChangeCallback mChangeCallback   = nullptr;
};

constexpr uint16_t kDeviceTypeBridgedNode        = 0x0013;
constexpr uint16_t kDeviceTypeOnOffLight         = 0x0100;
constexpr uint16_t kDeviceTypeDimmableLight      = 0x0101;
constexpr uint16_t kDeviceTypeOnOffPlugInUnit    = 0x010A;
constexpr uint16_t kDeviceTypeExtendedColorLight = 0x010D;
constexpr uint16_t kDeviceTypeRootNode           = 0x0016;
constexpr uint16_t kDeviceTypeBridge             = 0x000e;
constexpr uint8_t kDeviceVersionDefault          = 1;
constexpr EndpointId kAggregatorEndpointId       = 1;

constexpr uint16_t kBridgedDeviceBasicInformationClusterRevision = 2;
constexpr uint16_t kOnOffClusterRevision                         = 6;
constexpr uint16_t kLevelControlClusterRevision                  = 6;
constexpr uint16_t kColorControlClusterRevision                  = 7;
constexpr int kNodeLabelSize                                     = 32;
constexpr int kUniqueIdSize                                      = 32;
constexpr int kDescriptorAttributeArraySize                      = 254;
constexpr uint8_t kMinLevel                                      = 1;
constexpr uint8_t kMaxLevel                                      = 254;
constexpr uint16_t kColorTempMinMireds                           = 153;
constexpr uint16_t kColorTempMaxMireds                           = 500;
constexpr uint32_t kOnOffFeatureLighting                         = 0x00000001;
constexpr uint32_t kLevelControlFeatureOnOff                     = 0x00000001;
constexpr uint32_t kLevelControlFeatureLighting                  = 0x00000002;
constexpr uint32_t kLevelControlLightingFeatureMap                = kLevelControlFeatureOnOff | kLevelControlFeatureLighting;
constexpr uint32_t kColorControlFeatureHueSaturation             = 0x00000001;
constexpr uint32_t kColorControlFeatureColorTemperature          = 0x00000010;
constexpr uint32_t kColorControlLightingFeatureMap                = kColorControlFeatureHueSaturation | kColorControlFeatureColorTemperature;

EndpointId gCurrentEndpointId;
EndpointId gFirstDynamicEndpointId;
BridgeDevice * gDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];

BridgeDevice gOnOffLight("Light 1", "Office", "mock-light-1", true, false, false);
BridgeDevice gDimmableLight("Dimmable Light 1", "Office", "mock-dimmer-1", true, true, false);
BridgeDevice gColorLight("Color Light 1", "Kitchen", "mock-color-1", true, true, true);
BridgeDevice gPlug("Plug 1", "Den", "mock-plug-1", false, false, false);

std::unique_ptr<Actions::ActionsDelegateImpl> sActionsDelegateImpl;
std::unique_ptr<Actions::ActionsServer> sActionsServer;

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnOff::Id, BOOLEAN, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::FeatureMap::Id, BITMAP32, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(levelControlAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::CurrentLevel::Id, INT8U, 1, ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::MinLevel::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::MaxLevel::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::Options::Id, BITMAP8, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::OnLevel::Id, INT8U, 1,
                              ZAP_ATTRIBUTE_MASK(WRITABLE) | ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::FeatureMap::Id, BITMAP32, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(colorControlAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CurrentHue::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CurrentSaturation::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorTemperatureMireds::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorMode::Id, ENUM8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::Options::Id, BITMAP8, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::NumberOfPrimaries::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::EnhancedColorMode::Id, ENUM8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorCapabilities::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorTempPhysicalMinMireds::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorTempPhysicalMaxMireds::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CoupleColorTempToLevelMinMireds::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::StartUpColorTemperatureMireds::Id, INT16U, 2,
                              ZAP_ATTRIBUTE_MASK(WRITABLE) | ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::FeatureMap::Id, BITMAP32, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::DeviceTypeList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ServerList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClientList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::PartsList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedDeviceBasicAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, CHAR_STRING, kNodeLabelSize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::Reachable::Id, BOOLEAN, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::UniqueID::Id, CHAR_STRING, kUniqueIdSize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::FeatureMap::Id, BITMAP32, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

constexpr CommandId onOffIncomingCommands[] = {
    OnOff::Commands::Off::Id,
    OnOff::Commands::On::Id,
    OnOff::Commands::Toggle::Id,
    kInvalidCommandId,
};

constexpr CommandId levelControlIncomingCommands[] = {
    LevelControl::Commands::MoveToLevel::Id,
    LevelControl::Commands::Move::Id,
    LevelControl::Commands::Step::Id,
    LevelControl::Commands::Stop::Id,
    LevelControl::Commands::MoveToLevelWithOnOff::Id,
    LevelControl::Commands::MoveWithOnOff::Id,
    LevelControl::Commands::StepWithOnOff::Id,
    LevelControl::Commands::StopWithOnOff::Id,
    kInvalidCommandId,
};

constexpr CommandId colorControlIncomingCommands[] = {
    ColorControl::Commands::MoveToHue::Id,
    ColorControl::Commands::MoveHue::Id,
    ColorControl::Commands::StepHue::Id,
    ColorControl::Commands::MoveToSaturation::Id,
    ColorControl::Commands::MoveSaturation::Id,
    ColorControl::Commands::StepSaturation::Id,
    ColorControl::Commands::MoveToHueAndSaturation::Id,
    ColorControl::Commands::MoveToColorTemperature::Id,
    ColorControl::Commands::StopMoveStep::Id,
    ColorControl::Commands::MoveColorTemperature::Id,
    ColorControl::Commands::StepColorTemperature::Id,
    kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedOnOffLightClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr)
    DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedDimmableLightClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(LevelControl::Id, levelControlAttrs, ZAP_CLUSTER_MASK(SERVER), levelControlIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr)
    DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedColorLightClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(LevelControl::Id, levelControlAttrs, ZAP_CLUSTER_MASK(SERVER), levelControlIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ColorControl::Id, colorControlAttrs, ZAP_CLUSTER_MASK(SERVER), colorControlIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr)
    DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_ENDPOINT(bridgedOnOffLightEndpoint, bridgedOnOffLightClusters);
DECLARE_DYNAMIC_ENDPOINT(bridgedDimmableLightEndpoint, bridgedDimmableLightClusters);
DECLARE_DYNAMIC_ENDPOINT(bridgedColorLightEndpoint, bridgedColorLightClusters);

DataVersion gOnOffLightDataVersions[MATTER_ARRAY_SIZE(bridgedOnOffLightClusters)];
DataVersion gDimmableLightDataVersions[MATTER_ARRAY_SIZE(bridgedDimmableLightClusters)];
DataVersion gColorLightDataVersions[MATTER_ARRAY_SIZE(bridgedColorLightClusters)];
DataVersion gPlugDataVersions[MATTER_ARRAY_SIZE(bridgedOnOffLightClusters)];

const EmberAfDeviceType gRootDeviceTypes[]          = { { kDeviceTypeRootNode, kDeviceVersionDefault } };
const EmberAfDeviceType gAggregateNodeDeviceTypes[] = { { kDeviceTypeBridge, kDeviceVersionDefault } };
const EmberAfDeviceType gBridgedOnOffDeviceTypes[]  = { { kDeviceTypeOnOffLight, kDeviceVersionDefault },
                                                        { kDeviceTypeBridgedNode, kDeviceVersionDefault } };
const EmberAfDeviceType gBridgedDimmableDeviceTypes[] = { { kDeviceTypeDimmableLight, kDeviceVersionDefault },
                                                          { kDeviceTypeBridgedNode, kDeviceVersionDefault } };
const EmberAfDeviceType gBridgedColorDeviceTypes[] = { { kDeviceTypeExtendedColorLight, kDeviceVersionDefault },
                                                       { kDeviceTypeBridgedNode, kDeviceVersionDefault } };
const EmberAfDeviceType gBridgedPlugDeviceTypes[] = { { kDeviceTypeOnOffPlugInUnit, kDeviceVersionDefault },
                                                      { kDeviceTypeBridgedNode, kDeviceVersionDefault } };

void CallReportingCallback(intptr_t closure)
{
    auto path = reinterpret_cast<ConcreteAttributePath *>(closure);
    MatterReportingAttributeChangeCallback(*path);
    Platform::Delete(path);
}

void ScheduleReportingCallback(BridgeDevice * device, ClusterId cluster, AttributeId attribute)
{
    auto * path = Platform::New<ConcreteAttributePath>(device->GetEndpointId(), cluster, attribute);
    VerifyOrReturn(path != nullptr, ChipLogError(DeviceLayer, "Failed to allocate reporting path"));
    PlatformMgr().ScheduleWork(CallReportingCallback, reinterpret_cast<intptr_t>(path));
}

void HandleDeviceStatusChanged(BridgeDevice * device, BridgeDevice::Changed_t itemChangedMask)
{
    if (itemChangedMask & BridgeDevice::kChanged_Reachable)
    {
        ScheduleReportingCallback(device, BridgedDeviceBasicInformation::Id,
                                  BridgedDeviceBasicInformation::Attributes::Reachable::Id);
    }

    if (itemChangedMask & BridgeDevice::kChanged_State)
    {
        ScheduleReportingCallback(device, OnOff::Id, OnOff::Attributes::OnOff::Id);
    }

    if (itemChangedMask & BridgeDevice::kChanged_Level)
    {
        ScheduleReportingCallback(device, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    }

    if (itemChangedMask & BridgeDevice::kChanged_Color)
    {
        ScheduleReportingCallback(device, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
        ScheduleReportingCallback(device, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
        ScheduleReportingCallback(device, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
    }

    if (itemChangedMask & BridgeDevice::kChanged_Name)
    {
        ScheduleReportingCallback(device, BridgedDeviceBasicInformation::Id,
                                  BridgedDeviceBasicInformation::Attributes::NodeLabel::Id);
    }
}

int AddDeviceEndpoint(BridgeDevice * device, EmberAfEndpointType * endpointType,
                      const Span<const EmberAfDeviceType> & deviceTypeList, const Span<DataVersion> & dataVersionStorage,
                      EndpointId parentEndpointId)
{
    for (uint8_t index = 0; index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; index++)
    {
        if (gDevices[index] != nullptr)
        {
            continue;
        }

        gDevices[index] = device;
        while (true)
        {
            device->SetEndpointId(gCurrentEndpointId);
            CHIP_ERROR err =
                emberAfSetDynamicEndpoint(index, gCurrentEndpointId, endpointType, dataVersionStorage, deviceTypeList,
                                          parentEndpointId);
            if (err == CHIP_NO_ERROR)
            {
                ChipLogProgress(DeviceLayer, "Added device %s to dynamic endpoint %u (index=%u)", device->GetName(),
                                gCurrentEndpointId, index);
                return index;
            }

            if (err != CHIP_ERROR_ENDPOINT_EXISTS)
            {
                gDevices[index] = nullptr;
                return -1;
            }

            if (++gCurrentEndpointId < gFirstDynamicEndpointId)
            {
                gCurrentEndpointId = gFirstDynamicEndpointId;
            }
        }
    }

    ChipLogError(DeviceLayer, "Failed to add dynamic endpoint: no endpoint slot available");
    return -1;
}

Protocols::InteractionModel::Status CopyScalar(uint8_t * buffer, uint16_t maxReadLength, const void * value, size_t valueSize)
{
    VerifyOrReturnError(maxReadLength == valueSize, Protocols::InteractionModel::Status::Failure);
    memcpy(buffer, value, valueSize);
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
    memcpy(&value, buffer, sizeof(value));
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
        return CopyZclString(buffer, maxReadLength, device->GetName(), kNodeLabelSize);
    }

    if (attributeId == UniqueID::Id)
    {
        return CopyZclString(buffer, maxReadLength, device->GetUniqueId(), kUniqueIdSize);
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
        return attributeId == Options::Id ? CopyUint8(buffer, maxReadLength, 0) :
                                            CopyUint32(buffer, maxReadLength, kLevelControlLightingFeatureMap);
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

    if (attributeId == CurrentHue::Id)
    {
        return CopyUint8(buffer, maxReadLength, device->GetHue());
    }

    if (attributeId == CurrentSaturation::Id)
    {
        return CopyUint8(buffer, maxReadLength, device->GetSaturation());
    }

    if (attributeId == ColorTemperatureMireds::Id)
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
        return CopyUint16(buffer, maxReadLength, kColorControlLightingFeatureMap);
    }

    if (attributeId == ColorTempPhysicalMinMireds::Id || attributeId == CoupleColorTempToLevelMinMireds::Id)
    {
        return CopyUint16(buffer, maxReadLength, kColorTempMinMireds);
    }

    if (attributeId == ColorTempPhysicalMaxMireds::Id)
    {
        return CopyUint16(buffer, maxReadLength, kColorTempMaxMireds);
    }

    if (attributeId == StartUpColorTemperatureMireds::Id)
    {
        return CopyUint16(buffer, maxReadLength, device->GetStartUpColorTemperatureMireds());
    }

    if (attributeId == FeatureMap::Id)
    {
        return CopyUint32(buffer, maxReadLength, kColorControlLightingFeatureMap);
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

    if (attributeId == ColorControl::Attributes::CurrentHue::Id)
    {
        device->SetHue(*buffer);
        return Protocols::InteractionModel::Status::Success;
    }

    if (attributeId == ColorControl::Attributes::CurrentSaturation::Id)
    {
        device->SetSaturation(*buffer);
        return Protocols::InteractionModel::Status::Success;
    }

    if (attributeId == ColorControl::Attributes::ColorTemperatureMireds::Id)
    {
        device->SetColorTemperatureMireds(ReadUint16(buffer));
        return Protocols::InteractionModel::Status::Success;
    }

    if (attributeId == ColorControl::Attributes::StartUpColorTemperatureMireds::Id)
    {
        device->SetStartUpColorTemperatureMireds(ReadUint16(buffer));
        return Protocols::InteractionModel::Status::Success;
    }

    if (attributeId == ColorControl::Attributes::ColorMode::Id ||
        attributeId == ColorControl::Attributes::EnhancedColorMode::Id || attributeId == ColorControl::Attributes::Options::Id)
    {
        return Protocols::InteractionModel::Status::Success;
    }

    return Protocols::InteractionModel::Status::Failure;
}

void PrepareMockDevices()
{
    memset(gDevices, 0, sizeof(gDevices));

    gOnOffLight.SetReachable(true);
    gDimmableLight.SetReachable(true);
    gColorLight.SetReachable(true);
    gPlug.SetReachable(true);

    gOnOffLight.SetChangeCallback(HandleDeviceStatusChanged);
    gDimmableLight.SetChangeCallback(HandleDeviceStatusChanged);
    gColorLight.SetChangeCallback(HandleDeviceStatusChanged);
    gPlug.SetChangeCallback(HandleDeviceStatusChanged);
}

} // namespace

CHIP_ERROR InitBridgeApp()
{
    PrepareMockDevices();

    gFirstDynamicEndpointId = static_cast<EndpointId>(
        static_cast<int>(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1))) + 1);
    gCurrentEndpointId = gFirstDynamicEndpointId;

    EndpointId lastFixedEndpoint = emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1));
    if (lastFixedEndpoint > kAggregatorEndpointId)
    {
        emberAfEndpointEnableDisable(lastFixedEndpoint, false);
    }

    emberAfSetDeviceTypeList(0, Span<const EmberAfDeviceType>(gRootDeviceTypes));
    emberAfSetDeviceTypeList(kAggregatorEndpointId, Span<const EmberAfDeviceType>(gAggregateNodeDeviceTypes));

    VerifyOrReturnError(AddDeviceEndpoint(&gOnOffLight, &bridgedOnOffLightEndpoint,
                                          Span<const EmberAfDeviceType>(gBridgedOnOffDeviceTypes),
                                          Span<DataVersion>(gOnOffLightDataVersions), kAggregatorEndpointId) >= 0,
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AddDeviceEndpoint(&gDimmableLight, &bridgedDimmableLightEndpoint,
                                          Span<const EmberAfDeviceType>(gBridgedDimmableDeviceTypes),
                                          Span<DataVersion>(gDimmableLightDataVersions), kAggregatorEndpointId) >= 0,
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AddDeviceEndpoint(&gColorLight, &bridgedColorLightEndpoint,
                                          Span<const EmberAfDeviceType>(gBridgedColorDeviceTypes),
                                          Span<DataVersion>(gColorLightDataVersions), kAggregatorEndpointId) >= 0,
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AddDeviceEndpoint(&gPlug, &bridgedOnOffLightEndpoint,
                                          Span<const EmberAfDeviceType>(gBridgedPlugDeviceTypes),
                                          Span<DataVersion>(gPlugDataVersions), kAggregatorEndpointId) >= 0,
                        CHIP_ERROR_INTERNAL);

    return CHIP_NO_ERROR;
}

Protocols::InteractionModel::Status emberAfExternalAttributeReadCallback(EndpointId endpoint, ClusterId clusterId,
                                                                         const EmberAfAttributeMetadata * attributeMetadata,
                                                                         uint8_t * buffer, uint16_t maxReadLength)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    if (endpointIndex >= CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT || gDevices[endpointIndex] == nullptr)
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    BridgeDevice * device = gDevices[endpointIndex];

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
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    if (endpointIndex >= CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT || gDevices[endpointIndex] == nullptr)
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    BridgeDevice * device = gDevices[endpointIndex];

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

void emberAfActionsClusterInitCallback(EndpointId endpoint)
{
    VerifyOrReturn(endpoint == 1,
                   ChipLogError(Zcl, "Actions cluster delegate is not implemented for endpoint with id %u.", endpoint));
    VerifyOrReturn(emberAfContainsServer(endpoint, Actions::Id),
                   ChipLogError(Zcl, "Endpoint %u does not support Actions cluster.", endpoint));
    VerifyOrReturn(!sActionsDelegateImpl && !sActionsServer);

    sActionsDelegateImpl = std::make_unique<Actions::ActionsDelegateImpl>();
    sActionsServer       = std::make_unique<Actions::ActionsServer>(endpoint, *sActionsDelegateImpl.get());

    sActionsServer->Init();
}
