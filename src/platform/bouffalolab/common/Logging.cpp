/*
 *    Copyright (c) 2022 Project CHIP Authors
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
#include <stdio.h>
#include <string.h>

#include <platform/logging/LogV.h>

#include <lib/core/CHIPConfig.h>
#include <lib/support/logging/Constants.h>

#ifdef PW_RPC_ENABLED
#include "PigweedLogger.h"
#endif

#include <FreeRTOS.h>
#include <task.h>
#if ! CHIP_DEVICE_LAYER_TARGET_BL616
#include <utils_log.h>
#endif

namespace chip {
namespace Logging {
namespace Platform {

static char formattedMsg[CHIP_CONFIG_LOG_MESSAGE_MAX_SIZE];
void LogV(const char * module, uint8_t category, const char * msg, va_list v)
{
#ifndef PW_RPC_ENABLED
    vsnprintf(formattedMsg, sizeof(formattedMsg), msg, v);

    switch (category)
    {
    case kLogCategory_Error:
#if ! CHIP_DEVICE_LAYER_TARGET_BL616
        __utils_printf("[%10lu][%s][ERROR] %s\r\n", xTaskGetTickCount(), module, formattedMsg);
#else
        printf("[%10lu][%s][ERROR] %s\r\n", xTaskGetTickCount(), module, formattedMsg);
#endif
        break;
    case kLogCategory_Progress:
#if ! CHIP_DEVICE_LAYER_TARGET_BL616
        __utils_printf("[%10lu][%s][PROGR] %s\r\n", xTaskGetTickCount(), module, formattedMsg);
#else
        printf("[%10lu][%s][PROGR] %s\r\n", xTaskGetTickCount(), module, formattedMsg);
#endif
        break;
    case kLogCategory_Detail:
#if ! CHIP_DEVICE_LAYER_TARGET_BL616
        __utils_printf("[%10lu][%s][DETAIL] %s\r\n", xTaskGetTickCount(), module, formattedMsg);
#else
        printf("[%10lu][%s][DETAIL] %s\r\n", xTaskGetTickCount(), module, formattedMsg);
#endif
        break;
    }
#else
    size_t prefixLen = 0;

    switch (category)
    {
    case kLogCategory_Error:
        strcpy(formattedMsg, "[ERROR] ");
        break;
    case kLogCategory_Progress:
    default:
        strcpy(formattedMsg, "[INFO] ");
        break;
    case kLogCategory_Detail:
        strcpy(formattedMsg, "[TRACE] ");
        break;
    }

    prefixLen = strlen(formattedMsg);
    snprintf(formattedMsg + prefixLen, sizeof(formattedMsg) - prefixLen, "[%s] ", module);
    prefixLen = strlen(formattedMsg);
    vsnprintf(formattedMsg + prefixLen, sizeof(formattedMsg) - prefixLen, msg, v);

    PigweedLogger::putString(formattedMsg, strlen(formattedMsg));
    const char * newline = "\r\n";
    PigweedLogger::putString(newline, strlen(newline));
#endif
}

} // namespace Platform
} // namespace Logging
} // namespace chip
