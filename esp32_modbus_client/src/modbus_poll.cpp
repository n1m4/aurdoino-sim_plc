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
