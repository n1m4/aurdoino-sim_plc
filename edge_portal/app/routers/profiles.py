import json
from typing import Any

from fastapi import APIRouter, Depends, HTTPException
from fastapi.responses import Response
from sqlalchemy.orm import Session

from app.db import get_db
from app.models import FlashLog, Profile
from app.schemas import (
    FlashLogCreate,
    FlashLogOut,
    ProfileCreate,
    ProfileDetail,
    ProfileSummary,
    ProfileUpdate,
)
from app.validate import validate_config

router = APIRouter(prefix="/api/profiles", tags=["profiles"])


def _config_to_str(config: dict[str, Any]) -> str:
    return json.dumps(config, ensure_ascii=False, indent=2)


def _profile_detail(p: Profile) -> ProfileDetail:
    return ProfileDetail(
        id=p.id,
        name=p.name,
        created_at=p.created_at,
        updated_at=p.updated_at,
        config=json.loads(p.config_json),
    )


@router.get("", response_model=list[ProfileSummary])
def list_profiles(db: Session = Depends(get_db)) -> list[Profile]:
    return db.query(Profile).order_by(Profile.updated_at.desc()).all()


@router.post("", response_model=ProfileDetail, status_code=201)
def create_profile(body: ProfileCreate, db: Session = Depends(get_db)) -> ProfileDetail:
    errors = validate_config(body.config)
    if errors:
        raise HTTPException(status_code=400, detail={"errors": errors})
    row = Profile(name=body.name.strip(), config_json=_config_to_str(body.config))
    db.add(row)
    db.commit()
    db.refresh(row)
    return _profile_detail(row)


@router.get("/{profile_id}", response_model=ProfileDetail)
def get_profile(profile_id: int, db: Session = Depends(get_db)) -> ProfileDetail:
    row = db.get(Profile, profile_id)
    if not row:
        raise HTTPException(status_code=404, detail="profile not found")
    return _profile_detail(row)


@router.put("/{profile_id}", response_model=ProfileDetail)
def update_profile(
    profile_id: int, body: ProfileUpdate, db: Session = Depends(get_db)
) -> ProfileDetail:
    row = db.get(Profile, profile_id)
    if not row:
        raise HTTPException(status_code=404, detail="profile not found")
    if body.name is not None:
        row.name = body.name.strip()
    if body.config is not None:
        errors = validate_config(body.config)
        if errors:
            raise HTTPException(status_code=400, detail={"errors": errors})
        row.config_json = _config_to_str(body.config)
    db.commit()
    db.refresh(row)
    return _profile_detail(row)


@router.delete("/{profile_id}", status_code=204)
def delete_profile(profile_id: int, db: Session = Depends(get_db)) -> None:
    row = db.get(Profile, profile_id)
    if not row:
        raise HTTPException(status_code=404, detail="profile not found")
    db.delete(row)
    db.commit()


@router.get("/{profile_id}/config.json")
def download_config(profile_id: int, db: Session = Depends(get_db)) -> Response:
    row = db.get(Profile, profile_id)
    if not row:
        raise HTTPException(status_code=404, detail="profile not found")
    return Response(
        content=row.config_json + "\n",
        media_type="application/json",
        headers={"Content-Disposition": 'attachment; filename="config.json"'},
    )


@router.post("/flash-log", response_model=FlashLogOut, status_code=201)
def create_flash_log(body: FlashLogCreate, db: Session = Depends(get_db)) -> FlashLog:
    if body.profile_id is not None and not db.get(Profile, body.profile_id):
        raise HTTPException(status_code=404, detail="profile not found")
    row = FlashLog(
        profile_id=body.profile_id,
        firmware_version=body.firmware_version,
        success=body.success,
    )
    db.add(row)
    db.commit()
    db.refresh(row)
    return row
