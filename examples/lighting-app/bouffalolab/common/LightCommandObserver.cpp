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

#include "LightCommandObserver.h"

#include "AppTask.h"
#include "LightUartBridge.h"
#include "LightUartCommandEncoder.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <app/CommandHandlerInterface.h>
#include <app/CommandHandlerInterfaceRegistry.h>
#include <app/data-model/Decode.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace {

template <typename T>
uint32_t NullableForLog(const DataModel::Nullable<T> & value)
{
    return value.IsNull() ? UINT32_MAX : static_cast<uint32_t>(value.Value());
}

template <typename RequestT>
void ForwardObservedCommandToUart(CommandHandlerInterface::HandlerContext & context, const char * name, const RequestT & request)
{
    uint8_t payload[LightUartCommandEncoder::kCommandPayloadMax];
    uint16_t payloadLen = 0;

    lu_status_t status = LightUartCommandEncoder::EncodeCommandPayload(request, payload, sizeof(payload), payloadLen);
    if (status != LU_OK)
    {
        ChipLogError(Zcl, "Failed to encode UART light command %s: %d", name, static_cast<int>(status));
        return;
    }

    CHIP_ERROR err = LightUartBridgeSendCommand(context.mRequestPath.mEndpointId, context.mRequestPath.mClusterId,
                                                context.mRequestPath.mCommandId, payload, payloadLen);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(Zcl, "Failed to send UART light command %s: %" CHIP_ERROR_FORMAT, name, err.Format());
    }
}

template <typename RequestT, typename HandlerT>
void ObserveCommand(CommandHandlerInterface::HandlerContext & context, const char * name, HandlerT handler)
{
    if (context.mRequestPath.mClusterId != RequestT::GetClusterId() || context.mRequestPath.mCommandId != RequestT::GetCommandId())
    {
        return;
    }

    TLV::TLVReader reader(context.mPayload);
    RequestT request;
    CHIP_ERROR err = DataModel::Decode(reader, request);

    if (err == CHIP_NO_ERROR)
    {
        ChipLogProgress(Zcl, "Observed light command: ep %u cluster " ChipLogFormatMEI " command " ChipLogFormatMEI " %s",
                        context.mRequestPath.mEndpointId, ChipLogValueMEI(context.mRequestPath.mClusterId),
                        ChipLogValueMEI(context.mRequestPath.mCommandId), name);
        ForwardObservedCommandToUart(context, name, request);
        handler(request);
    }
    else
    {
        ChipLogError(Zcl, "Failed to decode observed light command %s: %" CHIP_ERROR_FORMAT, name, err.Format());
    }

    context.SetCommandNotHandled();
}

void ObserveOnOffCommands(CommandHandlerInterface::HandlerContext & context)
{
    using namespace OnOff::Commands;

    ObserveCommand<Off::DecodableType>(context, "OnOff.Off", [](const Off::DecodableType &) {});
    ObserveCommand<On::DecodableType>(context, "OnOff.On", [](const On::DecodableType &) {});
    ObserveCommand<Toggle::DecodableType>(context, "OnOff.Toggle", [](const Toggle::DecodableType &) {});
    ObserveCommand<OffWithEffect::DecodableType>(context, "OnOff.OffWithEffect", [](const OffWithEffect::DecodableType & command) {
        ChipLogProgress(Zcl, "  effectIdentifier=%u effectVariant=%u", static_cast<unsigned>(command.effectIdentifier),
                        static_cast<unsigned>(command.effectVariant));
    });
    ObserveCommand<OnWithRecallGlobalScene::DecodableType>(context, "OnOff.OnWithRecallGlobalScene",
                                                           [](const OnWithRecallGlobalScene::DecodableType &) {});
    ObserveCommand<OnWithTimedOff::DecodableType>(context, "OnOff.OnWithTimedOff", [](const OnWithTimedOff::DecodableType & command) {
        ChipLogProgress(Zcl, "  onOffControl=0x%02x onTime=%u offWaitTime=%u", static_cast<unsigned>(command.onOffControl.Raw()),
                        static_cast<unsigned>(command.onTime), static_cast<unsigned>(command.offWaitTime));
    });
}

void ObserveLevelControlCommands(CommandHandlerInterface::HandlerContext & context)
{
    using namespace LevelControl::Commands;

    ObserveCommand<MoveToLevel::DecodableType>(context, "LevelControl.MoveToLevel", [](const MoveToLevel::DecodableType & command) {
        ChipLogProgress(Zcl, "  level=%u transitionTime=%lu", static_cast<unsigned>(command.level),
                        static_cast<unsigned long>(NullableForLog(command.transitionTime)));
    });
    ObserveCommand<Move::DecodableType>(context, "LevelControl.Move", [](const Move::DecodableType & command) {
        ChipLogProgress(Zcl, "  moveMode=%u rate=%lu", static_cast<unsigned>(command.moveMode),
                        static_cast<unsigned long>(NullableForLog(command.rate)));
    });
    ObserveCommand<Step::DecodableType>(context, "LevelControl.Step", [](const Step::DecodableType & command) {
        ChipLogProgress(Zcl, "  stepMode=%u stepSize=%u transitionTime=%lu", static_cast<unsigned>(command.stepMode),
                        static_cast<unsigned>(command.stepSize), static_cast<unsigned long>(NullableForLog(command.transitionTime)));
    });
    ObserveCommand<Stop::DecodableType>(context, "LevelControl.Stop", [](const Stop::DecodableType &) {});
    ObserveCommand<MoveToLevelWithOnOff::DecodableType>(
        context, "LevelControl.MoveToLevelWithOnOff", [](const MoveToLevelWithOnOff::DecodableType & command) {
            ChipLogProgress(Zcl, "  level=%u transitionTime=%lu", static_cast<unsigned>(command.level),
                            static_cast<unsigned long>(NullableForLog(command.transitionTime)));
        });
    ObserveCommand<MoveWithOnOff::DecodableType>(context, "LevelControl.MoveWithOnOff",
                                                 [](const MoveWithOnOff::DecodableType & command) {
                                                     ChipLogProgress(Zcl, "  moveMode=%u rate=%lu",
                                                                     static_cast<unsigned>(command.moveMode),
                                                                     static_cast<unsigned long>(NullableForLog(command.rate)));
                                                 });
    ObserveCommand<StepWithOnOff::DecodableType>(
        context, "LevelControl.StepWithOnOff", [](const StepWithOnOff::DecodableType & command) {
            ChipLogProgress(Zcl, "  stepMode=%u stepSize=%u transitionTime=%lu", static_cast<unsigned>(command.stepMode),
                            static_cast<unsigned>(command.stepSize), static_cast<unsigned long>(NullableForLog(command.transitionTime)));
        });
    ObserveCommand<StopWithOnOff::DecodableType>(context, "LevelControl.StopWithOnOff",
                                                 [](const StopWithOnOff::DecodableType &) {});
    ObserveCommand<MoveToClosestFrequency::DecodableType>(
        context, "LevelControl.MoveToClosestFrequency", [](const MoveToClosestFrequency::DecodableType & command) {
            ChipLogProgress(Zcl, "  frequency=%u", static_cast<unsigned>(command.frequency));
        });
}

void ObserveColorControlCommands(CommandHandlerInterface::HandlerContext & context)
{
    using namespace ColorControl::Commands;

    ObserveCommand<MoveToHue::DecodableType>(context, "ColorControl.MoveToHue", [](const MoveToHue::DecodableType & command) {
        ChipLogProgress(Zcl, "  hue=%u direction=%u transitionTime=%u", static_cast<unsigned>(command.hue),
                        static_cast<unsigned>(command.direction), static_cast<unsigned>(command.transitionTime));
    });
    ObserveCommand<MoveHue::DecodableType>(context, "ColorControl.MoveHue", [](const MoveHue::DecodableType & command) {
        ChipLogProgress(Zcl, "  moveMode=%u rate=%u", static_cast<unsigned>(command.moveMode), static_cast<unsigned>(command.rate));
    });
    ObserveCommand<StepHue::DecodableType>(context, "ColorControl.StepHue", [](const StepHue::DecodableType & command) {
        ChipLogProgress(Zcl, "  stepMode=%u stepSize=%u transitionTime=%u", static_cast<unsigned>(command.stepMode),
                        static_cast<unsigned>(command.stepSize), static_cast<unsigned>(command.transitionTime));
    });
    ObserveCommand<MoveToSaturation::DecodableType>(
        context, "ColorControl.MoveToSaturation", [](const MoveToSaturation::DecodableType & command) {
            ChipLogProgress(Zcl, "  saturation=%u transitionTime=%u", static_cast<unsigned>(command.saturation),
                            static_cast<unsigned>(command.transitionTime));
        });
    ObserveCommand<MoveSaturation::DecodableType>(
        context, "ColorControl.MoveSaturation", [](const MoveSaturation::DecodableType & command) {
            ChipLogProgress(Zcl, "  moveMode=%u rate=%u", static_cast<unsigned>(command.moveMode),
                            static_cast<unsigned>(command.rate));
        });
    ObserveCommand<StepSaturation::DecodableType>(
        context, "ColorControl.StepSaturation", [](const StepSaturation::DecodableType & command) {
            ChipLogProgress(Zcl, "  stepMode=%u stepSize=%u transitionTime=%u", static_cast<unsigned>(command.stepMode),
                            static_cast<unsigned>(command.stepSize), static_cast<unsigned>(command.transitionTime));
        });
    ObserveCommand<MoveToHueAndSaturation::DecodableType>(
        context, "ColorControl.MoveToHueAndSaturation", [](const MoveToHueAndSaturation::DecodableType & command) {
            ChipLogProgress(Zcl, "  hue=%u saturation=%u transitionTime=%u", static_cast<unsigned>(command.hue),
                            static_cast<unsigned>(command.saturation), static_cast<unsigned>(command.transitionTime));
        });
    ObserveCommand<MoveToColor::DecodableType>(context, "ColorControl.MoveToColor", [](const MoveToColor::DecodableType & command) {
        ChipLogProgress(Zcl, "  colorX=%u colorY=%u transitionTime=%u", static_cast<unsigned>(command.colorX),
                        static_cast<unsigned>(command.colorY), static_cast<unsigned>(command.transitionTime));
    });
    ObserveCommand<MoveColor::DecodableType>(context, "ColorControl.MoveColor", [](const MoveColor::DecodableType & command) {
        ChipLogProgress(Zcl, "  rateX=%d rateY=%d", static_cast<int>(command.rateX), static_cast<int>(command.rateY));
    });
    ObserveCommand<StepColor::DecodableType>(context, "ColorControl.StepColor", [](const StepColor::DecodableType & command) {
        ChipLogProgress(Zcl, "  stepX=%d stepY=%d transitionTime=%u", static_cast<int>(command.stepX),
                        static_cast<int>(command.stepY), static_cast<unsigned>(command.transitionTime));
    });
    ObserveCommand<MoveToColorTemperature::DecodableType>(
        context, "ColorControl.MoveToColorTemperature", [](const MoveToColorTemperature::DecodableType & command) {
            ChipLogProgress(Zcl, "  colorTemperatureMireds=%u transitionTime=%u",
                            static_cast<unsigned>(command.colorTemperatureMireds), static_cast<unsigned>(command.transitionTime));
        });
    ObserveCommand<EnhancedMoveToHue::DecodableType>(
        context, "ColorControl.EnhancedMoveToHue", [](const EnhancedMoveToHue::DecodableType & command) {
            ChipLogProgress(Zcl, "  enhancedHue=%u direction=%u transitionTime=%u", static_cast<unsigned>(command.enhancedHue),
                            static_cast<unsigned>(command.direction), static_cast<unsigned>(command.transitionTime));
        });
    ObserveCommand<EnhancedMoveHue::DecodableType>(context, "ColorControl.EnhancedMoveHue",
                                                   [](const EnhancedMoveHue::DecodableType & command) {
                                                       ChipLogProgress(Zcl, "  moveMode=%u rate=%u",
                                                                       static_cast<unsigned>(command.moveMode),
                                                                       static_cast<unsigned>(command.rate));
                                                   });
    ObserveCommand<EnhancedStepHue::DecodableType>(
        context, "ColorControl.EnhancedStepHue", [](const EnhancedStepHue::DecodableType & command) {
            ChipLogProgress(Zcl, "  stepMode=%u stepSize=%u transitionTime=%u", static_cast<unsigned>(command.stepMode),
                            static_cast<unsigned>(command.stepSize), static_cast<unsigned>(command.transitionTime));
        });
    ObserveCommand<EnhancedMoveToHueAndSaturation::DecodableType>(
        context, "ColorControl.EnhancedMoveToHueAndSaturation",
        [](const EnhancedMoveToHueAndSaturation::DecodableType & command) {
            ChipLogProgress(Zcl, "  enhancedHue=%u saturation=%u transitionTime=%u", static_cast<unsigned>(command.enhancedHue),
                            static_cast<unsigned>(command.saturation), static_cast<unsigned>(command.transitionTime));
        });
    ObserveCommand<ColorLoopSet::DecodableType>(context, "ColorControl.ColorLoopSet", [](const ColorLoopSet::DecodableType & command) {
        ChipLogProgress(Zcl, "  updateFlags=0x%02x action=%u direction=%u time=%u startHue=%u",
                        static_cast<unsigned>(command.updateFlags.Raw()), static_cast<unsigned>(command.action),
                        static_cast<unsigned>(command.direction), static_cast<unsigned>(command.time),
                        static_cast<unsigned>(command.startHue));
    });
    ObserveCommand<StopMoveStep::DecodableType>(context, "ColorControl.StopMoveStep", [](const StopMoveStep::DecodableType &) {});
    ObserveCommand<MoveColorTemperature::DecodableType>(
        context, "ColorControl.MoveColorTemperature", [](const MoveColorTemperature::DecodableType & command) {
            ChipLogProgress(Zcl, "  moveMode=%u rate=%u minMireds=%u maxMireds=%u", static_cast<unsigned>(command.moveMode),
                            static_cast<unsigned>(command.rate), static_cast<unsigned>(command.colorTemperatureMinimumMireds),
                            static_cast<unsigned>(command.colorTemperatureMaximumMireds));
        });
    ObserveCommand<StepColorTemperature::DecodableType>(
        context, "ColorControl.StepColorTemperature", [](const StepColorTemperature::DecodableType & command) {
            ChipLogProgress(Zcl, "  stepMode=%u stepSize=%u transitionTime=%u minMireds=%u maxMireds=%u",
                            static_cast<unsigned>(command.stepMode), static_cast<unsigned>(command.stepSize),
                            static_cast<unsigned>(command.transitionTime),
                            static_cast<unsigned>(command.colorTemperatureMinimumMireds),
                            static_cast<unsigned>(command.colorTemperatureMaximumMireds));
        });
}

class LightCommandObserver : public CommandHandlerInterface
{
public:
    explicit LightCommandObserver(ClusterId clusterId) : CommandHandlerInterface(NullOptional, clusterId) {}

    void InvokeCommand(HandlerContext & context) override
    {
        switch (context.mRequestPath.mClusterId)
        {
        case OnOff::Id:
            ObserveOnOffCommands(context);
            break;
        case LevelControl::Id:
            ObserveLevelControlCommands(context);
            break;
        case ColorControl::Id:
            ObserveColorControlCommands(context);
            break;
        default:
            break;
        }
    }
};

LightCommandObserver sOnOffObserver(OnOff::Id);
LightCommandObserver sLevelControlObserver(LevelControl::Id);
LightCommandObserver sColorControlObserver(ColorControl::Id);
bool sRegistered = false;

} // namespace

CHIP_ERROR InitLightCommandObserver()
{
    VerifyOrReturnError(!sRegistered, CHIP_NO_ERROR);

    ReturnErrorOnFailure(CommandHandlerInterfaceRegistry::Instance().RegisterCommandHandler(&sOnOffObserver));
    ReturnErrorOnFailure(CommandHandlerInterfaceRegistry::Instance().RegisterCommandHandler(&sLevelControlObserver));
    ReturnErrorOnFailure(CommandHandlerInterfaceRegistry::Instance().RegisterCommandHandler(&sColorControlObserver));

    sRegistered = true;
    return CHIP_NO_ERROR;
}

void ShutdownLightCommandObserver()
{
    VerifyOrReturn(sRegistered);

    CommandHandlerInterfaceRegistry::Instance().UnregisterCommandHandler(&sColorControlObserver);
    CommandHandlerInterfaceRegistry::Instance().UnregisterCommandHandler(&sLevelControlObserver);
    CommandHandlerInterfaceRegistry::Instance().UnregisterCommandHandler(&sOnOffObserver);
    sRegistered = false;
}
