#include "EscTelemetry.h"

#include <Arduino.h>

static volatile uint32_t latestPulseWidth = 0;
static volatile unsigned long pulseStartTime = 0;
static const BoardConfig *s_cfg = nullptr;

void initEscTelemetry(const BoardConfig &cfg) { s_cfg = &cfg; }

void IRAM_ATTR handleTelemInterrupt() {
  if (!s_cfg) return;
  if (digitalRead(s_cfg->esc_telem_pin) == HIGH) {
    pulseStartTime = micros();
  } else {
    latestPulseWidth = micros() - pulseStartTime;
  }
}

void readEscTelemetry(bool simEnabled, const BoardConfig &cfg, float &escVoltage, float &escCurrent) {
  if (simEnabled) return;
  uint32_t pulse = latestPulseWidth;

  if (pulse >= (uint32_t)cfg.telem_voltage_min && pulse <= (uint32_t)cfg.telem_voltage_max) {
    escVoltage = (pulse - cfg.telem_voltage_min) / cfg.telem_scale;
  } else if (pulse >= (uint32_t)cfg.telem_current_min && pulse <= (uint32_t)cfg.telem_current_max) {
    escCurrent = (pulse - cfg.telem_current_min) / cfg.telem_scale;
  }
}
