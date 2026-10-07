#pragma once

#include "gateway_config.h"

// ── TagReading ─────────────────────────────────────────────────────────
// Snapshot of one tag value after a successful poll cycle.
// Used by mqtt_publish to build the telemetry payload.
struct TagReading {
  bool valid;       // false = tag was not read in last cycle
  bool is_bool;     // true for Coil / Bool holding register
  bool bool_val;    // valid only when is_bool == true
  float float_val;  // numeric value (scale already applied)
};

class ModbusIP;

bool modbusPollBegin(const GatewayConfig &cfg);

void modbusPollSetConfig(const GatewayConfig &cfg);

bool modbusPollCycle();

void modbusPollPrintValues();

void modbusPollTask();

bool modbusPollCheckBootConfigRequest();

ModbusIP &modbusPollGetClient();

// ── Value accessors for MQTT publisher ────────────────────────────────
uint8_t      modbusPollGetTagCount();
const char  *modbusPollGetTagId(uint8_t idx);
bool         modbusPollGetTagReading(uint8_t idx, TagReading &r);
