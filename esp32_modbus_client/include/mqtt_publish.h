#pragma once

#include "gateway_config.h"

/**
 * Initialise the MQTT publisher.
 * Must be called once after WiFi/Modbus begin, in WORK mode.
 * If cfg.mqtt.broker is empty the module stays disabled.
 */
void mqttPublishBegin(const GatewayConfig &cfg);

/**
 * Build a telemetry JSON payload from the last modbusPollCycle() result
 * and publish it to the VPS broker.
 *
 * Call this right after a successful modbusPollCycle().
 * Safe to call even when MQTT is disabled (returns immediately).
 */
void mqttPublishTelemetry();

/**
 * Keep the PubSubClient connection alive.
 * Call every loop iteration (same pattern as modbusPollTask).
 */
void mqttPublishTask();

/** True when MQTT is enabled and the client is currently connected. */
bool mqttIsConnected();
