#pragma once

#include "AppState.h"
#include "config/BoardConfig.h"

bool simEnabled(const BoardConfig &cfg);
void updateSimTelemetry(AppState &state, const BoardConfig &cfg);
