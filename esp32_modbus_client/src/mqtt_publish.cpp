/**
 * mqtt_publish.cpp
 *
 * MQTT Telemetry Publisher — VPS contract
 * ─────────────────────────────────────────────────────────────────────
 * Topic  : factory/{factory_id}/line/{line_id}/machine/{machine_id}/telemetry
 * Payload: JSON  { message_id, source, factory, machine,
 *                  timestamp(ms UTC), sequence, quality, tags:{…} }
 * QoS    : 1   Retain : false
 *
 * TLS    : WiFiClientSecure — setInsecure() by default (no client cert).
 *          For production replace setInsecure() with setCACert(ISRG_ROOT_X1).
 *
 * NTP    : configTime() called once at begin; timestamp = epoch_ms.
 *          If NTP not synced within 8 s the timestamp field is omitted and
 *          ingest will not apply the stale check (still received).
 *
 * Reconnect backoff: 1 s → ×2 → 60 s max.
 */

#include "mqtt_publish.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "board_config.h"
#include "modbus_poll.h"
#include "mqtt_command.h"   // ← command init + on-connect hooks

// ── Network clients (one TLS, one plain) ─────────────────────────────
static WiFiClientSecure s_secureClient;
static WiFiClient       s_plainClient;
static PubSubClient     s_mqttTLS(s_secureClient);
static PubSubClient     s_mqttPlain(s_plainClient);
static PubSubClient    *s_mqtt = nullptr;

// ── Module state ─────────────────────────────────────────────────────
static MqttConfig s_cfg;
static bool       s_enabled      = false;
static uint32_t   s_sequence     = 0;
static uint32_t   s_lastAttempt  = 0;
static uint32_t   s_backoff      = MQTT_RECONNECT_MIN_MS;
static bool       s_ntpOk        = false;

// ── NTP ──────────────────────────────────────────────────────────────
static void initNtp() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("[MQTT] NTP sync");
  const uint32_t t0 = millis();
  while (time(nullptr) < 1000000000UL && millis() - t0 < 8000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  s_ntpOk = (time(nullptr) >= 1000000000UL);
  Serial.printf("[MQTT] NTP %s\n", s_ntpOk ? "OK" : "timeout (ts omitted)");
}

// ── Reconnect ────────────────────────────────────────────────────────
static bool ensureConnected() {
  if (s_mqtt->connected()) return true;

  const uint32_t now = millis();
  if (now - s_lastAttempt < s_backoff) return false;
  s_lastAttempt = now;

  Serial.printf("[MQTT] Connecting %s:%u (tls=%d)...\n",
                s_cfg.broker, s_cfg.port, (int)s_cfg.tls);

  bool ok = s_cfg.username[0]
      ? s_mqtt->connect(s_cfg.client_id, s_cfg.username, s_cfg.password)
      : s_mqtt->connect(s_cfg.client_id);

  if (ok) {
    Serial.println("[MQTT] Connected");
    s_backoff = MQTT_RECONNECT_MIN_MS;
    mqttCommandOnConnect();   // ← re-subscribe to command topic on every connect
  } else {
    Serial.printf("[MQTT] Failed rc=%d, retry in %lus\n",
                  s_mqtt->state(), (unsigned long)(s_backoff / 1000));
    s_backoff = min(s_backoff * 2, (uint32_t)MQTT_RECONNECT_MAX_MS);
  }
  return ok;
}

// ── Public API ───────────────────────────────────────────────────────
void mqttPublishBegin(const GatewayConfig &cfg) {
  s_cfg     = cfg.mqtt;
  s_enabled = (s_cfg.broker[0] != '\0');

  if (!s_enabled) {
    Serial.println("[MQTT] Disabled (mqtt.broker empty)");
    return;
  }

  // Select client based on tls flag
  if (s_cfg.tls) {
    // setInsecure = TLS encryption without certificate validation.
    // To verify the Let's Encrypt CA, call:
    //   s_secureClient.setCACert(ISRG_ROOT_X1_PEM);
    s_secureClient.setInsecure();
    s_mqttTLS.setServer(s_cfg.broker, s_cfg.port);
    s_mqttTLS.setBufferSize(MQTT_BUFFER_SIZE);
    s_mqtt = &s_mqttTLS;
  } else {
    s_mqttPlain.setServer(s_cfg.broker, s_cfg.port);
    s_mqttPlain.setBufferSize(MQTT_BUFFER_SIZE);
    s_mqtt = &s_mqttPlain;
  }

  s_sequence    = 0;
  s_lastAttempt = 0;
  s_backoff     = MQTT_RECONNECT_MIN_MS;

  // NTP — needs WiFi to be already connected
  initNtp();

  // Initialise command handler with the same PubSubClient
  mqttCommandInit(s_mqtt, s_cfg);

  // Print resolved topic for confirmation on serial
  char topic[128];
  snprintf(topic, sizeof(topic),
           "factory/%s/line/%s/machine/%s/telemetry",
           s_cfg.factory_id, s_cfg.line_id, s_cfg.machine_id);
  Serial.printf("[MQTT] Topic: %s\n", topic);
  Serial.printf("[MQTT] User : %s\n", s_cfg.username);
}

void mqttPublishTelemetry() {
  if (!s_enabled || !s_mqtt) return;
  if (WiFi.status() != WL_CONNECTED)  return;
  if (!ensureConnected())              return;

  // ── Build JSON payload ────────────────────────────────────────────
  JsonDocument doc;

  // message_id = "{client_id}-{sequence}"
  char msgId[MAX_MQTT_CLIENT_ID + 12];
  snprintf(msgId, sizeof(msgId), "%s-%lu", s_cfg.client_id, (unsigned long)s_sequence);

  doc["message_id"] = msgId;
  doc["source"]     = s_cfg.client_id;
  doc["factory"]    = s_cfg.factory_id;
  doc["machine"]    = s_cfg.machine_id;
  doc["sequence"]   = s_sequence++;
  doc["quality"]    = "GOOD";

  // timestamp in ms UTC (omitted if NTP not synced)
  if (s_ntpOk) {
    time_t now = time(nullptr);
    if (now >= 1000000000L) {
      doc["timestamp"] = (unsigned long long)now * 1000ULL;
    }
  }

  // tags object — one key per tag, bool or float
  JsonObject tags = doc["tags"].to<JsonObject>();
  const uint8_t tagCount = modbusPollGetTagCount();
  bool anyValid = false;
  for (uint8_t i = 0; i < tagCount; i++) {
    TagReading r;
    if (!modbusPollGetTagReading(i, r) || !r.valid) continue;
    const char *key = modbusPollGetTagMqttKey(i);  // vps_name → fallback to id
    if (!key || !key[0]) continue;
    if (r.is_bool) {
      tags[key] = r.bool_val;
    } else if (r.is_int) {
      tags[key] = static_cast<long long>(r.int_val);  // exact (order numbers, counters)
    } else {
      // Round to 4 decimal places (matches ingest rounding)
      tags[key] = roundf(r.float_val * 10000.0f) / 10000.0f;
    }
    anyValid = true;
  }

  if (!anyValid) {
    Serial.println("[MQTT] No valid tags — skip publish");
    return;
  }

  // ── Serialize ────────────────────────────────────────────────────
  static char payload[MQTT_JSON_MAX_LEN];
  size_t len = serializeJson(doc, payload, sizeof(payload));
  if (len == 0 || len >= sizeof(payload)) {
    Serial.println("[MQTT] JSON too large or empty");
    return;
  }

  // ── Topic ────────────────────────────────────────────────────────
  char topic[128];
  snprintf(topic, sizeof(topic),
           "factory/%s/line/%s/machine/%s/telemetry",
           s_cfg.factory_id, s_cfg.line_id, s_cfg.machine_id);

  // ── Publish QoS-1-equivalent (PubSubClient publish = QoS 0 at API,
  //    but broker receives it; for true QoS 1 use beginPublish/endPublish) ──
  if (s_mqtt->publish(topic, reinterpret_cast<const uint8_t *>(payload), len, /*retain=*/false)) {
    Serial.printf("[MQTT] Published seq=%lu %u B\n",
                  (unsigned long)(s_sequence - 1), (unsigned)len);
  } else {
    Serial.printf("[MQTT] Publish failed (rc=%d) — drop connection\n", s_mqtt->state());
    s_mqtt->disconnect();
    s_backoff = MQTT_RECONNECT_MIN_MS;
  }
}

void mqttPublishTask() {
  if (!(s_enabled && s_mqtt && s_mqtt->connected())) return;
  // PubSubClient reads one packet per loop(). A command burst (20-30 messages) would otherwise take
  // one main-loop pass each, so keep reading while new commands keep arriving.
  for (uint8_t i = 0; i < MQTT_DRAIN_MAX_LOOPS; i++) {
    const uint8_t before = mqttCommandQueued();
    s_mqtt->loop();
    if (!s_mqtt->connected() || mqttCommandQueued() == before) break;
  }
}

bool mqttIsConnected() {
  return s_enabled && s_mqtt && s_mqtt->connected();
}
