#!/usr/bin/env python3
"""Generic entry point for Matter DUT discovery, planning, and test execution."""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from types import SimpleNamespace

REPO_ROOT = Path(__file__).resolve().parents[3]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from .backends import (
    BackendError,
    PythonBackend,
    YamlBackend,
    read_inventory_file,
)
from .catalog import (
    CatalogError,
    Selection,
    build_plan,
    load_catalog,
    parse_pics,
)
from .inventory import BackendContext, inventory_to_dict
from .report import (
    ReportWriteError,
    build_run_report,
    write_inventory,
    write_report,
)


TOOL_ROOT = REPO_ROOT / "tools/matter_dut"
DEFAULT_CATALOG = Path(__file__).with_name("catalog.json")


def _node_id(value: str) -> int:
    try:
        parsed = int(value, 0)
    except ValueError:
        try:
            parsed = int(value, 10)
        except ValueError:
            raise argparse.ArgumentTypeError("node ID must be decimal or 0x-prefixed hexadecimal") from None
    if not 0 <= parsed < (1 << 64):
        raise argparse.ArgumentTypeError("node ID must fit in uint64")
    return parsed


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="check static runner prerequisites")
    mode.add_argument("--list", action="store_true", help="list registered cases without contacting a DUT")
    mode.add_argument("--discover", action="store_true", help="discover and save a DUT inventory")
    mode.add_argument("--plan", action="store_true", help="create and save a plan without executing cases")
    mode.add_argument("--dry-run", action="store_true", help="show a plan and command arguments")
    parser.add_argument("--backend", choices=("python", "yaml"), required=True)
    parser.add_argument("--node-id", type=_node_id, default=0x12344321)
    parser.add_argument("--dut-role", choices=("server", "client", "commissioner", "mixed"), default="server")
    parser.add_argument("--suite", choices=("smoke", "functional", "stateful"), default="smoke")
    parser.add_argument("--case", dest="case_ids", action="append", default=[])
    parser.add_argument("--script", help="explicit unregistered script; requires one --case report ID")
    parser.add_argument("--case-arg", action="append", default=[], help="argument for an explicit script")
    parser.add_argument("--catalog", default=str(DEFAULT_CATALOG))
    parser.add_argument("--pics")
    parser.add_argument("--pics-scope", choices=("node", "endpoint"), default="node")
    parser.add_argument("--inventory", help="offline inventory JSON for planning")
    parser.add_argument("--output-dir", default="test_results/matter_dut")
    parser.add_argument("--storage-path")
    parser.add_argument("--storage-directory")
    parser.add_argument("--chip-tool", default="SOME-PATH/chip-tool")
    parser.add_argument("--python-executable", default=sys.executable)
    parser.add_argument("--timeout", type=int)
    parser.add_argument("--commission", action="store_true")
    parser.add_argument("--commissioning-method", choices=("ble-wifi", "ble-thread", "on-network"))
    parser.add_argument("--wifi-ssid")
    parser.add_argument("--wifi-passphrase")
    parser.add_argument("--thread-dataset-hex")
    parser.add_argument("--discriminator", type=int, default=3840)
    parser.add_argument("--passcode", type=int, default=20202021)
    args = parser.parse_args(argv)
    args.mode = ("check" if args.check else "list" if args.list else "discover" if args.discover
                 else "plan" if args.plan else "dry_run" if args.dry_run else "run")
    if args.inventory and args.discover:
        parser.error("--inventory cannot be combined with --discover")
    if args.script and len(args.case_ids) != 1:
        parser.error("--script requires exactly one --case report ID")
    if args.case_arg and not args.script:
        parser.error("--case-arg requires --script")
    if args.commission and not args.commissioning_method:
        parser.error("--commission requires --commissioning-method")
    if args.commission and (args.check or args.list):
        parser.error("--commission requires a DUT operation mode")
    if args.timeout is not None and args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def _repo_path(value: str | Path) -> Path:
    path = Path(value)
    return path if path.is_absolute() else REPO_ROOT / path


def _context(args: argparse.Namespace) -> BackendContext:
    if args.backend == "python":
        storage = _repo_path(args.storage_path or ".tmp/matter_dut/python_storage.json")
        storage_ref = str(storage)
        fabric_ref = f"python:{storage_ref}"
    else:
        storage = _repo_path(args.storage_directory or ".tmp/matter_dut/chip_tool")
        storage_ref = str(storage)
        fabric_ref = f"yaml:{storage_ref}"
    return BackendContext(args.backend, args.node_id, args.dut_role, storage_ref, fabric_ref)


def _backend(args: argparse.Namespace, context: BackendContext):
    if args.backend == "python":
        return PythonBackend(args.python_executable)
    chip_tool = _repo_path(args.chip_tool)
    return YamlBackend(str(chip_tool), context.storage_ref)


def _legacy_runner(backend: str) -> Path:
    name = "bouffalo_sanity_tests.sh" if backend == "python" else "bouffalo_yaml_tests.sh"
    return TOOL_ROOT / name


def _check(args: argparse.Namespace, cases: list) -> int:
    problems = []
    selected_cases = [case for case in cases if case.backend == args.backend]
    for case in selected_cases:
        script = _repo_path(case.script)
        if not script.is_file():
            problems.append(f"missing case script: {script}")
    if args.backend == "python" and not Path(args.python_executable).is_file():
        problems.append(f"Python executable not found: {args.python_executable}")
    if args.backend == "yaml":
        chip_tool = _repo_path(args.chip_tool)
        if not chip_tool.is_file():
            problems.append(f"chip-tool not found: {chip_tool}")
    if args.pics and not Path(args.pics).is_file():
        problems.append(f"PICS file not found: {args.pics}")
    if problems:
        for problem in problems:
            print(f"ERROR: {problem}", file=sys.stderr)
        return 2
    print(f"OK: backend={args.backend}, catalog cases={len(selected_cases)}")
    return 0


def _run_legacy(args: argparse.Namespace) -> int:
    runner = _legacy_runner(args.backend)
    environment = os.environ.copy()
    environment["NODE_ID"] = str(args.node_id)
    if args.pics:
        environment["PICS_FILE"] = args.pics
    if args.timeout:
        environment["TEST_TIMEOUT"] = str(args.timeout)

    if args.backend == "python":
        if args.storage_path:
            environment["STORAGE_PATH"] = args.storage_path
        python_path = Path(args.python_executable).resolve()
        python_env = python_path.parent.parent
        if (python_env / "bin/activate").is_file():
            environment.setdefault("PYENV", str(python_env))
    else:
        if args.storage_directory:
            environment["CHIP_TOOL_STORAGE_DIR"] = args.storage_directory
        environment["CHIP_TOOL"] = str(_repo_path(args.chip_tool))

    command = ["bash", str(runner)]
    if args.commission:
        command.append("--commission")
        environment["DISCRIMINATOR"] = str(args.discriminator)
        environment["PASSCODE"] = str(args.passcode)
        methods = {"ble-wifi": "wifi", "ble-thread": "thread", "on-network": "ethernet"}
        environment["COMMISSION_TRANSPORT"] = methods[args.commissioning_method]
        if args.wifi_ssid:
            environment["WIFI_SSID"] = args.wifi_ssid
        if args.wifi_passphrase is not None:
            environment["WIFI_PASSPHRASE"] = args.wifi_passphrase
        if args.thread_dataset_hex:
            environment["THREAD_DATASET"] = args.thread_dataset_hex
    command.extend(args.case_ids)
    try:
        return subprocess.run(command, cwd=REPO_ROOT, env=environment, check=False).returncode
    except OSError as error:
        print(f"LEGACY_RUNNER_FAILED: cannot start {runner}: {error}", file=sys.stderr)
        return 2


def _list_legacy_cases(backend: str) -> int:
    runner = _legacy_runner(backend)
    try:
        completed = subprocess.run(["bash", str(runner), "--list"], cwd=REPO_ROOT,
                                   text=True, capture_output=True, check=False)
    except OSError as error:
        print(f"LEGACY_LIST_FAILED: cannot start {runner}: {error}", file=sys.stderr)
        return 2
    if completed.returncode:
        print(completed.stderr, end="", file=sys.stderr)
        return completed.returncode
    legacy_cases = completed.stdout.splitlines()
    print(f"Legacy quick cases ({len(legacy_cases)}):")
    print(completed.stdout, end="")
    return 0


def _commission(args: argparse.Namespace, context: BackendContext, output_dir: Path) -> None:
    method = args.commissioning_method
    log_path = output_dir / "commission.log"
    if args.backend == "python":
        command = [args.python_executable, str(REPO_ROOT / "src/python_testing/hello_test.py"),
                   "--storage-path", context.storage_ref, "--commission-only",
                   "--commissioning-method", method, "--discriminator", str(args.discriminator),
                   "--passcode", str(args.passcode), "--dut-node-id", str(context.node_id)]
        if method == "ble-wifi":
            if not args.wifi_ssid or args.wifi_passphrase is None:
                raise BackendError("COMMISSION_ARGUMENTS", "ble-wifi requires --wifi-ssid and --wifi-passphrase")
            command.extend(("--wifi-ssid", args.wifi_ssid, "--wifi-passphrase", args.wifi_passphrase))
        elif method == "ble-thread":
            if not args.thread_dataset_hex:
                raise BackendError("COMMISSION_ARGUMENTS", "ble-thread requires --thread-dataset-hex")
            command.extend(("--thread-dataset-hex", args.thread_dataset_hex.removeprefix("hex:")))
    else:
        command = [args.chip_tool]
        if method == "ble-wifi":
            if not args.wifi_ssid or args.wifi_passphrase is None:
                raise BackendError("COMMISSION_ARGUMENTS", "ble-wifi requires --wifi-ssid and --wifi-passphrase")
            command.extend(("pairing", "ble-wifi", str(context.node_id), args.wifi_ssid,
                            args.wifi_passphrase, str(args.passcode), str(args.discriminator)))
        elif method == "ble-thread":
            if not args.thread_dataset_hex:
                raise BackendError("COMMISSION_ARGUMENTS", "ble-thread requires --thread-dataset-hex")
            command.extend(("pairing", "ble-thread", str(context.node_id),
                            f"hex:{args.thread_dataset_hex.removeprefix('hex:')}",
                            str(args.passcode), str(args.discriminator)))
        else:
            command.extend(("pairing", "onnetwork", str(context.node_id), str(args.passcode)))
        command.extend(("--storage-directory", context.storage_ref))

    output_dir.mkdir(parents=True, exist_ok=True)
    for attempt in range(1, 4):
        try:
            with log_path.open("wb") as log:
                completed = subprocess.run(command, cwd=REPO_ROOT, stdout=log,
                                            stderr=subprocess.STDOUT, timeout=180, check=False)
        except (OSError, subprocess.TimeoutExpired) as error:
            if attempt == 3:
                raise BackendError("COMMISSION_FAILED", f"commissioning failed; see {log_path}: {error}") from error
            time.sleep(3)
            continue
        if completed.returncode == 0:
            return
        if attempt < 3:
            time.sleep(3)
    raise BackendError("COMMISSION_FAILED", f"commissioning failed after 3 attempts; see {log_path}")


def _save_plan(path: Path, planning, context: BackendContext) -> None:
    from .report import _json_value

    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema_version": "1.0",
        "backend_context": _json_value(context),
        "entries": [_json_value(entry) for entry in planning.entries],
        "validation_issues": [_json_value(issue) for issue in planning.validation_issues],
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        cases = load_catalog(args.catalog)
        if args.mode == "check":
            return _check(args, cases)
        if args.mode == "list":
            selected = [case for case in cases if case.backend == args.backend]
            for case in selected:
                print(f"{case.id}\t{case.suite}\t{case.scope}\t{case.script}")
            print(f"{len(selected)} registered {args.backend} cases")
            return _list_legacy_cases(args.backend)

        if args.mode == "run" and args.case_ids and not args.script:
            known_ids = {case.id for case in cases}
            unknown_ids = [case_id for case_id in args.case_ids if case_id not in known_ids]
            if unknown_ids:
                if len(unknown_ids) != len(args.case_ids):
                    raise CatalogError("CASE_UNKNOWN", "registered and legacy case IDs cannot be mixed in one run")
                if args.inventory:
                    raise CatalogError("LEGACY_INVENTORY_UNSUPPORTED",
                                       "legacy cases use their original runner; omit --inventory")
                return _run_legacy(args)

        context = _context(args)
        adapter = _backend(args, context)
        output_dir = Path(args.output_dir)
        if args.commission:
            _commission(args, context, output_dir)
        inventory = read_inventory_file(args.inventory) if args.inventory else None
        pics = parse_pics(args.pics, args.pics_scope) if args.pics else None
        selection = Selection(
            mode=args.mode,
            case_ids=tuple(args.case_ids),
            suite=None if args.case_ids or args.script else args.suite,
            explicit_script=str(Path(args.script).resolve()) if args.script else None,
            arguments=tuple(args.case_arg),
        )
        if args.mode == "discover" or (args.mode in {"plan", "dry_run", "run"} and inventory is None):
            inventory = adapter.discover(context, cases)
        if inventory is not None and inventory.backend_context.backend != args.backend:
            raise CatalogError("CONTEXT_MISMATCH", "inventory backend does not match --backend")
        if inventory is not None and inventory.backend_context.node_id != args.node_id:
            raise CatalogError("CONTEXT_MISMATCH", "inventory node ID does not match --node-id")
        if inventory is not None and inventory.backend_context.storage_ref != context.storage_ref:
            raise CatalogError("CONTEXT_MISMATCH", "inventory storage reference does not match selected backend storage")
        if inventory is not None and inventory.backend_context.fabric_ref != context.fabric_ref:
            raise CatalogError("CONTEXT_MISMATCH", "inventory fabric reference does not match selected backend context")

        if args.mode == "discover":
            output = Path(args.output_dir) / f"inventory-{args.backend}-{args.node_id:x}.json"
            print(write_inventory(inventory_to_dict(inventory), output))
            return 0

        planning = build_plan(inventory, pics, cases, selection, context=context)
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
        run_dir = output_dir / stamp
        run_dir.mkdir(parents=True, exist_ok=True)
        inventory_ref = None
        if args.mode == "run" and inventory is not None:
            inventory_ref = write_inventory(inventory_to_dict(inventory), run_dir / "inventory.json")
        plan_path = run_dir / "plan.json"
        _save_plan(plan_path, planning, context)
        print(f"Plan: {plan_path}")
        for entry in planning.entries:
            endpoint = "" if entry.instance.endpoint_id is None else f" endpoint={entry.instance.endpoint_id}"
            reason = f" ({entry.reason})" if entry.reason else ""
            print(f"{entry.status}: {entry.instance.case_id}{endpoint}{reason}")
        for issue in planning.validation_issues:
            print(f"{issue.severity.upper()} {issue.code}: {issue.message}")
        selected_cases = {case.id: case for case in cases}
        if args.script:
            selected_cases[args.case_ids[0]] = SimpleNamespace(
                id=args.case_ids[0],
                script=str(Path(args.script).resolve()),
                timeout_seconds=args.timeout or 300,
            )
        if args.mode in {"plan", "dry_run"}:
            if args.mode == "dry_run":
                for entry in planning.entries:
                    if entry.status not in {"READY", "UNVERIFIED"}:
                        continue
                    case = selected_cases[entry.instance.case_id]
                    command = adapter.command(context, entry, case, pics_path=args.pics)
                    print(f"argv[{entry.instance.case_id}]: {shlex.join(command)}")
            has_validation_error = any(issue.severity == "error" for issue in planning.validation_issues)
            return 0 if planning.entries and all(entry.status == "READY" for entry in planning.entries) and not has_validation_error else 2

        results = []
        interrupted = False
        for entry in planning.entries:
            if entry.status in {"READY", "UNVERIFIED"}:
                case = selected_cases[entry.instance.case_id]
                if args.timeout:
                    case = SimpleNamespace(id=case.id, script=case.script,
                                           timeout_seconds=args.timeout)
                try:
                    results.append(adapter.execute(context, entry, case, pics_path=args.pics,
                                                   log_dir=run_dir))
                except KeyboardInterrupt:
                    interrupted = True
                    break
        report = build_run_report(context, inventory, planning.entries, results,
                                  planning.validation_issues, pics_ref=args.pics,
                                  inventory_ref=inventory_ref, interrupted=interrupted)
        paths = write_report(report, run_dir)
        print(f"Report: {paths}")
        print(f"Overall: {report.overall}")
        if interrupted:
            return 130
        return 0 if report.overall == "PASSED" else 1
    except (CatalogError, BackendError, ReportWriteError) as error:
        code = getattr(error, "code", "ERROR")
        print(f"{code}: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("INTERRUPTED", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
