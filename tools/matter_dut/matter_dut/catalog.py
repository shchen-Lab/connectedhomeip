#!/usr/bin/env python3
#
# Copyright (c) 2026 Project CHIP Authors
# Licensed under the Apache License, Version 2.0.

"""Static test catalog validation and capability based planning."""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal

from .inventory import BackendContext, ClusterCapability, Endpoint, Inventory


Backend = Literal["python", "yaml"]
Role = Literal["server", "client", "commissioner", "mixed"]
Scope = Literal["node", "endpoint", "scenario"]
PlanStatus = Literal["READY", "UNVERIFIED", "NOT_APPLICABLE", "BLOCKED"]

_BACKENDS = {"python", "yaml"}
_ROLES = {"server", "client", "commissioner", "mixed"}
_SCOPES = {"node", "endpoint", "scenario"}
_SUITES = {"smoke", "functional", "stateful"}
_TEST_ROOT = Path(__file__).resolve().parents[3]


class CatalogError(ValueError):
    """A catalog or selection could not be used safely."""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


@dataclass(frozen=True)
class Requirement:
    scope: Scope
    cluster_id: int
    role: Literal["server", "client"]
    device_type_ids: tuple[int, ...] = ()
    feature_bits_all: int = 0
    attribute_ids: tuple[int, ...] = ()
    command_ids: tuple[int, ...] = ()


@dataclass(frozen=True)
class CaseDefinition:
    id: str
    backend: Backend
    script: str
    dut_role: Role
    scope: Scope
    requirements: tuple[Requirement, ...] = ()
    pics_requirements: tuple[str, ...] = ()
    endpoint_argument: str | None = None
    suite: str = "smoke"
    effects: tuple[str, ...] = ()
    fixtures: tuple[str, ...] = ()
    timeout_seconds: int = 300
    arguments: tuple[str, ...] = ()


@dataclass(frozen=True)
class PicsDeclaration:
    source: str
    scope: Literal["node", "endpoint"]
    values: dict[str, bool | None]
    digest: str


@dataclass(frozen=True)
class CaseInstance:
    case_id: str
    endpoint_id: int | None
    arguments: tuple[str, ...]
    selection: Literal["explicit", "automatic"]


@dataclass(frozen=True)
class Selection:
    mode: Literal["list", "check", "discover", "plan", "dry_run", "run"] = "plan"
    case_ids: tuple[str, ...] = ()
    suite: str | None = None
    explicit_script: str | None = None
    arguments: tuple[str, ...] = ()


@dataclass(frozen=True)
class PlanEntry:
    instance: CaseInstance
    status: PlanStatus
    reason: str | None = None


@dataclass(frozen=True)
class ValidationIssue:
    code: str
    severity: Literal["error", "warning"]
    message: str
    case_ids: tuple[str, ...] = ()


@dataclass(frozen=True)
class PlanningResult:
    entries: tuple[PlanEntry, ...] = ()
    validation_issues: tuple[ValidationIssue, ...] = ()


def _integer(value: Any, field_name: str, maximum: int = 0xFFFFFFFF) -> int:
    if isinstance(value, bool):
        raise ValueError(f"{field_name} must be an integer")
    try:
        result = int(value, 0) if isinstance(value, str) else int(value)
    except (TypeError, ValueError):
        raise ValueError(f"{field_name} must be an integer") from None
    if result < 0 or result > maximum:
        raise ValueError(f"{field_name} must be in range 0..{maximum}")
    return result


def _string_tuple(value: Any, name: str, *, allow_empty: bool = True) -> tuple[str, ...]:
    if value is None:
        return ()
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        raise ValueError(f"{name} must be an array of strings")
    if not allow_empty and not value:
        raise ValueError(f"{name} must not be empty")
    return tuple(value)


def validate_case_definition(data: dict[str, object]) -> CaseDefinition:
    """Validate one catalog entry without importing or executing its script."""
    try:
        if not isinstance(data, dict):
            raise ValueError("entry must be an object")
        case_id = data.get("id")
        backend = data.get("backend")
        script = data.get("script")
        dut_role = data.get("dut_role")
        scope = data.get("scope")
        suite = data.get("suite", "smoke")
        timeout = _integer(data.get("timeout_seconds", 300), "timeout_seconds", 0xFFFFFFFF)
        if not isinstance(case_id, str) or not case_id.strip():
            raise ValueError("id must be a non-empty string")
        if backend not in _BACKENDS:
            raise ValueError("backend must be python or yaml")
        if dut_role not in _ROLES:
            raise ValueError("dut_role must be server, client, commissioner, or mixed")
        if scope not in _SCOPES:
            raise ValueError("scope must be node, endpoint, or scenario")
        if suite not in _SUITES:
            raise ValueError("suite must be smoke, functional, or stateful")
        if not isinstance(script, str) or not script.strip():
            raise ValueError("script must be a non-empty relative path")
        relative_script = Path(script)
        if relative_script.is_absolute():
            raise ValueError("script must be relative to the repository test root")
        resolved_script = (_TEST_ROOT / relative_script).resolve()
        if not resolved_script.is_relative_to(_TEST_ROOT):
            raise ValueError("script path escapes the repository")
        if not resolved_script.is_file():
            raise ValueError(f"script does not exist: {script}")
        if timeout <= 0:
            raise ValueError("timeout_seconds must be greater than zero")

        raw_requirements = data.get("requirements", [])
        if not isinstance(raw_requirements, list):
            raise ValueError("requirements must be an array")
        requirements: list[Requirement] = []
        for index, item in enumerate(raw_requirements):
            label = f"requirements[{index}]"
            if not isinstance(item, dict):
                raise ValueError(f"{label} must be an object")
            req_scope = item.get("scope", "endpoint")
            req_role = item.get("role")
            if req_scope not in {"node", "endpoint"}:
                raise ValueError(f"{label}.scope must be node or endpoint")
            if req_role not in {"server", "client"}:
                raise ValueError(f"{label}.role must be server or client")
            id_lists = {}
            for key in ("device_type_ids", "attribute_ids", "command_ids"):
                values = item.get(key, [])
                if not isinstance(values, list):
                    raise ValueError(f"{label}.{key} must be an array")
                id_lists[key] = values
            requirements.append(Requirement(
                scope=req_scope,
                cluster_id=_integer(item.get("cluster_id"), f"{label}.cluster_id"),
                role=req_role,
                device_type_ids=tuple(_integer(v, f"{label}.device_type_ids", 0xFFFFFFFF)
                                      for v in id_lists["device_type_ids"]),
                feature_bits_all=_integer(item.get("feature_bits_all", 0), f"{label}.feature_bits_all"),
                attribute_ids=tuple(_integer(v, f"{label}.attribute_ids", 0xFFFFFFFF)
                                    for v in id_lists["attribute_ids"]),
                command_ids=tuple(_integer(v, f"{label}.command_ids", 0xFFFFFFFF)
                                  for v in id_lists["command_ids"]),
            ))

        endpoint_argument = data.get("endpoint_argument")
        if endpoint_argument is not None and (not isinstance(endpoint_argument, str) or not endpoint_argument):
            raise ValueError("endpoint_argument must be a non-empty string or null")
        arguments = _string_tuple(data.get("arguments", []), "arguments")
        if any("\x00" in arg for arg in arguments):
            raise ValueError("arguments must not contain NUL")
        return CaseDefinition(
            id=case_id,
            backend=backend,
            script=resolved_script.relative_to(_TEST_ROOT).as_posix(),
            dut_role=dut_role,
            scope=scope,
            requirements=tuple(requirements),
            pics_requirements=_string_tuple(data.get("pics_requirements", []), "pics_requirements"),
            endpoint_argument=endpoint_argument,
            suite=suite,
            effects=_string_tuple(data.get("effects", []), "effects"),
            fixtures=_string_tuple(data.get("fixtures", []), "fixtures"),
            timeout_seconds=timeout,
            arguments=arguments,
        )
    except (ValueError, TypeError) as error:
        raise CatalogError("CATALOG_INVALID", str(error)) from error


def load_catalog(path: str | Path) -> list[CaseDefinition]:
    """Load and validate a JSON catalog. Catalog entries are data, never code."""
    catalog_path = Path(path)
    try:
        data = json.loads(catalog_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise CatalogError("CATALOG_INVALID", f"cannot read catalog {catalog_path}: {error}") from error
    if isinstance(data, dict):
        entries = data.get("cases")
        if not isinstance(entries, list):
            raise CatalogError("CATALOG_INVALID", "catalog object must contain a cases array")
    elif isinstance(data, list):
        entries = data
    else:
        raise CatalogError("CATALOG_INVALID", "catalog must be an array or an object with cases")
    cases: list[CaseDefinition] = []
    seen: set[str] = set()
    for index, entry in enumerate(entries):
        try:
            case = validate_case_definition(entry)
        except CatalogError as error:
            raise CatalogError(error.code, f"catalog entry {index}: {error.message}") from error
        if case.id in seen:
            raise CatalogError("CATALOG_INVALID", f"catalog entry {index}: duplicate id {case.id!r}")
        seen.add(case.id)
        cases.append(case)
    return cases


_PICS_LINE = re.compile(r"^\s*([A-Za-z][A-Za-z0-9_.:-]*)\s*=\s*(.*?)\s*$")


def parse_pics(path: str | Path, scope: Literal["node", "endpoint"] = "node") -> PicsDeclaration:
    """Parse the simple key/value PICS assignment format, preserving unknowns."""
    if scope not in {"node", "endpoint"}:
        raise CatalogError("PICS_INVALID", "PICS scope must be node or endpoint")
    source_path = Path(path)
    try:
        raw = source_path.read_bytes()
        text = raw.decode("utf-8-sig")
    except (OSError, UnicodeError) as error:
        raise CatalogError("PICS_INVALID", f"cannot read PICS file {source_path}: {error}") from error
    values: dict[str, bool | None] = {}
    for number, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith(("#", "//", ";")):
            continue
        match = _PICS_LINE.match(line)
        if not match:
            raise CatalogError("PICS_INVALID", f"{source_path}:{number}: expected NAME = value")
        name, raw_value = match.groups()
        value = raw_value.split("#", 1)[0].strip().lower()
        if value in {"1", "true", "yes", "supported"}:
            parsed: bool | None = True
        elif value in {"0", "false", "no", "unsupported"}:
            parsed = False
        elif value in {"?", "unknown", ""}:
            parsed = None
        else:
            raise CatalogError("PICS_INVALID", f"{source_path}:{number}: invalid value for {name}: {raw_value!r}")
        if name in values and values[name] != parsed:
            raise CatalogError("PICS_INVALID", f"{source_path}:{number}: conflicting values for {name}")
        values[name] = parsed
    return PicsDeclaration(str(source_path.resolve()), scope, values, hashlib.sha256(raw).hexdigest())


def _role_matches(actual: str, required: str) -> bool:
    return actual == required or (actual == "mixed" and required in {"server", "client", "commissioner"})


def _issue_affects(inventory: Inventory, endpoint_id: int, cluster_id: int, attribute_ids: set[int]) -> bool:
    for issue in inventory.read_issues:
        issue_endpoint = getattr(issue, "endpoint_id", None)
        issue_cluster = getattr(issue, "cluster_id", None)
        issue_attribute = getattr(issue, "attribute_id", None)
        if issue_endpoint not in (None, endpoint_id) or issue_cluster not in (None, cluster_id):
            continue
        if not attribute_ids or issue_attribute in (None, *attribute_ids):
            return True
    return False


def _capability_match(endpoint: Endpoint, requirement: Requirement, inventory: Inventory) -> tuple[bool | None, str | None]:
    capabilities = [c for c in endpoint.clusters
                    if c.cluster_id == requirement.cluster_id and c.role == requirement.role]
    if not capabilities:
        related = [c for c in endpoint.clusters if c.cluster_id == requirement.cluster_id]
        if related:
            return False, f"cluster {requirement.cluster_id:#x} has no {requirement.role} role"
        if _issue_affects(inventory, endpoint.endpoint_id, requirement.cluster_id, set()):
            return None, f"cluster {requirement.cluster_id:#x} discovery is incomplete"
        return False, f"cluster {requirement.cluster_id:#x} not observed"
    cap: ClusterCapability = capabilities[0]
    if requirement.feature_bits_all:
        if cap.feature_map is None:
            return None, f"cluster {requirement.cluster_id:#x} FeatureMap is unknown"
        if cap.feature_map & requirement.feature_bits_all != requirement.feature_bits_all:
            return False, f"cluster {requirement.cluster_id:#x} lacks required feature bits"
    for attr in requirement.attribute_ids:
        if cap.attribute_ids is None:
            return None, f"cluster {requirement.cluster_id:#x} AttributeList is unknown"
        if attr not in cap.attribute_ids:
            return False, f"cluster {requirement.cluster_id:#x} lacks attribute {attr:#x}"
    for command in requirement.command_ids:
        command_ids = cap.accepted_command_ids if requirement.role == "server" else cap.generated_command_ids
        if command_ids is None:
            return None, f"cluster {requirement.cluster_id:#x} command list is unknown"
        if command not in command_ids:
            return False, f"cluster {requirement.cluster_id:#x} lacks command {command:#x}"
    return True, None


def _device_type_match(endpoint: Endpoint, device_type_ids: tuple[int, ...],
                       inventory: Inventory) -> tuple[bool | None, str | None]:
    if not device_type_ids:
        return True, None
    available = {device_type.id for device_type in endpoint.device_types}
    if available.intersection(device_type_ids):
        return True, None
    descriptor_read_failed = any(
        issue.endpoint_id in (None, endpoint.endpoint_id)
        and issue.cluster_id in (None, 0x001D)
        and issue.attribute_id in (None, 0)
        for issue in inventory.read_issues
    )
    if descriptor_read_failed:
        return None, "Descriptor DeviceTypeList is unknown"
    return False, "required device type not observed"


def _requirements_for_endpoint(case: CaseDefinition, endpoint: Endpoint, inventory: Inventory) -> tuple[bool | None, str | None]:
    for requirement in case.requirements:
        if requirement.scope == "node":
            continue
        device_type_match, device_type_reason = _device_type_match(endpoint, requirement.device_type_ids, inventory)
        if device_type_match is not True:
            return device_type_match, device_type_reason
        matched, reason = _capability_match(endpoint, requirement, inventory)
        if matched is not True:
            return matched, reason
    return True, None


def _requirements_for_node(case: CaseDefinition, inventory: Inventory) -> tuple[bool | None, str | None]:
    for requirement in case.requirements:
        if requirement.scope == "endpoint":
            continue
        matches = []
        for endpoint in inventory.endpoints:
            device_type_match, device_type_reason = _device_type_match(
                endpoint, requirement.device_type_ids, inventory)
            if device_type_match is False:
                continue
            if device_type_match is None:
                matches.append((None, device_type_reason))
                continue
            matches.append(_capability_match(endpoint, requirement, inventory))
        if not matches and requirement.device_type_ids and any(
            issue.endpoint_id is None and issue.cluster_id in (None, 0x001D)
            for issue in inventory.read_issues
        ):
            return None, "Descriptor topology is unknown"
        if any(result is True for result, _ in matches):
            continue
        if any(result is None for result, _ in matches):
            return None, next(reason for result, reason in matches if result is None)
        return False, next((reason for _, reason in matches if reason), "node requirement not observed")
    return True, None


def _pics_state(case: CaseDefinition, pics: PicsDeclaration | None) -> tuple[bool | None, str | None]:
    if not case.pics_requirements:
        return True, None
    if pics is None:
        return None, "required PICS declaration was not provided"
    unknown = [name for name in case.pics_requirements if name not in pics.values or pics.values[name] is None]
    if unknown:
        return None, f"PICS value unknown: {', '.join(unknown)}"
    unsupported = [name for name in case.pics_requirements if pics.values[name] is False]
    if unsupported:
        return False, f"PICS declares unsupported: {', '.join(unsupported)}"
    return True, None


def _pics_capability_issues(pics: PicsDeclaration, inventory: Inventory) -> list[ValidationIssue]:
    """Compare explicit cluster capability keys when a catalog defines them.

    Supported keys are cluster:<hex-id>:<server|client>; arbitrary PICS names
    remain declarations and are not guessed from their spelling.
    """
    issues: list[ValidationIssue] = []
    pattern = re.compile(r"^cluster:((?:0x)?[0-9a-fA-F]+):(server|client)$")
    for name, declared in pics.values.items():
        match = pattern.fullmatch(name)
        if not match or declared is None:
            continue
        cluster_id = int(match.group(1), 16)
        role = match.group(2)
        observed = any(cap.cluster_id == cluster_id and cap.role == role
                       for endpoint in inventory.endpoints for cap in endpoint.clusters)
        incomplete = any(issue.cluster_id in (None, cluster_id) and issue.endpoint_id is None
                         or issue.cluster_id == cluster_id
                         for issue in inventory.read_issues)
        if declared and not observed and not incomplete:
            issues.append(ValidationIssue("PICS_CONFLICT", "error",
                                          f"PICS declares {name}=true but capability was not observed"))
        elif not declared and observed:
            issues.append(ValidationIssue("PICS_CONFLICT", "warning",
                                          f"PICS declares {name}=false but capability was observed"))
    return issues


def compare_pics_to_inventory(pics: PicsDeclaration, inventory: Inventory) -> list[ValidationIssue]:
    return _pics_capability_issues(pics, inventory)


def _temporary_case(context: BackendContext, selection: Selection) -> CaseDefinition:
    if not selection.case_ids:
        raise CatalogError("CASE_UNKNOWN", "explicit script selection requires one report case id")
    if len(selection.case_ids) != 1:
        raise CatalogError("CASE_UNKNOWN", "explicit script selection accepts exactly one case id")
    script = Path(selection.explicit_script or "")
    if not script.is_file():
        raise CatalogError("CASE_UNKNOWN", f"explicit script does not exist: {script}")
    case_id = selection.case_ids[0]
    return CaseDefinition(case_id, context.backend, str(script.resolve()), context.dut_role,
                          "node", suite="smoke", timeout_seconds=300)


def build_plan(inventory: Inventory | None, pics: PicsDeclaration | None,
               cases: list[CaseDefinition], selection: Selection,
               *, context: BackendContext | None = None) -> PlanningResult:
    """Create plan entries without discovering or executing test scripts."""
    context = inventory.backend_context if inventory is not None else context
    issues: list[ValidationIssue] = []
    selected: list[tuple[CaseDefinition, bool]] = []
    if selection.explicit_script is not None:
        if context is None:
            raise CatalogError("CONTEXT_MISMATCH", "explicit script planning requires backend context")
        selected.append((_temporary_case(context, selection), True))
    elif selection.case_ids:
        by_id = {case.id: case for case in cases}
        missing = [case_id for case_id in selection.case_ids if case_id not in by_id]
        if missing:
            raise CatalogError("CASE_UNKNOWN", f"unknown case id(s): {', '.join(missing)}")
        selected.extend((by_id[case_id], True) for case_id in selection.case_ids)
    else:
        selected.extend((case, False) for case in cases
                        if (selection.suite is None or case.suite == selection.suite))

    if inventory is None:
        entries = [PlanEntry(CaseInstance(case.id, None,
                                          tuple(selection.arguments) if explicit else case.arguments,
                                          "explicit" if explicit else "automatic"),
                             "UNVERIFIED" if selection.explicit_script is not None else "BLOCKED",
                             "unregistered script; applicability was not inferred"
                             if selection.explicit_script is not None else
                             "inventory is required for capability planning")
                   for case, explicit in selected]
        return PlanningResult(tuple(entries), tuple(issues))

    if pics is not None:
        issues.extend(compare_pics_to_inventory(pics, inventory))
    conflicted_case_ids: set[str] = set()
    for issue in issues:
        if issue.code != "PICS_CONFLICT" or issue.severity != "error":
            continue
        key_match = re.search(r"PICS declares cluster:((?:0x)?[0-9a-fA-F]+):", issue.message)
        if key_match:
            cluster_id = int(key_match.group(1), 16)
            conflicted_case_ids.update(case.id for case, _ in selected
                                       if any(req.cluster_id == cluster_id for req in case.requirements))
    entries: list[PlanEntry] = []
    for case, explicit in selected:
        if case.backend != context.backend:
            entries.append(PlanEntry(CaseInstance(case.id, None, case.arguments,
                                                   "explicit" if explicit else "automatic"),
                                     "NOT_APPLICABLE", f"case backend {case.backend} does not match {context.backend}"))
            continue
        if not _role_matches(context.dut_role, case.dut_role):
            entries.append(PlanEntry(CaseInstance(case.id, None, case.arguments,
                                                   "explicit" if explicit else "automatic"),
                                     "NOT_APPLICABLE", f"DUT role {context.dut_role} does not match {case.dut_role}"))
            continue
        if explicit and selection.explicit_script is not None:
            entries.append(PlanEntry(CaseInstance(case.id, None, tuple(selection.arguments), "explicit"), "UNVERIFIED",
                                     "unregistered script; applicability was not inferred"))
            continue

        if case.id in conflicted_case_ids:
            entries.append(PlanEntry(CaseInstance(case.id, None, case.arguments,
                                                   "explicit" if explicit else "automatic"),
                                     "BLOCKED", "PICS declares support that discovery did not confirm"))
            continue

        # v1 has no generic fixture provider; declared scenario dependencies are
        # therefore blocked until an orchestrator supplies them.
        missing_fixtures = list(case.fixtures)
        if missing_fixtures and case.scope == "scenario":
            entries.append(PlanEntry(CaseInstance(case.id, None, case.arguments,
                                                   "explicit" if explicit else "automatic"),
                                     "BLOCKED", f"scenario fixtures are not configured: {', '.join(missing_fixtures)}"))
            continue

        pics_state, pics_reason = _pics_state(case, pics)
        if pics_state is False:
            entries.append(PlanEntry(CaseInstance(case.id, None, case.arguments,
                                                   "explicit" if explicit else "automatic"),
                                     "NOT_APPLICABLE", pics_reason))
            continue

        if case.scope == "endpoint":
            node_state, node_reason = _requirements_for_node(case, inventory)
            if node_state is False or node_state is None:
                entries.append(PlanEntry(CaseInstance(case.id, None, case.arguments,
                                                       "explicit" if explicit else "automatic"),
                                         "NOT_APPLICABLE" if node_state is False else "BLOCKED",
                                         node_reason))
                continue
            if not inventory.endpoints and any(issue.endpoint_id is None for issue in inventory.read_issues):
                entries.append(PlanEntry(CaseInstance(case.id, None, case.arguments,
                                                       "explicit" if explicit else "automatic"),
                                         "BLOCKED", "endpoint topology could not be discovered"))
                continue
            candidates = []
            for endpoint in inventory.endpoints:
                state, reason = _requirements_for_endpoint(case, endpoint, inventory)
                if state is True:
                    candidates.append(endpoint)
                elif state is None:
                    entries.append(PlanEntry(CaseInstance(case.id, endpoint.endpoint_id, case.arguments,
                                                           "explicit" if explicit else "automatic"), "BLOCKED", reason))
            if candidates:
                for endpoint in candidates:
                    args = list(case.arguments)
                    if case.endpoint_argument:
                        args.extend((case.endpoint_argument, str(endpoint.endpoint_id)))
                    status: PlanStatus = "BLOCKED" if pics_state is None else "READY"
                    entries.append(PlanEntry(CaseInstance(case.id, endpoint.endpoint_id, tuple(args),
                                                           "explicit" if explicit else "automatic"),
                                             status, pics_reason if status == "BLOCKED" else None))
            elif not any(entry.instance.case_id == case.id for entry in entries):
                entries.append(PlanEntry(CaseInstance(case.id, None, case.arguments,
                                                       "explicit" if explicit else "automatic"),
                                         "NOT_APPLICABLE", "no endpoint satisfies the declared requirements"))
            continue

        state, reason = _requirements_for_node(case, inventory)
        if state is False:
            status: PlanStatus = "NOT_APPLICABLE"
        elif state is None or pics_state is None:
            status = "BLOCKED"
            reason = reason or pics_reason
        else:
            status = "READY"
        entries.append(PlanEntry(CaseInstance(case.id, None, case.arguments,
                                               "explicit" if explicit else "automatic"), status, reason))

    # A declared PICS conflict remains visible and is linked to matching planned cases.
    linked = []
    for issue in issues:
        key_match = re.search(r"PICS declares cluster:((?:0x)?[0-9a-fA-F]+):", issue.message)
        affected = tuple(case.id for case, _ in selected
                         if key_match and any(req.cluster_id == int(key_match.group(1), 16)
                                              for req in case.requirements))
        linked.append(ValidationIssue(issue.code, issue.severity, issue.message, affected))
    blocked_cases = {
        case_id
        for issue in linked
        if issue.severity == "error"
        for case_id in issue.case_ids
    }
    if blocked_cases:
        entries = [
            PlanEntry(entry.instance, "BLOCKED", "PICS conflicts with observed capability")
            if entry.instance.case_id in blocked_cases and entry.status == "READY" else entry
            for entry in entries
        ]
    return PlanningResult(tuple(entries), tuple(linked))
