# BBQ Wireless Probe Thermometer

A wireless BBQ thermometer built on an ESP8266 D1 Mini. Two NTC thermistor probes
(meat and grill) are read through an ADS1115 ADC and served as a live web
dashboard over WiFi, with every cook logged to an SD card.

No cloud, no account, no app — the device runs its own web server on your local
network. Open `http://bbq.local/` in a browser and it's there.

- **Dashboard:** `http://bbq.local/`
- **JSON API:** `http://bbq.local/api/status` — see below
- **Hardware, wiring and build details:** [`SPEC.md`](SPEC.md),
  [`hardware/WIRING.md`](hardware/WIRING.md)
- **Firmware internals:** [`arduino/FIRMWARE-SPEC.md`](arduino/FIRMWARE-SPEC.md)
- **Planned features:** [`ROADMAP.md`](ROADMAP.md)
- **Historical design and review notes:** [`docs/notes/`](docs/notes/)

> **Not a food-safety device.** This is a hobby project with no certification.
> Preset temperatures are guides, and readings depend on your probes and
> calibration. Check food with a calibrated food thermometer before serving it.

---

## Build your own

### Parts

A D1 Mini (ESP8266), an ADS1115 ADC module, a micro SD card module, two 100 kΩ
resistors, two 3.5 mm jack sockets, two 100 kΩ NTC probes and a USB battery pack.
[`hardware/WIRING.md`](hardware/WIRING.md) has the full list and pinout.

The electronics are low voltage, but the device itself is not heat-proof: keep
the board, battery and cables away from the grill, and check your probes' rated
maximum temperature, including the cable.

### Flash the firmware

You need [PlatformIO](https://platformio.org/install/cli). Library versions are
pinned in `platformio.ini` and install automatically.

```bash
cp arduino/bbq-thermometer/config.example.h arduino/bbq-thermometer/config.h
```

Edit `config.h`. **The build fails until you replace both `CHANGE_ME` passwords**:
`OTA_PASSWORD` protects firmware updates, and `AP_PASSWORD` protects the setup
network (minimum 8 characters). `WIFI_SSID` and `WIFI_PASSWORD` are optional,
because you can set up WiFi from the browser instead. `config.h` is gitignored.

Connect the board over USB, then build and upload the firmware and web UI:

```bash
pio run -t upload
```

Later updates can go over WiFi using the `OTA_PASSWORD` you set:

```bash
PLATFORMIO_UPLOAD_FLAGS="--auth=YOUR_OTA_PASSWORD" pio run -t upload --upload-port bbq.local
```

An upload rewrites the whole filesystem, which erases WiFi details saved from the
browser. Details compiled into `config.h` are restored on the next boot.

### First boot

If the device can't join a network it starts its own: connect to
`BBQ-Thermometer` with your `AP_PASSWORD`, open `http://192.168.4.1/`, and pick
your WiFi under Settings. After that the dashboard is at `http://bbq.local/`.
Calibrate your probes from Settings for better accuracy.

---

## Security

The device is designed for a **trusted home network** and must not be exposed to
the internet. The dashboard, JSON API and WebSocket have no login, so anyone on
your network can read temperatures and change settings, including WiFi details.
WebSocket connections from browsers are only accepted from the device's own
pages, which stops other websites driving it, and firmware updates need
`OTA_PASSWORD`.

---

## JSON API

The device exposes a read-only JSON endpoint so you can build your own add-ons
against it — desktop widgets, home-automation sensors, phone shortcuts, scripts,
dashboards — without touching the firmware or scraping the web UI.

### `GET /api/status`

Returns a snapshot of the current device state. No authentication, no
rate-limiting, `Access-Control-Allow-Origin: *` so browser apps can call it
directly.

```bash
curl -s http://bbq.local/api/status
```

```json
{
  "ts": "2026-09-06T13:03:29",
  "meat":  { "c": 74.9, "r": 7512 },
  "grill": { "c": 97.1, "r": 3920 },
  "meat_target": 90,
  "grill_low": 150,
  "grill_high": 200,
  "sd": true,
  "heap": 26600,
  "cook_active": true,
  "cook_name": "Tesco brisket",
  "cook_elapsed_s": 11622,
  "cook_meat_target": 90,
  "cook_grill_low": 150,
  "cook_grill_high": 200,
  "rssi": -80
}
```

### Fields

| Field | Type | Notes |
|---|---|---|
| `ts` | string | Device clock, ISO 8601, no timezone suffix |
| `meat.c` | number \| null | Meat probe, °C to 1 dp. **`null` when the probe is disconnected** |
| `grill.c` | number \| null | Grill probe, °C to 1 dp. `null` when disconnected |
| `meat.r` / `grill.r` | number \| null | Raw probe resistance in whole ohms (median-filtered). What the calibration wizard watches; `null` when disconnected |
| `meat_target` | number | The meat target **in force right now** (see below) |
| `grill_low` / `grill_high` | number | The grill band in force right now |
| `sd` | bool | SD card mounted. `false` means nothing is being recorded |
| `heap` | number | Free heap in bytes — useful for spotting memory trouble |
| `cook_active` | bool | Whether a cook is running |
| `cook_name` | string | *Only when `cook_active`* |
| `cook_elapsed_s` | number | *Only when `cook_active`* — seconds since the cook started |
| `cook_meat_target` | number | *Only when `cook_active`* — the cook's own target |
| `cook_grill_low` / `cook_grill_high` | number | *Only when `cook_active`* |
| `rssi` | number | WiFi signal, dBm. Absent in AP mode |
| `ap_mode` | bool | Present and `true` only when the device is in setup AP mode |

### Targets: read `meat_target`

The device holds two sets of targets — persisted global defaults, and the
targets belonging to whichever cook is running. **`meat_target` always reports
whichever is actually in force**, so for alerting you can just read it and
ignore the distinction.

`cook_meat_target` is also sent while a cook is active, for clients that want to
tell the two apart. If a cook is running, the two are equal.

### Handle `null`

A disconnected or failed probe reports `null`, not a number or a zero. Anything
doing arithmetic or comparisons on the value needs to check first — in Python,
`None >= target` raises `TypeError` rather than returning `False`.

### Addressing and availability

`bbq.local` is an mDNS name advertised by the device. If your platform doesn't
resolve `.local` names, use the IP directly — check your router, or run
`avahi-browse -rt _http._tcp` on Linux to find it.

Expect the device to disappear from time to time. It runs on WiFi from inside a
garden or a metal BBQ, and signal is often marginal; it reconnects by itself but
requests will fail meanwhile. **Treat a failed request as normal**, keep the
last known good reading, and don't crash or spam retries. Note that cooks keep
being logged to the SD card while the device is offline — it's only the network
that goes away, not the recording.

### Polling

Probes are sampled **once per second**, so polling faster than that returns
identical data for no benefit. **Two seconds or slower is recommended.**

The ESP8266 has a small pool of TCP connections shared with the web UI and
WebSocket. Reuse a connection (`requests.Session`, a keep-alive HTTP client)
rather than opening a fresh one every poll, and tolerate an occasional failed
request without immediately retrying.

### Examples

```python
import requests
s = requests.Session()                       # reuse the connection
d = s.get("http://bbq.local/api/status", timeout=3).json()

meat = d["meat"]["c"]
if meat is not None and meat >= d["meat_target"]:
    print(f"{d.get('cook_name', 'Cook')} has hit target!")
```

```javascript
const d = await (await fetch("http://bbq.local/api/status")).json();
console.log(d.cook_active ? `${d.cook_name}: ${d.meat.c}°C` : "idle");
```

Home Assistant REST sensor:

```yaml
sensor:
  - platform: rest
    name: BBQ Meat
    resource: http://bbq.local/api/status
    value_template: "{{ value_json.meat.c }}"
    unit_of_measurement: "°C"
    scan_interval: 30
```

### Live stream instead of polling

For anything tracking rate of change — stall detection, time-to-target
estimates — the WebSocket at `ws://bbq.local/ws` pushes the **same payload**
every second, plus typed messages for cook history, favourites, calibration and
WiFi setup. The dashboard itself uses it.

The tick and `GET /api/status` are built by one shared function in the firmware
(`snapshot.h`), so the two can never report different values.

### Scope and security

The endpoint is **read-only** — it exposes no way to start or stop a cook,
change settings, or alter the device.

There is **no authentication**, by design: anyone who can reach the device on
your network can read your cook temperatures. The web UI and WebSocket are
likewise unauthenticated, and the WebSocket additionally accepts *mutating*
commands. Treat the device as trusted-LAN-only and do not port-forward it.

Cook history and stored temperature series are not currently available over
HTTP — only over the WebSocket. See [`docs/notes/known-issues.md`](docs/notes/known-issues.md).

### Known quirk

`ts` is currently written in UTC rather than local time, so it can read an hour
behind during British Summer Time. Tracked in
[`docs/notes/known-issues.md`](docs/notes/known-issues.md); use your own client clock if
that matters.

---

## Tests

```bash
npm install
npx playwright-core install chromium-headless-shell
npm test
```

`npm test` runs host-compiled firmware checks (needs `g++`) and browser tests of
the web UI against a simulated device.

## License

[MIT](LICENSE). The web UI bundles [Chart.js](https://www.chartjs.org/) (MIT),
and the firmware builds on ESPAsyncWebServer and ESPAsyncTCP (LGPL-3.0),
ArduinoJson (MIT), the Adafruit ADS1X15 library (BSD) and NTPClient (MIT). Those
libraries are downloaded at build time and are not included in this repository.
