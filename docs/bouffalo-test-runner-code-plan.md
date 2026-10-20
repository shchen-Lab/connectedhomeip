# Matter 通用测试框架代码计划

状态：待实现。基于 `933944c451` 修订，目标为所有 Matter example 提供通用测试入口。

## 1. 目标和设计原则

框架依据 DUT 的角色、endpoint、设备类型和 cluster 能力规划测试，支持 lighting、bridge、sensor、lock、thermostat 及其他 example。新增 example 应通过能力发现和用例目录接入。

- 连接配置描述传输、controller 存储和测试环境；设备能力来自实际发现与用户提供的声明。
- 用例目录描述测试的适用条件、endpoint 参数、外部依赖和设备影响。
- 通用逻辑不根据 app 名称或 example 路径选择测试，也不固定功能 endpoint 为 1。
- Python 和 YAML 用例共用规划与报告规则，分别保留原框架的执行方式。
- server、client、commissioner 等 DUT 角色由用例元数据明确区分。只实现客户端行为的 example 通过对应测试场景接入，不能按 server DUT 强行配网和扫描。
- 通用接入能力与用例覆盖率分别报告。用例目录尚未覆盖的 cluster 或角色明确显示为覆盖缺口。
- 框架执行配网和测试；固件构建、烧录、重启、网络故障注入等由场景依赖接入。

## 2. 总体流程

```text
解析连接参数、测试选择和依赖
  → 执行前检查
  → 按请求配网 / 使用已有 controller 存储
  → 发现 DUT 能力，或加载离线能力快照
  → 校验用户声明与实际能力
  → 匹配用例及 endpoint，生成执行计划
  → 执行用例 / 调用场景依赖
  → 保存用例结果、覆盖缺口与运行报告
```

## 3. 代码组织

自动选例、结构化元数据和动态 endpoint 的数据处理集中在 Python。两个现有 Shell 文件作为兼容入口，转交新执行器。

| 文件 | 职责 |
| --- | --- |
| `tools/matter_dut/matter_dut_tests.py`（新增） | 通用 CLI、运行流程、参数优先级和退出状态 |
| `tools/matter_dut/matter_dut/backends.py`（新增） | Python/YAML 两种框架的配网、读取能力与用例执行适配 |
| `tools/matter_dut/matter_dut/inventory.py`（新增） | 能力快照、拓扑和声明一致性检查 |
| `tools/matter_dut/matter_dut/catalog.py`（新增） | 读取用例元数据，匹配能力，绑定 endpoint，生成计划 |
| `tools/matter_dut/matter_dut/catalog.json`（新增） | 已核实用例的适用条件、参数和前置要求 |
| `tools/matter_dut/matter_dut/report.py`（新增） | 结果分类、TXT/JSON 报告和重跑输入 |
| `tools/matter_dut/bouffalo_sanity_tests.sh` | 兼容旧命令和环境变量，默认选择 Python backend |
| `tools/matter_dut/bouffalo_yaml_tests.sh` | 兼容旧命令和环境变量，默认选择 YAML backend |
| `tools/matter_dut/bouffalo_test_common.sh` | 保留迁移所需兼容逻辑；迁移结束移除无调用代码 |
| `scripts/tests/test_matter_dut_tools.py`（新增） | 用例规划、结果分类、执行边界和兼容性检查 |
| `tools/matter_dut/README.matter-dut.md`（新增） | 通用框架文档 |
| `tools/matter_dut/README.bouffalo.md` | 旧入口迁移说明 |

公共工具只依赖 Python 标准库；访问 Matter stack 的操作通过指定的测试 venv 和 backend 执行。无需为每个 app 创建一份 runner 或配置档，也不修改 app 源码来完成普通能力发现。

## 4. 第一阶段：通用入口、能力发现、自动选例和报告

### 提交 1：通用入口和兼容参数

- 新增通用入口，选择 `--backend python|yaml`；一次运行对应一种 backend。
- 增加 `--dut-role server|client|commissioner`，默认 server；client/commissioner 模式只检查对应场景要求的目标和参考设备参数，不强制要求存在可扫描的 server DUT。
- 使用 `--case <id>` 指定用例、`--endpoint <id>` 显式指定功能 endpoint；未指定用例时按 `--suite smoke|functional|stateful` 自动匹配。
- 配网方式采用 `--commissioning-method ble-wifi|ble-thread|on-network`。连接方式与 app 名称无关。
- 通用入口显式提供连接参数；旧入口保留 `BOUFFALO_TARGET` 的传输默认映射，允许通过显式传输接入其他目标。
- 提供 `--storage-path`（Python）和 `--storage-directory`（YAML），明确使用已有配对状态。
- 保留旧测试名、省略前缀、Python 冒号参数和环境变量的转换规则。
- 默认执行集合调整为能力匹配后的 `smoke`，替换原固定 16/33 项 lighting 集合；在文档中明确这一行为变化。
- 取消通用入口对 lighting PICS 或 CI PICS 的隐式选择；用户可显式提供 `--pics`。
- 参数顺序为显式 CLI → 对应环境变量 → 通用默认值；不从文件路径猜测 app 类型。

验收：既能通过新入口运行指定用例，也能通过旧入口执行原有指定命令；两种 backend 沿用各自 controller 存储，不迁移 fabric 密钥或自动清空存储。

### 提交 2：执行前检查和能力发现

检查参数、venv、所需模块、chip-tool、用户文件、路径和存储可用性。用户输入相对路径按启动目录解析，框架资源路径按仓库根目录解析。支持从仓库外启动。

对具备 Matter server 行为的 DUT，从根节点遍历 Descriptor 并收集：

- endpoint ID、DeviceTypeList、PartsList、ServerList 和 ClientList。
- 每个相关 cluster 的 FeatureMap、AttributeList、AcceptedCommandList、GeneratedCommandList 和 ClusterRevision。
- bridge 的 Aggregator、桥接设备关系，以及实际动态 endpoint。
- 读取失败、权限不足和未知标准/厂商 cluster 的原始信息；未知值不能当作“不支持”。

保存 `inventory.json`。Python backend 使用其 controller；YAML backend 通过 chip-tool 的交互服务和现有适配器读取，确保发现与测试使用同一个 fabric。不能用另一个 controller 的读取失败判断 DUT 不支持。

能力发现只需读取 Descriptor 和选例相关的全局属性，不默认读取所有业务数据。优先复用仓库现有的设备组成检查和 PICS 映射能力。

新增操作：

| 参数 | 行为 |
| --- | --- |
| `--list` | 离线列出用例目录及适用条件，无需配网或完整运行环境 |
| `--check` | 检查执行所需静态环境，不访问 DUT |
| `--discover` | 访问已配对 DUT，保存能力快照后退出 |
| `--plan` | 发现能力后输出执行计划，不执行测试 |
| `--inventory <file>` | 使用已保存能力快照规划；实际执行前确认目标并刷新相关能力 |
| `--dry-run` | 离线预览；自动选例需提供能力快照，明确指定的用例可展示待核实条件 |

验收：bridge 的 endpoint 1 不会被当作灯；温度传感器不会自动选中调色测试；缺少 Descriptor 或读能力失败会报错，不会得到空计划的“全部通过”。

### 提交 3：用例目录、能力匹配与 PICS 校验

每条用例至少包含：

| 字段 | 含义 |
| --- | --- |
| `id / backend / path` | 测试标识、框架和可定位的脚本 |
| `dut_role` | server、client 或 commissioner 等角色 |
| `scope` | 整节点、单 endpoint、多 endpoint 或完整场景 |
| `requirements` | cluster 角色、FeatureMap、设备类型和属性/命令要求 |
| `pics_requirements` | 需核实的 PICS 条件，保留“未知”状态 |
| `endpoint_parameter` | 该用例是否接受 endpoint，以及实际参数名称 |
| `args / timeout_seconds` | 参数数组及用例超时 |
| `suite` | smoke、functional 或 stateful |
| `effects / fixtures` | 持久状态影响、人工操作、服务端和故障注入等依赖 |

先核实并迁移现有集合，再补充前面讨论的 Identify、网络诊断、配网回滚、多 fabric 等仓库用例。不能仅凭文件名或 cluster 前缀判断用例可自动执行。

匹配规则：

1. 整节点用例执行一次；单 endpoint 用例对每个符合条件的 endpoint 分别生成实例。
2. 多 endpoint、client、commissioner 和场景用例按各自声明的角色与依赖规划，不被单 endpoint 规则展开。
3. 不适用的目录用例列为 `NOT_APPLICABLE`；缺少信息或外部条件列为 `BLOCKED`，均记录原因。
4. 明确指定的用例仍支持执行，包括尚未登记的 Python/YAML 脚本。未登记用例标明适用性和影响尚未核实；不能悄悄替换为默认集合。
5. 保留 Python 显式参数和 YAML 实际 config 的传参规则；不假定所有用例都使用同名 endpoint 参数。
6. 自动目录只纳入已核实的用例；列出检测到但尚无适用自动用例的能力。
7. `--suite smoke` 默认选择可自动执行且影响较小的检查；修改 ACL、fabric、网络和持久配置的测试通过 `stateful` 明确选择，并按依赖串行执行。

PICS 与发现结果分别保存：

- 用户提供的 PICS 表示声明；发现结果表示当前观察。声明支持但观察缺失时报告不一致，相关测试不能被静默过滤。
- 根据 DeviceTypeList 校验规范要求，不因 DUT 漏实现必需 cluster 就把相关检查排除。
- 无用户 PICS 时，仅自动执行适用条件能由实际能力充分确定的用例。
- 可以按 endpoint 生成运行所需的能力片段，但应标为“发现生成的运行配置”，不作为认证 PICS。
- 无法从属性推断的人工能力、PIXIT、特殊 CI 标记和外部条件保持未知，相关用例标为 `BLOCKED`；不能默认设为 0 来绕过测试。
- 多 endpoint 声明的作用域必须明确；不能将节点级 PICS 的所有 cluster 都要求出现在每个 endpoint。

验收：同一套工具可为不同设备模型生成不同计划；同一设备的两个功能 endpoint 可独立执行同一用例；声明与实现不一致会暴露出来。

### 提交 4：统一报告、中断与覆盖统计

生成 `inventory.json`、`plan.json`、`summary.json`、`summary.txt`、逐项结果事件及原始日志。JSON 带 schema 版本，文本和结构化统计使用同一结果来源。

记录 SDK commit/工作区状态、backend、工具版本或哈希、连接方式、Node ID、controller 存储、PICS/能力快照哈希和实际用例参数。用户提供的固件文件独立记录，不声称已验证设备正在运行该文件。

| 状态 | 含义 |
| --- | --- |
| `PASS` | 框架确认成功且实际执行了测试 |
| `FAIL` | 断言、协议检查或声明一致性检查失败 |
| `SKIP` | 已选择用例由框架整项跳过 |
| `TIMEOUT` | 执行器确认超时 |
| `ERROR` | 启动失败、框架异常或摘要不可信 |
| `NOT_RUN` | 已计划但因配网失败、中断等未开始 |
| `NOT_APPLICABLE` | 用例目录中已确认不适用的候选项 |
| `BLOCKED` | 用例适用性或执行依赖未满足 |

报告区分“执行结果”和“覆盖情况”：不适用项不算失败或通过；未覆盖 cluster 不计为通过。用户请求范围内有 BLOCKED、SKIP 或 NOT_RUN 时报告不完整。零可执行用例明确显示无覆盖，不能返回成功的测试报告。

解析日志时清理 ANSI/CR 并读取完整数字字段，同时检查退出码。部分步骤按 PICS 正常跳过不等于整项失败；保留步骤跳过数和 warnings。

整体状态：PASSED、FAILED、INCOMPLETE、INTERRUPTED。退出码：0 为请求范围全部完成并通过，1 为执行失败或不完整，2 为参数或预检查错误；保留 SIGINT/SIGTERM 的 130/143。启动后异常也尽力完成报告，采用原子替换写 JSON。

## 5. 第二阶段：场景依赖与重跑

### 提交 5：设备状态影响和场景依赖

按用例声明接入人工操作或外部工具，而不是按 app 名称调用专用分支。

- bridge：添加/删除子设备、设备上下线、MCU 重启和状态上报。
- OTA：Provider、有效/无效镜像和传输中断条件。
- 通用恢复：设备重启、网络断开恢复、订阅重建和持久配置检查。
- client/commissioner：参考 server、触发 DUT 发起交互的手段，以及对应验收用例。

第一批只实现当前有实际用例需要的依赖接口。外部工具按参数数组调用并定义超时、完成条件及资源释放。未配置的依赖在计划阶段给出 BLOCKED，不要求每个 app 都有 UART、串口或相同复位方法。

动态拓扑场景前后刷新能力快照，核对 endpoint 增删与 PartsList；后续计划重新绑定 endpoint，不能继续使用已删除的 endpoint。静态测试期间拓扑意外变化也要报告，不能悄悄改变本次覆盖对象。

### 提交 6：重跑和兼容性验收

新增 `--rerun-failed <summary.json>`：

- 读取同类 backend 的支持版本报告，仅恢复 FAIL、TIMEOUT、ERROR 实例。
- 使用保存的用例身份、参数和作用域重新规划；报告内容不能作为 Shell 代码执行。
- 重新发现设备并核对 Node ID、controller 上下文、PICS 和 endpoint 身份。
- 动态 endpoint 已变化时，只有可可靠关联到同一设备才重新绑定，否则明确报告需要重新规划。
- 使用新的日志目录并关联原报告；状态修改类用例展示前置条件。
- 没有失败项时明确结束，不运行默认全集。

## 6. 验证计划

实现后验证：

1. 使用代表性能力快照验证选例：灯、bridge、传感器、门锁、温控器；另含 client/commissioner 用例的角色和依赖检查。
2. 检查多 endpoint、多设备类型、未知 cluster、权限不足、声明不一致、缺少必需 cluster 和动态拓扑变化。
3. 使用真实格式日志样例检查成功、失败、跳过、超时、异常、中断与报告统计；不依赖物理设备来验证日志解析。
4. 使用模拟子进程验证参数数组、路径含空格、外部依赖缺失和子进程释放。
5. 对可用的 lighting、bridge 和至少一种非 lighting server DUT 进行实机验收，分别验证两个 backend。
6. 有条件的 BLE Wi-Fi、BLE Thread、on-network 路径分别验收；client/commissioner 场景提供对应参考设备。
7. 验证任意新增 example 可以通过能力发现和目录元数据接入，通用执行器中没有 app 名称判断。

本次只修订代码计划，未执行测试。实机未覆盖的 app、传输和场景应在交付记录中明确列出。

## 7. 交付顺序与接口示例

第一阶段按提交 1 → 2 → 3 → 4 交付通用框架；第二阶段按提交 5 → 6 增加场景测试和重跑。每个提交同步更新文档和验收结果。

以下为计划中的接口：

```sh
# 列出已登记用例及条件
python3 tools/matter_dut/matter_dut_tests.py --backend python --list

# 对已配对 DUT 发现能力并生成用例计划
python3 tools/matter_dut/matter_dut_tests.py --backend python \
  --storage-path /path/controller.json --node-id 0x12344321 --plan

# 根据能力执行常规功能测试，app 类型由 DUT 能力决定
python3 tools/matter_dut/matter_dut_tests.py --backend yaml \
  --chip-tool /path/chip-tool --storage-directory /path/controller \
  --node-id 0x12344321 --pics /path/device.pics --suite functional

# 使用能力快照离线查看自动选例结果
python3 tools/matter_dut/matter_dut_tests.py --backend python \
  --inventory /path/inventory.json --dry-run

# 使用对应 backend 重跑失败实例
python3 tools/matter_dut/matter_dut_tests.py --backend yaml \
  --rerun-failed /path/summary.json
```
