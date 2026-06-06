#pragma once

#include <Adafruit_ADS1X15.h>
#include <math.h>
#include "config.h"

extern Adafruit_ADS1115 ads;

// ── Steinhart-Hart coefficients ───────────────────────────────────────────────

struct SHCoeffs {
  float A, B, C;
};

// Loaded from config.h defaults; overwritten when calibration file is loaded
// from SD card on boot, or when the browser sends new calibration data.
SHCoeffs probeCoeffs[2] = {
  { PROBE1_SH_A, PROBE1_SH_B, PROBE1_SH_C },
  { PROBE2_SH_A, PROBE2_SH_B, PROBE2_SH_C },
};

// ── ADC → resistance ──────────────────────────────────────────────────────────
//
// ADS1115 voltage divider topology:
//
//   3.3V ──── SERIES_RESISTOR ──── ADS1115 channel ──── NTC probe ──── GND
//
// At GAIN_ONE the ADS1115 measures ±4.096V (full scale = 32767 counts).
// We scale by the actual 3.3V supply.

float readResistance(uint8_t channel) {
  int16_t raw = ads.readADC_SingleEnded(channel);
  if (raw <= 0) return -1.0f;                         // open circuit / no probe
  float voltage = (raw / 32767.0f) * 4.096f;
  if (voltage >= SUPPLY_VOLTAGE) return -1.0f;        // short circuit
  // Rearranged voltage divider: R_ntc = R_series * V / (Vsupply - V)
  return SERIES_RESISTOR * voltage / (SUPPLY_VOLTAGE - voltage);
}

// ── Resistance → temperature (full Steinhart-Hart) ───────────────────────────
//
//   1/T(K) = A + B·ln(R) + C·(ln(R))³

float resistanceToCelsius(float resistance, uint8_t probeIndex) {
  if (resistance <= 0.0f) return -999.0f;
  float L = log(resistance);
  const SHCoeffs& c = probeCoeffs[probeIndex];
  float invT = c.A + c.B * L + c.C * L * L * L;
  if (invT <= 0.0f) return NAN;
  return (1.0f / invT) - 273.15f;
}

// Convenience: read a channel and return °C directly.
// Returns NAN (serialises as JSON null) when disabled or no probe detected.
float readCelsius(uint8_t channel) {
  if (channel == 0 && !PROBE0_ENABLED) return NAN;
  if (channel == 1 && !PROBE1_ENABLED) return NAN;
  float r = readResistance(channel);
  if (r < 500.0f || r > 2000000.0f) return NAN;
  return resistanceToCelsius(r, channel);
}

// Returns true if the probe appears to be connected.
bool probeConnected(uint8_t channel) {
  float r = readResistance(channel);
  return r > 500.0f && r < 2000000.0f;
}

// ── Calibration: calculate A, B, C from 3 reference points ───────────────────
//
// Pass three (temperature °C, resistance Ω) pairs measured at known
// reference temperatures (e.g. 0°C ice water, 25°C room temp, 100°C
// boiling water).
//
// Derivation:
//   Let Li = ln(Ri),  Yi = 1/Ti(K)
//
//   gamma2 = (Y2-Y1)/(L2-L1)
//   gamma3 = (Y3-Y1)/(L3-L1)
//
//   C = (gamma3 - gamma2) / ((L3-L2)·(L3+L2+L1))
//   B = gamma2 - C·(L2²+L2·L1+L1²)
//   A = Y1 - (B + C·L1²)·L1
//
// Returns true on success, false if inputs are invalid.

bool calcSHCoeffs(float t1c, float r1,
                  float t2c, float r2,
                  float t3c, float r3,
                  SHCoeffs* out) {
  if (r1 <= 0 || r2 <= 0 || r3 <= 0)           return false;
  if (t1c == t2c || t2c == t3c || t1c == t3c)  return false;

  float T1 = t1c + 273.15f, T2 = t2c + 273.15f, T3 = t3c + 273.15f;
  float L1 = log(r1), L2 = log(r2), L3 = log(r3);
  float Y1 = 1.0f / T1, Y2 = 1.0f / T2, Y3 = 1.0f / T3;

  float gamma2 = (Y2 - Y1) / (L2 - L1);
  float gamma3 = (Y3 - Y1) / (L3 - L1);

  float C = (gamma3 - gamma2) / ((L3 - L2) * (L3 + L2 + L1));
  float B = gamma2 - C * (L2 * L2 + L2 * L1 + L1 * L1);
  float A = Y1 - (B + C * L1 * L1) * L1;

  out->A = A;
  out->B = B;
  out->C = C;
  return true;
}
