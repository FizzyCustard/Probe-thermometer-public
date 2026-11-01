#pragma once

#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include "config.h"

#define WIFI_CREDS_PATH       "/wifi.json"
#define AP_SSID               "BBQ-Thermometer"
#define WIFI_CONNECT_TIMEOUT  15000UL
#define STA_TEST_TIMEOUT      20000UL
#define STA_RETRY_INTERVAL    60000UL   // background retry of saved creds in AP mode

static DNSServer dnsServer;
static bool      apMode = false;

struct WifiCreds {
  char ssid[64];
  char password[64];
};

// Saved-credential cache so AP mode can retry them without re-reading flash.
static WifiCreds savedCreds     = { "", "" };
static bool      haveSavedCreds = false;

// ── Non-blocking STA connection test ─────────────────────────────────────────
//
// Used for two things:
//   1. Background: while stuck in AP mode, periodically retry the saved
//      credentials (router may have been rebooting when we started up).
//   2. User: when the browser submits new WiFi credentials in AP mode, try
//      them BEFORE saving, so a typo'd password gets an error instead of a
//      restart into a broken config. The softAP stays up during the test
//      (WIFI_AP_STA), so the WebSocket survives to deliver the result.

enum StaTestResult : uint8_t { STA_TEST_NONE = 0, STA_TEST_OK, STA_TEST_FAIL };

static bool          staTestActive     = false;
static bool          staTestBackground = false;
static uint32_t      staTestStart      = 0;
static StaTestResult staTestResult     = STA_TEST_NONE;
static char          staTestSsid[64]   = "";
static char          staTestPass[64]   = "";
static uint32_t      lastStaRetry      = 0;

// Begin a connection attempt without dropping the softAP. Returns false if a
// test is already running.
bool startStaTest(const char* ssid, const char* pass, bool background) {
  if (staTestActive) return false;
  strlcpy(staTestSsid, ssid, sizeof(staTestSsid));
  strlcpy(staTestPass, pass, sizeof(staTestPass));
  staTestBackground = background;
  staTestResult     = STA_TEST_NONE;
  staTestStart      = millis();
  staTestActive     = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(staTestSsid, staTestPass);
  Serial.printf("STA test started (%s)%s\n", staTestSsid,
                background ? " [background retry]" : "");
  return true;
}

bool staTestRunning() { return staTestActive; }

// Returns the finished test's result once, then clears it.
StaTestResult staTestConsumeResult() {
  StaTestResult r = staTestResult;
  staTestResult = STA_TEST_NONE;
  return r;
}

const char* staTestedSsid() { return staTestSsid; }
const char* staTestedPass() { return staTestPass; }

// ── Credential persistence ────────────────────────────────────────────────────

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
  if (strlen(out.ssid) == 0) return false;
  savedCreds     = out;
  haveSavedCreds = true;
  return true;
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
  strlcpy(savedCreds.ssid,     ssid, sizeof(savedCreds.ssid));
  strlcpy(savedCreds.password, pass, sizeof(savedCreds.password));
  haveSavedCreds = true;
  Serial.printf("WiFi credentials saved for: %s\n", ssid);
  return true;
}

// ── Connection ────────────────────────────────────────────────────────────────

// Try to connect to the given SSID with a 15 s timeout. Returns true on success.
bool connectWifi(const char* ssid, const char* pass) {
  WiFi.persistent(false);   // we manage credentials ourselves — no SDK flash writes
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
  WiFi.setAutoReconnect(true);
  Serial.printf("\nConnected — IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

// Start a WPA2 SoftAP and DNS captive portal. Any domain resolves to the AP IP.
void startAP() {
  apMode = true;
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(53, "*", apIP);
  Serial.printf("AP mode — SSID: %s  IP: %s\n", AP_SSID, apIP.toString().c_str());
  Serial.println("Connect your phone to BBQ-Thermometer to configure WiFi.");
}

// Call once per loop(). Services the captive-portal DNS, drives any running
// STA test, and periodically retries saved credentials while stuck in AP mode.
void wifiManagerTick() {
  if (apMode) dnsServer.processNextRequest();

  // Drive an in-flight STA test to completion
  if (staTestActive) {
    if (WiFi.status() == WL_CONNECTED) {
      staTestActive = false;
      staTestResult = STA_TEST_OK;
      Serial.printf("STA test OK — IP: %s\n", WiFi.localIP().toString().c_str());
      if (staTestBackground) {
        // Saved network is back — restart for a clean boot into STA mode
        Serial.println("Saved network reachable again — restarting");
        delay(100);
        ESP.restart();
      }
    } else if (millis() - staTestStart > STA_TEST_TIMEOUT) {
      staTestActive = false;
      staTestResult = STA_TEST_FAIL;
      WiFi.disconnect();
      WiFi.enableSTA(false);   // back to pure AP
      Serial.println("STA test failed (timeout)");
    }
    return;
  }

  // Background retry of saved credentials while in AP mode
  if (apMode && haveSavedCreds && millis() - lastStaRetry >= STA_RETRY_INTERVAL) {
    lastStaRetry = millis();
    startStaTest(savedCreds.ssid, savedCreds.password, true);
  }
}

bool isAPMode() { return apMode; }
