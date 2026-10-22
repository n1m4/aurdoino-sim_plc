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
 *  4b. Queue full (CMD_QUEUE_MAX)        → silent drop (log only)
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
 * Write every queued command to the PLC, in the order received, and publish a result for each.
 * Call this in the main loop AFTER mqttPublishTask() so the callback has already had a chance to
 * queue commands during s_mqtt->loop(). A work-order start sends 20-30 writes in a row, so this
 * handles the whole queue (bounded by a time budget; the rest waits for the next pass).
 * Returns how many commands were handled.
 */
uint8_t mqttCommandProcess();

/** Number of commands waiting for mqttCommandProcess(). */
uint8_t mqttCommandQueued();
