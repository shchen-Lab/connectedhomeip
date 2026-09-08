# Reachable 离线验证

Bridge 重置后的持久化动态设备 Endpoint 4 保留。启动空清单 MCU 模拟器后，Bridge 完成 HELLO/LIST（空清单），没有删除 Endpoint。

chip-tool 命令：

`bridgeddevicebasicinformation read reachable 1234 4`

结果：Endpoint 4 返回 `Reachable: FALSE`。该结果证明设备通信/清单离线时保留动态 Endpoint，但对 Matter 标记不可达；与之前 UART Offline 通知和 Bridge console 日志一致。
