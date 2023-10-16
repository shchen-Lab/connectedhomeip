/**
 *
 *    Copyright (c) 2020-2022 Project CHIP Authors
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
 *
 */
#include "zigbee-diagnostic-server.h"
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app/DataModelRevision.h>
#include <app/EventLogging.h>
#include <app/InteractionModelEngine.h>
#include <app/util/attribute-storage.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ConfigurationManager.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <platform/PlatformManager.h>
#include <app/util/error-mapping.h>
#include <cstddef>
#include <cstring>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ZigbeeDiagnostic;
using namespace chip::app::Clusters::ZigbeeDiagnostic::Attributes;
using namespace chip::DeviceLayer;

namespace chip {
namespace app {
namespace Clusters {
namespace ZigbeeDiagnostic {

class ZigbeeDiagnosticServer : public AttributeAccessInterface
{
public:
    static ZigbeeDiagnosticServer & Instance();
    // Register for the Basic cluster on all endpoints.
    ZigbeeDiagnosticServer() : AttributeAccessInterface(Optional<EndpointId>::Missing(), ZigbeeDiagnostic::Id) {}
    bool TestDeviceCommand(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath);
    CHIP_ERROR Read(const ConcreteReadAttributePath & aPath, AttributeValueEncoder & aEncoder) override;
    CHIP_ERROR Write(const ConcreteDataAttributePath & aPath, AttributeValueDecoder & aDecoder) override;

private:
    static ZigbeeDiagnosticServer instance;
    CHIP_ERROR ReadLastMessageLQI(AttributeValueEncoder & aEncoder,chip::EndpointId endpointid);
    CHIP_ERROR ReadLastMessageRSSI(AttributeValueEncoder & aEncoder,chip::EndpointId endpointid);
    CHIP_ERROR WriteLastMessageRSSI(AttributeValueDecoder & aDecoder,chip::EndpointId endpointid);
    CHIP_ERROR WriteLastMessageLQI(AttributeValueDecoder & aDecoder,chip::EndpointId endpointid);
};

ZigbeeDiagnosticServer gAttrAccess;
ZigbeeDiagnosticServer ZigbeeDiagnosticServer::instance;

ZigbeeDiagnosticServer & ZigbeeDiagnosticServer::Instance()
{
    return instance;
}
CHIP_ERROR ZigbeeDiagnosticServer::Read(const ConcreteReadAttributePath & aPath, AttributeValueEncoder & aEncoder)
{
    printf("1111111111\r\n");
    if (aPath.mClusterId != Clusters::ZigbeeDiagnostic::Id)
    {
        // We shouldn't have been called at all.
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    CHIP_ERROR status = CHIP_NO_ERROR;

    switch (aPath.mAttributeId)
    {
    case LastMessageLQI::Id:
        status = ReadLastMessageLQI(aEncoder,aPath.mEndpointId);
        break;

    case LastMessageRSSI::Id:
        status = ReadLastMessageRSSI(aEncoder,aPath.mEndpointId);
        break;
    default:
        break;
    }
    return status;
}

CHIP_ERROR ZigbeeDiagnosticServer::ReadLastMessageLQI(AttributeValueEncoder & aEncoder,chip::EndpointId endpointid)
{
    uint8_t LastLQI;

    EmberAfStatus status = Attributes::LastMessageLQI::Get(endpointid, &LastLQI);
    if (status != EMBER_ZCL_STATUS_SUCCESS)
    {
        ChipLogProgress(Zcl, "ERR: ReadLastMessageLQI %x", status);
    }

    return aEncoder.Encode(LastLQI);
}
CHIP_ERROR ZigbeeDiagnosticServer::ReadLastMessageRSSI(AttributeValueEncoder & aEncoder,chip::EndpointId endpointid)
{   
    int8_t LastRSSI;

    EmberAfStatus status = Attributes::LastMessageRSSI::Get(endpointid, &LastRSSI);
    if (status != EMBER_ZCL_STATUS_SUCCESS)
    {
        ChipLogProgress(Zcl, "ERR: ReadLastMessageRSSI %x", status);
    }
    return aEncoder.Encode(LastRSSI);
}

CHIP_ERROR ZigbeeDiagnosticServer::Write(const ConcreteDataAttributePath & aPath, AttributeValueDecoder & aDecoder)
{
    VerifyOrDie(aPath.mClusterId == Clusters::ZigbeeDiagnostic::Id);
    CHIP_ERROR status = CHIP_NO_ERROR;
    switch (aPath.mAttributeId)
    {
    case LastMessageLQI::Id:
        status = WriteLastMessageLQI(aDecoder,aPath.mEndpointId);
    break;

    case LastMessageRSSI::Id:
        status = WriteLastMessageRSSI(aDecoder,aPath.mEndpointId);
    break;
    default:
        break;
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR ZigbeeDiagnosticServer::WriteLastMessageRSSI(AttributeValueDecoder & aDecoder,chip::EndpointId endpointid)
{
    int8_t LastRSSI;

    ReturnErrorOnFailure(aDecoder.Decode(LastRSSI));
    EmberAfStatus status = Attributes::LastMessageRSSI::Set(endpointid, LastRSSI);
    if (status != EMBER_ZCL_STATUS_SUCCESS)
    {
        ChipLogProgress(Zcl, "WriteLastMessageRSSI %x", status);
    }
    return CHIP_NO_ERROR;
}


CHIP_ERROR ZigbeeDiagnosticServer::WriteLastMessageLQI(AttributeValueDecoder & aDecoder,chip::EndpointId endpointid)
{
    uint8_t LastLQI;

    ReturnErrorOnFailure(aDecoder.Decode(LastLQI));
    EmberAfStatus status = Attributes::LastMessageLQI::Set(endpointid, LastLQI);
    if (status != EMBER_ZCL_STATUS_SUCCESS)
    {
        ChipLogProgress(Zcl, "WriteLastMessageLQI %x", status);
    }
    return CHIP_NO_ERROR;
}
bool ZigbeeDiagnosticServer::TestDeviceCommand(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath)
{
    commandObj->AddStatus(commandPath, app::ToInteractionModelStatus(EMBER_ZCL_STATUS_SUCCESS));
    return true;
}

} // namespace ZigbeeDiagnostic
} // namespace Clusters
} // namespace app
} // namespace chip

void MatterZigbeeDiagnosticPluginServerInitCallback()
{
    registerAttributeAccessOverride(&gAttrAccess);
}
bool emberAfZigbeeDiagnosticClusterTestDeviceCallback(
    chip::app::CommandHandler * commandObj, const chip::app::ConcreteCommandPath & commandPath,
    const chip::app::Clusters::ZigbeeDiagnostic::Commands::TestDevice::DecodableType & commandData)
{
    return ZigbeeDiagnosticServer::Instance().TestDeviceCommand(commandObj, commandPath);
}