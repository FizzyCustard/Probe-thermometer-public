#pragma once

#include <SD.h>
#include <Arduino.h>
#include "logger.h"

// ── Cook session state ────────────────────────────────────────────────────────

struct CookSession {
  bool     active;
  char     name[64];
  char     preset[48];
  char     filename[56];   // e.g. /BBQ/cooks/cook_2025-07-14T08-14-00.csv
  char     startedAt[24];  // ISO timestamp, e.g. "2025-07-14T08:14:00"
  uint32_t startMillis;
  float    meatTargetC;
  float    grillLowC;
  float    grillHighC;
};

CookSession activeCook = {};

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint32_t cookElapsedSeconds() {
  return (millis() - activeCook.startMillis) / 1000UL;
}

// ── Session control ───────────────────────────────────────────────────────────

// Creates the cook CSV file and writes the metadata header block.
// isoTimestamp e.g. "2025-07-14T08:14:00"
bool cookStart(const char* name, const char* preset,
               float meatTarget, float grillLow, float grillHigh,
               const char* isoTimestamp) {
  if (activeCook.active) return false;  // already running

  // Build filename: colons are illegal on FAT, replace with dashes
  char safets[20];
  strlcpy(safets, isoTimestamp, sizeof(safets));
  for (char* p = safets; *p; p++) if (*p == ':') *p = '-';
  snprintf(activeCook.filename, sizeof(activeCook.filename),
           "%s/cook_%s.csv", BBQ_COOKS, safets);

  strlcpy(activeCook.name,      name,         sizeof(activeCook.name));
  strlcpy(activeCook.preset,    preset,       sizeof(activeCook.preset));
  strlcpy(activeCook.startedAt, isoTimestamp, sizeof(activeCook.startedAt));
  activeCook.meatTargetC = meatTarget;
  activeCook.grillLowC   = grillLow;
  activeCook.grillHighC  = grillHigh;
  activeCook.startMillis = millis();

  File f = SD.open(activeCook.filename, FILE_WRITE);
  if (!f) {
    Serial.printf("Cook file open failed: %s\n", activeCook.filename);
    return false;
  }
  f.printf("#name,%s\n",          activeCook.name);
  f.printf("#preset,%s\n",        activeCook.preset);
  f.printf("#meat_target_c,%.0f\n", activeCook.meatTargetC);
  f.printf("#grill_low_c,%.0f\n",   activeCook.grillLowC);
  f.printf("#grill_high_c,%.0f\n",  activeCook.grillHighC);
  f.printf("#started_at,%s\n",    isoTimestamp);
  f.println("elapsed_s,meat_c,grill_c");
  f.close();

  activeCook.active = true;
  Serial.printf("Cook started: %s → %s\n", activeCook.name, activeCook.filename);
  return true;
}

// Appends a 30-second temperature sample row.
void cookAppendReading(float meatC, float grillC) {
  if (!activeCook.active) return;
  if (isnan(meatC) && isnan(grillC)) return;  // nothing to log
  File f = SD.open(activeCook.filename, FILE_WRITE);
  if (!f) return;
  char mBuf[12], gBuf[12];
  f.printf("%u,%s,%s\n",
           cookElapsedSeconds(),
           isnan(meatC)  ? "" : dtostrf(meatC,  1, 1, mBuf),
           isnan(grillC) ? "" : dtostrf(grillC, 1, 1, gBuf));
  f.close();
}

// Appends a timestamped note row.
void cookAppendNote(const char* text, float meatC) {
  if (!activeCook.active) return;
  File f = SD.open(activeCook.filename, FILE_WRITE);
  if (!f) return;
  if (isnan(meatC)) {
    f.printf("#note,%u,%s,\n", cookElapsedSeconds(), text);
  } else {
    f.printf("#note,%u,%s,%.1f\n", cookElapsedSeconds(), text, meatC);
  }
  f.close();
  Serial.printf("Note added at %us: %s\n", cookElapsedSeconds(), text);
}

// Writes the stopped_at footer row and clears the session.
void cookStop(const char* isoTimestamp) {
  if (!activeCook.active) return;
  File f = SD.open(activeCook.filename, FILE_WRITE);
  if (f) {
    f.printf("#stopped_at,%s\n", isoTimestamp);
    f.close();
  }
  Serial.printf("Cook stopped: %s (%.0f s)\n",
                activeCook.name, (float)cookElapsedSeconds());
  memset(&activeCook, 0, sizeof(activeCook));
}
