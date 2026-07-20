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

#include "AppTask.h"
#include "BridgeApp.h"
#include "BridgeUart.h"

#include <platform/bouffalolab/common/DiagnosticDataProviderImpl.h>
#include <system/SystemClock.h>

#include <plat.h>

#if CONFIG_ENABLE_CHIP_SHELL
#include <ChipShellCollection.h>
#include <lib/shell/Engine.h>
#endif

#if CHIP_DEVICE_LAYER_TARGET_BFLB
#ifdef BOOT_PIN_RESET
extern "C" {
#include <bflb_gpio.h>
}
#endif
#else
extern "C" {
#include <hal_gpio.h>
#include <hosal_gpio.h>
}
#endif

using namespace ::chip;
using namespace ::chip::DeviceLayer;

#if CONFIG_ENABLE_CHIP_SHELL
using namespace chip::Shell;
#endif

#ifdef BOOT_PIN_RESET
namespace {

constexpr DynamicBridgeDeviceParams kButtonColorTemperatureDevice = { DynamicBridgeDeviceType::kColorTemperatureLight,
                                                                      "Color Temperature Light 1", "Bedroom", "bridge-ct-1" };
constexpr DynamicBridgeDeviceParams kButtonHueSaturationDevice    = { DynamicBridgeDeviceType::kHueSaturationLight, "HS Light 1",
                                                                      "Living Room", "bridge-hs-1" };
constexpr DynamicBridgeDeviceParams kButtonOnOffDevice1 = { DynamicBridgeDeviceType::kOnOffLight, "OnOff Light 1", "Living Room",
                                                            "bridge-onoff-1" };
constexpr DynamicBridgeDeviceParams kButtonOnOffDevice2 = { DynamicBridgeDeviceType::kOnOffLight, "OnOff Light 2", "Living Room",
                                                            "bridge-onoff-2" };

EndpointId gButtonColorTemperatureEndpoint = kInvalidEndpointId;
EndpointId gButtonHueSaturationEndpoint    = kInvalidEndpointId;
EndpointId gButtonOnOffEndpoint1           = kInvalidEndpointId;
EndpointId gButtonOnOffEndpoint2           = kInvalidEndpointId;

bool AreButtonBridgeDevicesAdded()
{
    return gButtonColorTemperatureEndpoint != kInvalidEndpointId || gButtonHueSaturationEndpoint != kInvalidEndpointId ||
        gButtonOnOffEndpoint1 != kInvalidEndpointId || gButtonOnOffEndpoint2 != kInvalidEndpointId;
}

CHIP_ERROR RemoveButtonBridgeDevicesLocked();

CHIP_ERROR AddButtonBridgeDevicesLocked()
{
    if (AreButtonBridgeDevicesAdded())
    {
        return CHIP_ERROR_INCORRECT_STATE;
    }

    CHIP_ERROR err = AddDynamicBridgeDeviceLocked(kButtonColorTemperatureDevice, gButtonColorTemperatureEndpoint);
    if (err != CHIP_NO_ERROR)
    {
        return err;
    }

    err = AddDynamicBridgeDeviceLocked(kButtonHueSaturationDevice, gButtonHueSaturationEndpoint);
    if (err != CHIP_NO_ERROR)
    {
        CHIP_ERROR rollbackError = RemoveButtonBridgeDevicesLocked();
        return rollbackError == CHIP_NO_ERROR ? err : rollbackError;
    }

    err = AddDynamicBridgeDeviceLocked(kButtonOnOffDevice1, gButtonOnOffEndpoint1);
    if (err != CHIP_NO_ERROR)
    {
        CHIP_ERROR rollbackError = RemoveButtonBridgeDevicesLocked();
        return rollbackError == CHIP_NO_ERROR ? err : rollbackError;
    }

    err = AddDynamicBridgeDeviceLocked(kButtonOnOffDevice2, gButtonOnOffEndpoint2);
    if (err != CHIP_NO_ERROR)
    {
        CHIP_ERROR rollbackError = RemoveButtonBridgeDevicesLocked();
        return rollbackError == CHIP_NO_ERROR ? err : rollbackError;
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR RemoveButtonBridgeDevicesLocked()
{
    CHIP_ERROR result = CHIP_NO_ERROR;

    auto removeDevice = [&result](EndpointId & endpoint) {
        if (endpoint == kInvalidEndpointId)
        {
            return;
        }

        CHIP_ERROR err = RemoveDynamicBridgeDeviceLocked(endpoint);
        if (err == CHIP_NO_ERROR || err == CHIP_ERROR_NOT_FOUND)
        {
            endpoint = kInvalidEndpointId;
        }
        else if (result == CHIP_NO_ERROR)
        {
            result = err;
        }
    };

    removeDevice(gButtonOnOffEndpoint2);
    removeDevice(gButtonOnOffEndpoint1);
    removeDevice(gButtonHueSaturationEndpoint);
    removeDevice(gButtonColorTemperatureEndpoint);

    return result;
}

} // namespace
#endif

AppTask AppTask::sAppTask;
StackType_t AppTask::appStack[APP_TASK_STACK_SIZE / sizeof(StackType_t)];
StaticTask_t AppTask::appTaskStruct;

void StartAppTask(void)
{
    GetAppTask().sAppTaskHandle =
        xTaskCreateStatic(GetAppTask().AppTaskMain, "bridge_app", MATTER_ARRAY_SIZE(GetAppTask().appStack), nullptr, 2,
                          GetAppTask().appStack, &GetAppTask().appTaskStruct);
    if (GetAppTask().sAppTaskHandle == nullptr)
    {
        ChipLogError(NotSpecified, "Failed to create app task");
        appError(APP_ERROR_EVENT_QUEUE_FAILED);
    }
}

#if CONFIG_ENABLE_CHIP_SHELL
#if CHIP_DEVICE_LAYER_TARGET_BFLB
void AppTask::StartAppShellTask()
{
    Engine::Root().Init();
    cmd_misc_init();
    Engine::Root().RunMainLoop();
}
#else
void AppTask::AppShellTask(void * args)
{
    Engine::Root().RunMainLoop();
}

void AppTask::StartAppShellTask()
{
    static TaskHandle_t shellTask;

    Engine::Root().Init();
    cmd_misc_init();
    xTaskCreate(AppTask::AppShellTask, "chip_shell", 1024 / sizeof(configSTACK_DEPTH_TYPE), nullptr, 2, &shellTask);
}
#endif
#endif

void AppTask::PostEvent(app_event_t event)
{
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPrioTaskWoken = pdFALSE;
        xTaskNotifyFromISR(sAppTaskHandle, event, eSetBits, &higherPrioTaskWoken);
        portYIELD_FROM_ISR(higherPrioTaskWoken);
    }
    else
    {
        xTaskNotify(sAppTaskHandle, event, eSetBits);
    }
}

void AppTask::AppTaskMain(void * pvParameter)
{
    app_event_t appEvent;

#ifdef BOOT_PIN_RESET
    ButtonInit();
#endif

    GetAppTask().sTimer =
        xTimerCreate("bridgeTmr", pdMS_TO_TICKS(APP_TIMER_EVENT_DEFAULT_ITVL), false, nullptr, AppTask::TimerCallback);
    if (GetAppTask().sTimer == nullptr)
    {
        ChipLogError(NotSpecified, "Failed to create bridge timer");
        appError(APP_ERROR_EVENT_QUEUE_FAILED);
    }

    ChipLogProgress(NotSpecified, "Starting Platform Manager Event Loop");
    CHIP_ERROR err = PlatformMgr().StartEventLoopTask();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "PlatformMgr().StartEventLoopTask() failed: %" CHIP_ERROR_FORMAT, err.Format());
        appError(err);
    }

    GetAppTask().PostEvent(APP_EVENT_TIMER);

    vTaskSuspend(nullptr);

    CHIP_ERROR uartErr = CHIP_NO_ERROR;
    PlatformMgr().LockChipStack();
    err = InitBridgeApp();
    if (err == CHIP_NO_ERROR)
    {
        uartErr = InitBridgeUart();
    }
    PlatformMgr().UnlockChipStack();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "InitBridgeApp() failed: %" CHIP_ERROR_FORMAT, err.Format());
        appError(err);
    }

    if (uartErr != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "InitBridgeUart() failed: %" CHIP_ERROR_FORMAT, uartErr.Format());
        appError(uartErr);
    }

    uint64_t currentHeapFree = 0;
    DiagnosticDataProviderImpl::GetDefaultInstance().GetCurrentHeapFree(currentHeapFree);
    ChipLogProgress(NotSpecified, "Bridge app task started, with SRAM heap %llu left",
                    static_cast<unsigned long long>(currentHeapFree));

    while (true)
    {
        appEvent                 = APP_EVENT_NONE;
        BaseType_t eventReceived = xTaskNotifyWait(0, APP_EVENT_ALL_MASK, reinterpret_cast<uint32_t *>(&appEvent), portMAX_DELAY);

        if (eventReceived)
        {
            PlatformMgr().LockChipStack();

            if (APP_EVENT_FACTORY_RESET & appEvent)
            {
                DeviceLayer::ConfigurationMgr().InitiateFactoryReset();
            }

            TimerEventHandler(appEvent);

            PlatformMgr().UnlockChipStack();
        }
    }
}

bool AppTask::StartTimer(void)
{
    if (xTimerIsTimerActive(GetAppTask().sTimer))
    {
        CancelTimer();
    }

    if (xTimerChangePeriod(GetAppTask().sTimer, pdMS_TO_TICKS(GetAppTask().mTimerIntvl), pdMS_TO_TICKS(100)) != pdPASS)
    {
        ChipLogProgress(NotSpecified, "Failed to update bridge timer");
    }

    return true;
}

void AppTask::CancelTimer(void)
{
    xTimerStop(GetAppTask().sTimer, 0);
}

void AppTask::TimerCallback(TimerHandle_t xTimer)
{
    GetAppTask().PostEvent(APP_EVENT_TIMER);
}

void AppTask::TimerEventHandler(app_event_t event)
{
#ifdef BOOT_PIN_RESET
    uint64_t pressedTime = 0;

    if (GetAppTask().mButtonPressedTime)
    {
        pressedTime = System::SystemClock().GetMonotonicMilliseconds64().count() - GetAppTask().mButtonPressedTime;
        if (!ButtonPressed())
        {
            if (pressedTime >= APP_BUTTON_PRESS_LONG)
            {
                GetAppTask().PostEvent(APP_EVENT_FACTORY_RESET);
            }
            else
            {
                const bool removeDevice = AreButtonBridgeDevicesAdded();
                CHIP_ERROR err          = removeDevice ? RemoveButtonBridgeDevicesLocked() : AddButtonBridgeDevicesLocked();
                if (err == CHIP_NO_ERROR)
                {
                    ChipLogProgress(NotSpecified, "Dynamic bridge devices %s", removeDevice ? "removed" : "added");
                }
                else
                {
                    ChipLogError(NotSpecified, "Failed to %s dynamic bridge devices: %" CHIP_ERROR_FORMAT,
                                 removeDevice ? "remove" : "add", err.Format());
                }
            }
            GetAppTask().mButtonPressedTime = 0;
        }
    }
    else if (ButtonPressed())
    {
        GetAppTask().mButtonPressedTime = System::SystemClock().GetMonotonicMilliseconds64().count();
    }
#endif

    StartTimer();
}

#ifdef BOOT_PIN_RESET
#if CHIP_DEVICE_LAYER_TARGET_BFLB
static struct bflb_device_s * sGpio = nullptr;

static void app_task_gpio_isr(int irq, void * arg)
{
    bool intstatus = bflb_gpio_get_intstatus(sGpio, BOOT_PIN_RESET);
    if (intstatus)
    {
        bflb_gpio_int_clear(sGpio, BOOT_PIN_RESET);
    }
    GetAppTask().ButtonEventHandler(arg);
}
#else
static hosal_gpio_dev_t gpio_key = { .port = BOOT_PIN_RESET, .config = INPUT_HIGH_IMPEDANCE, .priv = nullptr };
#endif

void AppTask::ButtonInit(void)
{
    GetAppTask().mButtonPressedTime = 0;

#if CHIP_DEVICE_LAYER_TARGET_BFLB
    sGpio = bflb_device_get_by_name("gpio");
    bflb_gpio_init(sGpio, BOOT_PIN_RESET, GPIO_INPUT);
    bflb_gpio_int_init(sGpio, BOOT_PIN_RESET, GPIO_INT_TRIG_MODE_SYNC_FALLING_RISING_EDGE);
    bflb_gpio_int_mask(sGpio, BOOT_PIN_RESET, false);
    bflb_irq_attach(sGpio->irq_num, app_task_gpio_isr, sGpio);
    bflb_irq_enable(sGpio->irq_num);
#else
    hosal_gpio_init(&gpio_key);
    hosal_gpio_irq_set(&gpio_key, HOSAL_IRQ_TRIG_POS_PULSE, GetAppTask().ButtonEventHandler, nullptr);
#endif
}

bool AppTask::ButtonPressed(void)
{
#if CHIP_DEVICE_LAYER_TARGET_BFLB
    return bflb_gpio_read(sGpio, BOOT_PIN_RESET);
#else
    uint8_t val = 1;
    hosal_gpio_input_get(&gpio_key, &val);
    return val == 1;
#endif
}

void AppTask::ButtonEventHandler(void * arg)
{
    if (ButtonPressed())
    {
        GetAppTask().PostEvent(APP_EVENT_BTN_ISR);
    }
}
#endif
