#include "Auth.h"

bool authEnabled(const BoardConfig &cfg, bool wifiProvisioningMode) {
  if (wifiProvisioningMode) return false;
  if (cfg.auth_token[0] == '\0') return false;
  if (strcmp(cfg.auth_token, "changeme") == 0) return false;
  return true;
}

bool tokenMatches(const BoardConfig &cfg, bool wifiProvisioningMode, const char *token) {
  if (!authEnabled(cfg, wifiProvisioningMode)) return true;
  if (token == nullptr) return false;
  return strcmp(token, cfg.auth_token) == 0;
}

bool isAuthorizedRequest(const BoardConfig &cfg, bool wifiProvisioningMode, AsyncWebServerRequest *request) {
  if (!authEnabled(cfg, wifiProvisioningMode)) return true;
  if (request->hasHeader("X-Auth-Token")) {
    AsyncWebHeader *h = request->getHeader("X-Auth-Token");
    if (tokenMatches(cfg, wifiProvisioningMode, h->value().c_str())) return true;
  }
  if (request->hasHeader("Authorization")) {
    AsyncWebHeader *h = request->getHeader("Authorization");
    const String value = h->value();
    const String prefix = "Bearer ";
    if (value.startsWith(prefix)) {
      String token = value.substring(prefix.length());
      if (tokenMatches(cfg, wifiProvisioningMode, token.c_str())) return true;
    }
  }
  if (request->hasParam("token")) {
    AsyncWebParameter *p = request->getParam("token");
    if (tokenMatches(cfg, wifiProvisioningMode, p->value().c_str())) return true;
  }
  return false;
}

bool isAuthorizedWsClient(const BoardConfig &cfg, bool wifiProvisioningMode, AsyncWebSocketClient *client) {
  if (!authEnabled(cfg, wifiProvisioningMode)) return true;
  if (client == nullptr) return false;
  return client->_tempObject == (void *)1;
}
