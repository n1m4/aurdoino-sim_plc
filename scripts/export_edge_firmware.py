#!/usr/bin/env python3
"""Copy PlatformIO build artifacts into edge_portal/firmware/v1/ and refresh manifest offsets."""

from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUILD = REPO / "esp32_modbus_client" / ".pio" / "build" / "esp32dev"
OUT = REPO / "edge_portal" / "firmware" / "v1"

COPY_MAP = {
    "bootloader.bin": BUILD / "bootloader.bin",
    "partition-table.bin": BUILD / "partitions.bin",
    "firmware.bin": BUILD / "firmware.bin",
}


def parse_partition_csv(path: Path) -> dict[str, tuple[int, int]]:
    """Return name -> (offset, size) for data/app partitions."""
    out: dict[str, tuple[int, int]] = {}
    if not path.is_file():
        return out
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split(",")]
        if len(parts) < 5:
            continue
        name, ptype, subtype, offset, size = parts[:5]
        off = int(offset, 0) if offset.startswith("0x") else int(offset)
        sz = int(size, 0) if size.startswith("0x") else int(size)
        out[name.lower()] = (off, sz)
        key = subtype.lower()
        if key not in out:
            out[key] = (off, sz)
    return out


def find_partitions_csv() -> Path | None:
    candidates = list(BUILD.glob("*.csv")) + list((REPO / "esp32_modbus_client").glob("partitions*.csv"))
    for p in candidates:
        if p.is_file():
            return p
    return None


def main() -> int:
    missing = [name for name, src in COPY_MAP.items() if not src.is_file()]
    if missing:
        print("Build outputs missing. Run first:", file=sys.stderr)
        print("  python -m platformio run -d esp32_modbus_client", file=sys.stderr)
        print("Missing:", ", ".join(missing), file=sys.stderr)
        return 1

    OUT.mkdir(parents=True, exist_ok=True)
    for name, src in COPY_MAP.items():
        shutil.copy2(src, OUT / name)
        print(f"copied {name}")

    manifest_path = OUT / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    parts = parse_partition_csv(find_partitions_csv() or Path())
    littlefs_off, littlefs_sz = parts.get("littlefs", parts.get("spiffs", (0x190000, 0x70000)))

    manifest["files"] = [
        {"path": "bootloader.bin", "offset": 0x1000},
        {"path": "partition-table.bin", "offset": 0x8000},
        {"path": "firmware.bin", "offset": 0x10000},
    ]
    manifest["littlefs"] = {"offset": littlefs_off, "size": littlefs_sz}

    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"updated {manifest_path}")
    print(f"littlefs offset=0x{littlefs_off:X} size=0x{littlefs_sz:X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
