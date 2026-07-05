#pragma once

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "thermistor.h"
#include "logger.h"
#include "cook_session.h"
#include "wifi_manager.h"

extern AsyncWebSocket ws;
extern float meatTarget;
extern float grillLow;
extern float grillHigh;
extern Preset    presets[];
extern uint8_t   presetCount;
extern Favourite favs[];
extern uint8_t   favCount;

// Defined in the .ino — declared here so handlers can timestamp on-device.
String getTimestamp();
bool   ntpReady();

// Settings sanity limits (°C)
#define MEAT_TARGET_MIN    0.0f
#define MEAT_TARGET_MAX  150.0f
#define GRILL_RANGE_MIN    0.0f
#define GRILL_RANGE_MAX  400.0f

// Forward declarations
static void sendConfig(AsyncWebSocketClient* client);
static void sendCookList(AsyncWebSocketClient* client);
static void sendCookData(AsyncWebSocketClient* client, const char* file);

// Single-slot deferred-request queue. Heavy work (SD reads, WiFi scans) must
// NOT run inside the async WebSocket callback — it blocks the TCP stack and
// can trip the soft watchdog. The message handler records the request here and
// loop() services it via wsTick(). Single user → newest request wins.
//
// `pendingKind` is volatile and written LAST when enqueueing: the callback
// runs from the async context, so loop() must never observe a kind with a
// half-filled payload.
enum PendingKind : uint8_t { PENDING_NONE = 0, PENDING_COOK_LIST, PENDING_COOK_DATA, PENDING_WIFI_SCAN };
static volatile uint8_t pendingKind = PENDING_NONE;
static uint32_t pendingClientId = 0;
static char     pendingFile[64] = "";

// Scheduled restart (set instead of calling delay()+restart() inside a
// callback — the async context must never block).
static uint32_t wsRestartAt = 0;

// In-flight user WiFi test (AP-mode setup flow)
static bool     userWifiTestPending  = false;
static uint32_t userWifiTestClientId = 0;

// ── Inbound message handler ───────────────────────────────────────────────────

void onWsMessage(AsyncWebSocketClient* client, const char* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len) != DeserializationError::Ok) return;

  const char* type = doc["type"] | "";

  // ── Target settings ───────────────────────────────────────────────────────
  if (strcmp(type, "settings") == 0) {
    if (!doc["meat_target"].is<float>() ||
        !doc["grill_low"].is<float>()   ||
        !doc["grill_high"].is<float>()) return;
    float mt = doc["meat_target"].as<float>();
    float gl = doc["grill_low"].as<float>();
    float gh = doc["grill_high"].as<float>();
    if (mt < MEAT_TARGET_MIN || mt > MEAT_TARGET_MAX) return;
    if (gl < GRILL_RANGE_MIN || gh > GRILL_RANGE_MAX || gl >= gh) return;
    if (mt == meatTarget && gl == grillLow && gh == grillHigh) return;  // no change — skip the SD write

    meatTarget = mt;
    grillLow   = gl;
    grillHigh  = gh;
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

    // Prefer the device clock (NTP). The browser timestamp is only a fallback
    // for AP mode, and even then it's whitelisted before touching an SD path.
    char ts[24];
    if (!isAPMode() && ntpReady()) {
      strlcpy(ts, getTimestamp().c_str(), sizeof(ts));
    } else {
      const char* bts = doc["ts"] | "";
      bool ok = strlen(bts) == 19;
      for (const char* p = bts; ok && *p; p++) {
        if (!((*p >= '0' && *p <= '9') || *p == 'T' || *p == ':' || *p == '-')) ok = false;
      }
      strlcpy(ts, ok ? bts : "unknown", sizeof(ts));
    }

    bool started = cookStart(name, preset, mt, gl, gh, ts);

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
    char ts[24];
    if (!isAPMode() && ntpReady()) {
      strlcpy(ts, getTimestamp().c_str(), sizeof(ts));
    } else {
      strlcpy(ts, doc["ts"] | "unknown", sizeof(ts));
      csvSanitize(ts);   // browser-supplied — keep it from corrupting the CSV footer
    }
    cookStop(ts);

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
    // Use NAN sentinel only if field is actually present; cookAppendNote guards it
    float meatC = doc["meat_c"].is<float>() ? doc["meat_c"].as<float>() : NAN;
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

    if (idx < 0) {
      // List full — tell the user instead of silently dropping it
      JsonDocument resp;
      resp["type"]   = "fav_result";
      resp["saved"]  = false;
      resp["reason"] = "full";
      String out;
      serializeJson(resp, out);
      client->text(out);
      return;
    }

    strlcpy(favs[idx].id,     id,                            sizeof(favs[idx].id));
    strlcpy(favs[idx].name,   doc["name"]   | "",            sizeof(favs[idx].name));
    strlcpy(favs[idx].preset, doc["preset"] | "",            sizeof(favs[idx].preset));
    csvSanitize(favs[idx].name);
    csvSanitize(favs[idx].preset);
    favs[idx].meatTarget = doc["meat_target"].as<float>();
    favs[idx].grillLow   = doc["grill_low"].as<float>();
    favs[idx].grillHigh  = doc["grill_high"].as<float>();
    saveFavourites(favs, favCount);

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

  // ── Request cook history list from SD (deferred to loop) ──────────────────
  if (strcmp(type, "request_cook_list") == 0) {
    pendingClientId = client->id();
    pendingKind     = PENDING_COOK_LIST;
    return;
  }

  // ── Request full time-series for a single cook (deferred to loop) ─────────
  if (strcmp(type, "request_cook_data") == 0) {
    const char* fileReq = doc["file"] | "";
    size_t flen = strlen(fileReq);

    // Whitelist filename: cook_<safe-chars>.csv, no path separators or ".."
    // (cheap, string-only — safe to run here; the file I/O is deferred below)
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

    pendingClientId = client->id();
    strlcpy(pendingFile, fileReq, sizeof(pendingFile));  // flen ≤ 60 < 64
    pendingKind = PENDING_COOK_DATA;
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

  // ── WiFi: scan for nearby networks (deferred — a scan blocks for seconds) ─
  if (strcmp(type, "wifi_scan") == 0) {
    pendingClientId = client->id();
    pendingKind     = PENDING_WIFI_SCAN;
    return;
  }

  // ── WiFi: save credentials ────────────────────────────────────────────────
  //
  // AP mode (setup flow): test the credentials first — the softAP stays up
  // during the attempt, so a typo'd password gets an error message instead of
  // a restart into a broken config. Save + restart only on success.
  //
  // STA mode (switching networks): the socket can't survive the switch, so
  // keep the old save-acknowledge-restart flow; a bad password falls back to
  // AP mode after reboot.
  if (strcmp(type, "wifi_config") == 0) {
    const char* ssid = doc["ssid"]     | "";
    const char* pass = doc["password"] | "";

    JsonDocument resp;
    if (strlen(ssid) == 0) {
      resp["type"]   = "wifi_saved";
      resp["saved"]  = false;
      resp["reason"] = "no_ssid";
    } else if (isAPMode()) {
      if (staTestRunning()) {
        resp["type"]   = "wifi_saved";
        resp["saved"]  = false;
        resp["reason"] = "busy";
      } else {
        userWifiTestClientId = client->id();
        userWifiTestPending  = true;
        startStaTest(ssid, pass, false);
        resp["type"] = "wifi_testing";
        resp["ssid"] = ssid;
      }
    } else {
      bool saved = saveWifiCreds(ssid, pass);
      resp["type"]       = "wifi_saved";
      resp["saved"]      = saved;
      resp["restarting"] = saved;
      if (!saved) resp["reason"] = "fs_write_failed";
      if (saved) wsRestartAt = millis() + 1500;   // restart from loop, not here
    }
    String out;
    serializeJson(resp, out);
    client->text(out);
    return;
  }

  // ── Calibration: compute, apply and save coefficients from raw points ────
  //
  // The browser sends the three (temp, resistance) reference points; the
  // coefficients are computed HERE with calcSHCoeffs() and sanity-checked, so
  // a buggy or malicious client can never install NaN maths that bricks the
  // readings until recalibration.
  if (strcmp(type, "cal_save") == 0) {
    uint8_t probe = doc["probe"].as<uint8_t>();
    if (probe > 1) return;

    float t[3], r[3];
    bool ok = doc["points"].is<JsonArray>() && doc["points"].size() >= 3;
    for (uint8_t i = 0; ok && i < 3; i++) {
      if (!doc["points"][i]["temp_c"].is<float>() ||
          !doc["points"][i]["resistance"].is<float>()) { ok = false; break; }
      t[i] = doc["points"][i]["temp_c"].as<float>();
      r[i] = doc["points"][i]["resistance"].as<float>();
    }

    SHCoeffs c = { 0, 0, 0 };
    ok = ok && calcSHCoeffs(t[0], r[0], t[1], r[1], t[2], r[2], &c);

    // Round-trip sanity check: the middle reference point must convert back
    // to within ±3 °C of what the user entered.
    if (ok) {
      float L    = log(r[1]);
      float invT = c.A + c.B * L + c.C * L * L * L;
      ok = invT > 0.0f && fabsf((1.0f / invT - 273.15f) - t[1]) <= 3.0f;
    }

    bool saved = false;
    if (ok) {
      probeCoeffs[probe] = c;
      const char* ts = doc["timestamp"] | "unknown";
      saved = saveCalibration(probe, c, t[0], r[0], t[1], r[1], t[2], r[2], ts);
    }

    JsonDocument resp;
    resp["type"]  = "cal_saved";
    resp["probe"] = probe;
    resp["saved"] = saved;
    if (!ok) resp["reason"] = "bad_points";
    String out;
    serializeJson(resp, out);
    client->text(out);
    Serial.printf("Calibration %s for probe %d: A=%.4e B=%.4e C=%.4e\n",
                  saved ? "saved" : "FAILED", probe, c.A, c.B, c.C);
    return;
  }
}

// ── Deferred work (runs from loop(), not the async callback) ──────────────────
//
// SD reads and WiFi scans block for long enough to starve the TCP stack and
// trip the soft watchdog if run inside the WebSocket event handler. wsTick()
// below dispatches them from loop().

// Parse one line of a cook file into list metadata. Returns via out-params.
static void cookListParseLine(const String& line, JsonObject& cook,
                              uint32_t& lastElapsed, bool& stopped) {
  if      (line.startsWith("#name,"))          cook["name"]          = line.substring(6);
  else if (line.startsWith("#preset,"))        cook["preset"]        = line.substring(8);
  else if (line.startsWith("#meat_target_c,")) cook["meat_target_c"] = line.substring(15).toFloat();
  else if (line.startsWith("#grill_low_c,"))   cook["grill_low_c"]   = line.substring(13).toFloat();
  else if (line.startsWith("#grill_high_c,"))  cook["grill_high_c"]  = line.substring(14).toFloat();
  else if (line.startsWith("#started_at,"))    cook["started_at"]    = line.substring(12);
  else if (line.startsWith("#stopped_at,"))    { cook["stopped_at"]  = line.substring(12); stopped = true; }
  else if (line.length() > 0 && !line.startsWith("#") && !line.startsWith("elapsed")) {
    int comma = line.indexOf(',');
    if (comma > 0) lastElapsed = line.substring(0, comma).toInt();
  }
}

// Build the cook history list (metadata for every cook file) and send it.
//
// Large files are NOT read end-to-end: the header block lives in the first few
// hundred bytes and the footer/last-sample in the last few hundred, so we read
// one chunk from each end. A 12-hour cook (~1400 rows) costs two small reads
// instead of a full-file scan that stalls broadcasts.
static void sendCookList(AsyncWebSocketClient* client) {
  const size_t CHUNK = 768;

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
      bool     stopped     = false;
      size_t   size        = f.size();

      if (size <= 2 * CHUNK) {
        // Small file — scan everything
        while (f.available()) {
          String line = f.readStringUntil('\n');
          line.trim();
          cookListParseLine(line, cook, lastElapsed, stopped);
        }
      } else {
        // Head chunk: metadata header block ends at the CSV column row
        while (f.available() && f.position() < CHUNK) {
          String line = f.readStringUntil('\n');
          line.trim();
          if (line.startsWith("elapsed")) break;
          cookListParseLine(line, cook, lastElapsed, stopped);
        }
        // Tail chunk: last sample row and (if the cook finished) #stopped_at
        f.seek(size - CHUNK);
        f.readStringUntil('\n');  // discard the partial line at the seek point
        while (f.available()) {
          String line = f.readStringUntil('\n');
          line.trim();
          cookListParseLine(line, cook, lastElapsed, stopped);
        }
      }
      f.close();

      if (!stopped) cook["duration_s"] = lastElapsed;
      yield();   // keep WiFi + watchdog fed between files
    }
    dir.close();
  }

  String out;
  serializeJson(resp, out);
  client->text(out);
}

// Build and send the full (downsampled) time-series for one cook file.
// `file` has already been whitelist-validated by the caller.
static void sendCookData(AsyncWebSocketClient* client, const char* file) {
  String fullPath = String(BBQ_COOKS) + "/" + file;
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
    if ((dataRows & 0x3F) == 0) yield();
  }

  // 150 points is plenty at phone-chart resolution and keeps the JSON payload
  // well inside the ESP8266's heap headroom (this doc is built, serialised to
  // a String, then queued by the WS layer — three copies at peak).
  const uint32_t MAX_POINTS = 150;
  uint32_t stride = (dataRows + MAX_POINTS - 1) / MAX_POINTS;
  if (stride < 1) stride = 1;

  // Pass 2: parse rows, downsample, compute peaks + target-hit, capture notes
  f.seek(0);
  JsonDocument resp;
  resp["type"] = "cook_data";
  resp["file"] = file;
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
    if ((rowIdx & 0x3F) == 0) yield();
  }
  f.close();

  if (peakMeat  > -999) resp["peak_meat_c"]  = roundf(peakMeat  * 10) / 10.0f;
  if (peakGrill > -999) resp["peak_grill_c"] = roundf(peakGrill * 10) / 10.0f;
  if (targetHitS >= 0)  resp["target_hit_s"] = targetHitS;

  String out;
  serializeJson(resp, out);
  client->text(out);
  Serial.printf("Cook data sent: %s (%u rows → %u points)\n",
                file, dataRows, (unsigned)points.size());
}

// Run the WiFi scan (blocks ~2 s, but scanNetworks() yields internally so the
// watchdog and TCP stack stay serviced — safe from loop, never from callbacks).
static void sendWifiScan(AsyncWebSocketClient* client) {
  int n = WiFi.scanNetworks();
  JsonDocument resp;
  resp["type"] = "wifi_networks";
  JsonArray nets = resp["networks"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject net = nets.add<JsonObject>();
    net["ssid"] = WiFi.SSID(i);
    net["rssi"] = WiFi.RSSI(i);
  }
  WiFi.scanDelete();
  String out;
  serializeJson(resp, out);
  client->text(out);
}

// Called from loop(): services one deferred request, delivers WiFi-test
// results, and performs any scheduled restart.
void wsTick() {
  // 1. Deferred client request (cook list / cook data / wifi scan)
  if (pendingKind != PENDING_NONE) {
    uint8_t kind = pendingKind;
    pendingKind = PENDING_NONE;               // clear before the (slow) work

    AsyncWebSocketClient* client = ws.client(pendingClientId);
    if (client && client->status() == WS_CONNECTED) {
      if      (kind == PENDING_COOK_LIST) sendCookList(client);
      else if (kind == PENDING_COOK_DATA) sendCookData(client, pendingFile);
      else if (kind == PENDING_WIFI_SCAN) sendWifiScan(client);
    }
  }

  // 2. Result of a user-initiated WiFi credential test (AP-mode setup)
  if (userWifiTestPending) {
    StaTestResult r = staTestConsumeResult();
    if (r != STA_TEST_NONE) {
      userWifiTestPending = false;
      AsyncWebSocketClient* client = ws.client(userWifiTestClientId);

      JsonDocument resp;
      resp["type"] = "wifi_saved";
      if (r == STA_TEST_OK) {
        bool saved = saveWifiCreds(staTestedSsid(), staTestedPass());
        resp["saved"]      = saved;
        resp["restarting"] = saved;
        if (saved) resp["ip"] = WiFi.localIP().toString();
        else       resp["reason"] = "fs_write_failed";
        if (saved) wsRestartAt = millis() + 2000;
      } else {
        resp["saved"]  = false;
        resp["reason"] = "join_failed";
      }
      if (client && client->status() == WS_CONNECTED) {
        String out;
        serializeJson(resp, out);
        client->text(out);
      }
    }
  }

  // 3. Scheduled restart (gives the TCP stack time to flush the response)
  if (wsRestartAt != 0 && (int32_t)(millis() - wsRestartAt) >= 0) {
    ESP.restart();
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

  if (isAPMode()) doc["ap_mode"] = true;

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
        // Length-bounded parse — no NUL terminator is written past the buffer
        onWsMessage(client, (const char*)data, len);
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
// Pushed every 1s to all connected clients. Includes cook state when active,
// plus SD/heap health so the UI can warn when nothing is being recorded.

void wsBroadcast(float meatC, float grillC, String timestamp) {
  if (ws.count() == 0) return;

  // Back-pressure: if any client's TCP queue is congested (phone locked or
  // walked out of range), skip this tick instead of queueing frames into an
  // ever-shrinking heap until the device resets.
  if (!ws.availableForWriteAll()) return;

  JsonDocument doc;
  doc["ts"]          = timestamp;
  doc["meat"]["c"]   = meatC;
  doc["grill"]["c"]  = grillC;
  doc["meat_target"] = meatTarget;
  doc["grill_low"]   = grillLow;
  doc["grill_high"]  = grillHigh;
  doc["sd"]          = sdOk;
  doc["heap"]        = ESP.getFreeHeap();

  if (activeCook.active) {
    doc["cook_active"]    = true;
    doc["cook_name"]      = activeCook.name;
    doc["cook_elapsed_s"] = cookElapsedSeconds();
  } else {
    doc["cook_active"] = false;
  }

  if (isAPMode()) doc["ap_mode"] = true;
  else doc["rssi"] = WiFi.RSSI();

  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}
