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

> **Note:** ESPAsyncWebServer and ESPAsyncTCP must be installed manually from
> GitHub — they are not in the standard Library Manager.
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
| VCC | 3.3V |
| GND | GND |
| CLK | D5 (GPIO14) |
| MISO | D6 (GPIO12) |
| MOSI | D7 (GPIO13) |
| CS | D8 (GPIO15) |

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

This is more accurate than the simpler B-parameter equation (±0.1°C vs ±2°C
over the calibrated range), especially important for the meat probe where
hitting a precise target matters.

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

> **Note:** coefficient calculation is also mirrored in `data/app.js`
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
  "meat":  { "c": 67.4 },
  "grill": { "c": 182.3 },
  "meat_target": 74.0,
  "grill_low":   160.0,
  "grill_high":  200.0
}
```

### Inbound: settings update

```json
{ "type": "settings", "meat_target": 80.0, "grill_low": 150.0, "grill_high": 210.0 }
```

### Inbound: calibration — read resistance

Browser requests a live resistance reading on a probe channel:

```json
{ "type": "cal_read", "probe": 0 }
```

ESP8266 responds:

```json
{ "type": "cal_reading", "probe": 0, "resistance": 336400.0 }
```

### Inbound: calibration — save coefficients

After all 3 points are recorded the browser calculates A/B/C and sends:

```json
{
  "type": "cal_save",
  "probe": 0,
  "A": 4.475e-4, "B": 2.523e-4, "C": 1.701e-9,
  "points": [
    {"temp_c": 0.0,   "resistance": 336400.0},
    {"temp_c": 25.0,  "resistance": 100000.0},
    {"temp_c": 100.0, "resistance": 6960.0}
  ],
  "timestamp": "2025-07-14T14:30:00"
}
```

ESP8266 applies the coefficients immediately, saves to SD, responds:

```json
{ "type": "cal_saved", "probe": 0, "saved": true }
```

---

## Calibration Workflow

1. Open the web UI, expand the **Probe Calibration** section.
2. For each probe, place it in three reference media at known temperatures.
   Recommended points: **0°C** (ice water), **25°C** (room temp with a
   reference thermometer), **100°C** (boiling water at sea level).
3. Enter the known temperature, click **Record** — the browser sends a
   `cal_read` message and displays the measured resistance.
4. Once all 3 points are recorded, click **Calculate & Save**.
   - Browser computes A/B/C using the Steinhart-Hart algorithm.
   - Sends a `cal_save` message to the firmware.
   - Firmware saves `/calibration.json` to SD and immediately starts
     using the new coefficients.
5. The calculated A/B/C values are shown on screen for reference.

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

Upload the `data/` folder using the LittleFS Upload Tool (Arduino IDE)
or `pio run --target uploadfs` (PlatformIO).

Before uploading, update `data/app.js` to connect via WebSocket instead
of using the simulation. See the open tasks below.

---

## Open Tasks

- [ ] Implement `bbq-thermometer.ino` boot sequence and main loop
- [ ] Implement `readResistance()` in `thermistor.h`
- [ ] Implement `readCelsius()` / `probeConnected()` wrappers
- [ ] Implement `loggerBegin()` and `logReading()` in `logger.h`
- [ ] Implement `wsBroadcast()` JSON serialisation in `websocket_handler.h`
- [ ] Wire up NTP timestamp formatting
- [ ] Update `data/app.js` to use real WebSocket instead of simulation
- [ ] Fix iOS keyboard scroll bug (see `BUG-ios-keyboard-scroll.md`)
- [ ] Test calibration end-to-end with real probes (ice water + boiling water)
- [ ] Test SD logging over a full cook session
- [ ] Add mDNS (`bbq.local`) — v2
- [ ] Persist target settings to EEPROM — v2
