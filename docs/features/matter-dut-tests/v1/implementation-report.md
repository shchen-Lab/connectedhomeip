---
module: matter-dut-tests
version: v1
date: 2026-10-20
status: in-progress
tags: [implementation, matter, testing]
---

# Matter DUT 通用测试框架实现记录

## 已实现

- 公共 inventory 模型、JSON 转换及 Descriptor 拓扑合并，未知值保留为 `unknown`。
- JSON 用例目录校验、PICS 解析、角色/能力规划、动态 endpoint 实例化、场景 fixture 阻塞和 PICS 冲突记录。
- 报告结果补全、整体状态归并、coverage gaps，以及原子写入 inventory 和 JSON/TXT 报告。
- 通用 CLI 的 `--check`、`--list`、`--discover`、`--plan`、`--dry-run` 和默认运行入口。
- Python backend 的 Matter stack 属性发现和 Python 测试脚本执行。
- YAML backend 沿用现有 WebSocket runner 执行 YAML 用例。
- Python/YAML 各自读取独立 storage 路径，并在离线 inventory 规划时核对 backend、node ID、storage 和 fabric 引用。
- 通用使用文档和一个限定为 root node 必备 cluster 的 Python basic composition smoke 条目；未把旧快捷清单整体登记为自动用例。
- 原 Python/YAML shell 入口继续保留原快捷执行逻辑；新增 `--list`，通用 CLI 的 `--list` 合并展示旧快捷用例，目录外 case 名称转发给原 runner。

## 尚未完成

- YAML WebSocket adapter 尚未提供结构化 Matter Read 响应；YAML 在线 discover/plan/run 需要在线发现时返回 `BACKEND_UNAVAILABLE`。
- `--commission` 已提供参数映射，但未在真实设备上验证 Python hello_test 与 chip-tool 两条配网路径。
- 原快捷入口仍未接入 capability planner 和结构化 JSON 报告；旧 case 通过兼容转发执行时仍使用原 runner 的适用规则、日志和汇总格式。
- Python Matter 响应的部分读取错误状态映射仍需用实际设备/SDK 响应核对；整次 Read 失败、解码失败和缺失的 Descriptor 属性会汇总为 `ReadIssue`。
- 中断后写入 `INTERRUPTED` 报告、更多 catalog 用例登记、兼容包装器和完整设计验收尚未完成。

## 验证

- `.environment/pigweed-venv/bin/python3 -m unittest discover -s tools/matter_dut/matter_dut -p 'test_*.py' -v`：9 项通过。
- Bash 入口语法检查、Python CLI AST 解析和 `git diff --check`：通过。
- `matter_dut_tests.py --backend python --list`：列出 1 条 catalog 用例及 16 条旧快捷用例。
- `matter_dut_tests.py --backend yaml --list`：列出 33 条旧快捷用例；未登记 YAML case 数为 0。
- 使用临时离线 inventory 执行 Python `--plan`：root-node basic composition 用例规划为 `READY`。
- 未连接 DUT；本次只验证列表和静态语法，旧 case 转发的设备执行仍未验证。
