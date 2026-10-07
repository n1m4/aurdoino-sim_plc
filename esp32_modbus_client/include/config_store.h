#pragma once

#include "gateway_config.h"

bool configStoreInit();

bool configStoreLoad(GatewayConfig &cfg, char *errorBuf, size_t errorBufLen);

/** Parse file or defaults without full validate (CONFIG UI). */
bool configStoreLoadForEdit(GatewayConfig &cfg);

bool configStoreSave(const GatewayConfig &cfg, char *errorBuf, size_t errorBufLen);

bool configStoreValidate(const GatewayConfig &cfg, char *errorBuf, size_t errorBufLen);

bool configStoreExists();

bool configStoreParseJson(const char *json, size_t len, GatewayConfig &cfg, char *errorBuf,
                          size_t errorBufLen);

bool configStoreParseJsonNoValidate(const char *json, size_t len, GatewayConfig &cfg,
                                    char *errorBuf, size_t errorBufLen);

bool configStoreSerialize(const GatewayConfig &cfg, String &outJson, bool maskWifiPassword);
