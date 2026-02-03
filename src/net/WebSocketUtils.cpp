#include "WebSocketUtils.h"

#include "Auth.h"

void notifyClients(AsyncWebSocket &ws, const BoardConfig &cfg, bool wifiProvisioningMode, const String &message) {
  auto clients = ws.getClients();
  for (auto clientPtr : clients) {
    AsyncWebSocketClient *client = clientPtr;
    if (isAuthorizedWsClient(cfg, wifiProvisioningMode, client)) {
      client->text(message.c_str());
    }
  }
}

void notifyClients(AsyncWebSocket &ws, const BoardConfig &cfg, bool wifiProvisioningMode, const char *message) {
  if (!message) return;
  auto clients = ws.getClients();
  for (auto clientPtr : clients) {
    AsyncWebSocketClient *client = clientPtr;
    if (isAuthorizedWsClient(cfg, wifiProvisioningMode, client)) {
      client->text(message);
    }
  }
}

bool hasWsClients(AsyncWebSocket &ws) {
  auto clients = ws.getClients();
  for (auto clientPtr : clients) {
    if (clientPtr) return true;
  }
  return false;
}
