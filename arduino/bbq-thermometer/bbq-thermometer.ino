#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

#define MAX_PRESETS   16
#define MAX_FAVS      32

// WiFi connection behaviour: bound the boot-time wait, then continue offline and
// keep retrying in the background so the device still reads probes without WiFi.
#define WIFI_CONNECT_TIMEOUT_MS  20000UL
#define WIFI_RETRY_INTERVAL_MS   10000UL

#include "config.h"
#include "thermistor.h"
#include "logger.h"
#include "cook_session.h"
#include "websocket_handler.h"

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

// ── Core objects ─────────────────────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket  ws("/ws");
Adafruit_ADS1115 ads;
bool      adsReady = false;   // true once ads.begin() succeeds; gates all reads
WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, NTP_UTC_OFFSET_SEC, 60000);

// ── Timestamp helpers ─────────────────────────────────────────────────────────

String getTimestamp() {
  time_t t = timeClient.getEpochTime();
  struct tm* tm = gmtime(&t);
  char buf[21];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec);
  return String(buf);
}

String getDateStr() {
  time_t t = timeClient.getEpochTime();
  struct tm* tm = gmtime(&t);
  char buf[12];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
  return String(buf);
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nBBQ Monitor booting...");

  // Web UI files from onboard flash
  if (!LittleFS.begin()) {
    Serial.println("WARNING: LittleFS mount failed — web UI will not be served");
    Serial.println("  Upload data/ files using the LittleFS upload tool first");
  } else {
    Serial.println("LittleFS mounted OK");
  }

  // WiFi — wait up to WIFI_CONNECT_TIMEOUT_MS, then continue regardless so the
  // device still reads probes and logs to SD when WiFi is unavailable.
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to WiFi (%s)", WIFI_SSID);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - wifiStart < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }

  // NTP time sync (only meaningful once WiFi is up; maintainWiFi() re-syncs later)
  timeClient.begin();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConnected — IP: %s\n", WiFi.localIP().toString().c_str());
    timeClient.forceUpdate();
    Serial.printf("NTP synced: %s\n", getTimestamp().c_str());
  } else {
    Serial.println("\nWiFi connect timed out — continuing offline, will keep retrying");
  }

  // ADS1115 over I2C
  Wire.begin();
  adsReady = ads.begin(ADS1115_I2C_ADDRESS);
  if (!adsReady) {
    Serial.println("ERROR: ADS1115 not found — probe readings disabled");
    Serial.println("  Check: SDA→D2, SCL→D1, VDD→3.3V, ADDR→GND");
  } else {
    ads.setGain(GAIN_ONE);  // ±4.096 V full scale
    Serial.println("ADS1115 ready (GAIN_ONE)");
  }

  // SD card: create folder tree, then load all persisted config
  if (loggerBegin()) {
    loadCalibration();
    loadSettings(meatTarget, grillLow, grillHigh);

    presetCount = loadPresets(presets, MAX_PRESETS);
    if (presetCount == 0) seedDefaultPresets(presets, presetCount);

    favCount = loadFavourites(favs, MAX_FAVS);
  }

  // WebSocket handler at /ws
  wsBegin(&server);

  // Serve web UI from LittleFS for every other route
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.printf("\nReady — open http://%s in your browser\n",
                WiFi.localIP().toString().c_str());
}

// ── WiFi maintenance ────────────────────────────────────────────────────────
//
// Called every loop; acts at most once per WIFI_RETRY_INTERVAL_MS. Reconnects
// when the link is down and re-syncs NTP on the transition back to connected.

void maintainWiFi() {
  static unsigned long lastCheck = 0;
  static bool wasConnected = false;
  unsigned long now = millis();
  if (now - lastCheck < WIFI_RETRY_INTERVAL_MS) return;
  lastCheck = now;

  bool connected = (WiFi.status() == WL_CONNECTED);
  if (!connected) {
    Serial.println("WiFi down — reconnecting...");
    WiFi.reconnect();
  } else if (!wasConnected) {
    Serial.printf("WiFi reconnected — IP: %s\n", WiFi.localIP().toString().c_str());
    timeClient.forceUpdate();  // re-sync NTP after the outage
  }
  wasConnected = connected;
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
  ws.cleanupClients();
  wsProcessPending();   // run deferred (heavy) cook-history reads off the async path
  maintainWiFi();
  timeClient.update();

  unsigned long now = millis();

  // Read each channel once and reuse for broadcast, logging, and cook samples
  float rMeat  = PROBE0_ENABLED ? readResistance(ADS1115_CH_MEAT)  : -1.0f;
  float rGrill = PROBE1_ENABLED ? readResistance(ADS1115_CH_GRILL) : -1.0f;
  float tMeat  = (rMeat  > 500.0f && rMeat  < 2000000.0f) ? resistanceToCelsius(rMeat,  ADS1115_CH_MEAT)  : NAN;
  float tGrill = (rGrill > 500.0f && rGrill < 2000000.0f) ? resistanceToCelsius(rGrill, ADS1115_CH_GRILL) : NAN;

  // Push live readings to all connected browsers
  if (now - lastBroadcast >= BROADCAST_INTERVAL_MS) {
    lastBroadcast = now;
    Serial.printf("[MEAT]  R=%10.0f Ω  T=%.1f°C\n", rMeat,  tMeat);
    Serial.printf("[GRILL] R=%10.0f Ω  T=%.1f°C\n", rGrill, tGrill);
    wsBroadcast(tMeat, tGrill, getTimestamp());
  }

  // Append CSV row to SD card every LOG_INTERVAL_MS
  if (now - lastLog >= LOG_INTERVAL_MS) {
    lastLog = now;
    logReading(getTodayFilename(getDateStr()), getTimestamp(), tMeat, tGrill);
  }

  // Append a cook sample every 30 seconds while a cook is active
  if (activeCook.active && now - lastCookSample >= 30000UL) {
    lastCookSample = now;
    cookAppendReading(tMeat, tGrill);
  }
}
