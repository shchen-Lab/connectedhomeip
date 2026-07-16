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

#include "BridgeDevice.h"

#include <lib/support/CHIPMemString.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

using namespace chip;
using namespace chip::DeviceLayer;

BridgeDevice::BridgeDevice(const char * name, const char * location, const char * uniqueId, bool isLighting, bool hasLevel,
                           uint32_t colorFeatures) : mIsLighting(isLighting), mHasLevel(hasLevel), mColorFeatures(colorFeatures)
{
    Platform::CopyString(mName, sizeof(mName), name);
    Platform::CopyString(mLocation, sizeof(mLocation), location);
    Platform::CopyString(mUniqueId, sizeof(mUniqueId), uniqueId);
    if (!HasHueSaturation() && !HasXY() && HasColorTemperature())
    {
        mColorMode         = kColorModeColorTemperature;
        mEnhancedColorMode = kColorModeColorTemperature;
    }
}

void BridgeDevice::SetOnOff(bool on)
{
    const bool changed = (mOn != on);
    mOn                = on;
    ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: %s", mName, mOn ? "ON" : "OFF");
    NotifyIfChanged(changed, kChanged_OnOffState);
}

void BridgeDevice::SetReachable(bool reachable)
{
    const bool changed = (mReachable != reachable);
    mReachable         = reachable;
    ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: %s", mName, mReachable ? "ONLINE" : "OFFLINE");
    NotifyIfChanged(changed, kChanged_Reachable);
}

void BridgeDevice::SetLevel(uint8_t level)
{
    VerifyOrReturn(mHasLevel);
    const bool changed = (mLevel != level);
    mLevel             = level;
    ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: level %u", mName, mLevel);
    NotifyIfChanged(changed, kChanged_Level);
}

void BridgeDevice::SetOnLevel(uint8_t level)
{
    VerifyOrReturn(mHasLevel);
    mOnLevel = level;
    ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: on-level %u", mName, mOnLevel);
}

void BridgeDevice::SetHue(uint8_t hue)
{
    VerifyOrReturn(HasHueSaturation());
    const bool changed = (mHue != hue || mColorMode != kColorModeHueSaturation || mEnhancedColorMode != kColorModeHueSaturation);
    mHue               = hue;
    mColorMode         = kColorModeHueSaturation;
    mEnhancedColorMode = kColorModeHueSaturation;
    ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: hue %u", mName, mHue);
    NotifyIfChanged(changed, kChanged_Color);
}

void BridgeDevice::SetSaturation(uint8_t saturation)
{
    VerifyOrReturn(HasHueSaturation());
    const bool changed = (mSaturation != saturation || mColorMode != kColorModeHueSaturation || mEnhancedColorMode != kColorModeHueSaturation);
    mSaturation        = saturation;
    mColorMode         = kColorModeHueSaturation;
    mEnhancedColorMode = kColorModeHueSaturation;
    ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: saturation %u", mName, mSaturation);
    NotifyIfChanged(changed, kChanged_Color);
}

void BridgeDevice::SetCurrentX(uint16_t currentX)
{
    VerifyOrReturn(HasXY());
    const bool changed = (mCurrentX != currentX || mColorMode != kColorModeCurrentXy || mEnhancedColorMode != kColorModeCurrentXy);
    mCurrentX          = currentX;
    mColorMode         = kColorModeCurrentXy;
    mEnhancedColorMode = kColorModeCurrentXy;
    ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: current X %u", mName, mCurrentX);
    NotifyIfChanged(changed, kChanged_Color);
}

void BridgeDevice::SetCurrentY(uint16_t currentY)
{
    VerifyOrReturn(HasXY());
    const bool changed = (mCurrentY != currentY || mColorMode != kColorModeCurrentXy || mEnhancedColorMode != kColorModeCurrentXy);
    mCurrentY          = currentY;
    mColorMode         = kColorModeCurrentXy;
    mEnhancedColorMode = kColorModeCurrentXy;
    ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: current Y %u", mName, mCurrentY);
    NotifyIfChanged(changed, kChanged_Color);
}

void BridgeDevice::SetColorTemperatureMireds(uint16_t temperatureMireds)
{
    VerifyOrReturn(HasColorTemperature());
    const bool changed      = (mColorTemperatureMireds != temperatureMireds || mColorMode != kColorModeColorTemperature || mEnhancedColorMode != kColorModeColorTemperature);
    mColorTemperatureMireds = temperatureMireds;
    mColorMode              = kColorModeColorTemperature;
    mEnhancedColorMode      = kColorModeColorTemperature;
    ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: color temperature %u mireds", mName, mColorTemperatureMireds);
    NotifyIfChanged(changed, kChanged_Color);
}

void BridgeDevice::SetStartUpColorTemperatureMireds(uint16_t temperatureMireds)
{
    VerifyOrReturn(HasColorTemperature());
    mStartUpColorTemperatureMireds = temperatureMireds;
    ChipLogProgress(DeviceLayer, "BridgeDevice[%s]: startup color temperature %u mireds", mName, mStartUpColorTemperatureMireds);
}

void BridgeDevice::NotifyIfChanged(bool changed, Changed_t change)
{
    if (changed && mChangeCallback != nullptr)
    {
        mChangeCallback(this, change);
    }
}
