#pragma once

#include <stdint.h>
#include <string.h>
#include <strings.h>

// Checks applied to anything arriving over the network. Plain C strings and no
// Arduino dependencies, so tests/native can run them on a host compiler.

// Settings sanity limits (°C)
#define MEAT_TARGET_MIN    0.0f
#define MEAT_TARGET_MAX  150.0f
#define GRILL_RANGE_MIN    0.0f
#define GRILL_RANGE_MAX  400.0f

// serveStatic() hands the URL to LittleFS, which collapses "//" and resolves
// "." and "..", so the credential store is reachable under many spellings.
// Match the bare filename anywhere rather than one exact path.
inline bool isBlockedStaticUrl(const char* url) {
  return strstr(url, "wifi.json") != nullptr;
}

// Cross-site WebSocket defence: browsers do not apply CORS to WebSockets, so
// without this any web page open on the LAN could drive the device. A browser
// always sends Origin; non-browser clients send none and are allowed through.
inline bool originMatchesHost(const char* origin, const char* host) {
  if (origin == nullptr || origin[0] == '\0') return true;
  const char* sep = strstr(origin, "://");
  if (sep == nullptr) return false;
  return strcasecmp(sep + 3, host) == 0;
}

// Favourite ids land in a CSV field and the UI. The UI generates "f-<digits>";
// accept only that alphabet so an id can't add columns, or be cut short by the
// 24-byte Favourite::id and become undeletable.
inline bool isSafeFavId(const char* id) {
  size_t len = strlen(id);
  if (len == 0 || len >= 24) return false;
  for (const char* p = id; *p; p++) {
    bool ok = (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z') || *p == '-' || *p == '_';
    if (!ok) return false;
  }
  return true;
}

// Written as positive ranges so NaN fails every comparison and is rejected.
inline bool targetsValid(float meat, float grillLow, float grillHigh) {
  return meat >= MEAT_TARGET_MIN && meat <= MEAT_TARGET_MAX &&
         grillLow >= GRILL_RANGE_MIN && grillHigh <= GRILL_RANGE_MAX &&
         grillLow < grillHigh;
}

// Bounds the notes in a cook_data response; returns false once `max` are taken.
inline bool takeNoteSlot(uint16_t& taken, uint16_t max) {
  if (taken >= max) return false;
  taken++;
  return true;
}

// Compile-time string compare, for rejecting config.h placeholders.
constexpr bool sameStr(const char* a, const char* b) {
  return *a == *b && (*a == '\0' || sameStr(a + 1, b + 1));
}
