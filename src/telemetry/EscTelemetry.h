#pragma once

#include "config/BoardConfig.h"

void initEscTelemetry(const BoardConfig &cfg);
void handleTelemInterrupt();
void readEscTelemetry(bool simEnabled,
                      const BoardConfig &cfg,
                      float &escVoltage,
                      float &escCurrent,
                      bool &stale,
                      unsigned long &ageMs);
