#pragma once

#include <Arduino.h>

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

const char *getBoardConfigPath();
const char *getDefaultBoardConfigPgm();
size_t getDefaultBoardConfigLen();

void setBoardConfigDefaults(BoardConfig &cfg);
bool parseConfigContent(const char *content, BoardConfig &cfg, bool strictMode);
bool parseConfigContentDetailed(const char *content,
                                BoardConfig &cfg,
                                bool strictMode,
                                char *errSection,
                                size_t errSectionLen,
                                char *errKey,
                                size_t errKeyLen,
                                char *errMessage,
                                size_t errMessageLen);
void ensureConfigExists();
bool loadBoardConfig(BoardConfig &cfg);
bool writeDefaultBoardConfigToFile(const char *path);
