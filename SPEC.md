# BBQ Wireless Probe Thermometer — Project Spec

## Overview

A wireless BBQ thermometer system built on an ESP8266 D1 Mini. Two NTC thermistor probes (meat and grill) are read via an ADS1115 ADC module and served as a live web dashboard over WiFi. Data is logged to an SD card. The device is battery powered and accessible to anyone on the same local network — no cloud, no app, just open a browser.

---

## Software

- We want to make sure we are using coding best practices.
- We have to make sure we are not going to have any techical debt
- We should be proud of this code it should be seen as well thought out and put together projject

**`device/data/` is the web interface — the single source of truth, served
from LittleFS on the device. (The Netlify demo site that used to live in
`data/` has been removed; the live device replaces it.)**

---

## Hardware

### Core Board

| Component | Detail |
|---|---|
| **MCU** | AZDelivery D1 Mini (ESP8266-12F) |
| **Flash** | 4MB onboard (SPIFFS for web UI files) |
| **WiFi** | 802.11 b/g/n, 2.4GHz |
| **Power** | USB or battery pack via onboard regulator |

### Peripherals

| Component | Purpose | Interface |
|---|---|---|
| ADS1115 module | 4-channel 16-bit ADC — reads both probes | I2C (D1/D2) |
| SD card module | Logs temperature data with timestamps | SPI (D5/D6/D7/D8) |
| 2x 3.5mm stereo jack sockets (panel mount) | Probe connectors | Analogue signal |
| 2x 100kΩ resistors | Voltage divider per probe | — |
| Battery pack | Portable power | — |
| Enclosure | Project box or 3D print | — |

### Probes

- **Type:** Morpilot NTC thermistor probes, 3.5mm TRS connector
- **Resistance at 25°C:** 100kΩ
- **B-value:** ~3950K
- **Max rated temp:** ~300°C (suitable for BBQ and oven use)
- **Count:** 2
  - Probe 1 — Meat
  - Probe 2 — Grill / ambient

---

## Wiring

### Probe Voltage Divider (per probe)

```
3.3V ──── 100kΩ ──── ADC input (ADS1115 channel) ──── Probe (NTC) ──── GND
```

The 3.5mm jack Tip carries the thermistor signal; Sleeve is GND.

### Pin Assignments

| D1 Mini Pin | Connected To |
|---|---|
| D1 (SCL) | ADS1115 SCL |
| D2 (SDA) | ADS1115 SDA |
| D5 (CLK) | SD card CLK |
| D6 (MISO) | SD card MISO |
| D7 (MOSI) | SD card MOSI |
| D8 (CS) | SD card CS |
| 3.3V | ADS1115 VDD, SD card VCC |
| GND | ADS1115 GND, SD card GND, probe sleeves |

### ADS1115 Channel Map

| ADS1115 Channel | Probe |
|---|---|
| A0 | Probe 1 — Meat |
| A1 | Probe 2 — Grill |

---

## Firmware

### Platform

- **Framework:** Arduino (via Arduino IDE or PlatformIO)
- **Board package:** ESP8266 Arduino Core

### Key Libraries

| Library | Purpose |
|---|---|
| `ESP8266WiFi` | WiFi connection |
| `ESPAsyncWebServer` | Async HTTP + WebSocket server |
| `Adafruit_ADS1X15` | ADS1115 driver |
| `SD` | SD card read/write |
| `LittleFS` / `SPIFFS` | Serve web UI files from flash |
| `ArduinoJson` | JSON serialisation for WebSocket messages |
| `TimeLib` / NTP | Timestamps (sync via NTP on boot) |

### Core Loop

1. On boot: connect to WiFi, sync time via NTP, mount SPIFFS and SD card, start web server and WebSocket handler.
2. Every **1 second**: read both ADS1115 channels, convert resistance → °C using Steinhart-Hart equation, push JSON payload to all connected WebSocket clients.
3. Every **5 seconds**: append a CSV row to the SD card log file.
4. Web server serves static UI files from SPIFFS on port 80.

### Temperature Conversion

Uses the Steinhart-Hart simplified (B-parameter) equation:

```
1/T = 1/T₀ + (1/B) × ln(R/R₀)
```

Where:
- T₀ = 298.15K (25°C)
- R₀ = 100,000Ω
- B = 3950K
- R = measured resistance from voltage divider

### SD Card Logging

All data is written under a `/BBQ/` root folder on the SD card. This keeps the card organised and means a factory reset of the device never wipes historical data.

#### Folder structure

```
/BBQ/
  logs/
    log_2025-07-14.csv        ← rolling temperature log, one file per day
    log_2025-07-15.csv
  cooks/
    cook_2025-07-14T08-14-00.csv  ← one file per cook session
    cook_2025-07-14T19-05-00.csv
  config/
    settings.csv              ← last-saved targets (persists across reboots)
    presets.csv               ← user-defined preset temperatures
    favourites.csv            ← saved favourite cook configs
```

#### CSV formats

**`/BBQ/logs/log_YYYY-MM-DD.csv`** — temperature log, appended every 5 s while device is on:
```
timestamp,meat_c,grill_c
2025-07-14T14:32:05,67.4,182.3
```

**`/BBQ/cooks/cook_<ISO-datetime>.csv`** — one row per 30-second sample during an active cook session, plus header metadata rows prefixed with `#`:
```
#name,Sunday Brisket
#preset,Brisket / pulled pork
#meat_target_c,95
#grill_low_c,105
#grill_high_c,130
#started_at,2025-07-14T08:14:00
elapsed_s,meat_c,grill_c
0,18.0,115.2
30,18.3,118.6
...
#note,76m,Stall hit at 68°C. Spritzing with apple juice.
#note,192m,Wrapped in butcher paper at 75°C.
```

**`/BBQ/config/settings.csv`** — single row, written on every target update so the last settings survive a reboot:
```
meat_target_c,grill_low_c,grill_high_c
74,160,200
```

**`/BBQ/config/presets.csv`** — one row per preset, written when the user saves or deletes a preset:
```
name,meat_target_c
Beef — rare,52
Beef — medium rare,57
Chicken — safe minimum,74
Brisket / pulled pork,95
```

**`/BBQ/config/favourites.csv`** — one row per favourite, written on save/delete:
```
name,meat_target_c,grill_low_c,grill_high_c,preset
Sunday Brisket,95,105,130,Brisket / pulled pork
Reverse Sear Ribeye,54,110,130,Beef — medium rare
```

### WebSocket Payload (JSON, pushed every 1s)

```json
{
  "ts": "2025-07-14T14:32:05",
  "meat": { "c": 67.4, "f": 153.3 },
  "grill": { "c": 182.3, "f": 360.1 },
  "eta_minutes": 42,
  "meat_target": 74.0,
  "grill_low": 160.0,
  "grill_high": 200.0
}
```

---

## Web Dashboard

### Access

Any device on the same WiFi network opens `http://<device-ip>` in a browser. No login required.

### UI Layout (mobile-first, responsive)

```
┌─────────────────────────────┐
│        🔥 BBQ Monitor        │  ← header, device name, °C/°F toggle
├──────────────┬──────────────┤
│  MEAT PROBE  │  GRILL PROBE │  ← large temp readouts
│   67.4°C     │  182.3°C     │
│  Target: 74° │  160–200°    │  ← targets, status indicator
│  ETA: 42 min │  ✅ In range  │
├──────────────┴──────────────┤
│         TEMP GRAPH          │  ← Chart.js live scrolling graph
│  both probes, last 60 min   │
├─────────────────────────────┤
│      TARGET SETTINGS        │  ← input fields, saved to localStorage
│  Meat target: [____] °C     │
│  Grill low:   [____] °C     │
│  Grill high:  [____] °C     │
└─────────────────────────────┘
```

### Colour Scheme

| Token | Value | Usage |
|---|---|---|
| Background | `#0d0d0d` (near black) | Page background |
| Surface | `#1a1a1a` | Cards / panels |
| Primary | `#ff4d8d` (pink) | Highlights, active states |
| Text | `#ffffff` | Primary text |
| Text muted | `#888888` | Labels, secondary info |
| Success | `#4dff91` | In-range / target reached indicators |
| Warning | `#ffcc00` | Grill out of range |

### Alerts (in-browser only, no push)

| Condition | Behaviour |
|---|---|
| Meat probe reaches target | Card flashes pink, banner appears at top |
| Grill below low threshold | Grill card border turns yellow, label shows "TOO LOW" |
| Grill above high threshold | Grill card border turns yellow, label shows "TOO HIGH" |
| Probe disconnected | Reads shown as `---`, card dims |

### Temperature Toggle

- Toggle button in header switches display between °C and °F.
- Default is **°C**.
- Preference saved in browser `localStorage`.
- Conversion applied client-side; all data from firmware is always in °C.

### ETA Prediction

- Calculated client-side from the last 10 minutes of meat probe readings.
- Uses linear regression on the rolling window to project a rate of change (°C/min).
- Displayed as "ETA: X min" or "ETA: < 1 min" when very close.
- Hidden if the meat probe temp is above the target or rate of change is flat/negative.

### Graph

- Rendered with **Chart.js** (served from SPIFFS, no CDN dependency).
- X-axis: rolling 60-minute window.
- Two lines: meat (pink) and grill (white).
- Target meat temp shown as a dashed pink horizontal line.
- Grill high/low shown as dashed white lines.

---

## File Structure

### SPIFFS (onboard flash — web UI)

```
/data
  index.html
  style.css
  app.js
  chart.min.js
```

### SD Card (data logs)

```
/BBQ/
  logs/
    log_2025-07-14.csv
    log_2025-07-15.csv
  cooks/
    cook_2025-07-14T08-14-00.csv
  config/
    settings.csv
    presets.csv
    favourites.csv
```

### Firmware

```
/
  bbq-thermometer.ino   (or main.cpp for PlatformIO)
  thermistor.h          Steinhart-Hart conversion
  logger.h              SD card logging
  websocket_handler.h   WebSocket push logic
```

---

## Target Settings

- Configured via the web UI, not hardcoded in firmware.
- Settings sent from browser to ESP8266 via WebSocket message on change.
- Stored in firmware memory (not persisted across power cycles in v1 — EEPROM persistence is a future enhancement).

---

## Power

- Powered by existing USB battery pack.
- No onboard LiPo management required for v1.
- Estimated current draw: ~80–120mA (WiFi active, ADS1115 + SD).
- A 10,000mAh pack gives an estimated 40–50 hours of runtime.

---

## Limitations & Future Enhancements

| Item | Status |
|---|---|
| Target settings persist across reboot | v2 — EEPROM/NVS |
| Push notifications (phone, browser push API) | v2 — requires HTTPS + service worker |
| More than 2 probes | v2 — ADS1115 has 2 spare channels |
| OTA firmware updates | v2 |
| Historical log viewer in UI | v2 |
| Custom device name / mDNS (`bbq.local`) | v2 |
| PCB design | v3 |

---

## Bill of Materials (Estimated)

| Item | Est. Cost |
|---|---|
| AZDelivery D1 Mini (owned) | — |
| Morpilot probes (owned) | — |
| Battery pack (owned) | — |
| ADS1115 module | ~£3 |
| SD card module | ~£2 |
| 2x 3.5mm stereo jack sockets | ~£2 |
| 2x 100kΩ resistors | <£1 |
| Perfboard + wire | ~£3 |
| Project enclosure | ~£5 |
| **Total additional spend** | **~£16** |


Gravity: I2C ADS1115 16-Bit ADC Module
- https://thepihut.com/products/gravity-i2c-ads1115-16-bit-adc-module-arduino-raspberry-pi-compatible
- Sale price £14.90

Fermion: MicroSD Card Module for Arduino (Breakout)
- £5
- https://thepihut.com/products/microsd-card-module-for-arduino

3.5mm 1/8" Stereo Audio Jack Terminal Block
- https://thepihut.com/products/3-5mm-1-8-stereo-audio-jack-terminal-block
- £2.40

