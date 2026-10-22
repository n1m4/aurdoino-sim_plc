# فاز ۱.۵: JSON روی Flash + Web UI + Modbus پویا

## خلاصه

- **`/config.json`** روی LittleFS: WiFi + PLC (Modbus TCP) + tags
- **MODE=WORK:** polling پویا (batched read)، بدون Web
- **MODE=CONFIG:** AP + Web فرم — BOOT 3s یا config نامعتبر

## Schema (v1)

نمونه: [`esp32_modbus_client/profiles/simulator.json`](../esp32_modbus_client/profiles/simulator.json)

| فیلد | توضیح |
|------|--------|
| `wifi.ssid` / `password` | STA در WORK |
| `plc.host` | IPv4 PC/PLC |
| `plc.port` | معمولاً 5020 برای simulator |
| `plc.unit_id` | 1 |
| `tags[]` | `id`, `kind` (holding/coil), `datatype` (bool/int16/uint16/int32/uint32/float32), `address` (4xxxx for holding), optional `scale`, `label` |

`int32` و `uint32` و `float32` هر کدام **۲ register** (Big-Endian، word بالا اول). `bool` روی holding = register غیرصفر.

محدودیت: 64 تگ، 8 بلوک read، هر بلوک ≤ 125 register/coil.

## CONFIG vs WORK

| ورود CONFIG | |
|-------------|--|
| نگه‌داشتن **BOOT** هنگام روشن (~3s) | |
| `config.json` missing / invalid | |
| نگه‌داشتن BOOT در WORK (~3s) → reboot | |

CONFIG: WiFi AP **`PLC-GW-Setup`** / رمز **`setup1234`** → **http://192.168.4.1**

**LED روی برد (معمولاً GPIO2):** چشمک‌زن = CONFIG، ثابت روشن = WORK. اگر برعکس بود در [`board_config.h`](../esp32_modbus_client/include/board_config.h) مقدار `LED_ACTIVE_LOW` را `1` کنید.

پس از **Save & Reboot** → WORK.

Timeout CONFIG: **15 دقیقه** → reboot.

## فلش firmware و config

```powershell
cd E:\workspace3\aurdoino-sim_plc\esp32_modbus_client
python -m pip install platformio   # once, inside .venv
python -m platformio run -t upload --upload-port COM5
python -m platformio run -t uploadfs
```

- **`upload`:** firmware
- **`uploadfs`:** فقط `data/config.json` → بدون rebuild

ویرایش profile در PC:

```powershell
copy profiles\simulator.json data\config.json
python ..\scripts\validate_gateway_config.py data\config.json
python -m platformio run -t uploadfs
```

## Simulator vs PLC واقعی

همان firmware؛ فقط در Web یا JSON:

- `plc.host` / `port` / `unit_id`
- لیست `tags` مطابق map PLC

Simulator: [`plc_simulator.py`](../plc_simulator.py) روی `0.0.0.0:5020`.

## Validate از PC

```powershell
python scripts\validate_gateway_config.py esp32_modbus_client\profiles\simulator.json
```

## Serial در WORK

```
MODE=WORK
Profile: simulator | PLC: 192.168.1.51:5020 unit=1 | tags=7 | poll=1000ms
--- tags ---
temp=25.200
weight=1015
...
```

## امنیت

- رمز WiFi روی flash **plaintext**
- AP password ثابت در firmware — دسترسی فیزیکی را محدود کنید

## چک‌لیست تست

1. حذف/خراب کردن config → AP + Web
2. Save با 7 تگ simulator → WORK + live values
3. Tag `counter` افزایش یابد
4. BOOT 3s در WORK → CONFIG، Modbus متوقف
5. `uploadfs` بدون `upload`
6. validate رد برای بلوک >125

## خارج از scope

VPS, MQTT, HTTP در WORK, Modbus Write, S7, OTA.
