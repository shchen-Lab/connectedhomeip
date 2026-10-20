"""Result normalization and report serialization for Matter DUT runs."""

from __future__ import annotations

import dataclasses
import json
import os
import tempfile
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Any


REPORT_SCHEMA_VERSION = "1.0"


@dataclass
class CaseResult:
    instance: Any
    status: str
    exit_code: int | None = None
    duration_ms: int | None = None
    reason: str | None = None
    log_ref: str | None = None


@dataclass
class RunReport:
    schema_version: str
    backend_context: Any
    inventory_ref: str | None
    pics_ref: str | None
    plan: list[Any]
    results: list[CaseResult]
    validation_issues: list[Any]
    coverage_gaps: list[str]
    overall: str = "INCOMPLETE"
    interrupted: bool = False


class ReportWriteError(Exception):
    """A report could not be written completely."""

    code = "REPORT_WRITE_FAILED"

    def __init__(self, message: str, *, path: str | None = None) -> None:
        super().__init__(message)
        self.path = path


def _get(value: Any, key: str, default: Any = None) -> Any:
    if isinstance(value, Mapping):
        return value.get(key, default)
    return getattr(value, key, default)


def _json_value(value: Any) -> Any:
    if dataclasses.is_dataclass(value) and not isinstance(value, type):
        return {key: _json_value(item) for key, item in dataclasses.asdict(value).items()}
    if isinstance(value, Enum):
        return value.value
    if isinstance(value, Mapping):
        return {str(key): _json_value(item) for key, item in value.items()}
    if isinstance(value, (list, tuple, set, frozenset)):
        return [_json_value(item) for item in value]
    if isinstance(value, Path):
        return str(value)
    if value is None or isinstance(value, (str, int, float, bool)):
        return value
    if hasattr(value, "__dict__"):
        return {key: _json_value(item) for key, item in vars(value).items() if not key.startswith("_")}
    return str(value)


def _instance_key(instance: Any) -> tuple[Any, Any, tuple[str, ...], Any]:
    args = _get(instance, "arguments", []) or []
    return (
        _get(instance, "case_id"),
        _get(instance, "endpoint_id"),
        tuple(str(arg) for arg in args),
        _get(instance, "selection"),
    )


def _normalized_status(value: Any) -> str:
    if isinstance(value, Enum):
        value = value.value
    return str(value or "").upper()


def _plan_status(entry: Any) -> str:
    return _normalized_status(_get(entry, "status"))


def _coverage_gaps(plan: list[Any], results: list[CaseResult]) -> list[str]:
    gaps: list[str] = []
    seen: set[str] = set()
    by_key = {_instance_key(result.instance): result for result in results}

    def add(gap: str) -> None:
        if gap not in seen:
            seen.add(gap)
            gaps.append(gap)

    for entry in plan:
        instance = _get(entry, "instance")
        case_id = str(_get(instance, "case_id", "<unknown>"))
        endpoint_id = _get(instance, "endpoint_id")
        label = case_id if endpoint_id is None else f"{case_id}@endpoint-{endpoint_id}"
        status = _plan_status(entry)
        result = by_key.get(_instance_key(instance))
        result_status = _normalized_status(_get(result, "status")) if result else "NOT_RUN"

        if status == "UNVERIFIED":
            add(f"UNVERIFIED:{label}")
        if status == "BLOCKED" or result_status == "BLOCKED":
            add(f"BLOCKED:{label}")
        elif result_status in {"SKIP", "NOT_RUN"}:
            add(f"{result_status}:{label}")

    if not plan:
        add("NO_SELECTED_CASES")
    elif not any(_plan_status(item) in {"READY", "UNVERIFIED"} for item in plan):
        add("NO_EXECUTABLE_CASES")
    return gaps


def aggregate_status(report: RunReport, *, interrupted: bool | None = None) -> str:
    """Apply the design's precedence rules to a normalized run report."""
    results = [_normalized_status(_get(result, "status")) for result in report.results]
    has_error = any(
        str(_get(issue, "severity", "")).lower() == "error"
        for issue in report.validation_issues
    )
    if has_error or any(status in {"FAIL", "ERROR", "TIMEOUT"} for status in results):
        return "FAILED"

    was_interrupted = report.interrupted if interrupted is None else interrupted
    if was_interrupted:
        return "INTERRUPTED"

    plan_statuses = [_plan_status(entry) for entry in report.plan]
    if (
        any(status in {"BLOCKED", "SKIP", "NOT_RUN"} for status in results)
        or "UNVERIFIED" in plan_statuses
        or bool(report.coverage_gaps)
        or not any(status in {"READY", "UNVERIFIED"} for status in plan_statuses)
    ):
        return "INCOMPLETE"
    return "PASSED"


def build_run_report(
    context: Any,
    inventory: Any | None,
    plan: Sequence[Any],
    results: Sequence[Any],
    issues: Sequence[Any],
    *,
    pics_ref: str | None = None,
    inventory_ref: str | None = None,
    interrupted: bool = False,
) -> RunReport:
    """Build a report with exactly one result for every planned instance."""
    plan_items = list(plan)
    normalized_results: dict[tuple[Any, Any, tuple[str, ...], Any], CaseResult] = {}
    for raw in results:
        instance = _get(raw, "instance")
        if instance is None:
            continue
        normalized_results[_instance_key(instance)] = CaseResult(
            instance=instance,
            status=_normalized_status(_get(raw, "status", "ERROR")),
            exit_code=_get(raw, "exit_code"),
            duration_ms=_get(raw, "duration_ms"),
            reason=_get(raw, "reason"),
            log_ref=_get(raw, "log_ref"),
        )

    completed: list[CaseResult] = []
    for entry in plan_items:
        instance = _get(entry, "instance")
        if instance is None:
            continue
        key = _instance_key(instance)
        result = normalized_results.get(key)
        if result is None:
            plan_status = _plan_status(entry)
            mapped = plan_status if plan_status in {"BLOCKED", "NOT_APPLICABLE"} else "NOT_RUN"
            result = CaseResult(instance=instance, status=mapped, reason=_get(entry, "reason"))
        completed.append(result)

    gaps = _coverage_gaps(plan_items, completed)
    report = RunReport(
        schema_version=REPORT_SCHEMA_VERSION,
        backend_context=context,
        inventory_ref=inventory_ref or _get(inventory, "ref"),
        pics_ref=pics_ref,
        plan=plan_items,
        results=completed,
        validation_issues=list(issues),
        coverage_gaps=gaps,
        interrupted=interrupted,
    )
    report.overall = aggregate_status(report)
    return report


def _atomic_write(path: Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent, prefix=f".{path.name}.", suffix=".tmp", delete=False
        ) as output:
            temp_path = output.name
            output.write(contents)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temp_path, path)
        temp_path = None
    except OSError as error:
        raise ReportWriteError(f"failed to write {path}: {error}", path=str(path)) from error
    finally:
        if temp_path is not None:
            try:
                os.unlink(temp_path)
            except OSError:
                pass


def _report_dict(report: RunReport) -> dict[str, Any]:
    return {
        "schema_version": report.schema_version,
        "backend_context": _json_value(report.backend_context),
        "inventory_ref": report.inventory_ref,
        "pics_ref": report.pics_ref,
        "plan": [_json_value(item) for item in report.plan],
        "results": [_json_value(item) for item in report.results],
        "validation_issues": [_json_value(item) for item in report.validation_issues],
        "coverage_gaps": list(report.coverage_gaps),
        "overall": report.overall,
        "interrupted": report.interrupted,
    }


def write_inventory(inventory: Any, output_path: str | os.PathLike[str]) -> str:
    """Atomically write a versioned inventory snapshot as JSON."""
    data = _json_value(inventory)
    if not isinstance(data, dict) or "schema_version" not in data:
        raise ValueError("inventory must be an object with schema_version")
    path = Path(output_path)
    contents = json.dumps(data, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
    _atomic_write(path, contents)
    return str(path)


def _render_text(data: Mapping[str, Any]) -> str:
    lines = [
        f"Matter DUT test report (schema {data['schema_version']})",
        f"Overall: {data['overall']}",
        f"Backend: {_get(data['backend_context'], 'backend', 'unknown')}",
        f"Plan entries: {len(data['plan'])}",
        f"Results: {len(data['results'])}",
        f"Validation issues: {len(data['validation_issues'])}",
        f"Coverage gaps: {len(data['coverage_gaps'])}",
        "",
        "Results:",
    ]
    if data["results"]:
        for result in data["results"]:
            instance = result.get("instance", {})
            case_id = instance.get("case_id", "<unknown>")
            endpoint = instance.get("endpoint_id")
            label = case_id if endpoint is None else f"{case_id}@endpoint-{endpoint}"
            detail = f"  {label}: {result['status']}"
            if result.get("reason"):
                detail += f" ({result['reason']})"
            lines.append(detail)
    else:
        lines.append("  (none)")
    if data["coverage_gaps"]:
        lines.extend(["", "Coverage gaps:"])
        lines.extend(f"  {gap}" for gap in data["coverage_gaps"])
    if data["validation_issues"]:
        lines.extend(["", "Validation issues:"])
        for issue in data["validation_issues"]:
            lines.append(f"  {_get(issue, 'severity', 'unknown')}: {_get(issue, 'code', 'UNKNOWN')}: {_get(issue, 'message', '')}")
    return "\n".join(lines) + "\n"


def write_report(report: RunReport, output_dir: str | os.PathLike[str]) -> dict[str, str]:
    """Atomically write the JSON source report and its TXT projection."""
    data = _report_dict(report)
    data["overall"] = aggregate_status(report)
    out = Path(output_dir)
    paths = {
        "json": out / "run-report.json",
        "plan": out / "plan.json",
        "summary": out / "summary.json",
        "text": out / "run-report.txt",
    }
    plan_data = {
        "schema_version": report.schema_version,
        "plan": data["plan"],
        "results": data["results"],
    }
    summary_data = {
        "schema_version": report.schema_version,
        "overall": data["overall"],
        "coverage_gaps": data["coverage_gaps"],
        "validation_issues": data["validation_issues"],
        "result_counts": {
            status: sum(1 for result in data["results"] if result["status"] == status)
            for status in sorted({result["status"] for result in data["results"]})
        },
    }
    payloads = {
        paths["json"]: json.dumps(data, indent=2, sort_keys=True, ensure_ascii=True) + "\n",
        paths["plan"]: json.dumps(plan_data, indent=2, sort_keys=True, ensure_ascii=True) + "\n",
        paths["summary"]: json.dumps(summary_data, indent=2, sort_keys=True, ensure_ascii=True) + "\n",
        paths["text"]: _render_text(data),
    }
    for path, contents in payloads.items():
        _atomic_write(path, contents)
    return {name: str(path) for name, path in paths.items()}
