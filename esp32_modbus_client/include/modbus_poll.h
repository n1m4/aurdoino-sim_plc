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

ModbusIP &modbusPollGetClient();

// ── Value accessors for MQTT publisher ────────────────────────────────
uint8_t      modbusPollGetTagCount();
const char  *modbusPollGetTagId(uint8_t idx);
/**
 * Returns the MQTT telemetry key for tag idx:
 *   - vps_name if set (schema v2, = Tag.name on VPS)
 *   - falls back to id (schema v1 / local name)
 *
 * This is the key that must appear in the "tags" object of every telemetry
 * payload and must match Tag.name in the VPS database.
 */
const char  *modbusPollGetTagMqttKey(uint8_t idx);
bool         modbusPollGetTagReading(uint8_t idx, TagReading &r);

// ── Modbus Write API (for MQTT command handler) ────────────────────────

/** Result codes returned by modbusPollWriteTag(). */
enum class WriteResult : uint8_t {
  OK,
  TagNotFound,        // tag_uuid not in config
  ReadOnly,           // tag.access == Read
  TypeNotSupported,   // string or unsupported data_type
  ModbusError,        // PLC unreachable or transaction timeout
};

/** Value received from VPS command — pass both bool and numeric. */
struct CommandWriteValue {
  bool   is_bool;
  bool   bool_val;
  double num_val;   // used when is_bool == false
};

/** Human-readable string for a WriteResult (nullptr for OK). */
const char *writeResultToString(WriteResult r);

/**
 * Resolve tag_uuid → TagDef → write engineering value to PLC via Modbus.
 *
 * Conversion:  raw = (eng - offset) / scale
 *
 * @param tag_uuid  UUID from the MQTT command's tag_id field
 * @param val       Engineering value (bool or numeric)
 * @return          WriteResult::OK on success, error code otherwise
 */
WriteResult modbusPollWriteTag(const char *tag_uuid, const CommandWriteValue &val);
