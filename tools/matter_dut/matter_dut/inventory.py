"""Backend-independent Matter DUT inventory models and conversion helpers."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any, Mapping


SCHEMA_VERSION = "1.0"
UNKNOWN = "unknown"


@dataclass(frozen=True)
class BackendContext:
    backend: str
    node_id: int
    dut_role: str
    storage_ref: str
    fabric_ref: str


@dataclass(frozen=True)
class DeviceType:
    id: int
    revision: int


@dataclass(frozen=True)
class ClusterCapability:
    cluster_id: int
    role: str
    feature_map: int | None = None
    attribute_ids: tuple[int, ...] | None = None
    accepted_command_ids: tuple[int, ...] | None = None
    generated_command_ids: tuple[int, ...] | None = None
    revision: int | None = None


@dataclass(frozen=True)
class Endpoint:
    endpoint_id: int
    device_types: tuple[DeviceType, ...] = ()
    parts_list: tuple[int, ...] | None = None
    clusters: tuple[ClusterCapability, ...] = ()


@dataclass(frozen=True)
class ReadIssue:
    code: str
    message: str
    endpoint_id: int | None = None
    cluster_id: int | None = None
    attribute_id: int | None = None


@dataclass(frozen=True)
class Inventory:
    schema_version: str
    backend_context: BackendContext
    captured_at: datetime
    endpoints: tuple[Endpoint, ...] = ()
    read_issues: tuple[ReadIssue, ...] = ()


def _unknown(value: Any) -> Any:
    return None if value == UNKNOWN else value


def _as_int(value: Any, *, bits: int, field_name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{field_name} must be an integer")
    if value < 0 or value >= (1 << bits):
        raise ValueError(f"{field_name} must fit in uint{bits}")
    return value


def _id_list(value: Any, *, field_name: str) -> tuple[int, ...] | None:
    value = _unknown(value)
    if value is None:
        return None
    if not isinstance(value, (list, tuple)):
        raise ValueError(f"{field_name} must be an array or unknown")
    return tuple(_as_int(item, bits=32, field_name=field_name) for item in value)


def _json_unknown(value: int | tuple[int, ...] | None) -> Any:
    if value is None:
        return UNKNOWN
    if isinstance(value, tuple):
        return list(value)
    return value


def _context_to_dict(context: BackendContext) -> dict[str, object]:
    return {
        "backend": context.backend,
        "node_id": context.node_id,
        "dut_role": context.dut_role,
        "storage_ref": context.storage_ref,
        "fabric_ref": context.fabric_ref,
    }


def _context_from_dict(data: Mapping[str, Any]) -> BackendContext:
    return BackendContext(
        backend=str(data["backend"]),
        node_id=_as_int(data["node_id"], bits=64, field_name="node_id"),
        dut_role=str(data["dut_role"]),
        storage_ref=str(data["storage_ref"]),
        fabric_ref=str(data["fabric_ref"]),
    )


def inventory_to_dict(inventory: Inventory) -> dict[str, object]:
    """Return JSON-compatible data; references are serialized, never storage contents."""
    return {
        "schema_version": inventory.schema_version,
        "backend_context": _context_to_dict(inventory.backend_context),
        "captured_at": inventory.captured_at.isoformat(),
        "endpoints": [
            {
                "endpoint_id": endpoint.endpoint_id,
                "device_types": [
                    {"id": item.id, "revision": item.revision}
                    for item in endpoint.device_types
                ],
                "parts_list": _json_unknown(endpoint.parts_list),
                "clusters": [
                    {
                        "cluster_id": cluster.cluster_id,
                        "role": cluster.role,
                        "feature_map": _json_unknown(cluster.feature_map),
                        "attribute_ids": _json_unknown(cluster.attribute_ids),
                        "accepted_command_ids": _json_unknown(cluster.accepted_command_ids),
                        "generated_command_ids": _json_unknown(cluster.generated_command_ids),
                        "revision": _json_unknown(cluster.revision),
                    }
                    for cluster in endpoint.clusters
                ],
            }
            for endpoint in inventory.endpoints
        ],
        "read_issues": [
            {
                "endpoint_id": issue.endpoint_id,
                "cluster_id": issue.cluster_id,
                "attribute_id": issue.attribute_id,
                "code": issue.code,
                "message": issue.message,
            }
            for issue in inventory.read_issues
        ],
    }


def inventory_from_dict(data: dict[str, object]) -> Inventory:
    context = _context_from_dict(data["backend_context"])
    endpoints = []
    for raw_endpoint in data.get("endpoints", []):
        endpoint = raw_endpoint
        if not isinstance(endpoint, Mapping):
            raise ValueError("endpoint entries must be objects")
        device_types = tuple(
            DeviceType(
                id=_as_int(item["id"], bits=32, field_name="device_type.id"),
                revision=_as_int(item["revision"], bits=16, field_name="device_type.revision"),
            )
            for item in endpoint.get("device_types", [])
        )
        clusters = []
        for item in endpoint.get("clusters", []):
            if not isinstance(item, Mapping):
                raise ValueError("cluster entries must be objects")
            clusters.append(
                ClusterCapability(
                    cluster_id=_as_int(item["cluster_id"], bits=32, field_name="cluster_id"),
                    role=str(item["role"]),
                    feature_map=(None if _unknown(item.get("feature_map", UNKNOWN)) is None else
                                 _as_int(_unknown(item["feature_map"]), bits=32, field_name="feature_map")),
                    attribute_ids=_id_list(item.get("attribute_ids", UNKNOWN), field_name="attribute_ids"),
                    accepted_command_ids=_id_list(
                        item.get("accepted_command_ids", UNKNOWN), field_name="accepted_command_ids"
                    ),
                    generated_command_ids=_id_list(
                        item.get("generated_command_ids", UNKNOWN), field_name="generated_command_ids"
                    ),
                    revision=(None if _unknown(item.get("revision", UNKNOWN)) is None else
                              _as_int(_unknown(item["revision"]), bits=16, field_name="revision")),
                )
            )
        endpoints.append(
            Endpoint(
                endpoint_id=_as_int(endpoint["endpoint_id"], bits=16, field_name="endpoint_id"),
                device_types=device_types,
                parts_list=_id_list(endpoint.get("parts_list", UNKNOWN), field_name="parts_list"),
                clusters=tuple(clusters),
            )
        )
    issues = []
    for item in data.get("read_issues", []):
        if not isinstance(item, Mapping):
            raise ValueError("read_issue entries must be objects")
        issues.append(
            ReadIssue(
                code=str(item["code"]),
                message=str(item["message"]),
                endpoint_id=(None if item.get("endpoint_id") is None else
                             _as_int(item["endpoint_id"], bits=16, field_name="endpoint_id")),
                cluster_id=(None if item.get("cluster_id") is None else
                            _as_int(item["cluster_id"], bits=32, field_name="cluster_id")),
                attribute_id=(None if item.get("attribute_id") is None else
                              _as_int(item["attribute_id"], bits=32, field_name="attribute_id")),
            )
        )
    captured_at = datetime.fromisoformat(str(data["captured_at"]))
    return Inventory(
        schema_version=str(data["schema_version"]),
        backend_context=context,
        captured_at=captured_at,
        endpoints=tuple(endpoints),
        read_issues=tuple(issues),
    )


def _items_by_endpoint(data: object, *, label: str) -> dict[int, Mapping[str, Any]]:
    """Accept `{endpoints: [...]}`, an endpoint list, or an ID-keyed endpoint map."""
    if data is None or data == UNKNOWN:
        return {}
    if isinstance(data, Mapping):
        data = data.get("endpoints", data)
        if isinstance(data, Mapping):
            result = {}
            for key, value in data.items():
                if not isinstance(value, Mapping):
                    raise ValueError(f"{label} endpoint values must be objects")
                result[_as_int(int(key), bits=16, field_name="endpoint_id")] = value
            return result
    if not isinstance(data, (list, tuple)):
        raise ValueError(f"{label} data must contain endpoint objects")
    result = {}
    for item in data:
        if not isinstance(item, Mapping) or "endpoint_id" not in item:
            raise ValueError(f"{label} entries must have endpoint_id")
        endpoint_id = _as_int(item["endpoint_id"], bits=16, field_name="endpoint_id")
        if endpoint_id in result:
            raise ValueError(f"duplicate {label} endpoint_id {endpoint_id}")
        result[endpoint_id] = item
    return result


def _descriptor_device_types(value: Any) -> tuple[DeviceType, ...]:
    value = _unknown(value)
    if value is None:
        return ()
    if not isinstance(value, (list, tuple)):
        raise ValueError("device_types must be an array")
    return tuple(
        DeviceType(
            id=_as_int(item["id"], bits=32, field_name="device_type.id"),
            revision=_as_int(item["revision"], bits=16, field_name="device_type.revision"),
        )
        for item in value
    )


def _descriptor_clusters(endpoint_data: Mapping[str, Any]) -> list[tuple[int, str]]:
    """Derive cluster roles only from Descriptor ServerList and ClientList."""
    result = []
    for field_name, role in (("server_list", "server"), ("client_list", "client")):
        values = _unknown(endpoint_data.get(field_name))
        if values is None:
            continue
        if not isinstance(values, (list, tuple)):
            raise ValueError(f"{field_name} must be an array or unknown")
        result.extend((_as_int(item, bits=32, field_name=field_name), role) for item in values)
    return result


def build_inventory(
    context: BackendContext,
    descriptor_data: object,
    capability_data: object,
    read_issues: list[ReadIssue],
) -> Inventory:
    """Build endpoint topology from Descriptor values and merge observed capabilities.

    Descriptor data uses endpoint objects with `endpoint_id`, optional `device_types`,
    `parts_list`, `server_list`, and `client_list`. Capability data uses endpoint
    objects with `endpoint_id` and `clusters`; cluster fields match ClusterCapability.
    Either source may be an ID-keyed mapping or an object containing `endpoints`.
    Missing/unread values remain `unknown` (represented internally as None).
    """
    descriptors = _items_by_endpoint(descriptor_data, label="descriptor")
    capabilities = _items_by_endpoint(capability_data, label="capability")
    endpoint_ids = sorted(set(descriptors) | set(capabilities))
    endpoints = []

    for endpoint_id in endpoint_ids:
        descriptor = descriptors.get(endpoint_id, {})
        capability_endpoint = capabilities.get(endpoint_id, {})
        descriptor_roles = _descriptor_clusters(descriptor)
        cluster_roles = {cluster_id: role for cluster_id, role in descriptor_roles}
        clusters_by_id: dict[tuple[int, str], ClusterCapability] = {}
        raw_clusters = capability_endpoint.get("clusters", [])
        if raw_clusters == UNKNOWN or raw_clusters is None:
            raw_clusters = []
        if not isinstance(raw_clusters, (list, tuple)):
            raise ValueError("clusters must be an array")
        for raw in raw_clusters:
            if not isinstance(raw, Mapping):
                raise ValueError("cluster entries must be objects")
            cluster_id = _as_int(raw["cluster_id"], bits=32, field_name="cluster_id")
            role = str(raw.get("role", cluster_roles.get(cluster_id, UNKNOWN)))
            key = (cluster_id, role)
            clusters_by_id[key] = ClusterCapability(
                cluster_id=cluster_id,
                role=role,
                feature_map=(None if _unknown(raw.get("feature_map", UNKNOWN)) is None else
                             _as_int(_unknown(raw["feature_map"]), bits=32, field_name="feature_map")),
                attribute_ids=_id_list(raw.get("attribute_ids", UNKNOWN), field_name="attribute_ids"),
                accepted_command_ids=_id_list(
                    raw.get("accepted_command_ids", UNKNOWN), field_name="accepted_command_ids"
                ),
                generated_command_ids=_id_list(
                    raw.get("generated_command_ids", UNKNOWN), field_name="generated_command_ids"
                ),
                revision=(None if _unknown(raw.get("revision", UNKNOWN)) is None else
                          _as_int(_unknown(raw["revision"]), bits=16, field_name="revision")),
            )
        # Preserve Descriptor cluster roles when a capability read failed or omitted the cluster.
        for cluster_id, role in descriptor_roles:
            clusters_by_id.setdefault((cluster_id, role), ClusterCapability(cluster_id, role))

        raw_device_types = descriptor.get("device_types", UNKNOWN)
        endpoints.append(
            Endpoint(
                endpoint_id=endpoint_id,
                device_types=_descriptor_device_types(raw_device_types),
                parts_list=_id_list(descriptor.get("parts_list", UNKNOWN), field_name="parts_list"),
                clusters=tuple(sorted(clusters_by_id.values(), key=lambda item: (item.cluster_id, item.role))),
            )
        )

    return Inventory(
        schema_version=SCHEMA_VERSION,
        backend_context=context,
        captured_at=datetime.now(timezone.utc),
        endpoints=tuple(endpoints),
        read_issues=tuple(read_issues),
    )
