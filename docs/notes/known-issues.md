# Known issues — to work on later

Two items found while fixing [tick-stream.md](tick-stream.md) and flashing the
device on 2026-09-06. Neither is urgent; both need a reflash, so they pair well
with the next firmware change.

---

## 1. Cook timestamps are logged in UTC, an hour behind UK local time

**Severity:** low — cosmetic, but it corrupts the timeline of every cook record
**File:** `arduino/bbq-thermometer/config.h` (line 16), `bbq-thermometer.ino:60`

### Problem

`NTP_UTC_OFFSET_SEC` is `0`, carried over from the old Mac config. The UK is on
BST (UTC+1) from 2026-03-29 to 2026-10-25, so everything the device timestamps
is currently an hour behind local time.

Observed on the serial boot log immediately after flashing:

```
NTP: 2026-09-06T10:38:05      # device
local now: 2026-09-06T11:45   # actual (BST)
```

### What it affects

- Cook CSV filenames — `cook_<timestamp>.csv` (`cook_session.h`, `cookStart`)
- The `#started_at` metadata header inside each cook file
- Cook note timestamps
- The `ts` field on every WebSocket tick

### Why it isn't just "set it to 3600"

The offset is a fixed compile-time constant and the firmware does not track DST
(`config.example.h` says so explicitly). Setting `3600` fixes today but silently
breaks again on **2026-10-25**, when BST ends — and cook files written either
side of that boundary will disagree by an hour with no indication why.

### Options

1. **Quick:** set `NTP_UTC_OFFSET_SEC 3600`, and diarise reverting it to `0` on
   2026-10-25. Fixes now, guaranteed to be wrong again in seven weeks.
2. **Proper (preferred):** drop the `NTPClient` library's fixed offset and use
   the ESP8266 core's own `configTime()` with a POSIX TZ string —
   `GMT0BST,M3.5.0/1,M10.5.0` — which handles DST transitions automatically and
   forever. Touches `bbq-thermometer.ino:60` and the NTP block in `setup()`.
3. **Nice-to-have:** expose the timezone in the settings UI and persist it to SD
   alongside the other settings, so it never needs a reflash again.

Option 2 is the real fix. Option 1 is a stopgap if a cook is imminent.

---

## 2. `upload_all.py` doesn't pass the upload port through, so OTA half-works

**Severity:** low — confusing, not destructive
**File:** `scripts/upload_all.py`

### Problem

OTA itself is confirmed working. The device advertises it correctly:

```
_arduino._tcp on bbq.local / <device-ip>
port = 8266
txt  = ["auth_upload=yes" "board=d1_mini" "ssh_upload=no" "tcp_check=no"]
```

But the post-upload hook re-invokes PlatformIO for the filesystem **without
forwarding the port**:

```python
env.Execute("\"$PYTHONEXE\" -m platformio run -t uploadfs -e %s" % env["PIOENV"])
```

So after `pio run -t upload --upload-port <device-ip>`, the firmware goes over
the air as intended, then `uploadfs` runs with no port and falls back to
auto-detecting a **serial** one:

- USB still plugged in → filesystem silently flashes over USB. Works, but it is
  not the OTA upload you asked for, and it fails confusingly if the port is busy.
- USB unplugged → the step fails outright, *after* the firmware already flashed.
  Easy to misread as "the OTA flash failed" when it actually succeeded.

### Fix

```python
def upload_filesystem(source, target, env):
    port  = env.subst("$UPLOAD_PORT")
    extra = (" --upload-port " + port) if port else ""
    env.Execute(
        "\"$PYTHONEXE\" -m platformio run -t uploadfs -e %s%s" % (env["PIOENV"], extra)
    )
```

### Related, while in here

- **An OTA filesystem upload wipes `/wifi.json`**, same as a serial one, because
  it rewrites all of LittleFS. Recovery is automatic now that real credentials
  are compiled into `config.h` (`bbq-thermometer.ino:124-127` re-saves them on
  boot), but it is worth remembering before flashing from another room.
- **OTA needs free flash equal to the sketch size.** Currently 466 KB used of
  1044 KB program space, so ~577 KB headroom — fine today, worth watching as the
  firmware grows.
