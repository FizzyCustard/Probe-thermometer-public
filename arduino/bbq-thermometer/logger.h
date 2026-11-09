#pragma once

#include <SD.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include "config.h"
#include "thermistor.h"

// ── Storage limits ────────────────────────────────────────────────────────────

#ifndef MAX_PRESETS
#define MAX_PRESETS   16
#endif
#ifndef MAX_FAVS
#define MAX_FAVS      32
#endif

// ── SD paths ──────────────────────────────────────────────────────────────────

#define BBQ_ROOT    "/BBQ"
#define BBQ_LOGS    "/BBQ/logs"
#define BBQ_COOKS   "/BBQ/cooks"
#define BBQ_CONFIG  "/BBQ/config"
#define CAL_FILE    "/BBQ/config/calibration.json"
#define SETTINGS_FILE "/BBQ/config/settings.csv"
#define PRESETS_FILE  "/BBQ/config/presets.csv"
#define FAVS_FILE     "/BBQ/config/favourites.csv"

#define SD_REMOUNT_INTERVAL_MS  30000UL

// True while the SD card is mounted and writable. Cleared on any failed
// open/write; loggerTick() periodically attempts a remount. Broadcast to the
// UI so the user knows when nothing is being recorded.
bool sdOk = false;

// ── CSV field sanitiser ───────────────────────────────────────────────────────
//
// User-supplied text (cook names, presets, notes) is stored in comma-separated
// files that we parse back with plain delimiter splits. Strip the characters
// that would corrupt the format: newlines break rows, commas shift fields.

void csvSanitize(char* s) {
  for (; *s; s++) {
    if (*s == '\n' || *s == '\r') *s = ' ';
    else if (*s == ',')           *s = ';';
  }
}

// ── SD mount + folder tree ────────────────────────────────────────────────────

static void sdMakeTree() {
  SD.mkdir(BBQ_ROOT);
  SD.mkdir(BBQ_LOGS);
  SD.mkdir(BBQ_COOKS);
  SD.mkdir(BBQ_CONFIG);
}

bool loggerBegin() {
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("SD mount failed"));
    sdOk = false;
    return false;
  }
  Serial.println(F("SD card mounted"));
  sdMakeTree();
  sdOk = true;
  return true;
}

// Call once per loop(): retries the mount every 30 s after a failure so a
// reseated card starts recording again without a reboot.
void loggerTick() {
  static uint32_t lastAttempt = 0;
  if (sdOk) return;
  if (millis() - lastAttempt < SD_REMOUNT_INTERVAL_MS) return;
  lastAttempt = millis();
  if (SD.begin(SD_CS_PIN)) {
    sdMakeTree();
    sdOk = true;
    Serial.println(F("SD card remounted"));
  }
}

// Open a file for writing, tracking SD health on failure.
static File sdOpenWrite(const char* path) {
  File f = SD.open(path, FILE_WRITE);
  if (!f) sdOk = false;
  return f;
}

// ── Temperature CSV logging ───────────────────────────────────────────────────

String getTodayFilename(String dateStr) {
  return String(BBQ_LOGS) + "/log_" + dateStr + ".csv";
}

void logReading(String filename, String timestamp, float probe1C, float probe2C) {
  if (!sdOk) return;
  if (isnan(probe1C) && isnan(probe2C)) return;  // nothing to log
  bool newFile = !SD.exists(filename.c_str());
  File f = sdOpenWrite(filename.c_str());
  if (!f) return;
  if (newFile) f.println(F("timestamp,meat_c,grill_c"));
  char b1[12], b2[12];
  f.printf("%s,%s,%s\n",
           timestamp.c_str(),
           isnan(probe1C) ? "" : dtostrf(probe1C, 1, 1, b1),
           isnan(probe2C) ? "" : dtostrf(probe2C, 1, 1, b2));
  f.close();
}

// ── Settings persistence ──────────────────────────────────────────────────────

void saveSettings(float meatTarget, float grillLow, float grillHigh) {
  if (!sdOk) return;
  SD.remove(SETTINGS_FILE);
  File f = sdOpenWrite(SETTINGS_FILE);
  if (!f) return;
  f.println(F("meat_target_c,grill_low_c,grill_high_c"));
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
  if (!sdOk) return;
  SD.remove(PRESETS_FILE);
  File f = sdOpenWrite(PRESETS_FILE);
  if (!f) return;
  f.println(F("name,meat_target_c"));
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
    { "Pork \xe2\x80\x94 well done",        71.0f },
    { "Chicken \xe2\x80\x94 well done",     74.0f },
    { "Brisket / pulled pork",             95.0f },
  };
  count = 0;
  for (auto& d : defaults) {
    strlcpy(list[count].name, d.name, sizeof(list[count].name));
    list[count].tempC = d.t;
    count++;
  }
  savePresets(list, count);
  Serial.println(F("Default presets seeded"));
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
  if (!sdOk) return;
  SD.remove(FAVS_FILE);
  File f = sdOpenWrite(FAVS_FILE);
  if (!f) return;
  f.println(F("id,name,meat_target_c,grill_low_c,grill_high_c,preset"));
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

    // Split 6 comma-separated fields. Fields are sanitised on write
    // (csvSanitize) so they can never contain embedded commas.
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

// The file is written from RAM (probeCoeffs + probeCal) in one go, so saving
// both probes costs a single SD write. Probes still on config.h defaults
// (probeCal[i].calibrated empty) are left out of the file, so a later boot
// falls back to the defaults for them rather than persisting the defaults as
// if they were a calibration.
//
// SD writes block: only call from loop() (wsTick's deferred queue), never
// from the async WebSocket callback.
bool saveCalibrationFile() {
  if (!sdOk) return false;

  JsonDocument doc;
  for (uint8_t i = 0; i < 2; i++) {
    if (probeCal[i].calibrated[0] == 0) continue;

    char key[12];
    snprintf(key, sizeof(key), "probe%u", i);
    JsonObject probe = doc[key].to<JsonObject>();
    probe["A"] = probeCoeffs[i].A;
    probe["B"] = probeCoeffs[i].B;
    probe["C"] = probeCoeffs[i].C;
    probe["calibrated"] = probeCal[i].calibrated;

    JsonArray points = probe["points"].to<JsonArray>();
    for (uint8_t k = 0; k < 3; k++) {
      JsonObject p = points.add<JsonObject>();
      p["temp_c"]     = probeCal[i].t[k];
      p["resistance"] = probeCal[i].r[k];
    }
  }

  SD.remove(CAL_FILE);
  File f = sdOpenWrite(CAL_FILE);
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  Serial.println(F("Calibration file saved"));
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
    char key[12];
    snprintf(key, sizeof(key), "probe%u", i);
    if (!doc[key].is<JsonObject>()) continue;

    float A = doc[key]["A"].as<float>();
    float B = doc[key]["B"].as<float>();
    float C = doc[key]["C"].as<float>();
    if (!isfinite(A) || !isfinite(B) || !isfinite(C)) {
      Serial.printf("Ignoring corrupt calibration for probe %u\n", i);
      continue;
    }
    probeCoeffs[i].A = A;
    probeCoeffs[i].B = B;
    probeCoeffs[i].C = C;

    // Metadata is optional (older files may lack it); the coefficients are
    // what matter. A missing timestamp still counts as "calibrated from SD".
    const char* ts = doc[key]["calibrated"] | "unknown";
    strlcpy(probeCal[i].calibrated, ts, sizeof(probeCal[i].calibrated));
    JsonArray pts = doc[key]["points"].as<JsonArray>();
    for (uint8_t k = 0; k < 3; k++) {
      probeCal[i].t[k] = (pts && k < pts.size()) ? pts[k]["temp_c"].as<float>()     : NAN;
      probeCal[i].r[k] = (pts && k < pts.size()) ? pts[k]["resistance"].as<float>() : NAN;
    }

    Serial.printf("Loaded calibration for probe %u: A=%.6e B=%.6e C=%.6e (%s)\n",
                  i, A, B, C, probeCal[i].calibrated);
  }
}
