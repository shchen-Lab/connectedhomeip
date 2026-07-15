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
#include "BridgeAppInternal.h"
#include "BridgeDevice.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <app/reporting/reporting.h>
#include <app/util/attribute-storage.h>
#include <app/util/endpoint-config-api.h>
#include <bridged-actions-stub.h>
#include <devices/Ids.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/LockTracker.h>

#include <cstring>
#include <memory>

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::app::Clusters;
using namespace ::chip::DeviceLayer;

namespace {

constexpr EndpointId kAggregatorEndpointId = 1;

constexpr int kDescriptorAttributeArraySize = 254;

EndpointId gCurrentEndpointId;
EndpointId gFirstDynamicEndpointId;
BridgeDevice * gDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];

BridgeDevice gOnOffLight("Light 1", "Office", "bridge-light-1", true, false, 0);
BridgeDevice gDimmableLight("Dimmable Light 1", "Office", "bridge-dimmer-1", true, true, 0);
BridgeDevice gExtendedColorLight("Extended Color Light 1", "Kitchen", "bridge-extended-color-1", true, true,
                                 BridgeDevice::kExtendedColorFeatureMap);
BridgeDevice gPlug("Plug 1", "Den", "bridge-plug-1", false, false, 0);

BridgeDevice gDynamicOnOffLight("On/Off Light 2", "Office", "bridge-light-2", true, false, 0);
BridgeDevice gDynamicDimmableLight("Dimmable Light 2", "Office", "bridge-dimmer-2", true, true, 0);
BridgeDevice gDynamicColorTemperatureLight("Color Temperature Light 1", "Bedroom", "bridge-ct-1", true, true,
                                           BridgeDevice::kColorFeatureColorTemperature);
BridgeDevice gDynamicExtendedColorLight("Extended Color Light 2", "Living Room", "bridge-extended-color-2", true, true,
                                        BridgeDevice::kExtendedColorFeatureMap);
BridgeDevice * gDynamicDevice = nullptr;

std::unique_ptr<Actions::ActionsDelegateImpl> sActionsDelegateImpl;
std::unique_ptr<Actions::ActionsServer> sActionsServer;

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnOff::Id, BOOLEAN, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::FeatureMap::Id, BITMAP32, 4, 0), DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(levelControlAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::CurrentLevel::Id, INT8U, 1, ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::MinLevel::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::MaxLevel::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::Options::Id, BITMAP8, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::OnLevel::Id, INT8U, 1,
                              ZAP_ATTRIBUTE_MASK(WRITABLE) | ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::FeatureMap::Id, BITMAP32, 4, 0), DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(colorControlAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CurrentHue::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CurrentSaturation::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::RemainingTime::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CurrentX::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CurrentY::Id, INT16U, 2, 0),
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
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::FeatureMap::Id, BITMAP32, 4, 0), DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(colorControlTemperatureAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::RemainingTime::Id, INT16U, 2, 0),
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
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::FeatureMap::Id, BITMAP32, 4, 0), DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::DeviceTypeList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ServerList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClientList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::PartsList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedDeviceBasicAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, CHAR_STRING, BridgeDevice::kNameSize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::Reachable::Id, BOOLEAN, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::UniqueID::Id, CHAR_STRING, BridgeDevice::kUniqueIdSize, 0),
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

constexpr CommandId colorControlTemperatureIncomingCommands[] = {
    ColorControl::Commands::MoveToColorTemperature::Id,
    ColorControl::Commands::StopMoveStep::Id,
    ColorControl::Commands::MoveColorTemperature::Id,
    ColorControl::Commands::StepColorTemperature::Id,
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
    ColorControl::Commands::MoveToColor::Id,
    ColorControl::Commands::MoveColor::Id,
    ColorControl::Commands::StepColor::Id,
    ColorControl::Commands::MoveToColorTemperature::Id,
    ColorControl::Commands::StopMoveStep::Id,
    ColorControl::Commands::MoveColorTemperature::Id,
    ColorControl::Commands::StepColorTemperature::Id,
    kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedOnOffLightClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedDimmableLightClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(LevelControl::Id, levelControlAttrs, ZAP_CLUSTER_MASK(SERVER), levelControlIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedColorTemperatureLightClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(LevelControl::Id, levelControlAttrs, ZAP_CLUSTER_MASK(SERVER), levelControlIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ColorControl::Id, colorControlTemperatureAttrs, ZAP_CLUSTER_MASK(SERVER),
                            colorControlTemperatureIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedExtendedColorLightClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(LevelControl::Id, levelControlAttrs, ZAP_CLUSTER_MASK(SERVER), levelControlIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ColorControl::Id, colorControlAttrs, ZAP_CLUSTER_MASK(SERVER), colorControlIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_ENDPOINT(bridgedOnOffLightEndpoint, bridgedOnOffLightClusters);
DECLARE_DYNAMIC_ENDPOINT(bridgedDimmableLightEndpoint, bridgedDimmableLightClusters);
DECLARE_DYNAMIC_ENDPOINT(bridgedColorTemperatureLightEndpoint, bridgedColorTemperatureLightClusters);
DECLARE_DYNAMIC_ENDPOINT(bridgedExtendedColorLightEndpoint, bridgedExtendedColorLightClusters);

DataVersion gOnOffLightDataVersions[MATTER_ARRAY_SIZE(bridgedOnOffLightClusters)];
DataVersion gDimmableLightDataVersions[MATTER_ARRAY_SIZE(bridgedDimmableLightClusters)];
DataVersion gExtendedColorLightDataVersions[MATTER_ARRAY_SIZE(bridgedExtendedColorLightClusters)];
DataVersion gPlugDataVersions[MATTER_ARRAY_SIZE(bridgedOnOffLightClusters)];
DataVersion gDynamicDeviceDataVersions[MATTER_ARRAY_SIZE(bridgedExtendedColorLightClusters)];

const EmberAfDeviceType gRootDeviceTypes[]          = { { Device::kRootNodeDeviceTypeId, Device::kRootNodeDeviceTypeRevision } };
const EmberAfDeviceType gAggregateNodeDeviceTypes[] = { { Device::kAggregatorDeviceTypeId,
                                                          Device::kAggregatorDeviceTypeRevision } };
const EmberAfDeviceType gBridgedOnOffDeviceTypes[]  = { { Device::kOnOffLightDeviceTypeId, Device::kOnOffLightDeviceTypeRevision },
                                                        { Device::kBridgedNodeDeviceTypeId,
                                                          Device::kBridgedNodeDeviceTypeRevision } };
const EmberAfDeviceType gBridgedDimmableDeviceTypes[] = {
    { Device::kDimmableLightDeviceTypeId, Device::kDimmableLightDeviceTypeRevision },
    { Device::kBridgedNodeDeviceTypeId, Device::kBridgedNodeDeviceTypeRevision }
};
const EmberAfDeviceType gBridgedColorTemperatureDeviceTypes[] = {
    { Device::kColorTemperatureLightDeviceTypeId, Device::kColorTemperatureLightDeviceTypeRevision },
    { Device::kBridgedNodeDeviceTypeId, Device::kBridgedNodeDeviceTypeRevision }
};
const EmberAfDeviceType gBridgedExtendedColorDeviceTypes[] = {
    { Device::kExtendedColorLightDeviceTypeId, Device::kExtendedColorLightDeviceTypeRevision },
    { Device::kBridgedNodeDeviceTypeId, Device::kBridgedNodeDeviceTypeRevision }
};
const EmberAfDeviceType gBridgedPlugDeviceTypes[] = {
    { Device::kOnOffPlugInUnitDeviceTypeId, Device::kOnOffPlugInUnitDeviceTypeRevision },
    { Device::kBridgedNodeDeviceTypeId, Device::kBridgedNodeDeviceTypeRevision }
};

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
        if (device->HasHueSaturation())
        {
            ScheduleReportingCallback(device, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
            ScheduleReportingCallback(device, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
        }
        if (device->HasXY())
        {
            ScheduleReportingCallback(device, ColorControl::Id, ColorControl::Attributes::CurrentX::Id);
            ScheduleReportingCallback(device, ColorControl::Id, ColorControl::Attributes::CurrentY::Id);
        }
        if (device->HasColorTemperature())
        {
            ScheduleReportingCallback(device, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
        }
        ScheduleReportingCallback(device, ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
        ScheduleReportingCallback(device, ColorControl::Id, ColorControl::Attributes::EnhancedColorMode::Id);
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
            CHIP_ERROR err = emberAfSetDynamicEndpoint(index, gCurrentEndpointId, endpointType, dataVersionStorage, deviceTypeList,
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

void PrepareBridgeDevices()
{
    std::memset(gDevices, 0, sizeof(gDevices));
    gDynamicDevice = nullptr;

    gOnOffLight.SetReachable(true);
    gDimmableLight.SetReachable(true);
    gExtendedColorLight.SetReachable(true);
    gPlug.SetReachable(true);

    gOnOffLight.SetChangeCallback(HandleDeviceStatusChanged);
    gDimmableLight.SetChangeCallback(HandleDeviceStatusChanged);
    gExtendedColorLight.SetChangeCallback(HandleDeviceStatusChanged);
    gPlug.SetChangeCallback(HandleDeviceStatusChanged);
}

struct DynamicDeviceConfig
{
    BridgeDevice * device;
    EmberAfEndpointType * endpointType;
    const EmberAfDeviceType * deviceTypes;
    size_t deviceTypeCount;
};

const DynamicDeviceConfig kDynamicOnOffDeviceConfig = { &gDynamicOnOffLight, &bridgedOnOffLightEndpoint, gBridgedOnOffDeviceTypes,
                                                        MATTER_ARRAY_SIZE(gBridgedOnOffDeviceTypes) };
const DynamicDeviceConfig kDynamicDimmableDeviceConfig         = { &gDynamicDimmableLight, &bridgedDimmableLightEndpoint,
                                                                   gBridgedDimmableDeviceTypes,
                                                                   MATTER_ARRAY_SIZE(gBridgedDimmableDeviceTypes) };
const DynamicDeviceConfig kDynamicColorTemperatureDeviceConfig = { &gDynamicColorTemperatureLight,
                                                                   &bridgedColorTemperatureLightEndpoint,
                                                                   gBridgedColorTemperatureDeviceTypes,
                                                                   MATTER_ARRAY_SIZE(gBridgedColorTemperatureDeviceTypes) };
const DynamicDeviceConfig kDynamicExtendedColorDeviceConfig    = { &gDynamicExtendedColorLight, &bridgedExtendedColorLightEndpoint,
                                                                   gBridgedExtendedColorDeviceTypes,
                                                                   MATTER_ARRAY_SIZE(gBridgedExtendedColorDeviceTypes) };

const DynamicDeviceConfig * GetDynamicDeviceConfig(DynamicBridgeDeviceType type)
{
    switch (type)
    {
    case DynamicBridgeDeviceType::kOnOffLight:
        return &kDynamicOnOffDeviceConfig;
    case DynamicBridgeDeviceType::kDimmableLight:
        return &kDynamicDimmableDeviceConfig;
    case DynamicBridgeDeviceType::kColorTemperatureLight:
        return &kDynamicColorTemperatureDeviceConfig;
    case DynamicBridgeDeviceType::kExtendedColorLight:
        return &kDynamicExtendedColorDeviceConfig;
    }
    return nullptr;
}

const EmberAfCluster * FindCluster(const EmberAfEndpointType & endpointType, ClusterId clusterId)
{
    for (uint8_t index = 0; index < endpointType.clusterCount; index++)
    {
        if (endpointType.cluster[index].clusterId == clusterId)
        {
            return &endpointType.cluster[index];
        }
    }
    return nullptr;
}

bool ContainsAttribute(const EmberAfCluster * cluster, AttributeId attributeId)
{
    VerifyOrReturnValue(cluster != nullptr, false);
    for (uint16_t index = 0; index < cluster->attributeCount; index++)
    {
        if (cluster->attributes[index].attributeId == attributeId)
        {
            return true;
        }
    }
    return false;
}

bool AcceptsCommand(const EmberAfCluster * cluster, CommandId commandId)
{
    VerifyOrReturnValue(cluster != nullptr && cluster->acceptedCommandList != nullptr, false);
    for (const CommandId * command = cluster->acceptedCommandList; *command != kInvalidCommandId; command++)
    {
        if (*command == commandId)
        {
            return true;
        }
    }
    return false;
}

CHIP_ERROR ValidateDynamicDeviceTemplates()
{
    const EmberAfCluster * colorTemperatureCluster = FindCluster(bridgedColorTemperatureLightEndpoint, ColorControl::Id);
    const EmberAfCluster * extendedColorCluster    = FindCluster(bridgedExtendedColorLightEndpoint, ColorControl::Id);

    VerifyOrReturnError(ContainsAttribute(colorTemperatureCluster, ColorControl::Attributes::RemainingTime::Id),
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(ContainsAttribute(colorTemperatureCluster, ColorControl::Attributes::ColorTemperatureMireds::Id),
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(ContainsAttribute(extendedColorCluster, ColorControl::Attributes::RemainingTime::Id), CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(ContainsAttribute(extendedColorCluster, ColorControl::Attributes::CurrentHue::Id), CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(ContainsAttribute(extendedColorCluster, ColorControl::Attributes::CurrentSaturation::Id),
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(ContainsAttribute(extendedColorCluster, ColorControl::Attributes::CurrentX::Id), CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(ContainsAttribute(extendedColorCluster, ColorControl::Attributes::CurrentY::Id), CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(ContainsAttribute(extendedColorCluster, ColorControl::Attributes::ColorTemperatureMireds::Id),
                        CHIP_ERROR_INTERNAL);

    VerifyOrReturnError(AcceptsCommand(colorTemperatureCluster, ColorControl::Commands::MoveToColorTemperature::Id),
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AcceptsCommand(colorTemperatureCluster, ColorControl::Commands::StopMoveStep::Id), CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AcceptsCommand(colorTemperatureCluster, ColorControl::Commands::MoveColorTemperature::Id),
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AcceptsCommand(colorTemperatureCluster, ColorControl::Commands::StepColorTemperature::Id),
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AcceptsCommand(extendedColorCluster, ColorControl::Commands::MoveToColor::Id), CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AcceptsCommand(extendedColorCluster, ColorControl::Commands::MoveColor::Id), CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AcceptsCommand(extendedColorCluster, ColorControl::Commands::StepColor::Id), CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AcceptsCommand(extendedColorCluster, ColorControl::Commands::MoveToHueAndSaturation::Id),
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AcceptsCommand(extendedColorCluster, ColorControl::Commands::MoveToColorTemperature::Id),
                        CHIP_ERROR_INTERNAL);
    return CHIP_NO_ERROR;
}

} // namespace

BridgeDevice * FindBridgeDevice(EndpointId endpoint)
{
    uint16_t index = emberAfGetDynamicIndexFromEndpoint(endpoint);
    if (index >= CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        return nullptr;
    }
    return gDevices[index];
}

CHIP_ERROR InitBridgeApp()
{
    PrepareBridgeDevices();
    CHIP_ERROR templateValidationError = ValidateDynamicDeviceTemplates();
    VerifyOrReturnError(templateValidationError == CHIP_NO_ERROR, templateValidationError);

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
    VerifyOrReturnError(AddDeviceEndpoint(&gExtendedColorLight, &bridgedExtendedColorLightEndpoint,
                                          Span<const EmberAfDeviceType>(gBridgedExtendedColorDeviceTypes),
                                          Span<DataVersion>(gExtendedColorLightDataVersions), kAggregatorEndpointId) >= 0,
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AddDeviceEndpoint(&gPlug, &bridgedOnOffLightEndpoint,
                                          Span<const EmberAfDeviceType>(gBridgedPlugDeviceTypes),
                                          Span<DataVersion>(gPlugDataVersions), kAggregatorEndpointId) >= 0,
                        CHIP_ERROR_INTERNAL);

    return CHIP_NO_ERROR;
}

bool IsDynamicBridgeDeviceAddedLocked()
{
    assertChipStackLockedByCurrentThread();
    return gDynamicDevice != nullptr;
}

CHIP_ERROR AddDynamicBridgeDeviceLocked(DynamicBridgeDeviceType type)
{
    assertChipStackLockedByCurrentThread();
    VerifyOrReturnError(gDynamicDevice == nullptr, CHIP_ERROR_INCORRECT_STATE);

    const DynamicDeviceConfig * config = GetDynamicDeviceConfig(type);
    VerifyOrReturnError(config != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    VerifyOrReturnError(config->endpointType->clusterCount <= MATTER_ARRAY_SIZE(gDynamicDeviceDataVersions),
                        CHIP_ERROR_BUFFER_TOO_SMALL);
    config->device->SetReachable(true);
    config->device->SetChangeCallback(HandleDeviceStatusChanged);

    int index = AddDeviceEndpoint(config->device, config->endpointType,
                                  Span<const EmberAfDeviceType>(config->deviceTypes, config->deviceTypeCount),
                                  Span<DataVersion>(gDynamicDeviceDataVersions), kAggregatorEndpointId);
    if (index < 0)
    {
        config->device->SetChangeCallback(nullptr);
        config->device->SetReachable(false);
        config->device->SetEndpointId(kInvalidEndpointId);
        return CHIP_ERROR_NO_MEMORY;
    }

    gDynamicDevice = config->device;
    return CHIP_NO_ERROR;
}

CHIP_ERROR RemoveDynamicBridgeDeviceLocked()
{
    assertChipStackLockedByCurrentThread();
    VerifyOrReturnError(gDynamicDevice != nullptr, CHIP_ERROR_NOT_FOUND);

    for (uint8_t index = 0; index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; index++)
    {
        if (gDevices[index] != gDynamicDevice)
        {
            continue;
        }

        EndpointId endpoint = emberAfClearDynamicEndpoint(index);
        VerifyOrReturnError(endpoint != 0, CHIP_ERROR_INTERNAL);

        BridgeDevice * removedDevice = gDynamicDevice;
        gDevices[index]              = nullptr;
        gDynamicDevice               = nullptr;
        removedDevice->SetChangeCallback(nullptr);
        removedDevice->SetReachable(false);
        removedDevice->SetEndpointId(kInvalidEndpointId);

        ChipLogProgress(DeviceLayer, "Removed dynamic device %s from dynamic endpoint %u (index=%u)", removedDevice->GetName(),
                        endpoint, index);
        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_INTERNAL;
}

void emberAfActionsClusterInitCallback(EndpointId endpoint)
{
    VerifyOrReturn(endpoint == kAggregatorEndpointId,
                   ChipLogError(Zcl,
                                "Actions cluster delegate is not implemented for "
                                "endpoint with id %u.",
                                endpoint));
    VerifyOrReturn(emberAfContainsServer(endpoint, Actions::Id),
                   ChipLogError(Zcl, "Endpoint %u does not support Actions cluster.", endpoint));
    VerifyOrReturn(!sActionsDelegateImpl && !sActionsServer);

    sActionsDelegateImpl = std::make_unique<Actions::ActionsDelegateImpl>();
    sActionsServer       = std::make_unique<Actions::ActionsServer>(endpoint, *sActionsDelegateImpl.get());

    sActionsServer->Init();
}
