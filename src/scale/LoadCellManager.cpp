#include "LoadCellManager.h"

#include "FS.h"
#include "LittleFS.h"
#include <Arduino.h>

void saveScaleFactor(const BoardConfig &cfg, float value) {
  File file = LittleFS.open(cfg.scale_factor_file, "w");
  if (file) {
    file.printf("%.6f", value);
    file.close();
    Serial.printf("Scale factor saved: %.6f\n", value);
  } else {
    Serial.println("Failed to save scale factor!");
  }
}

float loadScaleFactor(const BoardConfig &cfg) {
  if (LittleFS.exists(cfg.scale_factor_file)) {
    File file = LittleFS.open(cfg.scale_factor_file, "r");
    if (file) {
      String val = file.readString();
      file.close();
      float loaded = val.toFloat();
      Serial.printf("Loaded scale factor: %.6f\n", loaded);
      return loaded;
    }
  }
  Serial.println("Using default scale factor.");
  return cfg.scale_factor_default;
}

void initLoadCell(bool simEnabled, HX711_ADC *loadCell, const BoardConfig &cfg, AppState &state) {
  if (simEnabled) return;
  if (!loadCell) return;
  loadCell->begin();
  state.scaleFactor = loadScaleFactor(cfg);
  loadCell->setCalFactor(state.scaleFactor);
  Serial.printf("Using scale factor: %.6f\n", state.scaleFactor);
  Serial.println("Taring scale at startup...");
  loadCell->tare();
  Serial.println("Startup Tare Complete.");
}

bool readThrust(bool simEnabled, HX711_ADC *loadCell, AppState &state, float *out) {
  if (out == nullptr) return false;
  if (simEnabled) {
    *out = state.simThrust;
    return true;
  }
  if (loadCell && loadCell->update()) {
    *out = loadCell->getData();
    return true;
  }
  *out = 0.0f;
  return false;
}

void tareScale(bool simEnabled, HX711_ADC *loadCell, AppState &state) {
  if (simEnabled) {
    state.simThrust = 0.0f;
  } else if (loadCell) {
    loadCell->tare();
  }
}

void setScaleFactor(HX711_ADC *loadCell, AppState &state, const BoardConfig &cfg, float value) {
  state.scaleFactor = value;
  if (loadCell) loadCell->setCalFactor(state.scaleFactor);
  saveScaleFactor(cfg, state.scaleFactor);
}

float getScaleFactor(const AppState &state) { return state.scaleFactor; }

long readRawReading(bool simEnabled, HX711_ADC *loadCell, AppState &state, float *weightOut) {
  long raw = 0;
  float weight = 0.0f;
  if (simEnabled) {
    weight = state.simThrust;
    raw = (long)(state.simThrust * state.scaleFactor);
  } else {
    if (loadCell) loadCell->update();
    raw = loadCell ? loadCell->getData() : 0;
    weight = loadCell ? loadCell->getData() : 0.0f;
  }
  if (weightOut) *weightOut = weight;
  return raw;
}
