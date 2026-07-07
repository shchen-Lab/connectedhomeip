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

#include "LightUartBridge.h"
#include "LightUartCommandEncoder.h"

#include <app/clusters/identify-server/IdentifyCommandObserver.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace {

template <typename CommandT>
void ForwardIdentifyCommandToUart(EndpointId endpoint, CommandId commandId, const char * name, const CommandT & command)
{
    uint8_t payload[LightUartCommandEncoder::kCommandPayloadMax];
    uint16_t payloadLen = 0;

    lu_status_t status = LightUartCommandEncoder::EncodeCommandPayload(command, payload, sizeof(payload), payloadLen);
    if (status != LU_OK)
    {
        ChipLogError(Zcl, "Failed to encode UART light command %s: %d", name, static_cast<int>(status));
        return;
    }

    ChipLogProgress(Zcl, "Observed light command: ep %u cluster " ChipLogFormatMEI " command " ChipLogFormatMEI " %s",
                    endpoint, ChipLogValueMEI(Identify::Id), ChipLogValueMEI(commandId), name);

    CHIP_ERROR err = LightUartBridgeSendCommand(endpoint, Identify::Id, commandId, payload, payloadLen);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(Zcl, "Failed to send UART light command %s: %" CHIP_ERROR_FORMAT, name, err.Format());
    }
}

} // namespace

namespace chip {
namespace app {
namespace Clusters {

void MatterIdentifyCommandReceivedCallback(EndpointId endpoint, const Identify::Commands::Identify::DecodableType & command)
{
    ForwardIdentifyCommandToUart(endpoint, Identify::Commands::Identify::Id, "Identify.Identify", command);
    ChipLogProgress(Zcl, "  identifyTime=%u", static_cast<unsigned>(command.identifyTime));
}

void MatterIdentifyTriggerEffectCommandReceivedCallback(EndpointId endpoint,
                                                        const Identify::Commands::TriggerEffect::DecodableType & command)
{
    ForwardIdentifyCommandToUart(endpoint, Identify::Commands::TriggerEffect::Id, "Identify.TriggerEffect", command);
    ChipLogProgress(Zcl, "  effectIdentifier=%u effectVariant=%u", static_cast<unsigned>(command.effectIdentifier),
                    static_cast<unsigned>(command.effectVariant));
}

} // namespace Clusters
} // namespace app
} // namespace chip
