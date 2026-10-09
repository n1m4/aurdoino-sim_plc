import json
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from fastapi import APIRouter, HTTPException
from fastapi.responses import Response

from app.schemas import GatewayConfigBody, ValidateResult
from app.settings import settings
from app.validate import validate_config

router = APIRouter(prefix="/api/config", tags=["config"])


@router.post("/validate", response_model=ValidateResult)
def post_validate(body: dict[str, Any]) -> ValidateResult:
    errors = validate_config(body)
    return ValidateResult(ok=len(errors) == 0, errors=errors)


@router.post("/build-littlefs")
def build_littlefs(body: dict[str, Any]) -> Response:
    errors = validate_config(body)
    if errors:
        raise HTTPException(status_code=400, detail={"errors": errors})

    mklittlefs = shutil.which(settings.mklittlefs_bin)
    if not mklittlefs:
        raise HTTPException(
            status_code=503,
            detail={
                "message": "mklittlefs not found on server PATH",
                "fallback": "Download config.json and run: platformio run -t uploadfs",
            },
        )

    with tempfile.TemporaryDirectory() as tmp:
        data_dir = Path(tmp) / "data"
        data_dir.mkdir()
        config_path = data_dir / "config.json"
        config_path.write_text(json.dumps(body, ensure_ascii=False, indent=2), encoding="utf-8")
        out_bin = Path(tmp) / "littlefs.bin"
        cmd = [
            mklittlefs,
            "-c",
            str(data_dir),
            "-s",
            str(settings.littlefs_size),
            str(out_bin),
        ]
        try:
            subprocess.run(cmd, check=True, capture_output=True, text=True)
        except subprocess.CalledProcessError as exc:
            raise HTTPException(
                status_code=500,
                detail={"message": "mklittlefs failed", "stderr": exc.stderr},
            ) from exc
        payload = out_bin.read_bytes()

    return Response(
        content=payload,
        media_type="application/octet-stream",
        headers={"Content-Disposition": 'attachment; filename="littlefs.bin"'},
    )
