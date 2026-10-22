#include <Arduino.h>
#include <Preferences.h>

#include "board_config.h"
#include "config_store.h"
#include "modbus_poll.h"
#include "mqtt_publish.h"
#include "mqtt_command.h"
#include "led_status.h"

static GatewayConfig g_cfg;

static void clearLegacyConfigModeFlag() {
  Preferences prefs;
  prefs.begin("plcgw", false);
  prefs.remove("force_cfg");
  prefs.end();
}

static void printBootSummary(const GatewayConfig *cfg) {
  Serial.println("MODE=WORK (LittleFS config.json only — no setup AP/UI)");
  if (!cfg) {
    return;
  }
  Serial.printf("Profile: %s | PLC: %s:%u unit=%u | tags=%u | poll=%lums\n", cfg->profile_name,
                cfg->plc_host, cfg->plc_port, cfg->plc_unit_id, cfg->tag_count,
                (unsigned long)cfg->poll_interval_ms);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("ESP32 PLC Gateway");

  if (!configStoreInit()) {
    Serial.println("LittleFS mount failed");
  }

  clearLegacyConfigModeFlag();

  ledStatusBegin();
  ledStatusSetWorkMode(true);

  char err[160];
  while (!configStoreLoad(g_cfg, err, sizeof(err))) {
    Serial.printf("Config invalid (%s) — fix esp32_modbus_client/data/config.json then uploadfs\n",
                  err);
    delay(5000);
  }

  modbusPollBegin(g_cfg);
  mqttPublishBegin(g_cfg);
  printBootSummary(&g_cfg);
}

void loop() {
  ledStatusTick();

  if (modbusPollCycle()) {
    modbusPollPrintValues();
    mqttPublishTelemetry();
  } else {
    Serial.println("[Poll] retry in 3s...");
    delay(3000);
  }

  modbusPollTask();
  mqttPublishTask();
  const uint8_t handled = mqttCommandProcess();
  if (handled > 0 || mqttCommandQueued() > 0) {
    // Commands pending: go straight to the next pass instead of sleeping a whole poll interval
    return;
  }
  delay(g_cfg.poll_interval_ms > 0 ? g_cfg.poll_interval_ms : 1000);
}
