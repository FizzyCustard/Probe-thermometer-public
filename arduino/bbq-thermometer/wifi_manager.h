#pragma once

#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

#define WIFI_CREDS_PATH       "/wifi.json"
#define AP_SSID               "BBQ-Thermometer"
#define WIFI_CONNECT_TIMEOUT  15000UL

static DNSServer dnsServer;
static bool      apMode = false;

struct WifiCreds {
  char ssid[64];
  char password[64];
};

// Load credentials from LittleFS /wifi.json. Returns false if file missing or malformed.
bool loadWifiCreds(WifiCreds& out) {
  File f = LittleFS.open(WIFI_CREDS_PATH, "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err != DeserializationError::Ok) return false;
  strlcpy(out.ssid,     doc["ssid"]     | "", sizeof(out.ssid));
  strlcpy(out.password, doc["password"] | "", sizeof(out.password));
  return strlen(out.ssid) > 0;
}

// Save credentials to LittleFS /wifi.json. Returns false on write failure.
bool saveWifiCreds(const char* ssid, const char* pass) {
  JsonDocument doc;
  doc["ssid"]     = ssid;
  doc["password"] = pass;
  File f = LittleFS.open(WIFI_CREDS_PATH, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  Serial.printf("WiFi credentials saved for: %s\n", ssid);
  return true;
}

// Try to connect to the given SSID with a 15 s timeout. Returns true on success.
bool connectWifi(const char* ssid, const char* pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.printf("Connecting to WiFi (%s)", ssid);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_CONNECT_TIMEOUT) {
      Serial.println("\nWiFi connection timed out");
      WiFi.disconnect();
      return false;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected — IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

// Start a SoftAP and DNS captive portal. Any domain resolves to the AP IP.
void startAP() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(53, "*", apIP);
  Serial.printf("AP mode — SSID: %s  IP: %s\n", AP_SSID, apIP.toString().c_str());
  Serial.println("Connect your phone to BBQ-Thermometer to configure WiFi.");
}

// Call once per loop() to service DNS requests in AP mode.
void wifiManagerTick() {
  if (apMode) dnsServer.processNextRequest();
}

bool isAPMode() { return apMode; }
