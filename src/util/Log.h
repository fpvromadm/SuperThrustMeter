#pragma once

#include <Arduino.h>
#include <stdarg.h>

void logInfo(const char *fmt, ...);
void logWarn(const char *fmt, ...);
void logError(const char *fmt, ...);
