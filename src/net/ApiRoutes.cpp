#include "ApiRoutes.h"

#include "FS.h"
#include "LittleFS.h"
#include "ArduinoJson.h"
#include "Auth.h"
#include "config/BoardConfig.h"
#include "net/WiFiManager.h"
#include "test/TestRunner.h"
#include <Arduino.h>
#include <WiFi.h>

void setupApiRoutes(AsyncWebServer &server, AsyncWebSocket &ws, AppState &state, BoardConfig &cfg, HX711_ADC *loadCell) {
  (void)loadCell;

  server.on("/", HTTP_GET, [&cfg, &state](AsyncWebServerRequest *request) {
    if (!isAuthorizedRequest(cfg, state.wifiProvisioningMode, request)) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }
    if (state.wifiProvisioningMode) request->send(LittleFS, "/wifi_setup.html", "text/html");
    else request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/api/scan", HTTP_GET, [&cfg, &state](AsyncWebServerRequest *request) {
    if (!isAuthorizedRequest(cfg, state.wifiProvisioningMode, request)) {
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

  server.on("/api/wifi", HTTP_POST,
            [&cfg, &state](AsyncWebServerRequest *request) {
              if (!isAuthorizedRequest(cfg, state.wifiProvisioningMode, request)) {
                request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
              }
              if (state.pendingWifiRequest) {
                request->send(409, "application/json", "{\"error\":\"WiFi connect already in progress\"}");
                return;
              }
            },
            nullptr,
            [&cfg, &state](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
              if (!isAuthorizedRequest(cfg, state.wifiProvisioningMode, request)) return;
              if (state.pendingWifiRequest) return;
              static String body;
              if (index == 0) body = "";
              if (total > 512) {
                request->send(413, "application/json", "{\"error\":\"Body too large\"}");
                return;
              }
              for (size_t i = 0; i < len; i++) body += (char)data[i];
              if (index + len != total) return;
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
              state.pendingWifiRequest = request;
              state.pendingWifiSsid = ssid;
              state.pendingWifiPassword = password;
              state.pendingWifiStartTime = millis();
            });

  server.on("/api/config", HTTP_GET, [&cfg, &state](AsyncWebServerRequest *request) {
    if (!isAuthorizedRequest(cfg, state.wifiProvisioningMode, request)) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }
    const char *cfgPath = getBoardConfigPath();
    if (!LittleFS.exists(cfgPath)) {
      request->send(404, "text/plain", "");
      return;
    }
    File f = LittleFS.open(cfgPath, "r");
    if (!f) {
      request->send(500, "text/plain", "Failed to read config");
      return;
    }
    String content = f.readString();
    f.close();
    request->send(200, "text/plain", content);
  });

  server.on("/api/results/latest", HTTP_GET, [&cfg, &state](AsyncWebServerRequest *request) {
    if (!isAuthorizedRequest(cfg, state.wifiProvisioningMode, request)) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }
    const char *resultsPath = getLastResultsPath();
    if (!LittleFS.exists(resultsPath)) {
      request->send(404, "text/plain", "No saved results");
      return;
    }
    request->send(LittleFS, resultsPath, "text/csv");
  });

  server.on("/api/telemetry/status", HTTP_GET, [&cfg, &state](AsyncWebServerRequest *request) {
    if (!isAuthorizedRequest(cfg, state.wifiProvisioningMode, request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    StaticJsonDocument<256> doc;
    doc["esc_voltage"] = state.escVoltage;
    doc["esc_current"] = state.escCurrent;
    doc["esc_telem_stale"] = state.escTelemStale;
    doc["esc_telem_age_ms"] = state.escTelemAgeMs;
    doc["pwm"] = state.currentPwm;
    doc["state"] = (int)state.currentState;
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on("/api/config/default", HTTP_GET, [&cfg, &state](AsyncWebServerRequest *request) {
    if (!isAuthorizedRequest(cfg, state.wifiProvisioningMode, request)) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }
    String content;
    size_t len = getDefaultBoardConfigLen();
    content.reserve(len);
    const char *pgm = getDefaultBoardConfigPgm();
    for (size_t i = 0; i < len; i++) content += (char)pgm_read_byte(pgm + i);
    request->send(200, "text/plain", content);
  });

  server.on("/api/config/validate", HTTP_POST,
            [&cfg, &state](AsyncWebServerRequest *request) {
              if (!isAuthorizedRequest(cfg, state.wifiProvisioningMode, request)) {
                request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
              }
            },
            nullptr,
            [&cfg, &state](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
              if (!isAuthorizedRequest(cfg, state.wifiProvisioningMode, request)) return;
              static String body;
              if (index == 0) body = "";
              if (total > 8192) {
                request->send(400, "application/json", "{\"error\":\"Config too large\"}");
                return;
              }
              for (size_t i = 0; i < len; i++) body += (char)data[i];
              if (index + len != total) return;
              if (body.length() == 0) {
                request->send(400, "application/json", "{\"error\":\"Empty config\"}");
                return;
              }
              BoardConfig backup = cfg;
              setBoardConfigDefaults(cfg);
              char errSection[32] = "";
              char errKey[32] = "";
              char errMessage[64] = "";
              if (!parseConfigContentDetailed(body.c_str(), cfg, true, errSection, sizeof(errSection), errKey, sizeof(errKey), errMessage, sizeof(errMessage))) {
                cfg = backup;
                StaticJsonDocument<192> errDoc;
                errDoc["error"] = "Invalid config";
                errDoc["section"] = errSection;
                errDoc["key"] = errKey;
                errDoc["message"] = errMessage;
                String out;
                serializeJson(errDoc, out);
                request->send(400, "application/json", out);
                return;
              }
              cfg = backup;
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            });

  server.on("/api/config", HTTP_POST,
            [&cfg, &state](AsyncWebServerRequest *request) {
              if (!isAuthorizedRequest(cfg, state.wifiProvisioningMode, request)) {
                request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
              }
            },
            nullptr,
            [&cfg, &state](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
              if (!isAuthorizedRequest(cfg, state.wifiProvisioningMode, request)) return;
              static String body;
              if (index == 0) body = "";
              if (total > 8192) {
                request->send(400, "application/json", "{\"error\":\"Config too large\"}");
                return;
              }
              for (size_t i = 0; i < len; i++) body += (char)data[i];
              if (index + len != total) return;
              if (body.length() == 0) {
                request->send(400, "application/json", "{\"error\":\"Empty config\"}");
                return;
              }
              BoardConfig backup = cfg;
              setBoardConfigDefaults(cfg);
              char errSection[32] = "";
              char errKey[32] = "";
              char errMessage[64] = "";
              if (!parseConfigContentDetailed(body.c_str(), cfg, true, errSection, sizeof(errSection), errKey, sizeof(errKey), errMessage, sizeof(errMessage))) {
                cfg = backup;
                StaticJsonDocument<192> errDoc;
                errDoc["error"] = "Invalid config";
                errDoc["section"] = errSection;
                errDoc["key"] = errKey;
                errDoc["message"] = errMessage;
                String out;
                serializeJson(errDoc, out);
                request->send(400, "application/json", out);
                return;
              }
              const char *cfgPath = getBoardConfigPath();
              File f = LittleFS.open(cfgPath, "w");
              if (!f) {
                cfg = backup;
                request->send(500, "application/json", "{\"error\":\"Failed to write config\"}");
                return;
              }
              f.print(body);
              f.close();
              request->send(200, "application/json", "{\"status\":\"saved\"}");
            });

  server.on("/api/reboot", HTTP_POST, [&cfg, &state](AsyncWebServerRequest *request) {
    if (!isAuthorizedRequest(cfg, state.wifiProvisioningMode, request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    state.rebootAtMs = millis() + 250;
    request->send(200, "application/json", "{\"status\":\"rebooting\"}");
  });

  (void)ws;
}
