#pragma once

#include <Arduino.h>

/** Onboard LED (GPIO2 on most ESP32-DevKitC). Set LED_ACTIVE_LOW if inverted. */
void ledStatusBegin();

/** true = running (LED solid on). */
void ledStatusSetWorkMode(bool workMode);

/** Call often from loop(); non-blocking blink in CONFIG. */
void ledStatusTick();
