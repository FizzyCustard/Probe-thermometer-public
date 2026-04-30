#pragma once

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "thermistor.h"
#include "logger.h"
#include "cook_session.h"

extern AsyncWebSocket ws;
extern float meatTarget;
extern float grillLow;
extern float grillHigh;
extern Preset    presets[];
extern uint8_t   presetCount;
extern Favourite favs[];
extern uint8_t   favCount;

// Forward declarations
static void sendConfig(AsyncWebSocketClient* client);

// ── Inbound message handler ───────────────────────────────────────────────────

void onWsMessage(AsyncWebSocketClient* client, String msg) {
  JsonDocument doc;
  if (deserializeJson(doc, msg) != DeserializationError::Ok) return;

  const char* type = doc["type"] | "";

  // ── Target settings ───────────────────────────────────────────────────────
  if (strcmp(type, "settings") == 0) {
    meatTarget = doc["meat_target"].as<float>();
    grillLow   = doc["grill_low"].as<float>();
    grillHigh  = doc["grill_high"].as<float>();
    saveSettings(meatTarget, grillLow, grillHigh);
    Serial.printf("Settings updated: meat=%.1f grill=%.1f–%.1f\n",
                  meatTarget, grillLow, grillHigh);
    return;
  }

  // ── Cook: start ───────────────────────────────────────────────────────────
  if (strcmp(type, "cook_start") == 0) {
    const char* name   = doc["name"]   | "Unnamed cook";
    const char* preset = doc["preset"] | "";
    float mt  = doc["meat_target"].as<float>();
    float gl  = doc["grill_low"].as<float>();
    float gh  = doc["grill_high"].as<float>();

    // Use provided timestamp if supplied, otherwise generate one
    String ts = doc.containsKey("ts") ? String(doc["ts"].as<const char*>())
                                      : String("unknown");

    bool started = cookStart(name, preset, mt, gl, gh, ts.c_str());

    JsonDocument resp;
    resp["type"]    = "cook_started";
    resp["started"] = started;
    String out;
    serializeJson(resp, out);
    client->text(out);
    return;
  }

  // ── Cook: stop ────────────────────────────────────────────────────────────
  if (strcmp(type, "cook_stop") == 0) {
    String ts = doc.containsKey("ts") ? String(doc["ts"].as<const char*>())
                                      : String("unknown");
    cookStop(ts.c_str());

    JsonDocument resp;
    resp["type"] = "cook_stopped";
    String out;
    serializeJson(resp, out);
    client->text(out);
    return;
  }

  // ── Cook: add note ────────────────────────────────────────────────────────
  if (strcmp(type, "cook_note") == 0) {
    const char* text = doc["text"] | "";
    float meatC = doc["meat_c"] | NAN;
    cookAppendNote(text, meatC);
    return;
  }

  // ── Favourite: save / update ──────────────────────────────────────────────
  if (strcmp(type, "fav_save") == 0) {
    const char* id = doc["id"] | "";
    if (strlen(id) == 0) return;

    // Find existing entry to update, or append
    int idx = -1;
    for (uint8_t i = 0; i < favCount; i++) {
      if (strcmp(favs[i].id, id) == 0) { idx = i; break; }
    }
    if (idx < 0 && favCount < MAX_FAVS) idx = favCount++;

    if (idx >= 0) {
      strlcpy(favs[idx].id,     id,                            sizeof(favs[idx].id));
      strlcpy(favs[idx].name,   doc["name"]   | "",            sizeof(favs[idx].name));
      strlcpy(favs[idx].preset, doc["preset"] | "",            sizeof(favs[idx].preset));
      favs[idx].meatTarget = doc["meat_target"].as<float>();
      favs[idx].grillLow   = doc["grill_low"].as<float>();
      favs[idx].grillHigh  = doc["grill_high"].as<float>();
      saveFavourites(favs, favCount);
    }

    sendConfig(client);
    return;
  }

  // ── Favourite: delete ─────────────────────────────────────────────────────
  if (strcmp(type, "fav_delete") == 0) {
    const char* id = doc["id"] | "";
    for (uint8_t i = 0; i < favCount; i++) {
      if (strcmp(favs[i].id, id) == 0) {
        // Shift remaining entries down
        memmove(&favs[i], &favs[i + 1], (favCount - i - 1) * sizeof(Favourite));
        favCount--;
        saveFavourites(favs, favCount);
        break;
      }
    }
    sendConfig(client);
    return;
  }

  // ── Request full config ───────────────────────────────────────────────────
  if (strcmp(type, "request_config") == 0) {
    sendConfig(client);
    return;
  }

  // ── Calibration: read live resistance on a probe channel ─────────────────
  if (strcmp(type, "cal_read") == 0) {
    uint8_t probe = doc["probe"].as<uint8_t>();
    uint8_t point = doc["point"] | 0;
    if (probe > 1) return;

    float r = readResistance(probe);
    JsonDocument resp;
    resp["type"]       = "cal_reading";
    resp["probe"]      = probe;
    resp["point"]      = point;
    resp["resistance"] = r;
    String out;
    serializeJson(resp, out);
    client->text(out);
    Serial.printf("Cal read probe %d point %d: %.0f Ω\n", probe, point, r);
    return;
  }

  // ── Calibration: apply and save coefficients sent by browser ─────────────
  if (strcmp(type, "cal_save") == 0) {
    uint8_t probe = doc["probe"].as<uint8_t>();
    if (probe > 1) return;

    SHCoeffs c;
    c.A = doc["A"].as<float>();
    c.B = doc["B"].as<float>();
    c.C = doc["C"].as<float>();
    probeCoeffs[probe] = c;

    float t1 = doc["points"][0]["temp_c"].as<float>();
    float r1 = doc["points"][0]["resistance"].as<float>();
    float t2 = doc["points"][1]["temp_c"].as<float>();
    float r2 = doc["points"][1]["resistance"].as<float>();
    float t3 = doc["points"][2]["temp_c"].as<float>();
    float r3 = doc["points"][2]["resistance"].as<float>();
    String ts = doc["timestamp"] | "unknown";

    bool saved = saveCalibration(probe, c, t1, r1, t2, r2, t3, r3, ts);

    JsonDocument resp;
    resp["type"]  = "cal_saved";
    resp["probe"] = probe;
    resp["saved"] = saved;
    String out;
    serializeJson(resp, out);
    client->text(out);
    Serial.printf("Calibration %s for probe %d: A=%.4e B=%.4e C=%.4e\n",
                  saved ? "saved" : "FAILED", probe, c.A, c.B, c.C);
    return;
  }
}

// ── Config payload ────────────────────────────────────────────────────────────
//
// Sent to a single client on connect or request_config.
// Carries current settings, all presets, and all favourites.

static void sendConfig(AsyncWebSocketClient* client) {
  JsonDocument doc;
  doc["type"] = "config";

  JsonObject settings = doc["settings"].to<JsonObject>();
  settings["meat_target"] = meatTarget;
  settings["grill_low"]   = grillLow;
  settings["grill_high"]  = grillHigh;

  JsonArray pArr = doc["presets"].to<JsonArray>();
  for (uint8_t i = 0; i < presetCount; i++) {
    JsonObject p = pArr.add<JsonObject>();
    p["name"] = presets[i].name;
    p["c"]    = presets[i].tempC;
  }

  JsonArray fArr = doc["favourites"].to<JsonArray>();
  for (uint8_t i = 0; i < favCount; i++) {
    JsonObject f = fArr.add<JsonObject>();
    f["id"]          = favs[i].id;
    f["name"]        = favs[i].name;
    f["meat_target"] = favs[i].meatTarget;
    f["grill_low"]   = favs[i].grillLow;
    f["grill_high"]  = favs[i].grillHigh;
    f["preset"]      = favs[i].preset;
  }

  String out;
  serializeJson(doc, out);
  client->text(out);
}

// ── WebSocket event dispatcher ────────────────────────────────────────────────

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WS client #%u connected from %s\n",
                    client->id(), client->remoteIP().toString().c_str());
      sendConfig(client);
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WS client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA: {
      AwsFrameInfo* info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len
          && info->opcode == WS_TEXT) {
        data[len] = '\0';
        onWsMessage(client, String((char*)data));
      }
      break;
    }
    default:
      break;
  }
}

void wsBegin(AsyncWebServer* server) {
  ws.onEvent(onWsEvent);
  server->addHandler(&ws);
}

// ── Outbound broadcast ────────────────────────────────────────────────────────
//
// Pushed every 1s to all connected clients. Includes cook state when active.

void wsBroadcast(float meatC, float grillC, String timestamp) {
  if (ws.count() == 0) return;

  JsonDocument doc;
  doc["ts"]          = timestamp;
  doc["meat"]["c"]   = meatC;
  doc["grill"]["c"]  = grillC;
  doc["meat_target"] = meatTarget;
  doc["grill_low"]   = grillLow;
  doc["grill_high"]  = grillHigh;

  if (activeCook.active) {
    doc["cook_active"]    = true;
    doc["cook_name"]      = activeCook.name;
    doc["cook_elapsed_s"] = (uint32_t)((millis() - activeCook.startMillis) / 1000UL);
  } else {
    doc["cook_active"] = false;
  }

  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}
