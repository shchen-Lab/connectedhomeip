# Bouffalo Matter 测试脚本

本文介绍 `bouffalo_sanity_tests.sh` 和 `bouffalo_yaml_tests.sh`。两个脚本面向 Matter DUT，可在 Bouffalo 开发板或 Linux 主机上运行；它们不负责构建、启动或烧录 DUT。默认用例集合是为当前 lighting 示例整理的快捷集合，但 DUT 不限于 lighting：可以指定仓库中其他 Python 或 YAML 用例，并用匹配的 PICS 文件描述设备能力。

## 准备

在仓库根目录执行一次 Python 测试环境准备：

```sh
source scripts/activate.sh
./scripts/build_python.sh -i out/python_env
```

YAML 测试还需要可执行的 `chip-tool`。默认位置是仓库根目录下的 `SOME-PATH/chip-tool`，也可以通过 `CHIP_TOOL` 指定其他构建产物。

运行测试前，先按对应 example 的说明构建并启动 DUT。省略 `--commission` 时，DUT 必须已与当前 runner 的存储配对。Python runner 和 YAML runner 使用不同的 Matter 存储，因此各自需要配对；若 DUT 已与一个 runner 配对，要再配对另一个 runner，先由现有管理员在 DUT 上打开 commissioning window，再执行另一个 runner 的 `--commission`。

## Python 用例

脚本：`scripts/tests/bouffalo_sanity_tests.sh`

不带参数时运行内置的 16 个快捷用例。指定一个或多个测试名可只运行这些用例，名称可省略 `TC_` 前缀：

```sh
./scripts/tests/bouffalo_sanity_tests.sh
./scripts/tests/bouffalo_sanity_tests.sh TC_CC_2_1 TC_ACL_2_2
./scripts/tests/bouffalo_sanity_tests.sh --commission TC_DA_1_5
```

也可以运行 `src/python_testing/` 下的其他测试脚本。非快捷清单中的用例如需额外参数，可将参数写在测试名后的冒号中：

```sh
./scripts/tests/bouffalo_sanity_tests.sh CADMIN_1_9
./scripts/tests/bouffalo_sanity_tests.sh 'CNET_4_3:--endpoint 0'
```

快捷清单已有的用例会沿用脚本预设参数（例如 endpoint）；显式提供冒号参数时会覆盖该用例的预设参数。

## YAML 用例

脚本：`scripts/tests/bouffalo_yaml_tests.sh`

不带用例参数时运行内置快捷集合，包含当前可自动执行的 OnOff、LevelControl、ColorControl、BasicInformation、Descriptor 和 Groups 用例。该集合按当前 lighting DUT 的能力筛选，不代表 YAML runner 只能测试 lighting。

可以按快捷用例全名或子串筛选，也可以指定 `src/app/tests/suites/` 中其他 YAML 用例名：

```sh
./scripts/tests/bouffalo_yaml_tests.sh OO
./scripts/tests/bouffalo_yaml_tests.sh CC_5
./scripts/tests/bouffalo_yaml_tests.sh Test_TC_OO_2_1
./scripts/tests/bouffalo_yaml_tests.sh Test_TC_AA_1_1
```

YAML 文件名可写为 `TC_AA_1_1` 或 `Test_TC_AA_1_1`；脚本会在 suites 目录中查找对应的 YAML 文件。若参数匹配快捷清单中的多个名称，会运行所有匹配项。

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

`$CHIP_ROOT` 表示仓库根目录。相对路径变量按脚本启动时的当前工作目录解释；上表中的默认存储和工具路径会由脚本解析为仓库内路径。

## 运行示例

指定开发板、PICS 和固件文件，执行一个 Python 用例：

```sh
BOUFFALO_TARGET=bl616dk \
PICS_FILE=/path/to/device.pics \
FIRMWARE_PATH=/path/to/app.bin \
./scripts/tests/bouffalo_sanity_tests.sh CC_2_1
```

`linux-x64-light` 是脚本当前提供的 Linux 主机连接配置名，不会强制要求 DUT 是 lighting example。测试哪些功能由所选用例和 PICS 决定。

## 配网示例

`--commission` 会使用 `BOUFFALO_TARGET` 对应的默认传输，也可以用 `COMMISSION_TRANSPORT` 覆盖。Wi-Fi 和 Thread 都通过 BLE 进行配对；Ethernet 使用 on-network 配对，DUT 必须已在可达的局域网中运行。

首次配对时，DUT 需要处于可配对状态。若它已加入其他 Matter fabric，单纯再次运行 `--commission` 不会自动打开 commissioning window；请先通过已有管理员打开窗口。两个 runner 的 fabric 和持久化存储互不共享。

Wi-Fi 开发板：填写 DUT 将要连接的 AP 信息。`bl616dk` 默认使用 Wi-Fi：

```sh
BOUFFALO_TARGET=bl616dk \
WIFI_SSID=my-ap \
WIFI_PASSPHRASE=my-password \
./scripts/tests/bouffalo_sanity_tests.sh --commission TC_CC_2_1
```

Thread 开发板：提供 Thread operational dataset。`bl704ldk` 默认使用 Thread：

```sh
BOUFFALO_TARGET=bl704ldk \
THREAD_DATASET='hex:<dataset-hex>' \
./scripts/tests/bouffalo_yaml_tests.sh --commission Test_TC_OO_2_1
```

Linux 主机 DUT：先启动对应 example 并确保主机和测试端在同一可达网络，再使用 Ethernet on-network 配对。YAML runner 需要额外指定 `chip-tool`：

```sh
BOUFFALO_TARGET=linux-x64-light \
CHIP_TOOL=/path/to/chip-tool \
./scripts/tests/bouffalo_yaml_tests.sh --commission Test_TC_OO_2_1
```

如 DUT 已与对应 runner 配对，直接运行测试即可，不要加 `--commission`：

```sh
./scripts/tests/bouffalo_sanity_tests.sh TC_CC_2_1
./scripts/tests/bouffalo_yaml_tests.sh Test_TC_OO_2_1
```

## 日志与退出状态

用例开始执行后，每个用例都有单独日志，运行目录中还有 `summary.txt` 汇总报告。可通过 `LOG_DIR` 指定目录，`RESULTS_ROOT` 指定默认归档根目录。

两个脚本成功时返回 `0`；有测试失败、跳过或未执行时返回非零。无效目标、传输方式或必需参数也会以非零状态退出。检查 `summary.txt` 和对应的单用例日志可定位具体结果。

Python runner 与 YAML runner 的 fabric 不共享。部分测试（例如 ACL、操作凭据相关测试）会修改 DUT 状态；是否恢复出厂取决于测试和开发板使用方式。
