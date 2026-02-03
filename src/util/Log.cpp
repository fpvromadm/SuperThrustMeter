#include "Log.h"

static void logWithPrefix(const char *prefix, const char *fmt, va_list args) {
  char buffer[192];
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  Serial.print(prefix);
  Serial.println(buffer);
}

void logInfo(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logWithPrefix("[INFO] ", fmt, args);
  va_end(args);
}

void logWarn(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logWithPrefix("[WARN] ", fmt, args);
  va_end(args);
}

void logError(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logWithPrefix("[ERROR] ", fmt, args);
  va_end(args);
}
