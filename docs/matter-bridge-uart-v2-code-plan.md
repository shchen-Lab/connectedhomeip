# Matter Bridge UART v2 Code Plan

更新：2026-09-08。本文描述当前工作区源码与剩余工作，替代此前 v1 现状表和 XY 转换任务。实现存在、主机测试通过、固件构建通过、实板通过是四种不同状态。

## 1. 范围和基准

- 唯一线协议：[Obsidian MCU 对接协议](</home/alvin/code/obsidian.md/alvin/Matter Bridge App UART v2 MCU 对接协议.md>) Rev.4；此次只取消 Bridge XY 功能，保留 v2 帧头和 HS/CT 线格式。
- CW：UART type=0x10、capabilityFlags=0x13；Matter CT FeatureMap=0x10。
- 彩灯：UART type=0x11、capabilityFlags=0x17；Matter HS+CT FeatureMap=0x11。HS=模式0、CT=模式2；无 XY 属性、命令、转换或 RGB 接口。
- UART 基本不丢包：正常增量上报无 ACK、无周期全量补偿；保留协议 CRC、有限重试和启动同步。
- 本轮用户用手机配网/控制 Bridge，电脑模拟 MCU 经 UART 驱动子设备行为；按[手机测试计划](matter-bridge-uart-v2-phone-test-plan.md)执行。chip-tool 降为必要时的诊断工具。
- 固件由用户编译烧录。旧固件构建记录不能证明当前工作区可构建。
- 仓库的 matter-bridge-uart-v2-mcu-protocol.md 是另一份 RGB 方案，不作为本轮规范，也不覆盖它的未提交内容。

## 2. 源码与设计对照

路径均相对 examples/bridge-app/bouffalolab。

| 范围 | 当前源码实现 | 实际验证状态/剩余事项 |
| --- | --- | --- |
| UART 物理层 | LightUartPort.cpp：uart1、921600、8N1、无流控、GPIO6 TX/7 RX、RX DMA/ring | 端口已由用户确认；当前固件实板通信待验 |
| codec | LightUartProtocol.h/.c：version2、31-byte header、payload≤1024、frame≤1059、CRC16/flags、非零 sequence | Python 向量通过；当前 C 测试和固件构建待用户执行 |
| 接收和线程 | LightUartBridge.cpp → BridgeUartRuntime.cpp：半帧超时、重同步、40条复制事件队列；Matter 线程应用 | 队列突发/调度失败/持续收发待实板验收 |
| 握手/清单 | runtime + BridgeUartLifecycle：HELLO→清单 CRC32→创建/恢复→顺序 Bind | 清单校验已有源码；完整实板时序待验 |
| 在线门槛 | 创建和恢复默认离线；Bind 后需有效完整快照才 Reachable=true | lifecycle Ready 表示绑定阶段结束，不表示所有子设备已收到快照 |
| 身份/持久化 | runtime 内32条记录；KVS key bridge-uart-v2；版本/CRC、绑定高水位、pending add/remove；恢复指定 Endpoint | 当前 UniqueID=bridge-uart-%08lx；无额外 Bridge 命名空间；KVS 断电原子性未验 |
| 增删/上下线 | runtime 处理 Add/Remove/Online/Offline；清单缺项保留离线；删除清理映射 | 正常和重传/重绑定缓存边界均需验收；不能仅凭存在 handler 标记完成 |
| 状态 | runtime 校验 session/device/endpoint/binding/version/范围；HS/CT完整快照、同模式增量、读响应 | 生产路径不经旧 BridgeUartAttribute.cpp；主动诊断 StateRequest 入口未完成 |
| Matter命令 | observer 接管命令，runtime 保存 CommandHandler::Handle，匹配 MCU 响应后完成；单在途忙时返回 Busy | OnLevel 开灯转换已有；完整 Options/nullable/Move/Step/Stop 与 App 参数组合待验 |
| HS/CT | BridgeDevice::SetHueSaturationState 成对更新；BridgeApp 模板去掉 XY 属性/accepted commands；runtime 阻止 XY | 源码仍有未启用的旧 XY helper/encoder；产品不暴露；无需新增颜色转换模块 |
| 演示设备 | AppTask 已删除短按批量添加/删除4个演示设备 | 无默认演示设备；已有持久化设备仍会离线恢复，不等于空映射 |
| 故障处理 | 写存储失败离线并拒绝后续通知成功；断线清旧会话标记；请求有界重试、心跳重探测 | 实板失败注入及断电未验 |
| 构建入口 | CMake 已包含 BridgeUartRuntime 和 BridgeUartLifecycle | 不包含 BridgeColorAdapter；当前固件未由本轮编译/烧录 |
| PC模拟器 | 启动清单、绑定、快照、心跳、即时控制、状态保存、JSONL日志 | 无交互增删/上下线/主动报告；非零渐变和多数Move/Step明确拒绝 |
| 自动smoke | run_smoke.py：UART模拟器+chip-tool，按实际绑定取 Endpoint | 入口已存在，未实板通过；不是手机主流程，不与手机runner并占串口 |

## 3. 实际模块路径

~~~text
手机 App / 家庭中枢
    ↕ Matter（Wi-Fi）
BridgeUartCommandObserver → BridgeUartRuntime → UART MCU
                               ↑
LightUartPort → LightUartBridge（解析/复制事件）
                               ↓ Matter线程
                    BridgeUartRuntime
                     ├─ BridgeUartLifecycle（清单事务）
                     ├─ KVS映射、pending、心跳、响应
                     └─ BridgeApp / BridgeDevice（实际属性和报告）
~~~

注册表与状态应用当前在 runtime 内，不再计划新增 BridgeDeviceRegistry 或 BridgeColorAdapter 文件。TX 由 Matter 线程上的 runtime 发起；不能额外引入任务绕过该入口直接发送协议帧。

## 4. 剩余开发顺序及门槛

| 阶段 | 当前状态 | 下一步及通过门槛 |
| --- | --- | --- |
| T0 规范/环境 | 已核对源码与 HS/CT 决策 | 固定本次固件版本/hash，用户编译烧录；记录手机App/OS/中枢版本 |
| T1 主机协议/模拟器 | 12项Python测试通过，模拟器部分实现 | 先补手机联调控制入口与真实TX/RX证据，新增针对性主机测试 |
| T2 会话/流接收 | 已有源码、实板待验 | 空清单 HELLO/list/heartbeat；两端分别重启能恢复，无需重新配网 |
| T3 动态设备/NVM | 已有源码、实板待验 | 手机配网后 Add CW+HS/CT；离线保留、删除移除、重启身份稳定；32/33容量及持久化单独验收 |
| T4 状态 | 已有源码、实板待验 | UART快照/增量→手机实际更新；模式切换用快照；无非法状态应用 |
| T5 手机命令 | 转发与异步响应已有；模拟器执行能力有限 | 手机控制→UART命令→MCU响应/实际上报→手机收敛；按App实际命令补渐变/Move/Step支持 |
| T6 PC+Bridge验收 | 尚未执行 | 手机计划P00–P12适用项有逐项证据；扩展容量/断电/持续流量另记录 |
| T7 真实MCU | 尚未执行 | 保持手机步骤和日志格式替换MCU，验证数值、渐变、Stop及真实发光 |

T1 联调工具需要提供：单进程独占 UART，交互式 add/remove/offline/online/state，先等通知响应再推进 Bind/快照，串行管理 MCU 方向请求，持久化删除状态及版本，记录真实收发时间和解码参数。完整用例见手机测试计划。以上是待开发接口，当前 mcu_simulator.py 没有这些命令。

不以随机丢包作为正常场景；异常验收采用一次明确的拒绝/延迟/旧绑定/坏帧。硬件验收发现的偏差先记录原始帧和源码位置，再定向修复。

## 5. 可用工具与当前证据

~~~bash
python3 -B -m unittest discover -s tests/bridge_uart_v2 -v
python3 tools/bridge_uart_v2/mcu_simulator.py --help
python3 tools/bridge_uart_v2/run_smoke.py --help
~~~

协议口 /dev/ttyUSB0，921600；日志口 /dev/ttyUSB2，2000000，重插后检查实际映射。当前模拟器支持 --devices、--state-dir、--log；已有状态文件优先于 fixture，修改 fixture 不会覆盖旧状态。

此前 chip-tool 位于 SOME-PATH/chip-tool，历史 Node ID=1234 只属于当时 Fabric。手机配网后不假定该身份可用；如需 chip-tool 诊断，先核实共享 Fabric / multi-admin 状态，不自动重配网或清除手机配网数据。

执行细节与历史验证限制见[开发记录](matter-bridge-uart-v2-development-record.md)。每阶段以本次固件和日志证据为准；旧的 Aggregator EP1 属性探测不是动态设备闭环成功证据。
