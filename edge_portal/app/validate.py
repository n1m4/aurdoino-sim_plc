"""Validate gateway config.json (schema v2) — aligned with esp32_modbus_client firmware."""

from __future__ import annotations

import ipaddress
from typing import Any

MAX_TAGS = 96
MAX_READ_BLOCKS = 8
MAX_MODBUS_READ = 125
IMPLEMENTED_DT = {"bool", "int16", "uint16", "int32", "float32"}


def modbus_4x_to_offset(raw: int) -> int:
    if 40001 <= raw <= 49999:
        return raw - 40001
    return raw


def parse_address(raw: Any) -> int:
    if isinstance(raw, int):
        return raw
    if isinstance(raw, str):
        return int(raw.strip() or "0")
    raise ValueError("invalid address")


def infer_kind(tag: dict[str, Any], raw_addr: int, data_type: str) -> str:
    kind = (tag.get("kind") or "").strip().lower()
    if kind in ("holding", "coil"):
        return kind
    if 40000 <= raw_addr <= 49999:
        return "holding"
    if data_type == "bool" and raw_addr < 40000:
        return "coil"
    return "holding"


def register_span(kind: str, data_type: str) -> int:
    if kind == "coil":
        return 1
    if data_type in ("int32", "float32"):
        return 2
    return 1


def to_modbus_offset(kind: str, raw: int, data_type: str) -> int:
    if kind == "holding":
        return modbus_4x_to_offset(raw)
    if kind == "coil" and 1 <= raw <= 9999:
        return raw - 1
    return raw


def merge_blocks(addresses: list[int]) -> list[tuple[int, int]]:
    if not addresses:
        return []
    addresses = sorted(set(addresses))
    blocks: list[tuple[int, int]] = []
    start = addresses[0]
    end = addresses[0]
    for addr in addresses[1:]:
        if addr == end + 1 and (addr - start + 1) <= MAX_MODBUS_READ:
            end = addr
            continue
        blocks.append((start, end - start + 1))
        start = end = addr
    blocks.append((start, end - start + 1))
    return blocks


def tag_key(tag: dict[str, Any]) -> str:
    vps = (tag.get("vps_name") or "").strip()
    if vps:
        return vps
    return (tag.get("id") or "").strip()


def validate_config(data: dict[str, Any]) -> list[str]:
    errors: list[str] = []

    schema = data.get("schema_version")
    if schema not in (1, 2):
        errors.append("schema_version must be 1 or 2")

    wifi = data.get("wifi") or {}
    if not (wifi.get("ssid") or "").strip():
        errors.append("wifi.ssid required")

    plc = data.get("plc") or {}
    if plc.get("protocol", "modbus_tcp") != "modbus_tcp":
        errors.append("only modbus_tcp supported")
    host = (plc.get("host") or "").strip()
    try:
        ipaddress.IPv4Address(host)
    except ipaddress.AddressValueError:
        errors.append("plc.host must be valid IPv4")
    port = int(plc.get("port", 5020))
    if not (1 <= port <= 65535):
        errors.append("plc.port invalid")
    unit = int(plc.get("unit_id", 1))
    if not (1 <= unit <= 247):
        errors.append("plc.unit_id must be 1-247")

    tags = data.get("tags")
    if not isinstance(tags, list) or not tags:
        errors.append("tags array required (min 1)")
        return errors
    if len(tags) > MAX_TAGS:
        errors.append(f"too many tags (max {MAX_TAGS})")

    seen_keys: set[str] = set()
    by_kind: dict[str, list[int]] = {"holding": [], "coil": []}

    for i, t in enumerate(tags):
        if not isinstance(t, dict):
            errors.append(f"tags[{i}] must be object")
            continue
        key = tag_key(t)
        if not key:
            errors.append(f"tags[{i}]: vps_name or id required")
            continue
        if key in seen_keys:
            errors.append(f"duplicate tag name: {key}")
        seen_keys.add(key)

        dt = (t.get("data_type") or t.get("datatype") or "uint16").strip().lower()
        if dt not in IMPLEMENTED_DT:
            errors.append(f"tag {key}: unsupported data_type {dt}")
            continue

        try:
            raw = parse_address(t.get("address", 0))
        except ValueError:
            errors.append(f"tag {key}: invalid address")
            continue

        kind = infer_kind(t, raw, dt)
        if kind not in ("holding", "coil"):
            errors.append(f"tag {key}: only holding/coil implemented")
            continue

        base = to_modbus_offset(kind, raw, dt)
        span = register_span(kind, dt)
        for r in range(base, base + span):
            if base + span - 1 > 0xFFFF:
                errors.append(f"tag {key}: address out of range for ESP32")
                break
            by_kind[kind].append(r)

    block_count = 0
    for kind, addrs in by_kind.items():
        for start, count in merge_blocks(addrs):
            if count > MAX_MODBUS_READ:
                errors.append(f"{kind} block @{start} count {count} > {MAX_MODBUS_READ}")
        block_count += len(merge_blocks(addrs))
    if block_count > MAX_READ_BLOCKS:
        errors.append(f"too many read blocks after merge (max {MAX_READ_BLOCKS})")

    return errors
