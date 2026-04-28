#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

#include "config.h"
#include "thermistor.h"
#include "logger.h"
#include "websocket_handler.h"

// ── Runtime settings — overridable via browser WebSocket ────────────────────
float meatTarget = DEFAULT_MEAT_TARGET;
float grillLow   = DEFAULT_GRILL_LOW;
float grillHigh  = DEFAULT_GRILL_HIGH;

// ── Timing ───────────────────────────────────────────────────────────────────
unsigned long lastBroadcast = 0;
unsigned long lastLog       = 0;

// ── Core objects ─────────────────────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket  ws("/ws");
Adafruit_ADS1115 ads;
WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, NTP_UTC_OFFSET_SEC, 60000);

// ── Timestamp helpers ─────────────────────────────────────────────────────────

String getTimestamp() {
  time_t t = timeClient.getEpochTime();
  struct tm* tm = gmtime(&t);
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec);
  return String(buf);
}

String getDateStr() {
  time_t t = timeClient.getEpochTime();
  struct tm* tm = gmtime(&t);
  char buf[11];
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

  // WiFi — blocks until connected
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to WiFi (%s)", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected — IP: %s\n", WiFi.localIP().toString().c_str());

  // NTP time sync
  timeClient.begin();
  timeClient.forceUpdate();
  Serial.printf("NTP synced: %s\n", getTimestamp().c_str());

  // ADS1115 over I2C
  Wire.begin();
  if (!ads.begin(ADS1115_I2C_ADDRESS)) {
    Serial.println("ERROR: ADS1115 not found");
    Serial.println("  Check: SDA→D2, SCL→D1, VDD→3.3V, ADDR→GND");
  } else {
    ads.setGain(GAIN_ONE);  // ±4.096 V full scale
    Serial.println("ADS1115 ready (GAIN_ONE)");
  }

  // SD card + load saved calibration
  if (loggerBegin()) {
    loadCalibration();
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

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
  ws.cleanupClients();
  timeClient.update();

  unsigned long now = millis();

  // Read each channel once and reuse for debug, broadcast, and logging
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

  // Append CSV row to SD card
  if (now - lastLog >= LOG_INTERVAL_MS) {
    lastLog = now;
    logReading(getTodayFilename(getDateStr()),
               getTimestamp(), tMeat, tGrill);
  }
}
