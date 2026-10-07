#include "led_status.h"

#include "board_config.h"

static bool s_workMode = false;
static bool s_ledOn = false;
static uint32_t s_lastToggleMs = 0;

static void writeLed(bool on) {
#if LED_ACTIVE_LOW
  digitalWrite(STATUS_LED_PIN, on ? LOW : HIGH);
#else
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
#endif
  s_ledOn = on;
}

void ledStatusBegin() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  writeLed(false);
  s_lastToggleMs = millis();
}

void ledStatusSetWorkMode(bool workMode) {
  s_workMode = workMode;
  if (workMode) {
    writeLed(true);
  } else {
    s_lastToggleMs = millis();
  }
}

void ledStatusTick() {
  if (s_workMode) {
    if (!s_ledOn) {
      writeLed(true);
    }
    return;
  }
  const uint32_t now = millis();
  if (now - s_lastToggleMs >= LED_CONFIG_BLINK_MS) {
    s_lastToggleMs = now;
    writeLed(!s_ledOn);
  }
}
