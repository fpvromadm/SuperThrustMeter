#include "BoardConfig.h"

#include "FS.h"
#include "LittleFS.h"
#include <Arduino.h>

static const char BOARD_CFG_PATH[] = "/board.cfg";

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

const char *getBoardConfigPath() { return BOARD_CFG_PATH; }

const char *getDefaultBoardConfigPgm() { return DEFAULT_BOARD_CFG; }

size_t getDefaultBoardConfigLen() { return strlen_P(DEFAULT_BOARD_CFG); }

void setBoardConfigDefaults(BoardConfig &cfg) {
  cfg.hx711_dout_pin = 21;
  cfg.hx711_sck_pin = 22;
  cfg.esc_pin = 27;
  cfg.esc_telem_pin = 32;
  cfg.esc_pwm_channel = 0;
  cfg.pwm_freq = 50;
  cfg.pwm_resolution = 16;
  cfg.min_pulse_width = 1000;
  cfg.max_pulse_width = 2000;
  cfg.abnormal_thrust_drop = 75.0f;
  cfg.safety_check_interval = 100;
  cfg.safety_pwm_threshold = 1150;
  cfg.scale_factor_default = -204.0f;
  strncpy(cfg.scale_factor_file, "/scale_factor.txt", sizeof(cfg.scale_factor_file) - 1);
  cfg.scale_factor_file[sizeof(cfg.scale_factor_file) - 1] = '\0';
  strncpy(cfg.wifi_credentials_file, "/wifi.json", sizeof(cfg.wifi_credentials_file) - 1);
  cfg.wifi_credentials_file[sizeof(cfg.wifi_credentials_file) - 1] = '\0';
  strncpy(cfg.wifi_ap_name, "ThrustScale_Setup", sizeof(cfg.wifi_ap_name) - 1);
  cfg.wifi_ap_name[sizeof(cfg.wifi_ap_name) - 1] = '\0';
  cfg.wifi_ap_password[0] = '\0';
  cfg.wifi_connect_timeout_ms = 10000;
  cfg.wifi_save_reboot_delay_ms = 2500;
  cfg.max_test_samples = 6000;
  cfg.pre_test_tare_pwm = 1100;
  cfg.pre_test_tare_spinup_ms = 2000;
  cfg.pre_test_tare_settle_ms = 500;
  cfg.esc_arming_delay_ms = 2100;
  cfg.telem_voltage_min = 1000;
  cfg.telem_voltage_max = 2000;
  cfg.telem_current_min = 2000;
  cfg.telem_current_max = 3000;
  cfg.telem_scale = 100.0f;
  cfg.auth_token[0] = '\0';
  cfg.sim_enabled = false;
  cfg.sim_thrust_max_g = 2000.0f;
  cfg.sim_noise_g = 5.0f;
  cfg.sim_response_ms = 250;
  cfg.sim_voltage = 16.0f;
  cfg.sim_current_max = 60.0f;
  cfg.sim_seed = 0;
}

enum class ConfigKeyResult { OK, UNKNOWN, INVALID };

static ConfigKeyResult setConfigKey(BoardConfig &cfg, const char *section, const char *key, const char *value) {
  if (strcmp(section, "pins") == 0) {
    if (strcmp(key, "HX711_DOUT_PIN") == 0) {
      int v = atoi(value);
      if (v >= 0 && v <= 39) {
        cfg.hx711_dout_pin = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "HX711_SCK_PIN") == 0) {
      int v = atoi(value);
      if (v >= 0 && v <= 39) {
        cfg.hx711_sck_pin = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "ESC_PIN") == 0) {
      int v = atoi(value);
      if (v >= 0 && v <= 39) {
        cfg.esc_pin = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "ESC_TELEM_PIN") == 0) {
      int v = atoi(value);
      if (v >= 0 && v <= 39) {
        cfg.esc_telem_pin = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
  }
  if (strcmp(section, "esc") == 0) {
    if (strcmp(key, "ESC_PWM_CHANNEL") == 0) {
      int v = atoi(value);
      if (v >= 0 && v <= 15) {
        cfg.esc_pwm_channel = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "PWM_FREQ") == 0) {
      int v = atoi(value);
      if (v > 0 && v <= 40000) {
        cfg.pwm_freq = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "PWM_RESOLUTION") == 0) {
      int v = atoi(value);
      if (v >= 1 && v <= 16) {
        cfg.pwm_resolution = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "MIN_PULSE_WIDTH") == 0) {
      int v = atoi(value);
      if (v >= 500 && v <= 2500) {
        cfg.min_pulse_width = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "MAX_PULSE_WIDTH") == 0) {
      int v = atoi(value);
      if (v >= 500 && v <= 2500) {
        cfg.max_pulse_width = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
  }
  if (strcmp(section, "safety") == 0) {
    if (strcmp(key, "ABNORMAL_THRUST_DROP") == 0) {
      float v = atof(value);
      if (v >= 0 && v <= 500) {
        cfg.abnormal_thrust_drop = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "SAFETY_CHECK_INTERVAL") == 0) {
      unsigned long v = atol(value);
      if (v >= 10 && v <= 10000) {
        cfg.safety_check_interval = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "SAFETY_PWM_THRESHOLD") == 0) {
      int v = atoi(value);
      if (v >= 1000 && v <= 2000) {
        cfg.safety_pwm_threshold = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
  }
  if (strcmp(section, "scale") == 0) {
    if (strcmp(key, "SCALE_FACTOR_DEFAULT") == 0) {
      cfg.scale_factor_default = atof(value);
      return ConfigKeyResult::OK;
    }
    if (strcmp(key, "SCALE_FACTOR_FILE") == 0) {
      strncpy(cfg.scale_factor_file, value, sizeof(cfg.scale_factor_file) - 1);
      cfg.scale_factor_file[sizeof(cfg.scale_factor_file) - 1] = '\0';
      return ConfigKeyResult::OK;
    }
  }
  if (strcmp(section, "wifi") == 0) {
    if (strcmp(key, "WIFI_CREDENTIALS_FILE") == 0) {
      strncpy(cfg.wifi_credentials_file, value, sizeof(cfg.wifi_credentials_file) - 1);
      cfg.wifi_credentials_file[sizeof(cfg.wifi_credentials_file) - 1] = '\0';
      return ConfigKeyResult::OK;
    }
    if (strcmp(key, "WIFI_AP_NAME") == 0) {
      strncpy(cfg.wifi_ap_name, value, sizeof(cfg.wifi_ap_name) - 1);
      cfg.wifi_ap_name[sizeof(cfg.wifi_ap_name) - 1] = '\0';
      return ConfigKeyResult::OK;
    }
    if (strcmp(key, "WIFI_AP_PASSWORD") == 0) {
      strncpy(cfg.wifi_ap_password, value, sizeof(cfg.wifi_ap_password) - 1);
      cfg.wifi_ap_password[sizeof(cfg.wifi_ap_password) - 1] = '\0';
      return ConfigKeyResult::OK;
    }
    if (strcmp(key, "WIFI_CONNECT_TIMEOUT_MS") == 0) {
      unsigned long v = atol(value);
      if (v >= 1000) {
        cfg.wifi_connect_timeout_ms = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "WIFI_SAVE_REBOOT_DELAY_MS") == 0) {
      unsigned long v = atol(value);
      if (v <= 10000) {
        cfg.wifi_save_reboot_delay_ms = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
  }
  if (strcmp(section, "test") == 0) {
    if (strcmp(key, "MAX_TEST_SAMPLES") == 0) {
      int v = atoi(value);
      if (v >= 100 && v <= 20000) {
        cfg.max_test_samples = (size_t)v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "PRE_TEST_TARE_PWM") == 0) {
      int v = atoi(value);
      if (v >= 1000 && v <= 2000) {
        cfg.pre_test_tare_pwm = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "PRE_TEST_TARE_SPINUP_MS") == 0) {
      unsigned long v = atol(value);
      if (v <= 60000) {
        cfg.pre_test_tare_spinup_ms = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "PRE_TEST_TARE_SETTLE_MS") == 0) {
      unsigned long v = atol(value);
      if (v <= 10000) {
        cfg.pre_test_tare_settle_ms = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "ESC_ARMING_DELAY_MS") == 0) {
      unsigned long v = atol(value);
      if (v >= 1000 && v <= 30000) {
        cfg.esc_arming_delay_ms = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
  }
  if (strcmp(section, "esc_telem") == 0) {
    if (strcmp(key, "TELEM_VOLTAGE_MIN") == 0) {
      int v = atoi(value);
      if (v >= 0) {
        cfg.telem_voltage_min = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "TELEM_VOLTAGE_MAX") == 0) {
      int v = atoi(value);
      if (v >= 0) {
        cfg.telem_voltage_max = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "TELEM_CURRENT_MIN") == 0) {
      int v = atoi(value);
      if (v >= 0) {
        cfg.telem_current_min = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "TELEM_CURRENT_MAX") == 0) {
      int v = atoi(value);
      if (v >= 0) {
        cfg.telem_current_max = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "TELEM_SCALE") == 0) {
      float v = atof(value);
      if (v > 0) {
        cfg.telem_scale = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
  }
  if (strcmp(section, "security") == 0) {
    if (strcmp(key, "AUTH_TOKEN") == 0) {
      strncpy(cfg.auth_token, value, sizeof(cfg.auth_token) - 1);
      cfg.auth_token[sizeof(cfg.auth_token) - 1] = '\0';
      return ConfigKeyResult::OK;
    }
  }
  if (strcmp(section, "sim") == 0) {
    if (strcmp(key, "SIM_ENABLED") == 0) {
      int v = atoi(value);
      cfg.sim_enabled = (v != 0);
      return ConfigKeyResult::OK;
    }
    if (strcmp(key, "SIM_THRUST_MAX_G") == 0) {
      float v = atof(value);
      if (v >= 0) {
        cfg.sim_thrust_max_g = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "SIM_NOISE_G") == 0) {
      float v = atof(value);
      if (v >= 0) {
        cfg.sim_noise_g = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "SIM_RESPONSE_MS") == 0) {
      unsigned long v = atol(value);
      if (v <= 10000) {
        cfg.sim_response_ms = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "SIM_VOLTAGE") == 0) {
      float v = atof(value);
      if (v >= 0) {
        cfg.sim_voltage = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "SIM_CURRENT_MAX") == 0) {
      float v = atof(value);
      if (v >= 0) {
        cfg.sim_current_max = v;
        return ConfigKeyResult::OK;
      }
      return ConfigKeyResult::INVALID;
    }
    if (strcmp(key, "SIM_SEED") == 0) {
      unsigned long v = atol(value);
      cfg.sim_seed = (uint32_t)v;
      return ConfigKeyResult::OK;
    }
  }
  return ConfigKeyResult::UNKNOWN;
}

static bool setError(char *dst, size_t len, const char *value) {
  if (!dst || len == 0) return false;
  if (!value) {
    dst[0] = '\0';
    return false;
  }
  strncpy(dst, value, len - 1);
  dst[len - 1] = '\0';
  return true;
}

bool parseConfigContentDetailed(const char *content,
                                BoardConfig &cfg,
                                bool strictMode,
                                char *errSection,
                                size_t errSectionLen,
                                char *errKey,
                                size_t errKeyLen,
                                char *errMessage,
                                size_t errMessageLen) {
  const char *p = content;
  char section[32] = "";
  while (*p) {
    const char *lineStart = p;
    while (*p && *p != '\n') p++;
    String line(lineStart, p - lineStart);
    line.trim();
    if (line.length() == 0 || line.startsWith("#")) {
      if (*p) p++;
      continue;
    }
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
    if (eq <= 0) {
      if (*p) p++;
      continue;
    }
    String key = line.substring(0, eq);
    key.trim();
    String val = line.substring(eq + 1);
    val.trim();
    if (key.length() == 0) {
      if (*p) p++;
      continue;
    }
    char secCopy[32];
    strncpy(secCopy, section, sizeof(secCopy) - 1);
    secCopy[sizeof(secCopy) - 1] = '\0';
    ConfigKeyResult result = setConfigKey(cfg, secCopy, key.c_str(), val.c_str());
    if (strictMode) {
      if (result == ConfigKeyResult::UNKNOWN) {
        setError(errSection, errSectionLen, secCopy);
        setError(errKey, errKeyLen, key.c_str());
        setError(errMessage, errMessageLen, "Unknown key");
        return false;
      }
      if (result == ConfigKeyResult::INVALID) {
        setError(errSection, errSectionLen, secCopy);
        setError(errKey, errKeyLen, key.c_str());
        setError(errMessage, errMessageLen, "Invalid value");
        return false;
      }
    } else if (result == ConfigKeyResult::UNKNOWN) {
      // ignore unknown keys in non-strict mode
    }
    if (*p) p++;
  }
  return true;
}

bool parseConfigContent(const char *content, BoardConfig &cfg, bool strictMode) {
  return parseConfigContentDetailed(content, cfg, strictMode, nullptr, 0, nullptr, 0, nullptr, 0);
}

bool writeDefaultBoardConfigToFile(const char *path) {
  File f = LittleFS.open(path, "w");
  if (!f) {
    Serial.println("Failed to create board.cfg");
    return false;
  }
  size_t len = strlen_P(DEFAULT_BOARD_CFG);
  for (size_t i = 0; i < len; i++) f.write((char)pgm_read_byte(DEFAULT_BOARD_CFG + i));
  f.close();
  return true;
}

void ensureConfigExists() {
  if (LittleFS.exists(BOARD_CFG_PATH)) return;
  if (writeDefaultBoardConfigToFile(BOARD_CFG_PATH)) {
    Serial.println("Created default board.cfg");
  }
}

bool loadBoardConfig(BoardConfig &cfg) {
  setBoardConfigDefaults(cfg);
  if (!LittleFS.exists(BOARD_CFG_PATH)) {
    ensureConfigExists();
    return true;
  }
  File f = LittleFS.open(BOARD_CFG_PATH, "r");
  if (!f) {
    Serial.println("Config file read failed, using defaults");
    if (writeDefaultBoardConfigToFile(BOARD_CFG_PATH)) {
      Serial.println("Repaired board.cfg with defaults");
    }
    return true;
  }
  String content = f.readString();
  f.close();
  if (!parseConfigContent(content.c_str(), cfg, false)) {
    Serial.println("Config parse failed, using defaults");
    setBoardConfigDefaults(cfg);
    if (writeDefaultBoardConfigToFile(BOARD_CFG_PATH)) {
      Serial.println("Repaired board.cfg with defaults");
    }
    return true;
  }
  return true;
}
