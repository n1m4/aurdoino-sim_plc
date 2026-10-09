from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

from app.db import init_db
from app.routers import config, firmware, profiles
from app.settings import settings


@asynccontextmanager
async def lifespan(_app: FastAPI):
    init_db()
    yield


app = FastAPI(title="Edge Flasher Portal", lifespan=lifespan)

templates = Jinja2Templates(directory=str(settings.static_dir / "templates"))

app.include_router(profiles.router)
app.include_router(config.router)
app.include_router(firmware.router)

if settings.static_dir.is_dir():
    app.mount("/static", StaticFiles(directory=str(settings.static_dir)), name="static")


@app.get("/", response_class=HTMLResponse)
def index(request: Request) -> HTMLResponse:
    return templates.TemplateResponse(request, "index.html")


@app.get("/flasher", response_class=HTMLResponse)
def flasher_page(request: Request) -> HTMLResponse:
    return templates.TemplateResponse(request, "flasher.html")


# Ensure tables exist when app is imported (e.g. TestClient without lifespan)
init_db()
