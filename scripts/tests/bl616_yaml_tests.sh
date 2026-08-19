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
# BL616DK Wi-Fi lighting —— YAML 认证测试一键执行脚本
#
# 覆盖: OnOff(TC_OO) / LevelControl(TC_LVL) / ColorControl(TC_CC) /
#       BasicInformation(TC_BINFO) / Descriptor(TC_DESC) / Groups(TC_G)
# 排除规则: 需要手动操作的(UserPrompt/重启)、设备不支持的 cluster(Sences)/
#           Simulated 专用用例一律不跑
#
# 用法:
#   ./scripts/tests/bl616_yaml_tests.sh                     # 全部 YAML 用例(设备已用 chip-tool 配网)
#   ./scripts/tests/bl616_yaml_tests.sh --commission        # 先 BLE+Wi-Fi 配网(设备需恢复出厂)
#   ./scripts/tests/bl616_yaml_tests.sh Test_TC_OO_2_1 ...  # 只跑指定用例
#   ./scripts/tests/bl616_yaml_tests.sh OO                  # 关键字过滤(含 OO 的用例)
#   ./scripts/tests/bl616_yaml_tests.sh --help
#
# 依赖:
#   - out/python_env (与 bl616_sanity_tests.sh 相同, 一次性: ./scripts/build_python.sh -i out/python_env)
#   - chip-tool 二进制 (CHIP_TOOL 路径, 默认 ./SOME-PATH/chip-tool)
#   - YAML 测试用 chip-tool 自己的 fabric 配网(与 python 框架不共享)
#
# 退出码: 0=全部通过, 1=有失败

set -uo pipefail

CHIP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PYENV="$CHIP_ROOT/out/python_env"
PICS_FILE="${PICS_FILE:-$CHIP_ROOT/examples/lighting-app/bouffalolab/pics-lighting}"
CHIP_TOOL="${CHIP_TOOL:-$CHIP_ROOT/SOME-PATH/chip-tool}"

# ---- 可按环境修改的参数 ----
NODE_ID="${NODE_ID:-0x12344321}"
DISCRIMINATOR="${DISCRIMINATOR:-3840}"
PASSCODE="${PASSCODE:-20202021}"
WIFI_SSID="${WIFI_SSID:-BF-OTA-AP}"
WIFI_PASSPHRASE="${WIFI_PASSPHRASE:-12345678}"
# 测试记录归档到 SDK 内, 按日期保存(认证留档用): test_results/bl616_yaml/<日期>_<时间>/
RESULTS_ROOT="${RESULTS_ROOT:-$CHIP_ROOT/test_results}"
LOG_DIR="${LOG_DIR:-$RESULTS_ROOT/bl616_yaml/$(date +%Y%m%d_%H%M%S)}"
# ---------------------------

# 25 个已验证用例(排除: OO_2_3/2_4 手动, OO_2_7 缺 Scenes, CC_6_5 手动, *_Simulated)
TESTS=(
    Test_TC_OO_2_1 Test_TC_OO_2_2 Test_TC_OO_2_6
    Test_TC_LVL_2_1 Test_TC_LVL_2_2
    Test_TC_CC_3_1 Test_TC_CC_3_2 Test_TC_CC_3_3
    Test_TC_CC_4_1 Test_TC_CC_4_2 Test_TC_CC_4_3 Test_TC_CC_4_4
    Test_TC_CC_5_1 Test_TC_CC_5_2 Test_TC_CC_5_3
    Test_TC_CC_6_1 Test_TC_CC_6_2 Test_TC_CC_6_3
    Test_TC_CC_7_1 Test_TC_CC_7_2 Test_TC_CC_7_3 Test_TC_CC_7_4
    Test_TC_BINFO_2_2
    Test_TC_DESC_2_1
    Test_TC_G_2_1
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

# ---- 环境检查 ----
[[ -f "$PYENV/bin/activate" ]] || { echo "[error] 缺少 $PYENV, 先: ./scripts/build_python.sh -i out/python_env"; exit 1; }
[[ -x "$CHIP_TOOL" ]] || { echo "[error] chip-tool 不存在: $CHIP_TOOL (可用 CHIP_TOOL=... 覆盖)"; exit 1; }
[[ -f "$PICS_FILE" ]] || { echo "[error] PICS 不存在: $PICS_FILE"; exit 1; }
# shellcheck disable=SC1091
source "$PYENV/bin/activate"
mkdir -p "$LOG_DIR"
echo "[env] python: $(which python3)"
echo "[env] chip-tool: $CHIP_TOOL"
echo "[env] PICS: $PICS_FILE"
echo "[env] logs: $LOG_DIR"

# ---- 用例选择 ----
if [[ ${#SELECTED[@]} -gt 0 ]]; then
    FILTERED=()
    for sel in "${SELECTED[@]}"; do
        matched=0
        for t in "${TESTS[@]}"; do
            # 精确名 或 关键字子串(如 OO / CC_5 / LVL)
            if [[ "$t" == "$sel" || "$t" == *"$sel"* ]]; then FILTERED+=("$t"); matched=1; fi
        done
        [[ $matched -eq 0 ]] && echo "[error] 未匹配到用例: $sel (可用: ${TESTS[*]})" && exit 1
    done
    TESTS=("${FILTERED[@]}")
fi
echo "[env] 用例数: ${#TESTS[@]}"

# ---- 可选: 配网(chip-tool 自己的 fabric, BLE 扫描偶发超时, 自动重试最多 3 次) ----
if [[ $COMMISSION -eq 1 ]]; then
    rm -f /tmp/chip_tool_config.ini /tmp/chip_tool_kvs /tmp/chip_tool_config.alpha.ini
    echo "[commission] BLE+Wi-Fi 配网: $WIFI_SSID nodeid=$NODE_ID"
    ok=0
    for attempt in 1 2 3; do
        echo "[commission] 尝试 $attempt/3 ..."
        "$CHIP_TOOL" pairing ble-wifi "$NODE_ID" "$WIFI_SSID" "$WIFI_PASSPHRASE" \
            "$PASSCODE" "$DISCRIMINATOR" > "$LOG_DIR/commission.log" 2>&1
        if [[ $? -eq 0 ]] && grep -aq "Pairing Success" "$LOG_DIR/commission.log"; then
            ok=1; break
        fi
        sleep 3
    done
    [[ $ok -eq 1 ]] || { echo "[commission] 3 次均失败, 详见 $LOG_DIR/commission.log"; exit 1; }
    echo "[commission] 成功"
fi

# ---- 跑测试 ----
pass=0; fail=0; declare -a failed=()
printf "%-22s %-8s %s\n" "TEST" "RESULT" "DETAIL"
printf "%-22s %-8s %s\n" "----" "------" "------"

for t in "${TESTS[@]}"; do
    log="$LOG_DIR/$t.log"
    timeout "${TEST_TIMEOUT:-300}" python3 "$CHIP_ROOT/scripts/tests/chipyaml/chiptool.py" \
        --PICS "$PICS_FILE" --server_path "$CHIP_TOOL" tests "$t" > "$log" 2>&1
    rc=$?
    summary=$(grep -aE "Test finished" "$log" | tail -1 | sed 's/\x1b\[[0-9;]*m//g; s/ Test finished in //')
    if [[ $rc -eq 0 ]]; then
        printf "%-22s %-8s %s\n" "$t" "PASS" "${summary:-ok}"
        pass=$((pass+1))
    else
        printf "%-22s %-8s %s\n" "$t" "FAIL" "${summary:-exit=$rc}"
        fail=$((fail+1)); failed+=("$t")
    fi
done

echo
echo "=========================================="
echo " 总计: $((pass+fail))  通过: $pass  失败: $fail"
[[ $fail -gt 0 ]] && status=FAILED || status=PASSED
# 生成汇总报告(认证留档)
{
    echo "BL616DK YAML 认证测试汇总报告"
    echo "日期: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "结果: $status (通过 $pass / 失败 $fail / 共 $((pass+fail)))"
    echo "SDK commit: $(cd "$CHIP_ROOT" && git rev-parse --short HEAD 2>/dev/null || echo 'n/a')"
    echo "chip-tool: $CHIP_TOOL"
    echo "PICS: $PICS_FILE"
    echo
    printf "%-22s %-8s %s\n" "TEST" "RESULT" "DETAIL"
    printf "%-22s %-8s %s\n" "----" "------" "------"
    for t in "${TESTS[@]}"; do
        if [[ " ${failed[*]} " == *" $t "* ]]; then r=FAIL; else r=PASS; fi
        summary=$(grep -aE "Test finished" "$LOG_DIR/$t.log" 2>/dev/null | tail -1 | sed 's/\x1b\[[0-9;]*m//g; s/ Test finished in //')
        printf "%-22s %-8s %s\n" "$t" "$r" "${summary:-}"
    done
} > "$LOG_DIR/summary.txt"
echo " 报告: $LOG_DIR/summary.txt"
[[ $fail -gt 0 ]] && { echo " 失败用例: ${failed[*]}"; exit 1; }
echo " 全部通过 ✅"
