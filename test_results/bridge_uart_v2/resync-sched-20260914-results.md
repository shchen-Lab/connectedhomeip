# 再同步调度验证结果（resync-code-plan P4，2026-09-14）

固件：resync-code-plan P1/P3 改动（设备级再同步调度 + 诊断日志）。
基线：HEAD `7e7d216473` + 未提交 diff；固件 bin md5 `4ca34c22`；phone_session `5aea027b`；mcu_simulator `4a33c26e`。
证据目录：[resync-sched-01](resync-sched-01/)（S1–S5 前半）、[-02](resync-sched-02/)（S7 重测）、[-03](resync-sched-03/)（S7c/S8a）、[-04](resync-sched-04/)（S8b/S8c/S9/S10/S11）。
测试计划：[resync-sched-20260914-test-plan.md](resync-sched-20260914-test-plan.md)。
配网：未清除；chip-tool 经 multi-admin（manual code 03519414369）加入为第二 fabric（Node 5678），手机配网保留。

## 结果总表

| 用例 | 状态 | 关键证据（bridge.log tick） |
| --- | --- | --- |
| S1 正常恢复 | PASS | -01/824506-831522：两灯同 endpoint 恢复，快照先到零 re-sync，快照前保持 OFFLINE |
| S2 延迟快照 | PASS | -01/887539-888057：恰好 1 次请求，OK 等待，517ms 快照到达即恢复，无超时 |
| S3 永无快照 | PASS | -01/943749-962899：3 次请求（间隔 2s/3s），每次等待 1s 超时，+9.2s exhausted，10s 冷却零新请求；期间心跳正常；clear 后恢复 |
| S4 状态不可用 | PASS | -01/1035499-1053477：STATE_UNAVAILABLE(15) 三次退避（+1/+2/+3s）→ exhausted；valid 恢复后状态机复位，v13 上线 |
| S5 BUSY | PASS | -01/1113949-1121137：同 seq 重传 1+3 次有界 → 断链 → 重连（注入随新 HELLO 清空）→ 两灯 ~6s 恢复。影响：BUSY 触发全局重连 |
| S6 两设备并发 | PASS | -01/1162990-1163511：pending 串行排队，各自恢复零污染；"快照先于应答"变体在会话建立时自然验证（771721-771734，未退回等待） |
| S7 版本校验 | PASS | -02/1543426（冲突首帧因 OnOff 字节越界被拒 status=1——字段校验生效）；-03/1834397 `rejected status=8 version=29 expected=29`（同版本不同内容）；同版本同内容幂等（全程多次自然重放） |
| S8 生命周期 | PASS | a) -03/1879403-1886501：等待中 remove→旧事务安全终止，重加 version=0 起步无污染；b) -04/1932788+：MCU 重启原 endpoint 恢复；c) -04/519-707：Bridge 复位 `, restored` 原位恢复，<1s |
| S9 亮度闭环 | PASS | -04：CW 拖动正常；彩灯 v6-v15 链路（165→181→224→211→178→208→216→120→196→198）全部 command finished 0x00、快照即时原子应用、无 rejected；chip-tool（独立 fabric）Read CurrentLevel=208 与 UART 真值一致；**无跨设备串值** |
| S10 Reachable | PASS | -04/311916-319937：offline→`OFFLINE`+Reachable callback；online→快照后 `ONLINE`+Reachable callback；endpoint 保留 |
| S11 冷启动订阅 | PASS | -04：519ms restored → 631-707ms 双灯 ONLINE → 74.5s CASE Sigma2Resume → 74.69s SubscribeRequest（CASE 后 150ms）→ 74.7-75s 5 条全量 ReportData。本轮 UI 未观察到上轮的陈旧值问题 |

## 亮度"回跳 50%"归因（S9 首轮现象）

- Bridge 状态真值单调推进，全程无 128（50%）回写；每条变更 ReportData 均被 App ACK。
- chip-tool 独立 fabric Read/Subscribe（初始报告 `CurrentLevel: 208`，DMG 解码 Endpoint=4 Cluster=8 Data=208）与 UART 真值一致。
- App 强制重开后**未向 Bridge 发起任何 Read/Subscribe**（日志零查询），直接渲染本地缓存 → 判定为 App/中枢侧卡片缓存问题，非固件缺陷。
- 后续复测未再复现（缓存刷新后消失）。
- 已知客户端风险：Bridge 重启后订阅重建延迟由客户端决定（本轮 75s，上轮 ~7min），期间 UI 可能显示陈旧值。

## 实际参数记录（与代码实现一致）

快照等待 1s；重试退避 attempts×1s（1/2/4s）；每轮最多 3 次；耗尽冷却 10s；单包重传 500ms×3；BUSY/超时按协议走全局断链重连。

## 遗留事项

1. S7 的同版本冲突注入需先破坏字段值（payload[7]）才能触发 status=8；"通知建立版本后的首个冲突内容会被接受"符合协议（通知不携带内容，首个声明该版本的快照定义内容），已记录。
2. 良性双重 re-sync（0x45 与快照之间的窗口）仍然存在，量级每事件多 1 次请求，见上轮记录。
3. 在线 Add 的 deviceListVersion 变化触发全局重连（Reachable 抖动）仍未优化。
4. chip-tool fabric（Node 5678）保留在 Bridge 上，便于后续独立验证；如需移除可经手机 App 或 `chip-tool pairing remove-device`。
