#pragma once

#include "AppState.h"
#include "config/BoardConfig.h"
#include <ESPAsyncWebServer.h>

class HX711_ADC;

void setupApiRoutes(AsyncWebServer &server, AsyncWebSocket &ws, AppState &state, BoardConfig &cfg, HX711_ADC *loadCell);
