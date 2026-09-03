/*
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

#ifdef __cplusplus
extern "C" {
#endif

enum app_dtim_hold_reason
{
    APP_DTIM_HOLD_STARTUP       = 1U << 0,
    APP_DTIM_HOLD_RECOVERY      = 1U << 1,
    APP_DTIM_HOLD_COMMISSIONING = 1U << 2,
};

void app_dtim_activity_notify(void);
void app_dtim_set_hold(enum app_dtim_hold_reason reason, bool hold);
int app_dtim_pm_check(void);

#ifdef __cplusplus
}
#endif
