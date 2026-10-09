#pragma once

/** Built-in LED on ESP32-DevKitC (change if your board differs). */
#define STATUS_LED_PIN 2
/** Set to 1 if LED turns on with LOW (some clones). */
#define LED_ACTIVE_LOW 0

#define CONFIG_FILE_PATH "/config.json"

#define MAX_TAGS 96
#define MAX_READ_BLOCKS 8
#define MAX_MODBUS_READ_COUNT 125

#define MAX_TAG_ID_LEN 24
#define MAX_LABEL_LEN 32
#define MAX_PROFILE_NAME 32
#define MAX_WIFI_SSID 32
#define MAX_WIFI_PASS 64
#define MAX_HOST_LEN 40
#define MAX_PROTOCOL_LEN 16

#define WIFI_CONNECT_TIMEOUT_MS 30000
#define MODBUS_TRANSACTION_TIMEOUT_MS 5000

// ── MQTT ─────────────────────────────────────────────────────────────
#define MAX_MQTT_HOST_LEN     64   // hostname / IP of broker
#define MAX_MQTT_USER_LEN     52   // "edge-{uuid}" → max ~42 + margin
#define MAX_MQTT_PASS_LEN     64
#define MAX_MQTT_CLIENT_ID    36   // e.g. "ESP32-FACTORY01-001"
#define MAX_MQTT_FACTORY_ID   40   // UUID (36) + margin
#define MAX_MQTT_LINE_ID      16   // e.g. "01"
#define MAX_MQTT_MACHINE_ID   32   // e.g. "machine-01"
/** PubSubClient rx/tx buffer — must fit full MQTT packet (topic+payload). */
#define MQTT_BUFFER_SIZE      4096
/** Maximum JSON payload bytes inside one telemetry message.
 *  ~67 tags produce about 2 KB of telemetry JSON. */
#define MQTT_JSON_MAX_LEN     3072
/** Initial reconnect wait (ms); doubles each failure up to MAX.        */
#define MQTT_RECONNECT_MIN_MS 1000
#define MQTT_RECONNECT_MAX_MS 60000

// ── MQTT Command (bidirectional) ──────────────────────────────────────
#define MAX_TAG_UUID_LEN       40   // UUID(36) + NUL margin
#define MAX_CMD_ID_LEN         40   // command_id UUID(36) + NUL
#define CMD_DEDUP_MAX          16   // ring buffer slots for dedup
#define CMD_DEDUP_WINDOW_MS    60000UL   // duplicate window: 60 s
#define CMD_RESULT_TOPIC_MAX   160  // result topic = command topic + "/result"

// ── Schema v2 tag fields ──────────────────────────────────────────────
#define MAX_VPS_NAME_LEN       48   // Tag.name from VPS (MQTT telemetry key)
#define MAX_UNIT_LEN           12   // engineering unit string (optional, informational)
