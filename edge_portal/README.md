# Edge Flasher Portal

پورتال FastAPI + SQLite برای ذخیره پروفایل `config.json` و فلش ESP32 از مرورگر (Web Serial + esptool-js).

## پیش‌نیاز

- Python 3.10+
- Chrome یا Edge (HTTPS یا localhost)
- برای فلش firmware: باینری‌ها در `firmware/v1/` (با اسکریپت export)
- برای فلش LittleFS از مرورگر: `mklittlefs` در PATH سرور (اختیاری — fallback: `uploadfs`)

## نصب و اجرا

```powershell
cd edge_portal
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
uvicorn app.main:app --reload --host 0.0.0.0 --port 8080
```

مرورگر: `http://127.0.0.1:8080` (Web Serial روی localhost مجاز است).

Production: پشت nginx با **HTTPS**؛ SQLite در `edge_portal/data/edge_portal.db` (backup دوره‌ای).

## Export firmware بعد از بیلد PlatformIO

```powershell
cd E:\workspace3\aurdoino-sim_plc
python -m platformio run -d esp32_modbus_client
python scripts\export_edge_firmware.py
```

فایل‌های `.bin` در `edge_portal/firmware/v1/` قرار می‌گیرند (در gitignore).

## متغیرهای محیط (اختیاری)

| متغیر | پیش‌فرض |
|--------|---------|
| `EDGE_DATABASE_URL` | `sqlite:///.../data/edge_portal.db` |
| `EDGE_LITTLEFS_SIZE` | `458752` (0x70000) |
| `EDGE_MKLITTLEFS_BIN` | `mklittlefs` |

## API خلاصه

- `GET/POST /api/profiles` — CRUD پروفایل
- `POST /api/config/validate` — اعتبارسنجی schema v2
- `POST /api/config/build-littlefs` — ساخت image (نیاز mklittlefs)
- `GET /api/firmware/v1/manifest.json` — آدرس فلش

## امنیت

- رمز WiFi/MQTT داخل JSON ذخیره می‌شود؛ `data/` را commit نکنید.
- برای دسترسی محدود: nginx basic auth یا VPN.
