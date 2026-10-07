#pragma once

#define BOOT_PIN 0
#define BOOT_HOLD_MS 3000

/** Built-in LED on ESP32-DevKitC (change if your board differs). */
#define STATUS_LED_PIN 2
/** Set to 1 if LED turns on with LOW (some clones). */
#define LED_ACTIVE_LOW 0
#define LED_CONFIG_BLINK_MS 400

#define AP_SSID "PLC-GW-Setup"
#define AP_PASSWORD "setup1234"

#define CONFIG_FILE_PATH "/config.json"
#define CONFIG_TIMEOUT_MS (15UL * 60UL * 1000UL)

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
