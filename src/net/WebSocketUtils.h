#pragma once

#include "config/BoardConfig.h"
#include <ESPAsyncWebServer.h>

void notifyClients(AsyncWebSocket &ws, const BoardConfig &cfg, bool wifiProvisioningMode, const String &message);
void notifyClients(AsyncWebSocket &ws, const BoardConfig &cfg, bool wifiProvisioningMode, const char *message);
bool hasWsClients(AsyncWebSocket &ws);
