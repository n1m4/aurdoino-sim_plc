#include "tag_planner.h"

#include <cstring>

#include "board_config.h"
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

/** Insertion sort + drop duplicates (several bit tags can share one register). Returns the new length. */
static uint16_t sortUnique(uint16_t *v, uint16_t n) {
  for (uint16_t i = 1; i < n; i++) {
    uint16_t key = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > key) {
      v[j + 1] = v[j];
      j--;
    }
    v[j + 1] = key;
  }
  uint16_t out = 0;
  for (uint16_t i = 0; i < n; i++) {
    if (out == 0 || v[i] != v[out - 1]) {
      v[out++] = v[i];
    }
  }
  return out;
}

/**
 * Sorted, unique addresses -> read blocks. Two points share a block when at most PLANNER_MAX_GAP unused
 * addresses lie between them and the block stays within MAX_MODBUS_READ_COUNT.
 */
static bool packBlocks(const uint16_t *v, uint16_t n, TagKind kind, ReadBlock *blocks, uint8_t *count,
                       char *errorBuf, size_t errorBufLen) {
  if (n == 0) {
    return true;
  }
  uint16_t blockStart = v[0];
  uint16_t blockEnd = v[0];
  for (uint16_t i = 1; i < n; i++) {
    const uint16_t gap = v[i] - blockEnd - 1;
    if (gap <= PLANNER_MAX_GAP && (v[i] - blockStart + 1) <= MAX_MODBUS_READ_COUNT) {
      blockEnd = v[i];
      continue;
    }
    if (!appendBlock(blocks, count, kind, blockStart, blockEnd - blockStart + 1, errorBuf, errorBufLen)) {
      return false;
    }
    blockStart = v[i];
    blockEnd = v[i];
  }
  return appendBlock(blocks, count, kind, blockStart, blockEnd - blockStart + 1, errorBuf, errorBufLen);
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

  n = sortUnique(s_holdingRegs, n);
  return packBlocks(s_holdingRegs, n, TagKind::Holding, blocks, count, errorBuf, errorBufLen);
}

static bool buildCoilBlocks(const GatewayConfig &cfg, ReadBlock *blocks, uint8_t *count,
                            char *errorBuf, size_t errorBufLen) {
  uint16_t n = 0;
  for (uint8_t i = 0; i < cfg.tag_count; i++) {
    if (cfg.tags[i].kind == TagKind::Coil) {
      s_coilAddrs[n++] = cfg.tags[i].address;
    }
  }
  n = sortUnique(s_coilAddrs, n);
  return packBlocks(s_coilAddrs, n, TagKind::Coil, blocks, count, errorBuf, errorBufLen);
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
