#include "tag_planner.h"

#include <cstring>

#include "gateway_config.h"

static uint16_t s_holdingRegs[MAX_TAGS * 2];
static uint16_t s_coilAddrs[MAX_TAGS];

static bool appendBlock(ReadBlock *blocks, uint8_t *count, TagKind kind, uint16_t start,
                        uint16_t countRegs, char *errorBuf, size_t errorBufLen) {
  if (*count >= MAX_READ_BLOCKS) {
    snprintf(errorBuf, errorBufLen, "too many read blocks (max %d)", MAX_READ_BLOCKS);
    return false;
  }
  if (countRegs == 0 || countRegs > MAX_MODBUS_READ_COUNT) {
    snprintf(errorBuf, errorBufLen, "block count invalid (%u)", countRegs);
    return false;
  }
  blocks[*count].kind = kind;
  blocks[*count].start = start;
  blocks[*count].count = countRegs;
  (*count)++;
  return true;
}

static bool buildHoldingBlocks(const GatewayConfig &cfg, ReadBlock *blocks, uint8_t *count,
                               char *errorBuf, size_t errorBufLen) {
  uint16_t n = 0;

  for (uint8_t i = 0; i < cfg.tag_count; i++) {
    const TagDef &td = cfg.tags[i];
    if (td.kind != TagKind::Holding) {
      continue;
    }
    const uint16_t span = gatewayTagRegisterSpan(td);
    for (uint16_t r = 0; r < span; r++) {
      if (n >= MAX_TAGS * 2) {
        snprintf(errorBuf, errorBufLen, "too many register points");
        return false;
      }
      s_holdingRegs[n++] = td.address + r;
    }
  }

  if (n == 0) {
    return true;
  }

  for (uint16_t i = 1; i < n; i++) {
    uint16_t key = s_holdingRegs[i];
    int j = i - 1;
    while (j >= 0 && s_holdingRegs[j] > key) {
      s_holdingRegs[j + 1] = s_holdingRegs[j];
      j--;
    }
    s_holdingRegs[j + 1] = key;
  }

  uint16_t blockStart = s_holdingRegs[0];
  uint16_t blockEnd = s_holdingRegs[0];
  for (uint16_t i = 1; i < n; i++) {
    if (s_holdingRegs[i] == blockEnd + 1 &&
        (s_holdingRegs[i] - blockStart + 1) <= MAX_MODBUS_READ_COUNT) {
      blockEnd = s_holdingRegs[i];
      continue;
    }
    if (!appendBlock(blocks, count, TagKind::Holding, blockStart, blockEnd - blockStart + 1,
                     errorBuf, errorBufLen)) {
      return false;
    }
    blockStart = s_holdingRegs[i];
    blockEnd = s_holdingRegs[i];
  }
  return appendBlock(blocks, count, TagKind::Holding, blockStart, blockEnd - blockStart + 1,
                     errorBuf, errorBufLen);
}

static bool buildCoilBlocks(const GatewayConfig &cfg, ReadBlock *blocks, uint8_t *count,
                            char *errorBuf, size_t errorBufLen) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < cfg.tag_count; i++) {
    if (cfg.tags[i].kind == TagKind::Coil) {
      s_coilAddrs[n++] = cfg.tags[i].address;
    }
  }
  if (n == 0) {
    return true;
  }

  for (uint8_t i = 1; i < n; i++) {
    uint16_t key = s_coilAddrs[i];
    int j = i - 1;
    while (j >= 0 && s_coilAddrs[j] > key) {
      s_coilAddrs[j + 1] = s_coilAddrs[j];
      j--;
    }
    s_coilAddrs[j + 1] = key;
  }

  uint16_t blockStart = s_coilAddrs[0];
  uint16_t blockEnd = s_coilAddrs[0];
  for (uint8_t i = 1; i < n; i++) {
    if (s_coilAddrs[i] == blockEnd + 1 &&
        (s_coilAddrs[i] - blockStart + 1) <= MAX_MODBUS_READ_COUNT) {
      blockEnd = s_coilAddrs[i];
      continue;
    }
    if (!appendBlock(blocks, count, TagKind::Coil, blockStart, blockEnd - blockStart + 1, errorBuf,
                     errorBufLen)) {
      return false;
    }
    blockStart = s_coilAddrs[i];
    blockEnd = s_coilAddrs[i];
  }
  return appendBlock(blocks, count, TagKind::Coil, blockStart, blockEnd - blockStart + 1, errorBuf,
                     errorBufLen);
}

bool tagPlannerBuild(const GatewayConfig &cfg, ReadBlock *blocks, uint8_t *blockCountOut,
                     char *errorBuf, size_t errorBufLen) {
  if (!blocks || !blockCountOut) {
    return false;
  }
  *blockCountOut = 0;
  if (!buildHoldingBlocks(cfg, blocks, blockCountOut, errorBuf, errorBufLen)) {
    return false;
  }
  if (!buildCoilBlocks(cfg, blocks, blockCountOut, errorBuf, errorBufLen)) {
    return false;
  }
  return true;
}
