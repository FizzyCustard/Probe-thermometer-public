#pragma once

#include <SD.h>
#include <Arduino.h>

const uint8_t SD_CS_PIN = D8;

// Mounts the SD card. Returns true on success.
bool loggerBegin() {
  // TODO: SD.begin(SD_CS_PIN)
  return false;
}

// Returns today's log filename based on the NTP date string.
// Format: /log_YYYY-MM-DD.csv
String getTodayFilename(String dateStr) {
  // TODO: build filename from dateStr
  return "/log_" + dateStr + ".csv";
}

// Appends a CSV row to today's log file.
// Creates the file with a header row if it does not yet exist.
void logReading(String filename, String timestamp, float probe1C, float probe2C) {
  // TODO: open file in append mode
  // TODO: write header if file is new
  // TODO: write timestamp,probe1C,probe2C row
  // TODO: close file
}
