# BBQ Thermometer — Firmware Spec

## Platform

| Item | Detail |
|---|---|
| Board | AZDelivery D1 Mini (ESP8266-12F) |
| Framework | Arduino (Arduino IDE 2.x or PlatformIO) |
| Board package | `esp8266/Arduino` — install via Arduino IDE Boards Manager |
| Language | C++ (.ino sketch + .h headers) |

---

## Sketch Structure

```
arduino/
  bbq-thermometer/
    config.example.h    Template — copy to config.h and fill in credentials
    config.h            Your values — gitignored, never committed
    bbq-thermometer.ino Main sketch — WiFi, server, loop
    thermistor.h        Steinhart-Hart resistance → temperature + calibration
    logger.h            SD card CSV logging + calibration JSON save/load
    websocket_handler.h WebSocket push and receive (settings + calibration)
```

---

## Library Dependencies

Install all via Arduino IDE Library Manager or `platformio.ini`.

| Library | Author | Purpose |
|---|---|---|
| `ESP8266WiFi` | ESP8266 core (built-in) | WiFi connection |
| `ESPAsyncWebServer` | Me-No-Dev | Async HTTP + WebSocket server |
| `ESPAsyncTCP` | Me-No-Dev | Required by ESPAsyncWebServer |
| `Adafruit ADS1X15` | Adafruit | ADS1115 I2C ADC driver |
| `ArduinoJson` | Bblanchon | JSON serialisation for WebSocket payloads |
| `SD` | Arduino (built-in) | SD card read/write |
| `LittleFS` | ESP8266 core (built-in) | Serve web UI files from onboard flash |
| `NTPClient` | Fabrice Weinberg | NTP time sync over WiFi |
| `WiFiUdp` | ESP8266 core (built-in) | Required by NTPClient |

> **Note:** ESPAsyncWebServer and ESPAsyncTCP are not in the standard Library
> Manager. PlatformIO installs them from GitHub via `lib_deps` in
> `platformio.ini`. In the Arduino IDE, install them manually from:
> - https://github.com/me-no-dev/ESPAsyncWebServer
> - https://github.com/me-no-dev/ESPAsyncTCP

---

## Configuration (`config.h`)

Copy `config.example.h` to `config.h` before flashing. The file is
gitignored so WiFi credentials are never committed.

Key values to set:
- `WIFI_SSID` / `WIFI_PASSWORD`
- `NTP_UTC_OFFSET_SEC` (e.g. `3600` for GMT+1)
- `PROBE1_SH_A/B/C` and `PROBE2_SH_A/B/C` — Steinhart-Hart defaults
  (overridden at runtime when calibration data is saved to SD)

---

## Hardware & Pin Assignments

### ADS1115 (I2C)

| ADS1115 Pin | D1 Mini Pin |
|---|---|
| VDD | 3.3V |
| GND | GND |
| SCL | D1 (GPIO5) |
| SDA | D2 (GPIO4) |
| A0 | Probe 1 — Meat (via 100kΩ voltage divider) |
| A1 | Probe 2 — Grill (via 100kΩ voltage divider) |
| ADDR | GND (sets I2C address to 0x48) |

### SD Card Module (SPI)

| SD Pin | D1 Mini Pin |
|---|---|
| +5V | 5V (module has onboard 3.3V regulator + level shifters) |
| GND | GND |
| SCK | D5 (GPIO14) |
| MISO | D6 (GPIO12) |
| MOSI | D7 (GPIO13) |
| SS (CS) | D8 (GPIO15) |

### Probe Voltage Divider (per probe)

```
3.3V ──── 100kΩ ──── ADS1115 channel ──── NTC probe ──── GND
```

The 3.5mm jack Tip is the signal line; Sleeve is GND.

---

## Thermistor Module (`thermistor.h`)

### Equation

Uses the **full 3-coefficient Steinhart-Hart equation** for best accuracy:

```
1/T(K) = A + B·ln(R) + C·(ln(R))³
```

This fits a probe's calibration points more closely than the simpler
B-parameter equation, which matters for the meat probe where hitting a precise
target matters. Resolution is set by the ADS1115 (16-bit, ±4.096 V at
GAIN_ONE, about 0.125 mV per count). The temperature accuracy actually achieved
depends on the individual probe, the divider resistor tolerance, and
calibration; this spec does not guarantee a figure.

### ADC → Resistance

```
R_ntc = R_series × V_adc / (V_supply − V_adc)
```

where `V_adc = (raw_counts / 32767) × 4.096` (ADS1115 GAIN_ONE).

### Coefficient Storage

`SHCoeffs probeCoeffs[2]` holds A/B/C for both probes. Populated from
`config.h` defaults on boot; overwritten when `loadCalibration()` finds
`/calibration.json` on the SD card.

### Coefficient Calculation

`calcSHCoeffs(t1c, r1, t2c, r2, t3c, r3, &out)` computes A/B/C from three
known (temperature °C, resistance Ω) pairs using the standard algorithm:

```
gamma2 = (Y2−Y1)/(L2−L1)
gamma3 = (Y3−Y1)/(L3−L1)
C = (gamma3−gamma2) / ((L3−L2)·(L3+L2+L1))
B = gamma2 − C·(L2²+L2·L1+L1²)
A = Y1 − (B + C·L1²)·L1
```

where `Li = ln(Ri)`, `Yi = 1/Ti(K)`.

> **Note:** coefficient calculation is also mirrored in `device/data/app.js`
> (`calcSHCoeffs()`). The browser performs the calculation and sends the
> result to the firmware, keeping the arithmetic off the ESP8266.

### Default Coefficients

Estimated values for a 100kΩ NTC B3950 (Morpilot probes):

| Coefficient | Value |
|---|---|
| A | 4.475 × 10⁻⁴ |
| B | 2.523 × 10⁻⁴ |
| C | 1.701 × 10⁻⁹ |

Run the calibration tool for probe-specific accuracy.

---

## Logger Module (`logger.h`)

### Temperature CSV

- File: `/log_YYYY-MM-DD.csv` — new file per calendar day
- Written every 5 seconds
- Format: `timestamp,probe1_c,probe2_c`

### Calibration JSON (`/calibration.json`)

Saved and loaded by the logger. Format:

```json
{
  "probe0": {
    "A": 4.475e-4, "B": 2.523e-4, "C": 1.701e-9,
    "calibrated": "2025-07-14T14:30:00",
    "points": [
      {"temp_c": 0.0,   "resistance": 336400.0},
      {"temp_c": 25.0,  "resistance": 100000.0},
      {"temp_c": 100.0, "resistance": 6960.0}
    ]
  },
  "probe1": { ... }
}
```

`loadCalibration()` is called in `setup()` after `loggerBegin()`. If the
file exists it overwrites `probeCoeffs[]` with the saved values.

---

## WebSocket Handler (`websocket_handler.h`)

### Outbound (ESP8266 → browser, every 1 second)

```json
{
  "ts": "2025-07-14T14:32:05",
  "meat":  { "c": 67.4,  "r": 9210 },
  "grill": { "c": 182.3, "r": 612 },
  "meat_target": 74.0,
  "grill_low":   160.0,
  "grill_high":  200.0
}
```

`r` is the median-of-3 resistance behind `c`, in whole ohms (`null` when the
probe is disconnected). It is what the calibration wizard watches for
settling, so there is no separate "read resistance" request.

### Inbound: settings update

```json
{ "type": "settings", "meat_target": 80.0, "grill_low": 150.0, "grill_high": 210.0 }
```

### Inbound: calibration — save reference points

The browser sends raw (temperature, resistance) reference points for either
or both probes in one message. It never sends coefficients: they are derived
and validated on the device.

```json
{
  "type": "cal_save",
  "timestamp": "2026-09-11T14:30:00Z",
  "probes": [
    { "probe": 0, "points": [
        {"temp_c": 0.0,   "resistance": 336210},
        {"temp_c": 25.0,  "resistance": 100000},
        {"temp_c": 100.0, "resistance": 6960} ] },
    { "probe": 1, "points": [ "..." ] }
  ]
}
```

The handler only parses and copies the points into `pendingCal`; the work
runs from `wsTick()` in `loop()` because the SD write blocks. There
`calibrateFromPoints()` (thermistor.h) checks every probe's points:

| Reason | Meaning |
|---|---|
| `bad_input` | non-finite / non-positive resistance, or temperature outside −50…350 °C |
| `temps_too_close` | two reference temperatures within 0.5 °C |
| `not_monotonic` | resistance does not fall as temperature rises — a probe was not in the bath |
| `degenerate` | the maths produced a non-finite coefficient |
| `roundtrip` | a point converts back more than ±3 °C from its reference |

A probe that passes is applied to `probeCoeffs[]` at once and recorded in
`probeCal[]`; then `saveCalibrationFile()` rewrites `/calibration.json` from
RAM in a single write. The reply carries one result per probe:

```json
{ "type": "cal_saved", "calibrated": "2026-09-11T14:30:00",
  "results": [
    { "probe": 0, "applied": true,  "saved": true },
    { "probe": 1, "applied": false, "saved": false, "reason": "not_monotonic", "point": 1 }
  ] }
```

`applied: true, saved: false, reason: "sd_write_failed"` means the new curve
is in use until reboot but could not be persisted. `timestamp` is replaced by
the device clock when NTP is available; in AP mode the browser's value is
character-whitelisted before it is stored.

### Config: calibration status

`config` (sent on connect / `request_config`) includes:

```json
"calibration": {
  "0": { "source": "sd", "calibrated": "2026-09-11T14:30:00",
         "points": [ {"temp_c": 0, "resistance": 336210}, "..." ] },
  "1": { "source": "default", "calibrated": null }
}
```

---

## Calibration Workflow

The web UI runs a four-step wizard that calibrates **both probes at once**,
one bath at a time. `device/data/app.js`, "Calibration wizard" section.

1. **Ice bath (0 °C)**, **room temperature (~25 °C, against a reference
   thermometer)**, **boiling water (100 °C at sea level)**. On each step both
   probes go in the same bath; the wizard shows each probe's live °C and Ω
   from the tick, and a settling/stable pill computed from the spread of the
   last 8 readings. One shared reference-temperature input per bath.
2. **Capture** stores the *mean* resistance of the buffered readings for every
   connected probe, tagged with the reference temperature. Capturing while
   still settling is allowed but flagged. An unplugged probe is skipped.
3. **Review & save** shows each probe's three points, the coefficients they
   yield, the round-trip fit per point, the previous calibration's resistances
   for comparison, and a live "would read X right now" preview. **Save** sends
   one `cal_save` for every probe with three valid points.
4. Progress (step, reference temperatures, captured points) is kept in
   `localStorage` under `bbq_cal_draft`, so a reload mid-calibration resumes
   where it left off. The draft is cleared on a successful save.

The collapsed header shows per-probe status from the `config` message:
"Meat ✓ 11 Sep 2026 · Grill defaults".

---

## Main Sketch (`bbq-thermometer.ino`)

### Boot sequence (`setup()`)

1. Start Serial (115200 baud)
2. Mount LittleFS — serve web UI files
3. Connect to WiFi — block until connected, print IP to Serial
4. Sync time via NTP
5. Initialise ADS1115 over I2C
6. `loggerBegin()` — mount SD card
7. `loadCalibration()` — load saved coefficients from SD if present
8. `wsBegin(&server)` — register WebSocket endpoint at `/ws`
9. Register static file handler (serves LittleFS for all other routes)
10. Start server on port 80

### Main loop (`loop()`)

- Every **1 second**: read both probes → `wsBroadcast()`
- Every **5 seconds**: `logReading()` → append CSV row
- Every loop: `ws.cleanupClients()`

---

## LittleFS — Web UI Files

Upload the `device/data/` folder using the LittleFS Upload Tool (Arduino IDE)
or `pio run --target uploadfs` (PlatformIO).

The `device/data/` folder is the web UI served from LittleFS, and the only
copy — `platformio.ini` points `data_dir` at it.

### Combined firmware + filesystem upload

Firmware lives in one flash partition; the LittleFS web UI lives in another.
A normal `pio run -t upload` only flashes firmware, so frontend changes in
`device/data/` would otherwise stay stale on the device.

`scripts/upload_all.py` is wired in via `extra_scripts` in `platformio.ini`
and registers a post-action on the `upload` target that runs `uploadfs`
immediately after the firmware flash. After this:

- `pio run -t upload` (and the PIO IDE Upload button) flash **both**
  partitions in one go — firmware first, LittleFS second.
- `pio run -t uploadfs` on its own still works for FS-only iteration.

To opt out for a firmware-only flash (rare), comment out the `extra_scripts`
line in `platformio.ini` for that run.

---

## Open Tasks

- [x] Implement `bbq-thermometer.ino` boot sequence and main loop
- [x] Implement `readResistance()` in `thermistor.h`
- [x] Implement `readCelsius()` / `probeConnected()` wrappers
- [x] Implement `loggerBegin()` and `logReading()` in `logger.h`
- [x] Implement `wsBroadcast()` JSON serialisation in `websocket_handler.h`
- [x] Wire up NTP timestamp formatting
- [x] Web UI: WebSocket live mode
- [x] Fix iOS keyboard scroll bug (see `BUG-ios-keyboard-scroll.md`)
- [ ] Test calibration end-to-end with real probes (ice water + boiling water)
- [ ] Test SD logging over a full cook session
- [x] Add mDNS (`bbq.local`) and OTA updates
- [x] Persist target settings to SD (`/BBQ/config/settings.csv`)
