#pragma once

#include "AppState.h"
#include "config/BoardConfig.h"
#include <ESPAsyncWebServer.h>

class HX711_ADC;

void setEscThrottlePwm(AppState &state, const BoardConfig &cfg, bool simEnabled, int pulse_width_us);
void triggerSafetyShutdown(AppState &state, const BoardConfig &cfg, bool simEnabled, AsyncWebSocket &ws, const char *reason);
void finishTest(AppState &state, const BoardConfig &cfg, bool simEnabled, AsyncWebSocket &ws);
bool parseAndStoreSequence(AppState &state, const BoardConfig &cfg, const char *sequenceStr);
void resetTest(AppState &state);
void startPreTestTare(AppState &state, const BoardConfig &cfg);
void tickTestRunner(AppState &state, const BoardConfig &cfg, bool simEnabled, HX711_ADC *loadCell, AsyncWebSocket &ws);
void deleteLastResultsFile();
const char *getLastResultsPath();
