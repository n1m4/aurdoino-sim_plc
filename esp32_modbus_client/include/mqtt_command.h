#pragma once

/**
 * mqtt_command.h
 *
 * Bidirectional MQTT Command Handler — ESP32 side
 * ─────────────────────────────────────────────────────────────────────
 * Subscribe: factory/{factory_id}/line/+/machine/+/command
 * Publish  : {command_topic}/result
 *
 * Validation order (silent drop unless noted):
 *  1. JSON parse error                   → silent drop
 *  2. Missing required field             → silent drop
 *  3. factory mismatch                   → silent drop
 *  4. Duplicate command_id (60s window)  → silent drop
 *  5. Command expired                    → result ok=false, error="command expired"
 *  6. Tag not found / read-only / error  → result ok=false, error=<string>
 *  7. Modbus Write OK                    → result ok=true
 */

#include <PubSubClient.h>
#include "gateway_config.h"

/**
 * Initialise the command handler.
 * Must be called ONCE with the same PubSubClient used by mqtt_publish.
 * Registers the PubSubClient callback — must be called before any connect.
 */
void mqttCommandInit(PubSubClient *client, const MqttConfig &cfg);

/**
 * Re-subscribe to the command topic after every MQTT (re)connect.
 * Called automatically from mqtt_publish.cpp after a successful connect.
 */
void mqttCommandOnConnect();

/**
 * Process one queued command (if any).
 * Call this in the main loop AFTER mqttPublishTask() so the callback
 * has already had a chance to queue a command during s_mqtt->loop().
 */
void mqttCommandProcess();
