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

#include <app/util/attribute-storage-null-handling.h>
#include <lib/core/DataModelTypes.h>

#include <cstddef>
#include <cstdint>

class BridgeDevice
{
public:
    static constexpr size_t kNameSize     = 32;
    static constexpr size_t kLocationSize = 32;
    static constexpr size_t kUniqueIdSize = 32;

    static constexpr uint32_t kColorFeatureHueSaturation    = 0x00000001;
    static constexpr uint32_t kColorFeatureXY               = 0x00000008;
    static constexpr uint32_t kColorFeatureColorTemperature = 0x00000010;
    static constexpr uint32_t kExtendedColorFeatureMap =
        kColorFeatureHueSaturation | kColorFeatureXY | kColorFeatureColorTemperature;

    enum Changed_t
    {
        kChanged_Reachable = 0x01,
        kChanged_State     = 0x02,
        kChanged_Location  = 0x04,
        kChanged_Name      = 0x08,
        kChanged_Level     = 0x10,
        kChanged_Color     = 0x20,
    };

    using ChangeCallback = void (*)(BridgeDevice * device, Changed_t changeMask);

    BridgeDevice(const char * name, const char * location, const char * uniqueId, bool isLighting, bool hasLevel,
                 uint32_t colorFeatures);

    bool IsOn() const { return mOn; }
    bool IsReachable() const { return mReachable; }
    bool IsLighting() const { return mIsLighting; }
    bool HasLevel() const { return mHasLevel; }
    bool HasColor() const { return mColorFeatures != 0; }
    bool HasHueSaturation() const { return (mColorFeatures & kColorFeatureHueSaturation) != 0; }
    bool HasXY() const { return (mColorFeatures & kColorFeatureXY) != 0; }
    bool HasColorTemperature() const { return (mColorFeatures & kColorFeatureColorTemperature) != 0; }
    uint32_t GetColorFeatures() const { return mColorFeatures; }
    uint8_t GetLevel() const { return mLevel; }
    uint8_t GetOnLevel() const { return mOnLevel; }
    uint8_t GetHue() const { return mHue; }
    uint8_t GetSaturation() const { return mSaturation; }
    uint16_t GetCurrentX() const { return mCurrentX; }
    uint16_t GetCurrentY() const { return mCurrentY; }
    uint8_t GetColorMode() const { return mColorMode; }
    uint8_t GetEnhancedColorMode() const { return mEnhancedColorMode; }
    uint16_t GetColorTemperatureMireds() const { return mColorTemperatureMireds; }
    uint16_t GetStartUpColorTemperatureMireds() const { return mStartUpColorTemperatureMireds; }
    const char * GetName() const { return mName; }
    const char * GetUniqueId() const { return mUniqueId; }
    chip::EndpointId GetEndpointId() const { return mEndpointId; }

    void SetEndpointId(chip::EndpointId endpoint) { mEndpointId = endpoint; }
    void SetChangeCallback(ChangeCallback callback) { mChangeCallback = callback; }

    void SetOnOff(bool on);
    void SetReachable(bool reachable);
    void SetLevel(uint8_t level);
    void SetOnLevel(uint8_t level);
    void SetHue(uint8_t hue);
    void SetSaturation(uint8_t saturation);
    void SetCurrentX(uint16_t currentX);
    void SetCurrentY(uint16_t currentY);
    void SetColorTemperatureMireds(uint16_t temperatureMireds);
    void SetStartUpColorTemperatureMireds(uint16_t temperatureMireds);

private:
    void NotifyIfChanged(bool changed, Changed_t change);

    bool mOn                                = false;
    bool mReachable                         = false;
    bool mIsLighting                        = false;
    bool mHasLevel                          = false;
    uint32_t mColorFeatures                 = 0;
    uint8_t mLevel                          = 128;
    uint8_t mOnLevel                        = chip::app::NumericAttributeTraits<uint8_t>::kNullValue;
    uint8_t mHue                            = 0;
    uint8_t mSaturation                     = 0;
    uint16_t mColorTemperatureMireds        = 250;
    uint16_t mCurrentX                      = 0x616B;
    uint16_t mCurrentY                      = 0x607D;
    uint16_t mStartUpColorTemperatureMireds = chip::app::NumericAttributeTraits<uint16_t>::kNullValue;
    uint8_t mColorMode                      = 0;
    uint8_t mEnhancedColorMode              = 0;
    char mName[kNameSize]                   = {};
    char mLocation[kLocationSize]           = {};
    char mUniqueId[kUniqueIdSize]           = {};
    chip::EndpointId mEndpointId            = chip::kInvalidEndpointId;
    ChangeCallback mChangeCallback          = nullptr;
};
