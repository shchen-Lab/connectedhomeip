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

#include "LightUartCommandEncoder.h"

#include <app-common/zap-generated/cluster-objects.h>

namespace chip {
namespace app {
namespace Clusters {
namespace LightUartCommandEncoder {

namespace {

#define LU_TRY(expr)                                                                                                               \
    do                                                                                                                             \
    {                                                                                                                              \
        lu_status_t _status = (expr);                                                                                              \
        if (_status != LU_OK)                                                                                                      \
        {                                                                                                                          \
            return _status;                                                                                                        \
        }                                                                                                                          \
    } while (false)

template <typename EnumT>
uint8_t Enum8(EnumT value)
{
    return static_cast<uint8_t>(value);
}

template <typename BitmapT>
uint8_t Bitmap8(const BitmapT & value)
{
    return static_cast<uint8_t>(value.Raw());
}

uint8_t NullableBit(bool isNull, uint8_t bit)
{
    return isNull ? static_cast<uint8_t>(1u << bit) : 0u;
}

uint8_t NullableU8Value(const DataModel::Nullable<uint8_t> & value)
{
    return value.IsNull() ? 0u : value.Value();
}

uint16_t NullableU16Value(const DataModel::Nullable<uint16_t> & value)
{
    return value.IsNull() ? 0u : value.Value();
}

} // namespace

lu_status_t EncodePayload(const OnOff::Commands::Off::DecodableType &, lu_payload_writer_t &)
{
    return LU_OK;
}

lu_status_t EncodePayload(const OnOff::Commands::On::DecodableType &, lu_payload_writer_t &)
{
    return LU_OK;
}

lu_status_t EncodePayload(const OnOff::Commands::Toggle::DecodableType &, lu_payload_writer_t &)
{
    return LU_OK;
}

lu_status_t EncodePayload(const OnOff::Commands::OffWithEffect::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.effectIdentifier)));
    return lu_payload_add_u8(&writer, command.effectVariant);
}

lu_status_t EncodePayload(const OnOff::Commands::OnWithRecallGlobalScene::DecodableType &, lu_payload_writer_t &)
{
    return LU_OK;
}

lu_status_t EncodePayload(const OnOff::Commands::OnWithTimedOff::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.onOffControl)));
    LU_TRY(lu_payload_add_u16(&writer, command.onTime));
    return lu_payload_add_u16(&writer, command.offWaitTime);
}

lu_status_t EncodePayload(const LevelControl::Commands::MoveToLevel::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, NullableBit(command.transitionTime.IsNull(), 0)));
    LU_TRY(lu_payload_add_u8(&writer, command.level));
    LU_TRY(lu_payload_add_u16(&writer, NullableU16Value(command.transitionTime)));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const LevelControl::Commands::Move::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, NullableBit(command.rate.IsNull(), 0)));
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.moveMode)));
    LU_TRY(lu_payload_add_u8(&writer, NullableU8Value(command.rate)));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const LevelControl::Commands::Step::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, NullableBit(command.transitionTime.IsNull(), 0)));
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.stepMode)));
    LU_TRY(lu_payload_add_u8(&writer, command.stepSize));
    LU_TRY(lu_payload_add_u16(&writer, NullableU16Value(command.transitionTime)));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const LevelControl::Commands::Stop::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const LevelControl::Commands::MoveToLevelWithOnOff::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, NullableBit(command.transitionTime.IsNull(), 0)));
    LU_TRY(lu_payload_add_u8(&writer, command.level));
    LU_TRY(lu_payload_add_u16(&writer, NullableU16Value(command.transitionTime)));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const LevelControl::Commands::MoveWithOnOff::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, NullableBit(command.rate.IsNull(), 0)));
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.moveMode)));
    LU_TRY(lu_payload_add_u8(&writer, NullableU8Value(command.rate)));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const LevelControl::Commands::StepWithOnOff::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, NullableBit(command.transitionTime.IsNull(), 0)));
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.stepMode)));
    LU_TRY(lu_payload_add_u8(&writer, command.stepSize));
    LU_TRY(lu_payload_add_u16(&writer, NullableU16Value(command.transitionTime)));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const LevelControl::Commands::StopWithOnOff::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const LevelControl::Commands::MoveToClosestFrequency::DecodableType & command,
                          lu_payload_writer_t & writer)
{
    return lu_payload_add_u16(&writer, command.frequency);
}

lu_status_t EncodePayload(const Identify::Commands::Identify::DecodableType & command, lu_payload_writer_t & writer)
{
    return lu_payload_add_u16(&writer, command.identifyTime);
}

lu_status_t EncodePayload(const Identify::Commands::TriggerEffect::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.effectIdentifier)));
    return lu_payload_add_u8(&writer, Enum8(command.effectVariant));
}

lu_status_t EncodePayload(const ColorControl::Commands::MoveToHue::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, command.hue));
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.direction)));
    LU_TRY(lu_payload_add_u16(&writer, command.transitionTime));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::MoveHue::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.moveMode)));
    LU_TRY(lu_payload_add_u8(&writer, command.rate));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::StepHue::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.stepMode)));
    LU_TRY(lu_payload_add_u8(&writer, command.stepSize));
    LU_TRY(lu_payload_add_u8(&writer, command.transitionTime));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::MoveToSaturation::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, command.saturation));
    LU_TRY(lu_payload_add_u16(&writer, command.transitionTime));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::MoveSaturation::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.moveMode)));
    LU_TRY(lu_payload_add_u8(&writer, command.rate));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::StepSaturation::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.stepMode)));
    LU_TRY(lu_payload_add_u8(&writer, command.stepSize));
    LU_TRY(lu_payload_add_u8(&writer, command.transitionTime));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::MoveToHueAndSaturation::DecodableType & command,
                          lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, command.hue));
    LU_TRY(lu_payload_add_u8(&writer, command.saturation));
    LU_TRY(lu_payload_add_u16(&writer, command.transitionTime));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::MoveToColor::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u16(&writer, command.colorX));
    LU_TRY(lu_payload_add_u16(&writer, command.colorY));
    LU_TRY(lu_payload_add_u16(&writer, command.transitionTime));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::MoveColor::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_i16(&writer, command.rateX));
    LU_TRY(lu_payload_add_i16(&writer, command.rateY));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::StepColor::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_i16(&writer, command.stepX));
    LU_TRY(lu_payload_add_i16(&writer, command.stepY));
    LU_TRY(lu_payload_add_u16(&writer, command.transitionTime));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::MoveToColorTemperature::DecodableType & command,
                          lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u16(&writer, command.colorTemperatureMireds));
    LU_TRY(lu_payload_add_u16(&writer, command.transitionTime));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::EnhancedMoveToHue::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u16(&writer, command.enhancedHue));
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.direction)));
    LU_TRY(lu_payload_add_u16(&writer, command.transitionTime));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::EnhancedMoveHue::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.moveMode)));
    LU_TRY(lu_payload_add_u16(&writer, command.rate));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::EnhancedStepHue::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.stepMode)));
    LU_TRY(lu_payload_add_u16(&writer, command.stepSize));
    LU_TRY(lu_payload_add_u16(&writer, command.transitionTime));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::EnhancedMoveToHueAndSaturation::DecodableType & command,
                          lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u16(&writer, command.enhancedHue));
    LU_TRY(lu_payload_add_u8(&writer, command.saturation));
    LU_TRY(lu_payload_add_u16(&writer, command.transitionTime));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::ColorLoopSet::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.updateFlags)));
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.action)));
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.direction)));
    LU_TRY(lu_payload_add_u16(&writer, command.time));
    LU_TRY(lu_payload_add_u16(&writer, command.startHue));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::StopMoveStep::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::MoveColorTemperature::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.moveMode)));
    LU_TRY(lu_payload_add_u16(&writer, command.rate));
    LU_TRY(lu_payload_add_u16(&writer, command.colorTemperatureMinimumMireds));
    LU_TRY(lu_payload_add_u16(&writer, command.colorTemperatureMaximumMireds));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

lu_status_t EncodePayload(const ColorControl::Commands::StepColorTemperature::DecodableType & command, lu_payload_writer_t & writer)
{
    LU_TRY(lu_payload_add_u8(&writer, Enum8(command.stepMode)));
    LU_TRY(lu_payload_add_u16(&writer, command.stepSize));
    LU_TRY(lu_payload_add_u16(&writer, command.transitionTime));
    LU_TRY(lu_payload_add_u16(&writer, command.colorTemperatureMinimumMireds));
    LU_TRY(lu_payload_add_u16(&writer, command.colorTemperatureMaximumMireds));
    LU_TRY(lu_payload_add_u8(&writer, Bitmap8(command.optionsMask)));
    return lu_payload_add_u8(&writer, Bitmap8(command.optionsOverride));
}

#undef LU_TRY

} // namespace LightUartCommandEncoder
} // namespace Clusters
} // namespace app
} // namespace chip
