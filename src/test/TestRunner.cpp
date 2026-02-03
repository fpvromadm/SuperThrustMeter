#include "TestRunner.h"

#include "FS.h"
#include "LittleFS.h"
#include "ArduinoJson.h"
#include "net/WebSocketUtils.h"
#include "scale/LoadCellManager.h"
#include "sim/Simulator.h"
#include "util/Log.h"
#include <Arduino.h>

static const size_t FINAL_RESULTS_CHUNK_SIZE = 100;
static const char LAST_RESULTS_PATH[] = "/last_test.csv";

const char *getLastResultsPath() { return LAST_RESULTS_PATH; }

void deleteLastResultsFile() {
  if (LittleFS.exists(LAST_RESULTS_PATH)) {
    LittleFS.remove(LAST_RESULTS_PATH);
  }
}

static void saveResultsCsv(const std::vector<DataPoint> &results) {
  File file = LittleFS.open(LAST_RESULTS_PATH, "w");
  if (!file) {
    logWarn("Failed to open %s for writing", LAST_RESULTS_PATH);
    return;
  }
  file.println("timestamp_ms,thrust_g,pwm_us");
  for (const auto &point : results) {
    file.printf("%lu,%.3f,%d\n", point.timestamp, point.thrust, point.pwm);
  }
  file.close();
  logInfo("Saved %u results to %s", (unsigned)results.size(), LAST_RESULTS_PATH);
}

void setEscThrottlePwm(AppState &state, const BoardConfig &cfg, bool simEnabled, int pulse_width_us) {
  if (pulse_width_us < cfg.min_pulse_width) pulse_width_us = cfg.min_pulse_width;
  if (pulse_width_us > cfg.max_pulse_width) pulse_width_us = cfg.max_pulse_width;

  state.currentPwm = pulse_width_us;

  if (!simEnabled) {
    const uint32_t maxDuty = (1UL << cfg.pwm_resolution) - 1UL;
    const uint32_t periodUs = (cfg.pwm_freq > 0) ? (1000000UL / (uint32_t)cfg.pwm_freq) : 20000UL;
    uint32_t duty = (maxDuty * (uint32_t)pulse_width_us) / periodUs;
    ledcWrite(cfg.esc_pwm_channel, duty);
  }
}

void triggerSafetyShutdown(AppState &state, const BoardConfig &cfg, bool simEnabled, AsyncWebSocket &ws, const char *reason) {
  setEscThrottlePwm(state, cfg, simEnabled, cfg.min_pulse_width);
  state.currentState = State::SAFETY_SHUTDOWN;
  Serial.printf("SAFETY SHUTDOWN TRIGGERED: %s\n", reason);

  StaticJsonDocument<200> doc;
  doc["type"] = "safety_shutdown";
  doc["message"] = reason;
  doc["state"] = "safety_shutdown";
  char output[256];
  size_t outLen = serializeJson(doc, output, sizeof(output));
  if (outLen > 0) {
    notifyClients(ws, cfg, state.wifiProvisioningMode, output);
  }
  state.testSequence.clear();
}

void finishTest(AppState &state, const BoardConfig &cfg, bool simEnabled, AsyncWebSocket &ws) {
  setEscThrottlePwm(state, cfg, simEnabled, cfg.min_pulse_width);
  state.currentState = State::TEST_FINISHED;
  Serial.println("Test sequence finished.");

  saveResultsCsv(state.testResults);

  StaticJsonDocument<200> doc;
  doc["type"] = "status";
  doc["message"] = "Test finished. Sending final results.";
  if (hasWsClients(ws)) {
    char output[256];
    size_t outLen = serializeJson(doc, output, sizeof(output));
    if (outLen > 0) {
      notifyClients(ws, cfg, state.wifiProvisioningMode, output);
    }
  }

  const size_t totalPoints = state.testResults.size();
  if (hasWsClients(ws)) {
    StaticJsonDocument<128> startDoc;
    startDoc["type"] = "final_results_start";
    startDoc["total"] = (uint32_t)totalPoints;
    char startOut[192];
    size_t startLen = serializeJson(startDoc, startOut, sizeof(startOut));
    if (startLen > 0) {
      notifyClients(ws, cfg, state.wifiProvisioningMode, startOut);
    }
  }

  if (hasWsClients(ws)) {
    static char chunkOut[9000];
    static DynamicJsonDocument chunkDoc(8192);
    for (size_t i = 0; i < totalPoints; i += FINAL_RESULTS_CHUNK_SIZE) {
      chunkDoc.clear();
      chunkDoc["type"] = "final_results_chunk";
      chunkDoc["index"] = (uint32_t)i;
      JsonArray data = chunkDoc.createNestedArray("data");
      size_t end = i + FINAL_RESULTS_CHUNK_SIZE;
      if (end > totalPoints) end = totalPoints;
      for (size_t j = i; j < end; j++) {
        JsonObject dataPoint = data.createNestedObject();
        dataPoint["time"] = state.testResults[j].timestamp;
        dataPoint["thrust"] = state.testResults[j].thrust;
        dataPoint["pwm"] = state.testResults[j].pwm;
      }
      size_t chunkLen = serializeJson(chunkDoc, chunkOut, sizeof(chunkOut));
      if (chunkLen > 0) {
        notifyClients(ws, cfg, state.wifiProvisioningMode, chunkOut);
      } else {
        logWarn("Chunk JSON buffer too small; skipping chunk %u", (unsigned)i);
      }
    }
  }

  if (hasWsClients(ws)) {
    StaticJsonDocument<128> endDoc;
    endDoc["type"] = "final_results_end";
    char endOut[192];
    size_t endLen = serializeJson(endDoc, endOut, sizeof(endOut));
    if (endLen > 0) {
      notifyClients(ws, cfg, state.wifiProvisioningMode, endOut);
    }
  }

  state.testResults.clear();
  state.currentState = State::IDLE;
}

bool parseAndStoreSequence(AppState &state, const BoardConfig &cfg, const char *sequenceStr) {
  if (!sequenceStr) return false;
  state.testSequence.clear();
  char *sequenceCopy = strdup(sequenceStr);
  char *stepToken = strtok(sequenceCopy, ";");

  while (stepToken != NULL) {
    TestStep step;
    int pwm, spinup, stable;
    if (sscanf(stepToken, "%d - %d - %d", &pwm, &spinup, &stable) == 3) {
      if (pwm < cfg.min_pulse_width || pwm > cfg.max_pulse_width) {
        Serial.printf("Invalid PWM in step: %s\n", stepToken);
        free(sequenceCopy);
        return false;
      }
      if (spinup < 0 || stable < 0) {
        Serial.printf("Invalid timing in step: %s\n", stepToken);
        free(sequenceCopy);
        return false;
      }
      step.pwm = pwm;
      step.spinup_ms = (unsigned long)spinup * 1000UL;
      step.stable_ms = (unsigned long)stable * 1000UL;
      state.testSequence.push_back(step);
    } else {
      Serial.printf("Failed to parse step: %s\n", stepToken);
      free(sequenceCopy);
      return false;
    }
    stepToken = strtok(NULL, ";");
  }
  free(sequenceCopy);
  return !state.testSequence.empty();
}

void resetTest(AppState &state) {
  state.currentState = State::IDLE;
  state.testResults.clear();
  state.testSequence.clear();
  deleteLastResultsFile();
  state.lastThrustForSafetyCheck = 0.0f;
  state.lastSafetyCheckTime = 0;
  state.lastSimSampleMs = 0;
  state.lastSimUpdateMs = 0;
}

void startPreTestTare(AppState &state, const BoardConfig &cfg) {
  state.currentState = State::PRE_TEST_TARE;
  state.stepStartTime = millis();
  state.preTestSettling = false;
  state.preTestSettleStart = 0;
  (void)cfg;
}

void tickTestRunner(AppState &state, const BoardConfig &cfg, bool simEnabled, HX711_ADC *loadCell, AsyncWebSocket &ws) {
  switch (state.currentState) {
    case State::ARMING: {
      if (state.armingStartTime == 0) {
        Serial.println("Arming ESC... Sending min throttle.");
        setEscThrottlePwm(state, cfg, simEnabled, cfg.min_pulse_width);
        state.armingStartTime = millis();
      } else if (millis() - state.armingStartTime >= cfg.esc_arming_delay_ms) {
        notifyClients(ws, cfg, state.wifiProvisioningMode, "{\"type\":\"status\", \"message\":\"ESC Armed. Ready.\"}");
        state.currentState = State::IDLE;
        state.armingStartTime = 0;
      }
      break;
    }
    case State::PRE_TEST_TARE: {
      if (millis() - state.stepStartTime < cfg.pre_test_tare_spinup_ms) {
        setEscThrottlePwm(state, cfg, simEnabled, cfg.pre_test_tare_pwm);
      } else {
        if (!state.preTestSettling) {
          setEscThrottlePwm(state, cfg, simEnabled, cfg.min_pulse_width);
          state.preTestSettling = true;
          state.preTestSettleStart = millis();
        } else if (millis() - state.preTestSettleStart >= cfg.pre_test_tare_settle_ms) {
          tareScale(simEnabled, loadCell, state);
          Serial.println("Pre-test tare complete.");
          notifyClients(ws, cfg, state.wifiProvisioningMode, "{\"type\":\"status\", \"message\":\"Pre-test tare complete. Starting sequence.\"}");

          state.currentState = State::RUNNING_SEQUENCE;
          state.currentSequenceStep = 0;
          state.testStartTime = millis();
          state.stepStartTime = millis();
          state.previousPwmForRamp = cfg.min_pulse_width;
          state.testResults.clear();
          state.testResults.reserve(cfg.max_test_samples);
          state.testResultsFullLogged = false;
          state.lastThrustForSafetyCheck = 0.0f;
          state.lastSafetyCheckTime = 0;
          state.lastSimSampleMs = 0;
          state.lastSimUpdateMs = 0;
          state.preTestSettling = false;
          state.preTestSettleStart = 0;
        }
      }
      break;
    }
    case State::RUNNING_SEQUENCE: {
      if (state.currentSequenceStep >= (int)state.testSequence.size()) {
        finishTest(state, cfg, simEnabled, ws);
        break;
      }

      TestStep &step = state.testSequence[state.currentSequenceStep];
      unsigned long elapsedInStep = millis() - state.stepStartTime;

      if (step.spinup_ms == 0) {
        setEscThrottlePwm(state, cfg, simEnabled, step.pwm);
      } else if (elapsedInStep < step.spinup_ms) {
        int new_pwm = map(elapsedInStep, 0, step.spinup_ms, state.previousPwmForRamp, step.pwm);
        setEscThrottlePwm(state, cfg, simEnabled, new_pwm);
      } else if (elapsedInStep < (step.spinup_ms + step.stable_ms)) {
        setEscThrottlePwm(state, cfg, simEnabled, step.pwm);
      } else {
        state.previousPwmForRamp = step.pwm;
        state.currentSequenceStep++;
        state.stepStartTime = millis();
      }

      if (simEnabled) {
        updateSimTelemetry(state, cfg);
      }

      float currentThrust = 0.0f;
      const bool simSamplingReady = !simEnabled || (millis() - state.lastSimSampleMs >= TELEMETRY_INTERVAL_MS);
      if (readThrust(simEnabled, loadCell, state, &currentThrust)) {
        unsigned long currentTime = millis() - state.testStartTime;
        if (simEnabled && simSamplingReady) {
          state.lastSimSampleMs = millis();
        }

        if (state.escTelemStale && hasWsClients(ws)) {
          const unsigned long now = millis();
          if (state.lastEscTelemWarningMs == 0 || (now - state.lastEscTelemWarningMs) > 2000) {
            state.lastEscTelemWarningMs = now;
            notifyClients(ws, cfg, state.wifiProvisioningMode,
                          "{\"type\":\"warning\",\"message\":\"ESC telemetry lost during test\"}");
          }
        } else {
          state.lastEscTelemWarningMs = 0;
        }

        if (!simEnabled || simSamplingReady) {
          if (state.testResults.size() < cfg.max_test_samples) {
            state.testResults.push_back({currentTime, currentThrust, state.currentPwm});
          } else if (!state.testResultsFullLogged) {
            logWarn("Memory limit reached for test results!");
            state.testResultsFullLogged = true;
          }
        }

        if (hasWsClients(ws) && millis() - state.lastTelemetryMs >= TELEMETRY_INTERVAL_MS &&
            (!simEnabled || simSamplingReady)) {
          state.lastTelemetryMs = millis();
          StaticJsonDocument<200> doc;
          doc["type"] = "live_data";
          doc["time"] = currentTime;
          doc["thrust"] = currentThrust;
          doc["pwm"] = state.currentPwm;
          doc["voltage"] = state.escVoltage;
          doc["current"] = state.escCurrent;
          doc["esc_telem_stale"] = state.escTelemStale;
          doc["esc_telem_age_ms"] = state.escTelemAgeMs;
          char output[256];
          size_t outLen = serializeJson(doc, output, sizeof(output));
          if (outLen > 0) {
            notifyClients(ws, cfg, state.wifiProvisioningMode, output);
          }
        }

        if (millis() - state.lastSafetyCheckTime > cfg.safety_check_interval) {
          bool isStablePhase = (elapsedInStep > step.spinup_ms);
          if (state.currentPwm > cfg.safety_pwm_threshold && isStablePhase) {
            if ((state.lastThrustForSafetyCheck - currentThrust) > cfg.abnormal_thrust_drop) {
              triggerSafetyShutdown(state, cfg, simEnabled, ws, "Abnormal thrust drop detected!");
            }
          }
          state.lastThrustForSafetyCheck = currentThrust;
          state.lastSafetyCheckTime = millis();
        }
      }
      break;
    }
    case State::IDLE:
    case State::SAFETY_SHUTDOWN:
    case State::TEST_FINISHED:
      break;
  }
}
