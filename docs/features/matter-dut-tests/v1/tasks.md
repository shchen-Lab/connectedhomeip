---
module: matter-dut-tests
version: v1
date: 2026-10-20
design: docs/features/matter-dut-tests/v1/design.md
total_tasks: 13
status: in-progress
tags: [tasks, backend]
---

# Matter DUT 通用测试框架任务清单

## 执行顺序

| # | 任务 | 状态 | 依赖 | 并行 |
|---|------|------|------|------|
| T01 | 定义公共 inventory 与 Descriptor 拓扑模型 | ✅ | - | - |
| T02 | 实现用例目录加载与静态校验 | ✅ | T01 | - |
| T03 | 实现能力、角色和 PICS 规划逻辑 | ✅ | T01, T02 | - |
| T04 | 实现 Python backend 发现与用例执行 | 🟡 | T01 | - |
| T05 | 实现 YAML backend 的结构化发现与用例执行 | 🟡 | T01 | - |
| T06 | 实现结果归一、覆盖缺口和报告写入 | ✅ | T01, T03 | - |
| T07 | 登记 v1 已核实用例元数据 | 🟡 | T02, T03 | - |
| T08 | 实现通用 CLI 诊断和计划模式 | 🟡 | T02, T03, T04, T05, T06 | - |
| T09 | 接通 CLI 的用例执行、异常和退出状态 | 🟡 | T07, T08 | - |
| T10 | 保留并增强 Python 旧入口 | 🟡 | T09 | - |
| T11 | 保留并增强 YAML 旧入口 | 🟡 | T09 | - |
| T12 | 兼容旧配置并增加 CLI 到旧 runner 的快捷转发 | 🟡 | T10, T11 | - |
| T13 | 更新用法文档并完成验收核对 | 🟡 | T09, T10, T11, T12 | - |

依赖顺序先建立 backend 无关的数据模型，再实现目录校验和规划规则；Python/YAML adapter 可在模型稳定后分别实现。CLI 已开始整合目录、adapter 和 reporter；旧 shell 入口迁移尚未开始。T13 同时核对设计验收项。设计要求使用不同拓扑的离线 inventory fixture 验证规划，但未指定测试文件、测试框架或验证命令，相关实现位置标为 `[设计缺漏?]`，需在实现中先核对仓库现有测试惯例，不能猜测或覆盖无关文件。

---

## T01: 定义公共 inventory 与 Descriptor 拓扑模型

- **状态**: ✅
- **目标文件**: `tools/matter_dut/matter_dut/inventory.py`
- **变更类型**: 新建

### 子步骤

1. 定义公共模型及 JSON 序列化/反序列化：`BackendContext`、`Inventory`、`Endpoint`、`DeviceType`、`ClusterCapability`、`ReadIssue`。字段和 nullable/unknown 语义逐项遵循 design.md §3.1。
2. 实现从 Descriptor 读取值构建 endpoint 拓扑的纯数据处理入口；endpoint ID 作为地址保留，不推断功能身份或固定 endpoint 1。
3. 支持读取不完整的数据：未知的 `parts_list`、feature map、属性/命令 ID、revision 保持 unknown；读取问题按 endpoint/cluster/attribute 路径序列化。
4. 序列化 inventory 时包含 `schema_version`、backend context 和 `captured_at`；存储引用不得包含 Matter 密钥内容。

### 代码骨架

```python
def inventory_to_dict(inventory: Inventory) -> dict[str, object]: ...
def inventory_from_dict(data: dict[str, object]) -> Inventory: ...
def build_inventory(context: BackendContext, descriptor_data: object,
                    capability_data: object, read_issues: list[ReadIssue]) -> Inventory: ...
```

### 红线

- 不将未知值转换为“不支持”、空集合或 `NOT_APPLICABLE`。
- 不依赖 backend SDK 类型，不在公共模型中导入 Matter Python SDK 或 chip-tool 实现。
- 不把 endpoint ID 当作设备功能身份；PartsList 是拓扑关系来源。
- 不序列化存储中的 fabric 密钥或凭据。

---

## T02: 实现用例目录加载与静态校验

- **状态**: ✅
- **目标文件**: `tools/matter_dut/matter_dut/catalog.py`
- **变更类型**: 新建

### 子步骤

1. 定义 `CaseDefinition`、`Requirement`、`CaseInstance`、`Selection`、`PlanEntry`、`ValidationIssue` 等公共目录/规划类型，字段遵循 design.md §3.1。
2. 实现 `load_catalog(path)`，读取 JSON 目录并校验 `id` 唯一性、backend、role、scope、suite、脚本路径、参数字段和超时。
3. 校验脚本路径相对测试根目录解析，拒绝越出测试根目录的路径；不执行或导入目录里的用例脚本。
4. 错误使用可区分的 `CATALOG_INVALID`，保留具体字段和目录行/条目上下文供 CLI 展示。

### 代码骨架

```python
def load_catalog(path: str) -> list[CaseDefinition]: ...
def validate_case_definition(data: dict[str, object]) -> CaseDefinition: ...
```

### 红线

- 不从 app 名称、源码目录名或脚本文件名推断用例语义和适用条件。
- 未登记脚本不能被目录扫描自动加入目录。
- 不新增第三方依赖；公共目录逻辑只依赖 Python 标准库。

---

## T03: 实现能力、角色和 PICS 规划逻辑

- **状态**: ✅
- **目标文件**: `tools/matter_dut/matter_dut/catalog.py`
- **变更类型**: 修改

### 子步骤

1. 定义 `PicsDeclaration` 与规划结果结构，并实现 PICS 文件解析；明确 node/endpoint scope，保留 unknown，不给未知项补默认值。
2. 实现 `build_plan(inventory, pics, cases, selection)`，按 backend、DUT role、node/endpoint/scenario scope、cluster role、device type、feature bits、attribute、command 和 PICS requirement 匹配。
3. 按 design.md 规则生成 endpoint 级 `CaseInstance`：同一用例匹配多个 endpoint 时生成多项；`endpoint_argument` 按数组参数传递。
4. 角色相同为兼容；`mixed` 实际角色可匹配任一专用角色；不兼容的已知角色标 `NOT_APPLICABLE`。场景依赖未配置、发现不完整或能力未知标 `BLOCKED`。
5. PICS 支持但实际缺失时保留候选并生成关联的 `PICS_CONFLICT` error；PICS 不支持但观察到能力时生成 warning，不覆盖声明。
6. 对显式未登记脚本创建临时定义和唯一报告 ID，生成 `UNVERIFIED` 项；对显式未知 case ID 返回 `CASE_UNKNOWN`。空计划是合法结果。

### 代码骨架

```python
def build_plan(inventory: Inventory | None, pics: PicsDeclaration | None,
               cases: list[CaseDefinition], selection: Selection) -> PlanningResult: ...
def compare_pics_to_inventory(pics: PicsDeclaration,
                              inventory: Inventory) -> list[ValidationIssue]: ...
```

### 红线

- 不因 PICS 声明与发现结果不一致而静默删除测试候选。
- 未知能力只能阻塞相关测试，不得据此判为不支持或不适用。
- 未登记脚本只允许显式选择后运行，不应用自动能力匹配或推断。
- 不能跨 backend 复用 inventory、配对状态或执行上下文。

---

## T04: 实现 Python backend 发现与用例执行

- **状态**: 🟡
- **目标文件**: `tools/matter_dut/matter_dut/backends.py`
- **变更类型**: 新建

### 子步骤

1. 定义 backend adapter 的公共入口，隔离 backend 上下文创建、能力发现、用例执行和关闭/超时行为。
2. Python adapter 在选定 `storage_path` 和 `node_id` 上使用现有 DeviceController 的 `ReadAttribute` / `Read` 能力，读取 Descriptor 拓扑及用例目录需求的全局属性。
3. 将属性值、读取路径和失败状态映射到 T01 公共模型；部分读取失败记录 `ReadIssue` 并保留其他成功读取的数据。
4. 执行已登记 Python 脚本时组合目录参数、PICS、node ID、endpoint 和 timeout；显式未登记脚本则使用用户传入参数并返回原始 exit code、duration、日志引用。
5. 将连接失败、读取失败、启动失败、超时转换为设计定义的 backend 错误或 `CaseResult`。

### 代码骨架

```python
class BackendAdapter:
    def discover(self, context: BackendContext, requirements: list[Requirement]) -> Inventory: ...
    def execute(self, context: BackendContext, entry: PlanEntry) -> CaseResult: ...
```

### 红线

- Matter SDK 导入和调用只能留在 backend 实现；planner/catalog 不依赖 SDK。
- 仅使用本次 Python backend 的 controller 存储和 fabric。
- 不能假定所有 app 均有 server endpoint；client DUT 可以没有功能 server endpoint。
- 读取失败不能伪装成 capability absence。

---

## T05: 实现 YAML backend 的结构化发现与用例执行

- **状态**: 🟡
- **目标文件**: `tools/matter_dut/matter_dut/backends.py`
- **变更类型**: 修改

### 子步骤

1. 实现 YAML adapter 的当前交互 server session 管理，使用选定 `chip_tool` 与 `storage_directory`，保持配对上下文与旧 runner 一致。
2. 扩展/适配现有 `scripts/tests/chipyaml/chiptool.py` WebSocket 通信路径，使 Descriptor 和全局属性读取返回结构化 Matter Read response、attribute path 和 status；需要改该文件时将其作为本任务内的额外目标文件并先核对其协议约束。
3. 将结构化读取映射到公共 `Inventory`；不得从终端文本日志解析属性值。
4. 如果所选 interactive session 无法提供结构化响应，`discover` 明确返回 `BACKEND_UNAVAILABLE`，不创建另一套 fabric 偷读数据。
5. 通过既有 YAML WebSocket runner/chip-tool adapter 执行 YAML 用例，传入 node ID、PICS、超时和目录参数，并归一执行结果。

### 红线

- 不假设 `send_raw_command` 当前已经能返回属性值；现状只提供布尔状态并打印日志。
- 不在 Python 与 YAML backend 间迁移、共享或推断 fabric 密钥。
- 不把 shell 输出、ANSI 清理后的终端摘要作为 inventory 数据源。
- 不在未核实 chip-tool 命令/响应契约前硬编码设计稿未规定的 API 名称。

---

## T06: 实现结果归一、覆盖缺口和报告写入

- **状态**: ✅
- **目标文件**: `tools/matter_dut/matter_dut/report.py`
- **变更类型**: 新建

### 子步骤

1. 定义 `CaseResult`、`RunReport` 和 `ValidationIssue` 的 JSON 结构，所有计划项都必须在结果中有对应项。
2. 将 `READY`、`UNVERIFIED` 执行结果与 `BLOCKED`、`NOT_APPLICABLE` 计划状态、未启动的 `NOT_RUN` 状态逐项关联；保存 `case_id`、endpoint、exit code、duration、reason 和 log reference。
3. 依据 design.md §4.2 实现整体状态归并：验证 error 或 FAIL/ERROR/TIMEOUT 优先 FAILED，其次 INTERRUPTED，再判 INCOMPLETE，范围完整通过才 PASSED。
4. 计算并写出 coverage gaps；输出版本化 JSON inventory、plan、summary 和便于阅读的 TXT 投影，写入错误返回 `REPORT_WRITE_FAILED`。
5. 输出路径由调用方指定；确保写报告失败不会被当作测试通过。

### 代码骨架

```python
def aggregate_status(report: RunReport) -> str: ...
def build_run_report(context: BackendContext, inventory: Inventory | None,
                     plan: list[PlanEntry], results: list[CaseResult],
                     issues: list[ValidationIssue]) -> RunReport: ...
def write_report(report: RunReport, output_dir: str) -> dict[str, str]: ...
```

### 红线

- 未运行、阻塞、跳过、未核实项或 coverage gap 不得汇总为 PASSED。
- 报告不能遗漏计划项，也不能把 PICS 校验问题伪装为用例失败/通过。
- TXT 是 JSON 事实数据的投影；不能仅保存终端摘要而丢弃版本化结构。

---

## T07: 登记 v1 已核实用例元数据

- **状态**: 🟡
- **目标文件**: `tools/matter_dut/matter_dut/catalog.json`
- **变更类型**: 新建

### 子步骤

1. 仅登记在本仓库存在、backend 明确、参数和前置条件已核实的 Python/YAML 用例；每项填写唯一 id、脚本相对路径、DUT role、scope、requirements、PICS requirements、endpoint argument、suite、effects、fixtures 和 timeout。
2. 以当前 Bouffalo runner 中已验证可执行的用例作为首批候选；重新核对参数和文件路径，不将既有快捷集合未经验证地整体视为适用于所有 DUT。
3. 按能力表达适用性，不加入 app/example 名称匹配字段或分支。
4. 确保目录可为空或仅含已核实子集；coverage gap 由报告表达，不为达到覆盖率虚构条目。

### 红线

- 不登记未经核实适用条件和执行参数的用例供自动规划。
- 不根据发现到的能力改写用户 PICS 声明。
- 不把场景依赖写成隐式 app 名称判断；缺失 fixture 必须能被规划为 `BLOCKED`。

---

## T08: 实现通用 CLI 诊断和计划模式

- **状态**: 🟡
- **目标文件**: `tools/matter_dut/matter_dut_tests.py`
- **变更类型**: 新建

### 子步骤

1. 建立单一 CLI 入口，解析 backend、storage、node ID、DUT role、PICS、suite、case、explicit script、case args、inventory 和 report output 参数。
2. 实现互斥模式 `--check`、`--list`、`--discover`、`--plan`、`--dry-run` 和默认 run；检测冲突参数，`--inventory` 仅用于离线计划/预览。
3. `--check` 检查静态运行环境但不访问 DUT；`--list` 离线列出已登记用例；二者不得创建 controller 或启动 interactive server。
4. `--discover` 在线读取并保存 inventory 后退出；`--plan` 支持在线发现或离线 inventory；`--dry-run` 展示计划和最终参数但不执行用例。
5. 默认值遵循 design.md：role 默认 server，suite 默认 smoke；backend 必须显式选择，storage 参数按 backend 解析。
6. `--commission` 只调用选定 backend 已有配网路径；未配置场景依赖可见地标为 BLOCKED。

### 代码骨架

```python
def parse_args(argv: list[str]) -> Selection: ...
def main(argv: list[str] | None = None) -> int: ...
```

### 红线

- `--list`、`--check` 不得访问 DUT；plan/dry-run 不得启动测试用例。
- 不允许在一次运行中混用 Python 与 YAML backend。
- `--inventory` 与在线 `--discover` 不得同时使用。
- 不添加烧录、复位、固件身份推断或 fabric 迁移行为。

---

## T09: 接通 CLI 的用例执行、异常和退出状态

- **状态**: 🟡
- **目标文件**: `tools/matter_dut/matter_dut_tests.py`
- **变更类型**: 修改

### 子步骤

1. 串联 backend context、目录加载、discover/inventory 输入、PICS 解析、planning、execution 和 report。
2. 仅执行 READY 与显式请求的 UNVERIFIED 项；将 BLOCKED、NOT_APPLICABLE 和未启动的项同样写入结果。
3. 执行前验证 inventory/backend/storage context 一致；不一致时停止执行，输出 `CONTEXT_MISMATCH` 与 FAILED 报告/退出状态。
4. 处理中断：记录已完成结果，剩余可执行项标 `NOT_RUN`，整体为 INTERRUPTED（若已有失败则按 FAILED 优先规则归并）。
5. 映射 `CATALOG_INVALID`、`CASE_UNKNOWN`、`PICS_INVALID`、`BACKEND_UNAVAILABLE`、`START_FAILED`、`TIMEOUT`、`REPORT_WRITE_FAILED` 等错误到可识别 CLI 输出和非零退出状态。
6. 在报告和控制台中分别展示执行结果、validation issues、coverage gaps 和 overall，不能将不完整覆盖显示为全通过。

### 红线

- 不自动重跑未登记脚本或有副作用的失败用例。
- 不因 PICS error 删除候选用例；执行安全性按设计中的 BLOCKED 状态处理。
- 不跨 backend 直接消费对方 inventory、配网或执行结果。

---

## T10: 保留并增强 Python 旧入口

- **状态**: ⬜
- **目标文件**: `tools/matter_dut/bouffalo_sanity_tests.sh`
- **变更类型**: 修改

### 子步骤

1. 保持原快捷用例清单、任意 `src/python_testing/` 脚本选择、位置参数和结果目录行为不变。
2. 增加不连接 DUT 的 `--list`，列出原有快捷用例。
3. 允许调用方通过 `PYENV` 选择已有 Python 测试环境，默认仍使用原来的 `out/python_env`。
4. 通用 CLI 对目录外的 case 名称转发至本入口，保留该 runner 的原参数解析、测试汇总和退出码。

### 红线

- 不把旧脚本改成只负责参数映射的薄包装器。
- 不缩减原快捷清单或移除任意 Python 测试脚本选择能力。
- 不改变 Python backend 的 storage/fabric 归属。
- 不让 Python wrapper 调用 YAML backend 或共享 YAML storage。

---

## T11: 保留并增强 YAML 旧入口

- **状态**: ⬜
- **目标文件**: `tools/matter_dut/bouffalo_yaml_tests.sh`
- **变更类型**: 修改

### 子步骤

1. 保持原快捷用例清单、关键字过滤、任意 YAML suite 选择和结果目录行为不变。
2. 增加不连接 DUT 的 `--list`，列出原有快捷用例。
3. 允许调用方通过 `PYENV` 选择已有 Python 测试环境，默认仍使用原来的 `out/python_env`。
4. 通用 CLI 对目录外的 case 名称转发至本入口，保留其 chip-tool、storage directory 和 node ID 处理路径。

### 红线

- 不把旧脚本改成只负责参数映射的薄包装器。
- 不缩减原快捷清单或移除任意 YAML suite 选择能力。
- 不改变 YAML backend 的 chip-tool storage/fabric 归属。
- 不将 Python DeviceController 用于 YAML backend 的发现或执行。

---

## T12: 兼容旧配置并增加快捷转发

- **状态**: ⬜
- **目标文件**: `tools/matter_dut/bouffalo_test_common.sh`
- **变更类型**: 修改

### 子步骤

1. 保留旧入口正在使用的参数解析和 transport 配置，不移除其快捷测试路径。
2. 在通用 CLI 中将未登记的 Python/YAML case 名称转发给对应旧 runner，并传递显式 node ID、storage、PICS、timeout 和 commissioning 参数。
3. 确保 `--list` 可以同时显示新 catalog 条目和旧快捷用例；列表操作不连接 DUT。
4. 文档明确区分已登记的自动规划用例与由旧 runner 执行的兼容用例。

### 红线

- 不能在 T10/T11 仍依赖 helper 时提前删除其逻辑。
- 不删除与本任务无关的 shell 公共功能。
- 不扩展支持的硬件目标或配网流程；v1 只复用既有功能。

---

## T13: 更新用法文档并完成验收核对

- **状态**: 🟡
- **目标文件**: `tools/matter_dut/README.matter-dut.md`、`tools/matter_dut/README.bouffalo.md`
- **变更类型**: 新建、修改

### 子步骤

1. 新建通用框架文档，说明 backend 上下文独立、CLI 模式、目录 schema、PICS/observed capability 区别、inventory/plan/report 结构、UNVERIFIED 运行和错误状态。
2. 更新 Bouffalo 文档，把旧入口描述为兼容包装器，列出旧变量/参数到通用 CLI 的映射和迁移限制。
3. 按 design.md §6 逐项核对验收标准，特别检查动态 endpoint、未知能力阻塞、PICS conflict、显式未登记脚本、模式不访问 DUT、commission/backend 隔离及状态归并。
4. 使用不同 endpoint/cluster topology 的离线 inventory fixture 验证 planner 对 lighting、bridge、sensor 能力形态不依赖 app 名称；测试文件位置和实际命令需按仓库现有测试惯例确认。[设计缺漏?]
5. 对照 §7 检查 v1 没有引入烧录、复位、多 fabric 共享、自动设备场景编排或未登记脚本自动推断。

### 红线

- 文档不得宣称框架接入能力等同于所有 Matter 测试用例已覆盖。
- 不把 inventory 自动转换为认证用 PICS。
- 不删除既有 README 内容中仍有效的环境配置或安全/副作用说明。
- 不为了 fixture 验证覆盖或重写用户已有的无关未提交文件。

---

## 最终验证

- ⬜ 按项目现有测试/质量规则验证新增 Python 模块、shell wrapper 和 CLI；仓库规则与测试入口需在实现前读取确认。
- ⬜ 完成 design.md §6 全部可执行验收项，并记录无法在当前环境验证的项及原因。
- ⬜ 验证 `--list`、`--check` 不访问 DUT，`--plan` / `--dry-run` 不执行用例，`--discover` 保存 inventory。
- ⬜ 验证 Python/YAML 使用各自 storage/fabric，报告记录 backend context，不发生跨 backend 状态复用。
- ⬜ 验证未知能力、PICS conflict、BLOCKED、UNVERIFIED、NOT_RUN、coverage gap 均不会被汇总为 PASSED。
- ⬜ 验证旧入口参数兼容，并且旧脚本仍可执行。
- ⬜ 对照 `git diff --check` 检查本任务新增/修改内容；保留工作区中既有的无关修改与未跟踪文件。
