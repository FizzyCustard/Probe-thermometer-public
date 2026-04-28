#pragma once

#include <SD.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include "config.h"
#include "thermistor.h"

// ── SD mount ──────────────────────────────────────────────────────────────────

bool loggerBegin() {
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD mount failed");
    return false;
  }
  Serial.println("SD card mounted");
  return true;
}

// ── Temperature CSV logging ───────────────────────────────────────────────────

String getTodayFilename(String dateStr) {
  return "/log_" + dateStr + ".csv";
}

// Appends one row; creates file with header if it does not exist.
void logReading(String filename, String timestamp, float probe1C, float probe2C) {
  bool newFile = !SD.exists(filename.c_str());
  File f = SD.open(filename.c_str(), FILE_WRITE);
  if (!f) return;
  if (newFile) f.println("timestamp,probe1_c,probe2_c");
  f.printf("%s,%.1f,%.1f\n", timestamp.c_str(), probe1C, probe2C);
  f.close();
}

// ── Calibration JSON ──────────────────────────────────────────────────────────
//
// Saved to /calibration.json on the SD card.
// Loaded on boot — if present, overrides the config.h defaults.
//
// Format:
// {
//   "probe0": { "A": 4.475e-4, "B": 2.523e-4, "C": 1.701e-9,
//     "calibrated": "2025-07-14T14:30:00",
//     "points": [
//       {"temp_c": 0.0,  "resistance": 336400.0},
//       {"temp_c": 25.0, "resistance": 100000.0},
//       {"temp_c": 100.0,"resistance": 6960.0}
//     ]
//   },
//   "probe1": { ... }
// }

#define CAL_FILE "/calibration.json"

bool saveCalibration(uint8_t probeIndex,
                     const SHCoeffs& coeffs,
                     float t1c, float r1,
                     float t2c, float r2,
                     float t3c, float r3,
                     String timestamp) {
  // Read existing file so we don't overwrite the other probe's data
  StaticJsonDocument<1024> doc;
  if (SD.exists(CAL_FILE)) {
    File f = SD.open(CAL_FILE, FILE_READ);
    if (f) { deserializeJson(doc, f); f.close(); }
  }

  char key[8];
  snprintf(key, sizeof(key), "probe%d", probeIndex);
  JsonObject probe = doc.createNestedObject(key);
  probe["A"] = coeffs.A;
  probe["B"] = coeffs.B;
  probe["C"] = coeffs.C;
  probe["calibrated"] = timestamp;

  JsonArray points = probe.createNestedArray("points");
  JsonObject p1 = points.createNestedObject();
  p1["temp_c"] = t1c; p1["resistance"] = r1;
  JsonObject p2 = points.createNestedObject();
  p2["temp_c"] = t2c; p2["resistance"] = r2;
  JsonObject p3 = points.createNestedObject();
  p3["temp_c"] = t3c; p3["resistance"] = r3;

  SD.remove(CAL_FILE);
  File f = SD.open(CAL_FILE, FILE_WRITE);
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  Serial.printf("Calibration saved for probe %d\n", probeIndex);
  return true;
}

// Loads calibration.json from SD and updates probeCoeffs[].
// Call once in setup() after loggerBegin().
void loadCalibration() {
  if (!SD.exists(CAL_FILE)) return;
  File f = SD.open(CAL_FILE, FILE_READ);
  if (!f) return;

  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();

  for (uint8_t i = 0; i < 2; i++) {
    char key[8];
    snprintf(key, sizeof(key), "probe%d", i);
    if (doc.containsKey(key)) {
      probeCoeffs[i].A = doc[key]["A"].as<float>();
      probeCoeffs[i].B = doc[key]["B"].as<float>();
      probeCoeffs[i].C = doc[key]["C"].as<float>();
      Serial.printf("Loaded calibration for probe %d: A=%.6e B=%.6e C=%.6e\n",
                    i, probeCoeffs[i].A, probeCoeffs[i].B, probeCoeffs[i].C);
    }
  }
}
