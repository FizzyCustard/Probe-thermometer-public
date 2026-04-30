#pragma once

#include <SD.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include "config.h"
#include "thermistor.h"

// ── SD paths ──────────────────────────────────────────────────────────────────

#define BBQ_ROOT    "/BBQ"
#define BBQ_LOGS    "/BBQ/logs"
#define BBQ_COOKS   "/BBQ/cooks"
#define BBQ_CONFIG  "/BBQ/config"
#define CAL_FILE    "/BBQ/config/calibration.json"
#define SETTINGS_FILE "/BBQ/config/settings.csv"
#define PRESETS_FILE  "/BBQ/config/presets.csv"
#define FAVS_FILE     "/BBQ/config/favourites.csv"

// ── SD mount + folder tree ────────────────────────────────────────────────────

bool loggerBegin() {
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD mount failed");
    return false;
  }
  Serial.println("SD card mounted");

  // Create the /BBQ/ folder hierarchy if it doesn't exist
  SD.mkdir(BBQ_ROOT);
  SD.mkdir(BBQ_LOGS);
  SD.mkdir(BBQ_COOKS);
  SD.mkdir(BBQ_CONFIG);

  return true;
}

// ── Temperature CSV logging ───────────────────────────────────────────────────

String getTodayFilename(String dateStr) {
  return String(BBQ_LOGS) + "/log_" + dateStr + ".csv";
}

void logReading(String filename, String timestamp, float probe1C, float probe2C) {
  if (isnan(probe1C) && isnan(probe2C)) return;  // nothing to log
  bool newFile = !SD.exists(filename.c_str());
  File f = SD.open(filename.c_str(), FILE_WRITE);
  if (!f) return;
  if (newFile) f.println("timestamp,meat_c,grill_c");
  f.printf("%s,%s,%s\n",
           timestamp.c_str(),
           isnan(probe1C) ? "" : String(probe1C, 1).c_str(),
           isnan(probe2C) ? "" : String(probe2C, 1).c_str());
  f.close();
}

// ── Settings persistence ──────────────────────────────────────────────────────

void saveSettings(float meatTarget, float grillLow, float grillHigh) {
  SD.remove(SETTINGS_FILE);
  File f = SD.open(SETTINGS_FILE, FILE_WRITE);
  if (!f) return;
  f.println("meat_target_c,grill_low_c,grill_high_c");
  f.printf("%.1f,%.1f,%.1f\n", meatTarget, grillLow, grillHigh);
  f.close();
  Serial.printf("Settings saved: meat=%.1f grill=%.1f–%.1f\n",
                meatTarget, grillLow, grillHigh);
}

// Returns false if file is missing; caller keeps its defaults.
bool loadSettings(float& meatTarget, float& grillLow, float& grillHigh) {
  if (!SD.exists(SETTINGS_FILE)) return false;
  File f = SD.open(SETTINGS_FILE, FILE_READ);
  if (!f) return false;

  f.readStringUntil('\n');  // skip header
  String line = f.readStringUntil('\n');
  f.close();
  if (line.length() == 0) return false;

  int c1 = line.indexOf(',');
  int c2 = line.indexOf(',', c1 + 1);
  if (c1 < 0 || c2 < 0) return false;

  meatTarget = line.substring(0, c1).toFloat();
  grillLow   = line.substring(c1 + 1, c2).toFloat();
  grillHigh  = line.substring(c2 + 1).toFloat();

  Serial.printf("Settings loaded: meat=%.1f grill=%.1f–%.1f\n",
                meatTarget, grillLow, grillHigh);
  return true;
}

// ── Presets ───────────────────────────────────────────────────────────────────

struct Preset {
  char  name[48];
  float tempC;
};

void savePresets(Preset* list, uint8_t count) {
  SD.remove(PRESETS_FILE);
  File f = SD.open(PRESETS_FILE, FILE_WRITE);
  if (!f) return;
  f.println("name,meat_target_c");
  for (uint8_t i = 0; i < count; i++) {
    f.printf("%s,%.0f\n", list[i].name, list[i].tempC);
  }
  f.close();
}

uint8_t loadPresets(Preset* list, uint8_t maxCount) {
  if (!SD.exists(PRESETS_FILE)) return 0;
  File f = SD.open(PRESETS_FILE, FILE_READ);
  if (!f) return 0;

  f.readStringUntil('\n');  // skip header
  uint8_t count = 0;
  while (f.available() && count < maxCount) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int comma = line.lastIndexOf(',');
    if (comma < 0) continue;
    strlcpy(list[count].name, line.substring(0, comma).c_str(), sizeof(list[count].name));
    list[count].tempC = line.substring(comma + 1).toFloat();
    count++;
  }
  f.close();
  Serial.printf("Loaded %d presets\n", count);
  return count;
}

void seedDefaultPresets(Preset* list, uint8_t& count) {
  const struct { const char* name; float t; } defaults[] = {
    { "Beef \xe2\x80\x94 rare",           52.0f },
    { "Beef \xe2\x80\x94 medium rare",     57.0f },
    { "Beef \xe2\x80\x94 medium",          63.0f },
    { "Beef \xe2\x80\x94 well done",       71.0f },
    { "Lamb \xe2\x80\x94 medium rare",     57.0f },
    { "Lamb \xe2\x80\x94 medium",          63.0f },
    { "Pork \xe2\x80\x94 safe minimum",    71.0f },
    { "Chicken \xe2\x80\x94 safe minimum", 74.0f },
    { "Brisket / pulled pork",             95.0f },
  };
  count = 0;
  for (auto& d : defaults) {
    strlcpy(list[count].name, d.name, sizeof(list[count].name));
    list[count].tempC = d.t;
    count++;
  }
  savePresets(list, count);
  Serial.println("Default presets seeded");
}

// ── Favourites ────────────────────────────────────────────────────────────────

struct Favourite {
  char  id[24];
  char  name[48];
  float meatTarget;
  float grillLow;
  float grillHigh;
  char  preset[48];
};

void saveFavourites(Favourite* list, uint8_t count) {
  SD.remove(FAVS_FILE);
  File f = SD.open(FAVS_FILE, FILE_WRITE);
  if (!f) return;
  f.println("id,name,meat_target_c,grill_low_c,grill_high_c,preset");
  for (uint8_t i = 0; i < count; i++) {
    f.printf("%s,%s,%.1f,%.1f,%.1f,%s\n",
             list[i].id, list[i].name,
             list[i].meatTarget, list[i].grillLow, list[i].grillHigh,
             list[i].preset);
  }
  f.close();
}

uint8_t loadFavourites(Favourite* list, uint8_t maxCount) {
  if (!SD.exists(FAVS_FILE)) return 0;
  File f = SD.open(FAVS_FILE, FILE_READ);
  if (!f) return 0;

  f.readStringUntil('\n');  // skip header
  uint8_t count = 0;
  while (f.available() && count < maxCount) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    // Split 6 comma-separated fields; preset field may contain commas only if
    // quoted — for now we use lastIndexOf chain (names don't contain commas).
    int c[5];
    c[0] = line.indexOf(',');
    c[1] = line.indexOf(',', c[0] + 1);
    c[2] = line.indexOf(',', c[1] + 1);
    c[3] = line.indexOf(',', c[2] + 1);
    c[4] = line.indexOf(',', c[3] + 1);
    if (c[3] < 0) continue;  // need at least 5 delimiters

    strlcpy(list[count].id,       line.substring(0, c[0]).c_str(),          sizeof(list[count].id));
    strlcpy(list[count].name,     line.substring(c[0]+1, c[1]).c_str(),     sizeof(list[count].name));
    list[count].meatTarget =       line.substring(c[1]+1, c[2]).toFloat();
    list[count].grillLow   =       line.substring(c[2]+1, c[3]).toFloat();
    list[count].grillHigh  =       line.substring(c[3]+1, c[4] < 0 ? line.length() : c[4]).toFloat();
    strlcpy(list[count].preset,   (c[4] < 0 ? "" : line.substring(c[4]+1).c_str()), sizeof(list[count].preset));
    count++;
  }
  f.close();
  Serial.printf("Loaded %d favourites\n", count);
  return count;
}

// ── Calibration JSON ──────────────────────────────────────────────────────────

bool saveCalibration(uint8_t probeIndex,
                     const SHCoeffs& coeffs,
                     float t1c, float r1,
                     float t2c, float r2,
                     float t3c, float r3,
                     String timestamp) {
  JsonDocument doc;
  if (SD.exists(CAL_FILE)) {
    File f = SD.open(CAL_FILE, FILE_READ);
    if (f) { deserializeJson(doc, f); f.close(); }
  }

  char key[8];
  snprintf(key, sizeof(key), "probe%d", probeIndex);
  JsonObject probe = doc[key].to<JsonObject>();
  probe["A"] = coeffs.A;
  probe["B"] = coeffs.B;
  probe["C"] = coeffs.C;
  probe["calibrated"] = timestamp;

  JsonArray points = probe["points"].to<JsonArray>();
  JsonObject p1 = points.add<JsonObject>(); p1["temp_c"] = t1c; p1["resistance"] = r1;
  JsonObject p2 = points.add<JsonObject>(); p2["temp_c"] = t2c; p2["resistance"] = r2;
  JsonObject p3 = points.add<JsonObject>(); p3["temp_c"] = t3c; p3["resistance"] = r3;

  SD.remove(CAL_FILE);
  File f = SD.open(CAL_FILE, FILE_WRITE);
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  Serial.printf("Calibration saved for probe %d\n", probeIndex);
  return true;
}

void loadCalibration() {
  if (!SD.exists(CAL_FILE)) return;
  File f = SD.open(CAL_FILE, FILE_READ);
  if (!f) return;

  JsonDocument doc;
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
