#pragma once

#include "config/BoardConfig.h"
#include <ESPAsyncWebServer.h>

bool authEnabled(const BoardConfig &cfg, bool wifiProvisioningMode);
bool tokenMatches(const BoardConfig &cfg, bool wifiProvisioningMode, const char *token);
bool isAuthorizedRequest(const BoardConfig &cfg, bool wifiProvisioningMode, AsyncWebServerRequest *request);
bool isAuthorizedWsClient(const BoardConfig &cfg, bool wifiProvisioningMode, AsyncWebSocketClient *client);
