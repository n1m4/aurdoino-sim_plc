from pathlib import Path

from pydantic_settings import BaseSettings, SettingsConfigDict

ROOT = Path(__file__).resolve().parent.parent


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="EDGE_", env_file=".env", extra="ignore")

    database_url: str = f"sqlite:///{ROOT / 'data' / 'edge_portal.db'}"
    firmware_dir: Path = ROOT / "firmware"
    static_dir: Path = ROOT / "static"
    littlefs_size: int = 0x70000  # must match partition; default 448KB
    mklittlefs_bin: str = "mklittlefs"


settings = Settings()
