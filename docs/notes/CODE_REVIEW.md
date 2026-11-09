# BBQ Thermometer — Firmware Code Review

A correctness / robustness / async-safety review of the ESP8266 firmware in
`arduino/bbq-thermometer/`. Scope is **firmware only** (the web UI is excluded).

Every item below was checked against the current source. False positives raised
during the scan are listed at the end under **Rejected / non-issues** so you can
see what was inspected and dismissed, and why.

This complements [`IMPROVEMENTS.md`](IMPROVEMENTS.md) (which focuses on RAM/heap/performance).
Items already detailed there are only cross-referenced here, not repeated.

No code was changed — this is a review only.

> **Status update (2026-07-05, the FIRMWARE-REVIEW.txt fix
> pass):** most items below are now resolved; see the Status column. Line
> numbers in this document refer to the code as it was at review time and have
> since shifted. Still open: **#5** (non-atomic config writes), **#7** (unpinned
> library versions), **#13** (fragmented WS messages — documented latent limit),
> and the JSON-size half of **#8**.

---

## Summary

| # | Severity | Issue | Location | Status |
|---|----------|-------|----------|--------|
| 1 | High | WiFi connect blocks forever at boot (no timeout) | `bbq-thermometer.ino:82-85` | ✅ Fixed earlier (15 s timeout + AP fallback) |
| 2 | High | No WiFi reconnection in `loop()` | `bbq-thermometer.ino:130-161` | ✅ Fixed — `WiFi.setAutoReconnect(true)`; AP mode retries saved creds every 60 s |
| 3 | High | Blocking SD I/O inside async WebSocket callbacks | `websocket_handler.h:139-294` | ✅ Fixed earlier (deferred queue); later extended to WiFi scans + head/tail chunk reads |
| 4 | High | ADS1115 init failure is ignored; garbage temps logged | `bbq-thermometer.ino:95-101`, `loop()` | ✅ Fixed earlier (`adsReady` gate) |
| 5 | Medium | Non-atomic config writes (remove-then-write) | `logger.h` (multiple) | ⏳ Open |
| 6 | Medium | NTP sync never validated before timestamping | `bbq-thermometer.ino:88-91` | ✅ Fixed — `ntpReady()` gates logging; cook timestamps prefer device NTP |
| 7 | Medium | Unpinned library versions | `platformio.ini:22-26` | ⏳ Open |
| 8 | Medium | Cook-list response unbounded in size | `websocket_handler.h:134-185` | 🟡 Partial — per-file cost now O(1) via head/tail reads, cook-data capped at 150 points; list JSON still grows with cook count |
| 9 | Low | No `probeIndex` bounds check in `resistanceToCelsius` | `thermistor.h:44-50` | ✅ Fixed |
| 10 | Low | `1.0f/invT` not guarded against 0 / non-finite | `thermistor.h:48-49` | ✅ Fixed (`invT <= 0` → `NAN`) |
| 11 | Low | Firmware `calcSHCoeffs` can divide by zero (equal R) | `thermistor.h:86-108` | ✅ Fixed — equal-R rejection + `isfinite` check; path is now live (firmware computes coefficients itself) |
| 12 | Low | `cal_save` reads `points[0..2]` without validating array | `websocket_handler.h:326-331` | ✅ Fixed — presence-checked, plus round-trip sanity check |
| 13 | Low | WS handler silently drops fragmented messages | `websocket_handler.h:412-420` | ⏳ Open (latent limit; all current messages are single-frame) |
| 14 | Low | Config hygiene (placeholder SSID, no target clamping) | `config.example.h`, `websocket_handler.h:30-38` | ✅ Fixed — placeholder-SSID check earlier; range clamps |

---

## High severity

> **All four high-severity items below were fixed** (boot timeout + offline
> continue, background reconnect with NTP re-sync, ADS1115 ready-flag gate, and
> deferral of the heavy cook-history reads from the async callback to `loop()`).
> The original descriptions are retained for context.

### 1. WiFi connect blocks forever at boot

`bbq-thermometer.ino:82-85`

```cpp
while (WiFi.status() != WL_CONNECTED) {
  delay(500);
  Serial.print(".");
}
```

There is no timeout. If the access point is down, out of range, or the password is
wrong when the device powers on, `setup()` never returns and the device hangs — it
never initialises the ADC or SD card and never reads a single probe. For a
standalone BBQ device that may be powered on next to the grill before the house
WiFi is reachable, this is the most user-visible failure mode.

**Direction:** bound the loop (e.g. ~20 s), then continue boot anyway so probe
reading/logging still work offline. Optionally fall back to a SoftAP so the UI is
reachable without infrastructure WiFi. (Related: `IMPROVEMENTS.md` 2.2.)

### 2. No WiFi reconnection in `loop()`

`bbq-thermometer.ino:130-161`

The main loop never checks `WiFi.status()`. If the connection drops mid-cook, the
ESP8266 core will not necessarily restore it, `wsBroadcast()` quietly stops
reaching browsers, and NTP stops updating — all with no recovery and no log. SD
logging continues, but the live UI goes dark for the rest of the cook.

**Direction:** periodically check the link and call `WiFi.reconnect()` (with
backoff) when down; on reconnect, re-`forceUpdate()` NTP. (Related: `IMPROVEMENTS.md`
2.1 / 2.5.)

### 3. Blocking SD I/O inside async WebSocket callbacks

`websocket_handler.h:139-185` (`request_cook_list`) and `188-294`
(`request_cook_data`)

These handlers run inside the ESPAsyncWebServer event callback, i.e. in the async
TCP context, not from `loop()`. Both do substantial **blocking** work there:

- Open files and read them line-by-line with `readStringUntil('\n')`.
- `request_cook_list` opens **every** cook file and reads it **in full** just to
  extract the header/metadata and the last elapsed time — O(files × rows).

```cpp
File dir = SD.open(BBQ_COOKS);
while (true) {
  File entry = dir.openNextFile();
  ...
  while (f.available()) {                 // full-file scan, per cook file
    String line = f.readStringUntil('\n');
    ...
  }
}
```

Long blocking operations in the async callback can starve the TCP stack and trip
the ESP8266 soft watchdog, causing a reset — and the risk grows with every cook
the user records.

**Direction:** maintain a small index file (one metadata line per cook, updated on
`cookStart`/`cookStop`) so the list handler doesn't open every file; and/or move
the heavy reads out of the callback (defer to `loop()`), and cap work per call.
This is the headline structural issue in the WebSocket layer.

### 4. ADS1115 init failure is ignored

`bbq-thermometer.ino:95-101`

```cpp
if (!ads.begin(ADS1115_I2C_ADDRESS)) {
  Serial.println("ERROR: ADS1115 not found");
  ...
} else {
  ads.setGain(GAIN_ONE);
}
```

The failure is logged but no state records it. `loop()` then calls
`readResistance()` → `ads.readADC_SingleEnded()` unconditionally, so if the ADC is
missing or the I2C wiring is bad, the device reads garbage and broadcasts/logs it
as if it were a real temperature.

**Direction:** store a global `adsReady` flag from `ads.begin()` and gate reads on
it, emitting `NAN` (which already serialises to JSON `null` and is skipped by the
loggers) when the ADC isn't available.

---

## Medium severity

### 5. Non-atomic config writes (remove-then-write)

`logger.h`: `saveSettings` (59-65), `savePresets` (101-110), `saveFavourites`
(167-179), `saveCalibration` (243-247)

Each follows the pattern:

```cpp
SD.remove(SETTINGS_FILE);
File f = SD.open(SETTINGS_FILE, FILE_WRITE);
if (!f) return;
...write...
```

The old file is deleted *before* the new one is written. A power loss (not unusual
on a battery/USB rig near a grill) between the `remove` and a successful write
leaves the config **gone**, not merely stale. `saveCalibration` is the most painful
to lose since it represents real calibration effort.

**Direction:** write to a temp file, then rename over the target (atomic-ish), or
at minimum write the new content before removing the old.

### 6. NTP sync never validated before timestamping

`bbq-thermometer.ino:88-91`

```cpp
timeClient.begin();
timeClient.forceUpdate();
Serial.printf("NTP synced: %s\n", getTimestamp().c_str());
```

`forceUpdate()`'s return value is ignored. If NTP fails (common when WiFi is flaky
at boot), `getEpochTime()` returns a near-1970 value. Every cook file is then named
`cook_1970-01-01T...csv` and all CSV rows are misdated — and because filenames are
derived from the timestamp, repeated boots can collide.

**Direction:** check that the epoch is plausible (e.g. > year 2024) before using it
for filenames/log rows; retry NTP, or mark timestamps as provisional until synced.

### 7. Unpinned library versions

`platformio.ini:22-26`

```ini
lib_deps =
    https://github.com/me-no-dev/ESPAsyncTCP
    https://github.com/me-no-dev/ESPAsyncWebServer
    adafruit/Adafruit ADS1X15@^2.6.0
    bblanchon/ArduinoJson@^7.0.0
    arduino-libraries/NTPClient@^3.2.1
```

The two async libraries are pulled from `master` with no tag or commit hash, and
the rest use caret ranges. Builds are not reproducible: a future push to either
me-no-dev repo (or a new minor of ArduinoJson) can change behaviour or break the
build with no local change. Given the firmware leans heavily on ESPAsyncWebServer,
this is worth locking down.

**Direction:** pin the GitHub deps to a specific tag or commit (`#<ref>`), and
consider `==`/tighter ranges for the registry libs once a known-good set is found.

### 8. Cook-list response unbounded in size

`websocket_handler.h:134-185`

`request_cook_list` accumulates every cook's metadata into a single `JsonDocument`
and serialises it into one `String out`. On a heap-constrained ESP8266, a long cook
history produces a large allocation that can fail or fragment the heap. (This is
the memory-side companion to issue #3's CPU-side cost.)

**Direction:** paginate the list, or cap it with a logged note when truncated so
the cap isn't silent.

---

## Low / polish

### 9. No `probeIndex` bounds check in `resistanceToCelsius`

`thermistor.h:44-50` indexes `probeCoeffs[probeIndex]` (size 2) with no guard.
Currently safe — all call sites pass channel 0/1 and the `cal_*` handlers reject
`probe > 1` — but a cheap `if (probeIndex > 1) return NAN;` future-proofs it.

### 10. `1.0f / invT` not guarded

`thermistor.h:48-49`:

```cpp
float invT = c.A + c.B * L + c.C * L * L * L;
return (1.0f / invT) - 273.15f;
```

With pathological/zeroed coefficients `invT` can be 0 or negative, yielding `inf`
or a nonsensical temperature that then propagates into logs and the UI. Real-world
risk is low (valid coefficients keep `invT` positive over the probe range), but a
`isfinite()` / positivity check would harden it.

### 11. Firmware `calcSHCoeffs` can divide by zero

`thermistor.h:86-108` rejects equal *temperatures* but not equal *resistances*. If
two reference resistances are identical, `L2 - L1 == 0` and `gamma2` divides by
zero; the result is never checked for finiteness before being returned. Note: per
`FIRMWARE-SPEC.md` the **browser** computes coefficients and the firmware just
stores them, so this code path may be dead — confirm before investing effort. If
kept, add `L1==L2 || L2==L3 || L1==L3` rejection and an `isfinite` output check.

### 12. `cal_save` reads `points[0..2]` without validating the array

`websocket_handler.h:326-331` assumes three points exist. In ArduinoJson v7 a
missing element resolves to 0 rather than crashing, so a malformed message just
stores zeroed reference points (cosmetic, since A/B/C themselves come straight from
the message). Worth a length check if you want clean rejection.

### 13. WS handler silently drops fragmented messages

`websocket_handler.h:412-420` only processes single-frame text where
`info->final && info->index == 0 && info->len == len`. Multi-frame / fragmented
frames are dropped with no error. All current inbound messages are small, so this
never triggers today — note it as a latent limit if message sizes ever grow.

### 14. Config hygiene

- `config.example.h:10-11` ships placeholder `WIFI_SSID "YOUR_SSID"`. A boot-time
  check that the SSID isn't the placeholder would give a clear early error instead
  of silently failing to connect.
- `websocket_handler.h:30-38` applies `meat_target` / `grill_low` / `grill_high`
  straight from the client with no firmware-side range clamp. The UI constrains
  input, but a direct WebSocket client could set absurd values. A defensive clamp
  is cheap.

---

## Already covered in `IMPROVEMENTS.md`

These are real but already tracked there — not repeated in detail:

- String/heap churn in hot paths (`getTimestamp()`, `String(float)`, path
  concatenation, `readStringUntil()`).
- Shared TX buffer for WebSocket responses; reduce `MAX_FAVS` static RAM.
- Periodic heap / fragmentation monitoring.
- SD write-error handling (return `bool` instead of `void`), SD re-mount recovery.
- `cookAppendNote()` calling `cookElapsedSeconds()` twice (minor).

---

## Rejected / non-issues

Flagged during the scan but **not** real after checking the code:

- **"`JsonDocument` defaults to 192 bytes → overflow."** False. This project uses
  ArduinoJson **v7**, where `JsonDocument` is elastic and heap-backed; it grows as
  needed. There is no fixed-capacity overflow. (The real cost is heap *usage* —
  see issue #8 and `IMPROVEMENTS.md`.)
- **"`millis()` overflow corrupts loop timers / `cookElapsedSeconds`."** False.
  Unsigned subtraction (`now - last`) is wrap-safe for the intervals used. It would
  only matter for a single continuous cook longer than ~49.7 days.
- **"`substring(15)` reads past the end of the string."** False. Arduino's
  `String::substring` clamps out-of-range indices and returns `""`.
- **"`NTP_UTC_OFFSET_SEC` comment is misleading."** False. `3600` seconds = GMT+1
  is correct as written (`config.example.h:16`).
- **"Division by zero in the voltage divider."** False. `readResistance` returns
  early when `voltage >= SUPPLY_VOLTAGE`, so the denominator is never zero
  (`thermistor.h:35-37`).
- **"`log()` of zero/negative resistance."** False. Guarded by
  `if (resistance <= 0.0f) return -999.0f;` (`thermistor.h:45`).
