#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <vector>

#ifndef ENABLE_HEAP_LOG
#define ENABLE_HEAP_LOG 0
#endif

struct DataPoint {
  unsigned long timestamp;
  float thrust;
  int pwm;
};

struct TestStep {
  int pwm;
  unsigned long spinup_ms;
  unsigned long stable_ms;
};

enum class State {
  IDLE,
  ARMING,
  PRE_TEST_TARE,
  RUNNING_SEQUENCE,
  SAFETY_SHUTDOWN,
  TEST_FINISHED
};

struct AppState {
  // WiFi provisioning and reboot
  bool wifiProvisioningMode = false;
  AsyncWebServerRequest *pendingWifiRequest = nullptr;
  String pendingWifiSsid;
  String pendingWifiPassword;
  unsigned long pendingWifiStartTime = 0;
  unsigned long rebootAtMs = 0;

  // ESC telemetry
  float escVoltage = 0.0f;
  float escCurrent = 0.0f;
  bool escTelemStale = false;
  bool lastEscTelemStaleNotified = false;
  unsigned long escTelemAgeMs = 0;
  unsigned long lastEscTelemWarningMs = 0;

  // PWM
  int currentPwm = 1000;
  int previousPwmForRamp = 1000;

  // Simulator
  float simThrust = 0.0f;
  unsigned long lastSimUpdateMs = 0;
  unsigned long lastSimSampleMs = 0;

  // State machine / test data
  State currentState = State::IDLE;
  std::vector<DataPoint> testResults;
  std::vector<TestStep> testSequence;
  unsigned long testStartTime = 0;
  unsigned long stepStartTime = 0;
  int currentSequenceStep = 0;
  bool testResultsFullLogged = false;
  unsigned long lastTelemetryMs = 0;

  // Safety trackers
  float lastThrustForSafetyCheck = 0.0f;
  unsigned long lastSafetyCheckTime = 0;

#if ENABLE_HEAP_LOG
  unsigned long lastHeapLogTime = 0;
#endif

  // Non-blocking state timers
  unsigned long armingStartTime = 0;
  bool preTestSettling = false;
  unsigned long preTestSettleStart = 0;

  // Scale factor
  float scaleFactor = -204.0f;
};

static const unsigned long TELEMETRY_INTERVAL_MS = 200;
