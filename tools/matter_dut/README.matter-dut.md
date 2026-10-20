# Matter DUT 通用测试入口

`tools/matter_dut/matter_dut_tests.py` 是通用入口，提供目录 case 的检查、发现、规划和结构化报告。快速 DUT 冒烟和回归可直接运行本目录的 shell runner。第一次使用说明见 [README.md](README.md)，DUT 操作细节见 [README.bouffalo.md](README.bouffalo.md)。

`--list` 同时显示 catalog 已登记的 case 和旧入口快捷集合；它不枚举仓库中的所有 Python/YAML 文件。运行 catalog 外的 case 时，新入口会转交对应旧 runner，因此原有快捷 case 和测试名仍然可用。Python 与 YAML 的 storage/fabric 相互独立；新入口默认 storage 路径也与旧入口默认路径不同，复用现有配对时要明确传入对应 storage 参数。

Python discovery 需要通过已配置 Matter Python SDK 的解释器运行 CLI；`--python-executable` 选择用例子进程解释器。当前 catalog 只登记了一个 Python 示例用例；其他未登记 Python 文件仍可直接通过旧入口运行，也可用 `--script` 作为显式未核实用例运行。

```sh
out/python_env/bin/python3 tools/matter_dut/matter_dut_tests.py --backend python --check
out/python_env/bin/python3 tools/matter_dut/matter_dut_tests.py --backend yaml --list
out/python_env/bin/python3 tools/matter_dut/matter_dut_tests.py --backend python --discover --storage-path matter_dut/dut_storage.json --node-id 0x12344321
out/python_env/bin/python3 tools/matter_dut/matter_dut_tests.py --backend python --plan --storage-path matter_dut/dut_storage.json --inventory test_results/matter_dut/inventory-python-12344321.json
out/python_env/bin/python3 tools/matter_dut/matter_dut_tests.py --backend python --dry-run --storage-path matter_dut/dut_storage.json --inventory inventory.json --case python.root-node-basic-composition
out/python_env/bin/python3 tools/matter_dut/matter_dut_tests.py --backend python --case MY_CASE --script src/python_testing/TC_EXAMPLE.py --case-arg=--endpoint --case-arg=2
out/python_env/bin/python3 tools/matter_dut/matter_dut_tests.py --backend python --storage-path matter_dut/dut_storage.json --case TC_CC_2_1
out/python_env/bin/python3 tools/matter_dut/matter_dut_tests.py --backend yaml --case OO --storage-directory .tmp/bouffalo/bl616dk/chip_tool
```

`--check` 和 `--list` 不连接设备。`--discover` 保存结构化 inventory；`--plan` 和 `--dry-run` 不启动用例。可用 `--inventory` 离线规划。v1 目录登记了 Python root node basic composition 用例；其他未登记脚本必须显式同时提供 `--case` 报告 ID 与 `--script`，其计划状态为 `UNVERIFIED`。

未登记的旧 case 转发示例：`--case TC_CC_2_1` 会调用 `bouffalo_sanity_tests.sh`，`--case OO` 会调用 `bouffalo_yaml_tests.sh`。传给旧 runner 的参数包括显式 storage、PICS、node ID、timeout 和 commissioning 参数。不要混合 catalog case 和旧 case 放在同一次命令里。

Inventory 保存 Descriptor endpoint、DeviceType、PartsList、cluster 角色和可读取的全局能力属性。未知能力保留为 unknown，并阻塞依赖该能力的计划项。PICS 文件作为声明单独保存；显式写成 `cluster:<hex-id>:server` 或 `cluster:<hex-id>:client` 的键可与发现结果比较，冲突进入报告。

报告包含版本化 `run-report.json`、`plan.json`、`summary.json` 和文本投影。执行结果、验证问题和 coverage gaps 分开记录；未知能力、阻塞项、未核实项或未执行项不会汇总为通过。

YAML WebSocket runner 当前没有暴露结构化 Matter Read payload，因此 YAML 用例可以沿现有 runner 执行，但 `--discover` 和需要在线发现的计划会返回 `BACKEND_UNAVAILABLE`。通用 CLI 的 `--commission` 要同时指定 `--commissioning-method ble-wifi|ble-thread|on-network`；Wi-Fi/Thread 还需提供对应凭据。旧 Bouffalo YAML runner 的 chip-tool 配网与 `OO` 快捷集合已在 BL616DK 上实机通过；通用 CLI 的 catalog commissioning/discovery 流程不等同于该实机验证。
