# مرحله ۱: PLC Simulator → Modbus TCP → ESP32

هدف: اثبات مسیر **Simulator (Server) → Modbus TCP → ESP32 (Client) → Serial Monitor** روی شبکه محلی. بدون VPS، MQTT، HTTP یا Write از ESP32.

---

## ۱. ساختار Simulator چگونه پیدا شد؟

- فایل اصلی: [`plc_simulator.py`](../plc_simulator.py) در root پروژه.
- سرور: `StartAsyncTcpServer` (PyModbus async).
- حلقه شبیه‌سازی: `plc_loop()` هر ۰.۵ ثانیه HR و Coil را به‌روز می‌کند.
- README قبلی وجود نداشت؛ تنها کد اپلیکیشن همین Simulator است (+ `.venv`).

## ۲. IP و Port فعلی Modbus Server

| پارامتر | مقدار |
|---------|--------|
| Bind | `0.0.0.0` (همه interfaceها — ESP32 روی LAN) |
| Port | **5020** (عمداً نه 502 — بدون Admin روی Windows) |
| Unit ID | **1** |

ESP32 باید **IP LAN کامپیوتر** را بزند (از `ipconfig`)، نه `127.0.0.1`.

## ۳. Registerهای انتخاب‌شده برای تست

**Holding Registers (FC3), آدرس 0-based (`zero_mode=True`):**

| Tag | HR | معنی |
|-----|-----|------|
| Tag0 | 0 | دما × 10 |
| Tag1 | 1 | وزن (kg) |
| Tag2 | 2 | فشار × 10 (bar) |
| Tag3 | 3 | شمارنده loop (برای اثبات Live) |

**Coils (FC1):** Motor=0, Pump=1, Valve=2

## ۴. ESP32: محیط و Library

| مورد | انتخاب |
|------|--------|
| محیط | **PlatformIO** — پوشه [`esp32_modbus_client/`](../esp32_modbus_client/) |
| Board پیش‌فرض | `esp32dev` |
| Library | **emelianov/modbus-esp8266** (`ModbusIP_ESP8266.h`) — TCP client، سبک، بدون MQTT/HTTP |
| حالت | فقط **Read** (`readHreg`, `readCoil`) |

## ۵. فایل‌های ایجاد یا تغییر شده

| فایل | توضیح |
|------|--------|
| `plc_simulator.py` | `HOST=0.0.0.0`، hook اختیاری `PLC_SIM_FIXED_HR0` |
| `esp32_modbus_client/platformio.ini` | تعریف env و lib |
| `esp32_modbus_client/src/main.cpp` | WiFi + Modbus + Serial |
| `esp32_modbus_client/include/config.h` | SSID، IP سرور، پورت |
| `esp32_modbus_client/include/config.h.example` | نمونه تنظیمات |
| `scripts/verify_modbus_client.py` | تست Read از PC |
| `requirements.txt` | `pymodbus==3.7.4` |
| `docs/PHASE1_MODBUS_ESP32.md` | همین سند |

## ۶. اجرای Simulator

```powershell
cd E:\workspace3\aurdoino-sim_plc
.\.venv\Scripts\Activate.ps1
python plc_simulator.py
```

خروجی startup: `Modbus TCP : 0.0.0.0:5020` و جدول HR/Coil.

**Test 5 — ثابت نگه داشتن Tag0 (HR0):**

```powershell
$env:PLC_SIM_FIXED_HR0='100'
python plc_simulator.py
# سپس Simulator را متوقف و:
$env:PLC_SIM_FIXED_HR0='250'
python plc_simulator.py
```

**تست از PC قبل از ESP32:**

```powershell
python scripts\verify_modbus_client.py --host 127.0.0.1 --count 3
```

## ۷. آپلود Firmware روی ESP32

1. [`include/config.h`](../esp32_modbus_client/include/config.h) را ویرایش کنید: Wi-Fi و `MODBUS_SERVER_IP_*` (IP PC).
2. PlatformIO CLI یا extension VS Code/Cursor.
3. برد را با USB وصل کنید.

```powershell
cd E:\workspace3\aurdoino-sim_plc\esp32_modbus_client
pio run -t upload
```

با venv فعال، PlatformIO را یک‌بار نصب کنید (حتماً با `python -m pip`، نه فقط `pip` اگر به venv دیگری اشاره کند):

```powershell
python -m pip install platformio
python -m platformio run -t upload --upload-port COM5 -d esp32_modbus_client
```

## ۸. Serial Monitor

```powershell
pio device monitor -b 115200
```

Arduino IDE: پورت COM برد، 115200 baud.

## ۹. خروجی مورد انتظار

```
ESP32 Modbus TCP Client (read-only) — Phase 1
Connecting to WiFi...
WiFi connected
ESP32 IP: 192.168.1.50

Connecting to Modbus Server: 192.168.1.100:5020
Modbus connected
Tag0: 196 (Temp x10)
Tag1: 1015 (Weight kg)
Tag2: 39 (Pressure x10)
Tag3: 12 (Counter)
Coil0 Motor: OFF | Coil1 Pump: OFF | Coil2 Valve: CLOSE
--------------------
```

مقادیر Tag با شبیه‌سازی عوض می‌شوند؛ **Tag3** هر ثانیه تقریباً +۲ می‌شود (loop هر ۰.۵s).

## ۱۰. Troubleshooting

| مشکل | اقدام |
|------|--------|
| ESP32 به Modbus وصل نمی‌شود | IP PC را با `ipconfig` چک کنید؛ پورت **5020**؛ Unit ID **1** |
| فقط localhost کار می‌کند | Simulator باید روی `0.0.0.0` باشد (نسخه فعلی) |
| Windows Firewall | Inbound rule برای TCP **5020** روی شبکه Private |
| Wi-Fi | PC و ESP32 روی **یک SSID**؛ از guest network جدا استفاده نکنید |
| چند Simulator روی یک پورت | فقط یک instance؛ `Get-NetTCPConnection -LocalPort 5020` |
| Tag0 با `PLC_SIM_FIXED_HR0` درست نیست | Simulator قدیمی را kill کنید؛ env را در همان shell ست کنید |
| PyModbus | venv فعلی: **3.7.4** (`requirements.txt`) — نه 3.15.0 |

---

## نتایج تست انجام‌شده روی PC (Test 1, 4, 5)

| تست | نتیجه |
|-----|--------|
| Simulator روی `0.0.0.0:5020` | OK |
| `verify_modbus_client.py` سه خواندن پشت‌سرهم | Tag3: 12 → 13 → 14 (Live) |
| `PLC_SIM_FIXED_HR0=100` | Tag0=100 در verify |

| Build PlatformIO (`python -m platformio run`) | OK |
| Upload به COM5 (CP210x) | OK — پس از پر کردن `config.h`، WiFi/Modbus را در Serial Monitor ببینید |

تست ESP32 کامل (WiFi + Modbus + Tagها) پس از تنظیم `WIFI_SSID` / `WIFI_PASS` و IP PC در [`config.h`](../esp32_modbus_client/include/config.h) انجام دهید.

---

## مرحله بعد

- **فاز ۱.۵:** [`PHASE1_5_CONFIG_WEB.md`](PHASE1_5_CONFIG_WEB.md) — JSON + Web UI + Modbus پویا
- **بعداً:** ESP32 → VPS (MQTT/HTTP و …)
