#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// config.h — copy this file to config.h and fill in your values before
// flashing. config.h is gitignored so credentials are never committed.
// ─────────────────────────────────────────────────────────────────────────────

// ── WiFi ─────────────────────────────────────────────────────────────────────

#define WIFI_SSID       "YOUR_SSID"
#define WIFI_PASSWORD   "YOUR_PASSWORD"

// WPA2 password for the device's own setup access point (min 8 chars).
#define AP_PASSWORD     "bbq-setup"

// ── OTA / mDNS ───────────────────────────────────────────────────────────────

#define MDNS_HOSTNAME   "bbq"          // dashboard reachable at http://bbq.local
#define OTA_PASSWORD    "bbq-update"   // change this before flashing

// ── NTP ──────────────────────────────────────────────────────────────────────
//
// Note: this is a fixed offset — DST changes are NOT tracked, so timestamps
// shift by an hour across a DST boundary. Set 0 to log in UTC.

#define NTP_SERVER          "pool.ntp.org"
#define NTP_UTC_OFFSET_SEC  0       // e.g. 3600 = GMT+1, 0 = UTC

// ── Target temperatures (°C) — overridable via web UI at runtime ─────────────

#define DEFAULT_MEAT_TARGET   74.0f
#define DEFAULT_GRILL_LOW    160.0f
#define DEFAULT_GRILL_HIGH   200.0f

// ── Probe enable flags ────────────────────────────────────────────────────────

#define PROBE0_ENABLED  1   // 1 = meat probe active, 0 = disabled
#define PROBE1_ENABLED  1   // 1 = grill probe active, 0 = disabled

// ── Hardware pins ─────────────────────────────────────────────────────────────

#define SD_CS_PIN             D8

#define ADS1115_I2C_ADDRESS   0x48  // ADDR pin tied to GND
#define ADS1115_CH_MEAT        0    // ADS1115 A0 = meat probe
#define ADS1115_CH_GRILL       1    // ADS1115 A1 = grill probe

// ── Voltage divider ───────────────────────────────────────────────────────────

#define SERIES_RESISTOR       100000.0f   // Ω — the fixed resistor in the divider
#define SUPPLY_VOLTAGE        3.3f        // V — D1 Mini logic supply

// ── Steinhart-Hart coefficients ───────────────────────────────────────────────
//
// The full Steinhart-Hart equation:
//   1/T(K) = A + B·ln(R) + C·(ln(R))³
//
// Default values below are estimates for a 100kΩ NTC B3950 probe (Morpilot).
// Run the on-device calibration tool and save to SD card for better accuracy.
// The firmware loads saved coefficients from SD on boot and uses these only
// as a fallback.
//
// Probe indices match the enable flags above: probe 0 = meat, probe 1 = grill.

// Probe 0 — Meat
#define PROBE0_SH_A   4.475e-4f
#define PROBE0_SH_B   2.523e-4f
#define PROBE0_SH_C   1.701e-9f

// Probe 1 — Grill
#define PROBE1_SH_A   4.475e-4f
#define PROBE1_SH_B   2.523e-4f
#define PROBE1_SH_C   1.701e-9f

// ── Timing ────────────────────────────────────────────────────────────────────

#define BROADCAST_INTERVAL_MS   1000   // WebSocket push to browser (ms)
#define LOG_INTERVAL_MS         5000   // SD card CSV write interval (ms)

// ── Debug ─────────────────────────────────────────────────────────────────────

#define DEBUG_READINGS  0   // 1 = print probe readings to Serial every second
