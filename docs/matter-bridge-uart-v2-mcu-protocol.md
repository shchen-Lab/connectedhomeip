# Matter Bridge UART v2.0 MCU 对接协议

> 状态：冻结候选版，供 Bridge 与 MCU 双方评审。
>
> 适用产品：CW_2CH、RGBCW_5CH；MCU 和 Bridge 最多管理 32 个当前存在的子设备。
>
> 已确认：UART 颜色控制只传 RGB 和色温，不向 MCU 下发 HS/XY；色温物理范围为 154~454 mireds；uartDeviceId 不得复用于另一个物理设备；渐变只上报最终值。

## 1. 设计原则

~~~text
MCU:
  uartDeviceId -> 真实物理子设备

Bridge:
  uartDeviceId -> Matter EndpointId
  uartDeviceId -> BridgeDevice -> Matter device type / cluster / capability
~~~

1. uartDeviceId 由 MCU 分配并持久化，是物理设备的稳定身份；必须非零，且在产品生命周期内不得分配给另一个物理设备。
2. matterEndpointId 由 Bridge 分配，是 Matter 路由地址，不是物理设备身份。
3. Bridge 是 uartDeviceId 到 Matter Endpoint 映射的最终所有者。
4. MCU 可以缓存 Endpoint 绑定，但必须接受 Bridge 重启后的重新绑定。
5. 子设备离线只更新 Reachable=false，不删除 Endpoint。
6. 只有明确的设备删除事件才删除 Endpoint 和 Bridge 映射。
7. UART 接收任务不直接修改 Matter 状态，Bridge 在 Matter 线程应用状态。
8. 离线但尚未删除的设备计入 32 个设备名额。

当前仓库中的 UART v1 只有命令转发、属性读取请求和属性上报。本文定义 UART v2，生命周期消息和数据消息统一使用 v2 帧格式。

## 2. 物理链路

| 项目 | 值 |
| --- | --- |
| UART | uart1 |
| Baudrate | 921600 |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |
| Hardware flow control | Disabled |
| Byte order | Little-endian |
| Maximum payload | 1024 bytes |
| Maximum frame | 1059 bytes |

UART 是板内可信链路。v2 使用 SOF、EOF 和 CRC16 做帧完整性校验，暂不包含加密和认证。

## 3. v2 二进制帧

### 3.1 字段布局

所有字段使用 little-endian，offset 从 0 开始。

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 2 | sof | 固定 A5 5A |
| 2 | 1 | version | 固定 0x02 |
| 3 | 1 | messageType | 消息类型 |
| 4 | 1 | flags | 标志位 |
| 5 | 2 | sequence | 发送端序列号，0 保留 |
| 7 | 4 | sessionId | MCU 当前启动会话 ID |
| 11 | 4 | uartDeviceId | 物理设备 ID；管理消息填 0 |
| 15 | 2 | matterEndpointId | Bridge Endpoint；未绑定填 0 |
| 17 | 4 | bindingVersion | 当前绑定版本；未绑定填 0 |
| 21 | 4 | clusterId | Matter Cluster ID；管理消息填 0 |
| 25 | 4 | commandOrAttributeId | Command ID 或 Attribute ID |
| 29 | 2 | payloadLength | Payload 长度，0 到 1024 |
| 31 | N | payload | 消息 payload |
| 31 + N | 2 | crc16 | CRC16/CCITT-FALSE |
| 33 + N | 2 | eof | 固定 0D 0A |

~~~text
frameLength = 35 + payloadLength
minimumFrameLength = 35
maximumFrameLength = 1059
~~~

CRC 覆盖 version 到 payload：

~~~text
offset = 2
length = 29 + payloadLength
~~~

CRC 不包含 SOF、CRC 自身和 EOF。CRC 参数：

~~~text
algorithm = CRC16/CCITT-FALSE
initial = 0xFFFF
polynomial = 0x1021
reflectIn = false
reflectOut = false
xorOut = 0x0000
wire byte order = little-endian
reference check: CRC16 ASCII "123456789" = 0x29B1
~~~

### 3.2 sessionId

sessionId 固定表示 MCU 当前启动会话：

- MCU 每次启动、复位或重新建立协议会话时生成新的非零 uint32。
- MCU 在同一次运行期间保持 sessionId 不变。
- Bridge 只接受当前 MCU sessionId 的设备数据。
- Bridge 尚未收到 HELLO_RESPONSE 时，Bridge 发出的帧使用 sessionId=0。
- HELLO_RESPONSE 的帧头携带新的 MCU sessionId。
- Bridge 发现新的 sessionId 后，必须重新执行设备清单同步和绑定。
- bridgeBootId 只放在 HELLO_REQUEST payload 中，不放在帧头。

sessionId 不要求跨 MCU 掉电持久化，只要求每次新启动不会继续使用上一次会话 ID。建议使用启动计数器、随机数或芯片唯一信息组合生成。

### 3.3 flags

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | ACK_REQUIRED | 接收方必须发送对应 response |
| 1 | RETRANSMISSION | 当前帧是相同事务的超时重传 |
| 2-7 | Reserved | 发送必须为 0；接收非 0 时拒绝该帧 |

每种请求都有确定的响应 messageType，错误通过响应 payload 的 status 表示，不再设置通用 RESPONSE、ERROR 或分片标志。请求/通知按消息定义设置 ACK_REQUIRED；所有 response 的 flags 必须为 0。

### 3.4 sequence

- Bridge 和 MCU 各自维护独立的发送序列号。
- 序列号从 1 开始递增，跳过 0，溢出后回到 1。
- 同一逻辑请求的重传必须使用相同 sequence，并设置 RETRANSMISSION。
- response 复用对应 request/notify 的 sequence。
- 去重键固定为 sessionId + messageType + sequence + uartDeviceId。

### 3.5 接收和重同步

接收方必须：

1. 在字节流中查找 A5 5A。
2. 读取完整固定头，检查 version 和 payloadLength。
3. payloadLength 大于 1024 时丢弃当前候选帧并继续搜索 SOF。
4. 等待完整 frame 后校验 EOF 和 CRC。
5. CRC 或 EOF 错误时丢弃当前候选帧，并继续搜索下一个 SOF。
6. 不因为坏帧删除设备、修改 Reachable 或修改 Endpoint 映射。

## 4. 消息类型

### 4.1 数据消息

| Value | Name | Direction |
| ---: | --- | --- |
| 0x10 | DOWN_CONTROL_REQUEST | Bridge -> MCU |
| 0x11 | Reserved | - |
| 0x12 | DOWN_CONTROL_RESPONSE | MCU -> Bridge |
| 0x13 | Reserved | - |
| 0x20 | UP_ATTRIBUTE_REPORT | MCU -> Bridge |

### 4.2 生命周期消息

| Value | Name | Direction |
| ---: | --- | --- |
| 0x30 | HELLO_REQUEST | Bridge -> MCU |
| 0x31 | HELLO_RESPONSE | MCU -> Bridge |
| 0x32 | DEVICE_LIST_REQUEST | Bridge -> MCU |
| 0x33 | DEVICE_LIST_RESPONSE | MCU -> Bridge |
| 0x34~0x35 | Reserved | - |
| 0x36 | DEVICE_ADD_NOTIFY | MCU -> Bridge |
| 0x37 | DEVICE_ADD_RESPONSE | Bridge -> MCU |
| 0x38 | DEVICE_REMOVE_NOTIFY | MCU -> Bridge |
| 0x39 | DEVICE_REMOVE_RESPONSE | Bridge -> MCU |
| 0x3A | DEVICE_BIND_REQUEST | Bridge -> MCU |
| 0x3B | DEVICE_BIND_RESPONSE | MCU -> Bridge |
| 0x3C | DEVICE_ONLINE_NOTIFY | MCU -> Bridge |
| 0x3D | DEVICE_ONLINE_RESPONSE | Bridge -> MCU |
| 0x3E | DEVICE_OFFLINE_NOTIFY | MCU -> Bridge |
| 0x3F | DEVICE_OFFLINE_RESPONSE | Bridge -> MCU |
| 0x40 | UP_STATE_SNAPSHOT | MCU -> Bridge |
| 0x41 | STATE_REPORT_RESPONSE | Bridge -> MCU |
| 0x42 | HEARTBEAT | Bridge -> MCU |
| 0x43 | HEARTBEAT_RESPONSE | MCU -> Bridge |
| 0x44 | DEVICE_STATE_REQUEST | Bridge -> MCU |
| 0x45 | DEVICE_STATE_RESPONSE | MCU -> Bridge |
| 0x46 | MCU_READY_NOTIFY | MCU -> Bridge |

除 MCU_READY_NOTIFY 外，需要响应的 request/notify 必须设置 ACK_REQUIRED；响应复用请求的 sequence。

### 4.3 完整帧测试向量

以下字节均为十六进制，空格只用于阅读；多字节字段已经按线上 little-endian 排列。实现必须生成完全相同的字节。

HELLO_REQUEST：sequence=1，bridgeBootId=0x12345678，32 devices，1024-byte payload limit；末尾 CRC 字节为 80 F5。

~~~text
A5 5A 02 30 01 01 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 09 00 02
00 78 56 34 12 20 00 04 F5 80 0D 0A
~~~

DEVICE_LIST_RESPONSE：sessionId=0xA1B2C3D4，一个在线有效的 RGBCW_5CH，uartDeviceId=0x01020304，stateVersion=5；末尾 CRC 字节为 83 7D。

~~~text
A5 5A 02 33 00 02 00 D4 C3 B2 A1 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 12 00 00
01 00 00 00 01 0A 00 04 03 02 01 11 07 05 00 00
00 83 7D 0D 0A
~~~

SET_RGB：sessionId=0xA1B2C3D4，uartDeviceId=0x01020304，Endpoint=3，bindingVersion=9，RGB=(255,128,0)，Mode=XY，transition=1.0 s；末尾 CRC 字节为 B7 7D。

~~~text
A5 5A 02 10 01 03 00 D4 C3 B2 A1 04 03 02 01 03
00 09 00 00 00 00 03 00 00 01 00 FF FF 06 00 FF
80 00 01 0A 00 B7 7D 0D 0A
~~~

## 5. 公共数据类型

### 5.1 Device Type

deviceType 是 UART 协议枚举，不直接使用 Matter Device Type ID。v2.0 只允许两个固定 Profile，deviceType 已完整决定能力，不再传 capabilityFlags。

| Value | Name | 物理通道 | Bridge Matter template | UART 控制能力 |
| ---: | --- | --- | --- | --- |
| 0x10 | CW_2CH | Cold White + Warm White | Color Temperature Light | OnOff、Level、Color Temperature |
| 0x11 | RGBCW_5CH | Red + Green + Blue + Cold White + Warm White | Extended Color Light | OnOff、Level、RGB、Color Temperature |

Bridge 收到其他类型时返回 UNSUPPORTED_DEVICE_TYPE。同一 uartDeviceId 的 deviceType 改变时返回 DEVICE_ID_CONFLICT。

### 5.2 Active Color Mode

activeColorMode 使用 Matter ColorMode 的数值，但 MCU 不处理 HS 或 XY 数值：

| Value | Name | UART 中的有效颜色值 |
| ---: | --- | --- |
| 0 | HUE_SATURATION | Red、Green、Blue；表示 RGB 由 Matter HS 转换而来 |
| 1 | XY | Red、Green、Blue；表示 RGB 由 Matter XY 转换而来 |
| 2 | COLOR_TEMPERATURE | ColorTemperatureMireds |

CW_2CH 的模式必须始终为 2。RGBCW_5CH 可使用 0、1、2。模式 0 和 1 都只要求 MCU 执行 RGB；Bridge 负责 HS/XY 与 RGB 的转换。MCU 必须保存并原样上报模式，使 Bridge 重启后可恢复 Matter ColorMode。

### 5.3 Device State Flags

~~~text
bit0  DEVICE_PRESENT
bit1  DEVICE_ONLINE
bit2  STATE_VALID
bit3-7 Reserved
~~~

清单和 Add 中的设备必须使用 DEVICE_PRESENT=1。普通离线使用 DEVICE_ONLINE=0，上线使用 DEVICE_ONLINE=1。DEVICE_PRESENT=0 不得出现在清单或 Add 中；删除只使用 DEVICE_REMOVE_NOTIFY。Reserved bit 非 0 时拒绝消息。

### 5.4 State Version

每个设备维护 uint32 stateVersion。STATE_VALID=0 时允许为 0；一旦产生有效状态，stateVersion 必须非零：

- 同一个 MCU sessionId 内，每次提交一组对外可见的最终状态时递增一次。
- 一个状态快照中的所有属性共用一个版本，并作为原子状态应用。
- 一个增量属性上报只能包含一个属性，并独占一个新版本。
- Bridge 收到更小版本时丢弃；相同版本和相同内容视为重传；相同版本但内容不同返回 BAD_STATE_VERSION。
- MCU 重启后 sessionId 改变，stateVersion 可以从 1 重新开始。
- uint32 溢出前 MCU 必须建立新 session，不允许同一 session 内回绕。

## 6. Status Code

所有 response payload 的第一个字段都是 status。

| Value | Name | Meaning |
| ---: | --- | --- |
| 0x00 | OK | 成功 |
| 0x01 | INVALID_ARGUMENT | 字段非法 |
| 0x02 | UNKNOWN_DEVICE | 未知 uartDeviceId |
| 0x03 | ENDPOINT_EXHAUSTED | Bridge 的 32 个动态 Endpoint 已满 |
| 0x04 | DEVICE_ID_CONFLICT | 当前已知的同一 ID 对应不同 deviceType |
| 0x05 | BINDING_MISMATCH | Endpoint 或 bindingVersion 不匹配 |
| 0x06 | UNSUPPORTED_DEVICE_TYPE | deviceType 不是 0x10 或 0x11 |
| 0x07 | BUSY | 当前事务尚未完成 |
| 0x08 | BAD_STATE_VERSION | 状态版本过期 |
| 0x09 | PROTOCOL_MISMATCH | 协议版本不兼容 |
| 0x0A | UNSUPPORTED_OPERATION | 当前 Profile 不支持该控制或属性 |
| 0x0B | INTERNAL_ERROR | 内部错误 |

重复 Add、Remove、Bind 的处理见第 17 节。已经达到目标状态的重复请求应返回 OK。

## 7. 启动握手

### 7.1 MCU_READY_NOTIFY

MCU 启动并加载完本地设备数据库后发送，payload 为空。帧头使用新的非零 sessionId，其他设备字段为 0，flags 为 0。收到 HELLO_REQUEST 前每 1 秒重发一次。Bridge 收到与当前会话不同的 READY 后，先将所有已映射设备设为 Reachable=false，再发 HELLO_REQUEST。

READY 丢失时，Bridge 的周期心跳/HELLO 恢复流程仍可发现 MCU。

### 7.2 HELLO_REQUEST

帧头中的设备和 Cluster 字段全部为 0，sessionId=0。Payload：

| Offset | Size | Field | Fixed value |
| ---: | ---: | --- | --- |
| 0 | 1 | protocolMajor | 2 |
| 1 | 1 | protocolMinor | 0 |
| 2 | 4 | bridgeBootId | 每次 Bridge 启动生成的非零值 |
| 6 | 1 | bridgeMaxDevices | 32 |
| 7 | 2 | bridgeMaxPayload | 1024 |

### 7.3 HELLO_RESPONSE

帧头中的 sessionId 是 MCU 当前会话，其他设备字段为 0。Payload：

| Offset | Size | Field | Fixed value |
| ---: | ---: | --- | --- |
| 0 | 1 | status | Status Code |
| 1 | 1 | protocolMajor | 2 |
| 2 | 1 | protocolMinor | 0 |
| 3 | 1 | mcuMaxDevices | 32 |
| 4 | 2 | mcuMaxPayload | 1024 |
| 6 | 4 | deviceListVersion | 当前持久化清单版本 |
| 10 | 4 | mcuFirmwareVersion | MCU 自定义单调版本号 |

新 bridgeBootId 表示 Bridge 已重启。握手后 MCU 暂停普通状态上报，直到每个设备重新绑定完成；Bridge 总是请求完整设备清单。

## 8. 设备清单同步

### 8.1 deviceListVersion

- MCU 持久化非零 uint32 deviceListVersion。
- 添加或删除设备后递增一次；上线、离线和灯状态变化不递增。
- 溢出时从 0xFFFFFFFF 回到 1。
- MCU 必须先持久化新清单和版本，再发送 Add/Remove Notify。
- Bridge 在心跳中发现版本变化时重新请求完整清单。

### 8.2 DEVICE_LIST_REQUEST

payload 为空，帧头中的设备、Endpoint、bindingVersion 和 Cluster 字段全部为 0。

### 8.3 DEVICE_LIST_RESPONSE

设备上限为 32，因此完整清单放在一个 frame 中，不分片，也不增加第二套 CRC32。Payload header：

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | status |
| 1 | 4 | deviceListVersion |
| 5 | 1 | entryCount，0~32 |
| 6 | 1 | entrySize，固定 10 |
| 7 | 1 | Reserved，填 0 |

随后紧跟 entryCount 个 10-byte entry：

| Entry offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | uartDeviceId |
| 4 | 1 | deviceType |
| 5 | 1 | stateFlags |
| 6 | 4 | stateVersion |

status=OK 时，总 payload 长度必须等于 8 + entryCount * 10；32 个设备时为 328 bytes。status 非 OK 时 entryCount 必须为 0，payload 仍为 8 bytes。uartDeviceId 必须非零且清单内不能重复，否则 Bridge 拒绝整个清单。帧 CRC16 已覆盖全部 entry。

清单处理规则：

- 已知 uartDeviceId：复用 NVM 中的 Endpoint 和 bindingVersion。
- 新 uartDeviceId：Bridge 按 deviceType 分配 Endpoint 并持久化映射。
- Bridge 已保存但清单缺少的设备：保留 Endpoint 和映射，设置 Reachable=false，不自动删除。
- 每个清单内设备随后执行 DEVICE_BIND_REQUEST。
- 超过 32 项、类型非法或 ID 重复：不应用部分结果，重新请求完整清单。
- 只有 DEVICE_REMOVE_NOTIFY 或管理员删除才清除映射。

## 9. Endpoint 绑定

### 9.1 DEVICE_BIND_REQUEST

用于启动恢复、任一端重启和 UART 会话恢复。运行期间的新设备由 DEVICE_ADD_RESPONSE 直接完成绑定，不再额外发送 Bind。

帧头：

~~~text
sessionId          current MCU session
uartDeviceId       target device
matterEndpointId   Bridge-assigned Endpoint
bindingVersion     Bridge-assigned nonzero version
~~~

Payload 固定 2 bytes：

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | deviceType |
| 1 | 1 | bindFlags |

bindFlags：bit0=REQUEST_STATE_SNAPSHOT，通常为 1；bit1~7 Reserved，发送为 0。

### 9.2 DEVICE_BIND_RESPONSE

帧头复用请求中的设备 ID、Endpoint 和 bindingVersion。Payload 只有 status uint8。

成功后 MCU 缓存帧头中的 Endpoint 和 bindingVersion。若请求快照，MCU 随后发送完整 UP_STATE_SNAPSHOT。绑定成功前不得发送该设备的普通状态上报。相同绑定的重复请求返回 OK；新的 bindingVersion 替换旧缓存，旧绑定数据随后必须被双方拒绝。

## 10. 设备添加

### 10.1 DEVICE_ADD_NOTIFY

MCU 发现并已持久化新设备后发送。帧头中的 uartDeviceId 为新设备的永久 ID，Endpoint 和 bindingVersion 填 0。Payload 固定 7 bytes：

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | deviceType |
| 1 | 1 | stateFlags |
| 2 | 4 | stateVersion |
| 6 | 1 | addReason |

addReason：0=DISCOVERED，1=RESTORED，2=USER_ACTION。

### 10.2 DEVICE_ADD_RESPONSE

Payload：

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | status |
| 1 | 2 | matterEndpointId，失败时 0 |
| 3 | 4 | bindingVersion，失败时 0 |

Bridge 必须在返回 OK 前完成 Endpoint 创建和映射持久化。该响应直接完成运行期绑定；MCU 保存 Endpoint/bindingVersion 后发送完整快照，不再等待 DEVICE_BIND_REQUEST。

相同 ID 和类型的重复 Add 返回已有绑定，不创建第二个 Endpoint。相同 ID、不同类型返回 DEVICE_ID_CONFLICT。没有空槽时返回 ENDPOINT_EXHAUSTED。

## 11. 设备删除

### 11.1 DEVICE_REMOVE_NOTIFY

只用于确认设备已永久移除。普通掉线、断电和通信超时必须使用 DEVICE_OFFLINE_NOTIFY。

MCU 必须先从当前设备清单中持久化删除该 ID、递增 deviceListVersion，并持久化一条待确认删除记录，再发送通知。收到 OK 前不得丢弃该记录；任一端重启并重新握手后仍须重发，避免删除事件因掉电丢失。

帧头使用已缓存的 ID、Endpoint 和 bindingVersion；缓存丢失时 Endpoint 和 bindingVersion 可填 0。Payload 只有 removeReason uint8：0=PHYSICAL_REMOVED，1=USER_REMOVED，2=MCU_DATABASE_RESET。

### 11.2 DEVICE_REMOVE_RESPONSE

Payload 只有 status uint8。Bridge 返回 OK 前必须将设备设为不可达、清除 Dynamic Endpoint、持久化删除结果并使旧 bindingVersion 失效。已删除设备的重复 Remove 返回 OK，MCU 收到 OK 后删除待确认记录。

uartDeviceId 不得分配给另一个物理设备。同一物理设备被删除后重新入网时应沿用原 ID；Bridge 可为这次新的入网关系分配新 Endpoint 和 bindingVersion。Bridge 不承担无限保存历史 ID 的责任。

## 12. 子设备离线和上线

### 12.1 DEVICE_OFFLINE_NOTIFY

MCU 检测到单个子设备不可用时发送。物理设备仍然存在，必须保持 DEVICE_PRESENT=1。

Payload 固定 8 bytes：

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | offlineReason |
| 1 | 4 | stateVersion |
| 5 | 1 | stateFlags |
| 6 | 2 | Reserved，填 0 |

offlineReason：

~~~text
0  DEVICE_REPORT
1  DEVICE_TIMEOUT
2  DEVICE_POWER_OFF
3  DEVICE_ERROR
~~~

Offline/Online 本身不改变灯状态，因此不递增 stateVersion；通知中的 stateVersion 填当前最终状态版本。Offline 的 stateFlags 必须为 PRESENT=1、ONLINE=0；Bridge 校验后设置 Reachable=false，保留 Dynamic Endpoint 和 NVM mapping，再返回 DEVICE_OFFLINE_RESPONSE(status)。重复 Offline 返回 OK。

### 12.2 DEVICE_ONLINE_NOTIFY

子设备恢复可用时发送。Payload 固定 8 bytes：

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | onlineReason |
| 1 | 4 | stateVersion |
| 5 | 1 | stateFlags |
| 6 | 2 | Reserved，填 0 |

Online 的 stateFlags 必须为 PRESENT=1、ONLINE=1。通知中的 stateVersion 填当前最终状态版本。推荐时序：

~~~text
MCU -> DEVICE_ONLINE_NOTIFY
Bridge -> DEVICE_ONLINE_RESPONSE
MCU -> UP_STATE_SNAPSHOT
Bridge -> STATE_REPORT_RESPONSE
~~~

Bridge 返回 DEVICE_ONLINE_RESPONSE 只表示接受上线事件；完整快照应用成功前可以保持 Reachable=false。收到合法快照后再设置 Reachable=true。

## 13. 状态快照

### 13.1 UP_STATE_SNAPSHOT

用于启动恢复、绑定、设备重新上线、颜色变化、渐变结束和主动状态查询。帧头必须包含当前 sessionId、uartDeviceId、matterEndpointId 和 bindingVersion，并设置 ACK_REQUIRED。

Payload：

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | stateVersion |
| 4 | 2 | fieldMask |
| 6 | Variable | 按 bit 从低到高排列的值 |

fieldMask 和编码：

| Bit | Field | Size | Range |
| ---: | --- | ---: | --- |
| 0 | OnOff | 1 | 0 或 1 |
| 1 | CurrentLevel | 1 | 1~254 |
| 2 | ActiveColorMode | 1 | 0、1、2 |
| 3 | Red | 1 | 0~255 |
| 4 | Green | 1 | 0~255 |
| 5 | Blue | 1 | 0~255 |
| 6 | ColorTemperatureMireds | 2 | 154~454 |
| 7~15 | Reserved | - | 必须为 0 |

完整快照固定要求：

| Profile | Required fieldMask | Payload length |
| --- | ---: | ---: |
| CW_2CH | 0x0047（OnOff、Level、Mode、CT） | 11 |
| RGBCW_5CH | 0x007F（全部字段） | 14 |

CW_2CH 的 Mode 必须为 2。RGBCW_5CH 总是发送 RGB 和 CT 的最近保存值，由 Mode 决定当前有效输出。Bridge 必须先校验全部字段，再原子应用整个快照；任一字段错误时拒绝全部字段。

RGB 是与 CurrentLevel 分离的 8-bit 归一化线性通道比例，不是 PWM 寄存器值，也不是带亮度的最终 sRGB。非黑色目标必须满足 max(R,G,B)=255；(0,0,0) 非法，关灯使用 OnOff。MCU 以 CurrentLevel 作为整体亮度，再结合 RGB 比例和硬件校准生成物理输出。Bridge 负责 HS/XY 到线性 RGB 的转换、色域裁剪和归一化；MCU 若限幅，必须返回实际接受的逻辑值。

Bridge 应用 RGBCW 快照时，Mode=0 将 RGB 转换为 Matter CurrentHue/CurrentSaturation 并设置 ColorMode=0；Mode=1 将 RGB 转换为 CurrentX/CurrentY 并设置 ColorMode=1；Mode=2 更新 ColorTemperatureMireds 并设置 ColorMode=2。非当前模式的 Matter 颜色属性保留最近值。转换算法和舍入必须在 Bridge 单元测试中固定，MCU 不参与反向转换。

### 13.2 STATE_REPORT_RESPONSE

用于响应 UP_STATE_SNAPSHOT 和 UP_ATTRIBUTE_REPORT：

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | status |
| 1 | 4 | appliedStateVersion |

### 13.3 DEVICE_STATE_REQUEST / RESPONSE

DEVICE_STATE_REQUEST 的 payload 为空，帧头指定当前设备和绑定。DEVICE_STATE_RESPONSE 的 payload 只有 status uint8。返回 OK 后 MCU 必须发送完整 UP_STATE_SNAPSHOT；即使状态未变化，也应复用当前 stateVersion 和相同内容发送，便于 Bridge 恢复内存状态。

## 14. 属性上报

### 14.1 UP_ATTRIBUTE_REPORT

只用于不要求多字段原子更新的 OnOff 或 Level 变化。颜色、ColorMode 和色温变化必须使用完整 Snapshot，避免多字段状态被拆开观察。

帧头和 value 编码：

| Cluster | clusterId | Attribute | attributeId | Value |
| --- | ---: | --- | ---: | --- |
| OnOff | 0x00000006 | OnOff | 0x00000000 | uint8，0 或 1 |
| LevelControl | 0x00000008 | CurrentLevel | 0x00000000 | uint8，1~254 |

Payload 前 4 bytes 是 stateVersion，随后是 1-byte value，总长固定 5。该上报设置 ACK_REQUIRED，Bridge 使用 STATE_REPORT_RESPONSE 确认。每个增量属性帧必须使用一个新 stateVersion，多个属性的同一次原子变化改用 Snapshot。

Bridge 必须校验 session、设备 ID、Endpoint、bindingVersion、类型和 stateVersion。未知设备、旧绑定和已删除 Endpoint 的上报必须拒绝，不得创建新设备。

## 15. Bridge 下行控制

MCU 只实现归一化的 OnOff、Level、RGB、色温和 Stop，不解析 Matter HS、XY、Move 或 Step。Bridge 根据缓存状态将 Matter 命令转换为明确目标值和持续时间。

### 15.1 DOWN_CONTROL_REQUEST 操作 ID

| Cluster | clusterId | Operation | commandOrAttributeId |
| --- | ---: | --- | ---: |
| OnOff | 0x00000006 | SET_OFF | 0x00000000 |
| OnOff | 0x00000006 | SET_ON | 0x00000001 |
| OnOff | 0x00000006 | TOGGLE | 0x00000002 |
| LevelControl | 0x00000008 | SET_LEVEL | 0x00000000 |
| LevelControl | 0x00000008 | STOP_LEVEL_TRANSITION | 0x00000003 |
| ColorControl | 0x00000300 | SET_RGB | 0xFFFF0001 |
| ColorControl | 0x00000300 | SET_COLOR_TEMPERATURE | 0x0000000A |
| ColorControl | 0x00000300 | STOP_COLOR_TRANSITION | 0x00000047 |

0xFFFF0001 是 UART v2 私有操作 ID，不是 Matter Command ID。

### 15.2 请求 Payload

| Operation | Payload | Length |
| --- | --- | ---: |
| SET_OFF / SET_ON / TOGGLE | empty | 0 |
| SET_LEVEL | level u8, transitionTimeDs u16, withOnOff u8 | 4 |
| STOP_LEVEL_TRANSITION | empty | 0 |
| SET_RGB | red u8, green u8, blue u8, activeColorMode u8, transitionTimeDs u16 | 6 |
| SET_COLOR_TEMPERATURE | mireds u16, activeColorMode u8, transitionTimeDs u16 | 5 |
| STOP_COLOR_TRANSITION | empty | 0 |

约束：

- transitionTimeDs 单位为 0.1 秒，范围 0~65534；0 表示立即应用。
- Bridge 在下发前把 Matter nullable/default transition 解析成具体值，不向 MCU 传 null。
- SET_LEVEL 的 level 为 1~254；withOnOff 只能为 0 或 1。
- SET_RGB 的 Mode 只能为 0 或 1；CW_2CH 不支持该操作。
- SET_COLOR_TEMPERATURE 的 mireds 为 154~454，Mode 必须为 2。
- STOP 停止当前对应渐变，保持停止时实际值，并立即上报完整最终 Snapshot。

Matter 命令归一化规则：

- HS 命令：Bridge 计算目标 RGB，发送 SET_RGB，Mode=0。
- XY 命令：Bridge 计算目标 RGB，发送 SET_RGB，Mode=1。
- CT 命令：发送 SET_COLOR_TEMPERATURE，Mode=2。
- Step：Bridge 根据已知当前值计算目标值，再发送 SET 操作。
- Move：Bridge 根据速率计算边界目标和持续时间，再发送 SET 操作；收到 Matter Stop 时发送对应 STOP。
- 设备不可达或转换依赖的当前状态未知时，Bridge 不下发依赖当前值的操作。

### 15.3 DOWN_CONTROL_RESPONSE

Payload 只有 status uint8。OK 表示 MCU 已校验并接受控制，不表示渐变已完成。MCU 先返回响应，再异步执行；达到最终状态后发送一次完整 Snapshot。

Bridge 当前 Matter 命令处理不会阻塞等待 UART ACK。UART 响应用于传输诊断、去重和纠错；MCU 拒绝或最终状态不同时，Bridge 以 MCU Snapshot 修正 Matter 缓存。最终状态 Snapshot 是状态真值。

### 15.4 渐变上报

1. 渐变期间不周期上报中间值，也不为每次 PWM 变化递增 stateVersion。
2. 正常完成时递增一次 stateVersion，只发送一个完整 Snapshot。
3. STOP 中断时，以停止时实际值为最终值，递增一次版本并发送一个完整 Snapshot。
4. 新命令替换旧渐变时连续执行，不上报旧命令的中间值，只在最后一个命令完成后上报最终 Snapshot。
5. MCU 限幅时上报实际最终逻辑值；无法可靠获得状态时上报 Offline，不得把目标值伪装成最终值。

## 16. 心跳和 MCU 整体离线

Bridge 每 5 秒发送一次 HEARTBEAT。帧头中的设备和 Cluster 字段全部为 0。

HEARTBEAT payload：

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | bridgeMonotonicSeconds |

HEARTBEAT_RESPONSE payload：

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | status |
| 1 | 4 | mcuMonotonicSeconds |
| 5 | 4 | deviceListVersion |

连续 3 个周期未收到合法响应时，Bridge 将所有已绑定设备设为 Reachable=false，但保留全部 Dynamic Endpoint 和 NVM 映射，不产生 Remove。恢复后重新执行 HELLO、完整清单、逐设备 Bind 和完整快照。

Bridge 在心跳中发现 deviceListVersion 变化时主动拉取完整清单。MCU 发现新的 bridgeBootId 或自身 session 变化后，在重新绑定前不得沿用旧绑定上报状态。

## 17. 超时、重试和幂等

| 项目 | 固定初始值 |
| --- | ---: |
| Response timeout | 500 ms |
| Maximum retransmissions | 3 |
| Heartbeat interval | 5 s |
| MCU offline threshold | 3 次心跳 |

首次发送不计为重传，最多发送 1+3 次。重传复用 sequence 并设置 RETRANSMISSION。接收方至少缓存最近 16 个需要响应的事务结果，缓存时间不少于 10 秒。

幂等规则：

- Add：相同 ID 和类型返回已有绑定，不创建重复 Endpoint。
- Remove：已经删除时返回 OK。
- Bind：相同 Endpoint 和 bindingVersion 返回 OK。
- Offline/Online：已经处于目标状态时返回 OK。
- Control：相同去重键只执行一次，返回缓存 response。
- State report：相同版本和内容返回 OK，不重复产生 Matter 状态变化。
- Device List：相同请求返回发送时的完整清单；若清单已变化，可返回新版本完整清单，Bridge 以 response sequence 和 payload 作为一次原子结果处理。

Control ACK 超时后 Bridge 发送 DEVICE_STATE_REQUEST 校准状态。连续控制失败可以把单个设备标记为不可达，但不能删除设备。

## 18. 掉电恢复时序

### 18.1 Bridge 重启，MCU 未重启

~~~text
Bridge -> HELLO_REQUEST(sessionId=0, new bridgeBootId)
MCU    -> HELLO_RESPONSE(current sessionId)
Bridge -> DEVICE_LIST_REQUEST
MCU    -> DEVICE_LIST_RESPONSE(full list)
Bridge -> DEVICE_BIND_REQUEST(each listed device)
MCU    -> DEVICE_BIND_RESPONSE
MCU    -> UP_STATE_SNAPSHOT(full)
Bridge -> STATE_REPORT_RESPONSE
~~~

### 18.2 MCU 重启，Bridge 未重启

~~~text
MCU    -> MCU_READY_NOTIFY(new session, repeat until HELLO)
Bridge -> marks all mapped devices unreachable
Bridge -> HELLO_REQUEST(sessionId=0)
MCU    -> HELLO_RESPONSE(new sessionId)
Bridge -> DEVICE_LIST_REQUEST
MCU    -> DEVICE_LIST_RESPONSE(full list)
Bridge -> DEVICE_BIND_REQUEST(each listed device)
MCU    -> DEVICE_BIND_RESPONSE + UP_STATE_SNAPSHOT
~~~

旧 MCU session 的状态、上线、离线和控制 response 全部丢弃。

### 18.3 子设备暂时离线

~~~text
MCU    -> DEVICE_OFFLINE_NOTIFY
Bridge -> DEVICE_OFFLINE_RESPONSE; Reachable=false
...
MCU    -> DEVICE_ONLINE_NOTIFY
Bridge -> DEVICE_ONLINE_RESPONSE
MCU    -> UP_STATE_SNAPSHOT(full)
Bridge -> STATE_REPORT_RESPONSE; Reachable=true
~~~

### 18.4 永久删除期间任一端掉电

MCU 的待确认删除记录跨重启保留。重新握手后先返回不含该设备的完整清单，再重发 DEVICE_REMOVE_NOTIFY，直到 Bridge 返回 OK。Bridge 对清单缺少但未收到 Remove 的旧映射只设为不可达，不自行删除。

## 19. Bridge NVM 映射建议

Bridge 至少持久化：

~~~text
recordVersion       uint16
uartDeviceId        uint32
matterEndpointId    uint16
deviceType          uint8
bindingVersion      uint32
mappingState        uint8   // ACTIVE or PENDING_REMOVE
~~~

- Add Response 返回 OK 前，ACTIVE 记录必须已可靠写入。
- Remove Response 返回 OK 前，PENDING_REMOVE 或删除结果必须已可靠写入。
- bindingVersion 由 Bridge 生成且非零；新映射或 Endpoint 改变时生成新值。
- Bridge 重启后先加载映射，再以 MCU 完整清单校准。
- Endpoint 是 Bridge 地址，uartDeviceId 是物理身份，两者不能互换。

## 20. MCU 侧最小实现清单

1. 持久化设备清单、非复用 uartDeviceId 和 deviceListVersion。
2. 最多保存 32 个当前设备，以及尚未确认的删除记录。
3. 每次启动生成新的非零 sessionId，并发送 MCU_READY_NOTIFY。
4. 实现帧长度、Reserved bit、CRC16 和字节流重同步检查。
5. 实现 HELLO、完整单帧 Device List 和 Bind。
6. 实现 Add、Remove、Online、Offline 及对应重试。
7. 只实现 OnOff、Level、RGB、CT 和 Stop 的归一化控制。
8. 保存并上报 ActiveColorMode；不实现 HS/XY 转换。
9. 实现完整 Snapshot；颜色变化不拆分为单属性上报。
10. 渐变期间不上报中间值，完成或停止后只报一次最终 Snapshot。
11. 对重传控制去重，不能执行两次。
12. 绑定成功前不发送普通设备状态。

## 21. Bridge 侧待实现项

1. 动态 Endpoint 容量配置为 32。
2. 实现 v2 编解码、事务、ACK、重试和重复帧缓存。
3. 实现 uartDeviceId 到 Endpoint/bindingVersion 的持久化管理器。
4. 实现启动清单对账、Add/Remove、Online/Offline 和掉电恢复状态机。
5. 将 Matter HS/XY 转换为 RGB，将 RGB 状态转换回当前 Matter 模式属性。
6. 将 Matter Move/Step/Stop 归一化为明确目标、时间和 STOP 操作。
7. 按 session、ID、Endpoint、bindingVersion 和 stateVersion 校验状态帧。
8. 以 MCU 最终 Snapshot 修正 Matter 缓存并触发属性报告。

## 22. 联调验收用例

| Case | Expected |
| --- | --- |
| MCU 上报 32 个混合设备 | Bridge 创建并绑定 32 个 Endpoint |
| 第 33 个设备 Add | 返回 ENDPOINT_EXHAUSTED，不影响已有设备 |
| CW_2CH 上报 Mode 0/1 | 拒绝整个 Snapshot |
| RGBCW 接收 Matter HS | MCU 收到 RGB + Mode 0，不收到 HS 值 |
| RGBCW 接收 Matter XY | MCU 收到 RGB + Mode 1，不收到 XY 值 |
| RGBCW 接收 Matter CT | MCU 收到 154~454 mireds + Mode 2 |
| 5 秒渐变 | 期间无中间上报；结束后一个完整 Snapshot |
| 渐变中收到 Stop | 停在实际值并上报一个完整 Snapshot |
| 快速连续颜色命令 | 前一渐变被连续替换，只报最后命令最终值 |
| MCU 重启且 ID 不变 | Bridge 复用原 Endpoint，重新 Bind |
| Bridge 重启 | 主动拉清单，不依赖 Add Notify |
| Add Response 丢失 | MCU 重发，Bridge 返回同一 Endpoint/bindingVersion |
| Remove 期间掉电 | MCU 恢复后重发，Bridge 最终删除映射 |
| 子设备离线 | Reachable=false，Endpoint 和映射保留 |
| 子设备上线 | 完整快照成功后 Reachable=true |
| UART 整体掉线 | 全部不可达，Endpoint 保留 |
| 旧 session 或旧 binding 上报 | 丢弃，不改变 Matter 状态 |
| 相同 stateVersion 不同内容 | 返回 BAD_STATE_VERSION，不应用 |
| 重复 Control sequence | MCU 只执行一次 |
| payload 超长或 CRC 错误 | 丢帧，不改变业务状态 |

## 23. 双方冻结检查

- [ ] 帧 offset、长度、大小端和 CRC 参数一致。
- [ ] MCU 接受的 RGB 是独立于 Level 的 8-bit 归一化线性通道比例，不是 PWM 原始值；非黑色值归一化到 max=255。
- [ ] Mode 0/1 都执行 RGB，Mode 2 执行 CW；MCU 持久化并原样上报 Mode。
- [x] CW 和 RGBCW 都覆盖 154~454 mireds。
- [x] uartDeviceId 产品生命周期内不复用，删除事务可跨掉电重试。
- [x] MCU 和 Bridge 当前设备容量均为 32。
- [x] 渐变只报告最终值，快速连续命令采用连续替换策略。
- [ ] UART ACK 不直接决定 Matter 命令响应，最终 Snapshot 是状态真值。
- [ ] 第 22 节用例全部通过后，将文档状态从冻结候选版改为已冻结。
