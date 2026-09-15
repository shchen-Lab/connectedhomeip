# MCU Guide 实机联调结果

日期：2026-09-15

## 环境

- Bridge UART1：`/dev/ttyUSB0`，921600 8N1，无硬件流控
- Bridge 日志：`/dev/ttyUSB2`
- MCU 侧：`tools/bridge_uart_v2/phone_session.py`
- 测试会话：MCU session `1557938726`
- 日志：`uart.jsonl`、`actions.jsonl`、`bridge.log`

## 已执行

| 用例 | 结果 | 证据 |
| --- | --- | --- |
| 空清单 HELLO/LIST | PASS | HELLO response、空 LIST_END |
| 添加 CW `uartDeviceId=1` | PASS | Endpoint 3、binding 10、CT snapshot mask `0x0043` |
| 添加 RGBCW/HSCT `uartDeviceId=3` | PASS | Endpoint 4、binding 11、HS snapshot mask `0x000F` |
| CW 状态更新 | PASS | `stateVersion=2`，On=1、Level=254、CT=300 |
| HS 状态更新 | PASS | `stateVersion=2`，On=1、Level=200、Hue=80、Sat=140 |
| Offline/Online | PASS | Offline ACK 后 Online ACK，再发送完整 snapshot |
| Online 无 snapshot | PASS | Bridge 发 `DEVICE_STATE_REQUEST`，连续重试后 `re-sync exhausted` |
| 同版本冲突 snapshot | PASS | `stateVersion=11` 内容冲突，Bridge response status=`0x08` |
| 删除 CW 和 RGBCW | PASS | Remove response status=`OK`，模拟器设备库最终为空 |

## 关键证据

Bridge log 中包含：

```text
UART state applied snapshot ep=3 dev=1 version=2 mask=0043 values=01 fe 2c 01
UART state applied snapshot ep=4 dev=3 version=2 mask=000f values=01 c8 50 8c
UART re-sync exhausted dev=3 ep=4
UART snapshot rejected status=8 ep=4 dev=3 version=11 expected=11
```

本次测试没有执行 Matter controller/chip-tool 读写验证，主要验证 Guide 定义的 UART 生命周期、状态快照和错误处理；Matter 侧的最终属性读回仍应在对方 MCU 固件完成后按 P06-P08 执行。
