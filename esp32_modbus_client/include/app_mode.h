#pragma once

#include "gateway_config.h"

enum class AppRunMode : uint8_t { Config, Work };

/** If boot selects WORK, valid config is written to outCfg (must not be null). */
AppRunMode appModeResolveAtBoot(GatewayConfig *outCfg);

void appModeRequestConfigOnNextBoot();

bool appModeCheckBootHoldDuringRun();

void appModePrintSummary(AppRunMode mode, const GatewayConfig *cfg);
