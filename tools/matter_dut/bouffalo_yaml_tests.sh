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
# Matter DUT —— YAML 认证测试一键执行脚本
#
# 默认快捷用例覆盖: OnOff(TC_OO) / LevelControl(TC_LVL) / ColorControl(TC_CC) /
#       BasicInformation(TC_BINFO) / Descriptor(TC_DESC) / Groups(TC_G)
# 排除规则: 需要手动操作的(UserPrompt/重启)、设备不支持的 cluster(Scenes)/
#           Simulated 专用用例一律不跑
#
# 用法:
#   ./tools/matter_dut/bouffalo_yaml_tests.sh                     # 运行 lighting 快捷用例集
#   ./tools/matter_dut/bouffalo_yaml_tests.sh Test_TC_AA_1_1      # 运行任意 certification YAML 用例
#   BOUFFALO_TARGET=bl616dk ./tools/matter_dut/bouffalo_yaml_tests.sh
#   PICS_FILE=/path/to/device.pics FIRMWARE_PATH=/path/to/app.bin ./tools/matter_dut/bouffalo_yaml_tests.sh Test_TC_OO_2_1
#   ./tools/matter_dut/bouffalo_yaml_tests.sh Test_TC_OO_2_1 ...  # 只跑指定用例
#   ./tools/matter_dut/bouffalo_yaml_tests.sh OO                  # 关键字过滤(含 OO 的用例)
#
# 配网 (--commission 会先配网再测试；不传时要求 DUT 已与 chip-tool 配对):
#   Wi-Fi:     BOUFFALO_TARGET=bl616dk WIFI_SSID=my-ap WIFI_PASSPHRASE=my-password CHIP_TOOL=/path/to/chip-tool ./tools/matter_dut/bouffalo_yaml_tests.sh --commission Test_TC_OO_2_1
#   Thread:    BOUFFALO_TARGET=bl704ldk THREAD_DATASET='hex:<dataset-hex>' CHIP_TOOL=/path/to/chip-tool ./tools/matter_dut/bouffalo_yaml_tests.sh --commission Test_TC_OO_2_1
#   Linux LAN: BOUFFALO_TARGET=linux-x64-light CHIP_TOOL=/path/to/chip-tool ./tools/matter_dut/bouffalo_yaml_tests.sh --commission Test_TC_OO_2_1
#   ./tools/matter_dut/bouffalo_yaml_tests.sh --help
#
# 依赖:
#   - out/python_env (与 bouffalo_sanity_tests.sh 相同, 一次性: ./scripts/build_python.sh -i out/python_env)
#   - chip-tool 二进制 (CHIP_TOOL 路径, 默认 ./SOME-PATH/chip-tool)
#   - YAML 测试用 chip-tool 自己的 fabric 配网(与 python 框架不共享)
#
# 退出码: 0=全部通过, 1=有失败或未执行用例

set -uo pipefail

CHIP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$(dirname "${BASH_SOURCE[0]}")/bouffalo_test_common.sh"
bouffalo_parse_args "$@"

PYENV="${PYENV:-$CHIP_ROOT/out/python_env}"
CHIP_TOOL="${CHIP_TOOL:-$CHIP_ROOT/SOME-PATH/chip-tool}"
bouffalo_init_common_config || exit $?

# ---- 可按环境修改的参数 ----
if [[ "$NODE_ID" =~ ^0[xX][0-9a-fA-F]+$ ]]; then
    YAML_NODE_ID=$((NODE_ID))
elif [[ "$NODE_ID" =~ ^[0-9]+$ ]]; then
    YAML_NODE_ID=$((10#$NODE_ID))
else
    echo "[error] NODE_ID 必须是十进制或 0x 开头的十六进制整数"; exit 2
fi
THREAD_DATASET="${THREAD_DATASET#hex:}"
CHIP_TOOL_STORAGE_DIR="${CHIP_TOOL_STORAGE_DIR:-$CHIP_ROOT/.tmp/bouffalo/$BOUFFALO_TARGET/chip_tool}"
# 测试记录归档到 SDK 内, 按日期保存(认证留档用): test_results/bouffalo_yaml/<日期>_<时间>/
RESULTS_ROOT="${RESULTS_ROOT:-$CHIP_ROOT/test_results}"
LOG_DIR="${LOG_DIR:-$RESULTS_ROOT/bouffalo_yaml/$(date +%Y%m%d_%H%M%S)}"
FIRMWARE_PATH="${FIRMWARE_PATH:-}"
# ---------------------------

# 已自动化用例(排除: OO_2_3/2_4 手动, OO_2_7 缺 Scenes, CC_6_5 手动,
#                    BINFO_2_2 需人工重启 DUT 无法自动化,
#                    LVL_8_1/9_1 DUT-Client 或依赖 Scenes(不支持), G_3_2 DUT-Client(不支持),
#                    G_2_4 依赖 MCORE.G.MULTIENDPOINT(单 endpoint 设备不适用), *_Simulated,
#                    当前 PICS 下没有可执行步骤的 LVL_7_1/BINFO_3_1/G_2_2/G_2_3)
TESTS=(
    Test_TC_OO_2_1 Test_TC_OO_2_2 Test_TC_OO_2_6
    Test_TC_LVL_2_1 Test_TC_LVL_2_2 Test_TC_LVL_3_1 Test_TC_LVL_4_1 Test_TC_LVL_5_1
    Test_TC_LVL_6_1
    Test_TC_CC_3_1 Test_TC_CC_3_2 Test_TC_CC_3_3
    Test_TC_CC_4_1 Test_TC_CC_4_2 Test_TC_CC_4_3 Test_TC_CC_4_4
    Test_TC_CC_5_1 Test_TC_CC_5_2 Test_TC_CC_5_3
    Test_TC_CC_6_1 Test_TC_CC_6_2 Test_TC_CC_6_3
    Test_TC_CC_7_1 Test_TC_CC_7_2 Test_TC_CC_7_3 Test_TC_CC_7_4
    Test_TC_CC_8_1
    Test_TC_CC_9_1 Test_TC_CC_9_2 Test_TC_CC_9_3
    Test_TC_BINFO_2_1
    Test_TC_DESC_2_1
    Test_TC_G_2_1
)

if [[ $LIST_ONLY -eq 1 ]]; then
    printf '%s\n' "${TESTS[@]}"
    exit 0
fi

# ---- 环境检查 ----
[[ -f "$PYENV/bin/activate" ]] || { echo "[error] 缺少 $PYENV, 先: ./scripts/build_python.sh -i out/python_env"; exit 1; }
[[ -x "$CHIP_TOOL" ]] || { echo "[error] chip-tool 不存在: $CHIP_TOOL (可用 CHIP_TOOL=... 覆盖)"; exit 1; }
[[ -f "$PICS_FILE" ]] || { echo "[error] PICS 不存在: $PICS_FILE"; exit 1; }
[[ -z "$FIRMWARE_PATH" || -f "$FIRMWARE_PATH" ]] || { echo "[error] 固件文件不存在: $FIRMWARE_PATH"; exit 1; }
if [[ ! -d "$CHIP_TOOL_STORAGE_DIR" && $COMMISSION -ne 1 ]]; then
    echo "[error] 找不到 chip-tool 存储: $CHIP_TOOL_STORAGE_DIR"
    echo "[error] YAML 使用独立的配对存储；先打开 DUT 的 commissioning window，再加 --commission 运行一次"
    exit 1
fi
mkdir -p "$CHIP_TOOL_STORAGE_DIR"
# shellcheck disable=SC1091
source "$PYENV/bin/activate"
mkdir -p "$LOG_DIR"
echo "[env] python: $(which python3)"
echo "[env] chip-tool: $CHIP_TOOL"
echo "[env] target: $BOUFFALO_TARGET (transport=$COMMISSION_TRANSPORT)"
echo "[env] PICS: $PICS_FILE"
echo "[env] chip-tool storage: $CHIP_TOOL_STORAGE_DIR"
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
        if [[ $matched -eq 0 ]]; then
            yaml_path=$(find "$CHIP_ROOT/src/app/tests/suites" -type f \( -name "$sel.yaml" -o -name "Test_$sel.yaml" \) -print -quit)
            [[ -n "$yaml_path" ]] || { echo "[error] 未找到 YAML 用例: $sel"; exit 1; }
            FILTERED+=("$(basename "$yaml_path" .yaml)")
        fi
    done
    TESTS=("${FILTERED[@]}")
fi
echo "[env] 用例数: ${#TESTS[@]}"

# ---- 可选: 配网(chip-tool 自己的 fabric, BLE 扫描偶发超时, 自动重试最多 3 次) ----
if [[ $COMMISSION -eq 1 ]]; then
    case "$COMMISSION_TRANSPORT" in
        wifi) PAIRING_ARGS=(pairing ble-wifi "$NODE_ID" "$WIFI_SSID" "$WIFI_PASSPHRASE" "$PASSCODE" "$DISCRIMINATOR") ;;
        thread)
            [[ -n "$THREAD_DATASET" ]] || { echo "[error] Thread 配网需要设置 THREAD_DATASET"; exit 2; }
            PAIRING_ARGS=(pairing ble-thread "$NODE_ID" "hex:$THREAD_DATASET" "$PASSCODE" "$DISCRIMINATOR")
            ;;
        ethernet) PAIRING_ARGS=(pairing onnetwork "$NODE_ID" "$PASSCODE") ;;
    esac
    mkdir -p "$CHIP_TOOL_STORAGE_DIR"
    echo "[commission] $COMMISSION_TRANSPORT 配网: nodeid=$NODE_ID"
    ok=0
    for attempt in 1 2 3; do
        echo "[commission] 尝试 $attempt/3 ..."
        "$CHIP_TOOL" "${PAIRING_ARGS[@]}" --storage-directory "$CHIP_TOOL_STORAGE_DIR" > "$LOG_DIR/commission.log" 2>&1
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
skip=0; declare -a skipped=()
printf "%-22s %-8s %s\n" "TEST" "RESULT" "DETAIL"
printf "%-22s %-8s %s\n" "----" "------" "------"

for t in "${TESTS[@]}"; do
    log="$LOG_DIR/$t.log"
    timeout "${TEST_TIMEOUT:-300}" python3 "$CHIP_ROOT/scripts/tests/chipyaml/chiptool.py" \
        --PICS "$PICS_FILE" --server_path "$CHIP_TOOL" \
        --server_arguments "interactive server --storage-directory $CHIP_TOOL_STORAGE_DIR" \
        tests "$t" --nodeId "$YAML_NODE_ID" > "$log" 2>&1
    rc=$?
    summary=$(grep -aE "Test finished" "$log" | tail -1 | sed 's/\x1b\[[0-9;]*m//g; s/ Test finished in //')
    executed=$(sed -nE 's/.*with ([0-9]+) steps runned and ([0-9]+) steps skipped.*/\1/p' "$log" | tail -1)
    if [[ $rc -eq 0 && "$executed" =~ ^[0-9]+$ && "$executed" -gt 0 ]]; then
        printf "%-22s %-8s %s\n" "$t" "PASS" "${summary:-ok}"
        pass=$((pass+1))
    elif [[ $rc -eq 0 && "$executed" == 0 ]]; then
        printf "%-22s %-8s %s\n" "$t" "SKIP" "${summary:-no steps executed}"
        skip=$((skip+1)); skipped+=("$t")
    else
        printf "%-22s %-8s %s\n" "$t" "FAIL" "${summary:-exit=$rc}"
        fail=$((fail+1)); failed+=("$t")
    fi
done

echo
echo "=========================================="
echo " 总计: $((pass+fail+skip))  通过: $pass  失败: $fail  跳过: $skip"
if [[ $fail -gt 0 ]]; then status=FAILED; elif [[ $skip -gt 0 ]]; then status=INCOMPLETE; else status=PASSED; fi
# 生成汇总报告(认证留档)
{
    echo "Matter YAML 认证测试汇总报告"
    echo "目标: $BOUFFALO_TARGET"
    echo "配网传输: $COMMISSION_TRANSPORT"
    echo "日期: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "结果: $status (通过 $pass / 失败 $fail / 跳过 $skip / 共 $((pass+fail+skip)))"
    echo "SDK source commit: $(cd "$CHIP_ROOT" && git rev-parse --short HEAD 2>/dev/null || echo 'n/a')"
    echo "chip-tool: $CHIP_TOOL"
    echo "chip-tool storage: $CHIP_TOOL_STORAGE_DIR"
    echo "PICS: $PICS_FILE"
    if [[ -n "$FIRMWARE_PATH" && -f "$FIRMWARE_PATH" ]]; then
        echo "固件文件: $FIRMWARE_PATH"
        echo "固件 SHA-256: $(sha256sum "$FIRMWARE_PATH" | awk '{print $1}')"
    else
        echo "固件文件: ${FIRMWARE_PATH:-未指定}"
        echo "固件 SHA-256: unknown"
    fi
    echo
    printf "%-22s %-8s %s\n" "TEST" "RESULT" "DETAIL"
    printf "%-22s %-8s %s\n" "----" "------" "------"
    for t in "${TESTS[@]}"; do
        if [[ " ${failed[*]} " == *" $t "* ]]; then r=FAIL; elif [[ " ${skipped[*]} " == *" $t "* ]]; then r=SKIP; else r=PASS; fi
        summary=$(grep -aE "Test finished" "$LOG_DIR/$t.log" 2>/dev/null | tail -1 | sed 's/\x1b\[[0-9;]*m//g; s/ Test finished in //')
        printf "%-22s %-8s %s\n" "$t" "$r" "${summary:-}"
    done
} > "$LOG_DIR/summary.txt"
echo " 报告: $LOG_DIR/summary.txt"
[[ $fail -gt 0 ]] && { echo " 失败用例: ${failed[*]}"; exit 1; }
[[ $skip -gt 0 ]] && { echo " 未执行用例: ${skipped[*]}"; exit 1; }
echo " 全部通过 ✅"
