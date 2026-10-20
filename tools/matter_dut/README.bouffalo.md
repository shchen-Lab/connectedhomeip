# Matter DUT 测试使用指南

本指南介绍仓库中用于手动验证 Matter DUT 的脚本入口。默认快捷用例以 lighting 示例为基准；也可以选择 `src/python_testing/` 或 `src/app/tests/suites/` 下其他用例，并提供匹配 DUT 能力的 PICS 文件。测试脚本不负责构建、启动或烧录 DUT。

## 脚本地图

| 脚本 | 用途 |
| --- | --- |
| `tools/matter_dut/bouffalo_sanity_tests.sh` | Python `python_testing` 快捷入口；16 个预设用例，也可直接指定其他 Python 测试脚本。 |
| `tools/matter_dut/bouffalo_yaml_tests.sh` | YAML/chip-tool 快捷入口；33 个预设用例，也可指定 `src/app/tests/suites/` 中的 YAML 文件。 |
| `tools/matter_dut/bouffalo_test_common.sh` | 上述两个 shell 入口共用的参数和目标配置；通常不直接运行。 |
| `tools/matter_dut/matter_dut_tests.py` | 通用目录入口，提供 case 列表、inventory、plan、结构化报告；目录未登记的 case 会转交原有快捷入口。 |
| `tools/matter_dut/matter_dut/` | 通用入口的 catalog、发现、规划、backend 和报告实现；通常不直接运行。 |

仓库里的 `run_test_suite.py`、`all_tests.sh`、`gn_tests.sh` 等属于 SDK/CI 测试 harness，不是本文介绍的 DUT 快捷入口。

## 准备

在仓库根目录执行一次 Python 测试环境准备：

```sh
source scripts/activate.sh
./scripts/build_python.sh -i out/python_env
```

YAML 测试还需要可执行的 `chip-tool`。默认位置是仓库根目录下的 `SOME-PATH/chip-tool`，也可以通过 `CHIP_TOOL` 指定其他构建产物。

运行测试前，先按对应 example 的说明构建并启动 DUT。`--commission` 会配网并把管理员凭证保存在当前 runner 的 storage 中，不会烧录固件或恢复出厂设置。

Python runner 和 YAML runner 的 storage/fabric 相互独立。只使用一个 runner 时，只需配对一次；切换 runner 时，必须使用目标 runner 自己的 storage 配对。若 DUT 已在另一个 fabric 中，先让现有管理员打开 commissioning window，再运行目标 runner 的 `--commission`。

快速操作：

```sh
# 查看各入口的快捷用例清单，不连接 DUT
./tools/matter_dut/bouffalo_sanity_tests.sh --list
./tools/matter_dut/bouffalo_yaml_tests.sh --list

# DUT 刚恢复出厂时，以下两条命令二选一，不要连续运行
./tools/matter_dut/bouffalo_sanity_tests.sh --commission TC_CC_2_1
# 或：YAML 首次配网
./tools/matter_dut/bouffalo_yaml_tests.sh --commission OO

# 已用对应 runner 配对后，只运行测试，不要重复配网
./tools/matter_dut/bouffalo_sanity_tests.sh TC_CC_2_1
./tools/matter_dut/bouffalo_yaml_tests.sh OO
```

默认 `NODE_ID` 为 `0x12344321`。Python 和 YAML 都会使用它；不需要从设备读取 node ID。可通过 `NODE_ID=0x...` 覆盖。

## Python 用例

脚本：`tools/matter_dut/bouffalo_sanity_tests.sh`

不带参数时运行内置的 16 个快捷用例。指定一个或多个测试名可只运行这些用例，名称可省略 `TC_` 前缀：

```sh
./tools/matter_dut/bouffalo_sanity_tests.sh
./tools/matter_dut/bouffalo_sanity_tests.sh TC_CC_2_1 TC_ACL_2_2
./tools/matter_dut/bouffalo_sanity_tests.sh --commission TC_DA_1_5
./tools/matter_dut/bouffalo_sanity_tests.sh --list
```

也可以运行 `src/python_testing/` 下的其他测试脚本。非快捷清单中的用例如需额外参数，可将参数写在测试名后的冒号中：

```sh
./tools/matter_dut/bouffalo_sanity_tests.sh CADMIN_1_9
./tools/matter_dut/bouffalo_sanity_tests.sh 'CNET_4_3:--endpoint 0'
```

快捷清单已有的用例会沿用脚本预设参数（例如 endpoint）；显式提供冒号参数时会覆盖该用例的预设参数。

`--list` 只显示维护过的快捷集合，不会枚举 `src/python_testing/` 下的全部脚本。查看所有 Python 测试文件：

```sh
rg --files src/python_testing -g 'TC_*.py'
```

Python storage 路径必须在配网和后续测试时保持一致。若之前用 `matter_dut/dut_storage.json` 配网，快捷入口也要显式指定它：

```sh
STORAGE_PATH=matter_dut/dut_storage.json \
  ./tools/matter_dut/bouffalo_sanity_tests.sh TC_CC_2_1
```

## YAML 用例

脚本：`tools/matter_dut/bouffalo_yaml_tests.sh`

不带用例参数时运行内置快捷集合，包含当前可自动执行的 OnOff、LevelControl、ColorControl、BasicInformation、Descriptor 和 Groups 用例。该集合按当前 lighting DUT 的能力筛选，不代表 YAML runner 只能测试 lighting。

可以按快捷用例全名或子串筛选，也可以指定 `src/app/tests/suites/` 中其他 YAML 用例名：

```sh
./tools/matter_dut/bouffalo_yaml_tests.sh --list
./tools/matter_dut/bouffalo_yaml_tests.sh OO
./tools/matter_dut/bouffalo_yaml_tests.sh CC_5
./tools/matter_dut/bouffalo_yaml_tests.sh Test_TC_OO_2_1
./tools/matter_dut/bouffalo_yaml_tests.sh Test_TC_AA_1_1
```

YAML 文件名可写为 `TC_AA_1_1` 或 `Test_TC_AA_1_1`；脚本会在 suites 目录中查找对应的 YAML 文件。若参数匹配快捷清单中的多个名称，会运行所有匹配项。

`--list` 只显示当前快捷集合，不代表 YAML runner 只支持这些用例。查看仓库中的 YAML 文件：

```sh
rg --files src/app/tests/suites -g '*.yaml'
```

其他 YAML 文件能否用于当前 DUT，还取决于 PICS、用例前置条件、设备角色和 runner 支持的步骤。用例文件存在不等于该用例适用于设备。

## 选项

两个脚本都接受以下命令行选项：

| 参数 | 说明 |
| --- | --- |
| `--commission` | 使用当前 runner 的持久化存储将 DUT 配对到 Matter fabric，然后执行测试。配网方式由 `COMMISSION_TRANSPORT` 决定；不传时假定 DUT 已与该 runner 的存储配对。此选项不会烧录固件或恢复出厂设置。 |
| `--help`、`-h` | 显示脚本用法。 |

Python 脚本将其余参数解释为测试名；YAML 脚本将其余参数解释为用例名或快捷集合过滤关键字。

## 环境变量

未特别注明时，变量同时适用于两个脚本。

| 变量 | 默认值 | 用途 |
| --- | --- | --- |
| `BOUFFALO_TARGET` | `bl616dk` | 目标配置名，用于选择默认配网传输和隔离存储目录。支持 `bl602dk`、`bl706dk`、`bl704ldk`、`bl616dk`、`bl616cldk`、`linux-x64-light`。它不会构建、启动或校验 DUT 固件型号。 |
| `COMMISSION_TRANSPORT` | 按目标选择 | 配网传输：`wifi`、`thread` 或 `ethernet`。BL602DK、BL706DK、BL616DK、BL616CLDK 默认 `wifi`；BL704LDK 默认 `thread`；`linux-x64-light` 默认 `ethernet`。 |
| `PICS_FILE` | `src/app/tests/suites/certification/ci-pics-values` | DUT 的 PICS 文件。应选择与 DUT 实际实现能力匹配的 PICS。 |
| `NODE_ID` | `0x12344321` | DUT 的 node ID。YAML runner 会将十进制或 `0x` 十六进制值转为十进制传给测试工具。 |
| `DISCRIMINATOR` | `3840` | BLE 配网使用的 discriminator。 |
| `PASSCODE` | `20202021` | 配网用 setup passcode。 |
| `WIFI_SSID` | `BF-OTA-AP` | Wi-Fi 配网使用的网络名称。 |
| `WIFI_PASSPHRASE` | `12345678` | Wi-Fi 配网使用的密码。 |
| `THREAD_DATASET` | 空 | Thread 配网用 operational dataset。使用 `--commission` 且 `COMMISSION_TRANSPORT=thread` 时必填；可带或不带 `hex:` 前缀。 |
| `TEST_TIMEOUT` | `300` | 每个测试用例的超时秒数。 |
| `FIRMWARE_PATH` | 空 | 可选的固件文件路径。文件存在时，汇总报告会记录路径和 SHA-256；它不会用于烧录或识别 DUT 当前运行的固件。 |
| `RESULTS_ROOT` | `$CHIP_ROOT/test_results` | 测试记录的根目录。 |
| `LOG_DIR` | 按 runner 和启动时间生成 | 本次运行的日志目录。Python 默认在 `test_results/bouffalo_sanity/<日期_时间>/`，YAML 默认在 `test_results/bouffalo_yaml/<日期_时间>/`。设置后可覆盖默认位置。 |

以下变量仅适用于对应脚本：

| 变量 | 适用脚本 | 默认值 | 用途 |
| --- | --- | --- | --- |
| `STORAGE_PATH` | Python | `.tmp/bouffalo/$BOUFFALO_TARGET/python_storage.json` | Python Matter controller 的持久化存储路径。 |
| `CHIP_TOOL` | YAML | `SOME-PATH/chip-tool` | 要运行的 `chip-tool` 可执行文件路径。 |
| `CHIP_TOOL_STORAGE_DIR` | YAML | `.tmp/bouffalo/$BOUFFALO_TARGET/chip_tool` | YAML runner 使用的 `chip-tool` 持久化存储目录。 |
| `PYENV` | 两者 | `out/python_env` | 使用已有 Python 测试虚拟环境时覆盖环境目录。 |

`$CHIP_ROOT` 表示仓库根目录。相对路径变量按脚本启动时的当前工作目录解释；上表中的默认存储和工具路径会由脚本解析为仓库内路径。

## 运行示例

指定开发板、PICS 和固件文件，执行一个 Python 用例：

```sh
BOUFFALO_TARGET=bl616dk \
PICS_FILE=/path/to/device.pics \
FIRMWARE_PATH=/path/to/app.bin \
./tools/matter_dut/bouffalo_sanity_tests.sh CC_2_1
```

`linux-x64-light` 是脚本当前提供的 Linux 主机连接配置名，不会强制要求 DUT 是 lighting example。测试哪些功能由所选用例和 PICS 决定。

## 配网与切换 runner

`--commission` 会使用 `BOUFFALO_TARGET` 对应的默认传输，也可以用 `COMMISSION_TRANSPORT` 覆盖。Wi-Fi 和 Thread 都通过 BLE 进行配对；Ethernet 使用 on-network 配对，DUT 必须已在可达的局域网中运行。

刚恢复出厂、正在等待首次配网的 DUT 可以直接使用目标 runner 的 `--commission`。如果设备已由 Python runner 配对，再让 YAML runner 配对，则需先由 Python fabric 的管理员打开窗口。可用同一份 Python storage 启动 Matter REPL：

```sh
out/python_env/bin/matter-repl --storage-path matter_dut/dut_storage.json
```

在 REPL 中执行以下命令。窗口使用设备原始 setup code，默认 15 分钟后关闭：

```python
params = await devCtrl.OpenCommissioningWindow(
    nodeId=0x12344321,
    timeout=900,
    iteration=1000,
    discriminator=3840,
    option=devCtrl.CommissioningWindowPasscode.kOriginalSetupCode)
params
```

看到命令成功后退出 REPL，马上用 YAML runner 配网。`--commission` 首次创建 chip-tool storage 并保存 YAML fabric：

```sh
./tools/matter_dut/bouffalo_yaml_tests.sh --commission OO
```

反向从 YAML runner 切换到 Python runner 时，用现有 chip-tool storage 打开窗口，然后对 Python runner 使用 `--commission`：

```sh
SOME-PATH/chip-tool pairing open-commissioning-window \
  0x12344321 0 900 1000 3840 \
  --storage-directory .tmp/bouffalo/bl616dk/chip_tool
./tools/matter_dut/bouffalo_sanity_tests.sh --commission TC_CC_2_1
```

如果 DUT 尚未加入任何 fabric，不需要打开窗口，直接执行对应的 `--commission` 即可。不要把 Python JSON storage 传给 chip-tool；两者格式和 fabric 不兼容。

Wi-Fi 开发板：填写 DUT 将要连接的 AP 信息。`bl616dk` 默认使用 Wi-Fi：

```sh
BOUFFALO_TARGET=bl616dk \
WIFI_SSID=my-ap \
WIFI_PASSPHRASE=my-password \
./tools/matter_dut/bouffalo_sanity_tests.sh --commission TC_CC_2_1
```

Thread 开发板：提供 Thread operational dataset。`bl704ldk` 默认使用 Thread：

```sh
BOUFFALO_TARGET=bl704ldk \
THREAD_DATASET='hex:<dataset-hex>' \
./tools/matter_dut/bouffalo_yaml_tests.sh --commission Test_TC_OO_2_1
```

Linux 主机 DUT：先启动对应 example 并确保主机和测试端在同一可达网络，再使用 Ethernet on-network 配对。YAML runner 需要额外指定 `chip-tool`：

```sh
BOUFFALO_TARGET=linux-x64-light \
CHIP_TOOL=/path/to/chip-tool \
./tools/matter_dut/bouffalo_yaml_tests.sh --commission Test_TC_OO_2_1
```

如 DUT 已与对应 runner 配对，直接运行测试即可，不要加 `--commission`：

```sh
./tools/matter_dut/bouffalo_sanity_tests.sh TC_CC_2_1
./tools/matter_dut/bouffalo_yaml_tests.sh Test_TC_OO_2_1
```

## 日志与退出状态

用例开始执行后，每个用例都有单独日志，运行目录中还有 `summary.txt` 汇总报告。可通过 `LOG_DIR` 指定目录，`RESULTS_ROOT` 指定默认归档根目录。

两个脚本成功时返回 `0`；有测试失败、跳过或未执行时返回非零。无效目标、传输方式或必需参数也会以非零状态退出。检查 `summary.txt` 和对应的单用例日志可定位具体结果。

Python runner 与 YAML runner 的 fabric 不共享。部分测试（例如 ACL、操作凭据相关测试）会修改 DUT 状态；是否恢复出厂取决于测试和开发板使用方式。

## 常见问题

| 现象 | 含义和处理 |
| --- | --- |
| `找不到 chip-tool 存储` | YAML storage 还没有创建。刚恢复出厂时直接加 `--commission`；已加入其他 fabric 时，先由当前管理员开 commissioning window。 |
| `Init Storage failure` / `Connecting to ws://localhost:9002 failed` | 旧版本可能在 storage 目录不存在时启动 chip-tool 失败。更新脚本后首次配对用 `--commission`；检查 `commission.log` 和 `CHIP_TOOL_STORAGE_DIR`。 |
| Python Controller 初始化失败或提示找不到节点 | 确认 `STORAGE_PATH` 指向配对成功时使用的文件，并检查 `NODE_ID` 是否与配对时一致。 |
| 用例 `SKIP` 或整体 `INCOMPLETE` | PICS 条件未满足或用例没有执行步骤。检查实际设备 PICS、单用例日志；不要把跳过当作通过。 |
| `--list` 没看到某个测试 | `--list` 只列快捷集合。Python 用例在 `src/python_testing/`，YAML 用例在 `src/app/tests/suites/`；可按上文指定文件名运行。 |
