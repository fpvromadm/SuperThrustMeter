#include "FS.h"
#include "HX711_ADC.h"
#include "LittleFS.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <stdarg.h>
#include <vector>
#include "esp_system.h"

#ifndef ENABLE_HEAP_LOG
#define ENABLE_HEAP_LOG 0
#endif

static void logWithPrefix(const char *prefix, const char *fmt, va_list args) {
  char buffer[192];
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  Serial.print(prefix);
  Serial.println(buffer);
}

static void logInfo(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logWithPrefix("[INFO] ", fmt, args);
  va_end(args);
}

static void logWarn(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logWithPrefix("[WARN] ", fmt, args);
  va_end(args);
}

static void logError(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logWithPrefix("[ERROR] ", fmt, args);
  va_end(args);
}

// --- Board config (INI file on LittleFS, never deleted) ---
const char BOARD_CFG_PATH[] = "/board.cfg";

static const char DEFAULT_BOARD_CFG[] PROGMEM = R"(
# Thrust Scale Board Configuration
# Edit and save from the Settings screen. Reboot to apply pin/ESC changes.

[pins]
# HX711 load cell data pin (GPIO)
HX711_DOUT_PIN = 21
# HX711 load cell clock pin (GPIO)
HX711_SCK_PIN = 22
# ESC PWM output pin (GPIO)
ESC_PIN = 27
# ESC telemetry input pin (GPIO)
ESC_TELEM_PIN = 32

[esc]
# PWM channel for ESC (0-15)
ESC_PWM_CHANNEL = 0
# PWM frequency in Hz
PWM_FREQ = 50
# PWM resolution (bits)
PWM_RESOLUTION = 16
# Minimum pulse width in us (1000 typical)
MIN_PULSE_WIDTH = 1000
# Maximum pulse width in us (2000 typical)
MAX_PULSE_WIDTH = 2000

[safety]
# Trigger safety if thrust drops by this many grams while PWM stable
ABNORMAL_THRUST_DROP = 75.0
# How often to check for anomalies (ms)
SAFETY_CHECK_INTERVAL = 100
# PWM above this value enables thrust-drop safety check (us)
SAFETY_PWM_THRESHOLD = 1150

[scale]
# Default calibration factor if no saved value
SCALE_FACTOR_DEFAULT = -204.0
# LittleFS path for scale factor file
SCALE_FACTOR_FILE = /scale_factor.txt

[wifi]
# Legacy LittleFS path for WiFi credentials (NVS is used now)
WIFI_CREDENTIALS_FILE = /wifi.json
# AP name when in provisioning mode
WIFI_AP_NAME = ThrustScale_Setup
# AP password (8+ chars enables WPA2; leave empty for open AP)
WIFI_AP_PASSWORD =
# WiFi connection timeout (ms)
WIFI_CONNECT_TIMEOUT_MS = 10000
# Delay after save before reboot when provisioning (ms)
WIFI_SAVE_REBOOT_DELAY_MS = 2500

[test]
# Maximum number of samples per test run
MAX_TEST_SAMPLES = 6000
# PWM during pre-test tare spinup (us)
PRE_TEST_TARE_PWM = 1100
# Pre-test tare spinup duration (ms)
PRE_TEST_TARE_SPINUP_MS = 2000
# Pre-test tare settle time before tare (ms)
PRE_TEST_TARE_SETTLE_MS = 500
# ESC arming hold time at min throttle (ms)
ESC_ARMING_DELAY_MS = 2100

[esc_telem]
# Voltage pulse range min (us)
TELEM_VOLTAGE_MIN = 1000
# Voltage pulse range max (us)
TELEM_VOLTAGE_MAX = 2000
# Current pulse range min (us)
TELEM_CURRENT_MIN = 2000
# Current pulse range max (us)
TELEM_CURRENT_MAX = 3000
# Scale factor for voltage/current
TELEM_SCALE = 100.0

[security]
# Shared auth token required for HTTP/WS access. Empty disables auth.
AUTH_TOKEN =

[sim]
# Enable simulated sensor/ESC data (1 = on, 0 = off)
SIM_ENABLED = 0
# Max simulated thrust in grams at max PWM
SIM_THRUST_MAX_G = 2000.0
# Noise amplitude in grams (+/-)
SIM_NOISE_G = 5.0
# First-order response time (ms)
SIM_RESPONSE_MS = 250
# Fixed simulated voltage
SIM_VOLTAGE = 16.0
# Max simulated current at max PWM
SIM_CURRENT_MAX = 60.0
# Random seed (0 = auto)
SIM_SEED = 0
)";

struct BoardConfig {
  int hx711_dout_pin, hx711_sck_pin, esc_pin, esc_telem_pin;
  int esc_pwm_channel, pwm_freq, pwm_resolution, min_pulse_width, max_pulse_width;
  float abnormal_thrust_drop;
  unsigned long safety_check_interval;
  int safety_pwm_threshold;
  float scale_factor_default;
  char scale_factor_file[48];
  char wifi_credentials_file[48];
  char wifi_ap_name[32];
  char wifi_ap_password[64];
  unsigned long wifi_connect_timeout_ms, wifi_save_reboot_delay_ms;
  size_t max_test_samples;
  int pre_test_tare_pwm;
  unsigned long pre_test_tare_spinup_ms, pre_test_tare_settle_ms, esc_arming_delay_ms;
  int telem_voltage_min, telem_voltage_max, telem_current_min, telem_current_max;
  float telem_scale;
  char auth_token[48];
  bool sim_enabled;
  float sim_thrust_max_g;
  float sim_noise_g;
  unsigned long sim_response_ms;
  float sim_voltage;
  float sim_current_max;
  uint32_t sim_seed;
};
BoardConfig boardConfig;

static void setBoardConfigDefaults();
static void ensureConfigExists();
static bool loadBoardConfig();
static bool parseConfigContent(const char *content, bool strictMode);

void setBoardConfigDefaults() {
  boardConfig.hx711_dout_pin = 21;
  boardConfig.hx711_sck_pin = 22;
  boardConfig.esc_pin = 27;
  boardConfig.esc_telem_pin = 32;
  boardConfig.esc_pwm_channel = 0;
  boardConfig.pwm_freq = 50;
  boardConfig.pwm_resolution = 16;
  boardConfig.min_pulse_width = 1000;
  boardConfig.max_pulse_width = 2000;
  boardConfig.abnormal_thrust_drop = 75.0f;
  boardConfig.safety_check_interval = 100;
  boardConfig.safety_pwm_threshold = 1150;
  boardConfig.scale_factor_default = -204.0f;
  strncpy(boardConfig.scale_factor_file, "/scale_factor.txt", sizeof(boardConfig.scale_factor_file) - 1);
  boardConfig.scale_factor_file[sizeof(boardConfig.scale_factor_file) - 1] = '\0';
  strncpy(boardConfig.wifi_credentials_file, "/wifi.json", sizeof(boardConfig.wifi_credentials_file) - 1);
  boardConfig.wifi_credentials_file[sizeof(boardConfig.wifi_credentials_file) - 1] = '\0';
  strncpy(boardConfig.wifi_ap_name, "ThrustScale_Setup", sizeof(boardConfig.wifi_ap_name) - 1);
  boardConfig.wifi_ap_name[sizeof(boardConfig.wifi_ap_name) - 1] = '\0';
  boardConfig.wifi_ap_password[0] = '\0';
  boardConfig.wifi_connect_timeout_ms = 10000;
  boardConfig.wifi_save_reboot_delay_ms = 2500;
  boardConfig.max_test_samples = 6000;
  boardConfig.pre_test_tare_pwm = 1100;
  boardConfig.pre_test_tare_spinup_ms = 2000;
  boardConfig.pre_test_tare_settle_ms = 500;
  boardConfig.esc_arming_delay_ms = 2100;
  boardConfig.telem_voltage_min = 1000;
  boardConfig.telem_voltage_max = 2000;
  boardConfig.telem_current_min = 2000;
  boardConfig.telem_current_max = 3000;
  boardConfig.telem_scale = 100.0f;
  boardConfig.auth_token[0] = '\0';
  boardConfig.sim_enabled = false;
  boardConfig.sim_thrust_max_g = 2000.0f;
  boardConfig.sim_noise_g = 5.0f;
  boardConfig.sim_response_ms = 250;
  boardConfig.sim_voltage = 16.0f;
  boardConfig.sim_current_max = 60.0f;
  boardConfig.sim_seed = 0;
}

static bool setConfigKey(const char *section, const char *key, const char *value) {
  if (strcmp(section, "pins") == 0) {
    if (strcmp(key, "HX711_DOUT_PIN") == 0) { int v = atoi(value); if (v >= 0 && v <= 39) boardConfig.hx711_dout_pin = v; return true; }
    if (strcmp(key, "HX711_SCK_PIN") == 0) { int v = atoi(value); if (v >= 0 && v <= 39) boardConfig.hx711_sck_pin = v; return true; }
    if (strcmp(key, "ESC_PIN") == 0) { int v = atoi(value); if (v >= 0 && v <= 39) boardConfig.esc_pin = v; return true; }
    if (strcmp(key, "ESC_TELEM_PIN") == 0) { int v = atoi(value); if (v >= 0 && v <= 39) boardConfig.esc_telem_pin = v; return true; }
  }
  if (strcmp(section, "esc") == 0) {
    if (strcmp(key, "ESC_PWM_CHANNEL") == 0) { int v = atoi(value); if (v >= 0 && v <= 15) boardConfig.esc_pwm_channel = v; return true; }
    if (strcmp(key, "PWM_FREQ") == 0) { int v = atoi(value); if (v > 0 && v <= 40000) boardConfig.pwm_freq = v; return true; }
    if (strcmp(key, "PWM_RESOLUTION") == 0) { int v = atoi(value); if (v >= 1 && v <= 16) boardConfig.pwm_resolution = v; return true; }
    if (strcmp(key, "MIN_PULSE_WIDTH") == 0) { int v = atoi(value); if (v >= 500 && v <= 2500) boardConfig.min_pulse_width = v; return true; }
    if (strcmp(key, "MAX_PULSE_WIDTH") == 0) { int v = atoi(value); if (v >= 500 && v <= 2500) boardConfig.max_pulse_width = v; return true; }
  }
  if (strcmp(section, "safety") == 0) {
    if (strcmp(key, "ABNORMAL_THRUST_DROP") == 0) { float v = atof(value); if (v >= 0 && v <= 500) boardConfig.abnormal_thrust_drop = v; return true; }
    if (strcmp(key, "SAFETY_CHECK_INTERVAL") == 0) { unsigned long v = atol(value); if (v >= 10 && v <= 10000) boardConfig.safety_check_interval = v; return true; }
    if (strcmp(key, "SAFETY_PWM_THRESHOLD") == 0) { int v = atoi(value); if (v >= 1000 && v <= 2000) boardConfig.safety_pwm_threshold = v; return true; }
  }
  if (strcmp(section, "scale") == 0) {
    if (strcmp(key, "SCALE_FACTOR_DEFAULT") == 0) { boardConfig.scale_factor_default = atof(value); return true; }
    if (strcmp(key, "SCALE_FACTOR_FILE") == 0) { strncpy(boardConfig.scale_factor_file, value, sizeof(boardConfig.scale_factor_file) - 1); boardConfig.scale_factor_file[sizeof(boardConfig.scale_factor_file) - 1] = '\0'; return true; }
  }
  if (strcmp(section, "wifi") == 0) {
    if (strcmp(key, "WIFI_CREDENTIALS_FILE") == 0) { strncpy(boardConfig.wifi_credentials_file, value, sizeof(boardConfig.wifi_credentials_file) - 1); boardConfig.wifi_credentials_file[sizeof(boardConfig.wifi_credentials_file) - 1] = '\0'; return true; }
    if (strcmp(key, "WIFI_AP_NAME") == 0) { strncpy(boardConfig.wifi_ap_name, value, sizeof(boardConfig.wifi_ap_name) - 1); boardConfig.wifi_ap_name[sizeof(boardConfig.wifi_ap_name) - 1] = '\0'; return true; }
    if (strcmp(key, "WIFI_AP_PASSWORD") == 0) { strncpy(boardConfig.wifi_ap_password, value, sizeof(boardConfig.wifi_ap_password) - 1); boardConfig.wifi_ap_password[sizeof(boardConfig.wifi_ap_password) - 1] = '\0'; return true; }
    if (strcmp(key, "WIFI_CONNECT_TIMEOUT_MS") == 0) { unsigned long v = atol(value); if (v >= 1000) boardConfig.wifi_connect_timeout_ms = v; return true; }
    if (strcmp(key, "WIFI_SAVE_REBOOT_DELAY_MS") == 0) { unsigned long v = atol(value); if (v <= 10000) boardConfig.wifi_save_reboot_delay_ms = v; return true; }
  }
  if (strcmp(section, "test") == 0) {
    if (strcmp(key, "MAX_TEST_SAMPLES") == 0) { int v = atoi(value); if (v >= 100 && v <= 20000) boardConfig.max_test_samples = (size_t)v; return true; }
    if (strcmp(key, "PRE_TEST_TARE_PWM") == 0) { int v = atoi(value); if (v >= 1000 && v <= 2000) boardConfig.pre_test_tare_pwm = v; return true; }
    if (strcmp(key, "PRE_TEST_TARE_SPINUP_MS") == 0) { unsigned long v = atol(value); if (v <= 60000) boardConfig.pre_test_tare_spinup_ms = v; return true; }
    if (strcmp(key, "PRE_TEST_TARE_SETTLE_MS") == 0) { unsigned long v = atol(value); if (v <= 10000) boardConfig.pre_test_tare_settle_ms = v; return true; }
    if (strcmp(key, "ESC_ARMING_DELAY_MS") == 0) { unsigned long v = atol(value); if (v >= 1000 && v <= 30000) boardConfig.esc_arming_delay_ms = v; return true; }
  }
  if (strcmp(section, "esc_telem") == 0) {
    if (strcmp(key, "TELEM_VOLTAGE_MIN") == 0) { int v = atoi(value); if (v >= 0) boardConfig.telem_voltage_min = v; return true; }
    if (strcmp(key, "TELEM_VOLTAGE_MAX") == 0) { int v = atoi(value); if (v >= 0) boardConfig.telem_voltage_max = v; return true; }
    if (strcmp(key, "TELEM_CURRENT_MIN") == 0) { int v = atoi(value); if (v >= 0) boardConfig.telem_current_min = v; return true; }
    if (strcmp(key, "TELEM_CURRENT_MAX") == 0) { int v = atoi(value); if (v >= 0) boardConfig.telem_current_max = v; return true; }
    if (strcmp(key, "TELEM_SCALE") == 0) { float v = atof(value); if (v > 0) boardConfig.telem_scale = v; return true; }
  }
  if (strcmp(section, "security") == 0) {
    if (strcmp(key, "AUTH_TOKEN") == 0) { strncpy(boardConfig.auth_token, value, sizeof(boardConfig.auth_token) - 1); boardConfig.auth_token[sizeof(boardConfig.auth_token) - 1] = '\0'; return true; }
  }
  if (strcmp(section, "sim") == 0) {
    if (strcmp(key, "SIM_ENABLED") == 0) { int v = atoi(value); boardConfig.sim_enabled = (v != 0); return true; }
    if (strcmp(key, "SIM_THRUST_MAX_G") == 0) { float v = atof(value); if (v >= 0) boardConfig.sim_thrust_max_g = v; return true; }
    if (strcmp(key, "SIM_NOISE_G") == 0) { float v = atof(value); if (v >= 0) boardConfig.sim_noise_g = v; return true; }
    if (strcmp(key, "SIM_RESPONSE_MS") == 0) { unsigned long v = atol(value); if (v <= 10000) boardConfig.sim_response_ms = v; return true; }
    if (strcmp(key, "SIM_VOLTAGE") == 0) { float v = atof(value); if (v >= 0) boardConfig.sim_voltage = v; return true; }
    if (strcmp(key, "SIM_CURRENT_MAX") == 0) { float v = atof(value); if (v >= 0) boardConfig.sim_current_max = v; return true; }
    if (strcmp(key, "SIM_SEED") == 0) { unsigned long v = atol(value); boardConfig.sim_seed = (uint32_t)v; return true; }
  }
  return false;
}

bool parseConfigContent(const char *content, bool strictMode) {
  const char *p = content;
  char section[32] = "";
  while (*p) {
    const char *lineStart = p;
    while (*p && *p != '\n') p++;
    String line(lineStart, p - lineStart);
    line.trim();
    if (line.length() == 0 || line.startsWith("#")) { if (*p) p++; continue; }
    if (line.startsWith("[")) {
      int end = line.indexOf(']');
      if (end > 1) {
        line.substring(1, end).toCharArray(section, sizeof(section));
        for (size_t i = 0; section[i]; i++) section[i] = tolower((unsigned char)section[i]);
      }
      if (*p) p++;
      continue;
    }
    int eq = line.indexOf('=');
    if (eq <= 0) { if (*p) p++; continue; }
    String key = line.substring(0, eq);
    key.trim();
    String val = line.substring(eq + 1);
    val.trim();
    if (key.length() == 0) { if (*p) p++; continue; }
    char secCopy[32];
    strncpy(secCopy, section, sizeof(secCopy) - 1);
    secCopy[sizeof(secCopy) - 1] = '\0';
    if (strictMode && !setConfigKey(secCopy, key.c_str(), val.c_str()))
      return false;
    else if (!strictMode)
      setConfigKey(secCopy, key.c_str(), val.c_str());
    if (*p) p++;
  }
  return true;
}

void ensureConfigExists() {
  if (LittleFS.exists(BOARD_CFG_PATH))
    return;
  File f = LittleFS.open(BOARD_CFG_PATH, "w");
  if (!f) {
    Serial.println("Failed to create board.cfg");
    return;
  }
  size_t len = strlen_P(DEFAULT_BOARD_CFG);
  for (size_t i = 0; i < len; i++)
    f.write((char)pgm_read_byte(DEFAULT_BOARD_CFG + i));
  f.close();
  Serial.println("Created default board.cfg");
}

bool loadBoardConfig() {
  setBoardConfigDefaults();
  if (!LittleFS.exists(BOARD_CFG_PATH)) {
    ensureConfigExists();
    return true;
  }
  File f = LittleFS.open(BOARD_CFG_PATH, "r");
  if (!f) {
    Serial.println("Config file read failed, using defaults");
    File w = LittleFS.open(BOARD_CFG_PATH, "w");
    if (w) {
      size_t len = strlen_P(DEFAULT_BOARD_CFG);
      for (size_t i = 0; i < len; i++)
        w.write((char)pgm_read_byte(DEFAULT_BOARD_CFG + i));
      w.close();
      Serial.println("Repaired board.cfg with defaults");
    }
    return true;
  }
  String content = f.readString();
  f.close();
  if (!parseConfigContent(content.c_str(), false)) {
    Serial.println("Config parse failed, using defaults");
    setBoardConfigDefaults();
    File w = LittleFS.open(BOARD_CFG_PATH, "w");
    if (w) {
      size_t len = strlen_P(DEFAULT_BOARD_CFG);
      for (size_t i = 0; i < len; i++)
        w.write((char)pgm_read_byte(DEFAULT_BOARD_CFG + i));
      w.close();
      Serial.println("Repaired board.cfg with defaults");
    }
    return true;
  }
  return true;
}

// --- WiFi Provisioning ---
bool wifiProvisioningMode = false;

bool loadWiFiCredentials(char *ssidBuf, char *passBuf, size_t maxLen) {
  if (ssidBuf == nullptr || passBuf == nullptr || maxLen == 0)
    return false;
  ssidBuf[0] = '\0';
  passBuf[0] = '\0';

  Preferences prefs;
  if (prefs.begin("wifi", true)) {
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();
    if (ssid.length() > 0) {
      ssid.toCharArray(ssidBuf, maxLen);
      pass.toCharArray(passBuf, maxLen);
      return true;
    }
  }

  // Legacy LittleFS fallback (migrate to NVS)
  if (!LittleFS.exists(boardConfig.wifi_credentials_file))
    return false;
  File file = LittleFS.open(boardConfig.wifi_credentials_file, "r");
  if (!file)
    return false;
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err || !doc["ssid"].is<const char *>() || !doc["password"].is<const char *>())
    return false;
  const char *ssid = doc["ssid"].as<const char *>();
  const char *pass = doc["password"].as<const char *>();
  strncpy(ssidBuf, ssid, maxLen - 1);
  ssidBuf[maxLen - 1] = '\0';
  strncpy(passBuf, pass, maxLen - 1);
  passBuf[maxLen - 1] = '\0';

  Preferences writePrefs;
  if (writePrefs.begin("wifi", false)) {
    writePrefs.putString("ssid", ssid);
    writePrefs.putString("pass", pass);
    writePrefs.end();
  }
  // Optional: keep the legacy file; uncomment to delete after migration
  // LittleFS.remove(boardConfig.wifi_credentials_file);
  return true;
}

bool saveWiFiCredentials(const char *ssid, const char *password) {
  if (ssid == nullptr || password == nullptr)
    return false;
  Preferences prefs;
  if (!prefs.begin("wifi", false))
    return false;
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  prefs.end();
  Serial.println("WiFi credentials saved to NVS.");
  return true;
}

// --- Global Objects ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
HX711_ADC *LoadCell = nullptr;

static float escVoltage = 0.0f;
static float escCurrent = 0.0f;
int currentPwm = 1000;
int previousPwmForRamp = 1000;

static bool simEnabled() {
  return boardConfig.sim_enabled;
}

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static float simThrust = 0.0f;
static unsigned long lastSimUpdateMs = 0;
static unsigned long lastSimSampleMs = 0;

static void updateSimTelemetry() {
  if (!simEnabled()) return;
  unsigned long now = millis();
  if (lastSimUpdateMs == 0) {
    lastSimUpdateMs = now;
    return;
  }
  unsigned long dt = now - lastSimUpdateMs;
  if (dt == 0) return;
  lastSimUpdateMs = now;

  float denom = (float)(boardConfig.max_pulse_width - boardConfig.min_pulse_width);
  float throttle = (denom > 0.0f)
                       ? (float)(currentPwm - boardConfig.min_pulse_width) / denom
                       : 0.0f;
  throttle = clampf(throttle, 0.0f, 1.0f);

  float noise = 0.0f;
  if (boardConfig.sim_noise_g > 0.0f) {
    long r = random(-1000, 1000);
    noise = ((float)r / 1000.0f) * boardConfig.sim_noise_g;
  }

  float target = (throttle * boardConfig.sim_thrust_max_g) + noise;
  float alpha = 1.0f;
  if (boardConfig.sim_response_ms > 0) {
    alpha = clampf((float)dt / (float)boardConfig.sim_response_ms, 0.0f, 1.0f);
  }
  simThrust = simThrust + (target - simThrust) * alpha;

  escVoltage = boardConfig.sim_voltage;
  escCurrent = boardConfig.sim_current_max * throttle;
}

static bool readThrust(float *out) {
  if (out == nullptr)
    return false;
  if (simEnabled()) {
    updateSimTelemetry();
    *out = simThrust;
    return true;
  }
  if (LoadCell && LoadCell->update()) {
    *out = LoadCell->getData();
    return true;
  }
  *out = 0.0f;
  return false;
}

static bool authEnabled() {
  if (wifiProvisioningMode)
    return false;
  if (boardConfig.auth_token[0] == '\0')
    return false;
  if (strcmp(boardConfig.auth_token, "changeme") == 0)
    return false;
  return true;
}

static bool tokenMatches(const char *token) {
  if (!authEnabled())
    return true;
  if (token == nullptr)
    return false;
  return strcmp(token, boardConfig.auth_token) == 0;
}

static bool isAuthorizedRequest(AsyncWebServerRequest *request) {
  if (!authEnabled())
    return true;
  if (request->hasHeader("X-Auth-Token")) {
    AsyncWebHeader *h = request->getHeader("X-Auth-Token");
    if (tokenMatches(h->value().c_str()))
      return true;
  }
  if (request->hasHeader("Authorization")) {
    AsyncWebHeader *h = request->getHeader("Authorization");
    const String value = h->value();
    const String prefix = "Bearer ";
    if (value.startsWith(prefix)) {
      String token = value.substring(prefix.length());
      if (tokenMatches(token.c_str()))
        return true;
    }
  }
  if (request->hasParam("token")) {
    AsyncWebParameter *p = request->getParam("token");
    if (tokenMatches(p->value().c_str()))
      return true;
  }
  return false;
}

static bool isAuthorizedWsClient(AsyncWebSocketClient *client) {
  if (!authEnabled())
    return true;
  if (client == nullptr)
    return false;
  return client->_tempObject == (void *)1;
}

// --- State Management ---
enum State {
  IDLE,
  ARMING,
  PRE_TEST_TARE,
  RUNNING_SEQUENCE,
  SAFETY_SHUTDOWN,
  TEST_FINISHED
};
State currentState = IDLE;

// --- Data & Sequence Storage ---
struct DataPoint {
  unsigned long timestamp;
  float thrust;
  int pwm;
};
std::vector<DataPoint> testResults;

struct TestStep {
  int pwm;
  unsigned long spinup_ms;
  unsigned long stable_ms;
};
std::vector<TestStep> testSequence;

// Timers and trackers
unsigned long testStartTime = 0;
unsigned long stepStartTime = 0;
int currentSequenceStep = 0;
bool testResultsFullLogged = false;
unsigned long lastTelemetryMs = 0;
static const unsigned long TELEMETRY_INTERVAL_MS = 200;

// Safety trackers
float lastThrustForSafetyCheck = 0.0;
unsigned long lastSafetyCheckTime = 0;

#if ENABLE_HEAP_LOG
unsigned long lastHeapLogTime = 0;
#endif

// Non-blocking state timers
unsigned long armingStartTime = 0;
bool preTestSettling = false;
unsigned long preTestSettleStart = 0;

// WiFi provisioning async connect
AsyncWebServerRequest *pendingWifiRequest = nullptr;
String pendingWifiSsid;
String pendingWifiPassword;
unsigned long pendingWifiStartTime = 0;
unsigned long rebootAtMs = 0;

// --- Scale Factor Persistent Storage ---
float scaleFactor = -204.0;

void saveScaleFactor(float value) {
  File file = LittleFS.open(boardConfig.scale_factor_file, "w");
  if (file) {
    file.printf("%.6f", value);
    file.close();
    Serial.printf("Scale factor saved: %.6f\n", value);
  } else {
    Serial.println("Failed to save scale factor!");
  }
}

float loadScaleFactor() {
  if (LittleFS.exists(boardConfig.scale_factor_file)) {
    File file = LittleFS.open(boardConfig.scale_factor_file, "r");
    if (file) {
      String val = file.readString();
      file.close();
      float loaded = val.toFloat();
      Serial.printf("Loaded scale factor: %.6f\n", loaded);
      return loaded;
    }
  }
  Serial.println("Using default scale factor.");
  return boardConfig.scale_factor_default;
}

// --- Helper Functions ---
void notifyClients(String message) {
  auto clients = ws.getClients();
  for (auto clientPtr : clients) {
    AsyncWebSocketClient *client = clientPtr;
    if (isAuthorizedWsClient(client)) {
      client->text(message);
    }
  }
}

void setEscThrottlePwm(int pulse_width_us) {
  if (pulse_width_us < boardConfig.min_pulse_width)
    pulse_width_us = boardConfig.min_pulse_width;
  if (pulse_width_us > boardConfig.max_pulse_width)
    pulse_width_us = boardConfig.max_pulse_width;

  currentPwm = pulse_width_us;

  if (!simEnabled()) {
    uint32_t duty = (65535UL * (uint32_t)pulse_width_us) / 20000;
    ledcWrite(boardConfig.esc_pwm_channel, duty);
  }
}

void triggerSafetyShutdown(const char *reason) {
  setEscThrottlePwm(boardConfig.min_pulse_width);
  currentState = SAFETY_SHUTDOWN;
  Serial.printf("SAFETY SHUTDOWN TRIGGERED: %s\n", reason);

  StaticJsonDocument<200> doc;
  doc["type"] = "safety_shutdown";
  doc["message"] = reason;
  String output;
  serializeJson(doc, output);
  notifyClients(output);
  testSequence.clear();
}

static const size_t FINAL_RESULTS_CHUNK_SIZE = 100;

void finishTest() {
  setEscThrottlePwm(boardConfig.min_pulse_width);
  currentState = TEST_FINISHED;
  Serial.println("Test sequence finished.");

  StaticJsonDocument<200> doc;
  doc["type"] = "status";
  doc["message"] = "Test finished. Sending final results.";
  String output;
  serializeJson(doc, output);
  notifyClients(output);

  // Send final results in chunks to reduce memory pressure
  const size_t totalPoints = testResults.size();
  {
    StaticJsonDocument<128> startDoc;
    startDoc["type"] = "final_results_start";
    startDoc["total"] = (uint32_t)totalPoints;
    String startOut;
    serializeJson(startDoc, startOut);
    notifyClients(startOut);
  }

  for (size_t i = 0; i < totalPoints; i += FINAL_RESULTS_CHUNK_SIZE) {
    DynamicJsonDocument chunkDoc(8192);
    chunkDoc["type"] = "final_results_chunk";
    chunkDoc["index"] = (uint32_t)i;
    JsonArray data = chunkDoc.createNestedArray("data");
    size_t end = i + FINAL_RESULTS_CHUNK_SIZE;
    if (end > totalPoints)
      end = totalPoints;
    for (size_t j = i; j < end; j++) {
      JsonObject dataPoint = data.createNestedObject();
      dataPoint["time"] = testResults[j].timestamp;
      dataPoint["thrust"] = testResults[j].thrust;
      dataPoint["pwm"] = testResults[j].pwm;
    }
    String chunkOut;
    serializeJson(chunkDoc, chunkOut);
    notifyClients(chunkOut);
  }

  {
    StaticJsonDocument<128> endDoc;
    endDoc["type"] = "final_results_end";
    String endOut;
    serializeJson(endDoc, endOut);
    notifyClients(endOut);
  }

  // Clear data for next run
  testResults.clear();
  currentState = IDLE;
}

bool parseAndStoreSequence(const char *sequenceStr) {
  testSequence.clear();
  char *sequenceCopy =
      strdup(sequenceStr); // Make a copy since strtok modifies the string
  char *stepToken = strtok(sequenceCopy, ";");

  while (stepToken != NULL) {
    TestStep step;
    int pwm, spinup, stable;
    if (sscanf(stepToken, "%d - %d - %d", &pwm, &spinup, &stable) == 3) {
      step.pwm = pwm;
      step.spinup_ms = spinup * 1000;
      step.stable_ms = stable * 1000;
      testSequence.push_back(step);
    } else {
      Serial.printf("Failed to parse step: %s\n", stepToken);
      free(sequenceCopy);
      return false; // Parsing failed
    }
    stepToken = strtok(NULL, ";");
  }
  free(sequenceCopy);
  return !testSequence.empty();
}

// --- WebSocket Event Handler ---
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    if (client)
      client->_tempObject = nullptr;
    if (client)
      client->keepAlivePeriod(10);
    Serial.printf("WebSocket client #%u connected\n", client->id());
    if (simEnabled() && client) {
      StaticJsonDocument<128> doc;
      doc["type"] = "status";
      doc["message"] = "Simulation mode active.";
      String output;
      serializeJson(doc, output);
      client->text(output);
    }
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len &&
        info->opcode == WS_TEXT) {
      data[len] = 0;

      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, (char *)data);
      if (error) {
        return;
      }

      const char *command = doc["command"];

      if (authEnabled() && !isAuthorizedWsClient(client)) {
        const char *token = doc["token"];
        if (!tokenMatches(token)) {
          logWarn("WebSocket unauthorized message");
          if (client)
            client->close();
          return;
        }
        if (client)
          client->_tempObject = (void *)1;
        if (command && strcmp(command, "auth") == 0)
          return;
      }

      if (!command)
        return;

      if (strcmp(command, "ping") == 0) {
        StaticJsonDocument<64> resp;
        resp["type"] = "pong";
        String out;
        serializeJson(resp, out);
        if (client)
          client->text(out);
        return;
      }

      if (!command)
        return;

      if (strcmp(command, "start_test") == 0) {
        if (currentState == IDLE) {
          const char *sequence = doc["sequence"];
          Serial.printf("Received test sequence: %s\n", sequence);
          if (parseAndStoreSequence(sequence)) {
            Serial.println(
                "Sequence parsed successfully. Starting pre-test tare.");
            currentState = PRE_TEST_TARE;
            stepStartTime = millis(); // Use step timer for pre-tare
            preTestSettling = false;
            preTestSettleStart = 0;
          } else {
            triggerSafetyShutdown("Invalid test sequence format.");
          }
        }
      } else if (strcmp(command, "stop_test") == 0) {
        triggerSafetyShutdown("Test stopped by user.");
      } else if (strcmp(command, "reset") == 0) {
        setEscThrottlePwm(boardConfig.min_pulse_width);
        currentState = IDLE;
        testResults.clear();
        testSequence.clear();
        lastSimSampleMs = 0;
        lastSimUpdateMs = 0;
        notifyClients("{\"type\":\"status\", \"message\":\"System reset.\"}");
      } else if (strcmp(command, "tare") == 0) {
        if (simEnabled()) {
          simThrust = 0.0f;
        } else if (LoadCell) {
          LoadCell->tare();
        }
        notifyClients("{\"type\":\"status\", \"message\":\"Scale tared.\"}");
      } else if (strcmp(command, "set_scale_factor") == 0) {
        if (doc.containsKey("value")) {
          float newFactor = doc["value"];
          scaleFactor = newFactor;
          if (LoadCell) LoadCell->setCalFactor(scaleFactor);
          saveScaleFactor(scaleFactor);
          StaticJsonDocument<128> resp;
          resp["type"] = "scale_factor";
          resp["value"] = scaleFactor;
          String out;
          serializeJson(resp, out);
          notifyClients(out);
          Serial.printf("Scale factor set to: %.6f\n", newFactor);
        }
      } else if (strcmp(command, "get_scale_factor") == 0) {
        StaticJsonDocument<128> resp;
        resp["type"] = "scale_factor";
        resp["value"] = scaleFactor;
        String out;
        serializeJson(resp, out);
        notifyClients(out);
      } else if (strcmp(command, "get_raw_reading") == 0) {
        long raw = 0;
        float weight = 0.0f;
        if (simEnabled()) {
          updateSimTelemetry();
          weight = simThrust;
          raw = (long)(simThrust * scaleFactor);
        } else {
          if (LoadCell) LoadCell->update();
          raw = LoadCell ? LoadCell->getData() : 0;
          weight = LoadCell ? LoadCell->getData() : 0.0f;
        }
        StaticJsonDocument<128> resp;
        resp["type"] = "raw_reading";
        resp["raw"] = raw;
        resp["weight"] = weight;
        resp["factor"] = scaleFactor;
        String out;
        serializeJson(resp, out);
        notifyClients(out);
      }
    }
  }
}

// --- Initialization ---
void initWiFi() {
  char savedSsid[64];
  char savedPass[64];
  if (!loadWiFiCredentials(savedSsid, savedPass, sizeof(savedSsid))) {
    Serial.println("No WiFi credentials. Starting setup AP.");
    wifiProvisioningMode = true;
    WiFi.mode(WIFI_AP);
    if (strlen(boardConfig.wifi_ap_password) >= 8) {
      WiFi.softAP(boardConfig.wifi_ap_name, boardConfig.wifi_ap_password);
    } else {
      WiFi.softAP(boardConfig.wifi_ap_name);
    }
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid, savedPass);
  Serial.print("Connecting to WiFi ..");

  const unsigned long timeoutMs = boardConfig.wifi_connect_timeout_ms;
  unsigned long startAttemptTime = millis();
  bool connected = false;
  while (millis() - startAttemptTime < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    }
    Serial.print('.');
    delay(500);
  }

  if (connected) {
    Serial.println("\nConnected!");
    Serial.println(WiFi.localIP());
    wifiProvisioningMode = false;
    return;
  }

  Serial.println("\nWiFi connection failed. Starting setup AP.");
  wifiProvisioningMode = true;
  WiFi.mode(WIFI_AP);
  if (strlen(boardConfig.wifi_ap_password) >= 8) {
    WiFi.softAP(boardConfig.wifi_ap_name, boardConfig.wifi_ap_password);
  } else {
    WiFi.softAP(boardConfig.wifi_ap_name);
  }
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void initLittleFS() {
  if (!LittleFS.begin()) {
    Serial.println("An error has occurred while mounting LittleFS");
    return;
  }
  Serial.println("LittleFS mounted successfully");
}

void initLoadCell() {
  if (simEnabled()) return;
  if (!LoadCell) return;
  LoadCell->begin();
  scaleFactor = loadScaleFactor();
  LoadCell->setCalFactor(scaleFactor);
  Serial.printf("Using scale factor: %.6f\n", scaleFactor);
  Serial.println("Taring scale at startup...");
  LoadCell->tare();
  Serial.println("Startup Tare Complete.");
}

void IRAM_ATTR handleTelemInterrupt();

void setup() {
  Serial.begin(115200);
  logInfo("Reset reason code: %d", (int)esp_reset_reason());

  initLittleFS();
  ensureConfigExists();
  loadBoardConfig();

  if (!simEnabled()) {
    LoadCell = new HX711_ADC(boardConfig.hx711_dout_pin, boardConfig.hx711_sck_pin);
  }
  currentPwm = boardConfig.min_pulse_width;
  previousPwmForRamp = boardConfig.min_pulse_width;

  initWiFi();
  initLoadCell();

  if (!simEnabled()) {
    ledcSetup(boardConfig.esc_pwm_channel, boardConfig.pwm_freq, boardConfig.pwm_resolution);
    ledcAttachPin(boardConfig.esc_pin, boardConfig.esc_pwm_channel);
  } else {
    if (boardConfig.sim_seed != 0) {
      randomSeed(boardConfig.sim_seed);
    } else {
      randomSeed((uint32_t)micros());
    }
  }
  currentState = ARMING;

  if (!simEnabled()) {
    pinMode(boardConfig.esc_telem_pin, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(boardConfig.esc_telem_pin), handleTelemInterrupt,
                    CHANGE);
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthorizedRequest(request)) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }
    if (wifiProvisioningMode)
      request->send(LittleFS, "/wifi_setup.html", "text/html");
    else
      request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthorizedRequest(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    int n = WiFi.scanNetworks();
    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject obj = arr.createNestedObject();
      obj["ssid"] = WiFi.SSID(i);
      obj["rssi"] = WiFi.RSSI(i);
      obj["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      obj["channel"] = WiFi.channel(i);
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
              if (!isAuthorizedRequest(request)) {
                request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
              }
              if (pendingWifiRequest) {
                request->send(409, "application/json", "{\"error\":\"WiFi connect already in progress\"}");
                return;
              }
            },
            nullptr,
            [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
              if (!isAuthorizedRequest(request)) {
                return;
              }
              if (pendingWifiRequest) {
                return;
              }
              static String body;
              if (index == 0)
                body = "";
              if (total > 512) {
                request->send(413, "application/json", "{\"error\":\"Body too large\"}");
                return;
              }
              for (size_t i = 0; i < len; i++)
                body += (char)data[i];
              if (index + len != total)
                return;
              StaticJsonDocument<512> doc;
              DeserializationError err = deserializeJson(doc, body);
              if (err || !doc["ssid"].is<const char *>() || !doc["password"].is<const char *>()) {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON or missing ssid/password\"}");
                return;
              }
              const char *ssid = doc["ssid"].as<const char *>();
              const char *password = doc["password"].as<const char *>();
              WiFi.mode(WIFI_AP_STA);
              WiFi.begin(ssid, password);
              pendingWifiRequest = request;
              pendingWifiSsid = ssid;
              pendingWifiPassword = password;
              pendingWifiStartTime = millis();
            });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthorizedRequest(request)) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }
    if (!LittleFS.exists(BOARD_CFG_PATH)) {
      request->send(404, "text/plain", "");
      return;
    }
    File f = LittleFS.open(BOARD_CFG_PATH, "r");
    if (!f) {
      request->send(500, "text/plain", "Failed to read config");
      return;
    }
    String content = f.readString();
    f.close();
    request->send(200, "text/plain", content);
  });

  server.on("/api/config/default", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthorizedRequest(request)) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }
    String content;
    size_t len = strlen_P(DEFAULT_BOARD_CFG);
    content.reserve(len);
    for (size_t i = 0; i < len; i++)
      content += (char)pgm_read_byte(DEFAULT_BOARD_CFG + i);
    request->send(200, "text/plain", content);
  });

  server.on("/api/config/validate", HTTP_POST, [](AsyncWebServerRequest *request) {
              if (!isAuthorizedRequest(request)) {
                request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
              }
            },
            nullptr,
            [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
              if (!isAuthorizedRequest(request)) {
                return;
              }
              static String body;
              if (index == 0)
                body = "";
              if (total > 8192) {
                request->send(400, "application/json", "{\"error\":\"Config too large\"}");
                return;
              }
              for (size_t i = 0; i < len; i++)
                body += (char)data[i];
              if (index + len != total)
                return;
              if (body.length() == 0) {
                request->send(400, "application/json", "{\"error\":\"Empty config\"}");
                return;
              }
              BoardConfig backup = boardConfig;
              setBoardConfigDefaults();
              if (!parseConfigContent(body.c_str(), true)) {
                boardConfig = backup;
                request->send(400, "application/json", "{\"error\":\"Invalid config: unknown key or invalid value\"}");
                return;
              }
              boardConfig = backup;
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            });

  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {
              if (!isAuthorizedRequest(request)) {
                request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
              }
            },
            nullptr,
            [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
              if (!isAuthorizedRequest(request)) {
                return;
              }
              static String body;
              if (index == 0)
                body = "";
              if (total > 8192) {
                request->send(400, "application/json", "{\"error\":\"Config too large\"}");
                return;
              }
              for (size_t i = 0; i < len; i++)
                body += (char)data[i];
              if (index + len != total)
                return;
              if (body.length() == 0) {
                request->send(400, "application/json", "{\"error\":\"Empty config\"}");
                return;
              }
              BoardConfig backup = boardConfig;
              setBoardConfigDefaults();
              if (!parseConfigContent(body.c_str(), true)) {
                boardConfig = backup;
                request->send(400, "application/json", "{\"error\":\"Invalid config: unknown key or invalid value\"}");
                return;
              }
              File f = LittleFS.open(BOARD_CFG_PATH, "w");
              if (!f) {
                boardConfig = backup;
                request->send(500, "application/json", "{\"error\":\"Failed to write config\"}");
                return;
              }
              f.print(body);
              f.close();
              request->send(200, "application/json", "{\"status\":\"saved\"}");
            });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAuthorizedRequest(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    rebootAtMs = millis() + 250;
    request->send(200, "application/json", "{\"status\":\"rebooting\"}");
  });

  server.begin();
}

// --- ESC Telemetry (Voltage/Current) ---

volatile uint32_t latestPulseWidth = 0;
volatile unsigned long pulseStartTime = 0;

void IRAM_ATTR handleTelemInterrupt() {
  if (digitalRead(boardConfig.esc_telem_pin) == HIGH) {
    pulseStartTime = micros();
  } else {
    latestPulseWidth = micros() - pulseStartTime;
  }
}

void readEscTelemetry() {
  if (simEnabled()) {
    return;
  }
  // Atomic read of volatile variable (uint32_t is atomic on ESP32 usually, but
  // good practice to be safe if needed)
  uint32_t pulse = latestPulseWidth;

  // Reset to 0 to detect if signal is lost (optional, or keep last known value)
  // latestPulseWidth = 0;

  if (pulse >= (uint32_t)boardConfig.telem_voltage_min && pulse <= (uint32_t)boardConfig.telem_voltage_max) {
    escVoltage = (pulse - boardConfig.telem_voltage_min) / boardConfig.telem_scale;
  } else if (pulse >= (uint32_t)boardConfig.telem_current_min && pulse <= (uint32_t)boardConfig.telem_current_max) {
    escCurrent = (pulse - boardConfig.telem_current_min) / boardConfig.telem_scale;
  }
}

// --- Main Loop (State Machine) ---
void loop() {
  ws.cleanupClients();
  readEscTelemetry();

#if ENABLE_HEAP_LOG
  if (millis() - lastHeapLogTime >= 5000) {
    lastHeapLogTime = millis();
    logInfo("Heap free: %u bytes", ESP.getFreeHeap());
  }
#endif

  if (pendingWifiRequest) {
    const unsigned long timeoutMs = boardConfig.wifi_connect_timeout_ms;
    if (WiFi.status() == WL_CONNECTED) {
      if (!saveWiFiCredentials(pendingWifiSsid.c_str(), pendingWifiPassword.c_str())) {
        pendingWifiRequest->send(500, "application/json", "{\"error\":\"Failed to save credentials\"}");
      } else {
        String ip = WiFi.localIP().toString();
        String json = "{\"status\":\"saved\",\"ip\":\"" + ip + "\"}";
        pendingWifiRequest->send(200, "application/json", json);
        rebootAtMs = millis() + boardConfig.wifi_save_reboot_delay_ms;
      }
      pendingWifiRequest = nullptr;
      pendingWifiSsid = "";
      pendingWifiPassword = "";
    } else if (millis() - pendingWifiStartTime >= timeoutMs) {
      pendingWifiRequest->send(400, "application/json", "{\"error\":\"Connection failed\"}");
      pendingWifiRequest = nullptr;
      pendingWifiSsid = "";
      pendingWifiPassword = "";
    }
  }

  if (rebootAtMs != 0 && (long)(millis() - rebootAtMs) >= 0) {
    ESP.restart();
  }

  // Send live telemetry ONLY when not running a test sequence
  if (currentState != RUNNING_SEQUENCE && (millis() - lastTelemetryMs >= TELEMETRY_INTERVAL_MS)) {
    lastTelemetryMs = millis();
    StaticJsonDocument<200> telemDoc;
    telemDoc["type"] = "live_data";
    telemDoc["time"] = millis();
    float thrust = 0.0f;
    if (readThrust(&thrust)) {
      telemDoc["thrust"] = thrust;
    } else {
      telemDoc["thrust"] = 0.0;
    }
    telemDoc["pwm"] = currentPwm;
    telemDoc["voltage"] = escVoltage;
    telemDoc["current"] = escCurrent;
    String telemOutput;
    serializeJson(telemDoc, telemOutput);
    notifyClients(telemOutput);
  }

  switch (currentState) {
  case ARMING: {
    if (armingStartTime == 0) {
      Serial.println("Arming ESC... Sending min throttle.");
      setEscThrottlePwm(boardConfig.min_pulse_width);
      armingStartTime = millis();
    } else if (millis() - armingStartTime >= boardConfig.esc_arming_delay_ms) {
      notifyClients("{\"type\":\"status\", \"message\":\"ESC Armed. Ready.\"}");
      currentState = IDLE;
      armingStartTime = 0;
    }
    break;
  }
  case PRE_TEST_TARE: {
    if (millis() - stepStartTime < boardConfig.pre_test_tare_spinup_ms) {
      setEscThrottlePwm(boardConfig.pre_test_tare_pwm);
    } else {
      if (!preTestSettling) {
        setEscThrottlePwm(boardConfig.min_pulse_width);
        preTestSettling = true;
        preTestSettleStart = millis();
      } else if (millis() - preTestSettleStart >= boardConfig.pre_test_tare_settle_ms) {
        if (simEnabled()) {
          simThrust = 0.0f;
        } else if (LoadCell) {
          LoadCell->tare();
        }
        Serial.println("Pre-test tare complete.");
        notifyClients("{\"type\":\"status\", \"message\":\"Pre-test tare "
                      "complete. Starting sequence.\"}");

        currentState = RUNNING_SEQUENCE;
        currentSequenceStep = 0;
        testStartTime = millis();
        stepStartTime = millis();
        previousPwmForRamp = boardConfig.min_pulse_width;
        testResults.clear();
        testResultsFullLogged = false;
        lastSimSampleMs = 0;
        lastSimUpdateMs = 0;
        preTestSettling = false;
        preTestSettleStart = 0;
      }
    }
    break;
  }
  case RUNNING_SEQUENCE: {
    if (currentSequenceStep >= testSequence.size()) {
      finishTest();
      break;
    }

    TestStep &step = testSequence[currentSequenceStep];
    unsigned long elapsedInStep = millis() - stepStartTime;

    if (elapsedInStep < step.spinup_ms) {
      int new_pwm =
          map(elapsedInStep, 0, step.spinup_ms, previousPwmForRamp, step.pwm);
      setEscThrottlePwm(new_pwm);
    } else if (elapsedInStep < (step.spinup_ms + step.stable_ms)) {
      setEscThrottlePwm(step.pwm);
    } else {
      previousPwmForRamp = step.pwm;
      currentSequenceStep++;
      stepStartTime = millis();
    }

    float currentThrust = 0.0f;
    const bool simSamplingReady = !simEnabled() ||
                                  (millis() - lastSimSampleMs >= TELEMETRY_INTERVAL_MS);
    if (readThrust(&currentThrust)) {
      unsigned long currentTime = millis() - testStartTime;
      if (simEnabled() && simSamplingReady) {
        lastSimSampleMs = millis();
      }

      if (!simEnabled() || simSamplingReady) {
        if (testResults.size() < boardConfig.max_test_samples) {
          testResults.push_back({currentTime, currentThrust, currentPwm});
        } else if (!testResultsFullLogged) {
          logWarn("Memory limit reached for test results!");
          testResultsFullLogged = true;
        }
      }

      // Send live data (throttled)
      if (millis() - lastTelemetryMs >= TELEMETRY_INTERVAL_MS &&
          (!simEnabled() || simSamplingReady)) {
        lastTelemetryMs = millis();
        StaticJsonDocument<200> doc;
        doc["type"] = "live_data";
        doc["time"] = currentTime; // This is the relative time since test start
        doc["thrust"] = currentThrust;
        doc["pwm"] = currentPwm;
        doc["voltage"] = escVoltage;
        doc["current"] = escCurrent;
        String output;
        serializeJson(doc, output);
        notifyClients(output);
      }

      if (millis() - lastSafetyCheckTime > boardConfig.safety_check_interval) {
        bool isStablePhase = (elapsedInStep > step.spinup_ms);
        if (currentPwm > boardConfig.safety_pwm_threshold && isStablePhase) {
          if ((lastThrustForSafetyCheck - currentThrust) >
              boardConfig.abnormal_thrust_drop) {
            triggerSafetyShutdown("Abnormal thrust drop detected!");
          }
        }
        lastThrustForSafetyCheck = currentThrust;
        lastSafetyCheckTime = millis();
      }
    }
    break;
  }
  case IDLE:
  case SAFETY_SHUTDOWN:
  case TEST_FINISHED: {
    break;
  }
  }
  delay(1);
}
