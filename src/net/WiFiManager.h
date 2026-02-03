#pragma once

#include "AppState.h"
#include "config/BoardConfig.h"

bool loadWiFiCredentials(const BoardConfig &cfg, char *ssidBuf, char *passBuf, size_t maxLen);
bool saveWiFiCredentials(const char *ssid, const char *password);
void initWiFi(AppState &state, const BoardConfig &cfg);
void tickWiFiProvisioning(AppState &state, const BoardConfig &cfg);
