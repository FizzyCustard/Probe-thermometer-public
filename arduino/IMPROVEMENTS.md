# Arduino Firmware Improvements

This document catalogues improvements identified for the BBQ thermometer firmware.
The primary focus is memory management, since a cook can run for 8+ hours and the
ESP8266 has a small heap (~20–40 KB available at runtime) with a simple allocator
that is vulnerable to fragmentation from repeated allocations and frees.

---

## 1 · Memory Management (Critical)

### 1.1 — Static broadcast buffer instead of `String` in `wsBroadcast()`

**File:** `arduino/bbq-thermometer/websocket_handler.h` lines 332–334

The broadcast fires every second. Each call allocates a heap `String`, serialises
JSON into it, sends it, then frees it. Over an 8-hour cook that is **28,800 heap
allocations** — the single biggest source of heap fragmentation.

```cpp
// ❌ Current — allocates on heap every second
String out;
serializeJson(doc, out);
ws.textAll(out);

// ✅ Fix — one static buffer, zero heap allocations at runtime
static char broadcastBuf[256];
serializeJson(doc, broadcastBuf, sizeof(broadcastBuf));
ws.textAll(broadcastBuf);
```

The broadcast payload is well under 200 bytes (timestamp + two temps + cook state),
so 256 bytes is comfortably safe.

---

### 1.2 — Replace `String(float, precision).c_str()` with `dtostrf()`

**Files:**
- `arduino/bbq-thermometer/logger.h` lines 52–53
- `arduino/bbq-thermometer/cook_session.h` lines 78–79

Creating a temporary `String` just to extract a `const char*` is wasteful.
`dtostrf()` writes directly to a stack buffer.

```cpp
// ❌ Current — two heap allocations per write, every 5 s (logging) / 30 s (cook)
f.printf("%s,%s,%s\n",
         timestamp.c_str(),
         isnan(probe1C) ? "" : String(probe1C, 1).c_str(),
         isnan(probe2C) ? "" : String(probe2C, 1).c_str());

// ✅ Fix — stack only
char s1[8], s2[8];
if (!isnan(probe1C)) dtostrf(probe1C, 1, 1, s1); else s1[0] = '\0';
if (!isnan(probe2C)) dtostrf(probe2C, 1, 1, s2); else s2[0] = '\0';
f.printf("%s,%s,%s\n", timestamp, s1, s2);
```

Over 8 hours: ~5,760 log writes + ~960 cook samples = **~13,440 allocations
eliminated**.

---

### 1.3 — Shared static TX buffer for all outbound WebSocket messages

**File:** `arduino/bbq-thermometer/websocket_handler.h`

The pattern `String out; serializeJson(resp, out); client->text(out);` appears **six
times** in the message handler and config sender. WebSocket sends are processed
synchronously on a single thread; a single shared buffer is safe.

```cpp
// Add once at file scope in websocket_handler.h
static char wsTxBuf[512];  // sized for sendConfig (largest payload)

// ❌ Current — per-message heap String
String out;
serializeJson(resp, out);
client->text(out);

// ✅ Fix — reuse shared buffer
serializeJson(resp, wsTxBuf, sizeof(wsTxBuf));
client->text(wsTxBuf);
```

Adjust the 512-byte size using `ArduinoJson`'s size assistant for the config
payload (presets + favourites is the largest message).

---

### 1.4 — Replace `String` return from timestamp helpers

**File:** `arduino/bbq-thermometer/bbq-thermometer.ino` lines 45–62

Both `getTimestamp()` and `getDateStr()` already build a `char buf[]` on the stack,
then wrap it in a `String` to return it — only to have callers call `.c_str()` on
the result. This double-indirection allocates a heap `String` on every call.

```cpp
// ❌ Current — stack buf → heap String → c_str()
String getTimestamp() {
  char buf[21];
  snprintf(buf, sizeof(buf), ...);
  return String(buf);   // heap allocation
}

// ✅ Fix — write into a static buffer, return const char*
const char* getTimestamp() {
  static char buf[21];
  time_t t = timeClient.getEpochTime();
  struct tm* tm = gmtime(&t);
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec);
  return buf;
}
```

The signatures of `wsBroadcast()`, `logReading()`, `getTodayFilename()`, and
`saveCalibration()` all need to change to accept `const char*` instead of `String`.

---

### 1.5 — Replace `readStringUntil()` with a fixed-size line reader

**Files:**
- `arduino/bbq-thermometer/logger.h` lines 76–77, 117, 119–120, 186, 188–189
- `arduino/bbq-thermometer/websocket_handler.h` lines 158–171

`File::readStringUntil()` allocates a heap `String` for every line. A 32-favourite
+ 16-preset load at startup performs 48+ allocations. Reading back a cook file for
the history view allocates one `String` per data row.

```cpp
// ❌ Current
String line = f.readStringUntil('\n');
line.trim();

// ✅ Fix — stack buffer, no heap
char line[128];
size_t len = 0;
while (f.available() && len < sizeof(line) - 1) {
  char c = f.read();
  if (c == '\n') break;
  line[len++] = c;
}
line[len] = '\0';
// trim trailing \r if present
if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';
```

Extract this into a small helper (e.g. `readLine(File&, char*, size_t)`) and call
it from all five parse loops.

---

### 1.6 — Replace `String` path construction with `snprintf`

**Files:**
- `arduino/bbq-thermometer/logger.h` lines 40–41
- `arduino/bbq-thermometer/websocket_handler.h` line 150

String concatenation like `String(BBQ_LOGS) + "/log_" + dateStr + ".csv"` creates
three or four temporary heap objects per call. `getTodayFilename()` is called every
5 seconds.

```cpp
// ❌ Current — 3–4 heap allocations per call, every 5 s
String getTodayFilename(String dateStr) {
  return String(BBQ_LOGS) + "/log_" + dateStr + ".csv";
}

// ✅ Fix — stack only
void getTodayFilename(const char* dateStr, char* out, size_t len) {
  snprintf(out, len, "%s/log_%s.csv", BBQ_LOGS, dateStr);
}
```

```cpp
// ❌ Current (websocket_handler.h:150)
String fullPath = String(BBQ_COOKS) + "/" + fname;
File f = SD.open(fullPath.c_str(), FILE_READ);

// ✅ Fix
char fullPath[64];
snprintf(fullPath, sizeof(fullPath), "%s/%s", BBQ_COOKS, fname);
File f = SD.open(fullPath, FILE_READ);
```

---

### 1.7 — Remove heap `String` wrapping of inbound WebSocket data

**File:** `arduino/bbq-thermometer/websocket_handler.h` line 295

The event handler already null-terminates `data` at `data[len]`. Wrapping it in a
`String` before passing to `onWsMessage()` is an unnecessary allocation.

```cpp
// ❌ Current — allocates heap String from raw buffer
data[len] = '\0';
onWsMessage(client, String((char*)data));

// ✅ Fix — deserialise directly from the raw buffer
// Change onWsMessage signature to accept const char*
data[len] = '\0';
onWsMessage(client, (const char*)data);
```

`deserializeJson()` accepts a `const char*` directly.

---

### 1.8 — Reduce static RAM for presets and favourites

**File:** `arduino/bbq-thermometer/bbq-thermometer.ino` lines 11–12, 26–29

The two arrays consume **5,056 bytes of static RAM** on boot, regardless of how
many entries actually exist:

| Array | Elements | Bytes/element | Total |
|-------|----------|---------------|-------|
| `Favourite favs[32]` | 32 | 132 | **4,224 B** |
| `Preset presets[16]` | 16 | 52 | **832 B** |

On an ESP8266 with ~20–40 KB of runtime heap, this is meaningful. Options:

- **Quick win:** Halve `MAX_FAVS` to 16 (saves 2,112 bytes, 16 favourites is still
  generous for a BBQ thermometer).
- **Better:** Load presets on-demand from SD for the `request_config` response and
  keep only `meatTarget`, `grillLow`, `grillHigh` in RAM as runtime state. Presets
  don't change during a cook.

---

### 1.9 — Add heap monitoring

**File:** `arduino/bbq-thermometer/bbq-thermometer.ino` loop()

Without visibility into heap state it is impossible to know how close to exhaustion
the device is during a long cook. Add a periodic report:

```cpp
// In loop(), add a new interval tracker alongside lastBroadcast/lastLog
unsigned long lastHeapReport = 0;

// Inside loop():
if (now - lastHeapReport >= 60000UL) {
  lastHeapReport = now;
  Serial.printf("[MEM] free=%u B  frag=%u%%\n",
                ESP.getFreeHeap(), ESP.getHeapFragmentation());
}
```

If fragmentation climbs above ~40% or free heap drops below 4 KB, the device is at
risk of a crash on the next large allocation (e.g. config payload with 32
favourites).

---

## 2 · Stability & Robustness

### 2.1 — WiFi reconnection during a long cook

**File:** `arduino/bbq-thermometer/bbq-thermometer.ino` loop()

If the router reboots or the signal drops mid-cook, the device never reconnects —
it just silently stops broadcasting. The cook continues logging to SD (good), but
the browser loses connection with no recovery path on the firmware side.

```cpp
// Add to loop() — cheap check, only acts on disconnection
if (WiFi.status() != WL_CONNECTED) {
  Serial.println("WiFi lost — reconnecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  // Non-blocking: next loop iteration will retry broadcast when reconnected
}
```

NTPClient will resume updates automatically once WiFi is restored.

---

### 2.2 — WiFi connect timeout at startup

**File:** `arduino/bbq-thermometer/bbq-thermometer.ino` lines 82–85

The current startup WiFi loop blocks indefinitely. If the device is powered on away
from the access point it will hang forever, preventing SD logging from starting.

```cpp
// ❌ Current — infinite block
while (WiFi.status() != WL_CONNECTED) {
  delay(500);
  Serial.print(".");
}

// ✅ Fix — 30-second timeout, then continue without WiFi
uint32_t wifiStart = millis();
while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 30000UL) {
  delay(500);
  Serial.print(".");
}
if (WiFi.status() != WL_CONNECTED) {
  Serial.println("\nWiFi timeout — running in offline mode (SD logging only)");
}
```

---

### 2.3 — SD card write error handling

**Files:** `arduino/bbq-thermometer/logger.h` line 44, `cook_session.h` lines 71, 84

`logReading()` and `cookAppendReading()` return `void` and silently swallow SD
failures. During an 8-hour cook a missed write could mean losing temperature data
without any indication.

```cpp
// ✅ Change return type to bool and log failures
bool logReading(const char* filename, const char* timestamp,
                float probe1C, float probe2C) {
  ...
  File f = SD.open(filename, FILE_WRITE);
  if (!f) {
    Serial.printf("SD write failed: %s\n", filename);
    return false;
  }
  ...
  return true;
}
```

In `loop()`, count consecutive SD failures and emit a warning after 3+ misses.

---

### 2.4 — SD card re-mount recovery

**File:** `arduino/bbq-thermometer/logger.h` / `bbq-thermometer.ino`

If the SD card is briefly removed or a write error corrupts the SPI state, the
device currently has no recovery path — all subsequent SD operations silently fail.

Add a re-mount attempt when an SD operation fails:

```cpp
bool sdReady = true;  // global flag

bool ensureSD() {
  if (sdReady) return true;
  sdReady = SD.begin(SD_CS_PIN);
  if (sdReady) Serial.println("SD remounted OK");
  return sdReady;
}
```

Call `ensureSD()` at the top of each SD-touching function; set `sdReady = false` on
any `SD.open()` failure.

---

### 2.5 — NTP timestamp drift

**File:** `arduino/bbq-thermometer/bbq-thermometer.ino` line 41

`NTPClient` is configured to update every 60,000 ms. If WiFi drops for several
minutes, timestamps in the CSV log will drift. This is low severity but worth
noting: on reconnection `forceUpdate()` should be called to re-sync immediately.

```cpp
// In the WiFi reconnection handler (see 2.1):
if (justReconnected) {
  timeClient.forceUpdate();
}
```

---

## 3 · Code Quality

### 3.1 — Bounds check in `resistanceToCelsius()`

**File:** `arduino/bbq-thermometer/thermistor.h` line 44

`probeCoeffs[probeIndex]` is accessed without checking `probeIndex < 2`. Calling
with an out-of-range index is undefined behaviour (reads past the array into
adjacent memory).

```cpp
float resistanceToCelsius(float resistance, uint8_t probeIndex) {
  if (probeIndex >= 2 || resistance <= 0.0f) return NAN;
  ...
}
```

---

### 3.2 — `String` timestamp in `saveCalibration()` signature

**File:** `arduino/bbq-thermometer/logger.h` line 218

The last parameter is `String timestamp` — a heap allocation on every calibration
save. Since calibration is infrequent this is low priority, but for consistency with
the rest of the refactor it should be `const char*`.

---

### 3.3 — `millis()` overflow — confirmed safe

**Files:** `bbq-thermometer.ino`, `cook_session.h`

All timing comparisons use the correct `now - lastX >= interval` pattern which
handles the 49.7-day rollover correctly. `startMillis` is `uint32_t`; a cook
duration of 8 hours = 28,800,000 ms is well within range. No action needed —
documented here for completeness.

---

### 3.4 — `cookAppendNote()` calls `cookElapsedSeconds()` twice

**File:** `arduino/bbq-thermometer/cook_session.h` lines 88–90

A minor inefficiency: `cookElapsedSeconds()` calls `millis()` twice in quick
succession (once for `printf` in the note, once for the serial log). Cache the
result in a local variable.

```cpp
void cookAppendNote(const char* text, float meatC) {
  if (!activeCook.active) return;
  uint32_t elapsed = cookElapsedSeconds();   // call once
  File f = SD.open(activeCook.filename, FILE_WRITE);
  if (!f) return;
  f.printf("#note,%u,%s,%.1f\n", elapsed, text, meatC);
  f.close();
  Serial.printf("Note added at %us: %s\n", elapsed, text);
}
```

---

## 4 · Quick Wins Summary

Ordered by impact-to-effort ratio:

| # | Change | Effort | Heap saved over 8 h |
|---|--------|--------|---------------------|
| 1.1 | Static broadcast buffer | Low | ~28,800 allocations |
| 1.2 | `dtostrf()` for float→string | Low | ~13,440 allocations |
| 1.3 | Shared TX buffer for responses | Low | ~6 allocations/interaction |
| 1.4 | Static timestamp helpers | Low | ~32,000 allocations (broadcast + log) |
| 1.5 | Fixed-size CSV line reader | Medium | 48–200 allocations at startup/history |
| 1.6 | `snprintf` path construction | Low | ~5,760 allocations (log path) |
| 1.7 | Remove inbound `String` wrap | Low | ~N per client message |
| 1.8 | Reduce `MAX_FAVS` to 16 | Trivial | 2,112 bytes static RAM freed |
| 1.9 | Heap monitoring | Low | (observability, not savings) |
| 2.1 | WiFi reconnection | Low | (stability) |
| 2.2 | WiFi startup timeout | Low | (stability) |
| 2.3 | SD write return value | Low | (observability) |
| 2.4 | SD re-mount recovery | Medium | (stability) |
| 3.1 | Bounds check in thermistor | Trivial | (safety) |

Implementing items 1.1–1.4 alone eliminates the vast majority of heap pressure and
should be enough to make an 8-hour cook reliable.
