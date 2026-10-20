"""Offline unit coverage for the generic Matter DUT framework."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from tools.matter_dut.matter_dut.catalog import (
    CaseDefinition,
    PicsDeclaration,
    Requirement,
    Selection,
    build_plan,
    load_catalog,
    validate_case_definition,
)
from tools.matter_dut.matter_dut.inventory import (
    BackendContext,
    Inventory,
    ReadIssue,
    build_inventory,
    inventory_from_dict,
    inventory_to_dict,
)
from tools.matter_dut.matter_dut.report import (
    CaseResult,
    RunReport,
    aggregate_status,
    build_run_report,
)


def context() -> BackendContext:
    return BackendContext("python", 0x12344321, "server", "/tmp/python-storage.json", "python:test-fabric")


def inventory(*, include_onoff: bool = True, known_attributes: bool = True) -> Inventory:
    server_list = [0x001D]
    if include_onoff:
        server_list.append(0x0006)
    descriptor = [
        {
            "endpoint_id": 0,
            "device_types": [{"id": 0x0016, "revision": 1}],
            "parts_list": [2],
            "server_list": [0x001D],
            "client_list": [],
        },
        {
            "endpoint_id": 2,
            "device_types": [{"id": 0x0100, "revision": 1}],
            "parts_list": [],
            "server_list": server_list,
            "client_list": [],
        },
    ]
    capabilities = []
    if include_onoff:
        capabilities.append({
            "endpoint_id": 2,
            "clusters": [{
                "cluster_id": 0x0006,
                "role": "server",
                "feature_map": 0,
                "attribute_ids": [0] if known_attributes else "unknown",
                "accepted_command_ids": [0],
                "generated_command_ids": [],
                "revision": 6,
            }],
        })
    return build_inventory(context(), descriptor, capabilities, [])


def onoff_case() -> CaseDefinition:
    return CaseDefinition(
        id="onoff.read",
        backend="python",
        script="src/python_testing/TC_DeviceBasicComposition.py",
        dut_role="server",
        scope="endpoint",
        requirements=(Requirement("endpoint", 0x0006, "server", attribute_ids=(0,)),),
        endpoint_argument="--endpoint",
        arguments=("--tests", "test_TC_SM_1_1"),
    )


class InventoryTests(unittest.TestCase):
    def test_descriptor_topology_and_unknown_round_trip(self):
        source = inventory(known_attributes=False)
        root, endpoint = source.endpoints
        self.assertEqual(root.parts_list, (2,))
        self.assertEqual(endpoint.endpoint_id, 2)
        self.assertIsNone(endpoint.clusters[0].attribute_ids)

        restored = inventory_from_dict(inventory_to_dict(source))
        self.assertEqual(restored, source)
        self.assertEqual(inventory_to_dict(restored)["endpoints"][1]["clusters"][0]["attribute_ids"], "unknown")

    def test_read_issues_preserve_attribute_path(self):
        issue = ReadIssue("READ_FAILED", "denied", endpoint_id=2, cluster_id=6, attribute_id=0)
        source = build_inventory(context(), [], [], [issue])
        encoded = inventory_to_dict(source)
        self.assertEqual(encoded["read_issues"][0]["endpoint_id"], 2)
        self.assertEqual(inventory_from_dict(encoded).read_issues, (issue,))


class CatalogPlanningTests(unittest.TestCase):
    def test_registered_catalog_loads_and_selects_capability_case(self):
        cases = load_catalog(Path(__file__).with_name("catalog.json"))
        self.assertEqual([case.id for case in cases], ["python.root-node-basic-composition"])

    def test_endpoint_planning_uses_observed_endpoint_id(self):
        result = build_plan(inventory(), None, [onoff_case()], Selection(case_ids=("onoff.read",)))
        self.assertEqual(len(result.entries), 1)
        self.assertEqual(result.entries[0].instance.endpoint_id, 2)
        self.assertEqual(result.entries[0].instance.arguments, ("--tests", "test_TC_SM_1_1", "--endpoint", "2"))
        self.assertEqual(result.entries[0].status, "READY")

    def test_unknown_required_attribute_blocks_case(self):
        result = build_plan(inventory(known_attributes=False), None, [onoff_case()],
                            Selection(case_ids=("onoff.read",)))
        self.assertEqual([entry.status for entry in result.entries], ["BLOCKED"])

    def test_pics_conflict_blocks_affected_case_and_remains_visible(self):
        pics = PicsDeclaration("inline", "node", {"cluster:0x0006:server": True}, "digest")
        result = build_plan(inventory(include_onoff=False), pics, [onoff_case()],
                            Selection(case_ids=("onoff.read",)))
        self.assertEqual([issue.code for issue in result.validation_issues], ["PICS_CONFLICT"])
        self.assertEqual(result.entries[0].status, "BLOCKED")

    def test_catalog_rejects_paths_outside_repository(self):
        with self.assertRaises(ValueError):
            validate_case_definition({
                "id": "bad.path",
                "backend": "python",
                "script": "../../outside.py",
                "dut_role": "server",
                "scope": "node",
            })


class ReportTests(unittest.TestCase):
    def test_report_adds_nonexecution_results_and_aggregates_incomplete(self):
        plan = build_plan(inventory(known_attributes=False), None, [onoff_case()],
                          Selection(case_ids=("onoff.read",))).entries
        report = build_run_report(context(), inventory(known_attributes=False), plan, [], [])
        self.assertEqual([result.status for result in report.results], ["BLOCKED"])
        self.assertEqual(report.overall, "INCOMPLETE")

    def test_failure_and_validation_error_take_precedence(self):
        item = onoff_case()
        from tools.matter_dut.matter_dut.catalog import CaseInstance, PlanEntry, ValidationIssue

        entry = PlanEntry(CaseInstance(item.id, 2, (), "explicit"), "READY")
        failure = CaseResult(entry.instance, "FAIL", exit_code=1)
        report = RunReport("1.0", context(), None, None, [entry], [failure], [], [])
        self.assertEqual(aggregate_status(report), "FAILED")
        report.results = [CaseResult(entry.instance, "PASS")]
        report.validation_issues = [ValidationIssue("PICS_CONFLICT", "error", "conflict", (item.id,))]
        self.assertEqual(aggregate_status(report), "FAILED")


if __name__ == "__main__":
    unittest.main()
