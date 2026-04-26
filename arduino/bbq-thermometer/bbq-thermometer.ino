#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "thermistor.h"
#include "logger.h"
#include "websocket_handler.h"

// ── WiFi credentials ─────────────────────────────────────────────────────────
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

// ── Target settings (updated via WebSocket from browser) ─────────────────────
float meatTarget = 74.0;
float grillLow   = 160.0;
float grillHigh  = 200.0;

// ── Timing ───────────────────────────────────────────────────────────────────
unsigned long lastBroadcast = 0;
unsigned long lastLog       = 0;
const unsigned long BROADCAST_INTERVAL = 1000;  // ms
const unsigned long LOG_INTERVAL       = 5000;  // ms

// ── Objects ──────────────────────────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Adafruit_ADS1115 ads;
WiFiUdp ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\nBBQ Monitor booting...");

  // TODO: mount LittleFS
  // TODO: connect WiFi
  // TODO: sync NTP
  // TODO: init ADS1115
  // TODO: init SD logger
  // TODO: register WebSocket handler
  // TODO: register static file handler
  // TODO: start server
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  ws.cleanupClients();

  unsigned long now = millis();

  if (now - lastBroadcast >= BROADCAST_INTERVAL) {
    lastBroadcast = now;
    // TODO: read probes
    // TODO: broadcast WebSocket JSON
  }

  if (now - lastLog >= LOG_INTERVAL) {
    lastLog = now;
    // TODO: log to SD card
  }
}
