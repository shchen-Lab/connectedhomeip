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

#include <platform/CHIPDeviceLayer.h>

#include "FreeRTOS.h"
#include "mboard.h"
#include "timers.h"

using namespace ::chip;
using namespace ::chip::DeviceLayer;

#define APP_BUTTON_PRESSED_ITVL 50
#define APP_BUTTON_PRESS_LONG 4000
#define APP_TIMER_EVENT_DEFAULT_ITVL 1000

#define APP_ERROR_EVENT_QUEUE_FAILED CHIP_APPLICATION_ERROR(0x01)

class AppTask
{
public:
    friend AppTask & GetAppTask(void);

    enum app_event_t
    {
        APP_EVENT_NONE          = 0x00000000,
        APP_EVENT_TIMER         = 0x00000010,
        APP_EVENT_FACTORY_RESET = 0x00000040,
        APP_EVENT_BTN_ISR       = 0x00000100,

        APP_EVENT_ALL_MASK = APP_EVENT_TIMER | APP_EVENT_FACTORY_RESET | APP_EVENT_BTN_ISR,
    };

    void PostEvent(app_event_t event);

#ifdef BOOT_PIN_RESET
    static void ButtonEventHandler(void * arg);
#endif

private:
    friend void StartAppTask(void);
    friend PlatformManagerImpl;

    static bool StartTimer(void);
    static void CancelTimer(void);
    static void TimerEventHandler(app_event_t event);
    static void TimerCallback(TimerHandle_t xTimer);

#ifdef BOOT_PIN_RESET
    static void ButtonInit(void);
    static bool ButtonPressed(void);
#endif

    static void AppTaskMain(void * pvParameter);

    static void StartAppShellTask();
    static void AppShellTask(void * args);

    TaskHandle_t sAppTaskHandle = nullptr;
    TimerHandle_t sTimer        = nullptr;
    uint32_t mTimerIntvl        = APP_TIMER_EVENT_DEFAULT_ITVL;
    uint64_t mButtonPressedTime = 0;

    static StackType_t appStack[APP_TASK_STACK_SIZE / sizeof(StackType_t)];
    static StaticTask_t appTaskStruct;
    static AppTask sAppTask;
};

inline AppTask & GetAppTask(void)
{
    return AppTask::sAppTask;
}

void StartAppTask();
