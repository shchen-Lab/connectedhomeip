# UART v2 再同步调度与属性验证测试计划（S1–S11）

日期：2026-09-14。依据：`docs/matter-bridge-uart-v2-resync-code-plan.md` P4。
前置：用户已按该计划 P1/P2/P3 修改固件与模拟器并重新编译烧录；本计划只做硬件验收。
上一轮记录（R0–R9）保留于 [resync-20260914-results.md](resync-20260914-results.md)，本轮证据存新目录 `resync-sched-01/-02/...`。

## P0 开始前基线

1. 记录：`git rev-parse HEAD` + `git diff --stat`、固件 bin 的 md5、模拟器/会话脚本 md5。
2. 串口按实际枚举确认（`ls /dev/ttyUSB*`），不假定 USB0/USB2 编号不变；协议口 921600，日志口 2000000。
3. 会话启动（证据目录由工具自动创建，须不存在）：

```bash
cd /home/alvin/code/bflb-connectedhomeip/tools/bridge_uart_v2
python3 phone_session.py --port <协议口> --console <日志口> \
  --output ../../test_results/bridge_uart_v2/resync-sched-01
```

4. chip-tool（已存在）：`/home/alvin/code/bflb-connectedhomeip/out/linux-x64-chip-tool-clang/chip-tool`；仅当其对已配网 fabric 有授权时用于属性 Read/Subscribe，否则用手机观测 + UART 日志，并在结果中注明。

## 本轮固件新日志（判定依据）

| 日志 | 含义 |
| --- | --- |
| `UART re-sync dev=D ep=E version=V` | 发起状态请求（第 syncAttempts 次） |
| `UART re-sync response OK dev=D; waiting for snapshot` | 0x45 OK，进入 1s 快照等待 |
| `UART re-sync response status=S dev=D retryAt=T` | 非法/错误响应，退避 T = now + attempts×1s |
| `UART re-sync snapshot timeout dev=D ep=E` | 等待 1s 无快照，退避 |
| `UART re-sync exhausted dev=D ep=E` | 本轮 3 次耗尽，冷却 10s（期间无新请求） |
| `UART state applied ...` / `UART snapshot rejected status=8 ...` | 快照应用/拒绝（同上轮） |

参数：快照等待 1s；重试间隔 1/2/4s；每轮最多 3 次；耗尽冷却 10s；online/offline 通知会复位该设备同步状态机。

## 本轮模拟器新操作（仅测试用）

| stdin JSON | 作用 |
| --- | --- |
| `{"op":"valid","id":N,"state_valid":false}` | 切换设备有效状态位（STATE_REQUEST 返回 STATE_UNAVAILABLE） |
| `{"op":"inject","id":N,"mode":"no_snapshot"}` | 0x44 回 OK 但不发快照 |
| `{"op":"inject","id":N,"mode":"delay_snapshot","milliseconds":500}` | 0x44 回 OK，快照延迟 500ms（事件循环调度） |
| `{"op":"inject","id":N,"mode":"busy"}` | 0x44 回 BUSY |
| `{"op":"inject","id":N,"mode":"old_version"}` | OK 后发 version-1 且 level 被篡改的快照 |
| `{"op":"inject","id":N,"mode":"conflict"}` | OK 后发同 version 不同 level 的快照 |
| `{"op":"inject","id":N,"mode":"clear"}` | 清除该设备注入 |
| `{"op":"online","id":N,"snapshot":false}` | 上线但不自动推快照（沿用上轮） |

新 HELLO（新会话）自动清空全部注入与延迟帧。

## 用例矩阵

| ID | 操作 | 必须验证 | 判定日志/证据 |
| --- | --- | --- | --- |
| S1 正常恢复 | 两灯各做一轮 `offline` → `online`（正常快照） | 同 endpoint 恢复；首个有效快照前 OFFLINE；快照先到则**无** re-sync 请求 | `OFFLINE`→`state applied`→`ONLINE`，无 `re-sync dev` |
| S2 延迟快照 | dev1：`offline`；`inject delay_snapshot 500`；`online snapshot:false`；结束后 `inject clear` | 仅 **1 次** re-sync 请求；OK 后等待；500ms 快照到达即恢复；无 timeout/无第二次请求 | `re-sync`×1、`response OK`、`state applied`、无 `snapshot timeout` |
| S3 永无快照 | dev1：`offline`；`inject no_snapshot`；`online snapshot:false`；观察 ≥25s | 请求恰好 3 次后 `exhausted`；请求间隔 ≥1s；期间 dev2 可控、无忙循环；冷却期每 10s 一条 exhausted 但零新请求；`clear` 后 `offline`→`online` 能恢复 | 请求计数、`exhausted`、心跳持续 0x42/0x43 |
| S4 状态不可用 | dev1：`valid false`；`offline`→`online snapshot:false`；观察；`valid true`；`offline`→`online` | 0x44 收 STATE_UNAVAILABLE(0x0F)，无快照下发，退避×3 后 exhausted；dev2 全程可控；恢复后正常上线 | `re-sync response status=15`、`exhausted`、恢复后 `state applied` |
| S5 BUSY | dev1：`inject busy`；触发 re-sync（offline→online snapshot:false） | BUSY 重传有界（500ms×3 重传）；按现有设计走全局断链重连；重连后注入被清空、恢复正常 | `UART v2 TX type=44`（带重传）→ 重连 HELLO → 正常恢复；记录影响 |
| S6 两设备并发 | dev1、dev2 先后 `offline`；两条 `online snapshot:false` 连发 | 两个设备各自完成 re-sync（受 pending 串行化排队）；等待/退避互不阻塞；无跨设备状态污染 | 两条 `re-sync`（不同 dev）、各自 `state applied` |
| S7 版本校验 | dev1 分别注入 `old_version`、`conflict` 各一轮，每轮后 `clear` | 两种注入均 `snapshot rejected status=8`；Reachable 保持 false、属性不变；同版本同内容（自然重复）幂等无副作用 | rejected 日志 + 手机/chip-tool 读值不变 |
| S8 生命周期 | a) dev1 注入 no_snapshot 等待期间 `remove`→重 `add`；b) 模拟 MCU 重启（退出会话，新目录 + `--state` 保留状态）；c) Bridge 重启 | a) 旧事务结果不污染新绑定；b) 全部离线→原 endpoint 恢复；c) `, restored` 恢复 | 无 `maps to slot` 错误；endpoint 不变 |
| S9 亮度闭环 | 手机对两灯分别 25%→75%→100%→25% | UART 最终 `state applied` 的 level 与 Matter Read/Subscribe 一致；无跨设备串值；每条命令 `command finished` 0x00 | 命令/快照链 + chip-tool `read current-level`（或手机显示） |
| S10 Reachable | 订阅保持下 dev1 `offline`→`online` | 实际读取 false→true（不只看 ONLINE 日志）；ReachableChanged 事件与报告一致 | chip-tool `read reachable`×2 或手机卡片离线/恢复 |
| S11 冷启动订阅 | Bridge 重启，记录时刻线 | CASE Resume、SubscribeRequest、首份 ReportData、App UI 更新各自时刻；记录重订阅延迟 | EM 日志时间线 + 用户观察 |

## 执行纪律

- 每用例前确认无残留注入（`inject clear` 两设备）；S3/S4/S7 各轮之间给足退避/冷却时间再清理。
- 判定以 UART 原始帧 + bridge.log 为准；手机画面为辅；"命令 OK"不等于"快照已应用"。
- PASS 需 UART 与 Matter 侧一致；BLOCKED 注明缺失能力；每用例记录时间点与 sequence。
- chip-tool 读法示例（endpoint 以实际为准）：
  `chip-tool levelcontrol read current-level <nodeid> 5`、`chip-tool bridgeddevicebasicinformation read reachable <nodeid> 5`

## 结果记录

完成后写 `resync-sched-20260914-results.md`：总表（PASS/FAIL/BLOCKED/NOT_RUN）+ 每项证据定位 + 实际参数记录（等待 1s/退避 1-2-4s/3 次/冷却 10s 是否与实现一致）+ P5 要求的旧记录修订说明。
