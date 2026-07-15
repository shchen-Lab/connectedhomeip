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

#include "BridgeUartCommandObserver.h"

#include "BridgeAppInternal.h"
#include "BridgeDevice.h"
#include "LightUartBridge.h"
#include "LightUartCommandEncoder.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <app/CommandHandlerInterface.h>
#include <app/CommandHandlerInterfaceRegistry.h>
#include <app/data-model/Decode.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/LockTracker.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace {

bool SupportsColorCommand(const BridgeDevice & device, CommandId commandId)
{
    switch (commandId)
    {
    case ColorControl::Commands::MoveToHue::Id:
    case ColorControl::Commands::MoveHue::Id:
    case ColorControl::Commands::StepHue::Id:
    case ColorControl::Commands::MoveToSaturation::Id:
    case ColorControl::Commands::MoveSaturation::Id:
    case ColorControl::Commands::StepSaturation::Id:
    case ColorControl::Commands::MoveToHueAndSaturation::Id:
        return device.HasHueSaturation();
    case ColorControl::Commands::MoveToColor::Id:
    case ColorControl::Commands::MoveColor::Id:
    case ColorControl::Commands::StepColor::Id:
        return device.HasXY();
    case ColorControl::Commands::MoveToColorTemperature::Id:
    case ColorControl::Commands::MoveColorTemperature::Id:
    case ColorControl::Commands::StepColorTemperature::Id:
        return device.HasColorTemperature();
    case ColorControl::Commands::StopMoveStep::Id:
        return device.HasColor();
    default:
        return false;
    }
}

bool SupportsCommand(const BridgeDevice & device, ClusterId clusterId, CommandId commandId)
{
    if (!device.IsReachable())
    {
        return false;
    }

    switch (clusterId)
    {
    case OnOff::Id:
        return true;
    case LevelControl::Id:
        return device.HasLevel();
    case ColorControl::Id:
        return SupportsColorCommand(device, commandId);
    default:
        return false;
    }
}

template <typename RequestT>
void ForwardCommand(CommandHandlerInterface::HandlerContext & context, const char * name)
{
    TLV::TLVReader reader(context.mPayload);
    RequestT request;
    CHIP_ERROR err = DataModel::Decode(reader, request);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(Zcl, "Failed to decode bridge UART command %s: %" CHIP_ERROR_FORMAT, name, err.Format());
        context.SetCommandNotHandled();
        return;
    }

    uint8_t payload[LightUartCommandEncoder::kCommandPayloadMax];
    uint16_t payloadLen = 0;
    lu_status_t status  = LightUartCommandEncoder::EncodeCommandPayload(request, payload, sizeof(payload), payloadLen);
    if (status != LU_OK)
    {
        ChipLogError(Zcl, "Failed to encode bridge UART command %s: %d", name, static_cast<int>(status));
        context.SetCommandNotHandled();
        return;
    }

    err = LightUartBridgeSendCommand(context.mRequestPath.mEndpointId, context.mRequestPath.mClusterId,
                                     context.mRequestPath.mCommandId, payload, payloadLen);
    if (err == CHIP_NO_ERROR)
    {
        ChipLogProgress(Zcl, "Forwarded bridge command to UART: ep=%u cluster=" ChipLogFormatMEI " command=" ChipLogFormatMEI " %s",
                        static_cast<unsigned>(context.mRequestPath.mEndpointId), ChipLogValueMEI(context.mRequestPath.mClusterId),
                        ChipLogValueMEI(context.mRequestPath.mCommandId), name);
    }
    else
    {
        ChipLogError(Zcl, "Failed to forward bridge UART command %s: %" CHIP_ERROR_FORMAT, name, err.Format());
    }

    context.SetCommandNotHandled();
}

#define FORWARD_COMMAND(CommandType)                                                                                               \
    case CommandType::Id:                                                                                                          \
        ForwardCommand<CommandType::DecodableType>(context, #CommandType);                                                         \
        break

void ForwardOnOffCommand(CommandHandlerInterface::HandlerContext & context)
{
    switch (context.mRequestPath.mCommandId)
    {
        FORWARD_COMMAND(OnOff::Commands::Off);
        FORWARD_COMMAND(OnOff::Commands::On);
        FORWARD_COMMAND(OnOff::Commands::Toggle);
    default:
        break;
    }
}

void ForwardLevelControlCommand(CommandHandlerInterface::HandlerContext & context)
{
    switch (context.mRequestPath.mCommandId)
    {
        FORWARD_COMMAND(LevelControl::Commands::MoveToLevel);
        FORWARD_COMMAND(LevelControl::Commands::Move);
        FORWARD_COMMAND(LevelControl::Commands::Step);
        FORWARD_COMMAND(LevelControl::Commands::Stop);
        FORWARD_COMMAND(LevelControl::Commands::MoveToLevelWithOnOff);
        FORWARD_COMMAND(LevelControl::Commands::MoveWithOnOff);
        FORWARD_COMMAND(LevelControl::Commands::StepWithOnOff);
        FORWARD_COMMAND(LevelControl::Commands::StopWithOnOff);
    default:
        break;
    }
}

void ForwardColorControlCommand(CommandHandlerInterface::HandlerContext & context)
{
    switch (context.mRequestPath.mCommandId)
    {
        FORWARD_COMMAND(ColorControl::Commands::MoveToHue);
        FORWARD_COMMAND(ColorControl::Commands::MoveHue);
        FORWARD_COMMAND(ColorControl::Commands::StepHue);
        FORWARD_COMMAND(ColorControl::Commands::MoveToSaturation);
        FORWARD_COMMAND(ColorControl::Commands::MoveSaturation);
        FORWARD_COMMAND(ColorControl::Commands::StepSaturation);
        FORWARD_COMMAND(ColorControl::Commands::MoveToHueAndSaturation);
        FORWARD_COMMAND(ColorControl::Commands::MoveToColor);
        FORWARD_COMMAND(ColorControl::Commands::MoveColor);
        FORWARD_COMMAND(ColorControl::Commands::StepColor);
        FORWARD_COMMAND(ColorControl::Commands::MoveToColorTemperature);
        FORWARD_COMMAND(ColorControl::Commands::StopMoveStep);
        FORWARD_COMMAND(ColorControl::Commands::MoveColorTemperature);
        FORWARD_COMMAND(ColorControl::Commands::StepColorTemperature);
    default:
        break;
    }
}

#undef FORWARD_COMMAND

class BridgeUartCommandObserver : public CommandHandlerInterface
{
public:
    explicit BridgeUartCommandObserver(ClusterId clusterId) : CommandHandlerInterface(NullOptional, clusterId) {}

    void InvokeCommand(HandlerContext & context) override
    {
        BridgeDevice * device = FindBridgeDevice(context.mRequestPath.mEndpointId);
        if (device == nullptr || !SupportsCommand(*device, context.mRequestPath.mClusterId, context.mRequestPath.mCommandId))
        {
            return;
        }

        switch (context.mRequestPath.mClusterId)
        {
        case OnOff::Id:
            ForwardOnOffCommand(context);
            break;
        case LevelControl::Id:
            ForwardLevelControlCommand(context);
            break;
        case ColorControl::Id:
            ForwardColorControlCommand(context);
            break;
        default:
            break;
        }
    }
};

BridgeUartCommandObserver sOnOffObserver(OnOff::Id);
BridgeUartCommandObserver sLevelControlObserver(LevelControl::Id);
BridgeUartCommandObserver sColorControlObserver(ColorControl::Id);
bool sRegistered = false;

} // namespace

CHIP_ERROR InitBridgeUartCommandObserver()
{
    assertChipStackLockedByCurrentThread();
    if (sRegistered)
    {
        return CHIP_NO_ERROR;
    }

    CommandHandlerInterfaceRegistry & registry = CommandHandlerInterfaceRegistry::Instance();
    ReturnErrorOnFailure(registry.RegisterCommandHandler(&sOnOffObserver));

    CHIP_ERROR err = registry.RegisterCommandHandler(&sLevelControlObserver);
    if (err != CHIP_NO_ERROR)
    {
        registry.UnregisterCommandHandler(&sOnOffObserver);
        return err;
    }

    err = registry.RegisterCommandHandler(&sColorControlObserver);
    if (err != CHIP_NO_ERROR)
    {
        registry.UnregisterCommandHandler(&sLevelControlObserver);
        registry.UnregisterCommandHandler(&sOnOffObserver);
        return err;
    }

    sRegistered = true;
    return CHIP_NO_ERROR;
}
