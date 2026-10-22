#include "gateway_config.h"

#include <cstring>

void gatewayConfigSetDefaults(GatewayConfig &cfg) {
  std::memset(&cfg, 0, sizeof(cfg));
  cfg.schema_version = 1;
  std::strncpy(cfg.profile_name, "default", sizeof(cfg.profile_name) - 1);
  std::strncpy(cfg.plc_protocol, "modbus_tcp", sizeof(cfg.plc_protocol) - 1);
  cfg.plc_port = 5020;
  cfg.plc_unit_id = 1;
  cfg.poll_interval_ms = 1000;
  // MQTT defaults — broker empty = disabled
  cfg.mqtt.port = 8883;
  cfg.mqtt.tls  = true;
  std::strncpy(cfg.mqtt.line_id,    "01",        sizeof(cfg.mqtt.line_id)    - 1);
  std::strncpy(cfg.mqtt.machine_id, "machine-01", sizeof(cfg.mqtt.machine_id) - 1);
  // Note: TagDef.access defaults to Read (= 0) via memset
}

uint16_t gatewayModbus4xToOffset(uint32_t modbusRef) {
  if (modbusRef >= 40001 && modbusRef <= 49999) {
    return static_cast<uint16_t>(modbusRef - 40001);
  }
  return static_cast<uint16_t>(modbusRef);
}

bool gatewayTagTypeIsMultiRegister(TagDataType dt) {
  return dt == TagDataType::Int32 || dt == TagDataType::Uint32 || dt == TagDataType::Float32;
}

uint16_t gatewayTagRegisterSpan(const TagDef &tag) {
  if (tag.kind == TagKind::Coil) {
    return 1;
  }
  if (gatewayTagTypeIsMultiRegister(tag.data_type)) {
    return 2;
  }
  return 1;
}

TagDataType gatewayParseDataType(const char *s) {
  if (!s || !*s) {
    return TagDataType::Uint16;
  }
  if (strcmp(s, "bool") == 0) {
    return TagDataType::Bool;
  }
  if (strcmp(s, "int16") == 0) {
    return TagDataType::Int16;
  }
  if (strcmp(s, "uint16") == 0) {
    return TagDataType::Uint16;
  }
  if (strcmp(s, "int32") == 0) {
    return TagDataType::Int32;
  }
  if (strcmp(s, "uint32") == 0) {
    return TagDataType::Uint32;
  }
  if (strcmp(s, "float32") == 0) {
    return TagDataType::Float32;
  }
  return TagDataType::Uint16;
}

const char *gatewayDataTypeToString(TagDataType dt) {
  switch (dt) {
    case TagDataType::Bool:
      return "bool";
    case TagDataType::Int16:
      return "int16";
    case TagDataType::Uint16:
      return "uint16";
    case TagDataType::Int32:
      return "int32";
    case TagDataType::Uint32:
      return "uint32";
    case TagDataType::Float32:
      return "float32";
    default:
      return "uint16";
  }
}
