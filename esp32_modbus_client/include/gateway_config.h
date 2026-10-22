#pragma once

#include <Arduino.h>
#include "board_config.h"

enum class TagKind : uint8_t { Holding, Coil, Input, Discrete, Unknown };

// Uint32 is appended (not inserted) so existing numeric values never change.
enum class TagDataType : uint8_t { Bool, Int16, Uint16, Int32, Float32, Uint32 };

/** Write access level for a tag. Default = Read (backward-compatible). */
enum class TagAccess : uint8_t { Read, Write, ReadWrite };

struct TagDef {
  char id[MAX_TAG_ID_LEN];           // local tag name (optional in v2, used for serial debug)
  char vps_name[MAX_VPS_NAME_LEN];   // Tag.name on VPS — used as MQTT telemetry key
  char unit[MAX_UNIT_LEN];           // engineering unit, e.g. "%", "°C" (informational)
  char tag_uuid[MAX_TAG_UUID_LEN];   // UUID for command routing (empty = not commandable)
  char label[MAX_LABEL_LEN];
  TagKind kind;
  TagDataType data_type;
  uint16_t address;
  float scale;
  float offset;                      // engineering offset: eng = raw*scale + offset
  TagAccess access;                  // default: Read
  bool has_bit;                      // address written as "48310.3": a single bit of a holding register
  uint8_t bit;                       // 0..15, valid when has_bit (data_type must be Bool)
};

/** Modbus 4xxxx display address (e.g. 40047) -> 0-based holding offset. */
uint16_t gatewayModbus4xToOffset(uint32_t modbusRef);

uint16_t gatewayTagRegisterSpan(const TagDef &tag);

TagDataType gatewayParseDataType(const char *s);

const char *gatewayDataTypeToString(TagDataType dt);

bool gatewayTagTypeIsMultiRegister(TagDataType dt);

// ── MQTT publisher config (per VPS contract) ──────────────────────────
struct MqttConfig {
  char broker[MAX_MQTT_HOST_LEN];       // e.g. plc.khze.org
  uint16_t port;                        // 8883 (TLS) or 1883
  bool tls;                             // enable TLS (WiFiClientSecure)
  char username[MAX_MQTT_USER_LEN];     // edge-{factory_uuid}
  char password[MAX_MQTT_PASS_LEN];
  char client_id[MAX_MQTT_CLIENT_ID];   // unique per device
  char factory_id[MAX_MQTT_FACTORY_ID]; // UUID matching VPS DB
  char line_id[MAX_MQTT_LINE_ID];       // "01"
  char machine_id[MAX_MQTT_MACHINE_ID]; // machine / PLC name
};

struct GatewayConfig {
  uint8_t schema_version;
  char profile_name[MAX_PROFILE_NAME];
  char wifi_ssid[MAX_WIFI_SSID];
  char wifi_password[MAX_WIFI_PASS];
  char plc_protocol[MAX_PROTOCOL_LEN];
  char plc_host[MAX_HOST_LEN];
  uint16_t plc_port;
  uint8_t plc_unit_id;
  uint32_t poll_interval_ms;
  MqttConfig mqtt;                      // MQTT publisher — empty broker = disabled
  TagDef tags[MAX_TAGS];
  uint8_t tag_count;
};

void gatewayConfigSetDefaults(GatewayConfig &cfg);
