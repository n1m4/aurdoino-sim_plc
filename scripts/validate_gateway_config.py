#!/usr/bin/env python3
"""Validate esp32 gateway config.json (schema v1)."""

import argparse
import ipaddress
import json
import sys
from pathlib import Path

MAX_TAGS = 96
MAX_READ_BLOCKS = 16
MAX_MODBUS_READ = 125
PLANNER_MAX_GAP = 4
IMPLEMENTED_KINDS = {"holding", "coil"}


def modbus_4x_to_offset(raw: int) -> int:
    if 40001 <= raw <= 49999:
        return raw - 40001
    return raw


def register_span(tag: dict) -> int:
    dt = tag.get("datatype") or tag.get("data_type") or "uint16"
    if tag.get("kind") != "holding":
        return 1
    if dt in ("int32", "uint32", "float32"):
        return 2
    return 1


def merge_blocks(addresses: list[int]) -> list[tuple[int, int]]:
    """Same packing as the firmware planner: unique addresses, small gaps bridged (PLANNER_MAX_GAP)."""
    if not addresses:
        return []
    addresses = sorted(set(addresses))
    blocks: list[tuple[int, int]] = []
    start = addresses[0]
    end = addresses[0]
    for addr in addresses[1:]:
        if (addr - end - 1) <= PLANNER_MAX_GAP and (addr - start + 1) <= MAX_MODBUS_READ:
            end = addr
            continue
        blocks.append((start, end - start + 1))
        start = end = addr
    blocks.append((start, end - start + 1))
    return blocks


def validate(data: dict) -> list[str]:
    errors: list[str] = []
    if data.get("schema_version") != 1:
        errors.append("schema_version must be 1")

    wifi = data.get("wifi") or {}
    if not wifi.get("ssid"):
        errors.append("wifi.ssid required")

    plc = data.get("plc") or {}
    if plc.get("protocol", "modbus_tcp") != "modbus_tcp":
        errors.append("only modbus_tcp supported")
    host = plc.get("host", "")
    try:
        ipaddress.IPv4Address(host)
    except ipaddress.AddressValueError:
        errors.append("plc.host must be valid IPv4")
    port = plc.get("port", 5020)
    if not (1 <= int(port) <= 65535):
        errors.append("plc.port invalid")
    unit = plc.get("unit_id", 1)
    if not (1 <= int(unit) <= 247):
        errors.append("plc.unit_id must be 1-247")

    tags = data.get("tags")
    if not isinstance(tags, list) or not tags:
        errors.append("tags array required (min 1)")
        return errors

    if len(tags) > MAX_TAGS:
        errors.append(f"too many tags (max {MAX_TAGS})")

    seen: set[str] = set()
    by_kind: dict[str, list[int]] = {"holding": [], "coil": []}

    for i, t in enumerate(tags):
        tid = (t.get("id") or "").strip()
        if not tid:
            errors.append(f"tags[{i}].id required")
            continue
        if tid in seen:
            errors.append(f"duplicate tag id: {tid}")
        seen.add(tid)
        kind = t.get("kind")
        if kind not in IMPLEMENTED_KINDS:
            errors.append(f"tag {tid}: only holding/coil implemented")
            continue
        raw = int(t.get("address", 0))
        if kind == "holding":
            base = modbus_4x_to_offset(raw)
        elif kind == "coil" and 1 <= raw <= 9999:
            base = raw - 1
        else:
            base = raw
        span = register_span(t)
        for r in range(base, base + span):
            by_kind[kind].append(r)

    block_count = 0
    for kind, addrs in by_kind.items():
        blocks = merge_blocks(addrs)
        block_count += len(blocks)
        for start, count in blocks:
            if count > MAX_MODBUS_READ:
                errors.append(f"{kind} block @{start} count {count} > {MAX_MODBUS_READ}")
    if block_count > MAX_READ_BLOCKS:
        errors.append(f"too many read blocks after merge (max {MAX_READ_BLOCKS})")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("config", type=Path, help="Path to config.json")
    args = parser.parse_args()
    data = json.loads(args.config.read_text(encoding="utf-8"))
    errors = validate(data)
    if errors:
        for e in errors:
            print(e, file=sys.stderr)
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
