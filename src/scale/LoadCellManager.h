#pragma once

#include "AppState.h"
#include "HX711_ADC.h"
#include "config/BoardConfig.h"

void initLoadCell(bool simEnabled, HX711_ADC *loadCell, const BoardConfig &cfg, AppState &state);
bool readThrust(bool simEnabled, HX711_ADC *loadCell, AppState &state, float *out);
void tareScale(bool simEnabled, HX711_ADC *loadCell, AppState &state);
void setScaleFactor(HX711_ADC *loadCell, AppState &state, const BoardConfig &cfg, float value);
float getScaleFactor(const AppState &state);
void saveScaleFactor(const BoardConfig &cfg, float value);
float loadScaleFactor(const BoardConfig &cfg);
long readRawReading(bool simEnabled, HX711_ADC *loadCell, AppState &state, float *weightOut);
