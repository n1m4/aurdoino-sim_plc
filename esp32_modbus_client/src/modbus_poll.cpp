#include "modbus_poll.h"

#include <ModbusIP_ESP8266.h>
#include <WiFi.h>

#include <cstring>

#include "app_mode.h"
#include "board_config.h"
#include "gateway_config.h"
#include "tag_planner.h"

static ModbusIP s_modbus;
static GatewayConfig s_cfg;
static ReadBlock s_blocks[MAX_READ_BLOCKS];
static uint8_t s_blockCount = 0;
static IPAddress s_server;

static bool s_tagValid[MAX_TAGS];
static bool s_boolValues[MAX_TAGS];
static int32_t s_int32Values[MAX_TAGS];
static uint16_t s_uint16Values[MAX_TAGS];
static float s_floatValues[MAX_TAGS];
static bool s_coilValues[MAX_TAGS];

static uint16_t s_hregReadBuf[MAX_MODBUS_READ_COUNT];
static bool s_coilReadBuf[MAX_MODBUS_READ_COUNT];

static float decodeFloat32BigEndian(uint16_t regHi, uint16_t regLo) {
  uint32_t raw = (static_cast<uint32_t>(regHi) << 16) | regLo;
  float value;
  memcpy(&value, &raw, sizeof(value));
  return value;
}

static int32_t decodeInt32BigEndian(uint16_t regHi, uint16_t regLo) {
  uint32_t raw = (static_cast<uint32_t>(regHi) << 16) | regLo;
  return static_cast<int32_t>(raw);
}

static bool waitTransaction(uint16_t transId, const char *label) {
  if (transId == 0) {
    Serial.printf("[Modbus] %s: failed to start\n", label);
    return false;
  }
  const uint32_t start = millis();
  while (s_modbus.isTransaction(transId)) {
    s_modbus.task();
    if (millis() - start > MODBUS_TRANSACTION_TIMEOUT_MS) {
      Serial.printf("[Modbus] %s: timeout\n", label);
      return false;
    }
    delay(10);
  }
  return true;
}

static bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(s_cfg.wifi_ssid, s_cfg.wifi_password);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println("WiFi connect timeout");
      return false;
    }
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

static bool ensureModbusConnected() {
  if (s_modbus.isConnected(s_server)) {
    return true;
  }
  Serial.print("Connecting to Modbus Server: ");
  Serial.print(s_server);
  Serial.print(":");
  Serial.println(s_cfg.plc_port);
  if (!s_modbus.connect(s_server, s_cfg.plc_port)) {
    Serial.println("[Modbus] connect failed");
    return false;
  }
  Serial.println("Modbus connected");
  return true;
}

static void resetTagValues() {
  for (uint8_t i = 0; i < MAX_TAGS; i++) {
    s_tagValid[i] = false;
  }
}

static void decodeHoldingTag(uint8_t ti, const TagDef &td, const uint16_t *hregs, uint16_t idx) {
  switch (td.data_type) {
    case TagDataType::Bool:
      s_boolValues[ti] = hregs[idx] != 0;
      s_tagValid[ti] = true;
      break;
    case TagDataType::Int16:
      s_int32Values[ti] = static_cast<int16_t>(hregs[idx]);
      s_tagValid[ti] = true;
      break;
    case TagDataType::Uint16:
      s_uint16Values[ti] = hregs[idx];
      s_tagValid[ti] = true;
      break;
    case TagDataType::Int32:
      s_int32Values[ti] = decodeInt32BigEndian(hregs[idx], hregs[idx + 1]);
      s_tagValid[ti] = true;
      break;
    case TagDataType::Float32:
      s_floatValues[ti] = decodeFloat32BigEndian(hregs[idx], hregs[idx + 1]);
      s_tagValid[ti] = true;
      break;
    default:
      break;
  }
}

static void mapBlockToTags(const ReadBlock &block, const uint16_t *hregs, const bool *coils) {
  for (uint8_t ti = 0; ti < s_cfg.tag_count; ti++) {
    const TagDef &td = s_cfg.tags[ti];
    if (td.kind != block.kind) {
      continue;
    }
    if (td.address < block.start) {
      continue;
    }
    const uint16_t span = gatewayTagRegisterSpan(td);
    if (td.address + span - 1 >= block.start + block.count) {
      continue;
    }
    uint16_t idx = td.address - block.start;
    if (block.kind == TagKind::Holding) {
      decodeHoldingTag(ti, td, hregs, idx);
    } else if (block.kind == TagKind::Coil) {
      s_coilValues[ti] = coils[idx];
      s_tagValid[ti] = true;
    }
  }
}

static float applyScale(float v, float scale) {
  return v * (scale != 0.0f ? scale : 1.0f);
}

bool modbusPollBegin(const GatewayConfig &cfg) {
  modbusPollSetConfig(cfg);
  s_modbus.client();
  return true;
}

void modbusPollSetConfig(const GatewayConfig &cfg) {
  s_cfg = cfg;
  char err[128];
  if (!tagPlannerBuild(s_cfg, s_blocks, &s_blockCount, err, sizeof(err))) {
    Serial.printf("[Planner] %s\n", err);
    s_blockCount = 0;
  }
  if (!s_server.fromString(s_cfg.plc_host)) {
    s_server = IPAddress(0, 0, 0, 0);
  }
  resetTagValues();
}

ModbusIP &modbusPollGetClient() {
  return s_modbus;
}

bool modbusPollCycle() {
  if (!connectWiFi()) {
    return false;
  }
  if (!ensureModbusConnected()) {
    return false;
  }

  resetTagValues();

  for (uint8_t bi = 0; bi < s_blockCount; bi++) {
    const ReadBlock &block = s_blocks[bi];
    if (block.kind == TagKind::Holding) {
      uint16_t trans = s_modbus.readHreg(s_server, block.start, s_hregReadBuf, block.count, nullptr,
                                         s_cfg.plc_unit_id);
      if (!waitTransaction(trans, "readHreg")) {
        s_modbus.disconnect(s_server);
        return false;
      }
      mapBlockToTags(block, s_hregReadBuf, nullptr);
    } else if (block.kind == TagKind::Coil) {
      uint16_t trans = s_modbus.readCoil(s_server, block.start, s_coilReadBuf, block.count, nullptr,
                                         s_cfg.plc_unit_id);
      if (!waitTransaction(trans, "readCoil")) {
        s_modbus.disconnect(s_server);
        return false;
      }
      mapBlockToTags(block, nullptr, s_coilReadBuf);
    }
  }
  return true;
}

void modbusPollPrintValues() {
  Serial.println("--- tags ---");
  for (uint8_t i = 0; i < s_cfg.tag_count; i++) {
    const TagDef &td = s_cfg.tags[i];
    if (!s_tagValid[i]) {
      Serial.printf("%s=?\n", td.id);
      continue;
    }
    if (td.kind == TagKind::Coil) {
      Serial.printf("%s=%s\n", td.id, s_coilValues[i] ? "ON" : "OFF");
      continue;
    }
    switch (td.data_type) {
      case TagDataType::Bool:
        Serial.printf("%s=%s\n", td.id, s_boolValues[i] ? "true" : "false");
        break;
      case TagDataType::Int16: {
        const float v = applyScale(static_cast<float>(static_cast<int16_t>(s_int32Values[i])), td.scale);
        Serial.printf("%s=%.0f\n", td.id, v);
        break;
      }
      case TagDataType::Uint16:
        Serial.printf("%s=%u\n", td.id, static_cast<unsigned>(applyScale(s_uint16Values[i], td.scale)));
        break;
      case TagDataType::Int32: {
        const float v = applyScale(static_cast<float>(s_int32Values[i]), td.scale);
        Serial.printf("%s=%.0f\n", td.id, v);
        break;
      }
      case TagDataType::Float32:
        Serial.printf("%s=%.4f\n", td.id, applyScale(s_floatValues[i], td.scale));
        break;
      default:
        Serial.printf("%s=?\n", td.id);
        break;
    }
  }
  Serial.println("--------------------");
}

void modbusPollTask() {
  s_modbus.task();
}

bool modbusPollCheckBootConfigRequest() {
  return appModeCheckBootHoldDuringRun();
}

// ── Value accessors for MQTT publisher ───────────────────────────────

uint8_t modbusPollGetTagCount() {
  return s_cfg.tag_count;
}

const char *modbusPollGetTagId(uint8_t idx) {
  if (idx >= s_cfg.tag_count) return "";
  return s_cfg.tags[idx].id;
}

const char *modbusPollGetTagMqttKey(uint8_t idx) {
  if (idx >= s_cfg.tag_count) return "";
  const TagDef &td = s_cfg.tags[idx];
  // Prefer vps_name (schema v2) — fallback to id for backward compat
  return (td.vps_name[0]) ? td.vps_name : td.id;
}

bool modbusPollGetTagReading(uint8_t idx, TagReading &r) {
  if (idx >= s_cfg.tag_count) return false;
  const TagDef &td = s_cfg.tags[idx];
  r.valid     = s_tagValid[idx];
  r.is_bool   = false;
  r.bool_val  = false;
  r.float_val = 0.0f;

  if (!r.valid) return true;  // caller checks r.valid

  if (td.kind == TagKind::Coil) {
    r.is_bool  = true;
    r.bool_val = s_coilValues[idx];
    r.float_val = s_coilValues[idx] ? 1.0f : 0.0f;
    return true;
  }

  switch (td.data_type) {
    case TagDataType::Bool:
      r.is_bool   = true;
      r.bool_val  = s_boolValues[idx];
      r.float_val = s_boolValues[idx] ? 1.0f : 0.0f;
      break;
    case TagDataType::Float32:
      r.float_val = applyScale(s_floatValues[idx], td.scale);
      break;
    case TagDataType::Int16:
      r.float_val = applyScale(static_cast<float>(static_cast<int16_t>(s_int32Values[idx])), td.scale);
      break;
    case TagDataType::Uint16:
      r.float_val = applyScale(static_cast<float>(s_uint16Values[idx]), td.scale);
      break;
    case TagDataType::Int32:
      r.float_val = applyScale(static_cast<float>(s_int32Values[idx]), td.scale);
      break;
    default:
      break;
  }
  return true;
}

// ── Modbus Write ──────────────────────────────────────────────────────

static void encodeFloat32(float value, uint16_t &hi, uint16_t &lo) {
  uint32_t raw;
  memcpy(&raw, &value, sizeof(raw));
  hi = static_cast<uint16_t>(raw >> 16);
  lo = static_cast<uint16_t>(raw & 0xFFFFu);
}

static void encodeInt32(int32_t value, uint16_t &hi, uint16_t &lo) {
  const uint32_t raw = static_cast<uint32_t>(value);
  hi = static_cast<uint16_t>(raw >> 16);
  lo = static_cast<uint16_t>(raw & 0xFFFFu);
}

const char *writeResultToString(WriteResult r) {
  switch (r) {
    case WriteResult::OK:               return nullptr;
    case WriteResult::TagNotFound:      return "tag not found";
    case WriteResult::ReadOnly:         return "tag is read-only";
    case WriteResult::TypeNotSupported: return "unsupported tag type";
    case WriteResult::ModbusError:      return "PLC connection error";
    default:                            return "unknown error";
  }
}

WriteResult modbusPollWriteTag(const char *tag_uuid, const CommandWriteValue &val) {
  // ── Find tag by UUID ──────────────────────────────────────────────
  int8_t found = -1;
  for (uint8_t i = 0; i < s_cfg.tag_count; i++) {
    if (s_cfg.tags[i].tag_uuid[0] &&
        strcmp(s_cfg.tags[i].tag_uuid, tag_uuid) == 0) {
      found = static_cast<int8_t>(i);
      break;
    }
  }
  if (found < 0) {
    Serial.printf("[CMD] ERROR: tag not found (uuid=%s)\n", tag_uuid);
    return WriteResult::TagNotFound;
  }

  const TagDef &td = s_cfg.tags[found];
  Serial.printf("[COMMAND] Tag=%s addr=%u kind=%s dtype=%s\n",
                td.id, td.address,
                td.kind == TagKind::Coil    ? "coil"    : "holding",
                td.kind == TagKind::Holding ? gatewayDataTypeToString(td.data_type) : "-");

  // ── Access check ──────────────────────────────────────────────────
  if (td.access == TagAccess::Read) {
    Serial.printf("[CMD] ERROR: tag is read-only (%s)\n", td.id);
    return WriteResult::ReadOnly;
  }

  // ── Ensure Modbus connected ────────────────────────────────────────
  if (!ensureModbusConnected()) {
    Serial.println("[CMD] ERROR: PLC disconnected");
    return WriteResult::ModbusError;
  }

  // ── Engineering → raw conversion ──────────────────────────────────
  // raw = (eng - offset) / scale
  const float scale = (td.scale != 0.0f) ? td.scale : 1.0f;

  Serial.printf("[COMMAND] Writing...\n");

  // ── Coil ──────────────────────────────────────────────────────────
  if (td.kind == TagKind::Coil) {
    bool boolVal = val.is_bool ? val.bool_val : (val.num_val != 0.0);
    uint16_t trans = s_modbus.writeCoil(s_server, td.address,
                                         boolVal, nullptr, s_cfg.plc_unit_id);
    if (!waitTransaction(trans, "writeCoil")) return WriteResult::ModbusError;
    Serial.printf("[COMMAND] Write successful (coil %s = %s)\n",
                  td.id, boolVal ? "ON" : "OFF");
    return WriteResult::OK;
  }

  // ── Holding register ──────────────────────────────────────────────
  if (td.kind != TagKind::Holding) return WriteResult::TypeNotSupported;

  switch (td.data_type) {
    case TagDataType::Bool: {
      bool boolVal = val.is_bool ? val.bool_val : (val.num_val != 0.0);
      uint16_t raw = boolVal ? 1u : 0u;
      uint16_t trans = s_modbus.writeHreg(s_server, td.address,
                                           raw, nullptr, s_cfg.plc_unit_id);
      if (!waitTransaction(trans, "writeHreg(bool)")) return WriteResult::ModbusError;
      break;
    }
    case TagDataType::Int16: {
      float eng  = val.is_bool ? (val.bool_val ? 1.0f : 0.0f) : static_cast<float>(val.num_val);
      int16_t raw = static_cast<int16_t>((eng - td.offset) / scale);
      uint16_t trans = s_modbus.writeHreg(s_server, td.address,
                                           static_cast<uint16_t>(raw), nullptr, s_cfg.plc_unit_id);
      if (!waitTransaction(trans, "writeHreg(int16)")) return WriteResult::ModbusError;
      break;
    }
    case TagDataType::Uint16: {
      float eng   = val.is_bool ? (val.bool_val ? 1.0f : 0.0f) : static_cast<float>(val.num_val);
      uint16_t raw = static_cast<uint16_t>(static_cast<uint32_t>((eng - td.offset) / scale));
      uint16_t trans = s_modbus.writeHreg(s_server, td.address,
                                           raw, nullptr, s_cfg.plc_unit_id);
      if (!waitTransaction(trans, "writeHreg(uint16)")) return WriteResult::ModbusError;
      break;
    }
    case TagDataType::Float32: {
      float eng  = val.is_bool ? (val.bool_val ? 1.0f : 0.0f) : static_cast<float>(val.num_val);
      float rawf = (eng - td.offset) / scale;
      uint16_t buf[2];
      encodeFloat32(rawf, buf[0], buf[1]);
      uint16_t trans = s_modbus.writeHreg(s_server, td.address,
                                           buf, 2, nullptr, s_cfg.plc_unit_id);
      if (!waitTransaction(trans, "writeHreg(float32)")) return WriteResult::ModbusError;
      break;
    }
    case TagDataType::Int32: {
      float eng    = val.is_bool ? (val.bool_val ? 1.0f : 0.0f) : static_cast<float>(val.num_val);
      int32_t raw32 = static_cast<int32_t>((eng - td.offset) / scale);
      uint16_t buf[2];
      encodeInt32(raw32, buf[0], buf[1]);
      uint16_t trans = s_modbus.writeHreg(s_server, td.address,
                                           buf, 2, nullptr, s_cfg.plc_unit_id);
      if (!waitTransaction(trans, "writeHreg(int32)")) return WriteResult::ModbusError;
      break;
    }
    default:
      return WriteResult::TypeNotSupported;
  }

  Serial.printf("[COMMAND] Write successful (%s)\n", td.id);
  return WriteResult::OK;
}
