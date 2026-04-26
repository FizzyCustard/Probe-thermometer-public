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
    bbq-thermometer.ino   Main sketch — WiFi, server, loop
    thermistor.h          Steinhart-Hart resistance → temperature
    logger.h              SD card CSV logging
    websocket_handler.h   WebSocket push and receive
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

## WiFi Configuration

Credentials are defined as constants in `bbq-thermometer.ino`.
Do not commit real credentials — use placeholder values and replace
before flashing.

```cpp
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";
```

The device connects as a WiFi client (station mode) and receives an
IP via DHCP. The IP is printed to Serial on boot. Open
`http://<device-ip>` in a browser on the same network.

Future improvement: mDNS so the device is accessible at `http://bbq.local`.

---

## Thermistor Module (`thermistor.h`)

Converts ADS1115 raw counts → resistance → temperature using the
Steinhart-Hart B-parameter equation.

**Probe specs (Morpilot NTC):**
- Resistance at 25°C (R₀): 100,000 Ω
- B-value: 3950 K
- Supply voltage: 3.3V
- Series resistor: 100,000 Ω

**Equation:**
```
R = R_series × (ADC_max / ADC_raw - 1)
1/T = 1/T₀ + (1/B) × ln(R / R₀)
T_celsius = T_kelvin - 273.15
```

**ADS1115 settings:**
- Gain: GAIN_ONE (±4.096V range) — gives ~0.125mV per count
- Mode: continuous
- Sample rate: 128 SPS (default)
- ADC_max for gain ±4.096V = 32767 counts = 4.096V; scale to 3.3V

**Functions to implement:**
```cpp
float readResistance(uint8_t channel);
float resistanceTocelsius(float resistance);
bool probeConnected(uint8_t channel);   // returns false if reading is open/short
```

---

## Logger Module (`logger.h`)

Appends temperature readings to a CSV file on the SD card.

**File naming:** `/log_YYYY-MM-DD.csv` — new file per calendar day.

**CSV format:**
```
timestamp,probe1_c,probe2_c
2025-07-14T14:32:05,67.4,182.3
```

**Write interval:** every 5 seconds.

**Functions to implement:**
```cpp
bool loggerBegin();                                  // mount SD, open/create today's file
void logReading(String timestamp, float p1, float p2);
String getTodayFilename();                           // based on NTP date
```

---

## WebSocket Handler (`websocket_handler.h`)

Handles bidirectional WebSocket communication between the ESP8266
and the browser dashboard.

### Outbound (ESP8266 → browser, every 1 second)

JSON payload pushed to all connected clients:

```json
{
  "ts": "2025-07-14T14:32:05",
  "meat": { "c": 67.4 },
  "grill": { "c": 182.3 },
  "meat_target": 74.0,
  "grill_low": 160.0,
  "grill_high": 200.0
}
```

Temperature is always sent in °C. Unit conversion is done client-side
in the browser.

### Inbound (browser → ESP8266)

Settings update sent when user taps "Update Targets":

```json
{
  "type": "settings",
  "meat_target": 80.0,
  "grill_low": 150.0,
  "grill_high": 210.0
}
```

The firmware updates its in-memory settings and uses the new values
for subsequent outbound payloads.

**Functions to implement:**
```cpp
void wsBegin(AsyncWebServer* server);
void wsBroadcast(float meatC, float grillC, String ts);
void onWsMessage(AsyncWebSocketClient* client, String msg);
```

---

## Main Sketch (`bbq-thermometer.ino`)

### Boot sequence (`setup()`)

1. Start Serial (115200 baud)
2. Mount LittleFS — serve web UI files
3. Connect to WiFi — block until connected, print IP to Serial
4. Sync time via NTP
5. Initialise ADS1115 over I2C
6. Initialise SD card logger
7. Start AsyncWebServer on port 80
8. Register WebSocket endpoint at `/ws`
9. Register static file handler (serves LittleFS files for all other routes)
10. Start server

### Main loop (`loop()`)

- Every **1 second**: read both probes → broadcast JSON via WebSocket
- Every **5 seconds**: append row to SD card CSV
- Call `ws.cleanupClients()` every loop to remove stale connections

### Settings (in-memory, not persisted in v1)

```cpp
float meatTarget = 74.0;
float grillLow   = 160.0;
float grillHigh  = 200.0;
```

Updated when a settings WebSocket message is received.
Persisting across reboots (EEPROM/LittleFS) is a v2 task.

---

## LittleFS — Web UI Files

The `data/` folder at the project root contains the web UI files.
These are uploaded to the ESP8266's onboard flash using the
**LittleFS Upload Tool** for Arduino IDE or the PlatformIO
`uploadfs` command.

Files uploaded:
```
data/index.html
data/style.css
data/app.js
```

Before uploading, `app.js` needs updating to connect via WebSocket
instead of using the fake simulation data. See `BUG-ios-keyboard-scroll.md`
for the open UI bug to resolve first.

---

## Serial Debug Output

Useful output to print during development:

```
BBQ Monitor booting...
LittleFS mounted
Connecting to WiFi...
Connected! IP: 192.168.1.42
NTP synced: 2025-07-14T14:30:00
ADS1115 found at 0x48
SD card mounted
Server started
Probe 1 (meat):  48.3°C
Probe 2 (grill): 182.1°C
WS client connected: 192.168.1.55
Settings update received: target=80.0 low=150.0 high=210.0
```

---

## Open Tasks

- [ ] Implement `thermistor.h`
- [ ] Implement `logger.h`
- [ ] Implement `websocket_handler.h`
- [ ] Implement `bbq-thermometer.ino` main sketch
- [ ] Update `data/app.js` to use real WebSocket instead of simulation
- [ ] Fix iOS keyboard scroll bug (see `BUG-ios-keyboard-scroll.md`) before deploying UI to LittleFS
- [ ] Test probe readings against a known temperature (ice water = 0°C, boiling = 100°C)
- [ ] Test SD logging over a full cook session
- [ ] Add mDNS (`bbq.local`) — v2
- [ ] Persist settings to EEPROM — v2
