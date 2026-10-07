#include <Arduino.h>

#include "app_mode.h"
#include "board_config.h"
#include "config_store.h"
#include "modbus_poll.h"
#include "led_status.h"
#include "web_config.h"

static AppRunMode g_mode = AppRunMode::Config;
static GatewayConfig g_workCfg;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("ESP32 PLC Gateway — Phase 1.5");

  if (!configStoreInit()) {
    Serial.println("LittleFS mount failed");
  }

  ledStatusBegin();

  g_mode = appModeResolveAtBoot(&g_workCfg);

  if (g_mode == AppRunMode::Work) {
    ledStatusSetWorkMode(true);
    modbusPollBegin(g_workCfg);
    appModePrintSummary(AppRunMode::Work, &g_workCfg);
    Serial.println("LED: solid = WORK");
  } else {
    ledStatusSetWorkMode(false);
    webConfigBegin();
    appModePrintSummary(AppRunMode::Config, nullptr);
    Serial.println("LED: blink = CONFIG");
  }
}

void loop() {
  if (g_mode == AppRunMode::Config) {
    webConfigLoop();
    ledStatusTick();
    if (webConfigTimedOut()) {
      Serial.println("CONFIG timeout -> reboot");
      webConfigRebootToWork();
    }
    delay(10);
    return;
  }

  ledStatusTick();
  modbusPollCheckBootConfigRequest();

  if (modbusPollCycle()) {
    modbusPollPrintValues();
  } else {
    Serial.println("[Poll] retry in 3s...");
    delay(3000);
  }

  modbusPollTask();
  delay(g_workCfg.poll_interval_ms > 0 ? g_workCfg.poll_interval_ms : 1000);
}
