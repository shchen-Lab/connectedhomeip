# Matter DUT 测试脚本

手动测试 DUT 时，优先使用已有的 Python 或 YAML shell runner。它们保留原有快捷用例；通用 catalog CLI 用于 case 检查、规划和结构化报告。

## 快速开始

在仓库根目录准备 Python 测试环境：

```sh
source scripts/activate.sh
./scripts/build_python.sh -i out/python_env
```

按对应 example 的说明构建并启动 DUT。设备恢复出厂后，选择一个 runner 首次配网。Python runner：

```sh
./tools/matter_dut/bouffalo_sanity_tests.sh --commission TC_CC_2_1
```

或者使用 YAML runner（需要 `SOME-PATH/chip-tool`）：

```sh
./tools/matter_dut/bouffalo_yaml_tests.sh --commission OO
```

配网成功后，继续使用同一个 runner 测试，不要加 `--commission`。Python：

```sh
./tools/matter_dut/bouffalo_sanity_tests.sh TC_CC_2_1
```

YAML：

```sh
./tools/matter_dut/bouffalo_yaml_tests.sh OO
```

Python 和 YAML runner 使用独立的 storage 和 Matter fabric。已配网的设备要切换 runner 时，先由当前 fabric 的管理员打开 commissioning window，再使用另一个 runner 配网。默认 node ID 是 `0x12344321`。

## 文档

- [DUT 使用指南](README.bouffalo.md)：环境变量、用例选择、storage、配网、日志和常见问题。
- [通用 catalog CLI](README.matter-dut.md)：`matter_dut_tests.py`、inventory、规划、dry-run 和结构化报告。

shell runner 的 `--list` 只显示维护过的快捷用例，不会列出仓库中的全部测试文件。使用指南说明了如何查找和运行其他 Python、YAML 用例。
