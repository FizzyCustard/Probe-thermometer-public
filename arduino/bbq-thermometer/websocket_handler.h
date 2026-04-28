#pragma once

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "thermistor.h"
#include "logger.h"

extern AsyncWebSocket ws;
extern float meatTarget;
extern float grillLow;
extern float grillHigh;

// Stores pending calibration state per probe (3 points each)
struct CalPoint { float tempC; float resistance; bool recorded; };
CalPoint calPoints[2][3] = {};

// ── Inbound message handler ───────────────────────────────────────────────────

void onWsMessage(AsyncWebSocketClient* client, String msg) {
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, msg) != DeserializationError::Ok) return;

  const char* type = doc["type"];
  if (!type) return;

  // ── Target settings update from browser ──────────────────────────────────
  if (strcmp(type, "settings") == 0) {
    meatTarget = doc["meat_target"].as<float>();
    grillLow   = doc["grill_low"].as<float>();
    grillHigh  = doc["grill_high"].as<float>();
    Serial.printf("Settings: target=%.1f low=%.1f high=%.1f\n",
                  meatTarget, grillLow, grillHigh);
    return;
  }

  // ── Calibration: read current resistance for a probe channel ─────────────
  //
  // Browser sends:  {"type":"cal_read","probe":0}
  // We respond:     {"type":"cal_reading","probe":0,"resistance":336400.0}
  if (strcmp(type, "cal_read") == 0) {
    uint8_t probe = doc["probe"].as<uint8_t>();
    if (probe > 1) return;
    float r = readResistance(probe);
    StaticJsonDocument<128> resp;
    resp["type"] = "cal_reading";
    resp["probe"] = probe;
    resp["resistance"] = r;
    String out;
    serializeJson(resp, out);
    client->text(out);
    return;
  }

  // ── Calibration: save A/B/C coefficients sent by browser ─────────────────
  //
  // Browser calculates coefficients from the 3 recorded points and sends:
  // {
  //   "type": "cal_save",
  //   "probe": 0,
  //   "A": 4.475e-4, "B": 2.523e-4, "C": 1.701e-9,
  //   "points": [
  //     {"temp_c": 0.0,   "resistance": 336400.0},
  //     {"temp_c": 25.0,  "resistance": 100000.0},
  //     {"temp_c": 100.0, "resistance": 6960.0}
  //   ],
  //   "timestamp": "2025-07-14T14:30:00"
  // }
  if (strcmp(type, "cal_save") == 0) {
    uint8_t probe = doc["probe"].as<uint8_t>();
    if (probe > 1) return;

    SHCoeffs c;
    c.A = doc["A"].as<float>();
    c.B = doc["B"].as<float>();
    c.C = doc["C"].as<float>();
    probeCoeffs[probe] = c;  // apply immediately

    float t1 = doc["points"][0]["temp_c"].as<float>();
    float r1 = doc["points"][0]["resistance"].as<float>();
    float t2 = doc["points"][1]["temp_c"].as<float>();
    float r2 = doc["points"][1]["resistance"].as<float>();
    float t3 = doc["points"][2]["temp_c"].as<float>();
    float r3 = doc["points"][2]["resistance"].as<float>();
    String ts = doc["timestamp"].as<String>();

    bool saved = saveCalibration(probe, c, t1, r1, t2, r2, t3, r3, ts);

    StaticJsonDocument<128> resp;
    resp["type"]  = "cal_saved";
    resp["probe"] = probe;
    resp["saved"] = saved;
    String out;
    serializeJson(resp, out);
    client->text(out);
    return;
  }
}

// ── WebSocket event dispatcher ────────────────────────────────────────────────

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WS client connected: %s\n",
                    client->remoteIP().toString().c_str());
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

void wsBegin(AsyncWebServer* server) {
  ws.onEvent(onWsEvent);
  server->addHandler(&ws);
}

// ── Outbound broadcast ────────────────────────────────────────────────────────

void wsBroadcast(float meatC, float grillC, String timestamp) {
  if (ws.count() == 0) return;
  StaticJsonDocument<256> doc;
  doc["ts"]          = timestamp;
  doc["meat"]["c"]   = serialized(String(meatC, 1));
  doc["grill"]["c"]  = serialized(String(grillC, 1));
  doc["meat_target"] = meatTarget;
  doc["grill_low"]   = grillLow;
  doc["grill_high"]  = grillHigh;
  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}
