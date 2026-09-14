# Bridge UART v2 状态再同步 + 亮度回退整改验证计划

日期：2026-09-14。本固件新增 4 项改动（对照 git diff）：

1. `NextBind()` 对 `bound && online && !synced` 设备补发 `DEVICE_STATE_REQUEST (0x44)`
2. `FindBridgeDevice()` 端点一致性校验
3. 端点创建/恢复/删除诊断日志
4. 命令 ACK / 快照应用 / 快照拒绝诊断日志

模拟器新增能力（tools/bridge_uart_v2）：响应 0x44/0x45；`online` 支持 `"snapshot": false` 延迟快照（构造快照丢失场景）。

## 运行方式

```bash
cd /home/alvin/code/bflb-connectedhomeip/tools/bridge_uart_v2
python3 phone_session.py --port /dev/ttyUSB0 --console /dev/ttyUSB2 \
  --output ../../test_results/bridge_uart_v2/resync-20260914-01
```

stdin 逐行输入 JSON 命令（见各用例）。Bridge 调试口 /dev/ttyUSB2 会被自动采集到 bridge.log。
前置：烧录含本次改动的新固件；无旧会话残留（换新目录编号即隔离）。

## 本轮新增/变更日志格式（判定依据）

| 日志 | 含义 |
| --- | --- |
| `Dynamic endpoint created: endpoint=N device=UART-XXXXXXXX reachable=false, waiting for first snapshot` | 端点创建，等待首个快照 |
| `Added device UART-XXXXXXXX (uniqueId=... type=0x010D) to dynamic endpoint N (index=M, restored)` | 添加（恢复时带 `, restored`） |
| `Removed dynamic device ... (uniqueId=...) from dynamic endpoint N (index=M)` | 删除 |
| `UART re-sync dev=ID ep=N version=V` | 补发 0x44 |
| `UART state applied snapshot ep=N dev=ID version=V mask=0x0043 values=...` | 快照原子应用 |
| `UART snapshot rejected status=S ep=N dev=ID version=V expected=E` | 旧快照/非法快照被拒 |
| `UART command finished ep=N cluster=0x00000008 cmd=0x... status=0x00` | Matter 命令 UART ACK |

## 用例

| ID | 操作（stdin JSON / 手机） | 预期 UART / 日志 | 通过标准 |
| --- | --- | --- | --- |
| R0 | 启动模拟器（空清单），确认 Bridge 新固件启动 | HELLO→LIST(0条)→心跳正常；无异常新日志 | 固件版本/时间为本次编译 |
| R1 | `{"op":"add","id":1,"kind":"cw"}` | 0x36→37 OK(含 ep/binding)；0x3A→3B；0x40→41；日志含 `waiting for first snapshot` + `Added device ... type=0x010C` | 手机出现色温灯；Reachable=true |
| R2 | `{"op":"add","id":2,"kind":"hsct"}` | 同上，HS 快照 mask 0x0F；无 re-sync 日志（Bind 已带快照） | 手机出现彩灯；无重复卡片 |
| R3 正常上线回归 | `{"op":"offline","id":1}` → Reachable=false；`{"op":"online","id":1}` | 离线 0x3E→3F；上线 0x3C→3D 后模拟器自动推快照 0x40→41；**不应**出现 `UART re-sync`（快照先到） | 手机恢复可控；Endpoint 不变 |
| R4 补发路径（核心） | `{"op":"offline","id":1}` → `{"op":"online","id":1,"snapshot":false}` | 0x3D 后无快照；日志出现 `UART re-sync dev=1 ep=N version=V`；Bridge 发 0x44；模拟器回 0x45 OK + 0x40；`UART state applied snapshot` | Reachable=true；手机显示上线且状态为模拟器值 |
| R5 端点映射一致性 | R4 后读取手机状态；再 `{"op":"remove","id":1}` → 重新 `{"op":"add","id":1,"kind":"cw"}` | 删除 0x38→39；再添加分配新 endpoint/binding；全程 bridge.log **无** `maps to slot ... rejecting` 错误 | 手机无重复设备；新灯可控；属性与模拟器一致 |
| R6 Bridge 重启（P11 前半） | 仅重启 Bridge（保留模拟器） | 重新 HELLO/清单/Bind/快照；恢复日志带 `, restored`；Endpoint/uniqueId 不变 | 手机设备不消失（或短暂不可用后恢复）；状态来自 MCU |
| R7 模拟 MCU 重启（P11 后半） | 重启 phone_session（保留 mcu-state.json） | Bridge 收到新 session 后全部离线；重新握手后逐设备 Bind+快照 | 最终两台在线；状态与 MCU 一致 |
| R8 亮度链路（旧快照过滤） | 手机对 CW 灯连续操作：25%→75%→100%，间隔 <1s；再 100%→25% | 每次一条 `UART command finished ... cluster=0x00000008`；每条 snapshot `version` 单调递增；`values=xx` level 字段与手机最终一致 | 手机最终亮度=模拟器值；若出现 `snapshot rejected`，确认手机仍显示 applied 版本的值 |
| R9 增量上报 | `{"op":"state","id":2,"values":{"on":0}}` → `{"op":"state","id":2,"values":{"on":1}}` | 0x40 快照（模拟器 state 用完整快照）；`UART state applied ... mask=0x000F`；version 递增 | 手机开关状态跟随 |

## 结果记录

每用例记录：PASS/FAIL/BLOCKED/NOT_RUN、时间点、sequence、bridge.log 行号摘录、手机现象（是否需刷新）。R8 需额外记录：App 写入值（手机 UI%）→ `UART command finished` → `state applied version/level` 三元组，用于亮度回退归因。
