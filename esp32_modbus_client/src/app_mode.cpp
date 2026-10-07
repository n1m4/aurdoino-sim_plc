#include "app_mode.h"

#include <Preferences.h>
#include <WiFi.h>

#include "board_config.h"
#include "config_store.h"

static const char *PREFS_NS = "plcgw";
static const char *KEY_FORCE_CONFIG = "force_cfg";

static bool isBootPinHeld() {
  return digitalRead(BOOT_PIN) == LOW;
}

static bool detectBootHoldAtStartup() {
  pinMode(BOOT_PIN, INPUT_PULLUP);
  if (!isBootPinHeld()) {
    return false;
  }
  const uint32_t start = millis();
  while (millis() - start < BOOT_HOLD_MS) {
    if (!isBootPinHeld()) {
      return false;
    }
    delay(10);
  }
  return isBootPinHeld();
}

void appModeRequestConfigOnNextBoot() {
  Preferences prefs;
  prefs.begin(PREFS_NS, false);
  prefs.putBool(KEY_FORCE_CONFIG, true);
  prefs.end();
}

static bool consumeForceConfigFlag() {
  Preferences prefs;
  prefs.begin(PREFS_NS, false);
  bool force = prefs.getBool(KEY_FORCE_CONFIG, false);
  if (force) {
    prefs.putBool(KEY_FORCE_CONFIG, false);
  }
  prefs.end();
  return force;
}

AppRunMode appModeResolveAtBoot(GatewayConfig *outCfg) {
  if (consumeForceConfigFlag()) {
    Serial.println("Boot: force CONFIG (NVS)");
    return AppRunMode::Config;
  }
  if (detectBootHoldAtStartup()) {
    Serial.println("Boot: BOOT held -> CONFIG");
    return AppRunMode::Config;
  }

  if (!outCfg) {
    Serial.println("Boot: internal error (null cfg) -> CONFIG");
    return AppRunMode::Config;
  }
  char err[160];
  if (!configStoreLoad(*outCfg, err, sizeof(err))) {
    Serial.printf("Boot: config invalid (%s) -> CONFIG\n", err);
    return AppRunMode::Config;
  }
  Serial.println("Boot: valid config -> WORK");
  return AppRunMode::Work;
}

bool appModeCheckBootHoldDuringRun() {
  static uint32_t holdStart = 0;
  pinMode(BOOT_PIN, INPUT_PULLUP);
  if (!isBootPinHeld()) {
    holdStart = 0;
    return false;
  }
  if (holdStart == 0) {
    holdStart = millis();
    return false;
  }
  if (millis() - holdStart >= BOOT_HOLD_MS) {
    Serial.println("BOOT held 3s -> reboot to CONFIG");
    appModeRequestConfigOnNextBoot();
    WiFi.disconnect(true);
    delay(200);
    ESP.restart();
    return true;
  }
  return false;
}

void appModePrintSummary(AppRunMode mode, const GatewayConfig *cfg) {
  if (mode == AppRunMode::Config) {
    Serial.println("MODE=CONFIG");
    Serial.printf("Setup AP: %s / %s -> http://192.168.4.1\n", AP_SSID, AP_PASSWORD);
    return;
  }
  Serial.println("MODE=WORK");
  if (!cfg) {
    return;
  }
  Serial.printf("Profile: %s | PLC: %s:%u unit=%u | tags=%u | poll=%lums\n", cfg->profile_name,
                cfg->plc_host, cfg->plc_port, cfg->plc_unit_id, cfg->tag_count,
                (unsigned long)cfg->poll_interval_ms);
}
