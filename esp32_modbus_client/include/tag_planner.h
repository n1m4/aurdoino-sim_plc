#pragma once

#include "gateway_config.h"

struct ReadBlock {
  TagKind kind;
  uint16_t start;
  uint16_t count;
};

bool tagPlannerBuild(const GatewayConfig &cfg, ReadBlock *blocks, uint8_t *blockCountOut,
                     char *errorBuf, size_t errorBufLen);
