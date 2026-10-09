#include "config_store.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>

#include <cstring>

#include "gateway_config.h"
#include "tag_planner.h"

static TagKind parseKind(const char *s) {
  if (!s) {
    return TagKind::Unknown;
  }
  if (strcmp(s, "holding") == 0) {
    return TagKind::Holding;
  }
  if (strcmp(s, "coil") == 0) {
    return TagKind::Coil;
  }
  if (strcmp(s, "input") == 0) {
    return TagKind::Input;
  }
  if (strcmp(s, "discrete") == 0) {
    return TagKind::Discrete;
  }
  return TagKind::Unknown;
}

static const char *kindToString(TagKind k) {
  switch (k) {
    case TagKind::Holding:
      return "holding";
    case TagKind::Coil:
      return "coil";
    case TagKind::Input:
      return "input";
    case TagKind::Discrete:
      return "discrete";
    default:
      return "unknown";
  }
}

static TagAccess parseAccess(const char *s) {
  if (!s) return TagAccess::Read;
  if (strcmp(s, "read_write") == 0) return TagAccess::ReadWrite;
  if (strcmp(s, "write")      == 0) return TagAccess::Write;
  return TagAccess::Read;
}

static const char *accessToString(TagAccess a) {
  switch (a) {
    case TagAccess::ReadWrite: return "read_write";
    case TagAccess::Write:     return "write";
    default:                   return "read";
  }
}

static void copyStr(char *dst, size_t dstLen, const char *src) {
  if (!dst || dstLen == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  std::strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

static void fillTagFromJson(TagDef &td, JsonObject t) {
  std::memset(&td, 0, sizeof(td));
  copyStr(td.id,       sizeof(td.id),       t["id"]       | "");
  copyStr(td.vps_name, sizeof(td.vps_name), t["vps_name"] | "");
  copyStr(td.unit,     sizeof(td.unit),     t["unit"]     | "");
  copyStr(td.tag_uuid, sizeof(td.tag_uuid), t["tag_uuid"] | "");
  copyStr(td.label,    sizeof(td.label),    t["label"]    | "");

  // data_type: accept both "datatype" (v1) and "data_type" (v2)
  td.data_type = gatewayParseDataType(t["datatype"] | t["data_type"] | "uint16");

  // address: accept both number (v1) and string (v2 / VPS export)
  uint32_t rawAddr = 0;
  if (t["address"].is<uint32_t>()) {
    rawAddr = t["address"].as<uint32_t>();
  } else if (t["address"].is<long>()) {
    rawAddr = static_cast<uint32_t>(t["address"].as<long>());
  } else if (t["address"].is<const char *>()) {
    rawAddr = static_cast<uint32_t>(atol(t["address"].as<const char *>()));
  }

  // kind: explicit field (v1 compat) or inferred from address/data_type (v2)
  const char *kindStr = t["kind"] | "";
  if (kindStr[0]) {
    td.kind = parseKind(kindStr);
  } else {
    // Inference rule:
    //   40000-49999 → holding register (standard Modbus 4xxxx notation)
    //   data_type=bool + address<40000 → coil
    //   anything else → holding register
    if (rawAddr >= 40000 && rawAddr <= 49999) {
      td.kind = TagKind::Holding;
    } else if (td.data_type == TagDataType::Bool) {
      td.kind = TagKind::Coil;
    } else {
      td.kind = TagKind::Holding;
    }
  }

  // Convert display address to 0-based Modbus offset
  if (td.kind == TagKind::Holding) {
    td.address = gatewayModbus4xToOffset(rawAddr);
  } else if (td.kind == TagKind::Coil && rawAddr >= 1 && rawAddr <= 9999) {
    td.address = static_cast<uint16_t>(rawAddr - 1);
  } else {
    td.address = static_cast<uint16_t>(rawAddr);
  }

  td.scale  = t["scale"]  | 0.0f;
  td.offset = t["offset"] | 0.0f;
  td.access = parseAccess(t["access"] | "read");
}

static bool isValidIpv4(const char *host) {
  if (!host || !*host) {
    return false;
  }
  IPAddress ip;
  return ip.fromString(host);
}

bool configStoreInit() {
  if (!LittleFS.begin(true)) {
    return false;
  }
  return true;
}

bool configStoreExists() {
  return LittleFS.exists(CONFIG_FILE_PATH);
}

bool configStoreParseJson(const char *json, size_t len, GatewayConfig &cfg, char *errorBuf,
                          size_t errorBufLen) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) {
    snprintf(errorBuf, errorBufLen, "JSON parse: %s", err.c_str());
    return false;
  }

  gatewayConfigSetDefaults(cfg);

  cfg.schema_version = doc["schema_version"] | 0;
  copyStr(cfg.profile_name, sizeof(cfg.profile_name), doc["profile_name"] | "default");

  JsonObject wifi = doc["wifi"];
  if (!wifi.isNull()) {
    copyStr(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), wifi["ssid"] | "");
    copyStr(cfg.wifi_password, sizeof(cfg.wifi_password), wifi["password"] | "");
  }

  JsonObject plc = doc["plc"];
  if (!plc.isNull()) {
    copyStr(cfg.plc_protocol, sizeof(cfg.plc_protocol), plc["protocol"] | "modbus_tcp");
    copyStr(cfg.plc_host, sizeof(cfg.plc_host), plc["host"] | "");
    cfg.plc_port = plc["port"] | 5020;
    cfg.plc_unit_id = plc["unit_id"] | 1;
  }

  JsonObject poll = doc["poll"];
  if (!poll.isNull()) {
    cfg.poll_interval_ms = poll["interval_ms"] | 1000;
  }

  // ── MQTT (optional — empty broker = disabled) ──────────────────────
  JsonObject mqtt = doc["mqtt"];
  if (!mqtt.isNull()) {
    copyStr(cfg.mqtt.broker,     sizeof(cfg.mqtt.broker),     mqtt["broker"]     | "");
    cfg.mqtt.port = mqtt["port"] | 8883;
    cfg.mqtt.tls  = mqtt["tls"]  | true;
    copyStr(cfg.mqtt.username,   sizeof(cfg.mqtt.username),   mqtt["username"]   | "");
    copyStr(cfg.mqtt.password,   sizeof(cfg.mqtt.password),   mqtt["password"]   | "");
    copyStr(cfg.mqtt.client_id,  sizeof(cfg.mqtt.client_id),  mqtt["client_id"]  | "");
    copyStr(cfg.mqtt.factory_id, sizeof(cfg.mqtt.factory_id), mqtt["factory_id"] | "");
    copyStr(cfg.mqtt.line_id,    sizeof(cfg.mqtt.line_id),    mqtt["line_id"]    | "01");
    copyStr(cfg.mqtt.machine_id, sizeof(cfg.mqtt.machine_id), mqtt["machine_id"] | "machine-01");
  }

  JsonArray tags = doc["tags"].as<JsonArray>();
  if (tags.isNull()) {
    snprintf(errorBuf, errorBufLen, "tags array required");
    return false;
  }

  cfg.tag_count = 0;
  for (JsonObject t : tags) {
    if (cfg.tag_count >= MAX_TAGS) {
      snprintf(errorBuf, errorBufLen, "too many tags (max %d)", MAX_TAGS);
      return false;
    }
    fillTagFromJson(cfg.tags[cfg.tag_count], t);
    cfg.tag_count++;
  }

  return configStoreValidate(cfg, errorBuf, errorBufLen);
}

bool configStoreParseJsonNoValidate(const char *json, size_t len, GatewayConfig &cfg,
                                    char *errorBuf, size_t errorBufLen) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) {
    snprintf(errorBuf, errorBufLen, "JSON parse: %s", err.c_str());
    return false;
  }

  gatewayConfigSetDefaults(cfg);

  cfg.schema_version = doc["schema_version"] | 0;
  copyStr(cfg.profile_name, sizeof(cfg.profile_name), doc["profile_name"] | "default");

  JsonObject wifi = doc["wifi"];
  if (!wifi.isNull()) {
    copyStr(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), wifi["ssid"] | "");
    copyStr(cfg.wifi_password, sizeof(cfg.wifi_password), wifi["password"] | "");
  }

  JsonObject plc = doc["plc"];
  if (!plc.isNull()) {
    copyStr(cfg.plc_protocol, sizeof(cfg.plc_protocol), plc["protocol"] | "modbus_tcp");
    copyStr(cfg.plc_host, sizeof(cfg.plc_host), plc["host"] | "");
    cfg.plc_port = plc["port"] | 5020;
    cfg.plc_unit_id = plc["unit_id"] | 1;
  }

  JsonObject poll = doc["poll"];
  if (!poll.isNull()) {
    cfg.poll_interval_ms = poll["interval_ms"] | 1000;
  }

  // ── MQTT (optional) ────────────────────────────────────────────────
  JsonObject mqtt = doc["mqtt"];
  if (!mqtt.isNull()) {
    copyStr(cfg.mqtt.broker,     sizeof(cfg.mqtt.broker),     mqtt["broker"]     | "");
    cfg.mqtt.port = mqtt["port"] | 8883;
    cfg.mqtt.tls  = mqtt["tls"]  | true;
    copyStr(cfg.mqtt.username,   sizeof(cfg.mqtt.username),   mqtt["username"]   | "");
    copyStr(cfg.mqtt.password,   sizeof(cfg.mqtt.password),   mqtt["password"]   | "");
    copyStr(cfg.mqtt.client_id,  sizeof(cfg.mqtt.client_id),  mqtt["client_id"]  | "");
    copyStr(cfg.mqtt.factory_id, sizeof(cfg.mqtt.factory_id), mqtt["factory_id"] | "");
    copyStr(cfg.mqtt.line_id,    sizeof(cfg.mqtt.line_id),    mqtt["line_id"]    | "01");
    copyStr(cfg.mqtt.machine_id, sizeof(cfg.mqtt.machine_id), mqtt["machine_id"] | "machine-01");
  }

  JsonArray tags = doc["tags"].as<JsonArray>();
  if (!tags.isNull()) {
    cfg.tag_count = 0;
    for (JsonObject t : tags) {
      if (cfg.tag_count >= MAX_TAGS) {
        break;
      }
      fillTagFromJson(cfg.tags[cfg.tag_count], t);
      cfg.tag_count++;
    }
  }
  return true;
}

bool configStoreValidate(const GatewayConfig &cfg, char *errorBuf, size_t errorBufLen) {
  if (cfg.schema_version != 1) {
    snprintf(errorBuf, errorBufLen, "unsupported schema_version");
    return false;
  }
  if (cfg.wifi_ssid[0] == '\0') {
    snprintf(errorBuf, errorBufLen, "wifi.ssid required");
    return false;
  }
  if (strcmp(cfg.plc_protocol, "modbus_tcp") != 0) {
    snprintf(errorBuf, errorBufLen, "only modbus_tcp supported");
    return false;
  }
  if (!isValidIpv4(cfg.plc_host)) {
    snprintf(errorBuf, errorBufLen, "plc.host invalid IPv4");
    return false;
  }
  if (cfg.plc_port == 0) {
    snprintf(errorBuf, errorBufLen, "plc.port invalid");
    return false;
  }
  if (cfg.plc_unit_id < 1 || cfg.plc_unit_id > 247) {
    snprintf(errorBuf, errorBufLen, "plc.unit_id must be 1-247");
    return false;
  }
  if (cfg.tag_count == 0) {
    snprintf(errorBuf, errorBufLen, "at least one tag required");
    return false;
  }

  for (uint8_t i = 0; i < cfg.tag_count; i++) {
    const TagDef &td = cfg.tags[i];
    if (td.id[0] == '\0') {
      snprintf(errorBuf, errorBufLen, "tag[%u].id required", i);
      return false;
    }
    for (uint8_t j = i + 1; j < cfg.tag_count; j++) {
      if (strcmp(td.id, cfg.tags[j].id) == 0) {
        snprintf(errorBuf, errorBufLen, "duplicate tag id: %s", td.id);
        return false;
      }
    }
    if (td.kind != TagKind::Holding && td.kind != TagKind::Coil) {
      snprintf(errorBuf, errorBufLen, "tag %s: only holding/coil implemented", td.id);
      return false;
    }
    if (td.kind == TagKind::Coil) {
      continue;
    }
    if (td.kind != TagKind::Holding) {
      continue;
    }
    switch (td.data_type) {
      case TagDataType::Bool:
      case TagDataType::Int16:
      case TagDataType::Uint16:
      case TagDataType::Int32:
      case TagDataType::Float32:
        break;
      default:
        snprintf(errorBuf, errorBufLen, "tag %s: unsupported datatype", td.id);
        return false;
    }
  }

  ReadBlock blocks[MAX_READ_BLOCKS];
  uint8_t blockCount = 0;
  if (!tagPlannerBuild(cfg, blocks, &blockCount, errorBuf, errorBufLen)) {
    return false;
  }
  (void)blocks;
  (void)blockCount;
  return true;
}

bool configStoreLoadForEdit(GatewayConfig &cfg) {
  gatewayConfigSetDefaults(cfg);
  if (!LittleFS.exists(CONFIG_FILE_PATH)) {
    return true;
  }
  File f = LittleFS.open(CONFIG_FILE_PATH, "r");
  if (!f) {
    return true;
  }
  String content = f.readString();
  f.close();
  char err[128];
  if (!configStoreParseJsonNoValidate(content.c_str(), content.length(), cfg, err, sizeof(err))) {
    gatewayConfigSetDefaults(cfg);
  }
  return true;
}

bool configStoreLoad(GatewayConfig &cfg, char *errorBuf, size_t errorBufLen) {
  if (!LittleFS.exists(CONFIG_FILE_PATH)) {
    snprintf(errorBuf, errorBufLen, "config.json missing");
    return false;
  }
  File f = LittleFS.open(CONFIG_FILE_PATH, "r");
  if (!f) {
    snprintf(errorBuf, errorBufLen, "cannot open config.json");
    return false;
  }
  String content = f.readString();
  f.close();
  char parseErr[128];
  if (!configStoreParseJson(content.c_str(), content.length(), cfg, parseErr, sizeof(parseErr))) {
    snprintf(errorBuf, errorBufLen, "%s", parseErr);
    return false;
  }
  return true;
}

bool configStoreSave(const GatewayConfig &cfg, char *errorBuf, size_t errorBufLen) {
  if (!configStoreValidate(cfg, errorBuf, errorBufLen)) {
    return false;
  }

  String json;
  if (!configStoreSerialize(cfg, json, false)) {
    snprintf(errorBuf, errorBufLen, "serialize failed");
    return false;
  }

  File f = LittleFS.open(CONFIG_FILE_PATH, "w");
  if (!f) {
    snprintf(errorBuf, errorBufLen, "cannot write config.json");
    return false;
  }
  if (f.print(json) != (int)json.length()) {
    f.close();
    snprintf(errorBuf, errorBufLen, "short write config.json");
    return false;
  }
  f.close();
  return true;
}

bool configStoreSerialize(const GatewayConfig &cfg, String &outJson, bool maskWifiPassword) {
  JsonDocument doc;
  doc["schema_version"] = cfg.schema_version;
  doc["profile_name"] = cfg.profile_name;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = cfg.wifi_ssid;
  if (maskWifiPassword) {
    wifi["password"] = "********";
  } else {
    wifi["password"] = cfg.wifi_password;
  }

  JsonObject plc = doc["plc"].to<JsonObject>();
  plc["protocol"] = cfg.plc_protocol;
  plc["host"] = cfg.plc_host;
  plc["port"] = cfg.plc_port;
  plc["unit_id"] = cfg.plc_unit_id;

  JsonObject poll = doc["poll"].to<JsonObject>();
  poll["interval_ms"] = cfg.poll_interval_ms;

  // ── MQTT (serialized even when disabled — empty broker is ok) ───────
  JsonObject mqtt = doc["mqtt"].to<JsonObject>();
  mqtt["broker"]     = cfg.mqtt.broker;
  mqtt["port"]       = cfg.mqtt.port;
  mqtt["tls"]        = cfg.mqtt.tls;
  mqtt["username"]   = cfg.mqtt.username;
  // Mask password in API responses the same way WiFi password is masked
  if (maskWifiPassword) {
    mqtt["password"] = "********";
  } else {
    mqtt["password"] = cfg.mqtt.password;
  }
  mqtt["client_id"]  = cfg.mqtt.client_id;
  mqtt["factory_id"] = cfg.mqtt.factory_id;
  mqtt["line_id"]    = cfg.mqtt.line_id;
  mqtt["machine_id"] = cfg.mqtt.machine_id;

  JsonArray tags = doc["tags"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.tag_count; i++) {
    const TagDef &td = cfg.tags[i];
    JsonObject t = tags.add<JsonObject>();
    t["id"] = td.id;
    if (td.vps_name[0]) {
      t["vps_name"] = td.vps_name;
    }
    if (td.tag_uuid[0]) {
      t["tag_uuid"] = td.tag_uuid;
    }
    if (td.label[0]) {
      t["label"] = td.label;
    }
    if (td.unit[0]) {
      t["unit"] = td.unit;
    }
    t["kind"] = kindToString(td.kind);
    if (td.kind == TagKind::Holding) {
      t["address"] = td.address + 40001;
    } else if (td.kind == TagKind::Coil) {
      t["address"] = td.address + 1;
    } else {
      t["address"] = td.address;
    }
    if (td.kind == TagKind::Holding) {
      t["datatype"] = gatewayDataTypeToString(td.data_type);
    }
    if (td.scale != 0.0f && td.scale != 1.0f) {
      t["scale"] = td.scale;
    }
    if (td.offset != 0.0f) {
      t["offset"] = td.offset;
    }
    // Always serialize access so it's explicit in the config file
    t["access"] = accessToString(td.access);
  }

  outJson.clear();
  serializeJson(doc, outJson);
  return true;
}
