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
    };

    using ChangeCallback = void (*)(BridgeDevice * device, Changed_t changeMask);

    BridgeDevice(const char * name, const char * location)
    {
        Platform::CopyString(mName, sizeof(mName), name);
        Platform::CopyString(mLocation, sizeof(mLocation), location);
    }

    bool IsOn() const { return mOn; }
    bool IsReachable() const { return mReachable; }
    const char * GetName() const { return mName; }
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

private:
    void NotifyIfChanged(bool changed, Changed_t change)
    {
        if (changed && mChangeCallback != nullptr)
        {
            mChangeCallback(this, change);
        }
    }

    bool mOn                       = false;
    bool mReachable                = false;
    char mName[kNameSize]          = {};
    char mLocation[kLocationSize]  = {};
    EndpointId mEndpointId         = kInvalidEndpointId;
    ChangeCallback mChangeCallback = nullptr;
};

constexpr uint16_t kDeviceTypeBridgedNode = 0x0013;
constexpr uint16_t kDeviceTypeOnOffLight  = 0x0100;
constexpr uint16_t kDeviceTypeRootNode    = 0x0016;
constexpr uint16_t kDeviceTypeBridge      = 0x000e;
constexpr uint8_t kDeviceVersionDefault   = 1;

constexpr uint16_t kBridgedDeviceBasicInformationClusterRevision = 2;
constexpr uint16_t kOnOffClusterRevision                         = 4;
constexpr int kNodeLabelSize                                     = 32;
constexpr int kDescriptorAttributeArraySize                      = 254;

EndpointId gCurrentEndpointId;
EndpointId gFirstDynamicEndpointId;
BridgeDevice * gDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];

BridgeDevice gLight1("Light 1", "Office");
BridgeDevice gLight2("Light 2", "Office");
BridgeDevice gLight3("Light 3", "Kitchen");
BridgeDevice gLight4("Light 4", "Den");

std::unique_ptr<Actions::ActionsDelegateImpl> sActionsDelegateImpl;
std::unique_ptr<Actions::ActionsServer> sActionsServer;

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnOff::Id, BOOLEAN, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::ClusterRevision::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::DeviceTypeList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ServerList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClientList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::PartsList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClusterRevision::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedDeviceBasicAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, CHAR_STRING, kNodeLabelSize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::Reachable::Id, BOOLEAN, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::ClusterRevision::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

constexpr CommandId onOffIncomingCommands[] = {
    OnOff::Commands::Off::Id,
    OnOff::Commands::On::Id,
    OnOff::Commands::Toggle::Id,
    OnOff::Commands::OffWithEffect::Id,
    OnOff::Commands::OnWithRecallGlobalScene::Id,
    OnOff::Commands::OnWithTimedOff::Id,
    kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedLightClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr)
    DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_ENDPOINT(bridgedLightEndpoint, bridgedLightClusters);

DataVersion gLight1DataVersions[MATTER_ARRAY_SIZE(bridgedLightClusters)];
DataVersion gLight2DataVersions[MATTER_ARRAY_SIZE(bridgedLightClusters)];
DataVersion gLight3DataVersions[MATTER_ARRAY_SIZE(bridgedLightClusters)];
DataVersion gLight4DataVersions[MATTER_ARRAY_SIZE(bridgedLightClusters)];

const EmberAfDeviceType gRootDeviceTypes[]          = { { kDeviceTypeRootNode, kDeviceVersionDefault } };
const EmberAfDeviceType gAggregateNodeDeviceTypes[] = { { kDeviceTypeBridge, kDeviceVersionDefault } };
const EmberAfDeviceType gBridgedOnOffDeviceTypes[]  = { { kDeviceTypeOnOffLight, kDeviceVersionDefault },
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

Protocols::InteractionModel::Status HandleReadBridgedDeviceBasicAttribute(BridgeDevice * device, AttributeId attributeId,
                                                                          uint8_t * buffer, uint16_t maxReadLength)
{
    using namespace BridgedDeviceBasicInformation::Attributes;

    if (attributeId == Reachable::Id && maxReadLength == 1)
    {
        *buffer = device->IsReachable() ? 1 : 0;
        return Protocols::InteractionModel::Status::Success;
    }

    if (attributeId == NodeLabel::Id && maxReadLength == kNodeLabelSize)
    {
        MutableByteSpan zclNameSpan(buffer, maxReadLength);
        MakeZclCharString(zclNameSpan, device->GetName());
        return Protocols::InteractionModel::Status::Success;
    }

    if (attributeId == ClusterRevision::Id && maxReadLength == sizeof(uint16_t))
    {
        uint16_t rev = kBridgedDeviceBasicInformationClusterRevision;
        memcpy(buffer, &rev, sizeof(rev));
        return Protocols::InteractionModel::Status::Success;
    }

    return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status HandleReadOnOffAttribute(BridgeDevice * device, AttributeId attributeId, uint8_t * buffer,
                                                             uint16_t maxReadLength)
{
    if (attributeId == OnOff::Attributes::OnOff::Id && maxReadLength == 1)
    {
        *buffer = device->IsOn() ? 1 : 0;
        return Protocols::InteractionModel::Status::Success;
    }

    if (attributeId == OnOff::Attributes::ClusterRevision::Id && maxReadLength == sizeof(uint16_t))
    {
        uint16_t rev = kOnOffClusterRevision;
        memcpy(buffer, &rev, sizeof(rev));
        return Protocols::InteractionModel::Status::Success;
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

void PrepareMockDevices()
{
    memset(gDevices, 0, sizeof(gDevices));

    gLight1.SetReachable(true);
    gLight2.SetReachable(true);
    gLight3.SetReachable(true);
    gLight4.SetReachable(true);

    gLight1.SetChangeCallback(HandleDeviceStatusChanged);
    gLight2.SetChangeCallback(HandleDeviceStatusChanged);
    gLight3.SetChangeCallback(HandleDeviceStatusChanged);
    gLight4.SetChangeCallback(HandleDeviceStatusChanged);
}

} // namespace

CHIP_ERROR InitBridgeApp()
{
    PrepareMockDevices();

    gFirstDynamicEndpointId = static_cast<EndpointId>(
        static_cast<int>(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1))) + 1);
    gCurrentEndpointId = gFirstDynamicEndpointId;

    EndpointId placeholderEndpoint = emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1));
    emberAfEndpointEnableDisable(placeholderEndpoint, false);

    emberAfSetDeviceTypeList(0, Span<const EmberAfDeviceType>(gRootDeviceTypes));
    emberAfSetDeviceTypeList(1, Span<const EmberAfDeviceType>(gAggregateNodeDeviceTypes));

    VerifyOrReturnError(AddDeviceEndpoint(&gLight1, &bridgedLightEndpoint, Span<const EmberAfDeviceType>(gBridgedOnOffDeviceTypes),
                                          Span<DataVersion>(gLight1DataVersions), 1) >= 0,
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AddDeviceEndpoint(&gLight2, &bridgedLightEndpoint, Span<const EmberAfDeviceType>(gBridgedOnOffDeviceTypes),
                                          Span<DataVersion>(gLight2DataVersions), 1) >= 0,
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AddDeviceEndpoint(&gLight3, &bridgedLightEndpoint, Span<const EmberAfDeviceType>(gBridgedOnOffDeviceTypes),
                                          Span<DataVersion>(gLight3DataVersions), 1) >= 0,
                        CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(AddDeviceEndpoint(&gLight4, &bridgedLightEndpoint, Span<const EmberAfDeviceType>(gBridgedOnOffDeviceTypes),
                                          Span<DataVersion>(gLight4DataVersions), 1) >= 0,
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

    return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status emberAfExternalAttributeWriteCallback(EndpointId endpoint, ClusterId clusterId,
                                                                          const EmberAfAttributeMetadata * attributeMetadata,
                                                                          uint8_t * buffer)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT && gDevices[endpointIndex] != nullptr && clusterId == OnOff::Id)
    {
        return HandleWriteOnOffAttribute(gDevices[endpointIndex], attributeMetadata->attributeId, buffer);
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
