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
    String ts = doc["ts"].is<const char*>() ? String(doc["ts"].as<const char*>())
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
    String ts = doc["ts"].is<const char*>() ? String(doc["ts"].as<const char*>())
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

  // ── Request cook history list from SD ─────────────────────────────────────
  if (strcmp(type, "request_cook_list") == 0) {
    JsonDocument resp;
    resp["type"] = "cook_list";
    JsonArray arr = resp["cooks"].to<JsonArray>();

    File dir = SD.open(BBQ_COOKS);
    if (dir) {
      while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (entry.isDirectory()) { entry.close(); continue; }

        String fname = String(entry.name());
        entry.close();
        if (!fname.startsWith("cook_") || !fname.endsWith(".csv")) continue;

        String fullPath = String(BBQ_COOKS) + "/" + fname;
        File f = SD.open(fullPath.c_str(), FILE_READ);
        if (!f) continue;

        JsonObject cook = arr.add<JsonObject>();
        cook["file"] = fname;
        uint32_t lastElapsed = 0;

        while (f.available()) {
          String line = f.readStringUntil('\n');
          line.trim();
          if      (line.startsWith("#name,"))          cook["name"]          = line.substring(6);
          else if (line.startsWith("#preset,"))        cook["preset"]        = line.substring(8);
          else if (line.startsWith("#meat_target_c,")) cook["meat_target_c"] = line.substring(15).toFloat();
          else if (line.startsWith("#grill_low_c,"))   cook["grill_low_c"]   = line.substring(13).toFloat();
          else if (line.startsWith("#grill_high_c,"))  cook["grill_high_c"]  = line.substring(14).toFloat();
          else if (line.startsWith("#started_at,"))    cook["started_at"]    = line.substring(12);
          else if (line.startsWith("#stopped_at,"))    cook["stopped_at"]    = line.substring(12);
          else if (line.length() > 0 && !line.startsWith("#") && !line.startsWith("elapsed")) {
            int comma = line.indexOf(',');
            if (comma > 0) lastElapsed = line.substring(0, comma).toInt();
          }
        }
        f.close();

        if (!cook["stopped_at"].is<const char*>())
          cook["duration_s"] = lastElapsed;
      }
      dir.close();
    }

    String out;
    serializeJson(resp, out);
    client->text(out);
    return;
  }

  // ── Request full time-series for a single cook ───────────────────────────
  if (strcmp(type, "request_cook_data") == 0) {
    const char* fileReq = doc["file"] | "";
    size_t flen = strlen(fileReq);

    // Whitelist filename: cook_<safe-chars>.csv, no path separators or ".."
    if (flen < 10 || flen > 60) return;
    if (strncmp(fileReq, "cook_", 5) != 0) return;
    if (strcmp(fileReq + flen - 4, ".csv") != 0) return;
    if (strstr(fileReq, "..") != nullptr) return;
    for (size_t i = 0; i < flen; i++) {
      char ch = fileReq[i];
      bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
      if (!ok) return;
    }

    String fullPath = String(BBQ_COOKS) + "/" + fileReq;
    File f = SD.open(fullPath.c_str(), FILE_READ);
    if (!f) return;

    // Pass 1: count data rows, read meat target (needed for target-hit calc)
    uint32_t dataRows   = 0;
    float    meatTargetC = 0;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      if (line.startsWith("#meat_target_c,")) meatTargetC = line.substring(15).toFloat();
      if (line.startsWith("#") || line.startsWith("elapsed")) continue;
      dataRows++;
    }

    const uint32_t MAX_POINTS = 240;
    uint32_t stride = (dataRows + MAX_POINTS - 1) / MAX_POINTS;
    if (stride < 1) stride = 1;

    // Pass 2: parse rows, downsample, compute peaks + target-hit, capture notes
    f.seek(0);
    JsonDocument resp;
    resp["type"] = "cook_data";
    resp["file"] = fileReq;
    JsonArray points = resp["points"].to<JsonArray>();
    JsonArray notes  = resp["notes"].to<JsonArray>();

    float    peakMeat   = -1000;
    float    peakGrill  = -1000;
    int32_t  targetHitS = -1;
    uint32_t rowIdx     = 0;

    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;

      if (line.startsWith("#note,")) {
        // #note,<elapsed_s>,<text>,<meat_c>  — text may contain commas
        int c1   = line.indexOf(',', 6);
        int last = line.lastIndexOf(',');
        if (c1 > 6 && last > c1) {
          JsonObject n = notes.add<JsonObject>();
          n["ts"]   = (uint32_t)line.substring(6, c1).toInt();
          n["text"] = line.substring(c1 + 1, last);
        }
        continue;
      }
      if (line.startsWith("#") || line.startsWith("elapsed")) continue;

      int c1 = line.indexOf(',');
      int c2 = line.indexOf(',', c1 + 1);
      if (c1 < 0 || c2 < 0) continue;

      uint32_t t = (uint32_t)line.substring(0, c1).toInt();
      String mStr = line.substring(c1 + 1, c2);
      String gStr = line.substring(c2 + 1);
      bool hasM = mStr.length() > 0;
      bool hasG = gStr.length() > 0;
      float m = hasM ? mStr.toFloat() : 0;
      float g = hasG ? gStr.toFloat() : 0;

      // Track peaks + first target-hit on every row (not just downsampled)
      if (hasM && m > peakMeat)  peakMeat  = m;
      if (hasG && g > peakGrill) peakGrill = g;
      if (hasM && targetHitS < 0 && meatTargetC > 0 && m >= meatTargetC) {
        targetHitS = (int32_t)t;
      }

      if (rowIdx % stride == 0) {
        JsonObject p = points.add<JsonObject>();
        p["t"] = t;
        if (hasM) p["m"] = roundf(m * 10) / 10.0f;
        if (hasG) p["g"] = roundf(g * 10) / 10.0f;
      }
      rowIdx++;
    }
    f.close();

    if (peakMeat  > -999) resp["peak_meat_c"]  = roundf(peakMeat  * 10) / 10.0f;
    if (peakGrill > -999) resp["peak_grill_c"] = roundf(peakGrill * 10) / 10.0f;
    if (targetHitS >= 0)  resp["target_hit_s"] = targetHitS;

    String out;
    serializeJson(resp, out);
    client->text(out);
    Serial.printf("Cook data sent: %s (%u rows → %u points)\n",
                  fileReq, dataRows, (unsigned)points.size());
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

  if (activeCook.active) {
    doc["cook_active"]      = true;
    doc["cook_name"]        = activeCook.name;
    doc["cook_preset"]      = activeCook.preset;
    doc["cook_elapsed_s"]   = cookElapsedSeconds();
    doc["cook_started_at"]  = activeCook.startedAt;
    doc["cook_meat_target"] = activeCook.meatTargetC;
    doc["cook_grill_low"]   = activeCook.grillLowC;
    doc["cook_grill_high"]  = activeCook.grillHighC;
  } else {
    doc["cook_active"] = false;
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
