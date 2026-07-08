/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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

#include <app/clusters/identify-server/identify-server.h>

#include <app/server/Dnssd.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <platform/bouffalolab/common/BflbConfig.h>
#include <platform/bouffalolab/common/DiagnosticDataProviderImpl.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <system/SystemClock.h>

#if HEAP_MONITORING
#include "MemMonitoring.h"
#endif

#if CHIP_ENABLE_OPENTHREAD
#include <platform/OpenThread/OpenThreadUtils.h>
#include <platform/ThreadStackManager.h>
#include <platform/bouffalolab/common/ThreadStackManagerImpl.h>
#include <utils_list.h>
#endif

#if CONFIG_ENABLE_CHIP_SHELL
#include <ChipShellCollection.h>
#include <lib/shell/Engine.h>
#endif

#include <plat.h>

#include "AppTask.h"
#include "LightCommandObserver.h"
#include "LightUartBridge.h"

#if CHIP_DEVICE_LAYER_TARGET_BFLB
#ifdef BOOT_PIN_RESET
#include <bflb_gpio.h>
#endif
#else
extern "C" {
#include <bl_gpio.h>
#include <hal_gpio.h>
#include <hosal_gpio.h>
}
#endif

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceLayer;

#if CONFIG_ENABLE_CHIP_SHELL
using namespace chip::Shell;
#endif

namespace {

Identify sIdentify = {
    APP_LIGHT_ENDPOINT_ID,
    AppTask::IdentifyStartHandler,
    AppTask::IdentifyStopHandler,
    Clusters::Identify::IdentifyTypeEnum::kLightOutput,
};

} // namespace

AppTask AppTask::sAppTask;
StackType_t AppTask::appStack[APP_TASK_STACK_SIZE / sizeof(StackType_t)];
StaticTask_t AppTask::appTaskStruct;

void StartAppTask(void)
{
    GetAppTask().sAppTaskHandle =
        xTaskCreateStatic(GetAppTask().AppTaskMain, APP_TASK_NAME, MATTER_ARRAY_SIZE(GetAppTask().appStack), NULL,
                          APP_TASK_PRIORITY, GetAppTask().appStack, &GetAppTask().appTaskStruct);
    if (GetAppTask().sAppTaskHandle == NULL)
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

    xTaskCreate(AppTask::AppShellTask, "chip_shell", 1024 / sizeof(configSTACK_DEPTH_TYPE), NULL, APP_TASK_PRIORITY, &shellTask);
}
#endif
#endif

void AppTask::PostEvent(app_event_t event)
{
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPrioTaskWoken = pdFALSE;
        xTaskNotifyFromISR(sAppTaskHandle, event, eSetBits, &higherPrioTaskWoken);
    }
    else
    {
        xTaskNotify(sAppTaskHandle, event, eSetBits);
    }
}

void AppTask::AppTaskMain(void * pvParameter)
{
    app_event_t appEvent;
    uint64_t currentHeapFree = 0;

#ifdef BOOT_PIN_RESET
    ButtonInit();
#else
    uint32_t resetCnt = 0;
    Internal::BflbConfig::ReadConfigValue(APP_REBOOT_RESET_COUNT_KEY, resetCnt);
    resetCnt++;
    Internal::BflbConfig::WriteConfigValue(APP_REBOOT_RESET_COUNT_KEY, resetCnt);
    GetAppTask().mButtonPressedTime = System::SystemClock().GetMonotonicMilliseconds64().count();
    ChipLogProgress(NotSpecified, "AppTaskMain %llu, resetCnt %u", (unsigned long long) GetAppTask().mButtonPressedTime,
                    (unsigned int) resetCnt);
#endif

    GetAppTask().sTimer =
        xTimerCreate("lightTmr", pdMS_TO_TICKS(APP_TIMER_EVENT_DEFAULT_ITVL), false, NULL, AppTask::TimerCallback);
    if (GetAppTask().sTimer == NULL)
    {
        ChipLogError(NotSpecified, "Failed to create timer task");
        appError(APP_ERROR_EVENT_QUEUE_FAILED);
    }

    ChipLogProgress(NotSpecified, "Starting Platform Manager Event Loop");
    CHIP_ERROR ret = PlatformMgr().StartEventLoopTask();
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "PlatformMgr().StartEventLoopTask() failed");
        appError(ret);
    }

    ret = InitLightCommandObserver();
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "InitLightCommandObserver() failed");
        appError(ret);
    }

    ret = InitLightUartBridge();
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "InitLightUartBridge() failed");
        appError(ret);
    }

    GetAppTask().PostEvent(APP_EVENT_TIMER);

    vTaskSuspend(NULL);

    DiagnosticDataProviderImpl::GetDefaultInstance().GetCurrentHeapFree(currentHeapFree);
    ChipLogProgress(NotSpecified, "App Task started, with SRAM heap %lld left\r\n", currentHeapFree);

    while (true)
    {
        appEvent                 = APP_EVENT_NONE;
        BaseType_t eventReceived = xTaskNotifyWait(0, APP_EVENT_ALL_MASK, (uint32_t *) &appEvent, portMAX_DELAY);

        if (eventReceived)
        {
            PlatformMgr().LockChipStack();

#ifndef BOOT_PIN_RESET
            if (APP_EVENT_RESET_CNT & appEvent)
            {
                if (resetCnt >= APP_REBOOT_RESET_COUNT)
                {
                    GetAppTask().PostEvent(APP_EVENT_FACTORY_RESET);
                }
                ChipLogProgress(NotSpecified, "APP_REBOOT_RESET_COUNT_KEY resetCnt %ld", resetCnt);
                resetCnt = 0;
                Internal::BflbConfig::WriteConfigValue(APP_REBOOT_RESET_COUNT_KEY, resetCnt);
            }
#endif
            if (APP_EVENT_IDENTIFY_MASK & appEvent)
            {
                IdentifyHandleOp(appEvent);
            }

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

    if (GetAppTask().mTimerIntvl == 0)
    {
        GetAppTask().mTimerIntvl = APP_TIMER_EVENT_DEFAULT_ITVL;
    }

    if (xTimerChangePeriod(GetAppTask().sTimer, pdMS_TO_TICKS(GetAppTask().mTimerIntvl), pdMS_TO_TICKS(100)) != pdPASS)
    {
        ChipLogProgress(NotSpecified, "Failed to access timer with 100 ms delay.");
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
    uint32_t pressedTime = 0;

    if (GetAppTask().mButtonPressedTime)
    {
        pressedTime = System::SystemClock().GetMonotonicMilliseconds64().count() - GetAppTask().mButtonPressedTime;
        if (ButtonPressed())
        {
            if (pressedTime >= APP_BUTTON_PRESS_SHORT)
            {
                ChipLogProgress(NotSpecified, "Button long press pending");
            }
        }
        else
        {
            if (pressedTime >= APP_BUTTON_PRESS_LONG)
            {
                GetAppTask().PostEvent(APP_EVENT_FACTORY_RESET);
            }
            GetAppTask().mTimerIntvl        = APP_BUTTON_PRESSED_ITVL;
            GetAppTask().mButtonPressedTime = 0;
        }
    }
    else
    {
        if (ButtonPressed())
        {
            GetAppTask().mTimerIntvl        = APP_BUTTON_PRESSED_ITVL;
            GetAppTask().mButtonPressedTime = System::SystemClock().GetMonotonicMilliseconds64().count();
        }
    }
#else
    if (GetAppTask().mButtonPressedTime &&
        System::SystemClock().GetMonotonicMilliseconds64().count() - GetAppTask().mButtonPressedTime > APP_BUTTON_PRESS_LONG)
    {
        ChipLogProgress(NotSpecified, "Factory reset confirm timeout");
        /** factory reset confirm timeout */
        GetAppTask().mButtonPressedTime = 0;
        GetAppTask().PostEvent(APP_EVENT_RESET_CNT);
    }
#endif

    StartTimer();
}

void AppTask::IdentifyStartHandler(Identify *)
{
    GetAppTask().PostEvent(APP_EVENT_IDENTIFY_START);
}

void AppTask::IdentifyStopHandler(Identify *)
{
    GetAppTask().PostEvent(APP_EVENT_IDENTIFY_STOP);
}

void AppTask::IdentifyHandleOp(app_event_t event)
{
    static uint32_t identifyState = 0;

    if (APP_EVENT_IDENTIFY_START & event)
    {
        identifyState = 1;
        ChipLogProgress(NotSpecified, "identify start");
    }

    if ((APP_EVENT_IDENTIFY_IDENTIFY & event) && identifyState)
    {
        ChipLogProgress(NotSpecified, "identify");
    }

    if (APP_EVENT_IDENTIFY_STOP & event)
    {
        identifyState = 0;
        ChipLogProgress(NotSpecified, "identify stop");
    }
}

void AppTask::ButtonEventHandler(uint8_t btnIdx, uint8_t btnAction)
{
    GetAppTask().PostEvent(APP_EVENT_FACTORY_RESET);
}

#ifdef BOOT_PIN_RESET
#if CHIP_DEVICE_LAYER_TARGET_BFLB
static struct bflb_device_s * app_task_gpio_var = NULL;
static void app_task_gpio_isr(int irq, void * arg)
{
    bool intstatus = bflb_gpio_get_intstatus(app_task_gpio_var, BOOT_PIN_RESET);
    if (intstatus)
    {
        bflb_gpio_int_clear(app_task_gpio_var, BOOT_PIN_RESET);
    }

    GetAppTask().ButtonEventHandler(arg);
}
#else
static hosal_gpio_dev_t gpio_key = { .port = BOOT_PIN_RESET, .config = INPUT_HIGH_IMPEDANCE, .priv = NULL };
#endif

void AppTask::ButtonInit(void)
{
    GetAppTask().mButtonPressedTime = 0;

#if CHIP_DEVICE_LAYER_TARGET_BFLB
    app_task_gpio_var = bflb_device_get_by_name("gpio");

    bflb_gpio_init(app_task_gpio_var, BOOT_PIN_RESET, GPIO_INPUT);
    bflb_gpio_int_init(app_task_gpio_var, BOOT_PIN_RESET, GPIO_INT_TRIG_MODE_SYNC_FALLING_RISING_EDGE);
    bflb_gpio_int_mask(app_task_gpio_var, BOOT_PIN_RESET, false);

    bflb_irq_attach(app_task_gpio_var->irq_num, app_task_gpio_isr, app_task_gpio_var);
    bflb_irq_enable(app_task_gpio_var->irq_num);
#else
    hosal_gpio_init(&gpio_key);
    hosal_gpio_irq_set(&gpio_key, HOSAL_IRQ_TRIG_POS_PULSE, GetAppTask().ButtonEventHandler, NULL);
#endif
}

bool AppTask::ButtonPressed(void)
{
#if CHIP_DEVICE_LAYER_TARGET_BFLB
    return bflb_gpio_read(app_task_gpio_var, BOOT_PIN_RESET);
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
