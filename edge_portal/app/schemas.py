from datetime import datetime
from typing import Any

from pydantic import BaseModel, Field


class ValidateResult(BaseModel):
    ok: bool
    errors: list[str] = Field(default_factory=list)


class GatewayConfigBody(BaseModel):
    """Raw config.json object."""

    model_config = {"extra": "allow"}

    schema_version: int = 2
    profile_name: str = "edge"
    wifi: dict[str, Any] = Field(default_factory=dict)
    plc: dict[str, Any] = Field(default_factory=dict)
    poll: dict[str, Any] = Field(default_factory=lambda: {"interval_ms": 400})
    mqtt: dict[str, Any] = Field(default_factory=dict)
    tags: list[dict[str, Any]] = Field(default_factory=list)


class ProfileCreate(BaseModel):
    name: str = Field(min_length=1, max_length=128)
    config: dict[str, Any]


class ProfileUpdate(BaseModel):
    name: str | None = Field(default=None, min_length=1, max_length=128)
    config: dict[str, Any] | None = None


class ProfileSummary(BaseModel):
    id: int
    name: str
    created_at: datetime
    updated_at: datetime

    model_config = {"from_attributes": True}


class ProfileDetail(ProfileSummary):
    config: dict[str, Any]


class FlashLogCreate(BaseModel):
    profile_id: int | None = None
    firmware_version: str = "v1"
    success: bool = True


class FlashLogOut(BaseModel):
    id: int
    profile_id: int | None
    firmware_version: str
    success: bool
    created_at: datetime

    model_config = {"from_attributes": True}
