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
#
# BL616DK Wi-Fi lighting —— python_testing 通用测试集一键执行脚本
#
# 用法:
#   ./scripts/tests/bl616_sanity_tests.sh                    # 对已配网设备直接 CASE 直连测试(全部用例)
#   ./scripts/tests/bl616_sanity_tests.sh --commission       # 先 BLE+Wi-Fi 配网再测试(需设备处于出厂/配网广播状态)
#   ./scripts/tests/bl616_sanity_tests.sh TC_CC_2_1 TC_ACL_2_2        # 只跑指定用例(16个预设之一)
#   ./scripts/tests/bl616_sanity_tests.sh TC_CADMIN_1_9               # 任意其他用例(src/python_testing/ 下存在的)
#   ./scripts/tests/bl616_sanity_tests.sh "TC_CNET_4_3:--endpoint 0"  # 非预设用例附加参数
#   ./scripts/tests/bl616_sanity_tests.sh --commission TC_DA_1_5      # 配网后只跑指定用例
#   ./scripts/tests/bl616_sanity_tests.sh --help
#
# 依赖:
#   - 一次性准备(已完成的机器可跳过):
#       source scripts/activate.sh && ./scripts/build_python.sh -i out/python_env
#   - 设备已被本框架配网(或使用 --commission 现场配网)
#
# ⚠️ 重要: ACE/OPCREDS 等测试会持久修改设备 ACL, 每轮测试前建议将设备恢复出厂
#          (BL616DK: 长按 BOOT 键 GPIO2), 再用 --commission 重新配网, 否则可能
#          出现批量 UnsupportedAccess 失败。
#
# 退出码: 0=全部通过, 1=有失败

set -uo pipefail

CHIP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PYENV="$CHIP_ROOT/out/python_env"

# ---- 可按环境修改的参数 ----
STORAGE_PATH="${STORAGE_PATH:-/tmp/admin_storage.json}"
# 测试记录归档到 SDK 内, 按日期保存(认证留档用): test_results/bl616_sanity/<日期>_<时间>/
RESULTS_ROOT="${RESULTS_ROOT:-$CHIP_ROOT/test_results}"
LOG_DIR="${LOG_DIR:-$RESULTS_ROOT/bl616_sanity/$(date +%Y%m%d_%H%M%S)}"
DISCRIMINATOR="${DISCRIMINATOR:-3840}"
PASSCODE="${PASSCODE:-20202021}"
WIFI_SSID="${WIFI_SSID:-BF-OTA-AP}"
WIFI_PASSPHRASE="${WIFI_PASSPHRASE:-12345678}"
# ---------------------------

# 16 个通过的用例: 名称 | 额外参数
#   第一梯队(通用)      : DeviceBasicComposition, CGEN_2_1, G_2_2(ep1), IDM_1_2,
#                         IDM_2_2(ep1), IDM_4_2(ep1), DGSW_2_1(ep0), DGGEN_2_4
#   lighting 核心       : CC_2_1(ep1), CC_2_2(ep1), LVL_2_3(ep1)
#   安全/证书           : DA_1_5, OPCREDS_3_1, ACE_1_2, ACE_1_3, ACL_2_2
# (带 endpoint 的测试通过第二个字段传入)
TESTS=(
    "TC_DeviceBasicComposition|"
    "TC_CGEN_2_1|"
    "TC_G_2_2|--endpoint 1"
    "TC_IDM_1_2|"
    "TC_IDM_2_2|--endpoint 1"
    "TC_IDM_4_2|--endpoint 1"
    "TC_DGSW_2_1|--endpoint 0"
    "TC_DGGEN_2_4|"
    "TC_CC_2_1|--endpoint 1"
    "TC_CC_2_2|--endpoint 1"
    "TC_LVL_2_3|--endpoint 1"
    "TC_DA_1_5|"
    "TC_OPCREDS_3_1|"
    "TC_ACE_1_2|"
    "TC_ACE_1_3|"
    "TC_ACL_2_2|"
)

COMMISSION=0
SELECTED=()
for arg in "$@"; do
    case "$arg" in
        --commission) COMMISSION=1 ;;
        --help|-h) awk '/^# 用法:/{f=1} f&&/^# 退出码/{print;exit} f{print}' "$0"; exit 0 ;;
        *) SELECTED+=("$arg") ;;
    esac
done

# 支持只跑指定用例: 传入用例名(可省略 TC_ 前缀)。
#   - 名字在 TESTS 列表内 -> 过滤出该用例(带预设的 endpoint 等参数)
#   - 名字不在列表内但文件存在 -> 直接运行(无额外参数; 需要 endpoint 的用例
#     请用 "名字:--endpoint 1" 形式追加参数)
if [[ ${#SELECTED[@]} -gt 0 ]]; then
    FILTERED=()
    for sel in "${SELECTED[@]}"; do
        # "TC_CNET_4_3:--endpoint 0" -> name=TC_CNET_4_3, extra=--endpoint 0
        sel_name="${sel%%:*}"
        sel_extra="${sel#*:}"; [[ "$sel_extra" == "$sel_name" ]] && sel_extra=""
        [[ "$sel_name" == "TC_"* ]] || sel_name="TC_$sel_name"

        matched=""
        for entry in "${TESTS[@]}"; do
            [[ "${entry%%|*}" == "$sel_name" ]] && matched="$entry"
        done
        if [[ -n "$matched" ]]; then
            # 列表内: 若用户附加了参数则覆盖预设
            [[ -n "$sel_extra" ]] && matched="$sel_name|$sel_extra"
            FILTERED+=("$matched")
        elif [[ -f "$CHIP_ROOT/src/python_testing/$sel_name.py" ]]; then
            echo "[note] $sel_name 不在预设清单, 直接运行 ${sel_extra:+(参数: $sel_extra)}"
            FILTERED+=("$sel_name|$sel_extra")
        else
            echo "[error] 未找到用例 $sel_name (src/python_testing/ 下也无此文件)"
            echo "预设清单: ${TESTS[*]%%|*}"
            exit 1
        fi
    done
    TESTS=("${FILTERED[@]}")
fi

# ---- 环境激活 ----
if [[ ! -f "$PYENV/bin/activate" ]]; then
    echo "[env] $PYENV 不存在, 请先执行:"
    echo "       source scripts/activate.sh && ./scripts/build_python.sh -i out/python_env"
    exit 1
fi
# shellcheck disable=SC1091
source "$PYENV/bin/activate"
echo "[env] python: $(which python3)"

mkdir -p "$LOG_DIR"
echo "[env] logs: $LOG_DIR"

# ---- 可选: 现场配网(BLE 扫描偶发超时, 自动重试最多 3 次) ----
if [[ $COMMISSION -eq 1 ]]; then
    # 配网前清掉旧 storage, 避免残留 fabric 信息
    rm -f "$STORAGE_PATH"
    echo "[commission] BLE+Wi-Fi 配网: $WIFI_SSID (discriminator=$DISCRIMINATOR)"
    ok=0
    for attempt in 1 2 3; do
        echo "[commission] 尝试 $attempt/3 ..."
        python3 "$CHIP_ROOT/src/python_testing/hello_test.py" \
            --commissioning-method ble-wifi \
            --discriminator "$DISCRIMINATOR" --passcode "$PASSCODE" \
            --wifi-ssid "$WIFI_SSID" --wifi-passphrase "$WIFI_PASSPHRASE" \
            --storage-path "$STORAGE_PATH" > "$LOG_DIR/commission.log" 2>&1
        if [[ $? -eq 0 ]] && grep -aq "Test results: Error 0" "$LOG_DIR/commission.log"; then
            ok=1; break
        fi
        sleep 3
    done
    [[ $ok -eq 1 ]] || { echo "[commission] 3 次均失败, 详见 $LOG_DIR/commission.log"; exit 1; }
    echo "[commission] 成功"
fi

# ---- 跑测试 ----
pass=0; fail=0; declare -a failed=()
printf "%-28s %-8s %s\n" "TEST" "RESULT" "DETAIL"
printf "%-28s %-8s %s\n" "----" "------" "------"

for entry in "${TESTS[@]}"; do
    name="${entry%%|*}"
    extra="${entry#*|}"
    log="$LOG_DIR/$name.log"

    timeout "${TEST_TIMEOUT:-300}" python3 "$CHIP_ROOT/src/python_testing/$name.py" \
        --storage-path "$STORAGE_PATH" $extra > "$log" 2>&1
    rc=$?

    summary=$(grep -aE "Summary for test class" "$log" | tail -1 | sed 's/.*Summary for test class //')
    if [[ $rc -eq 0 && "$summary" == *"Error 0"* && "$summary" == *"Failed 0"* ]]; then
        printf "%-28s %-8s %s\n" "$name" "PASS" "${summary:-ok}"
        pass=$((pass+1))
    else
        printf "%-28s %-8s %s\n" "$name" "FAIL" "${summary:-exit=$rc}"
        fail=$((fail+1)); failed+=("$name")
    fi
done

echo
echo "=========================================="
echo " 总计: $((pass+fail))  通过: $pass  失败: $fail"
[[ $fail -gt 0 ]] && status=FAILED || status=PASSED
# 生成汇总报告(认证留档)
{
    echo "BL616DK python_testing 汇总报告"
    echo "日期: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "结果: $status (通过 $pass / 失败 $fail / 共 $((pass+fail)))"
    echo "固件: $(cd $CHIP_ROOT && git rev-parse --short HEAD 2>/dev/null || echo 'n/a') / sha256 $(sha256sum $CHIP_ROOT/examples/lighting-app/bouffalolab/build/build_out/chip-bflb-lighting-example_bl616.bin 2>/dev/null | cut -c1-16 || echo 'unknown')"
    echo
    printf "%-28s %-8s %s\n" "TEST" "RESULT" "DETAIL"
    printf "%-28s %-8s %s\n" "----" "------" "------"
    for entry in "${TESTS[@]}"; do
        name="${entry%%|*}"
        if [[ " ${failed[*]} " == *" $name "* ]]; then r=FAIL; else r=PASS; fi
        summary=$(grep -aE "Summary for test class" "$LOG_DIR/$name.log" 2>/dev/null | tail -1 | sed 's/.*Summary for test class //')
        printf "%-28s %-8s %s\n" "$name" "$r" "${summary:-}"
    done
} > "$LOG_DIR/summary.txt"
echo " 报告: $LOG_DIR/summary.txt"
[[ $fail -gt 0 ]] && { echo " 失败用例: ${failed[*]}"; exit 1; }
echo " 全部通过 ✅"
