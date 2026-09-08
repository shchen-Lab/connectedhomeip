# 手机 UART 联调记录

## UART 主动关灯与上下线

- 用户确认主动On=1/Level=64后App自动更新，P03快照刷新部分通过（不是增量验收）。
- 主动关灯：seq17/version10，0x41 OK。随后App主动On seq295、Off seq296均完成响应/快照，最终关灯。
- Offline：0x3E seq20/version13，0x3F OK；ONLINE恢复前实际间隔约83.6秒，期间保持心跳。
- Online：0x3C seq21/version14，收到0x3D OK后才发送完整快照seq22；0x41确认应用version14。
- Endpoint=3、binding=2不变；恢复状态On=0、Level=64、CT=250，无Remove/Add操作。
- UART关灯/上下线响应已确认；手机关灯、离线提示及恢复同一卡片仍待用户反馈。没有延迟快照阶段，尚不计“Online ACK后无快照仍离线”的独立验收。

2026-09-08；用户自研 App，用户已确认配网成功。保留手机配网，未复位/烧录。
串口：ttyUSB0 921600；console：ttyUSB2 2000000。
固件构建/hash未取得；实板主动发送v2 HELLO，console显示Matter报告获得StatusResponse成功。

| 用例 | UART/Bridge证据 | 手机结果 | 总状态 |
| --- | --- | --- | --- |
| P00 | HELLO seq126；空清单transaction1；持续心跳成功 | 配网成功由用户确认，空设备UI未确认 | PARTIAL |
| P01 | Add device1/CW seq3返回OK，Endpoint3/binding1；Bind seq139；CT快照seq4返回OK，console ONLINE | 等用户确认新增卡片及初始状态 | PARTIAL |

用户已确认第一台灯出现并上线；App只看到OnOff/Level，色温入口问题待查。

手机控制捕获：Off seq205、On seq206、Level=198 seq208、Level=86 seq245、Level=229 seq246、Off seq250、On seq251。
所有命令响应0x12均OK；对应快照seq9–15均收到0x41 OK，stateVersion递增2–8。
Level使用MoveToLevelWithOnOff，transition=0，Options=0。console可见实际属性更新与Matter InvokeCommandResponse。
P05/P06 UART与Bridge侧通过，App最终状态仍待用户明确确认；最后On=1/Level=229/CT=250。

用户要求UART主动刷新：发送完整CT快照seq16，version9，On=1、Level=64（约25%）、CT=250。
收到0x41 OK确认应用version9；手机是否自动更新待用户反馈。本次是完整快照0x40，不是无ACK增量0x20。

初始状态：On=1，Level=128（约50%），CT=250mired（4000K）。
设备名称UART-00000001。模拟器保持运行，接收手机命令并保存uart.jsonl / bridge.log / actions.jsonl。

观察项：Add使清单版本1→2；下一次heartbeat触发Bridge断线重同步，Endpoint保持3，binding更新为2，快照seq8确认后重新ONLINE。
不是丢包；属于当前Bridge发现清单版本改变后的实现行为。记录手机是否出现短暂离线/重复卡片，不能把动态添加体验直接标PASS。

主机新增phone_session入口支持JSON add/list/state快照/offline/online/remove；6项通知时序测试新增，总计18项Python测试通过。
此入口仍不支持增量state操作、自动删除重启续传、完整异常注入、非零渐变/多数Move/Step，不作为全量模拟器验收。
