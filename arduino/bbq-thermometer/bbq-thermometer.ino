#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

#include "config.h"

// Fallbacks so an older config.h (pre-dating these options) still compiles
#ifndef MDNS_HOSTNAME
#define MDNS_HOSTNAME "bbq"
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "bbq-update"
#endif
#ifndef DEBUG_READINGS
#define DEBUG_READINGS 0
#endif

#include "wifi_manager.h"
#include "thermistor.h"
#include "logger.h"
#include "cook_session.h"
#include "websocket_handler.h"
#include "api_handler.h"

// ── Runtime settings — overridable via browser WebSocket ────────────────────
float meatTarget = DEFAULT_MEAT_TARGET;
float grillLow   = DEFAULT_GRILL_LOW;
float grillHigh  = DEFAULT_GRILL_HIGH;

// ── Preset / favourite storage (loaded from SD on boot) ──────────────────────
Preset    presets[MAX_PRESETS];
uint8_t   presetCount = 0;
Favourite favs[MAX_FAVS];
uint8_t   favCount = 0;

// ── Timing ───────────────────────────────────────────────────────────────────
unsigned long lastBroadcast = 0;
unsigned long lastLog       = 0;
unsigned long lastCookSample = 0;

// ── Current readings (sampled once per second, reused everywhere) ────────────
float   curMeat   = NAN;
float   curGrill  = NAN;
float   curMeatR  = NAN;   // median-filtered resistance behind curMeat (Ω)
float   curGrillR = NAN;
uint8_t meatBadCount  = 0;
uint8_t grillBadCount = 0;

// ── Core objects ─────────────────────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket  ws("/ws");
Adafruit_ADS1115 ads;
bool      adsReady = false;   // true once ads.begin() succeeds; gates all reads
WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, NTP_UTC_OFFSET_SEC, 60000);

// ── Timestamp helpers ─────────────────────────────────────────────────────────

bool ntpReady() {
  return timeClient.getEpochTime() > 1000000000UL;
}

String getTimestamp() {
  time_t t = timeClient.getEpochTime();
  struct tm* tm = gmtime(&t);
  char buf[72];   // sized for the compiler's worst-case %d expansion
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec);
  return String(buf);
}

String getDateStr() {
  time_t t = timeClient.getEpochTime();
  struct tm* tm = gmtime(&t);
  char buf[40];   // sized for the compiler's worst-case %d expansion
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
  return String(buf);
}

// ── Probe sampling ────────────────────────────────────────────────────────────
//
// Median-of-3 rejects single-sample glitches; the debounce below stops one bad
// read from blanking the display — only 3 consecutive bad samples (~3 s)
// declare a probe disconnected.

//
// `rOut` receives the resistance behind the returned temperature (NAN when
// disconnected, previous value while debouncing) so the tick can carry both.

static float sampleProbe(uint8_t channel, bool enabled, uint8_t& badCount,
                         float prev, float& rOut) {
  if (!enabled) { rOut = NAN; return NAN; }
  float r = readResistanceMedian(channel);
  if (r > PROBE_R_MIN && r < PROBE_R_MAX) {
    badCount = 0;
    rOut = r;
    return resistanceToCelsius(r, channel);
  }
  if (badCount < 3) badCount++;
  if (badCount >= 3) { rOut = NAN; return NAN; }
  return prev;   // rOut untouched: keeps the previous resistance while debouncing
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nBBQ Monitor booting...");

  // Web UI files from onboard flash
  if (!LittleFS.begin()) {
    Serial.println(F("WARNING: LittleFS mount failed — web UI will not be served"));
    Serial.println(F("  Upload device/data/ files using the LittleFS upload tool first"));
  } else {
    Serial.println(F("LittleFS mounted OK"));
  }

  // WiFi — try saved credentials, fall back to compiled-in, then AP mode
  WifiCreds creds;
  bool connected = false;
  if (loadWifiCreds(creds)) {
    connected = connectWifi(creds.ssid, creds.password);
  }
  if (!connected && strcmp(WIFI_SSID, "YOUR_SSID") != 0) {
    connected = connectWifi(WIFI_SSID, WIFI_PASSWORD);
    if (connected) saveWifiCreds(WIFI_SSID, WIFI_PASSWORD);  // persist for next boot
  }
  if (!connected) {
    startAP();
  }

  // NTP time sync (skipped in AP mode — no internet)
  if (!isAPMode()) {
    timeClient.begin();
    uint32_t ntpStart = millis();
    while (timeClient.getEpochTime() < 1000000000UL && millis() - ntpStart < 5000) {
      timeClient.update();
      delay(200);
    }
    Serial.printf("NTP: %s\n", getTimestamp().c_str());
  }

  // OTA + mDNS — ArduinoOTA.begin() starts the MDNS responder with our
  // hostname; we then advertise HTTP so the dashboard is at http://bbq.local
  ArduinoOTA.setHostname(MDNS_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.printf("mDNS: http://%s.local  (OTA enabled)\n", MDNS_HOSTNAME);

  // ADS1115 over I2C
  Wire.begin();
  adsReady = ads.begin(ADS1115_I2C_ADDRESS);
  if (!adsReady) {
    Serial.println(F("ERROR: ADS1115 not found — probe readings disabled"));
    Serial.println(F("  Check: SDA→D2, SCL→D1, VDD→3.3V, ADDR→GND"));
  } else {
    ads.setGain(GAIN_ONE);  // ±4.096 V full scale
    Serial.println(F("ADS1115 ready (GAIN_ONE)"));
  }

  // SD card: create folder tree, then load all persisted config
  if (loggerBegin()) {
    loadCalibration();
    loadSettings(meatTarget, grillLow, grillHigh);

    presetCount = loadPresets(presets, MAX_PRESETS);
    if (presetCount == 0) seedDefaultPresets(presets, presetCount);

    favCount = loadFavourites(favs, MAX_FAVS);

    // Pick up a cook that was running when power was lost
    cookTryResume();
  }

  // WebSocket handler at /ws
  wsBegin(&server);

  // Read-only JSON snapshot for external apps. Registered BEFORE the
  // serveStatic("/") below, which matches the "/" prefix.
  apiBegin(&server);

  // The WiFi credential store lives in the same LittleFS the UI is served
  // from — explicitly refuse to serve it, or anyone on the network could
  // download the WiFi password in plaintext.
  server.on(WIFI_CREDS_PATH, HTTP_ANY, [](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "Not found");
  });

  // Serve web UI from LittleFS for every other route
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "Not found");
  });

  server.begin();
  if (isAPMode()) {
    Serial.printf("\nReady — connect to WiFi \"%s\" then open http://%s\n",
                  AP_SSID, WiFi.softAPIP().toString().c_str());
  } else {
    Serial.printf("\nReady — open http://%s or http://%s.local\n",
                  WiFi.localIP().toString().c_str(), MDNS_HOSTNAME);
  }
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
  wifiManagerTick();
  wsTick();        // deferred SD/scan work, WiFi-test results, scheduled restart
  loggerTick();    // SD remount attempts after a failure
  ArduinoOTA.handle();
  if (!isAPMode()) {
    timeClient.update();
    MDNS.update();
  }

  unsigned long now = millis();

  // Sample both probes once per second and reuse the values for the
  // broadcast, SD log, and cook samples. (Each ADS1115 read blocks ~8 ms, so
  // reading on every loop pass would burn ~95% of loop time on readings that
  // get thrown away.)
  if (now - lastBroadcast >= BROADCAST_INTERVAL_MS) {
    lastBroadcast = now;
    ws.cleanupClients(4);

    curMeat  = sampleProbe(ADS1115_CH_MEAT,  PROBE0_ENABLED, meatBadCount,  curMeat,  curMeatR);
    curGrill = sampleProbe(ADS1115_CH_GRILL, PROBE1_ENABLED, grillBadCount, curGrill, curGrillR);

#if DEBUG_READINGS
    Serial.printf("[MEAT]  T=%.1f°C %.0fΩ   [GRILL] T=%.1f°C %.0fΩ   heap=%u\n",
                  curMeat, curMeatR, curGrill, curGrillR, ESP.getFreeHeap());
#endif

    wsBroadcast(curMeat, curGrill, curMeatR, curGrillR, getTimestamp());
  }

  // Append CSV row to SD card every LOG_INTERVAL_MS (requires valid NTP timestamp)
  if (!isAPMode() && ntpReady() && now - lastLog >= LOG_INTERVAL_MS) {
    lastLog = now;
    logReading(getTodayFilename(getDateStr()), getTimestamp(), curMeat, curGrill);
  }

  // Append a cook sample every 30 seconds while a cook is active
  if (activeCook.active && now - lastCookSample >= 30000UL) {
    lastCookSample = now;
    cookAppendReading(curMeat, curGrill);
  }
}
