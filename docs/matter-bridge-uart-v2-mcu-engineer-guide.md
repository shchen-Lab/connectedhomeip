# Matter Bridge UART v2 MCU 开发与联调指南

版本：v2.0 / HS+色温方案 / 2026-09-15

本文给 MCU 固件工程师使用。目标是让 MCU 固件可以独立实现 UART v2，并按统一步骤与 Matter Bridge 联调。本文是实施指南；字段、偏移和字节级定义以同版本的《Matter Bridge App UART v2 MCU 对接协议》Rev.4 为准（当前 Bridge 工作区的原文路径为 `/home/alvin/code/obsidian.md/alvin/Matter Bridge App UART v2 MCU 对接协议.md`）。若两份文档冲突，以评审确认后的 Rev.4 为准，并在双方同步修改版本号。

## 1. 先确认产品边界

本版本只有两种 MCU 设备：

| `deviceType` | 名称 | Bridge 模板 | 能力 |
| ---: | --- | --- | --- |
| `0x10` | `CW_2CH` | Color Temperature Light | On/Off、Level、色温 |
| `0x11` | `RGBCW_5CH` | Extended Color Light | On/Off、Level、HS、色温 |

- MCU 上报的是逻辑属性：OnOff、CurrentLevel、Hue、Saturation、ColorTemperatureMireds。
- MCU 自己把 HS/色温目标转换为物理 PWM。Bridge 不做 RGB 转换，也不做 XY 转换。
- 不实现、不发送、不读取 XY、RGB、CurrentX、CurrentY。收到 XY/RGB 命令必须返回 `UNSUPPORTED_COMMAND`；读 XY 必须返回 `UNSUPPORTED_ATTRIBUTE`。
- `0x10` 永远是色温模式；`0x11` 只在 HS（模式值 0）和色温（模式值 2）之间切换。
- 不增加默认 dynamic device。只有 MCU 清单或 Add Notify 中出现的设备才会创建 Matter Endpoint。
- UART 是板内可靠的顺序链路，正常属性上报不逐帧 ACK，不做周期补包，也不把序号跳变当作丢包。CRC、有限重试和启动同步仍必须实现。

能力位必须精确匹配设备类型：

```text
bit0 CAP_ON_OFF
bit1 CAP_LEVEL
bit2 CAP_HUE_SATURATION
bit3 Reserved（XY，固定 0）
bit4 CAP_COLOR_TEMPERATURE
bit5 CAP_PLUG（固定 0）
bit6..31 Reserved（固定 0）

CW_2CH    capabilityFlags = 0x00000013
RGBCW_5CH capabilityFlags = 0x00000017
```

能力位描述的是 UART 能力，不是 Matter FeatureMap；Bridge 的 RGBCW Matter FeatureMap 为 `0x11`（HS+CT）。

建议在 MCU CI 中固定以下 codec 检查值：

```text
CRC16/CCITT-FALSE("123456789") = 0x29B1，线上为 B1 29
CRC32/ISO-HDLC("123456789")    = 0xCBF43926，线上为 26 39 F4 CB
空清单 entry 集合 CRC32       = 0x00000000
CW snapshot payload            = 01 00 00 00 43 00 01 80 FA 00
                                （stateVersion=1, mask=0x0043, On=1, Level=128, CT=250）
```

## 2. 联调通过标准

交付 MCU 固件前，必须同时满足：

1. 每个真实物理设备拥有永久且不复用的 `uartDeviceId`。
2. MCU 重启后设备清单、设备 ID、待确认删除记录和状态恢复正确；每次启动生成新的非零 `sessionId`。
3. Bridge 重启或 MCU 重启后，均能自动完成 `HELLO -> LIST -> BIND -> SNAPSHOT`，不需要重新配网。
4. Matter 上看到的 Endpoint 类型、Reachable、OnOff、Level、HS/色温与 MCU 最终状态一致。
5. 重复 Add/Remove/Bind、请求重试、旧 session、旧 binding、旧 stateVersion 都不会重复执行或污染状态。
6. 以下指南中的主机模拟器和硬件验收用例全部有 UART 原始日志证据。

## 3. 硬件和串口

| 项目 | 固定值 |
| --- | --- |
| UART | Bridge `uart1` |
| 默认设备节点 | Bridge UART `/dev/ttyUSB0`；Bridge log `/dev/ttyUSB2` |
| 速率 | `921600` |
| 格式 | 8 data bits, no parity, 1 stop bit（8N1） |
| 流控 | 关闭 RTS/CTS |
| 默认 Bridge 引脚 | TX GPIO6，RX GPIO7（以板级配置为准） |
| 字节序 | 所有多字节字段 little-endian |

TX/RX 交叉连接并共地，只接同电平 TTL UART，不能接 RS-232 电平。协议 UART 不能混入文本日志；日志应走独立串口。MCU 发送线程必须一次串行发送完整帧，不能让两个任务交错写入。

## 4. 帧编解码

固定帧布局如下，`N` 为 payload 长度：

| Offset | Size | 字段 |
| ---: | ---: | --- |
| 0 | 2 | SOF `A5 5A` |
| 2 | 1 | version，固定 `0x02` |
| 3 | 1 | messageType |
| 4 | 1 | flags |
| 5 | 2 | sequence，非零 |
| 7 | 4 | `sessionId` |
| 11 | 4 | `uartDeviceId` |
| 15 | 2 | `matterEndpointId` |
| 17 | 4 | `bindingVersion` |
| 21 | 4 | `clusterId` |
| 25 | 4 | `commandOrAttributeId` |
| 29 | 2 | `payloadLength`，0..1024 |
| 31 | N | payload |
| 31+N | 2 | CRC16 |
| 33+N | 2 | EOF `0D 0A` |

`frameLength = 35 + payloadLength`，最大 1059 字节。CRC16 为 CRC16/CCITT-FALSE：初值 `0xFFFF`、多项式 `0x1021`、不反射、异或输出 `0`；覆盖 `version` 到 payload（offset 2，长度 `29+N`），线上 CRC 低字节先发。检查值：ASCII `123456789` 的 CRC16 是 `0x29B1`。

接收器必须按字节流寻找 SOF，检查长度、EOF、CRC 后才交给业务层；CRC/EOF/版本/长度错误只丢弃候选帧并继续找下一个 SOF。半帧 100 ms 未完成时清除该候选。payload 内出现 SOF/EOF 不需要转义，长度负责分帧。坏帧不能删除设备、改变 Reachable 或清除 Endpoint。

### flags

| Bit | 含义 |
| ---: | --- |
| 0 | `RESPONSE` |
| 1 | `ERROR` |
| 2 | `ACK_REQUIRED` |
| 3 | `NULL_VALUE`，本产品固定 0 |
| 4 | `MORE`，本版本固定 0 |
| 5 | `RETRANSMISSION` |
| 6..7 | 保留，发送 0 |

普通请求/通知使用 `ACK_REQUIRED`；成功 response 使用 `RESPONSE`；失败 response 使用 `RESPONSE|ERROR`。属性增量报告和清单 Begin/Entry/End 的 flags 为 0（失败的清单 End 仅带 ERROR）。

### messageType

| 值 | 名称 | 方向 |
| ---: | --- | --- |
| `0x10` | `DOWN_INVOKE_COMMAND` | Bridge -> MCU |
| `0x11` | `DOWN_READ_ATTRIBUTE` | Bridge -> MCU |
| `0x12` | `DOWN_COMMAND_RESPONSE` | MCU -> Bridge |
| `0x13` | `DOWN_READ_ATTRIBUTE_RESPONSE` | MCU -> Bridge |
| `0x20` | `UP_ATTRIBUTE_REPORT` | MCU -> Bridge |
| `0x30/31` | `HELLO_REQUEST/RESPONSE` | 双向 |
| `0x32..35` | `DEVICE_LIST_REQUEST/BEGIN/ENTRY/END` | 双向 |
| `0x36/37` | `DEVICE_ADD_NOTIFY/RESPONSE` | 双向 |
| `0x38/39` | `DEVICE_REMOVE_NOTIFY/RESPONSE` | 双向 |
| `0x3A/3B` | `DEVICE_BIND_REQUEST/RESPONSE` | 双向 |
| `0x3C/3D` | `DEVICE_ONLINE_NOTIFY/RESPONSE` | 双向 |
| `0x3E/3F` | `DEVICE_OFFLINE_NOTIFY/RESPONSE` | 双向 |
| `0x40/41` | `UP_STATE_SNAPSHOT/STATE_SNAPSHOT_RESPONSE` | 双向 |
| `0x42/43` | `HEARTBEAT/HEARTBEAT_RESPONSE` | 双向 |
| `0x44/45` | `DEVICE_STATE_REQUEST/RESPONSE` | 双向 |

### sequence 和重试

- Bridge 与 MCU 各自维护发送序列号，从 1 递增，跳过 0，溢出回 1。
- response 复用被响应请求的 sequence，不推进本端序列号。
- 重试必须复用相同 sequence、payload 和业务字段，并设置 `RETRANSMISSION`。
- 去重键为 `sessionId + messageType + sequence + uartDeviceId`；至少缓存最近 8 个需要响应的请求及其 response。
- response 超时 500 ms；首次发送后最多再重试 3 次（总计 4 次）。`BUSY` 可按同一事务重试，其他业务错误不自动重发。
- Toggle 等非幂等命令在重试耗尽后结果未知，不能换新 sequence 自动再执行；回到 HELLO 恢复流程。

普通 response 的 payload 第一个字节是 status（清单 End 的 status 位于 offset 10）：

| 值 | 名称 | 使用场景 |
| ---: | --- | --- |
| `0x00` | `OK` | 成功 |
| `0x01` | `INVALID_ARGUMENT` | 长度、枚举、范围或保留位非法 |
| `0x02` | `UNKNOWN_DEVICE` | 未知设备 ID |
| `0x03` | `ENDPOINT_EXHAUSTED` | Bridge 无动态 Endpoint 槽位 |
| `0x04` | `DEVICE_ID_CONFLICT` | 类型/能力与已知 ID 不一致 |
| `0x05` | `BINDING_MISMATCH` | Endpoint/binding 不匹配 |
| `0x06` | `UNSUPPORTED_DEVICE_TYPE` | 类型或能力不支持 |
| `0x07` | `BUSY` | 当前事务未完成，可按原事务重试 |
| `0x08` | `BAD_STATE_VERSION` | 状态版本过期或同版本内容冲突 |
| `0x09` | `PROTOCOL_MISMATCH` | major/minor 不兼容 |
| `0x0B` | `INTERNAL_ERROR` | 持久化或内部处理失败 |
| `0x0C` | `UNSUPPORTED_COMMAND` | 不支持的命令/设备能力 |
| `0x0D` | `UNSUPPORTED_ATTRIBUTE` | 不支持的属性 |
| `0x0E` | `DEVICE_OFFLINE` | 已绑定但设备当前离线 |
| `0x0F` | `STATE_UNAVAILABLE` | 无有效状态或属性不属于当前模式 |

`0x0A CRC_OR_FORMAT_ERROR` 只用于本地诊断，不作为坏帧的业务 response。未知 messageType 直接丢弃并记录。

## 5. 会话和持久化

### MCU 必须持久化

```text
uartDeviceId       uint32，物理设备永久 ID，不得分配给另一设备
deviceType         uint8
capabilityFlags    uint32
deviceListVersion  uint32
stateVersion       uint32
state              OnOff/Level/HS/CT/当前模式
pendingRemove      bool，直到 Remove response=OK
endpoint/binding   可缓存，但不是最终所有者
```

`uartDeviceId=0` 保留，禁止使用。添加、删除、类型/能力变化必须先写入设备数据库并递增 `deviceListVersion`，再发送通知。删除时先标记 `pendingRemove`/`DEVICE_PRESENT=0`；收到 Bridge `DEVICE_REMOVE_RESPONSE(status=OK)` 后才能擦除记录。MCU 复位前未收到 OK，下一次清单仍要带该待删除记录并继续 Remove Notify。

每次 MCU 启动或重新建立协议会话生成新的非零 `sessionId`，同一次运行保持不变，不要求跨掉电持久化。新 session 后 `stateVersion` 可以从 1 开始。状态版本针对对外可见的逻辑状态，不按 PWM tick 增长；一次 HS 成对更新或模式切换应作为一个版本并用完整快照表达。

## 6. 启动状态机

MCU 上电后处于 `WAIT_HELLO`，除 HELLO 外的旧 session 请求直接丢弃：

```text
Bridge -> HELLO_REQUEST(sessionId=0, bridgeBootId=B)
MCU    -> HELLO_RESPONSE(sessionId=S)
Bridge -> DEVICE_LIST_REQUEST
MCU    -> DEVICE_LIST_BEGIN -> ENTRY* -> DEVICE_LIST_END
Bridge -> DEVICE_BIND_REQUEST（逐设备）
MCU    -> DEVICE_BIND_RESPONSE
MCU    -> UP_STATE_SNAPSHOT（设备在线且状态有效时）
Bridge -> STATE_SNAPSHOT_RESPONSE
```

收到同一 session 的新 `bridgeBootId` 时，MCU 清除旧 Bridge 请求缓存、取消旧清单事务、使 endpoint/binding 缓存失效，但不撤销正在执行的物理渐变。HELLO 成功前不要发送属性报告。Bind response 先于 snapshot；完整 snapshot 成功前 Bridge 仍会显示 Reachable=false。

## 7. HELLO 和设备清单

### HELLO_REQUEST payload（14 字节）

```text
u8  protocolMajor       = 2
u8  protocolMinor       = 0
u32 bridgeBootId        非零，同一次 Bridge 启动保持不变
u32 bridgeFeatureFlags  当前必须为 0
u16 bridgeMaxPayload    通常 1024
u16 bridgeMaxFrame      必须等于 bridgeMaxPayload + 35
```

HELLO 帧头的 device/endpoint/binding/cluster/id 全为 0，sessionId 为 0。版本不兼容返回单字节 `PROTOCOL_MISMATCH`。

### HELLO_RESPONSE payload

```text
u8  status
u8  protocolMajor       = 2
u8  protocolMinor       = 0
u32 mcuFeatureFlags     当前必须为 0
u16 mcuMaxPayload       至少 1024（双方取最小值）
u32 deviceListVersion
u32 mcuFirmwareVersion  major:8 / minor:8 / patch:16
```

### DEVICE_LIST_REQUEST payload（12 字节）

```text
u32 listTransactionId
u32 knownDeviceListVersion
u8  requestFlags         = 0
u8[3] reserved           = 0
```

MCU 必须锁定一次清单视图，按 `uartDeviceId` 升序发送，包含在线、离线和待删除设备；不要在 Begin/Entry 发送期间修改该视图。清单事务重试使用同一 transactionId 和完全相同的 entry 字节。

MCU 应在收到合法 `DEVICE_LIST_REQUEST` 后 500 ms 内开始 Begin；Bridge 单次事务总超时 5 s，失败最多重试同一 transactionId 3 次。清单期间暂停主动 Add/Remove/Online/Offline，待 End 后按顺序发送；不要因为等待某个 response 而停止接收。

### 清单三种 payload

`DEVICE_LIST_BEGIN`（16 字节）：

```text
u32 listTransactionId
u32 deviceListVersion
u16 entryCount
u16 entrySize = 18
u32 listChecksum32
```

`DEVICE_LIST_ENTRY`（18 字节）：

```text
u32 listTransactionId
u32 uartDeviceId
u8  deviceType
u32 capabilityFlags
u8  stateFlags
u32 stateVersion
```

`DEVICE_LIST_END`（11 字节）：

```text
u32 listTransactionId
u16 receivedEntryCount
u32 listChecksum32
u8  status
```

checksum 是所有 entry payload 按发送顺序拼接后的 CRC32/ISO-HDLC（初值/异或 `0xFFFFFFFF`、多项式 `0x04C11DB7`、输入输出反射）；空清单 checksum 为 0。Bridge 只有在 session、transaction、数量、entrySize、无重复 ID、checksum 和 status 全部正确时才提交清单。错误 End 仍使用 11 字节布局，count/checksum 填 0。

## 8. 设备绑定、添加、删除和上下线

### Bind

`DEVICE_BIND_REQUEST` payload 固定 10 字节：

```text
u8  deviceType
u32 capabilityFlags
u8  bindFlags            bit0=REQUEST_STATE_SNAPSHOT, bit1=ENDPOINT_REASSIGNED
u32 stateVersionHint
```

Bridge 分配 `matterEndpointId` 和非零单调 `bindingVersion`。MCU 校验本次 session、设备类型/能力，成功后缓存绑定并回复：

```text
DEVICE_BIND_RESPONSE payload: u8 status + u16 acceptedEndpoint + u32 acceptedBinding
```

同一绑定重复请求返回 OK，并重新安排一份当前完整 snapshot；不能创建第二个设备。

### Add

新设备先持久化，再发送 `DEVICE_ADD_NOTIFY`。payload 13 字节：

```text
u8  deviceType
u32 capabilityFlags
u32 stateVersion
u8  stateFlags
u8  addReason          0=DISCOVERED, 1=RESTORED, 2=USER_ACTION
u8[2] reserved         = 0
```

帧头 endpoint/binding 必须为 0。Bridge 返回 `status + endpoint(u16) + binding(u32)`；已知 ID 且类型/能力一致时只返回已有映射。

### Remove

只有物理设备明确删除才发 Remove；通信掉线、电源关闭、暂时不在无线扫描中都不能发 Remove。payload 8 字节：

```text
u8  removeReason       0=PHYSICAL_REMOVED, 1=USER_REMOVED, 2=MCU_DATABASE_RESET
u32 stateVersion
u8  removeFlags        = 0
u8[2] reserved         = 0
```

Bridge 成功清理 Dynamic Endpoint 和 NVM 映射后才回复 OK。旧 endpoint、旧 binding、旧 session 的报文之后必须被拒绝。相同 Remove 重传返回 OK。

### Offline / Online

两者 payload 均为 8 字节：

```text
u8  reason
u32 stateVersion
u8  stateFlags
u8[2] reserved = 0
```

Offline reason：`0=DEVICE_REPORT, 1=DEVICE_TIMEOUT, 2=DEVICE_POWER_OFF, 3=DEVICE_ERROR`；Online reason：`0=DEVICE_REPORT, 1=COMMUNICATION_RESTORED, 2=DEVICE_POWER_ON`。Offline 必须 `DEVICE_PRESENT=1, DEVICE_ONLINE=0`，只把 Reachable 置 false，保留 Endpoint；Online 必须先等待 response，再发送完整 snapshot：

```text
MCU -> DEVICE_ONLINE_NOTIFY
Bridge -> DEVICE_ONLINE_RESPONSE(status=OK)
MCU -> UP_STATE_SNAPSHOT
Bridge -> STATE_SNAPSHOT_RESPONSE(status=OK)
```

快照成功前不得认为 Reachable=true。状态尚无效时可 `STATE_VALID=0`，等状态有效后再发快照。

## 9. 状态编码与报告

### stateFlags

```text
bit0 DEVICE_PRESENT
bit1 DEVICE_ONLINE
bit2 STATE_VALID
bit3..7 reserved = 0
```

### 状态范围

| 属性 | 范围/规则 |
| --- | --- |
| OnOff | 0 或 1 |
| CurrentLevel | 1..254；关灯时仍保留上次逻辑亮度 |
| CurrentHue | 0..254，仅 `0x11` |
| CurrentSaturation | 0..254，仅 `0x11` |
| ColorTemperatureMireds | 154..454 |

### UP_STATE_SNAPSHOT

payload 是 `u32 stateVersion + u16 fieldMask + values`。值按 mask bit 从低到高排列：

```text
bit0 OnOff              u8
bit1 CurrentLevel       u8
bit2 CurrentHue         u8
bit3 CurrentSaturation  u8
bit6 ColorTemperature   u16
```

唯一合法完整快照：

| Profile/模式 | mask | 值顺序 | payload 长度 |
| --- | ---: | --- | ---: |
| CW / CT | `0x0043` | OnOff, Level, CT | 10 |
| RGBCW / HS | `0x000F` | OnOff, Level, Hue, Saturation | 10 |
| RGBCW / CT | `0x0043` | OnOff, Level, CT | 10 |

不得混发 HS 和 CT，不得发送 XY mask `0x0033`，也不得只发送 OnOff 而省略 Level/当前颜色。快照必须先完整校验，再原子应用；任何字段非法都拒绝整帧。Bridge 回复 `STATE_SNAPSHOT_RESPONSE`：成功为 `status=OK + appliedStateVersion(u32)`，失败只有 1 字节 status。

### UP_ATTRIBUTE_REPORT

增量报告无 response、无重传，payload 严格为 `u32 stateVersion + value`：

| cluster | attribute | 值 |
| ---: | ---: | --- |
| `0x00000006` | `0x00000000` OnOff | u8 |
| `0x00000008` | `0x00000000` CurrentLevel | u8 |
| `0x00000300` | `0x00000000` CurrentHue | u8 |
| `0x00000300` | `0x00000001` CurrentSaturation | u8 |
| `0x00000300` | `0x00000007` ColorTemperatureMireds | u16 |

只有完成初始快照且在线后才发送增量。HS 成对改变、HS/CT 模式切换、渐变结束和 Stop 使用完整快照。Bridge 按接收顺序在 Matter 线程校验版本并应用；相同版本相同内容幂等，相同版本不同内容拒绝为 `BAD_STATE_VERSION`。版本按 uint32 模运算比较：`a != b && uint32(a - b) < 0x80000000` 表示 a 新于 b，跳过 0 回绕；同一 session 的比较跨度不得达到 2^31。不要为每个 PWM tick 增加版本，也不要因为版本跳跃自行补发。

### DEVICE_STATE_REQUEST / RESPONSE

Bridge 可在上线通知没有及时收到快照、或需要主动恢复状态时请求状态。请求 payload 固定 5 字节：`knownStateVersion(u32) + requestFlags(u8=0)`；帧头带当前 session、设备、endpoint 和 binding。MCU 回复 payload 为 `status(u8) + currentStateVersion(u32)`；status=OK 后必须再发送一份完整 `UP_STATE_SNAPSHOT`，即使版本没有变化。离线返回 `DEVICE_OFFLINE`，无有效状态返回 `STATE_UNAVAILABLE`，失败时不要发送快照。

## 10. Bridge 下行读和命令

### Read

`DOWN_READ_ATTRIBUTE` 的 payload 为空，帧头带当前绑定、cluster 和 attribute。支持上一节的 5 个属性。成功 `DOWN_READ_ATTRIBUTE_RESPONSE` payload 为 `status + stateVersion(u32) + value`（6 或 7 字节）；失败只有 status。离线返回 `DEVICE_OFFLINE`，无有效当前模式返回 `STATE_UNAVAILABLE`。读响应不能代替初始 snapshot。

### Command

`DOWN_INVOKE_COMMAND` 的 payload 是固定字段，不是 TLV；所有多字节数值 little-endian。支持 Cluster：OnOff `0x0006`、LevelControl `0x0008`、ColorControl `0x0300`。

| Cluster/Command | ID | payload |
| --- | ---: | --- |
| OnOff Off/On/Toggle | `0x00/01/02` | 空 |
| Level MoveToLevel / WithOnOff | `0x00/04` | nullable u8, level u8, transition u16, optionsMask u8, optionsOverride u8 |
| Level Move / WithOnOff | `0x01/05` | nullable u8, moveMode u8, rate u8, optionsMask u8, optionsOverride u8 |
| Level Step / WithOnOff | `0x02/06` | nullable u8, stepMode u8, stepSize u8, transition u16, optionsMask u8, optionsOverride u8 |
| Level Stop / WithOnOff | `0x03/07` | optionsMask u8, optionsOverride u8 |
| Color MoveToHue | `0x00` | hue, direction, transition u16, optionsMask, optionsOverride |
| Color MoveHue / StepHue | `0x01/02` | mode, rate/step, transition, optionsMask, optionsOverride |
| Color MoveToSaturation | `0x03` | saturation, transition u16, optionsMask, optionsOverride |
| Color MoveSaturation / StepSaturation | `0x04/05` | mode, rate/step, transition, optionsMask, optionsOverride |
| Color MoveToHueAndSaturation | `0x06` | hue, saturation, transition u16, optionsMask, optionsOverride |
| Color MoveToColorTemperature | `0x0A` | mireds u16, transition u16, optionsMask, optionsOverride |
| Color StopMoveStep | `0x47` | optionsMask, optionsOverride |
| Color MoveColorTemperature | `0x4B` | mode, rate u16, min u16, max u16, optionsMask, optionsOverride |
| Color StepColorTemperature | `0x4C` | mode, step u16, transition u16, min u16, max u16, optionsMask, optionsOverride |

Bridge 当前产品不支持 Color MoveToColor `0x07`、MoveColor `0x08`、StepColor `0x09`；不会下发。MCU 即使收到也必须返回 `UNSUPPORTED_COMMAND`。

执行规则：`transitionTime` 单位 0.1 s，0 表示立即；Level move/step mode 为 Up=0、Down=1；Color move mode 为 Stop=0、Up=1、Down=3；Color step mode 为 Up=1、Down=3；`options` 目前只有 bit0 ExecuteIfOff。非法枚举、范围、Reserved bit 或 `0xFFFF` transition 返回 `INVALID_ARGUMENT`。

`DOWN_COMMAND_RESPONSE` 成功 payload 为 `status=OK + stateVersion(u32) + responseFlags(u8=0)`。OK 只表示 MCU 接受/开始执行，不代表渐变已完成。渐变完成、Stop 或新命令中断后，500 ms 内上报最终实际状态。失败不能提前写入目标值；执行中故障发送 Offline。

## 11. 心跳和整体离线

Bridge 每 5 s 发送 HEARTBEAT。payload：`bridgeMonotonicSeconds(u32) + lastSeenMcuSessionId(u32) + heartbeatFlags(u8=0)`。MCU 回复：`status + mcuMonotonicSeconds(u32) + deviceListVersion(u32)`。

连续 3 次（每次等待 500 ms）无合法 heartbeat response 时，Bridge 把所有设备 Reachable=false，但保留 Endpoint 和 NVM，不发送 Remove。UART 恢复后重新 HELLO、清单、Bind、snapshot。MCU 不应通过周期主动 HELLO 替代 Bridge 探测。

## 12. MCU 固件实现建议

建议拆成四层：

```text
uart_driver       DMA/中断、环形缓冲、整帧 TX 互斥
frame_codec       SOF/长度/CRC/flags/sequence
protocol_session  HELLO、清单、Bind、响应缓存、超时重试
device_manager    设备数据库、状态/渐变、命令执行、snapshot/report
```

主循环或任务应遵循：

```text
启动 -> 生成新 sessionId -> WAIT_HELLO
HELLO OK -> 锁定并发送完整清单
收到 Bind -> 持久化 endpoint/binding 缓存 -> 回复 Bind
在线且状态有效 -> 发送完整 snapshot
收到 command -> 校验绑定/能力/参数 -> 执行或排队 -> 回复 OK/错误
最终物理状态确定 -> stateVersion++ -> snapshot 或单属性 report
物理掉线 -> Offline；恢复 -> Online，等待 response 后 snapshot
收到 Remove response=OK -> 擦除 pendingRemove 记录
```

协议接收任务不要直接改 Matter 或共享设备状态；先复制完整事件到线程安全队列，再由协议状态机/设备任务按顺序应用。快照应使用临时结构完整解析后一次性提交，避免半帧或半状态可见。

## 13. PC 模拟器联调

仓库提供单进程 UART owner：`tools/bridge_uart_v2/phone_session.py`。它会记录真实收发，不要同时打开串口终端或 PICOCOM。

```bash
cd /home/alvin/code/bflb-connectedhomeip
python3 -u tools/bridge_uart_v2/phone_session.py \
  --port /dev/ttyUSB0 \
  --console /dev/ttyUSB2 \
  --output test_results/bridge_uart_v2/mcu-guide-YYYYMMDD-01
```

启动后等待 `HELLO/LIST/BIND/SNAPSHOT` 完成，再从 stdin 逐行发送 JSON：

```json
{"op":"add","id":1,"kind":"cw"}
{"op":"add","id":3,"kind":"hsct"}
{"op":"list"}
{"op":"state","id":1,"values":{"on":1,"level":254,"color_temperature":300}}
{"op":"state","id":3,"values":{"on":1,"level":200,"hue":80,"saturation":140}}
{"op":"offline","id":3}
{"op":"online","id":3}
{"op":"remove","id":1}
```

每条 action 要等前一条 response/snapshot ACK 完成；`uart.jsonl` 是收发帧，`actions.jsonl` 是测试动作，`bridge.log` 是 Bridge log，`mcu-state.json` 是模拟 MCU 持久化状态。一次测试只能有一个 UART owner；输出目录必须是新目录。

异常注入用于验证 Bridge 错误处理：

```json
{"op":"inject","id":1,"mode":"no_snapshot"}
{"op":"inject","id":1,"mode":"delay_snapshot","milliseconds":500}
{"op":"inject","id":1,"mode":"busy"}
{"op":"inject","id":1,"mode":"old_version"}
{"op":"inject","id":1,"mode":"conflict"}
{"op":"inject","id":1,"mode":"clear"}
```

## 14. MCU 工程师验收表

每项保存 MCU log、Bridge log 和 `uart.jsonl` 中的原始帧/时间戳。

| 编号 | 操作 | 必须观察到 |
| --- | --- | --- |
| P01 | 空清单启动 | HELLO 成功、LIST_END count=0、无动态 Endpoint |
| P02 | 添加 CW | Add response、Bind、`0x0043` snapshot、Matter CT Endpoint |
| P03 | 添加 RGBCW | Bind、HS `0x000F` snapshot、Matter Extended Color Endpoint |
| P04 | Bridge 重启 | MCU ID 不变；重新 LIST/BIND；旧 Endpoint 被复用 |
| P05 | MCU 重启 | 新 sessionId；设备库保留；旧帧被 Bridge 丢弃 |
| P06 | On/Off 与 Level | UART command 字段正确；最终 snapshot/read-back 与 Matter 一致 |
| P07 | HS 控制 | Hue/Saturation 成对更新，模式 0，禁止 XY/RGB |
| P08 | 色温控制 | mired 154..454，模式 2，完整 CT snapshot |
| P09 | 离线/上线 | Offline 只变 Reachable；Online response 后 snapshot 才恢复在线 |
| P10 | UART 整体断开 | 所有设备 Reachable=false，Endpoint 保留；恢复后自动同步 |
| P11 | 删除/复位中断 | pendingRemove 保留；重启后重发 Remove；收到 OK 才擦除 |
| P12 | 重复 Add/Remove/Bind | 不重复建 Endpoint、不重复物理执行，返回幂等 OK |
| P13 | 旧 binding/session/stateVersion | 丢弃或返回明确错误，不改变 Matter 状态 |
| P14 | 坏 CRC、半帧、超长 payload | 丢帧并重新找 SOF，不删除设备/改变 Reachable |
| P15 | 渐变、Stop、模式切换 | OK 与最终状态分离；最终值 500 ms 内上报 |
| P16 | 读属性/失败响应 | 成功长度 6/7；失败只有 status；XY 读返回 UNSUPPORTED_ATTRIBUTE |
| P17 | 两个设备并发控制 | 每设备状态独立，response 不串设备，清单/Bind 顺序不死锁 |

## 15. 常见错误

- 把 `matterEndpointId` 当成物理 ID 保存：Endpoint 由 Bridge 分配，MCU 只能缓存。
- 每次上电复用旧 `sessionId`：会让 Bridge 接受旧会话数据或无法重新同步。
- 设备掉线发送 Remove：会导致 Matter Endpoint 被删除；暂时不可用必须 Offline。
- Online response 后立即宣称在线：必须等完整 snapshot 被 Bridge 接受。
- snapshot 只发改变的一个字段：初始/上线/模式切换必须使用完整 mask。
- OnOff=0 时把 Level 置 0：协议要求逻辑 Level 保留 1..254。
- command response=OK 就上报目标值：OK 只代表接受，最终值必须来自实际执行后的 report/snapshot。
- 相同 stateVersion 的不同内容直接覆盖：必须拒绝为 `BAD_STATE_VERSION`。
- 为普通属性报告做 ACK/周期补报：本产品不需要；必要时由 Bridge 发 `DEVICE_STATE_REQUEST`。
- 混入 RGB/XY 字段或旧 v1 flags：这会破坏当前 HS/CT 产品定义，必须拒绝并记录。

## 16. 交付物

MCU 提交给 Bridge 团队：

1. 固件版本号、源码 commit/hash、支持的 `deviceType/capabilityFlags`。
2. 设备数据库字段说明，尤其是 `uartDeviceId` 分配策略和 pendingRemove 恢复策略。
3. UART 编解码单元测试：CRC、最小/最大帧、半帧、坏 CRC、sequence 回绕。
4. P01-P17 验收日志和失败原始帧；每个测试注明 Bridge 固件版本。
5. 至少一个 CW 和一个 RGBCW 实物的状态范围、HS/CT 模式切换和渐变最终值验证结果。

完成上述交付后，双方再冻结协议版本；任何新增能力（尤其 XY、RGB、动态能力边界或 Bridge->MCU 管理命令）必须另开协议版本，不能复用本版保留字段。
