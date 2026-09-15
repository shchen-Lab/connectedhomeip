# Matter Bridge UART v2：chip-tool 完整验证计划

日期：2026-09-14。目标：使用 chip-tool 验证动态设备的 Matter 属性、ReportData、UART online/offline、控制和恢复行为。

## 测试约束

- 使用已配网的 Bridge 和现有 fabric；不执行 factory reset，不清除手机配网。
- chip-tool 与 Bridge 在同一网络，命令统一加 `--storage-directory <独立目录>`，避免污染手机或其他测试 fabric。
- Bridge 日志口和 UART 模拟器同时采集。UART 端口以 `ls /dev/ttyUSB*` 实际枚举为准。
- 每次操作记录：时间、node ID、endpoint、UART device ID、binding、Bridge log、chip-tool 输出。
- `Online` 只有在有效 snapshot 应用后才算成功；UART ACK 不等同于 Matter 属性已生效。

## 工具和变量

```bash
CHIP_TOOL=/home/alvin/code/bflb-connectedhomeip/out/linux-x64-chip-tool-clang/chip-tool
NODE_ID=5678
PAA=/home/alvin/code/bflb-connectedhomeip/csa264g2mat5517024.der
OUT=test_results/bridge_uart_v2/chip-tool-20260914-01
mkdir -p "$OUT"
```

如果已有 chip-tool fabric，使用该 fabric 的 node ID；不要重复配网。若必须新建 fabric，记录 discriminator、manual code 和 storage directory。

## 0. 基线和设备发现

保存 Bridge 启动日志、UART 原始帧和当前动态设备清单。使用 Descriptor 读取确认动态 endpoint：

```bash
$CHIP_TOOL descriptor read parts-list $NODE_ID 1
$CHIP_TOOL descriptor read device-type-list $NODE_ID 4
$CHIP_TOOL descriptor read server-list $NODE_ID 4
$CHIP_TOOL descriptor read device-type-list $NODE_ID 5
$CHIP_TOOL descriptor read server-list $NODE_ID 5
```

按实际动态 endpoint 替换 `4`、`5`。记录 CT 和 ExtendedColorLight 的 endpoint、device type、server cluster 列表；不凭固定 endpoint 假设设备身份。

## 1. 基础属性 Read

对每个动态 endpoint 执行：

```bash
$CHIP_TOOL bridgeddevicebasicinformation read reachable $NODE_ID <EP>
$CHIP_TOOL bridgeddevicebasicinformation read node-label $NODE_ID <EP>
$CHIP_TOOL bridgeddevicebasicinformation read unique-id $NODE_ID <EP>
$CHIP_TOOL onoff read on-off $NODE_ID <EP>
$CHIP_TOOL levelcontrol read current-level $NODE_ID <EP>
$CHIP_TOOL levelcontrol read min-level $NODE_ID <EP>
$CHIP_TOOL levelcontrol read max-level $NODE_ID <EP>
$CHIP_TOOL levelcontrol read on-level $NODE_ID <EP>
$CHIP_TOOL levelcontrol read options $NODE_ID <EP>
$CHIP_TOOL colorcontrol read color-mode $NODE_ID <EP>
$CHIP_TOOL colorcontrol read enhanced-color-mode $NODE_ID <EP>
$CHIP_TOOL colorcontrol read color-temperature-mireds $NODE_ID <EP>
$CHIP_TOOL colorcontrol read options $NODE_ID <EP>
$CHIP_TOOL colorcontrol read startup-color-temperature-mireds $NODE_ID <EP>
```

CT 设备重点检查色温；ExtendedColorLight 重点检查 HS 和色温能力。记录读值与 UART snapshot 的 on、level、hue、saturation、temperature 一一对应。

## 2. ReportData 监听

为每个动态 endpoint 单独启动订阅，终端保持运行并保存 stdout/stderr：

```bash
$CHIP_TOOL onoff subscribe on-off 0 3600 $NODE_ID <EP> | tee "$OUT/ep<EP>-onoff-report.log"
$CHIP_TOOL levelcontrol subscribe current-level 0 3600 $NODE_ID <EP> | tee "$OUT/ep<EP>-level-report.log"
$CHIP_TOOL bridgeddevicebasicinformation subscribe reachable 0 3600 $NODE_ID <EP> | tee "$OUT/ep<EP>-reachable-report.log"
$CHIP_TOOL colorcontrol subscribe color-mode 0 3600 $NODE_ID <EP> | tee "$OUT/ep<EP>-color-mode-report.log"
$CHIP_TOOL colorcontrol subscribe color-temperature-mireds 0 3600 $NODE_ID <EP> | tee "$OUT/ep<EP>-temperature-report.log"
```

ExtendedColorLight 另外订阅：

```bash
$CHIP_TOOL colorcontrol subscribe current-hue 0 3600 $NODE_ID <EP>
$CHIP_TOOL colorcontrol subscribe current-saturation 0 3600 $NODE_ID <EP>
```

判定要求：启动订阅后收到初始 ReportData；每次 UART snapshot 应只产生对应属性变化报告；未变化字段不应持续重复报告；Reachable 离线/上线报告值必须分别为 false/true。

## 3. UART online/offline 验证

保持上述订阅运行，通过 UART 模拟器执行：

```json
{"op":"offline","id":1}
{"op":"online","id":1}
{"op":"offline","id":2}
{"op":"online","id":2}
```

检查顺序：

1. UART offline ACK 为 0。
2. Bridge 将对应 endpoint 的 Reachable 设为 false。
3. chip-tool 收到 Reachable=false ReportData。
4. online ACK 为 0。
5. 有效 snapshot 应用后 Bridge 才打印 ONLINE。
6. chip-tool 收到 Reachable=true 和状态属性报告。
7. endpoint、binding、设备名称保持不变。

再测试不自动发送 snapshot：

```json
{"op":"offline","id":1}
{"op":"online","id":1,"snapshot":false}
```

预期 Bridge 发送 `DEVICE_STATE_REQUEST (0x44)`；收到 `STATE_RESPONSE OK` 后等待 snapshot；没有 snapshot 时不应立即重复请求；超时后按 1/2/4 秒退避，最多 3 次。

## 4. OnOff 控制和报告

```bash
$CHIP_TOOL onoff on $NODE_ID <EP>
$CHIP_TOOL onoff off $NODE_ID <EP>
$CHIP_TOOL onoff toggle $NODE_ID <EP>
```

每个命令记录：chip-tool InvokeResponse、UART command、MCU ACK、snapshot version、Bridge state applied、ReportData。要求 OnOff 最终读值与 UART 状态一致，不能只依据 InvokeResponse 判定成功。

## 5. Level 控制和回跳验证

```bash
$CHIP_TOOL levelcontrol move-to-level 64 $NODE_ID <EP>
$CHIP_TOOL levelcontrol move-to-level-with-on-off 128 $NODE_ID <EP>
$CHIP_TOOL levelcontrol move-to-level-with-on-off 254 $NODE_ID <EP>
$CHIP_TOOL levelcontrol read current-level $NODE_ID <EP>
```

连续执行 `25% → 75% → 100% → 25%`，间隔小于 1 秒。验证 UART snapshot version 单调递增，Bridge 不应用旧 level，订阅值与最终 Read 一致。若 UI 回跳，优先检查 chip-tool Read/Subscribe 和 ReportData，不能只依据手机画面下结论。

## 6. HS/色温控制

ExtendedColorLight：

```bash
$CHIP_TOOL colorcontrol move-to-hue-and-saturation 32 180 $NODE_ID <EP>
$CHIP_TOOL colorcontrol move-to-color-temperature 250 $NODE_ID <EP>
$CHIP_TOOL colorcontrol read current-hue $NODE_ID <EP>
$CHIP_TOOL colorcontrol read current-saturation $NODE_ID <EP>
$CHIP_TOOL colorcontrol read color-temperature-mireds $NODE_ID <EP>
```

CT 灯：

```bash
$CHIP_TOOL colorcontrol move-to-color-temperature 154 $NODE_ID <EP>
$CHIP_TOOL colorcontrol move-to-color-temperature 454 $NODE_ID <EP>
```

验证 HS/色温状态、ColorMode、EnhancedColorMode 与 UART 快照一致。XY 不作为本计划测试项；若 chip-tool 暴露 XY 命令，应记录为模板/能力声明问题，不进行 RGB 转换。

## 7. 属性写入与读回

```bash
$CHIP_TOOL levelcontrol write options 1 $NODE_ID <EP>
$CHIP_TOOL levelcontrol read options $NODE_ID <EP>
$CHIP_TOOL levelcontrol write on-level 128 $NODE_ID <EP>
$CHIP_TOOL levelcontrol read on-level $NODE_ID <EP>
$CHIP_TOOL colorcontrol write options 1 $NODE_ID <EP>
$CHIP_TOOL colorcontrol read options $NODE_ID <EP>
$CHIP_TOOL colorcontrol write startup-color-temperature-mireds 300 $NODE_ID <EP>
$CHIP_TOOL colorcontrol read startup-color-temperature-mireds $NODE_ID <EP>
```

验证写入成功后立即读回、订阅收到报告；非法范围必须返回 ConstraintError，原值不变。确认这些值是否需要跨 Bridge 重启持久化，若协议未承载则记录为 Bridge 本地配置限制。

## 8. 重启和恢复

- 只重启 MCU 模拟器，保留 state 文件：验证两个 endpoint 重新绑定、快照恢复和 Reachable 报告。
- 只重启 Bridge：验证 `restored` 日志、endpoint/unique ID 不变、订阅重建后全量 ReportData 正确。
- 重启期间执行一次 chip-tool Read，记录不可达、超时或旧缓存；订阅建立前的客户端旧值不作为固件失败。

## 9. 删除与重新添加

UART 删除设备后确认：

- 原 endpoint 的 Read/Subscribe 终止或返回路径不存在。
- 新设备名称使用类型名加递增 ID，例如 `Color Temperature Light 3`。
- 新 endpoint/binding 与旧设备不混淆。
- 旧的延迟 snapshot/report 不会更新新 endpoint。
- Descriptor 和 PartsList 更新后，chip-tool 能发现新 endpoint。

## 10. 结果判定和证据

每个用例保存 chip-tool 输出、Bridge log、UART jsonl、操作 JSON 和时间线。PASS 必须同时满足 UART 状态、Bridge 内部状态、Matter Read/ReportData 三者一致；只看到 UART ACK 或手机 UI 变化不能单独判 PASS。
