#pragma once

#include <Adafruit_ADS1X15.h>
#include <math.h>

// Probe specs — Morpilot NTC thermistor
const float THERMISTOR_R0  = 100000.0;  // resistance at 25°C (Ω)
const float THERMISTOR_B   = 3950.0;    // B-value (K)
const float THERMISTOR_T0  = 298.15;    // 25°C in Kelvin
const float SERIES_R       = 100000.0;  // voltage divider resistor (Ω)

// ADS1115 full-scale for GAIN_ONE (±4.096V) = 32767 counts
// Scaled to 3.3V supply: effective max counts for 3.3V
const float ADC_COUNTS_3V3 = 32767.0 * (3.3f / 4.096f);

extern Adafruit_ADS1115 ads;

// Returns the thermistor resistance in Ω for the given ADS1115 channel.
float readResistance(uint8_t channel) {
  // TODO: read raw ADC, convert counts to resistance via voltage divider
  return 0.0;
}

// Converts a resistance (Ω) to temperature (°C) using Steinhart-Hart B equation.
float resistanceToCelsius(float resistance) {
  // TODO: implement Steinhart-Hart B-parameter equation
  return 0.0;
}

// Returns true if a probe is connected (reading is within sane range).
bool probeConnected(uint8_t channel) {
  // TODO: check resistance is within plausible bounds (e.g. 500Ω – 500kΩ)
  return false;
}

// Convenience: read a channel and return °C directly.
float readCelsius(uint8_t channel) {
  return resistanceToCelsius(readResistance(channel));
}
