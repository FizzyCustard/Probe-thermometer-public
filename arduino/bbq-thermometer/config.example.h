#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// config.h — copy this file to config.h and fill in your values before
// flashing. config.h is gitignored so credentials are never committed.
// ─────────────────────────────────────────────────────────────────────────────

// ── WiFi ─────────────────────────────────────────────────────────────────────

#define WIFI_SSID       "YOUR_SSID"
#define WIFI_PASSWORD   "YOUR_PASSWORD"

// ── NTP ──────────────────────────────────────────────────────────────────────

#define NTP_SERVER          "pool.ntp.org"
#define NTP_UTC_OFFSET_SEC  0       // e.g. 3600 = GMT+1, 0 = UTC

// ── Target temperatures (°C) — overridable via web UI at runtime ─────────────

#define DEFAULT_MEAT_TARGET   74.0f
#define DEFAULT_GRILL_LOW    160.0f
#define DEFAULT_GRILL_HIGH   200.0f

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

// Probe 1 — Meat
#define PROBE1_SH_A   4.475e-4f
#define PROBE1_SH_B   2.523e-4f
#define PROBE1_SH_C   1.701e-9f

// Probe 2 — Grill
#define PROBE2_SH_A   4.475e-4f
#define PROBE2_SH_B   2.523e-4f
#define PROBE2_SH_C   1.701e-9f

// ── Timing ────────────────────────────────────────────────────────────────────

#define BROADCAST_INTERVAL_MS   1000   // WebSocket push to browser (ms)
#define LOG_INTERVAL_MS         5000   // SD card CSV write interval (ms)
