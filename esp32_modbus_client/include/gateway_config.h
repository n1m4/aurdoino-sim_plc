#pragma once

#include <Arduino.h>
#include "board_config.h"

enum class TagKind : uint8_t { Holding, Coil, Input, Discrete, Unknown };

enum class TagDataType : uint8_t { Bool, Int16, Uint16, Int32, Float32 };

struct TagDef {
  char id[MAX_TAG_ID_LEN];
  char label[MAX_LABEL_LEN];
  TagKind kind;
  TagDataType data_type;
  uint16_t address;
  float scale;
};

/** Modbus 4xxxx display address (e.g. 40047) -> 0-based holding offset. */
uint16_t gatewayModbus4xToOffset(uint32_t modbusRef);

uint16_t gatewayTagRegisterSpan(const TagDef &tag);

TagDataType gatewayParseDataType(const char *s);

const char *gatewayDataTypeToString(TagDataType dt);

bool gatewayTagTypeIsMultiRegister(TagDataType dt);

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
  TagDef tags[MAX_TAGS];
  uint8_t tag_count;
};

void gatewayConfigSetDefaults(GatewayConfig &cfg);
