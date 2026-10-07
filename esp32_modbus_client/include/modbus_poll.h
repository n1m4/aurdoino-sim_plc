#pragma once

#include "gateway_config.h"

class ModbusIP;

bool modbusPollBegin(const GatewayConfig &cfg);

void modbusPollSetConfig(const GatewayConfig &cfg);

bool modbusPollCycle();

void modbusPollPrintValues();

void modbusPollTask();

bool modbusPollCheckBootConfigRequest();

ModbusIP &modbusPollGetClient();
