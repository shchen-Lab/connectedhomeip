"""Python and YAML execution adapters for the Matter DUT runner."""

from __future__ import annotations

import asyncio
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

from .catalog import CaseDefinition, PlanEntry
from .inventory import BackendContext, ReadIssue, build_inventory
from .report import CaseResult


class BackendError(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


class BackendAdapter:
    def discover(self, context: BackendContext, cases: list[CaseDefinition]):
        raise NotImplementedError

    def execute(self, context: BackendContext, entry: PlanEntry, case: CaseDefinition,
                *, pics_path: str | None, log_dir: Path) -> CaseResult:
        raise NotImplementedError


def _script_path(script: str) -> Path:
    path = Path(script)
    if path.is_absolute():
        return path
    return Path(__file__).resolve().parents[3] / path


def _case_log_path(log_dir: Path, entry: PlanEntry) -> Path:
    endpoint = entry.instance.endpoint_id
    suffix = "" if endpoint is None else f"-endpoint-{endpoint}"
    return log_dir / f"{entry.instance.case_id}{suffix}.log"


def _python_status(log_path: Path, exit_code: int) -> tuple[str, str | None]:
    if exit_code:
        return "FAIL", f"process exited with {exit_code}"
    try:
        output = log_path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        return "ERROR", f"cannot read test log: {error}"
    summaries = re.findall(r"Summary for test class .*", output)
    if not summaries:
        return "ERROR", "test runner exited successfully without a result summary"
    summary = summaries[-1]
    if "Error 0" not in summary or "Failed 0" not in summary:
        return "FAIL", summary
    if re.search(r"Passed[\s:]+[1-9][0-9]*", summary):
        return "PASS", None
    if re.search(r"Skipped[\s:]+[1-9][0-9]*", summary):
        return "SKIP", summary
    return "ERROR", summary


def _yaml_status(log_path: Path, exit_code: int) -> tuple[str, str | None]:
    if exit_code:
        return "FAIL", f"process exited with {exit_code}"
    try:
        output = log_path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        return "ERROR", f"cannot read test log: {error}"
    steps = re.findall(r"with ([0-9]+) steps runned and ([0-9]+) steps skipped", output)
    if not steps:
        return "ERROR", "YAML runner exited successfully without a step summary"
    executed, skipped = map(int, steps[-1])
    if executed > 0:
        return "PASS", None
    if skipped > 0:
        return "SKIP", "no YAML steps executed"
    return "ERROR", "YAML runner reported no executed or skipped steps"


class PythonBackend(BackendAdapter):
    def __init__(self, python: str | None = None):
        self.python = python or sys.executable

    def discover(self, context: BackendContext, cases: list[CaseDefinition]):
        if not Path(context.storage_ref).is_file():
            raise BackendError("BACKEND_UNAVAILABLE",
                               f"Python controller storage does not exist: {context.storage_ref}")
        root = Path(__file__).resolve().parents[3]
        testing_root = root / "src/python_testing"
        sys.path.insert(0, str(testing_root / "matter_testing_infrastructure"))
        try:
            import matter
            import matter.CertificateAuthority
            import matter.native
            from matter.ChipStack import ChipStack
            from matter.clusters import Attribute, Objects as Clusters
            from matter.storage import PersistentStorageJSON
        except Exception as error:
            raise BackendError("BACKEND_UNAVAILABLE", f"Matter Python SDK import failed: {error}") from error

        stack = None
        authority = None
        try:
            matter.native.Init()
            stack = ChipStack(PersistentStorageJSON(context.storage_ref))
            authority = matter.CertificateAuthority.CertificateAuthorityManager(chipStack=stack)
            authority.LoadAuthoritiesFromStorage()
            if not authority.activeCaList or not authority.activeCaList[0].adminList:
                raise BackendError("BACKEND_UNAVAILABLE", "storage has no commissioned FabricAdmin")
            controller = authority.activeCaList[0].adminList[0].NewController()
        except Exception as error:
            try:
                if authority is not None:
                    authority.Shutdown()
            except Exception:
                pass
            try:
                if stack is not None:
                    stack.Shutdown()
            except Exception:
                pass
            if isinstance(error, BackendError):
                raise
            raise BackendError("CONNECTION_FAILED", f"cannot initialize Python controller: {error}") from error

        async def read_inventory():
            issues: list[ReadIssue] = []
            descriptor_data: dict[int, dict[str, Any]] = {}
            capability_data: dict[int, dict[str, Any]] = {}
            try:
                response = await controller.Read(
                    context.node_id,
                    [(Clusters.Descriptor),
                     Attribute.AttributePath(None, None, 0xFFF8),
                     Attribute.AttributePath(None, None, 0xFFF9),
                     Attribute.AttributePath(None, None, 0xFFFA),
                     Attribute.AttributePath(None, None, 0xFFFC),
                     Attribute.AttributePath(None, None, 0xFFFD)])
            except Exception as error:
                issues.append(ReadIssue("READ_FAILED", str(error)))
                return build_inventory(context, [], [], issues)

            for endpoint_id, cluster_map in response.attributes.items():
                for cluster_type, attribute_map in cluster_map.items():
                    cluster_id = getattr(cluster_type, "id", None)
                    if cluster_id is None:
                        continue
                    for attribute_type, value in attribute_map.items():
                        attribute_id = getattr(attribute_type, "attribute_id", None)
                        if attribute_id is None:
                            continue
                        if value.__class__.__name__ == "ValueDecodeFailure":
                            issues.append(ReadIssue("READ_FAILED", str(getattr(value, "Reason", value)),
                                                    endpoint_id, cluster_id, attribute_id))
                            continue
                        if cluster_id == 0x001D:
                            endpoint = descriptor_data.setdefault(endpoint_id, {"endpoint_id": endpoint_id})
                            descriptor_names = {
                                0: "device_types", 1: "server_list", 2: "client_list", 3: "parts_list"
                            }
                            name = descriptor_names.get(attribute_id)
                            if name == "device_types":
                                endpoint[name] = [
                                    {"id": item.deviceType, "revision": item.revision}
                                    for item in value]
                            elif name:
                                endpoint[name] = list(value)
                            continue

                        endpoint = capability_data.setdefault(endpoint_id, {"endpoint_id": endpoint_id, "clusters": []})
                        cluster = next((item for item in endpoint["clusters"]
                                        if item["cluster_id"] == cluster_id), None)
                        if cluster is None:
                            cluster = {"cluster_id": cluster_id, "role": "unknown"}
                            endpoint["clusters"].append(cluster)
                        if attribute_id == 0xFFF8:
                            cluster["attribute_ids"] = list(value)
                        elif attribute_id == 0xFFF9:
                            cluster["accepted_command_ids"] = list(value)
                        elif attribute_id == 0xFFFA:
                            cluster["generated_command_ids"] = list(value)
                        elif attribute_id == 0xFFFC:
                            cluster["feature_map"] = int(value)
                        elif attribute_id == 0xFFFD:
                            cluster["revision"] = int(value)

            descriptors = list(descriptor_data.values())
            if not descriptors:
                issues.append(ReadIssue("READ_MISSING", "Descriptor topology was not returned", cluster_id=0x001D))
            descriptor_attribute_ids = {
                "device_types": 0, "server_list": 1, "client_list": 2, "parts_list": 3
            }
            for endpoint in descriptors:
                for field, attribute_id in descriptor_attribute_ids.items():
                    if field not in endpoint:
                        issues.append(ReadIssue(
                            "READ_MISSING", f"Descriptor {field} was not returned",
                            endpoint["endpoint_id"], 0x001D, attribute_id))
            roles_by_cluster: dict[tuple[int, int], set[str]] = {}
            for endpoint in descriptors:
                for field, role in (("server_list", "server"), ("client_list", "client")):
                    for cluster_id in endpoint.get(field, []):
                        roles_by_cluster.setdefault((endpoint["endpoint_id"], cluster_id), set()).add(role)
            for endpoint in capability_data.values():
                expanded = []
                for cluster in endpoint["clusters"]:
                    roles = roles_by_cluster.get((endpoint["endpoint_id"], cluster["cluster_id"]), {"unknown"})
                    expanded.extend({**cluster, "role": role} for role in sorted(roles))
                endpoint["clusters"] = expanded
            return build_inventory(context, descriptors, list(capability_data.values()), issues)

        try:
            return asyncio.run(read_inventory())
        finally:
            try:
                authority.Shutdown()
                stack.Shutdown()
            except Exception:
                pass

    def command(self, context: BackendContext, entry: PlanEntry, case: CaseDefinition,
                *, pics_path: str | None) -> list[str]:
        script = _script_path(case.script)
        args = [self.python, str(script), "--storage-path", context.storage_ref,
                "--dut-node-id", str(context.node_id)]
        if pics_path:
            args.extend(("--PICS", pics_path))
        args.extend(entry.instance.arguments)
        return args

    def execute(self, context: BackendContext, entry: PlanEntry, case: CaseDefinition,
                *, pics_path: str | None, log_dir: Path) -> CaseResult:
        args = self.command(context, entry, case, pics_path=pics_path)
        log_path = _case_log_path(log_dir, entry)
        start = time.monotonic()
        try:
            with log_path.open("wb") as output:
                completed = subprocess.run(args, stdout=output, stderr=subprocess.STDOUT,
                                            timeout=case.timeout_seconds, check=False)
            status, reason = _python_status(log_path, completed.returncode)
            return CaseResult(entry.instance, status, completed.returncode,
                              int((time.monotonic() - start) * 1000), reason, str(log_path))
        except subprocess.TimeoutExpired:
            return CaseResult(entry.instance, "TIMEOUT", None,
                              int((time.monotonic() - start) * 1000),
                              f"exceeded {case.timeout_seconds}s", str(log_path))
        except OSError as error:
            return CaseResult(entry.instance, "ERROR", None,
                              int((time.monotonic() - start) * 1000), str(error), str(log_path))


class YamlBackend(BackendAdapter):
    def __init__(self, chip_tool: str, storage_directory: str):
        self.chip_tool = chip_tool
        self.storage_directory = storage_directory

    def discover(self, context: BackendContext, cases: list[CaseDefinition]):
        raise BackendError("BACKEND_UNAVAILABLE",
                           "YAML interactive server does not expose a structured Matter Read response")

    def command(self, context: BackendContext, entry: PlanEntry, case: CaseDefinition,
                *, pics_path: str | None) -> list[str]:
        script = _script_path(case.script)
        root = Path(__file__).resolve().parents[3]
        args = [sys.executable, str(root / "scripts/tests/chipyaml/chiptool.py"),
                "--server_path", self.chip_tool,
                "--server_arguments", f"interactive server --storage-directory {self.storage_directory}"]
        if pics_path:
            args.extend(("--PICS", pics_path))
        args.extend(("tests", script.stem, "--nodeId", str(context.node_id)))
        args.extend(entry.instance.arguments)
        return args

    def execute(self, context: BackendContext, entry: PlanEntry, case: CaseDefinition,
                *, pics_path: str | None, log_dir: Path) -> CaseResult:
        args = self.command(context, entry, case, pics_path=pics_path)
        log_path = _case_log_path(log_dir, entry)
        start = time.monotonic()
        try:
            with log_path.open("wb") as output:
                completed = subprocess.run(args, stdout=output, stderr=subprocess.STDOUT,
                                            timeout=case.timeout_seconds, check=False)
            status, reason = _yaml_status(log_path, completed.returncode)
            return CaseResult(entry.instance, status, completed.returncode,
                              int((time.monotonic() - start) * 1000), reason, str(log_path))
        except subprocess.TimeoutExpired:
            return CaseResult(entry.instance, "TIMEOUT", None,
                              int((time.monotonic() - start) * 1000),
                              f"exceeded {case.timeout_seconds}s", str(log_path))
        except OSError as error:
            return CaseResult(entry.instance, "ERROR", None,
                              int((time.monotonic() - start) * 1000), str(error), str(log_path))


def read_inventory_file(path: str | os.PathLike[str]):
    from .inventory import inventory_from_dict

    try:
        with Path(path).open(encoding="utf-8") as source:
            data = json.load(source)
        return inventory_from_dict(data)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise BackendError("INVENTORY_INVALID", f"cannot load inventory {path}: {error}") from error
