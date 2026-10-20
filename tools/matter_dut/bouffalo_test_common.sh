#!/usr/bin/env bash
#
#    Copyright (c) 2026 Project CHIP Authors
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.

bouffalo_parse_args() {
    COMMISSION=0
    LIST_ONLY=0
    SELECTED=()

    for arg in "$@"; do
        case "$arg" in
            --commission) COMMISSION=1 ;;
            --list) LIST_ONLY=1 ;;
            --help|-h)
                awk '/^# 用法:/{f=1} f&&/^# 退出码/{print;exit} f{print}' "$0"
                exit 0
                ;;
            *) SELECTED+=("$arg") ;;
        esac
    done
}

bouffalo_init_common_config() {
    PICS_FILE="${PICS_FILE:-$CHIP_ROOT/src/app/tests/suites/certification/ci-pics-values}"
    BOUFFALO_TARGET="${BOUFFALO_TARGET:-bl616dk}"
    DISCRIMINATOR="${DISCRIMINATOR:-3840}"
    PASSCODE="${PASSCODE:-20202021}"
    NODE_ID="${NODE_ID:-0x12344321}"
    WIFI_SSID="${WIFI_SSID:-BF-OTA-AP}"
    WIFI_PASSPHRASE="${WIFI_PASSPHRASE:-12345678}"
    THREAD_DATASET="${THREAD_DATASET:-}"
    FIRMWARE_PATH="${FIRMWARE_PATH:-}"

    case "$BOUFFALO_TARGET" in
        bl602dk|bl706dk|bl616dk|bl616cldk) DEFAULT_TRANSPORT=wifi ;;
        bl704ldk) DEFAULT_TRANSPORT=thread ;;
        linux-x64-light) DEFAULT_TRANSPORT=ethernet ;;
        *)
            echo "[error] 不支持的 BOUFFALO_TARGET: $BOUFFALO_TARGET (可选: bl602dk bl706dk bl704ldk bl616dk bl616cldk linux-x64-light)"
            return 2
            ;;
    esac

    COMMISSION_TRANSPORT="${COMMISSION_TRANSPORT:-$DEFAULT_TRANSPORT}"
    case "$COMMISSION_TRANSPORT" in
        wifi|thread|ethernet) ;;
        *)
            echo "[error] COMMISSION_TRANSPORT 必须是 wifi、thread 或 ethernet"
            return 2
            ;;
    esac
}
