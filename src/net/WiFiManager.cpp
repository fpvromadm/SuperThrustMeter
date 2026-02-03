#include "WiFiManager.h"

#include "FS.h"
#include "LittleFS.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>

bool loadWiFiCredentials(const BoardConfig &cfg, char *ssidBuf, char *passBuf, size_t maxLen) {
  if (ssidBuf == nullptr || passBuf == nullptr || maxLen == 0) return false;
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

  if (!LittleFS.exists(cfg.wifi_credentials_file)) return false;
  File file = LittleFS.open(cfg.wifi_credentials_file, "r");
  if (!file) return false;
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err || !doc["ssid"].is<const char *>() || !doc["password"].is<const char *>()) return false;
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
  return true;
}

bool saveWiFiCredentials(const char *ssid, const char *password) {
  if (ssid == nullptr || password == nullptr) return false;
  Preferences prefs;
  if (!prefs.begin("wifi", false)) return false;
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  prefs.end();
  Serial.println("WiFi credentials saved to NVS.");
  return true;
}

void initWiFi(AppState &state, const BoardConfig &cfg) {
  char savedSsid[64];
  char savedPass[64];
  if (!loadWiFiCredentials(cfg, savedSsid, savedPass, sizeof(savedSsid))) {
    Serial.println("No WiFi credentials. Starting setup AP.");
    state.wifiProvisioningMode = true;
    WiFi.mode(WIFI_AP);
    if (strlen(cfg.wifi_ap_password) >= 8) {
      WiFi.softAP(cfg.wifi_ap_name, cfg.wifi_ap_password);
    } else {
      WiFi.softAP(cfg.wifi_ap_name);
    }
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid, savedPass);
  Serial.print("Connecting to WiFi ..");

  const unsigned long timeoutMs = cfg.wifi_connect_timeout_ms;
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
    state.wifiProvisioningMode = false;
    return;
  }

  Serial.println("\nWiFi connection failed. Starting setup AP.");
  state.wifiProvisioningMode = true;
  WiFi.mode(WIFI_AP);
  if (strlen(cfg.wifi_ap_password) >= 8) {
    WiFi.softAP(cfg.wifi_ap_name, cfg.wifi_ap_password);
  } else {
    WiFi.softAP(cfg.wifi_ap_name);
  }
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void tickWiFiProvisioning(AppState &state, const BoardConfig &cfg) {
  if (!state.pendingWifiRequest) return;
  const unsigned long timeoutMs = cfg.wifi_connect_timeout_ms;
  if (WiFi.status() == WL_CONNECTED) {
    if (!saveWiFiCredentials(state.pendingWifiSsid.c_str(), state.pendingWifiPassword.c_str())) {
      state.pendingWifiRequest->send(500, "application/json", "{\"error\":\"Failed to save credentials\"}");
    } else {
      String ip = WiFi.localIP().toString();
      String json = "{\"status\":\"saved\",\"ip\":\"" + ip + "\"}";
      state.pendingWifiRequest->send(200, "application/json", json);
      state.rebootAtMs = millis() + cfg.wifi_save_reboot_delay_ms;
    }
    state.pendingWifiRequest = nullptr;
    state.pendingWifiSsid = "";
    state.pendingWifiPassword = "";
  } else if (millis() - state.pendingWifiStartTime >= timeoutMs) {
    state.pendingWifiRequest->send(400, "application/json", "{\"error\":\"Connection failed\"}");
    state.pendingWifiRequest = nullptr;
    state.pendingWifiSsid = "";
    state.pendingWifiPassword = "";
  }
}
