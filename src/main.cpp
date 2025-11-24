#include "FS.h"
#include "HX711_ADC.h"
#include "LittleFS.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <vector>

// --- Configuration ---
const char *ssid = "romadOK24";
const char *password = "boosido3087";

// Pin Assignments
const int HX711_DOUT_PIN = 21;
const int HX711_SCK_PIN = 22;
const int ESC_PIN = 27;
const int ESC_TELEM_PIN = 32; // <-- Changed from 4 to 15

// ESC Configuration
const int ESC_PWM_CHANNEL = 0;
const int PWM_FREQ = 50;
const int PWM_RESOLUTION = 16;
const int MIN_PULSE_WIDTH = 1000;
const int MAX_PULSE_WIDTH = 2000;

// Safety Configuration
const float ABNORMAL_THRUST_DROP =
    75.0; // grams. Trigger safety if thrust drops by this much while PWM is
          // stable.
const unsigned long SAFETY_CHECK_INTERVAL =
    100; // ms. How often to check for anomalies.

// --- Global Objects ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
HX711_ADC LoadCell(HX711_DOUT_PIN, HX711_SCK_PIN);

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
const size_t MAX_TEST_SAMPLES = 6000;
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
int currentPwm = MIN_PULSE_WIDTH;
int previousPwmForRamp = MIN_PULSE_WIDTH;

// Safety trackers
float lastThrustForSafetyCheck = 0.0;
unsigned long lastSafetyCheckTime = 0;

// --- Scale Factor Persistent Storage ---
float scaleFactor = -204.0; // Default calibration value

const char *SCALE_FACTOR_FILE = "/scale_factor.txt";

void saveScaleFactor(float value) {
  File file = LittleFS.open(SCALE_FACTOR_FILE, "w");
  if (file) {
    file.printf("%.6f", value);
    file.close();
    Serial.printf("Scale factor saved: %.6f\n", value);
  } else {
    Serial.println("Failed to save scale factor!");
  }
}

float loadScaleFactor() {
  if (LittleFS.exists(SCALE_FACTOR_FILE)) {
    File file = LittleFS.open(SCALE_FACTOR_FILE, "r");
    if (file) {
      String val = file.readString();
      file.close();
      float loaded = val.toFloat();
      Serial.printf("Loaded scale factor: %.6f\n", loaded);
      return loaded;
    }
  }
  Serial.println("Using default scale factor.");
  return scaleFactor;
}

// --- Helper Functions ---
void notifyClients(String message) { ws.textAll(message); }

void setEscThrottlePwm(int pulse_width_us) {
  if (pulse_width_us < MIN_PULSE_WIDTH)
    pulse_width_us = MIN_PULSE_WIDTH;
  if (pulse_width_us > MAX_PULSE_WIDTH)
    pulse_width_us = MAX_PULSE_WIDTH;

  currentPwm = pulse_width_us; // Update global PWM tracker

  uint32_t duty = (65535UL * pulse_width_us) / 20000;
  ledcWrite(ESC_PWM_CHANNEL, duty);
}

void triggerSafetyShutdown(const char *reason) {
  setEscThrottlePwm(MIN_PULSE_WIDTH);
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

void finishTest() {
  setEscThrottlePwm(MIN_PULSE_WIDTH);
  currentState = TEST_FINISHED;
  Serial.println("Test sequence finished.");

  StaticJsonDocument<200> doc;
  doc["type"] = "status";
  doc["message"] = "Test finished. Sending final results.";
  String output;
  serializeJson(doc, output);
  notifyClients(output);

  // Create and send final results JSON
  DynamicJsonDocument finalDoc(15000);
  finalDoc["type"] = "final_results";
  JsonArray data = finalDoc.createNestedArray("data");
  for (const auto &point : testResults) {
    JsonObject dataPoint = data.createNestedObject();
    dataPoint["time"] = point.timestamp;
    dataPoint["thrust"] = point.thrust;
    dataPoint["pwm"] = point.pwm;
  }
  String finalOutput;
  serializeJson(finalDoc, finalOutput);
  notifyClients(finalOutput);

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
    Serial.printf("WebSocket client #%u connected\n", client->id());
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

      if (strcmp(command, "start_test") == 0) {
        if (currentState == IDLE) {
          const char *sequence = doc["sequence"];
          Serial.printf("Received test sequence: %s\n", sequence);
          if (parseAndStoreSequence(sequence)) {
            Serial.println(
                "Sequence parsed successfully. Starting pre-test tare.");
            currentState = PRE_TEST_TARE;
            stepStartTime = millis(); // Use step timer for pre-tare
          } else {
            triggerSafetyShutdown("Invalid test sequence format.");
          }
        }
      } else if (strcmp(command, "stop_test") == 0) {
        triggerSafetyShutdown("Test stopped by user.");
      } else if (strcmp(command, "reset") == 0) {
        setEscThrottlePwm(MIN_PULSE_WIDTH);
        currentState = IDLE;
        testResults.clear();
        testSequence.clear();
        notifyClients("{\"type\":\"status\", \"message\":\"System reset.\"}");
      } else if (strcmp(command, "tare") == 0) {
        LoadCell.tare();
        notifyClients("{\"type\":\"status\", \"message\":\"Scale tared.\"}");
      } else if (strcmp(command, "set_scale_factor") == 0) {
        if (doc.containsKey("value")) {
          float newFactor = doc["value"];
          scaleFactor = newFactor;
          LoadCell.setCalFactor(scaleFactor);
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
        LoadCell.update();
        long raw = LoadCell.getData();
        float weight = LoadCell.getData();
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
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");

  unsigned long startAttemptTime = millis();
  bool connected = false;
  while (millis() - startAttemptTime < 10000) { // 10 second timeout
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
  } else {
    Serial.println("\nWiFi Connection Failed. Starting AP Mode.");
    WiFi.softAP("ThrustScale_AP");
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
  }
}

void initLittleFS() {
  if (!LittleFS.begin()) {
    Serial.println("An error has occurred while mounting LittleFS");
    return;
  }
  Serial.println("LittleFS mounted successfully");
}

void initLoadCell() {
  LoadCell.begin();
  scaleFactor = loadScaleFactor();
  LoadCell.setCalFactor(scaleFactor);
  Serial.printf("Using scale factor: %.6f\n", scaleFactor);
  Serial.println("Taring scale at startup...");
  LoadCell.tare();
  Serial.println("Startup Tare Complete.");
}

void setup() {
  Serial.begin(115200);

  initLittleFS();
  initWiFi();
  initLoadCell();

  // Init ESC
  ledcSetup(ESC_PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(ESC_PIN, ESC_PWM_CHANNEL);
  currentState = ARMING;

  pinMode(ESC_TELEM_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(ESC_TELEM_PIN), handleTelemInterrupt,
                  CHANGE);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.begin();
}

// --- ESC Telemetry (Voltage/Current) ---
// --- ESC Telemetry (Voltage/Current) ---
float escVoltage = 0.0;
float escCurrent = 0.0;

volatile uint32_t latestPulseWidth = 0;
volatile unsigned long pulseStartTime = 0;

void IRAM_ATTR handleTelemInterrupt() {
  if (digitalRead(ESC_TELEM_PIN) == HIGH) {
    pulseStartTime = micros();
  } else {
    latestPulseWidth = micros() - pulseStartTime;
  }
}

void readEscTelemetry() {
  // Atomic read of volatile variable (uint32_t is atomic on ESP32 usually, but
  // good practice to be safe if needed)
  uint32_t pulse = latestPulseWidth;

  // Reset to 0 to detect if signal is lost (optional, or keep last known value)
  // latestPulseWidth = 0;

  if (pulse > 1000 && pulse < 2000) {
    escVoltage = (pulse - 1000) / 100.0;
  } else if (pulse > 2000 && pulse < 3000) {
    escCurrent = (pulse - 2000) / 100.0;
  }
}

// --- Main Loop (State Machine) ---
void loop() {
  ws.cleanupClients();
  readEscTelemetry();

  // Send live telemetry ONLY when not running a test sequence
  if (currentState != RUNNING_SEQUENCE) {
    StaticJsonDocument<200> telemDoc;
    telemDoc["type"] = "live_data";
    telemDoc["time"] = millis(); // Absolute time, not relevant for idle
    if (LoadCell.update()) {
      telemDoc["thrust"] = LoadCell.getData();
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
    Serial.println("Arming ESC... Sending min throttle.");
    setEscThrottlePwm(MIN_PULSE_WIDTH);
    delay(2100);
    notifyClients("{\"type\":\"status\", \"message\":\"ESC Armed. Ready.\"}");
    currentState = IDLE;
    break;
  }
  case PRE_TEST_TARE: {
    if (millis() - stepStartTime < 2000) {
      setEscThrottlePwm(1100);
    } else {
      setEscThrottlePwm(MIN_PULSE_WIDTH);
      delay(500);
      LoadCell.tare();
      Serial.println("Pre-test tare complete.");
      notifyClients("{\"type\":\"status\", \"message\":\"Pre-test tare "
                    "complete. Starting sequence.\"}");

      currentState = RUNNING_SEQUENCE;
      currentSequenceStep = 0;
      testStartTime = millis();
      stepStartTime = millis();
      previousPwmForRamp = MIN_PULSE_WIDTH;
      testResults.clear();
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

    if (LoadCell.update()) {
      unsigned long currentTime = millis() - testStartTime;
      float currentThrust = LoadCell.getData();

      if (testResults.size() < MAX_TEST_SAMPLES) {
        testResults.push_back({currentTime, currentThrust, currentPwm});
      } else if (testResults.size() == MAX_TEST_SAMPLES) {
        // Mark full (optional: could add a flag or log once)
        testResults.push_back(
            {currentTime, currentThrust, currentPwm}); // Add one last point
        Serial.println("Memory limit reached for test results!");
      }

      // Send live data
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

      if (millis() - lastSafetyCheckTime > SAFETY_CHECK_INTERVAL) {
        bool isStablePhase = (elapsedInStep > step.spinup_ms);
        if (currentPwm > 1150 && isStablePhase) {
          if ((lastThrustForSafetyCheck - currentThrust) >
              ABNORMAL_THRUST_DROP) {
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
