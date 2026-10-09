/**
 * mqtt_command.cpp
 *
 * Bidirectional MQTT Command Handler — VPS → ESP32 → PLC
 * ─────────────────────────────────────────────────────────────────────
 *
 * Command JSON (from VPS):
 *   {
 *     "profile_version": "2026-04-08",
 *     "command_id": "<uuid-v4>",
 *     "issued_at_ms": 1234567890000,
 *     "expires_at_ms": 1234567920000,
 *     "factory": "<factory-uuid>",
 *     "tag_id": "<tag-uuid>",
 *     "value": true
 *   }
 *
 * Result JSON (to VPS):
 *   {
 *     "profile_version": "2026-04-08",
 *     "command_id": "<same>",
 *     "factory": "<factory-uuid>",
 *     "ok": true | false,
 *     "error": null | "<string>",
 *     "tag_id": "<tag-uuid>",
 *     "value": <echo of written value> | null
 *   }
 *
 * Security notes:
 *  - factory_id validated before any PLC write
 *  - No credential is logged to Serial
 *  - Dedup window: CMD_DEDUP_WINDOW_MS (60 s), ring buffer size CMD_DEDUP_MAX
 */

#include "mqtt_command.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstring>
#include <time.h>

#include "board_config.h"
#include "modbus_poll.h"

// ── Module state ──────────────────────────────────────────────────────
static PubSubClient *s_client = nullptr;
static MqttConfig    s_cfg;
static bool          s_inited = false;

// ── Dedup ring buffer ─────────────────────────────────────────────────
struct DedupEntry {
  char     cmd_id[MAX_CMD_ID_LEN];
  uint32_t ts_ms;   // millis() when received
};
static DedupEntry s_dedup[CMD_DEDUP_MAX];
static uint8_t    s_dedupHead = 0;

static bool isDuplicate(const char *cmd_id) {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < CMD_DEDUP_MAX; i++) {
    if (s_dedup[i].cmd_id[0] &&
        strcmp(s_dedup[i].cmd_id, cmd_id) == 0 &&
        (now - s_dedup[i].ts_ms) < CMD_DEDUP_WINDOW_MS) {
      return true;
    }
  }
  return false;
}

static void dedupAdd(const char *cmd_id) {
  strncpy(s_dedup[s_dedupHead].cmd_id, cmd_id, MAX_CMD_ID_LEN - 1);
  s_dedup[s_dedupHead].cmd_id[MAX_CMD_ID_LEN - 1] = '\0';
  s_dedup[s_dedupHead].ts_ms = millis();
  s_dedupHead = (s_dedupHead + 1) % CMD_DEDUP_MAX;
}

// ── Pending command (single-slot queue) ──────────────────────────────
struct PendingCmd {
  bool   active;
  char   cmd_id[MAX_CMD_ID_LEN];
  char   factory[MAX_MQTT_FACTORY_ID];
  char   tag_id[MAX_TAG_UUID_LEN];
  char   result_topic[CMD_RESULT_TOPIC_MAX];
  bool   value_is_bool;
  bool   value_bool;
  double value_num;
  uint64_t expires_at_ms;
};
static PendingCmd s_pending;

// ── Current UTC time in milliseconds (0 if NTP not synced) ───────────
static uint64_t nowMs() {
  time_t t = time(nullptr);
  if (t >= 1000000000L) return (uint64_t)t * 1000ULL;
  return 0;  // NTP not ready — caller skips expiry check
}

// ── Publish result ────────────────────────────────────────────────────
static void publishResult(const char *result_topic,
                          const char *cmd_id,
                          const char *factory,
                          const char *tag_id,
                          bool ok, const char *error,
                          bool value_is_bool, bool value_bool, double value_num) {
  if (!s_client || !s_client->connected()) {
    Serial.println("[MQTT] Cannot publish result — not connected");
    return;
  }

  JsonDocument doc;
  doc["profile_version"] = "2026-04-08";
  doc["command_id"]      = cmd_id;
  doc["factory"]         = factory;
  doc["ok"]              = ok;
  doc["tag_id"]          = tag_id;

  if (ok) {
    doc["error"] = nullptr;
    if (value_is_bool) {
      doc["value"] = value_bool;
    } else {
      doc["value"] = value_num;
    }
  } else {
    doc["error"] = error;
    doc["value"] = nullptr;
  }

  static char payload[MQTT_JSON_MAX_LEN];
  const size_t len = serializeJson(doc, payload, sizeof(payload));
  if (len == 0 || len >= sizeof(payload)) {
    Serial.println("[CMD] Result JSON too large");
    return;
  }

  if (s_client->publish(result_topic,
                         reinterpret_cast<const uint8_t *>(payload),
                         len, /*retain=*/false)) {
    Serial.printf("[MQTT] Result published: ok=%s\n", ok ? "true" : "false");
  } else {
    Serial.printf("[MQTT] Result publish failed (rc=%d)\n", s_client->state());
  }
}

// ── PubSubClient callback (called during s_client->loop()) ───────────
static void commandCallback(char *topic, byte *raw, unsigned int len) {
  Serial.println("[MQTT] Command received");

  // ── 1. Parse JSON ─────────────────────────────────────────────────
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, raw, len);
  if (err) {
    Serial.printf("[CMD] JSON parse error (%s) — silent drop\n", err.c_str());
    return;
  }

  // ── 2. Validate required string fields ────────────────────────────
  const char *profile_version = doc["profile_version"] | (const char *)nullptr;
  const char *cmd_id          = doc["command_id"]      | (const char *)nullptr;
  const char *factory         = doc["factory"]         | (const char *)nullptr;
  const char *tag_id          = doc["tag_id"]          | (const char *)nullptr;

  if (!profile_version || !cmd_id || !factory || !tag_id) {
    Serial.println("[CMD] Missing required string field — silent drop");
    return;
  }

  // Validate timestamp fields (ArduinoJson v7 — avoid deprecated containsKey)
  if (!doc["issued_at_ms"].is<unsigned long long>() &&
      !doc["issued_at_ms"].is<long long>()) {
    Serial.println("[CMD] Missing issued_at_ms — silent drop");
    return;
  }
  if (!doc["expires_at_ms"].is<unsigned long long>() &&
      !doc["expires_at_ms"].is<long long>()) {
    Serial.println("[CMD] Missing expires_at_ms — silent drop");
    return;
  }

  // Validate 'value' field exists and is not null/absent
  // In ArduinoJson v7, absent key and JSON null both produce isNull()==true
  // A bool 'false' has isNull()==false, a number 0 has isNull()==false
  {
    JsonVariant v = doc["value"];
    if (v.isNull() && !v.is<bool>()) {
      Serial.println("[CMD] Missing or null 'value' — silent drop");
      return;
    }
    // Reject string values (Modbus doesn't support strings)
    if (v.is<const char *>()) {
      Serial.println("[CMD] String value not supported — silent drop");
      return;
    }
  }

  const uint64_t expires_at = doc["expires_at_ms"].as<unsigned long long>();

  Serial.printf("[MQTT] command_id=%s\n", cmd_id);
  Serial.printf("[MQTT] tag_id=%s\n",     tag_id);

  // ── 3. Factory validation ─────────────────────────────────────────
  if (strcmp(factory, s_cfg.factory_id) != 0) {
    Serial.println("[CMD] Factory mismatch — silent drop");
    return;
  }

  // ── 4. Dedup ──────────────────────────────────────────────────────
  if (isDuplicate(cmd_id)) {
    Serial.printf("[CMD] Duplicate command_id=%s — drop\n", cmd_id);
    return;
  }
  dedupAdd(cmd_id);

  // ── Build result topic ─────────────────────────────────────────────
  char result_topic[CMD_RESULT_TOPIC_MAX];
  snprintf(result_topic, sizeof(result_topic), "%s/result", topic);

  // ── Parse value ───────────────────────────────────────────────────
  bool   value_is_bool = false;
  bool   value_bool    = false;
  double value_num     = 0.0;

  JsonVariant valueVar = doc["value"];
  if (valueVar.is<bool>()) {
    value_is_bool = true;
    value_bool    = valueVar.as<bool>();
  } else {
    value_num = valueVar.as<double>();
  }

  Serial.printf("[MQTT] value=%s\n",
    value_is_bool ? (value_bool ? "true" : "false") : "(numeric)");

  // ── Queue pending command ──────────────────────────────────────────
  s_pending.active = true;

  strncpy(s_pending.cmd_id,       cmd_id,       MAX_CMD_ID_LEN        - 1);
  s_pending.cmd_id[MAX_CMD_ID_LEN - 1] = '\0';

  strncpy(s_pending.factory,      factory,      MAX_MQTT_FACTORY_ID   - 1);
  s_pending.factory[MAX_MQTT_FACTORY_ID - 1] = '\0';

  strncpy(s_pending.tag_id,       tag_id,       MAX_TAG_UUID_LEN      - 1);
  s_pending.tag_id[MAX_TAG_UUID_LEN - 1] = '\0';

  strncpy(s_pending.result_topic, result_topic, CMD_RESULT_TOPIC_MAX  - 1);
  s_pending.result_topic[CMD_RESULT_TOPIC_MAX - 1] = '\0';

  s_pending.expires_at_ms = expires_at;
  s_pending.value_is_bool = value_is_bool;
  s_pending.value_bool    = value_bool;
  s_pending.value_num     = value_num;
}

// ── Public API ────────────────────────────────────────────────────────

void mqttCommandInit(PubSubClient *client, const MqttConfig &cfg) {
  s_client = client;
  s_cfg    = cfg;
  s_inited = true;
  memset(&s_pending, 0, sizeof(s_pending));
  memset(s_dedup,    0, sizeof(s_dedup));
  s_dedupHead = 0;
  client->setCallback(commandCallback);
}

void mqttCommandOnConnect() {
  if (!s_inited || !s_client) return;

  char subTopic[128];
  snprintf(subTopic, sizeof(subTopic),
           "factory/%s/line/+/machine/+/command", s_cfg.factory_id);

  // QoS 1 so the broker retransmits if ESP32 misses the message
  if (s_client->subscribe(subTopic, /*qos=*/1)) {
    Serial.printf("[CMD] Subscribed QoS1: %s\n", subTopic);
  } else {
    Serial.printf("[CMD] Subscribe failed: %s\n", subTopic);
  }
}

void mqttCommandProcess() {
  if (!s_inited || !s_pending.active) return;
  s_pending.active = false;

  // ── 5. Expiry check ───────────────────────────────────────────────
  const uint64_t now = nowMs();
  if (now > 0 && now > s_pending.expires_at_ms) {
    Serial.println("[CMD] ERROR: command expired");
    publishResult(s_pending.result_topic, s_pending.cmd_id, s_pending.factory,
                  s_pending.tag_id,
                  /*ok=*/false, "command expired",
                  false, false, 0.0);
    return;
  }

  // ── 6+7. Tag resolve → Modbus write ──────────────────────────────
  Serial.printf("[COMMAND] Resolving tag...\n");

  CommandWriteValue wval;
  wval.is_bool  = s_pending.value_is_bool;
  wval.bool_val = s_pending.value_bool;
  wval.num_val  = s_pending.value_num;

  const WriteResult wr  = modbusPollWriteTag(s_pending.tag_id, wval);
  const char       *err = writeResultToString(wr);

  if (wr == WriteResult::OK) {
    publishResult(s_pending.result_topic, s_pending.cmd_id, s_pending.factory,
                  s_pending.tag_id,
                  /*ok=*/true, nullptr,
                  s_pending.value_is_bool, s_pending.value_bool, s_pending.value_num);
  } else {
    Serial.printf("[CMD] ERROR: %s\n", err);
    publishResult(s_pending.result_topic, s_pending.cmd_id, s_pending.factory,
                  s_pending.tag_id,
                  /*ok=*/false, err,
                  false, false, 0.0);
  }
}
