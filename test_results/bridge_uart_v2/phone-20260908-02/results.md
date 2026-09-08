# chip-tool + UART 动态设备验证

日期：2026-09-08。Bridge 已重置；MCU 模拟器从 empty fixture 启动。chip-tool 使用 PIN 20202021、discriminator 3840 和测试 Wi-Fi 完成重新配网，未记录密码。

## 发现与属性

| Endpoint | UART设备 | Matter类型 | 读取结果 |
| --- | ---: | --- | --- |
| 3 | 1 / type 0x10 / caps 0x13 | Color Temperature Light (268) | Reachable=TRUE |
| 4 | 2 / type 0x11 / caps 0x17 | Extended Color Light (269) | FeatureMap=17 (0x11), ColorCapabilities=17 (0x11) |

Endpoint 3 色温初始值 250 mired；Endpoint 4 HS 快照 mask=0x0F。

## 控制闭环

| 操作 | UART证据 | 读取结果 |
| --- | --- | --- |
| EP3 Off | command cluster 0x0006 id 0，seq141；response 0x12 status=0 | 快照 version=2，OnOff=0 |
| EP3 Level 64 | MoveToLevelWithOnOff，seq266，transition=0；response OK | CurrentLevel=64 |
| EP3 CT 300 | MoveToColorTemperature，seq268；response OK | ColorTemperatureMireds=300 |
| EP4 HS 80/140 | MoveToHueAndSaturation，seq275；response OK | CurrentHue=80，CurrentSaturation=140 |
| EP4 CT 320 | MoveToColorTemperature，seq278；response OK | ColorMode=2，快照 CT=320 |

每条命令均有对应完整快照 0x40 和 0x41 status=0；UART 日志见 uart.jsonl，Bridge console 见 bridge.log。

第一次控制命令缺少 chip-tool 要求的 OptionsMask/OptionsOverride，返回参数错误；补齐为 0、0 后通过。该次失败没有产生 UART 控制帧。

## 删除边界

Matter/chip-tool 本轮没有向 MCU 的设备删除协议；删除由 MCU 发送 DEVICE_REMOVE_NOTIFY（0x38）触发。App 删除动态设备不可作为本协议删除测试，之前已观察到 App 无删除入口。需测试删除时使用 phone_session 的 remove 动作或真实 MCU 用户删除事件，并核对 0x39、Endpoint 清除和重复删除。

本轮已执行删除：模拟 MCU 发送 0x38 seq22（device=1, ep=3, binding=4），Bridge 返回 0x39 status=0。
chip-tool 读取 Endpoint 3 随后返回 UNSUPPORTED_ENDPOINT；Endpoint 4 Descriptor 仍为 Extended Color Light，Reachable=TRUE。
因此删除闭环通过，且未误删其他动态设备。
