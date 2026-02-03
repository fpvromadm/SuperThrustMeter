#include <Arduino.h>
#include <unity.h>

#include "AppState.h"
#include "config/BoardConfig.h"
#include "test/TestRunner.h"

static void test_parse_sequence_ok() {
  AppState state;
  BoardConfig cfg;
  setBoardConfigDefaults(cfg);
  bool ok = parseAndStoreSequence(state, cfg, "1100 - 2 - 3; 1200 - 1 - 2");
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT32(2, state.testSequence.size());
  TEST_ASSERT_EQUAL_INT(1100, state.testSequence[0].pwm);
  TEST_ASSERT_EQUAL_UINT32(2000, state.testSequence[0].spinup_ms);
  TEST_ASSERT_EQUAL_UINT32(3000, state.testSequence[0].stable_ms);
}

static void test_parse_sequence_invalid() {
  AppState state;
  BoardConfig cfg;
  setBoardConfigDefaults(cfg);
  bool ok = parseAndStoreSequence(state, cfg, "bad-input");
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT32(0, state.testSequence.size());
}

static void test_parse_sequence_invalid_pwm() {
  AppState state;
  BoardConfig cfg;
  setBoardConfigDefaults(cfg);
  bool ok = parseAndStoreSequence(state, cfg, "999 - 1 - 1");
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT32(0, state.testSequence.size());
}

static void test_parse_sequence_negative_time() {
  AppState state;
  BoardConfig cfg;
  setBoardConfigDefaults(cfg);
  bool ok = parseAndStoreSequence(state, cfg, "1100 - -1 - 2");
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT32(0, state.testSequence.size());
}

static void test_config_parse_strict_ok() {
  BoardConfig cfg;
  setBoardConfigDefaults(cfg);
  const char *content =
      "[pins]\n"
      "HX711_DOUT_PIN = 19\n"
      "HX711_SCK_PIN = 18\n"
      "[esc]\n"
      "PWM_FREQ = 400\n";
  bool ok = parseConfigContent(content, cfg, true);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_INT(19, cfg.hx711_dout_pin);
  TEST_ASSERT_EQUAL_INT(18, cfg.hx711_sck_pin);
  TEST_ASSERT_EQUAL_INT(400, cfg.pwm_freq);
}

static void test_config_parse_strict_rejects_unknown() {
  BoardConfig cfg;
  setBoardConfigDefaults(cfg);
  const char *content =
      "[pins]\n"
      "HX711_DOUT_PIN = 19\n"
      "UNKNOWN_KEY = 42\n";
  bool ok = parseConfigContent(content, cfg, true);
  TEST_ASSERT_FALSE(ok);
}

static void test_config_parse_detailed_invalid_value() {
  BoardConfig cfg;
  setBoardConfigDefaults(cfg);
  const char *content =
      "[esc]\n"
      "PWM_RESOLUTION = 32\n";
  char section[32] = "";
  char key[32] = "";
  char message[64] = "";
  bool ok = parseConfigContentDetailed(content, cfg, true, section, sizeof(section), key, sizeof(key), message,
                                       sizeof(message));
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_STRING("esc", section);
  TEST_ASSERT_EQUAL_STRING("PWM_RESOLUTION", key);
  TEST_ASSERT_EQUAL_STRING("Invalid value", message);
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_parse_sequence_ok);
  RUN_TEST(test_parse_sequence_invalid);
  RUN_TEST(test_parse_sequence_invalid_pwm);
  RUN_TEST(test_parse_sequence_negative_time);
  RUN_TEST(test_config_parse_strict_ok);
  RUN_TEST(test_config_parse_strict_rejects_unknown);
  RUN_TEST(test_config_parse_detailed_invalid_value);
  UNITY_END();
}

void loop() {}
