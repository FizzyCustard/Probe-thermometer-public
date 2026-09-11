#pragma once

#include <Adafruit_ADS1X15.h>
#include <math.h>
#include "config.h"

extern Adafruit_ADS1115 ads;
extern bool adsReady;   // false when the ADS1115 failed to initialise

// Resistance sanity window: outside this the probe is open/shorted/absent.
#define PROBE_R_MIN       500.0f
#define PROBE_R_MAX   2000000.0f

// ── Steinhart-Hart coefficients ───────────────────────────────────────────────

struct SHCoeffs {
  float A, B, C;
};

// Loaded from config.h defaults; overwritten when calibration file is loaded
// from SD card on boot, or when the browser sends new calibration data.
SHCoeffs probeCoeffs[2] = {
  { PROBE0_SH_A, PROBE0_SH_B, PROBE0_SH_C },
  { PROBE1_SH_A, PROBE1_SH_B, PROBE1_SH_C },
};

// Where the coefficients came from, and the reference points behind them.
// Filled by loadCalibration() on boot and by a successful cal_save; reported
// in the `config` message so the UI can show "Calibrated ✓ <date>" and let the
// user compare a new capture against the previous one. `calibrated[0] == 0`
// means the probe is running on the config.h defaults.
struct CalInfo {
  char  calibrated[25];   // ISO-8601 timestamp from the browser, or ""
  float t[3];             // reference temperatures, °C
  float r[3];             // measured resistances, Ω
};

CalInfo probeCal[2] = {};

// ── ADC → resistance ──────────────────────────────────────────────────────────
//
// ADS1115 voltage divider topology:
//
//   3.3V ──── SERIES_RESISTOR ──── ADS1115 channel ──── NTC probe ──── GND
//
// At GAIN_ONE the ADS1115 measures ±4.096V (full scale = 32767 counts).
// We scale by the actual 3.3V supply.

float readResistance(uint8_t channel) {
  if (!adsReady) return -1.0f;                        // ADC unavailable
  int16_t raw = ads.readADC_SingleEnded(channel);
  if (raw <= 0) return -1.0f;                         // node at GND = shorted probe
  float voltage = (raw / 32767.0f) * 4.096f;
  if (voltage >= SUPPLY_VOLTAGE) return -1.0f;        // pulled to 3.3V = open circuit / no probe
  // Rearranged voltage divider: R_ntc = R_series * V / (Vsupply - V)
  return SERIES_RESISTOR * voltage / (SUPPLY_VOLTAGE - voltage);
}

// Median of three consecutive reads — rejects single-sample glitches so the
// displayed value and range alerts don't twitch near thresholds. ~24 ms per
// call at the default 128 SPS; only call from loop(), never async callbacks.
float readResistanceMedian(uint8_t channel) {
  float a = readResistance(channel);
  float b = readResistance(channel);
  float c = readResistance(channel);
  if (a > b) { float t = a; a = b; b = t; }
  if (b > c) { float t = b; b = c; c = t; }
  if (a > b) { float t = a; a = b; b = t; }
  return b;
}

// ── Resistance → temperature (full Steinhart-Hart) ───────────────────────────
//
//   1/T(K) = A + B·ln(R) + C·(ln(R))³
//
// Returns NAN for any invalid input (serialises as JSON null).

float resistanceToCelsius(float resistance, uint8_t probeIndex) {
  if (probeIndex > 1) return NAN;
  if (resistance <= 0.0f) return NAN;
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
  if (r < PROBE_R_MIN || r > PROBE_R_MAX) return NAN;
  return resistanceToCelsius(r, channel);
}

// Returns true if the probe appears to be connected.
bool probeConnected(uint8_t channel) {
  float r = readResistance(channel);
  return r > PROBE_R_MIN && r < PROBE_R_MAX;
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
// Returns true on success, false if inputs are invalid or the maths
// degenerates (duplicate points → division by zero → non-finite output).

bool calcSHCoeffs(float t1c, float r1,
                  float t2c, float r2,
                  float t3c, float r3,
                  SHCoeffs* out) {
  if (r1 <= 0 || r2 <= 0 || r3 <= 0)           return false;
  if (t1c == t2c || t2c == t3c || t1c == t3c)  return false;

  float T1 = t1c + 273.15f, T2 = t2c + 273.15f, T3 = t3c + 273.15f;
  float L1 = log(r1), L2 = log(r2), L3 = log(r3);
  if (L1 == L2 || L2 == L3 || L1 == L3)        return false;  // duplicate resistances
  float Y1 = 1.0f / T1, Y2 = 1.0f / T2, Y3 = 1.0f / T3;

  float gamma2 = (Y2 - Y1) / (L2 - L1);
  float gamma3 = (Y3 - Y1) / (L3 - L1);

  float C = (gamma3 - gamma2) / ((L3 - L2) * (L3 + L2 + L1));
  float B = gamma2 - C * (L2 * L2 + L2 * L1 + L1 * L1);
  float A = Y1 - (B + C * L1 * L1) * L1;

  if (!isfinite(A) || !isfinite(B) || !isfinite(C)) return false;

  out->A = A;
  out->B = B;
  out->C = C;
  return true;
}

// ── Calibration: validate three reference points and derive coefficients ─────
//
// The browser only ever sends raw (temp, resistance) points; the coefficients
// are computed HERE so a buggy or malicious client can never install NaN maths
// that bricks the readings until recalibration. Every rejection names the
// offending point so the UI can highlight it instead of showing a generic
// error.

enum CalError : uint8_t {
  CAL_OK = 0,
  CAL_BAD_INPUT,        // non-finite or non-positive resistance / temperature
  CAL_TEMPS_TOO_CLOSE,  // two reference temperatures within 0.5 °C
  CAL_NOT_MONOTONIC,    // resistance doesn't fall as temperature rises (NTC)
  CAL_DEGENERATE,       // maths blew up (duplicate resistances etc.)
  CAL_ROUNDTRIP,        // a point converts back more than ±3 °C from its reference
};

static const char* calErrorReason(CalError e) {
  switch (e) {
    case CAL_OK:              return "ok";
    case CAL_BAD_INPUT:       return "bad_input";
    case CAL_TEMPS_TOO_CLOSE: return "temps_too_close";
    case CAL_NOT_MONOTONIC:   return "not_monotonic";
    case CAL_DEGENERATE:      return "degenerate";
    case CAL_ROUNDTRIP:       return "roundtrip";
  }
  return "unknown";
}

// On failure *badPoint is the index (0-2) of the point to blame, or 255 when
// the failure isn't attributable to one point.
CalError calibrateFromPoints(const float t[3], const float r[3],
                             SHCoeffs* out, uint8_t* badPoint) {
  *badPoint = 255;

  for (uint8_t i = 0; i < 3; i++) {
    if (!isfinite(t[i]) || !isfinite(r[i]) || r[i] <= 0.0f ||
        t[i] < -50.0f || t[i] > 350.0f) {
      *badPoint = i;
      return CAL_BAD_INPUT;
    }
  }

  // Order the points by temperature so the monotonic check reads naturally.
  uint8_t idx[3] = { 0, 1, 2 };
  for (uint8_t i = 0; i < 2; i++)
    for (uint8_t j = i + 1; j < 3; j++)
      if (t[idx[j]] < t[idx[i]]) { uint8_t s = idx[i]; idx[i] = idx[j]; idx[j] = s; }

  for (uint8_t k = 0; k < 2; k++) {
    uint8_t lo = idx[k], hi = idx[k + 1];
    if (t[hi] - t[lo] < 0.5f) { *badPoint = hi; return CAL_TEMPS_TOO_CLOSE; }
    // NTC: warmer must mean lower resistance. If not, one probe never went in
    // the bath, or two baths were mixed up.
    if (r[hi] >= r[lo])       { *badPoint = hi; return CAL_NOT_MONOTONIC; }
  }

  SHCoeffs c;
  if (!calcSHCoeffs(t[0], r[0], t[1], r[1], t[2], r[2], &c)) return CAL_DEGENERATE;

  // Every reference point must convert back to within ±3 °C of what the user
  // entered. A 3-point exact fit normally reproduces them to float precision;
  // a big residual means a wildly wrong point slipped past the checks above.
  for (uint8_t i = 0; i < 3; i++) {
    float L    = log(r[i]);
    float invT = c.A + c.B * L + c.C * L * L * L;
    if (invT <= 0.0f || fabsf((1.0f / invT - 273.15f) - t[i]) > 3.0f) {
      *badPoint = i;
      return CAL_ROUNDTRIP;
    }
  }

  *out = c;
  return CAL_OK;
}
