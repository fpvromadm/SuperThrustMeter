#include "WebSocketHandler.h"

#include "ArduinoJson.h"
#include "Auth.h"
#include "net/WebSocketUtils.h"
#include "scale/LoadCellManager.h"
#include "sim/Simulator.h"
#include "test/TestRunner.h"
#include "util/Log.h"
#include <Arduino.h>

static AppState *s_state = nullptr;
static BoardConfig *s_cfg = nullptr;
static HX711_ADC *s_loadCell = nullptr;

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (!s_state || !s_cfg) return;

  if (type == WS_EVT_CONNECT) {
    if (client) client->_tempObject = nullptr;
    if (client) client->keepAlivePeriod(10);
    Serial.printf("WebSocket client #%u connected\n", client->id());
    if (simEnabled(*s_cfg) && client) {
      StaticJsonDocument<128> doc;
      doc["type"] = "status";
      doc["message"] = "Simulation mode active.";
      char output[192];
      size_t outLen = serializeJson(doc, output, sizeof(output));
      if (outLen > 0) {
        client->text(output);
      }
    }
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0;

      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, (char *)data);
      if (error) return;

      const char *command = doc["command"];

      if (authEnabled(*s_cfg, s_state->wifiProvisioningMode) && !isAuthorizedWsClient(*s_cfg, s_state->wifiProvisioningMode, client)) {
        const char *token = doc["token"];
        if (!tokenMatches(*s_cfg, s_state->wifiProvisioningMode, token)) {
          logWarn("WebSocket unauthorized message");
          if (client) client->close();
          return;
        }
        if (client) client->_tempObject = (void *)1;
        if (command && strcmp(command, "auth") == 0) return;
      }

      if (!command) return;

      if (strcmp(command, "ping") == 0) {
        StaticJsonDocument<64> resp;
        resp["type"] = "pong";
        char out[128];
        size_t outLen = serializeJson(resp, out, sizeof(out));
        if (client && outLen > 0) client->text(out);
        return;
      }

      if (strcmp(command, "start_test") == 0) {
        if (s_state->currentState == State::IDLE) {
          const char *sequence = doc["sequence"];
          Serial.printf("Received test sequence: %s\n", sequence);
          if (parseAndStoreSequence(*s_state, sequence)) {
            deleteLastResultsFile();
            Serial.println("Sequence parsed successfully. Starting pre-test tare.");
            startPreTestTare(*s_state, *s_cfg);
          } else {
            triggerSafetyShutdown(*s_state, *s_cfg, simEnabled(*s_cfg), *server, "Invalid test sequence format.");
          }
        }
      } else if (strcmp(command, "stop_test") == 0) {
        triggerSafetyShutdown(*s_state, *s_cfg, simEnabled(*s_cfg), *server, "Test stopped by user.");
      } else if (strcmp(command, "reset") == 0) {
        setEscThrottlePwm(*s_state, *s_cfg, simEnabled(*s_cfg), s_cfg->min_pulse_width);
        resetTest(*s_state);
        notifyClients(*server, *s_cfg, s_state->wifiProvisioningMode, "{\"type\":\"status\", \"message\":\"System reset.\"}");
      } else if (strcmp(command, "tare") == 0) {
        tareScale(simEnabled(*s_cfg), s_loadCell, *s_state);
        notifyClients(*server, *s_cfg, s_state->wifiProvisioningMode, "{\"type\":\"status\", \"message\":\"Scale tared.\"}");
      } else if (strcmp(command, "set_scale_factor") == 0) {
        if (doc.containsKey("value")) {
          float newFactor = doc["value"];
          setScaleFactor(s_loadCell, *s_state, *s_cfg, newFactor);
          StaticJsonDocument<128> resp;
          resp["type"] = "scale_factor";
          resp["value"] = getScaleFactor(*s_state);
          char out[192];
          size_t outLen = serializeJson(resp, out, sizeof(out));
          if (outLen > 0) {
            notifyClients(*server, *s_cfg, s_state->wifiProvisioningMode, out);
          }
          Serial.printf("Scale factor set to: %.6f\n", newFactor);
        }
      } else if (strcmp(command, "get_scale_factor") == 0) {
        StaticJsonDocument<128> resp;
        resp["type"] = "scale_factor";
        resp["value"] = getScaleFactor(*s_state);
        char out[192];
        size_t outLen = serializeJson(resp, out, sizeof(out));
        if (outLen > 0) {
          notifyClients(*server, *s_cfg, s_state->wifiProvisioningMode, out);
        }
      } else if (strcmp(command, "get_raw_reading") == 0) {
        if (simEnabled(*s_cfg)) {
          updateSimTelemetry(*s_state, *s_cfg);
        }
        float weight = 0.0f;
        long raw = readRawReading(simEnabled(*s_cfg), s_loadCell, *s_state, &weight);
        StaticJsonDocument<128> resp;
        resp["type"] = "raw_reading";
        resp["raw"] = raw;
        resp["weight"] = weight;
        resp["factor"] = getScaleFactor(*s_state);
        char out[256];
        size_t outLen = serializeJson(resp, out, sizeof(out));
        if (outLen > 0) {
          notifyClients(*server, *s_cfg, s_state->wifiProvisioningMode, out);
        }
      }
    }
  }
}

void configureWebSocket(AsyncWebSocket &ws, AppState &state, BoardConfig &cfg, HX711_ADC *loadCell) {
  s_state = &state;
  s_cfg = &cfg;
  s_loadCell = loadCell;
  ws.onEvent(onWsEvent);
}
