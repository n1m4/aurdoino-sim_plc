#pragma once

#include "gateway_config.h"

bool webConfigBegin();

void webConfigLoop();

bool webConfigTimedOut();

void webConfigRebootToWork();
