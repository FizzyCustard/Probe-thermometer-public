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

  // Never append into an existing cook file (duplicate timestamp or repeated
  // "unknown" fallback) — add a numeric suffix until the name is free.
  for (uint8_t i = 1; i < 10 && SD.exists(activeCook.filename); i++) {
    snprintf(activeCook.filename, sizeof(activeCook.filename),
             "%s/cook_%s-%u.csv", BBQ_COOKS, safets, i);
  }

  strlcpy(activeCook.name,      name,         sizeof(activeCook.name));
  strlcpy(activeCook.preset,    preset,       sizeof(activeCook.preset));
  strlcpy(activeCook.startedAt, isoTimestamp, sizeof(activeCook.startedAt));
  csvSanitize(activeCook.name);
  csvSanitize(activeCook.preset);
  activeCook.meatTargetC = meatTarget;
  activeCook.grillLowC   = grillLow;
  activeCook.grillHighC  = grillHigh;
  activeCook.startMillis = millis();

  File f = sdOpenWrite(activeCook.filename);
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
  File f = sdOpenWrite(activeCook.filename);
  if (!f) return;
  char mBuf[12], gBuf[12];
  f.printf("%u,%s,%s\n",
           cookElapsedSeconds(),
           isnan(meatC)  ? "" : dtostrf(meatC,  1, 1, mBuf),
           isnan(grillC) ? "" : dtostrf(grillC, 1, 1, gBuf));
  f.close();
}

// Appends a timestamped note row. Text is sanitised: newlines would split the
// row and a leading '#'-keyword could collide with the metadata format.
void cookAppendNote(const char* text, float meatC) {
  if (!activeCook.active) return;
  char safe[200];
  strlcpy(safe, text, sizeof(safe));
  for (char* p = safe; *p; p++) {
    if (*p == '\n' || *p == '\r') *p = ' ';
  }
  File f = sdOpenWrite(activeCook.filename);
  if (!f) return;
  uint32_t elapsed = cookElapsedSeconds();
  if (isnan(meatC)) {
    f.printf("#note,%u,%s,\n", elapsed, safe);
  } else {
    f.printf("#note,%u,%s,%.1f\n", elapsed, safe, meatC);
  }
  f.close();
  Serial.printf("Note added at %us: %s\n", elapsed, safe);
}

// Writes the stopped_at footer row and clears the session.
void cookStop(const char* isoTimestamp) {
  if (!activeCook.active) return;
  File f = sdOpenWrite(activeCook.filename);
  if (f) {
    f.printf("#stopped_at,%s\n", isoTimestamp);
    f.close();
  }
  Serial.printf("Cook stopped: %s (%.0f s)\n",
                activeCook.name, (float)cookElapsedSeconds());
  memset(&activeCook, 0, sizeof(activeCook));
}

// ── Reboot recovery ───────────────────────────────────────────────────────────
//
// A brown-out mid-cook used to lose the session entirely: the file stayed
// "in progress" forever and sampling never resumed. On boot we scan for cook
// files without a #stopped_at footer, resume the newest (elapsed time is
// reconstructed from its last sample row), and close any older orphans.

// Scans one cook file. Fills header fields into `s`, returns the last logged
// elapsed_s, and sets `stopped` if a #stopped_at footer exists.
static uint32_t cookScanFile(const char* path, CookSession& s, bool& stopped) {
  stopped = false;
  uint32_t lastElapsed = 0;
  File f = SD.open(path, FILE_READ);
  if (!f) { stopped = true; return 0; }  // unreadable — treat as closed

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if      (line.startsWith("#stopped_at,"))    { stopped = true; break; }
    else if (line.startsWith("#name,"))          strlcpy(s.name,   line.substring(6).c_str(),  sizeof(s.name));
    else if (line.startsWith("#preset,"))        strlcpy(s.preset, line.substring(8).c_str(),  sizeof(s.preset));
    else if (line.startsWith("#meat_target_c,")) s.meatTargetC = line.substring(15).toFloat();
    else if (line.startsWith("#grill_low_c,"))   s.grillLowC   = line.substring(13).toFloat();
    else if (line.startsWith("#grill_high_c,"))  s.grillHighC  = line.substring(14).toFloat();
    else if (line.startsWith("#started_at,"))    strlcpy(s.startedAt, line.substring(12).c_str(), sizeof(s.startedAt));
    else if (!line.startsWith("#") && !line.startsWith("elapsed")) {
      int comma = line.indexOf(',');
      if (comma > 0) lastElapsed = line.substring(0, comma).toInt();
    }
    yield();
  }
  f.close();
  return lastElapsed;
}

// Call once from setup() after the SD card is mounted. Returns true if a cook
// was resumed.
bool cookTryResume() {
  if (!sdOk) return false;

  char newest[40] = "";
  File dir = SD.open(BBQ_COOKS);
  if (!dir) return false;

  // Pass 1: find the newest unstopped cook (filenames sort chronologically)
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) { entry.close(); continue; }
    char fname[40];
    strlcpy(fname, entry.name(), sizeof(fname));
    entry.close();
    if (strncmp(fname, "cook_", 5) != 0) continue;

    char path[64];
    snprintf(path, sizeof(path), "%s/%s", BBQ_COOKS, fname);
    CookSession tmp = {};
    bool stopped;
    cookScanFile(path, tmp, stopped);
    if (!stopped && strcmp(fname, newest) > 0) strlcpy(newest, fname, sizeof(newest));
    yield();
  }
  dir.close();

  if (newest[0] == '\0') return false;

  // Pass 2: close any other orphans so the history list stays sane
  dir = SD.open(BBQ_COOKS);
  if (dir) {
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      if (entry.isDirectory()) { entry.close(); continue; }
      char fname[40];
      strlcpy(fname, entry.name(), sizeof(fname));
      entry.close();
      if (strncmp(fname, "cook_", 5) != 0) continue;
      if (strcmp(fname, newest) == 0) continue;

      char path[64];
      snprintf(path, sizeof(path), "%s/%s", BBQ_COOKS, fname);
      CookSession tmp = {};
      bool stopped;
      cookScanFile(path, tmp, stopped);
      if (!stopped) {
        File f = SD.open(path, FILE_WRITE);
        if (f) { f.println("#stopped_at,unknown"); f.close(); }
        Serial.printf("Closed orphaned cook: %s\n", fname);
      }
      yield();
    }
    dir.close();
  }

  // Resume the newest
  CookSession resumed = {};
  bool stopped;
  uint32_t lastElapsed = cookScanFile(
      (String(BBQ_COOKS) + "/" + newest).c_str(), resumed, stopped);
  if (stopped) return false;  // vanished/changed between passes

  snprintf(resumed.filename, sizeof(resumed.filename), "%s/%s", BBQ_COOKS, newest);
  // uint32 wraparound makes (millis() - startMillis) correct even though this
  // subtraction "underflows" right after boot
  resumed.startMillis = millis() - lastElapsed * 1000UL;
  resumed.active      = true;
  activeCook = resumed;

  cookAppendNote("Device restarted \xe2\x80\x94 cook resumed", NAN);
  Serial.printf("Resumed cook: %s (%u s elapsed)\n", activeCook.name, lastElapsed);
  return true;
}
