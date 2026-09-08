# Matter Bridge UART v2 手机联调测试计划

日期：2026-09-08。基准：Obsidian Rev.4、当前 HS/CT 源码。
状态：已开始本轮实板测试，用户自研 App 已配网。固件由用户编译烧录。
首轮证据见 ../../test_results/bridge_uart_v2/phone-20260908-01/results.md。
新增 phone_session.py 提供单串口owner的JSON交互增删/上下线/完整状态快照及真实IO日志；18项Python测试通过。
下文M1–M6仍是完整工具验收要求；增量上报、重启待删除续传、异常注入和完整渐变未完成。

## 1. 目标与分工

用户用手机 App 配网 Bridge、查看子设备并发出控制。电脑作为模拟 MCU，使用 UART 创建/删除动态设备、改变在线状态和实际属性、接收手机转来的命令。助手负责操作模拟器、核对原始帧并维护逐项记录。用户每次反馈“用例编号 + 手机看到的结果”，有异常时补充出现时刻和画面。

手机控制必须走完整闭环：

~~~text
手机/家庭中枢 → Matter → Bridge → UART命令0x10
  ↑                                  ↓
显示实际状态 ← Matter属性报告 ← Bridge ← MCU响应0x12 + 实际状态0x40/0x20
~~~

UART rx/tx 均以电脑为观察点：rx=Bridge→模拟MCU，tx=模拟MCU→Bridge。
只看到0x10证明命令到达；0x12 OK证明接受；有效上报与手机最终更新才能证明闭环。命令OK不等于渐变完成。

## 2. 本轮范围

先测2台设备：CW(type=0x10, caps=0x13)和HS/色温(type=0x11, caps=0x17)。MCU设备ID与Endpoint分开记录，不硬编码Endpoint，不向Aggregator EP1发送灯属性。

| 产品属性 | 数值/行为 |
| --- | --- |
| 彩灯 ColorControl FeatureMap / ColorCapabilities | 0x11，HS+CT；CW为0x10 |
| OnOff | 0/1；关灯保留亮度和颜色 |
| Level | 1..254，约对应手机1..100%；UI可能取整 |
| Hue / Saturation | 各0..254；Hue≈raw×360/254度，Saturation≈raw×100/254% |
| CT | 154..454 mired；K≈1000000/mired，值越大越暖 |
| HS完整快照 | mask=0x000F，payload=10 bytes，模式0 |
| CT完整快照 | mask=0x0043，payload=10 bytes，模式2 |
| XY/RGB | 不支持、不转换、不下发；手机发XY记兼容性问题 |

本轮重心是动态设备生命周期和手机双向操作。32设备/第33个拒绝、断电写入边界、30分钟压力、全面命令组合和真实MCU物理输出作为后续扩展验收，不能用本轮手机观察替代。

## 3. 开始前确认

1. 记录手机型号/OS、App名称/版本、家庭中枢型号/版本及网络拓扑。当前App尚未确定，具体页面入口待补。
2. 用户编译并烧录本次源码，记录固件构建时间/版本/hash；助手不编译、不烧录。
3. 协议口 /dev/ttyUSB0：921600 8N1，无流控；日志口 /dev/ttyUSB2：2000000。重插后确认映射；协议UART不混入文本日志。
4. PICOCOM和旧runner释放协议口；只有一个模拟器负责读写。console可独立采集。
5. 使用本轮独立模拟器状态目录。已有devices.json会覆盖fixture，不能把旧设备误当默认设备。Bridge也可能恢复已有KVS设备；先列出旧映射，通过正式Remove清理指定测试设备。空fixture不等于清空Bridge。
6. 手机已拥有Bridge时复用现有配网。确需新配网时由用户在App完成；不用旧chip-tool Node1234来证明手机可访问，也不自行恢复出厂。
7. 模拟器需持续响应HELLO/清单/心跳/命令，不能只发一条Add后停止。动态子设备不单独扫描二维码配网。

## 4. 执行前的工具门槛

当前 mcu_simulator.py 能启动CW+HS清单、响应Bind/心跳、执行即时OnOff/Level/HS/CT并保存状态。
它尚不提供交互式增删、上下线、主动属性上报，因此目前不能执行完整P01–P12；先完成以下工具任务，再进入正式手机联调。

| 任务 | 要求 | 验证门槛 |
| --- | --- | --- |
| M1 交互控制 | 同一串口owner提供 list/add/remove/offline/online/state/snapshot 操作，动作前后显示device/endpoint/binding/version | 主机用例覆盖正常时序、失败不提交、重启恢复；禁止通过重启fixture冒充在线Add |
| M2 通知事务 | Add响应后等待Bridge Bind，Bind响应后发初始快照；Online响应后发完整快照；Remove成功前保留待删除记录 | 0x36/38/3C/3E和响应匹配；BUSY/超时有界重试，同一方向最多一个普通请求 |
| M3 主动状态 | 递增stateVersion，同模式用0x20增量；切换HS/CT用0x40完整快照；不定时全量补偿 | 同版本不同属性、旧版本、同值重复、非法字段均有验证 |
| M4 手机命令适配 | 首次捕获手机实际cluster/command/transition/options；补充其使用的渐变/Move/Step/Stop | 当前非零渐变/多数Move/Step返回0x0C，不得悄悄改成即时成功；未补齐的用例标BLOCKED |
| M5 证据记录 | 真实write完成/read收到时记录帧、单调时间和墙钟时间；解码命令名、参数、响应、stateVersion、执行计数 | 当前日志主要是raw_hex，部分tx在构造/排队时打印，缓存重传rx覆盖也不完整；不能据此测执行时延 |
| M6 小范围异常 | 可指定下一次命令失败或延迟响应，保存旧绑定帧，用于P11/P12 | 明确一次故障，不引入随机丢包；故障结束后正常通信恢复 |

拟提供的交互操作是需求，不是现有CLI：
add cw <id>、add hsct <id>、offline <id>、online <id>、
state <id> on=1 level=128 hue=80 saturation=140、
state <id> ct=300、remove <id>、list。
最终命令语法以工具实现后的--help为准。

当前仅启动固定清单的可用入口如下，会在握手时添加fixture中的设备，不能代替“配网后在线Add”：

~~~bash
python3 tools/bridge_uart_v2/mcu_simulator.py \
  --port /dev/ttyUSB0 --baud 921600 \
  --devices tools/bridge_uart_v2/fixtures/cw_and_hs.json \
  --state-dir /tmp/bridge-uart-v2-phone-01
~~~

使用新的目录编号保留历史状态，不运行自动chip-tool smoke来代替手机操作。正式runner需同时保存uart.jsonl和bridge.log。

## 5. 逐项执行

每次只执行一个操作，等响应/快照完成并观察手机后再继续。普通界面更新先观察10秒；动态拓扑/在线状态观察30秒。超时先记录实际等待时间，再尝试刷新或重进页面，并注明“需刷新”。这些是测试观察窗口，不是协议或App时延保证。渐变另按实际transitionTime增加观察时间。

| ID | 前置与操作 | UART/Bridge预期 | 手机观察与通过标准 |
| --- | --- | --- | --- |
| P00 空清单配网 | 确认测试映射为空；启动空清单模拟器；用户手机配网或复用已有Bridge | HELLO 0x30/31，LIST 0x32→33/35（0条），持续0x42/43；无Bind/演示设备 | Bridge可用；无旧测试灯/4台演示灯。App可能只展示配件或子设备，记录实际呈现 |
| P01 配网后添加CW | MCU新增稳定ID=1，type0x10/caps0x13，发送Add | 0x36→37 OK含分配结果；0x3A→3B；0x40(mask0x43)→41 OK；快照后可达 | 无需再次配网出现1台可控色温灯；记录卡片名称、Endpoint、是否自动发现及刷新需求 |
| P02 添加HS/CT | ID=2，type0x11/caps0x17，同一会话在线Add | 获得另一Endpoint；HS快照mask0x0F；不改变ID1绑定 | 出现第二台彩灯；开关/亮度/彩色/色温入口符合App支持能力；无重复卡片 |
| P03 MCU主动上报开关/亮度 | 对ID1，先off，再on，再level=64→192，逐步等待 | 0x20增量含递增版本，无逐帧ACK；ID2不受影响 | 手机反映关/开和约25%→76%；关灯不把保存Level置0 |
| P04 MCU主动上报HS/CT | 对ID2开灯；HS=(80,140)，再CT=300，再HS=(170,200) | 模式切换发10-byte完整快照并获ACK；模式0→2→0；无XY属性 | 彩色/色温呈现跟随变化；CT约3333K。不要求手机显示精确raw或立刻切换编辑面板 |
| P05 手机开/关 | 用户分别控制两台灯Off→On，每次等闭环；App有Toggle入口再测 | rx0x10 cluster0x0006 Off/On/Toggle；对应0x12；tx实际状态。配置OnLevel时允许转换为LevelWithOnOff | 手机最终状态与模拟MCU一致，只操作目标设备；按同一sequence核对重传不重复执行 |
| P06 手机调光 | 对ID1和ID2分别选25%→75%，先单次落点，再短拖动 | rx LevelControl 0x0008；记录具体命令、nullable/transition/options；模拟实际Level上报 | 手机最终亮度与模拟器一致，UI取整允许；连续拖动记录Busy/失败和最终收敛，不能只看第一帧 |
| P07 手机选颜色 | ID2开灯，依次选择红/绿/蓝附近及一个低饱和色 | rx ColorControl 0x0300的HS命令0x00..0x06；MCU响应+实际HS；无0x07/08/09 | App最后颜色与模拟状态方向一致；精确raw以UART为准。若App发XY，记录不兼容，不做转换掩盖 |
| P08 手机调色温/关灯保持 | 两台分别选冷→暖；ID2再回彩色；Off后再On | CT命令0x0A或App实际使用的CT Move/Step；CT快照模式2，HS模式0；关灯保存状态 | 冷暖方向正确、模式切换可用；开灯恢复保存状态。App不可设置某范围时记N/A，不能伪造完成 |
| P09 单设备离线/恢复 | ID1 Offline；保持ID2可控；随后ID1 Online，暂不发快照，最后发完整快照 | 0x3E→3F，Reachable=false且Endpoint保留；0x3C→3D后仍离线；0x40→41后在线 | ID1显示不可用或控制失败，ID2正常；恢复后同一卡片可控，无重复设备。App可能延迟显示离线，记录延迟 |
| P10 删除/幂等/再添加 | Remove ID1；同请求重传；删除成功后再添加同ID1；重复Add | 0x38→39；清除动态Endpoint。重复操作无多次创建/删除；重新分配有效绑定，旧绑定失效 | ID1消失或被App标为已删除，ID2仍可控；再添加仅1台。新Endpoint不要求等于旧值 |
| P11 两端分别重启 | 先仅重启Bridge，再仅重启模拟MCU（保留设备状态），不恢复出厂 | 新boot/session重新握手/清单/Bind/快照；已有Endpoint/UniqueID稳定，绑定可更新；旧帧拒绝 | 先不可用后恢复；不重配网、不新增重复卡片；最终状态来自MCU |
| P12 明确失败与恢复 | 在线设备下一条手机命令故意返回合法错误，再执行正常命令；另做一次命令超时 | 匹配0x12错误或超时，Bridge不把未执行目标当实际；有限重试，恢复后重新同步 | App可能乐观展示目标，但最终必须回到实际状态或报失败；下一条正常控制成功。无明确MCU响应前不能计闭环PASS |

正常控制用例P05–P08若遇非零渐变/未实现命令，先完成M4再复测；记录BLOCKED（模拟器缺能力），不能据此宣称Bridge执行正确或错误。

## 6. 日志与判定

每轮目录建议 test_results/bridge_uart_v2/phone-YYYYMMDD-HHMM/：

- manifest.md：App/OS/中枢/固件/协议hash、串口、测试设备清单、配网是否复用。
- uart.jsonl：原始字节和解码值、真实TX/RX时间、session/sequence/device/endpoint/binding、错误码。
- bridge.log：console原始日志，保留会话、绑定、拒绝及复位上下文；共享前隐藏配网凭据。
- actions.jsonl：用例ID、操作者、操作与时刻、预期目标；phone-notes.md记录手机现象和是否刷新。
- bindings.json：各阶段device→endpoint/binding/UniqueID映射。
- results.md：逐项状态、证据定位、问题和复测结果。需要时附用户提供的截图/录像。

结果表模板：

| 用例 | UART结果 | 手机结果 | 总状态 | 时间/sequence/证据 | 问题与复测 |
| --- | --- | --- | --- | --- | --- |
| P00 | NOT_RUN | NOT_RUN | NOT_RUN | — | — |

PASS需两侧证据一致。FAIL表示已执行但不符合预期；BLOCKED表示工具、固件或平台前置缺失；N/A需写明App没有入口或不适用；未执行为NOT_RUN。UART成功而手机需刷新时分别记录，不能自动合并为完全通过。

手机画面不能精确证明FeatureMap、ColorMode、PartsList或未显示的数值。若遇拓扑更新/精确属性争议，可用App诊断或在用户共享Fabric后用chip-tool读取；不因此自动重配网。没有这些证据的属性级断言保持未验证。

## 7. 完成标准与后续

本轮主路径要求P00–P12适用项完成并有日志和用户观察记录；App不具备的入口写清楚。手机控制命令族以实测为准，不预先假定全是即时HS命令。动态发现、在线/删除UI、控制回报分别结论。

后续扩展：32/33容量、持久化边界真实断电、清单缺项只离线、同版本多属性、旧绑定/非法mask/坏CRC恢复、30分钟混合流量。最后替换真实MCU，保持相同手机步骤，增加HS/CT物理输出、渐变/Stop和范围验收。
