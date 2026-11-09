#pragma once

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include "logger.h"
#include "cook_session.h"
#include "wifi_manager.h"

// ── Device snapshot ───────────────────────────────────────────────────────────
//
// The single definition of "current device state". Both transports use it:
// the 1 s WebSocket tick (wsBroadcast, websocket_handler.h) and the REST
// endpoint (GET /api/status, api_handler.h).
//
// Keeping one builder is deliberate. An earlier bug came from two
// independent sources of truth for the same value — the tick reported the
// persisted globals while the cook record held its own targets — so a second
// payload builder for REST would recreate exactly that hazard.

// Runtime settings, defined in bbq-thermometer.ino
extern float meatTarget;
extern float grillLow;
extern float grillHigh;

// Round to 1 dp. roundf(NAN) is NAN, so a disconnected probe still serialises
// as null rather than a bogus number — app.js relies on that (`!= null`).
// Same idiom already used for cook points and peaks in websocket_handler.h.
static inline float snap1dp(float v) { return roundf(v * 10.0f) / 10.0f; }

// Whole ohms. Same NAN → null behaviour as snap1dp; ArduinoJson prints an
// integral float without a decimal point, so 65521.0f serialises as 65521.
static inline float snapOhms(float r) { return roundf(r); }

// meatR / grillR: the median-filtered probe resistance behind each reading.
// The calibration wizard in app.js watches these live to show settling and to
// average a capture, so no separate "read resistance" request is needed.
static void buildSnapshot(JsonDocument& doc, float meatC, float grillC,
                          float meatR, float grillR,
                          const String& timestamp) {
  // A cook carries its own targets, which may differ from the persisted
  // globals. Report the target *in force right now* so external monitors
  // don't alarm against a stale global; the cook's values are also echoed
  // explicitly below for clients that want to tell the two apart.
  const bool ck = activeCook.active;

  doc["ts"]          = timestamp;
  doc["meat"]["c"]   = snap1dp(meatC);
  doc["meat"]["r"]   = snapOhms(meatR);
  doc["grill"]["c"]  = snap1dp(grillC);
  doc["grill"]["r"]  = snapOhms(grillR);
  doc["meat_target"] = ck ? activeCook.meatTargetC : meatTarget;
  doc["grill_low"]   = ck ? activeCook.grillLowC   : grillLow;
  doc["grill_high"]  = ck ? activeCook.grillHighC  : grillHigh;
  doc["sd"]          = sdOk;
  doc["heap"]        = ESP.getFreeHeap();

  if (ck) {
    doc["cook_active"]      = true;
    doc["cook_name"]        = activeCook.name;
    doc["cook_elapsed_s"]   = cookElapsedSeconds();
    doc["cook_meat_target"] = activeCook.meatTargetC;
    doc["cook_grill_low"]   = activeCook.grillLowC;
    doc["cook_grill_high"]  = activeCook.grillHighC;
  } else {
    doc["cook_active"] = false;
  }

  if (isAPMode()) doc["ap_mode"] = true;
  else            doc["rssi"]    = WiFi.RSSI();
}
