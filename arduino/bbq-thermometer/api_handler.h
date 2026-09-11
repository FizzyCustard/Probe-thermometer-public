#pragma once

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "snapshot.h"

// ── Read-only JSON API ────────────────────────────────────────────────────────
//
// GET /api/status — the current device snapshot, for anything that would rather
// fetch a value than hold a WebSocket open (desktop widgets, scripts, browser
// dashboards). The body is byte-identical to the 1 s WebSocket tick because
// both come from buildSnapshot() in snapshot.h.
//
// SAFETY: this handler runs in the AsyncWebServer callback context, where the
// rule documented at the top of websocket_handler.h applies — no SD reads, no
// WiFi scans, nothing blocking, or the TCP stack stalls and the soft watchdog
// fires. Everything below is a RAM read. Any future endpoint that needs cook
// files off the SD card must NOT follow this pattern; it needs the deferred
// queue that wsTick() services from loop(), or a chunked response.
//
// Note on probe values: a disconnected probe is NAN, which ArduinoJson emits as
// JSON null (ARDUINOJSON_ENABLE_NAN is 0 by default). Never enable that flag —
// it emits a bare NaN token, which is invalid JSON and would break every
// external parser, on this endpoint and on the WebSocket alike.

// Current readings, sampled once per second by loop() — defined in the .ino
extern float curMeat;
extern float curGrill;
extern float curMeatR;
extern float curGrillR;

void apiBegin(AsyncWebServer* server) {
  server->on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    buildSnapshot(doc, curMeat, curGrill, curMeatR, curGrillR, getTimestamp());

    String out;
    serializeJson(doc, out);

    AsyncWebServerResponse* res = req->beginResponse(200, "application/json", out);
    // Set per-response rather than via DefaultHeaders, which would also alter
    // every static-file and 404 response.
    res->addHeader("Access-Control-Allow-Origin", "*");
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });
}
