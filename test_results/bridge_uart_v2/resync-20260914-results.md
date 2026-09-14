# Bridge UART v2 状态再同步验证结果（2026-09-14）

> 2026-09-14 审查修订（依据 resync-code-plan P5，原记录保留）：
> 1. "良性双重 re-sync / 最多多一次请求"仅适用于本轮已测条件（OK 应答先于快照到达的窗口）；调度重构后由设备级等待+退避接管，不再作为代码保证。
> 2. R8 归因修正：本轮未观察到 UART 状态回退，"回跳 50%"与重启后 App 订阅恢复延迟相关；后续经 chip-tool 独立 fabric Read/Subscribe 证实 Bridge 数值正确，详见 resync-sched-20260914-results.md。
> 3. 原记录中"3 条全量 ReportData"为订阅初始化的分块报告描述，不代表每次报告均为全量。

固件：含 4 项改动（re-sync 补发、端点一致性校验、创建/恢复日志、命令/快照诊断日志）。
证据目录：[resync-20260914-01](resync-20260914-01/)、[resync-20260914-02](resync-20260914-02/)（-02 为模拟 MCU 重启后新会话，状态文件由 -01 保留）。
测试计划：[resync-20260914-test-plan.md](resync-20260914-test-plan.md)。

## 结果总表

| 用例 | 状态 | 证据 | 说明 |
| --- | --- | --- | --- |
| R0 启动/握手 | PASS | -01/uart.jsonl HELLO seq=85 → LIST → 0x42/0x43 心跳 | 新固件日志格式生效 |
| R1 添加 CW | PASS | -01/bridge.log 634323-634423 | endpoint 3，type=0x010c，快照 version=1 mask=0043 原子应用 |
| R2 添加 HS/CT | PASS | -01/bridge.log 803848+ | endpoint 4，快照 mask=000f |
| R3 正常上下线回归 | PASS | -01/bridge.log 821369-823387 | 上线后保持 OFFLINE 直到快照（version=3）才 ONLINE，符合协议 §12.2 |
| R4 补发路径（核心） | PASS | -01/bridge.log 845080-845130；uart.jsonl 0x44 seq=152 | `snapshot_deferred` 后 Bridge 保持 OFFLINE → `UART re-sync dev=1 ep=3 version=5` → 0x44 → 0x45 OK + 快照 → ONLINE。丢快照场景闭环成立 |
| R5 删除/再添加 | PASS | -01/bridge.log 910154-913183 | 删除 ep3 → 重新添加得 ep5；全程无 `maps to slot ... rejecting` |
| R7 模拟 MCU 重启 | PASS | -02/bridge.log 1070751+ | 掉线期间 Endpoint 保留；新 session 握手后 ep5/ep4 原位恢复，状态与 MCU 一致（dev2: on=1 level=200 version=3） |
| R6 Bridge 重启 | PASS | -02/bridge.log 423-756（重启后毫秒级时间戳） | KVS 恢复：两台带 `, restored` 原位恢复（ep5/ep4 不变）；握手后快照重放（dev1 v1，dev2 v3）；binding 续用高位（11/12）；无映射错误 |
| R8 手机亮度链路 | PASS | -02/bridge.log 119445-167569、422830、696665-703375 | 首轮"回跳 50%"诊断为订阅缺失陈旧 UI（见下）；订阅重建后复测：9 条命令（含快速连续拖动 5 档）全部 0x00，快照版本 5→9 严格单调、即时应用，无回跳、无 rejected |
| R9 MCU 主动上报 | PASS | -01/bridge.log 890491-892495 | version=2（off）→ OnOff 报告；version=3（on, level=200）→ OnOff+Level 报告 |
| 旧快照注入 | BLOCKED | — | 模拟器无异常注入能力（M6 未实现），stateVersion 拒绝逻辑只能靠日志观察 |

## 观察与瑕疵（不阻塞）

1. **良性双重 re-sync**：0x45 应答清掉 pending 后、快照尚未应用的窗口内，NextBind 会再补发一次 0x44（R4 与每次 Bind 后均出现两次 `state applied`）。同版本同内容幂等，最多多一次请求。若要消除，可在 Record 上加 in-flight re-sync 标志。
2. **清单版本变化触发整体重连**：Add 后 deviceListVersion 1→2，Bridge 在心跳响应中发现后选择 Disconnect+完整重握手（binding 1→2）。协议 §16 允许；但每次在线 Add 都会引起一次全局重连和 Reachable 抖动。后续可考虑改为仅拉清单不断链。
3. 新日志 `Added device ... type=0x010c`、`waiting for first snapshot`、`Removed ... (uniqueId=...)` 均按预期出现。

## R8 诊断（2026-09-14 首轮 App 操作）

用户症状：App 显示"未响应"，亮度回跳 50%。

日志证据链（-02/bridge.log，时间戳为 Bridge 开机后 ms）：

1. `0s` Bridge 重启，Matter 订阅失效；App 缓存灯状态 = 重启前 level=128（50%）。
2. `119275-119438` CASE Sigma2Resume 恢复会话（peer ADBF702D，LSID 38941）。
3. `119445-167530` 用户 5 条命令全部成功：MoveToLevelWithOnOff ×3（level 112→254→191，即 44%→100%→75%）+ On ×2；`UART command finished` 全部 0x00；快照 v2/v3/v4 原子应用。
4. `119s-422s` **零 SubscribeRequest**：App 收不到任何属性报告，界面停留在陈旧缓存 50%，表现为"回跳"。
5. `422830` Apple Home 中枢重建订阅（SubscribeRequest B:615）→ Bridge 连发 3 条全量 ReportData（B:1179/1195/1201）。

结论：非固件缺陷。`state applied` 日志证明 level 从未回到 128，Bridge 状态真值始终单调推进（v1→v4）；"50%" 是订阅缺失期间的 App 陈旧 UI，"未响应" 是 CASE 恢复前的首次点击。Bridge 侧行为（Sigma2Resume 接受、命令执行、状态报告）均正常。

待复测：订阅已重建，用户重做滑条操作，确认实时跟随。——已复测通过：696665-703375 共 9 条命令（含快速连续拖动 5 档）全部 0x00，快照版本 5→9 严格单调即时应用，App 实时跟随无回跳，最终 239（94%）。

## 后续优化项（非阻塞，本轮不处理）

1. 良性双重 re-sync：0x45 应答清 pending 后、快照应用前的窗口会多发一次 0x44；可在 Record 上加 in-flight 标志消除。
2. 在线 Add 引起 deviceListVersion 变化时 Bridge 选择整体断链重握手，Reachable 短暂抖动；可改为只拉清单不断链。
3. Bridge 重启后 Apple Home 中枢重订阅延迟实测约 7 分钟（期间用户操作看到陈旧值），属客户端行为；Bridge 侧无可强制手段，建议在验收文档注明"重启后 App 首次操作前刷新家庭页面"。

## R6 补充证据（2026-09-14 复位实测）

- `[423ms] Added device UART-00000001 ... to dynamic endpoint 5 (index=0, restored)`
- `[426ms] Added device UART-00000002 ... to dynamic endpoint 4 (index=1, restored)`
- `[729ms] state applied snapshot ep=5 dev=1 version=1 mask=0043` → ONLINE
- `[742ms] UART re-sync dev=2 ep=4 version=0` → `[744ms] state applied snapshot ep=4 dev=2 version=3 mask=000f` → ONLINE
- `maps to slot` 错误计数：0
