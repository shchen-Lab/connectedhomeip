# UART v2 再同步与属性验证 Code Plan

日期：2026-09-14。状态：待实施。

## 目标与边界

修复再同步失败时连续请求、共享 pending 被长期占用的问题；修正模拟器无有效状态时的响应；使用 UART 与 Matter 属性读取/订阅共同验证设备恢复和亮度一致性。

本计划接续 `matter-bridge-uart-v2-code-plan.md`，以现有 UART v2 协议为约束。UART 丢包不是主要假设，重点覆盖 MCU 暂无状态、快照延迟、错误响应等合法或可恢复情况。保持 CT、HS/色温能力，不增加 XY/RGB 转换。不清除配网，不改变已持久化的设备身份或 endpoint。用户负责固件编译和烧录。

## 已确认事实与待验证结论

- `NextBind()` 对 `bound && online && !synced` 发起状态请求；响应结束后再次调用 `NextBind()`，缺少独立的等待快照阶段和设备级退避。
- MCU 返回错误或 OK 后未提供有效快照时，可能反复创建新请求；现有单包重传次数不能限制这种新的事务循环。
- 模拟器 `STATE_REQUEST` 未检查 `state_valid`，可能把无效状态发送为有效快照。
- Reachable 的读取真值来自 `BridgeDevice`；专用 cluster callback 生成 ReachableChanged 事件，通用 reporting callback 通知属性变化。没有证据支持增加另一份属性缓存。
- 现有硬件记录支持正常恢复及订阅重建后的控制成功；尚未完整验证错误响应、旧版本注入和实际 Matter 属性值。Apple Home 回跳原因仍需以同一 endpoint 的 Read/Subscribe 数值补证。

## P0：确认约束并固定回归基线

- [ ] 阅读当前协议的状态请求/响应、BUSY、状态版本、绑定及重试规定，确认实现参数与协议一致。
- [ ] 记录代码 revision、未提交 diff、固件构建标识、设备 ID/endpoint/binding、模拟器 state 文件和日志目录。
- [ ] 保留现有 R0–R9 证据；端口按实际枚举确认，不能假定 ttyUSB 编号始终不变。

验收：每次测试能关联具体固件与模拟器版本，未重置配网。

## P1：设备级再同步调度

主要文件：`examples/bridge-app/bouffalolab/common/BridgeUartRuntime.cpp`。

- [ ] 在 Record 中表达待请求、请求进行中、等待快照、退避及本轮停止的状态；字段以实现所需为限，不能只增加一个无超时的布尔标志。
- [ ] 将协议单包重传与设备级恢复尝试分开计数。OK 响应只结束请求事务，不等于状态同步成功；只有合法快照应用成功才设置 synced/Reachable。
- [ ] OK 后进入等待快照阶段，释放共享 pending 供其他设备使用；快照等待到期后才允许下一次恢复尝试。
- [ ] 允许快照先于响应到达。后续响应不得将已同步设备退回等待状态，也不能提前释放不属于本请求的 pending。
- [ ] 应用有效快照后取消该设备恢复等待及重试计划；普通重复快照保持幂等。
- [ ] 使用协议已有时间参数；若未规定，实施初始值采用快照等待 1 秒、重试间隔 1/2/4 秒、每轮最多 3 次新请求，并在测试后记录最终参数。单包重传沿用已有规则。
- [ ] 一轮耗尽后保持设备不可达并暂停主动请求；通过合法 online、重绑定或新会话重新启动恢复，主动到达的有效快照仍可恢复设备。
- [ ] 选择到期设备时避免首个失败设备长期占优；等待/退避不阻塞其他设备绑定、命令及心跳。
- [ ] 删除、离线、断链、重新绑定时清理恢复状态；异步结果继续校验 session/device/endpoint/binding，避免旧事务改变新设备。

响应处理表（具体状态码以协议为准）：

| 响应 | 处理要求 |
| --- | --- |
| OK | 等待有效快照，不能立即再次请求 |
| BUSY | 保留现有有界事务期限，重复 BUSY 不能无限延长 |
| STATE_UNAVAILABLE | 保持不可达，设备级退避，限制新事务次数 |
| DEVICE_OFFLINE | 停止本轮恢复，保持不可达，等待上线流程 |
| BINDING_MISMATCH / UNKNOWN_DEVICE | 进入协议规定的绑定/清单恢复；不得立即重复相同状态请求 |
| 非法响应 / 无响应 | 有界超时；保留协议要求的链路恢复行为，记录具体原因 |

验收：响应后快照正常到达只产生一次恢复请求；持续状态不可用时请求有间隔且有限；另一盏已上线灯仍可控制。传输超时若按协议必须全局重连，应单独验证并注明影响。

## P2：模拟器状态语义与可控异常

主要文件：`tools/bridge_uart_v2/mcu_simulator.py`、`phone_session.py`。

- [ ] STATE_REQUEST 检查 state_valid；无有效状态时返回 STATE_UNAVAILABLE 且不发送快照。
- [ ] 核对请求 payload 和身份校验、错误 flag/status、响应长度，保持与协议一致。
- [ ] 增加仅限测试的状态请求响应控制：无有效状态、OK 后延迟/不发快照、指定错误、持续 BUSY。
- [ ] 延迟通过事件循环调度，不能 sleep 阻塞另一设备或心跳；模拟器正常模式不受异常配置影响。
- [ ] 增加旧版本和同版本冲突快照注入；异常帧不修改模拟器持久化正常状态。
- [ ] 延迟快照/抑制快照按设备或事务保存，避免当前单个 defer_snapshot 字段在两设备操作时互相覆盖。
- [ ] 记录注入参数与 UART sequence/session/binding，输出可重放操作说明。

验收：无效状态不会被模拟器伪装成有效状态；两个设备并行操作互不覆盖注入设置。

## P3：诊断日志与属性证据

主要文件：`BridgeUartRuntime.cpp`、必要时 `BridgeApp.cpp` / `BridgeAttributeAccess.cpp`。

- [ ] 再同步日志包含 device、endpoint、binding、阶段、尝试次数、响应状态、超时原因；状态应用记录版本及接受/拒绝结果。
- [ ] 报告日志明确区分“已标记属性变化”“实际属性读取”“已发送 ReportData”，避免把 callback 执行当成客户端已收到报告。
- [ ] 仅在调试开关开启时记录 Reachable、OnOff、CurrentLevel、HS/色温的实际读取值及 endpoint；控制日志量。
- [ ] 不添加重复属性存储，不直接照搬 BL702 的 Refresh；如需参考，先确认其真实职责与本实现的数据模型。

验收：一次手机控制可串联 UART 命令、响应、快照、Bridge 读取值及 Matter 客户端接收值。专用事件回调与属性 reporting 均保持正确职责。

## P4：测试与验收矩阵

优先使用已有测试入口；补充有行为断言的模拟器测试。固件侧若没有可直接运行的 host seam，以硬件 UART 注入验证真实调度，不复制生产算法制作自证测试。

| 用例 | 输入/操作 | 必须验证 |
| --- | --- | --- |
| S1 正常恢复 | 两灯 offline→online，正常快照 | 同 endpoint 恢复；首次有效快照前不可达 |
| S2 延迟快照 | OK 后延迟，但在等待期限内发送 | 不重复新请求，最终在线 |
| S3 永无快照 | OK 后持续不发 | 有界次数/退避，耗尽后不忙循环 |
| S4 状态不可用 | state_valid=false，随后恢复 | 错误响应无快照；另一灯可控；合法恢复触发后上线 |
| S5 BUSY/错误 | 持续 BUSY、OFFLINE、绑定错误 | 不无限延长；分别进入规定恢复路径 |
| S6 顺序竞态 | 快照先到、响应后到；两灯连续 online | 不降级已同步状态，不覆盖另一设备等待设置 |
| S7 版本校验 | 旧版本、同版本冲突、同版本相同内容 | 前两者拒绝且属性不变，后者幂等 |
| S8 生命周期 | 等待时删除重加、MCU 重启、Bridge 重启 | 无旧结果污染；持久化身份按预期恢复 |
| S9 属性闭环 | 手机亮度 25→75→100→25%，两灯分别测 | UART 最终值与 Matter Read/Subscribe 一致，无跨设备串值 |
| S10 Reachable | offline→online，订阅保持 | 实际读取 false→true；确认报告值与事件，不仅 ONLINE 日志 |
| S11 冷启动订阅 | Bridge 重启后观察手机 | 记录 CASE、SubscribeRequest/Response、首份有效属性报告时刻与 UI |

Matter 验证使用已授权且具备 fabric 访问权的 chip-tool；若尚无访问权，先记录此限制，使用手机观测加实际属性读日志，不将该证据等同于 chip-tool 订阅通过。新增 fabric 不得通过清除手机配网实现。

每项保存动作、原始 UART/Bridge 日志、endpoint、时间点、预期/实际值及 PASS/FAIL/NOT_RUN。命令数量不必等于快照或报告数量，按状态变化和最终一致性判定。

## P5：更新测试结论与交付

- [ ] 修订 `test_results/bridge_uart_v2/resync-20260914-results.md`：原记录保留，附加审查说明；“最多多一次请求”限定为已测条件，不作为代码保证。
- [ ] R8 改为“本轮未观察到 UART 状态回退，现象与订阅恢复延迟相关”；补充实际属性读/订阅证据后再确定归因。
- [ ] 校正订阅初始化 ReportData 分块描述；旧版本拒绝测试单独记录，不以正常连续调光替代。
- [ ] 在新的 test_results 子目录保存本轮结果，并更新 phone-test-plan 的操作步骤和异常注入说明。

交付顺序：P0 → P1/P2 → P3 → 用户编译烧录 → P4 → P5。每阶段说明改动与检查结果；本计划完成仅表示计划落盘，不代表功能或硬件验收已完成。


## 实施状态更新（2026-09-14）

已落地：

- re-sync 增加设备级等待快照、超时、最多 3 次尝试和退避；STATE_RESPONSE=OK 不再立即重复请求。
- 模拟器在 state_valid=false 时返回 STATE_UNAVAILABLE，并支持两个设备独立延迟快照。
- Level/Color Options 增加运行时存储和读回；写入限制为 bit 0。
- OnLevel、启动色温增加 nullable/范围校验，并触发对应属性 reporting。
- OnOff、CurrentLevel、当前色温增加输入范围校验。
- 保持 BridgeDevice 为属性读取唯一真值，不增加 Reachable 第二缓存；Reachable 专用事件 callback 与通用 reporting 分工保留。

待硬件验证：

- 用户编译烧录后的 C++ 构建、双设备恢复、状态不可用和快照超时路径。
- Options/OnLevel/启动色温的 Matter Read、Write、Subscribe 结果及重启后的持久化策略。
- BL702 与当前模板的完整 DeviceType、Descriptor、DataVersion、reporting 生命周期对照。
- 动态端点删除后迟到 reporting 工作项、旧版本/同版本冲突快照，以及实际 Matter ReportData 数值。

说明：上述“已落地”仅指代码修改和静态 diff 检查；未编译的项目必须由用户烧录后按 P4 用例验收。
