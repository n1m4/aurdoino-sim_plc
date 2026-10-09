import json
from pathlib import Path

from fastapi import APIRouter, HTTPException
from fastapi.responses import FileResponse

from app.settings import settings

router = APIRouter(prefix="/api/firmware", tags=["firmware"])


def _version_dir(version: str) -> Path:
    return settings.firmware_dir / version


@router.get("/{version}/manifest.json")
def get_manifest(version: str) -> dict:
    path = _version_dir(version) / "manifest.json"
    if not path.is_file():
        raise HTTPException(status_code=404, detail="manifest not found — run export script after build")
    return json.loads(path.read_text(encoding="utf-8"))


@router.get("/{version}/{filename}")
def get_firmware_file(version: str, filename: str) -> FileResponse:
    allowed = {"bootloader.bin", "partition-table.bin", "firmware.bin", "littlefs.bin"}
    if filename not in allowed:
        raise HTTPException(status_code=404, detail="unknown file")
    path = _version_dir(version) / filename
    if not path.is_file():
        raise HTTPException(status_code=404, detail=f"{filename} missing — build and export firmware")
    return FileResponse(path, media_type="application/octet-stream", filename=filename)
