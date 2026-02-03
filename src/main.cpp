#include "AppState.h"
#include "config/BoardConfig.h"
#include "net/ApiRoutes.h"
#include "net/WebSocketHandler.h"
#include "net/WebSocketUtils.h"
#include "net/WiFiManager.h"
#include "scale/LoadCellManager.h"
#include "sim/Simulator.h"
#include "telemetry/EscTelemetry.h"
#include "test/TestRunner.h"
#include "util/Log.h"

#include "HX711_ADC.h"
#include "LittleFS.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <new>
#include "esp_system.h"

static BoardConfig boardConfig;
static AppState appState;

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static HX711_ADC *loadCell = nullptr;
static bool loadCellInitialized = false;
alignas(HX711_ADC) static unsigned char loadCellStorage[sizeof(HX711_ADC)];

static void initLittleFS() {
  if (!LittleFS.begin()) {
    Serial.println("An error has occurred while mounting LittleFS");
    return;
  }
  Serial.println("LittleFS mounted successfully");
}

static void clampMaxTestSamples(BoardConfig &cfg) {
  const size_t sampleBytes = sizeof(DataPoint);
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t budget = freeHeap / 4; // keep 75% free for everything else
  size_t maxByHeap = (sampleBytes > 0) ? (budget / sampleBytes) : cfg.max_test_samples;
  if (maxByHeap == 0) maxByHeap = 1;
  if (cfg.max_test_samples > maxByHeap) {
    logWarn("Clamping MAX_TEST_SAMPLES from %u to %u (free heap %u bytes)",
            (unsigned)cfg.max_test_samples,
            (unsigned)maxByHeap,
            (unsigned)freeHeap);
    cfg.max_test_samples = maxByHeap;
  }
}

void setup() {
  Serial.begin(115200);
  logInfo("Reset reason code: %d", (int)esp_reset_reason());

  initLittleFS();
  ensureConfigExists();
  loadBoardConfig(boardConfig);
  clampMaxTestSamples(boardConfig);

  if (!simEnabled(boardConfig)) {
    loadCell = new (loadCellStorage) HX711_ADC(boardConfig.hx711_dout_pin, boardConfig.hx711_sck_pin);
    loadCellInitialized = true;
  }
  appState.scaleFactor = boardConfig.scale_factor_default;
  appState.currentPwm = boardConfig.min_pulse_width;
  appState.previousPwmForRamp = boardConfig.min_pulse_width;

  initWiFi(appState, boardConfig);
  initLoadCell(simEnabled(boardConfig), loadCellInitialized ? loadCell : nullptr, boardConfig, appState);

  if (!simEnabled(boardConfig)) {
    ledcSetup(boardConfig.esc_pwm_channel, boardConfig.pwm_freq, boardConfig.pwm_resolution);
    ledcAttachPin(boardConfig.esc_pin, boardConfig.esc_pwm_channel);
  } else {
    if (boardConfig.sim_seed != 0) {
      randomSeed(boardConfig.sim_seed);
    } else {
      randomSeed((uint32_t)micros());
    }
  }
  appState.currentState = State::ARMING;

  if (!simEnabled(boardConfig)) {
    pinMode(boardConfig.esc_telem_pin, INPUT_PULLDOWN);
    initEscTelemetry(boardConfig);
    attachInterrupt(digitalPinToInterrupt(boardConfig.esc_telem_pin), handleTelemInterrupt, CHANGE);
  }

  configureWebSocket(ws, appState, boardConfig, loadCellInitialized ? loadCell : nullptr);
  server.addHandler(&ws);

  setupApiRoutes(server, ws, appState, boardConfig, loadCellInitialized ? loadCell : nullptr);
  server.begin();
}

void loop() {
  ws.cleanupClients();
  readEscTelemetry(simEnabled(boardConfig), boardConfig, appState.escVoltage, appState.escCurrent);

#if ENABLE_HEAP_LOG
  if (millis() - appState.lastHeapLogTime >= 5000) {
    appState.lastHeapLogTime = millis();
    logInfo("Heap free: %u bytes", ESP.getFreeHeap());
  }
#endif

  tickWiFiProvisioning(appState, boardConfig);

  if (appState.rebootAtMs != 0 && (long)(millis() - appState.rebootAtMs) >= 0) {
    ESP.restart();
  }

  if (appState.currentState != State::RUNNING_SEQUENCE && (millis() - appState.lastTelemetryMs >= TELEMETRY_INTERVAL_MS)) {
    appState.lastTelemetryMs = millis();
    if (simEnabled(boardConfig)) {
      updateSimTelemetry(appState, boardConfig);
    }

    StaticJsonDocument<200> telemDoc;
    telemDoc["type"] = "live_data";
    telemDoc["time"] = millis();
    float thrust = 0.0f;
    if (readThrust(simEnabled(boardConfig), loadCellInitialized ? loadCell : nullptr, appState, &thrust)) {
      telemDoc["thrust"] = thrust;
    } else {
      telemDoc["thrust"] = 0.0;
    }
    telemDoc["pwm"] = appState.currentPwm;
    telemDoc["voltage"] = appState.escVoltage;
    telemDoc["current"] = appState.escCurrent;
    char telemOutput[256];
    size_t len = serializeJson(telemDoc, telemOutput, sizeof(telemOutput));
    if (len > 0) {
      notifyClients(ws, boardConfig, appState.wifiProvisioningMode, telemOutput);
    }
  }

  tickTestRunner(appState, boardConfig, simEnabled(boardConfig), loadCellInitialized ? loadCell : nullptr, ws);

  delay(1);
}
