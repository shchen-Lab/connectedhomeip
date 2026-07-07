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

#pragma once

#include "LightUartProtocol.h"

#include <app-common/zap-generated/cluster-objects.h>

#include <cstddef>
#include <cstdint>

namespace chip {
namespace app {
namespace Clusters {
namespace LightUartCommandEncoder {

constexpr size_t kCommandPayloadMax = 128;

template <typename RequestT>
lu_status_t EncodeCommandFields(const RequestT &, lu_field_writer_t &) = delete;

lu_status_t EncodeCommandFields(const OnOff::Commands::Off::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const OnOff::Commands::On::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const OnOff::Commands::Toggle::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const OnOff::Commands::OffWithEffect::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const OnOff::Commands::OnWithRecallGlobalScene::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const OnOff::Commands::OnWithTimedOff::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const LevelControl::Commands::MoveToLevel::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const LevelControl::Commands::Move::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const LevelControl::Commands::Step::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const LevelControl::Commands::Stop::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const LevelControl::Commands::MoveToLevelWithOnOff::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const LevelControl::Commands::MoveWithOnOff::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const LevelControl::Commands::StepWithOnOff::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const LevelControl::Commands::StopWithOnOff::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const Identify::Commands::Identify::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const Identify::Commands::TriggerEffect::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveToHue::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveHue::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::StepHue::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveToSaturation::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveSaturation::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::StepSaturation::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveToHueAndSaturation::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveToColor::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveColor::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::StepColor::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveToColorTemperature::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::EnhancedMoveToHue::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::EnhancedMoveHue::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::EnhancedStepHue::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::EnhancedMoveToHueAndSaturation::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::ColorLoopSet::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::StopMoveStep::DecodableType & command, lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveColorTemperature::DecodableType & command,
                              lu_field_writer_t & writer);
lu_status_t EncodeCommandFields(const ColorControl::Commands::StepColorTemperature::DecodableType & command,
                              lu_field_writer_t & writer);

template <typename RequestT>
lu_status_t EncodeCommandPayload(const RequestT & request, uint8_t * payload, size_t payloadCap, uint16_t & payloadLen)
{
    if (payload == nullptr || payloadCap == 0 || payloadCap > UINT16_MAX)
    {
        return LU_ERR_ARG;
    }

    lu_field_writer_t writer;
    lu_field_writer_init(&writer, payload, payloadCap);

    lu_status_t status = EncodeCommandFields(request, writer);
    if (status == LU_OK)
    {
        payloadLen = static_cast<uint16_t>(writer.len);
    }
    return status;
}

} // namespace LightUartCommandEncoder
} // namespace Clusters
} // namespace app
} // namespace chip
