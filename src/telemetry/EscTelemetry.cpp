#include "EscTelemetry.h"

#include <Arduino.h>

static volatile uint32_t latestPulseWidth = 0;
static volatile unsigned long pulseStartTime = 0;
static volatile uint32_t lastPulseAtUs = 0;
static const BoardConfig *s_cfg = nullptr;
static const unsigned long TELEM_STALE_MS = 500;

void initEscTelemetry(const BoardConfig &cfg) { s_cfg = &cfg; }

void IRAM_ATTR handleTelemInterrupt() {
  if (!s_cfg) return;
  if (digitalRead(s_cfg->esc_telem_pin) == HIGH) {
    pulseStartTime = micros();
  } else {
    latestPulseWidth = micros() - pulseStartTime;
    lastPulseAtUs = micros();
  }
}

void readEscTelemetry(bool simEnabled,
                      const BoardConfig &cfg,
                      float &escVoltage,
                      float &escCurrent,
                      bool &stale,
                      unsigned long &ageMs) {
  if (simEnabled) {
    stale = false;
    ageMs = 0;
    return;
  }
  uint32_t nowUs = micros();
  uint32_t ageUs = nowUs - lastPulseAtUs;
  ageMs = (lastPulseAtUs == 0) ? 0 : (unsigned long)(ageUs / 1000UL);
  if (lastPulseAtUs == 0 || ageMs > TELEM_STALE_MS) {
    escVoltage = 0.0f;
    escCurrent = 0.0f;
    stale = true;
    return;
  }
  stale = false;
  uint32_t pulse = latestPulseWidth;

  if (pulse >= (uint32_t)cfg.telem_voltage_min && pulse <= (uint32_t)cfg.telem_voltage_max) {
    escVoltage = (pulse - cfg.telem_voltage_min) / cfg.telem_scale;
  } else if (pulse >= (uint32_t)cfg.telem_current_min && pulse <= (uint32_t)cfg.telem_current_max) {
    escCurrent = (pulse - cfg.telem_current_min) / cfg.telem_scale;
  }
}
