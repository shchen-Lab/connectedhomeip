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

#define LU_TRY(expr)                                                                                                      \
    do                                                                                                                    \
    {                                                                                                                     \
        lu_status_t _status = (expr);                                                                                     \
        if (_status != LU_OK)                                                                                             \
        {                                                                                                                 \
            return _status;                                                                                               \
        }                                                                                                                 \
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

template <typename FieldT>
uint8_t FieldId(FieldT field)
{
    return static_cast<uint8_t>(field);
}

lu_status_t AddNullableU8(lu_field_writer_t & writer, uint8_t fieldId, const DataModel::Nullable<uint8_t> & value)
{
    return value.IsNull() ? lu_field_add_null(&writer, fieldId, LU_VT_U8) : lu_field_add_u8(&writer, fieldId, value.Value());
}

lu_status_t AddNullableU16(lu_field_writer_t & writer, uint8_t fieldId, const DataModel::Nullable<uint16_t> & value)
{
    return value.IsNull() ? lu_field_add_null(&writer, fieldId, LU_VT_U16) : lu_field_add_u16(&writer, fieldId, value.Value());
}

} // namespace

lu_status_t EncodeCommandFields(const OnOff::Commands::Off::DecodableType &, lu_field_writer_t &)
{
    return LU_OK;
}

lu_status_t EncodeCommandFields(const OnOff::Commands::On::DecodableType &, lu_field_writer_t &)
{
    return LU_OK;
}

lu_status_t EncodeCommandFields(const OnOff::Commands::Toggle::DecodableType &, lu_field_writer_t &)
{
    return LU_OK;
}

lu_status_t EncodeCommandFields(const OnOff::Commands::OffWithEffect::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(OnOff::Commands::OffWithEffect::Fields::kEffectIdentifier),
                              Enum8(command.effectIdentifier)));
    return lu_field_add_enum8(&writer, FieldId(OnOff::Commands::OffWithEffect::Fields::kEffectVariant), command.effectVariant);
}

lu_status_t EncodeCommandFields(const OnOff::Commands::OnWithRecallGlobalScene::DecodableType &, lu_field_writer_t &)
{
    return LU_OK;
}

lu_status_t EncodeCommandFields(const OnOff::Commands::OnWithTimedOff::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(OnOff::Commands::OnWithTimedOff::Fields::kOnOffControl),
                                Bitmap8(command.onOffControl)));
    LU_TRY(lu_field_add_u16(&writer, FieldId(OnOff::Commands::OnWithTimedOff::Fields::kOnTime), command.onTime));
    return lu_field_add_u16(&writer, FieldId(OnOff::Commands::OnWithTimedOff::Fields::kOffWaitTime), command.offWaitTime);
}

lu_status_t EncodeCommandFields(const LevelControl::Commands::MoveToLevel::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_u8(&writer, FieldId(LevelControl::Commands::MoveToLevel::Fields::kLevel), command.level));
    LU_TRY(AddNullableU16(writer, FieldId(LevelControl::Commands::MoveToLevel::Fields::kTransitionTime), command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::MoveToLevel::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::MoveToLevel::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const LevelControl::Commands::Move::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(LevelControl::Commands::Move::Fields::kMoveMode), Enum8(command.moveMode)));
    LU_TRY(AddNullableU8(writer, FieldId(LevelControl::Commands::Move::Fields::kRate), command.rate));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::Move::Fields::kOptionsMask), Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::Move::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const LevelControl::Commands::Step::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(LevelControl::Commands::Step::Fields::kStepMode), Enum8(command.stepMode)));
    LU_TRY(lu_field_add_u8(&writer, FieldId(LevelControl::Commands::Step::Fields::kStepSize), command.stepSize));
    LU_TRY(AddNullableU16(writer, FieldId(LevelControl::Commands::Step::Fields::kTransitionTime), command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::Step::Fields::kOptionsMask), Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::Step::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const LevelControl::Commands::Stop::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::Stop::Fields::kOptionsMask), Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::Stop::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const LevelControl::Commands::MoveToLevelWithOnOff::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_u8(&writer, FieldId(LevelControl::Commands::MoveToLevelWithOnOff::Fields::kLevel), command.level));
    LU_TRY(AddNullableU16(writer, FieldId(LevelControl::Commands::MoveToLevelWithOnOff::Fields::kTransitionTime),
                          command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::MoveToLevelWithOnOff::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::MoveToLevelWithOnOff::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const LevelControl::Commands::MoveWithOnOff::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(LevelControl::Commands::MoveWithOnOff::Fields::kMoveMode),
                              Enum8(command.moveMode)));
    LU_TRY(AddNullableU8(writer, FieldId(LevelControl::Commands::MoveWithOnOff::Fields::kRate), command.rate));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::MoveWithOnOff::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::MoveWithOnOff::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const LevelControl::Commands::StepWithOnOff::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(LevelControl::Commands::StepWithOnOff::Fields::kStepMode),
                              Enum8(command.stepMode)));
    LU_TRY(lu_field_add_u8(&writer, FieldId(LevelControl::Commands::StepWithOnOff::Fields::kStepSize), command.stepSize));
    LU_TRY(AddNullableU16(writer, FieldId(LevelControl::Commands::StepWithOnOff::Fields::kTransitionTime),
                          command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::StepWithOnOff::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::StepWithOnOff::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const LevelControl::Commands::StopWithOnOff::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::StopWithOnOff::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(LevelControl::Commands::StopWithOnOff::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const Identify::Commands::Identify::DecodableType & command, lu_field_writer_t & writer)
{
    return lu_field_add_u16(&writer, FieldId(Identify::Commands::Identify::Fields::kIdentifyTime), command.identifyTime);
}

lu_status_t EncodeCommandFields(const Identify::Commands::TriggerEffect::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(Identify::Commands::TriggerEffect::Fields::kEffectIdentifier),
                              Enum8(command.effectIdentifier)));
    return lu_field_add_enum8(&writer, FieldId(Identify::Commands::TriggerEffect::Fields::kEffectVariant),
                              Enum8(command.effectVariant));
}


lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveToHue::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_u8(&writer, FieldId(ColorControl::Commands::MoveToHue::Fields::kHue), command.hue));
    LU_TRY(lu_field_add_enum8(&writer, FieldId(ColorControl::Commands::MoveToHue::Fields::kDirection), Enum8(command.direction)));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::MoveToHue::Fields::kTransitionTime), command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveToHue::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveToHue::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveHue::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(ColorControl::Commands::MoveHue::Fields::kMoveMode), Enum8(command.moveMode)));
    LU_TRY(lu_field_add_u8(&writer, FieldId(ColorControl::Commands::MoveHue::Fields::kRate), command.rate));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveHue::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveHue::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::StepHue::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(ColorControl::Commands::StepHue::Fields::kStepMode), Enum8(command.stepMode)));
    LU_TRY(lu_field_add_u8(&writer, FieldId(ColorControl::Commands::StepHue::Fields::kStepSize), command.stepSize));
    LU_TRY(lu_field_add_u8(&writer, FieldId(ColorControl::Commands::StepHue::Fields::kTransitionTime), command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::StepHue::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::StepHue::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveToSaturation::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_u8(&writer, FieldId(ColorControl::Commands::MoveToSaturation::Fields::kSaturation), command.saturation));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::MoveToSaturation::Fields::kTransitionTime),
                            command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveToSaturation::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveToSaturation::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveSaturation::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(ColorControl::Commands::MoveSaturation::Fields::kMoveMode),
                              Enum8(command.moveMode)));
    LU_TRY(lu_field_add_u8(&writer, FieldId(ColorControl::Commands::MoveSaturation::Fields::kRate), command.rate));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveSaturation::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveSaturation::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::StepSaturation::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(ColorControl::Commands::StepSaturation::Fields::kStepMode),
                              Enum8(command.stepMode)));
    LU_TRY(lu_field_add_u8(&writer, FieldId(ColorControl::Commands::StepSaturation::Fields::kStepSize), command.stepSize));
    LU_TRY(lu_field_add_u8(&writer, FieldId(ColorControl::Commands::StepSaturation::Fields::kTransitionTime),
                           command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::StepSaturation::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::StepSaturation::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveToHueAndSaturation::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_u8(&writer, FieldId(ColorControl::Commands::MoveToHueAndSaturation::Fields::kHue), command.hue));
    LU_TRY(lu_field_add_u8(&writer, FieldId(ColorControl::Commands::MoveToHueAndSaturation::Fields::kSaturation),
                           command.saturation));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::MoveToHueAndSaturation::Fields::kTransitionTime),
                            command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveToHueAndSaturation::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveToHueAndSaturation::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveToColor::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::MoveToColor::Fields::kColorX), command.colorX));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::MoveToColor::Fields::kColorY), command.colorY));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::MoveToColor::Fields::kTransitionTime),
                            command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveToColor::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveToColor::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveColor::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_i16(&writer, FieldId(ColorControl::Commands::MoveColor::Fields::kRateX), command.rateX));
    LU_TRY(lu_field_add_i16(&writer, FieldId(ColorControl::Commands::MoveColor::Fields::kRateY), command.rateY));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveColor::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveColor::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::StepColor::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_i16(&writer, FieldId(ColorControl::Commands::StepColor::Fields::kStepX), command.stepX));
    LU_TRY(lu_field_add_i16(&writer, FieldId(ColorControl::Commands::StepColor::Fields::kStepY), command.stepY));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::StepColor::Fields::kTransitionTime), command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::StepColor::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::StepColor::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveToColorTemperature::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::MoveToColorTemperature::Fields::kColorTemperatureMireds),
                            command.colorTemperatureMireds));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::MoveToColorTemperature::Fields::kTransitionTime),
                            command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveToColorTemperature::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveToColorTemperature::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::EnhancedMoveToHue::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::EnhancedMoveToHue::Fields::kEnhancedHue),
                            command.enhancedHue));
    LU_TRY(lu_field_add_enum8(&writer, FieldId(ColorControl::Commands::EnhancedMoveToHue::Fields::kDirection),
                              Enum8(command.direction)));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::EnhancedMoveToHue::Fields::kTransitionTime),
                            command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::EnhancedMoveToHue::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::EnhancedMoveToHue::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::EnhancedMoveHue::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(ColorControl::Commands::EnhancedMoveHue::Fields::kMoveMode),
                              Enum8(command.moveMode)));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::EnhancedMoveHue::Fields::kRate), command.rate));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::EnhancedMoveHue::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::EnhancedMoveHue::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::EnhancedStepHue::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(ColorControl::Commands::EnhancedStepHue::Fields::kStepMode),
                              Enum8(command.stepMode)));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::EnhancedStepHue::Fields::kStepSize), command.stepSize));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::EnhancedStepHue::Fields::kTransitionTime),
                            command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::EnhancedStepHue::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::EnhancedStepHue::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::EnhancedMoveToHueAndSaturation::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::EnhancedMoveToHueAndSaturation::Fields::kEnhancedHue),
                            command.enhancedHue));
    LU_TRY(lu_field_add_u8(&writer, FieldId(ColorControl::Commands::EnhancedMoveToHueAndSaturation::Fields::kSaturation),
                           command.saturation));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::EnhancedMoveToHueAndSaturation::Fields::kTransitionTime),
                            command.transitionTime));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::EnhancedMoveToHueAndSaturation::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::EnhancedMoveToHueAndSaturation::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::ColorLoopSet::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::ColorLoopSet::Fields::kUpdateFlags),
                                Bitmap8(command.updateFlags)));
    LU_TRY(lu_field_add_enum8(&writer, FieldId(ColorControl::Commands::ColorLoopSet::Fields::kAction), Enum8(command.action)));
    LU_TRY(lu_field_add_enum8(&writer, FieldId(ColorControl::Commands::ColorLoopSet::Fields::kDirection),
                              Enum8(command.direction)));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::ColorLoopSet::Fields::kTime), command.time));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::ColorLoopSet::Fields::kStartHue), command.startHue));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::ColorLoopSet::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::ColorLoopSet::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::StopMoveStep::DecodableType & command, lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::StopMoveStep::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::StopMoveStep::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::MoveColorTemperature::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(ColorControl::Commands::MoveColorTemperature::Fields::kMoveMode),
                              Enum8(command.moveMode)));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::MoveColorTemperature::Fields::kRate), command.rate));
    LU_TRY(lu_field_add_u16(&writer,
                            FieldId(ColorControl::Commands::MoveColorTemperature::Fields::kColorTemperatureMinimumMireds),
                            command.colorTemperatureMinimumMireds));
    LU_TRY(lu_field_add_u16(&writer,
                            FieldId(ColorControl::Commands::MoveColorTemperature::Fields::kColorTemperatureMaximumMireds),
                            command.colorTemperatureMaximumMireds));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveColorTemperature::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::MoveColorTemperature::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

lu_status_t EncodeCommandFields(const ColorControl::Commands::StepColorTemperature::DecodableType & command,
                              lu_field_writer_t & writer)
{
    LU_TRY(lu_field_add_enum8(&writer, FieldId(ColorControl::Commands::StepColorTemperature::Fields::kStepMode),
                              Enum8(command.stepMode)));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::StepColorTemperature::Fields::kStepSize), command.stepSize));
    LU_TRY(lu_field_add_u16(&writer, FieldId(ColorControl::Commands::StepColorTemperature::Fields::kTransitionTime),
                            command.transitionTime));
    LU_TRY(lu_field_add_u16(&writer,
                            FieldId(ColorControl::Commands::StepColorTemperature::Fields::kColorTemperatureMinimumMireds),
                            command.colorTemperatureMinimumMireds));
    LU_TRY(lu_field_add_u16(&writer,
                            FieldId(ColorControl::Commands::StepColorTemperature::Fields::kColorTemperatureMaximumMireds),
                            command.colorTemperatureMaximumMireds));
    LU_TRY(lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::StepColorTemperature::Fields::kOptionsMask),
                                Bitmap8(command.optionsMask)));
    return lu_field_add_bitmap8(&writer, FieldId(ColorControl::Commands::StepColorTemperature::Fields::kOptionsOverride),
                                Bitmap8(command.optionsOverride));
}

#undef LU_TRY

} // namespace LightUartCommandEncoder
} // namespace Clusters
} // namespace app
} // namespace chip
