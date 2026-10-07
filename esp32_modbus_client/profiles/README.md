# Gateway profiles

Copy a profile to `../data/config.json` and run `python -m platformio run -t uploadfs` to flash LittleFS without rebuilding firmware.

- `simulator.json` — matches root `plc_simulator.py` (HR0–3, Coil0–2).
- `main_plc_simulator.json` — Main PLC Modbus TCP `:1502`, float32 tags @ 40035–40051.
