# UART v2 HS/色温开发记录

日期：2026-09-08。当前规范：Obsidian Rev.4（SHA-256 fd2266a6be5c637d4bea0cfd4f1889ef82113445bcce80b696c8cc57b8a0f705）。
Rev.4 已将用户 HS/色温决定写入协议正文：Bridge 不提供 XY 能力或转换，RGB 物理输出由 MCU 负责；v2 线格式和第24节字节向量不变。
历史 Rev.3 指纹为 be7b7843858fed77e8cac633cc116896aa47030bbff9774899b60b347a48a925，不再作为当前设计版本。

## 文档对齐与当前联调路线

本轮按源码重写 Code Plan，移除过时的 v1 现状、待新增颜色适配模块和 XY 正向验收任务。
已将“已有实现”和“待构建/实板验收”分开；不宣称全部协议行为已验收。

主流程改为用户手机配网/控制，助手通过 UART 模拟 MCU 产生动态设备和状态变化。
见[手机联调测试计划](matter-bridge-uart-v2-phone-test-plan.md)，包含 P00–P12、M1–M6 工具前置任务和结果记录格式。
目前模拟器没有交互式 Add/Remove/Online/Offline/主动报告入口；手机可能使用的渐变/Move/Step 也未完整支持。
这些工具缺口需先补齐；现有固定清单启动流程不能替代“手机配网后在线 Add”的验证。
当前 JSONL 部分 tx 在帧构造/排队时输出，缓存重传 rx 记录也不完整，正式测试前需改为真实收发时刻和解码参数记录。

## 当前实现

- 启动 HELLO、完整清单 CRC32 校验、动态 Endpoint 创建、Bind 提交、完整快照上线。
- 32 条映射；持久化格式版本与 CRC；先写绑定号高水位，再写待绑定记录；恢复固定 Endpoint 为离线。
- 删除先持久化 PENDING_REMOVE，重启完成清理；清单缺项保留 Endpoint。
- UART 事件复制到有界队列，Matter 线程校验 session/device/endpoint/binding/version，再应用状态。
- Snapshot 必须 ACK_REQUIRED；合法业务拒绝不触发字节流重解析；半帧超时不反复保留原 SOF。
- Matter 命令保存异步句柄，以匹配的 MCU response 完成请求；不调用默认 handler 修改实际状态。
- HS FeatureMap=0x11（HS+CT）；无 XY 属性或 accepted commands；HS 成对更新保持模式0，CT保持模式2。
- 持久化失败立即离线并拒绝后续通知成功响应；断线清除旧清单删除标记；BUSY 不无限延长请求期限；定时器恢复事件调度失败。
- 模拟器支持即时 OnOff、Level、HS、CT；非零渐变和未实现 Move/Step 明确返回不支持。

## 已执行验证

`python3 -B -m unittest discover -s tests/bridge_uart_v2 -v`：12 项通过。
覆盖 codec 基础向量、HS/CT 快照、ACK_REQUIRED、即时控制、重复 Toggle、旧绑定、
非法 HS/CT、XY 拒绝、清单 CRC 和新 Bridge boot 清除模拟器绑定。

更新了生产 C codec 和生命周期 C++ 测试；遵照用户“编译由用户执行”的要求，本轮未编译执行。
以上 Python 测试不证明 C++ 生产代码可构建或实板闭环通过。

## 备用 chip-tool smoke（非当前手机主流程）

协议口 /dev/ttyUSB0，921600 8N1，无流控；日志口 /dev/ttyUSB2，2000000。
历史 chip-tool Node ID 为1234；以下入口只适用于核实该 Fabric 仍可访问设备的情况。手机配网不自动赋予旧 chip-tool 访问权，需要时确认 multi-admin；不自行重新配网。

```bash
python3 tools/bridge_uart_v2/run_smoke.py \
  --port /dev/ttyUSB0 --chip-tool SOME-PATH/chip-tool --node 1234 \
  --output test_results/bridge_uart_v2/hs_ct_smoke
```

运行器先等待两个模拟设备完成 UART 绑定，再从实际分配结果获取 Endpoint，
使用 chip-tool 验证 Reachable、OnOff、Level、HS、CT 和 ColorMode。
输出 uart.jsonl、matter.log 和 summary.json；端口须由测试进程独占。
本轮尚未运行此硬件测试。不要与手机联调模拟器同时占用协议串口。

## 尚未验收

- 2026-09-08 chip-tool 重配网后，Endpoint 3/4 的 Descriptor、Reachable、FeatureMap/ColorCapabilities、OnOff/Level/HS/CT 控制闭环已通过；模拟 MCU Remove 后 Endpoint 3 返回 UNSUPPORTED_ENDPOINT、Endpoint 4 仍在线。证据见 test_results/bridge_uart_v2/phone-20260908-02/。
- 补充验证：空清单导致保留 Endpoint 4 的 chip-tool Reachable 明确返回 FALSE；证据见 test_results/bridge_uart_v2/phone-20260908-03/results.md。

- 新固件构建、用户烧录，以及手机测试计划 P00–P12 适用项。
- 断电期间 KVS 写入原子性及 PENDING_ADD/PENDING_REMOVE 恢复。
- 32设备、30分钟混合流量、队列高水位/heap/RX drop。
- 完整 Move/Step/渐变/Stop 与真实 MCU 物理输出测试。
- 独立主动 StateRequest 诊断入口和完整异常注入 runner。

旧 UART 探测（向 Aggregator 发送未绑定属性）只能作为接收/拒绝证据，
不得计为动态设备上报闭环 PASS。
