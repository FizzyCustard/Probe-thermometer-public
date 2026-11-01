#pragma once
#include <stdint.h>
// Host stand-in: thermistor.h only needs the type to exist.
struct Adafruit_ADS1115 {
  int16_t readADC_SingleEnded(uint8_t) { return 0; }
};
