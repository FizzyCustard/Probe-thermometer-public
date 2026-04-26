#pragma once

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

extern AsyncWebSocket ws;
extern float meatTarget;
extern float grillLow;
extern float grillHigh;

// Called when a message arrives from a browser client.
void onWsMessage(AsyncWebSocketClient* client, String msg) {
  // TODO: parse JSON
  // TODO: if type == "settings", update meatTarget, grillLow, grillHigh
  // TODO: log to Serial
}

// WebSocket event dispatcher — wire this up in setup().
void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WS client connected: %s\n", client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.println("WS client disconnected");
      break;
    case WS_EVT_DATA:
      onWsMessage(client, String((char*)data).substring(0, len));
      break;
    default:
      break;
  }
}

// Registers the WebSocket handler on the server. Call once in setup().
void wsBegin(AsyncWebServer* server) {
  ws.onEvent(onWsEvent);
  server->addHandler(&ws);
}

// Broadcasts current temperature and settings to all connected clients.
void wsBroadcast(float meatC, float grillC, String timestamp) {
  if (ws.count() == 0) return;

  StaticJsonDocument<256> doc;
  // TODO: populate JSON fields (ts, meat.c, grill.c, meat_target, grill_low, grill_high)
  // TODO: serialise to string and ws.textAll(...)
}
