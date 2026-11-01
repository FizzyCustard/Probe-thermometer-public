# Probe Calibration Redesign — Plan

**Status:** implemented 2026-09-11 (steps 1–7 of §5; the optional 2-point mode
in §3.5 / step 8 was not done). Firmware passes a syntax-only build against the
ESP8266 toolchain and the wizard logic passes a node test, but **§6 has not yet
been run on hardware.**
**Date:** 2026-09-11
**Scope:** `device/data/` (web UI) + `arduino/bbq-thermometer/` (firmware)

Deviations from the plan as written:

- The old single-probe `cal_save` shape was removed in the same change rather
  than kept for a release, since the old UI went at the same time.
- The firmware validator lives in `thermistor.h` as `calibrateFromPoints()`,
  and the JS mirrors it (`calValidate()`), so the review step can name the
  bad point before the round trip.
- A successful-but-unpersisted save is reported as
  `applied: true, saved: false, reason: "sd_write_failed"` rather than a plain
  failure, because the new curve *is* in use until reboot.
- While the wizard is open the "target reached" alert is suppressed: a meat
  probe in boiling water would otherwise fire it.

---

## 1. What the current flow does, and where it falls down

Today the Settings sheet has two identical blocks (Meat, Grill), each with three
rows of `temp input → Record → resistance`, and a per-probe **Calculate & Save**
button (`device/data/index.html:210-300`, `device/data/app.js:736-820`).

Walking through a real calibration with both probes exposes the friction:

| # | Problem | Where |
|---|---------|-------|
| 1 | **Both probes sit in the same bath, but you record them separately.** Six Record clicks and six temperature inputs for what is physically three events. | UI layout |
| 2 | **No live reading while you wait.** You can't tell whether the probe has settled before you press Record. The main dashboard shows °C, but only through the *old* coefficients, and the resistance isn't shown anywhere until you click. | tick payload has no resistance |
| 3 | **Record takes one instantaneous ADC sample.** The rest of the firmware uses median-of-3 precisely because single samples glitch, but `cal_read` uses `readResistance()` once. One noisy sample becomes a permanent coefficient. | `websocket_handler.h:238-252` |
| 4 | **`cal_read` and `cal_save` run inside the async WebSocket callback.** `cal_read` does an I2C read there and `cal_save` does an SD read-modify-write there. Every other SD/heavy operation in the file is explicitly deferred to `loop()` via the pending queue because it can starve the TCP stack. Calibration is the one path that still bypasses that rule. | `websocket_handler.h:238`, `:313` |
| 5 | **No memory of calibration.** After reload nothing says whether a probe is calibrated, when, or with which points. `loadCalibration()` discards the timestamp and points it reads from SD. | `logger.h:312-338`, `FRONTEND_IMPROVEMENTS.md §6` |
| 6 | **Session state lives only in JS memory.** Lock the phone mid-calibration in a steamy kitchen, reload, and all captured points are gone. | `calState` in app.js |
| 7 | **No validation until the firmware rejects it.** Duplicate temperatures or a probe that never went in the bath are only caught by `bad_points`, with no hint which point is wrong. | `TODO.md` "Calibration accepts duplicate temperatures" |
| 8 | **Two probes = two saves = two SD writes**, each reading and rewriting the whole JSON. | `logger.h:276` |

The maths is fine. `calcSHCoeffs()` exists in both JS and C++, the firmware
recomputes and sanity-checks server-side, and the SD JSON format already stores
points and a timestamp. None of that needs to change.

---

## 2. Target experience

Replace the two static blocks with a **guided wizard that calibrates both probes
at once, one bath at a time**:

```
┌──────────────────────────────────────────────────────┐
│  Calibrate probes                       Step 1 of 4  │
│  ● ○ ○ ○                                             │
│                                                      │
│  ICE BATH                                            │
│  Fill a cup with crushed ice, top up with a little   │
│  water, stir, and put BOTH probe tips in. Wait for   │
│  the readings to settle.                             │
│                                                      │
│  Reference temperature  [  0.0 ] °C                  │
│                                                      │
│  ┌ MEAT ─────────────────┐ ┌ GRILL ────────────────┐ │
│  │  0.4 °C     336,210 Ω │ │  1.1 °C     332,940 Ω │ │
│  │  ● stable (±0.1 °C)   │ │  ◌ settling… (−0.6/8s)│ │
│  └───────────────────────┘ └───────────────────────┘ │
│                                                      │
│  [ Capture both ]                     Skip grill ▸   │
│                                                      │
│  ◂ Back                                   Next ▸     │
└──────────────────────────────────────────────────────┘
```

Steps:

1. **Ice bath (0 °C)** — both probes, one reference input, live readings, stability pill, one Capture.
2. **Room / reference (≈25 °C)** — same layout; hint text says "use a reference thermometer, any temperature well between the other two is fine".
3. **Boiling (100 °C)** — same layout; hint mentions altitude (≈ −1 °C per 300 m).
4. **Review & save** — one table with both probes' three points, the coefficients each would get, a residual per point (how far each reference round-trips through the *new* curve), and a live preview line: *"Right now meat would read 21.3 °C (currently 20.1 °C)"*. One **Save both** button; per-probe success/failure.

Afterwards the collapsed header shows a badge per probe: **Meat ✓ 11 Sep 2026 · Grill ✓ 11 Sep 2026**, or *Default coefficients* if never calibrated.

Design rules:

- A probe that reads `null` (unplugged) on a step is greyed out and auto-skipped; you can still calibrate one probe alone.
- Capture is allowed while "settling" but the button turns amber and the row keeps a "captured while drifting" marker, so the user knows why a point might be off. Don't block; the user knows their kitchen better than a threshold does.
- Each point can be **re-captured** (go Back, Capture again) without restarting.
- Progress survives reload (see §4.3).

---

## 3. Firmware changes

### 3.1 Put raw resistance in the tick *(enables live view; ~30 bytes per tick)*

`sampleProbe()` in `bbq-thermometer.ino:94` already has the median-of-3
resistance and throws it away. Return it via an out-param, keep it in
`curMeatR` / `curGrillR`, and have `buildSnapshot()` add:

```json
"meat":  { "c": 20.1, "r": 65521 },
"grill": { "c": 19.8, "r": 65489 }
```

`r` is an integer ohm value, or `null` when the probe is absent (same
`snap1dp`/NaN idiom as `c`). REST `/api/status` gets it for free via the shared
builder, which is also useful for anyone scripting against the device.

This single change removes the need for `cal_read` entirely: the browser
already receives a median-filtered resistance every second. Delete `cal_read`
and its I2C-in-callback hazard (§1 #3, #4).

### 3.2 Move `cal_save` onto the deferred queue and accept both probes

Add `PENDING_CAL_SAVE` to `PendingKind`. The callback only parses and copies
into a static payload:

```cpp
struct PendingCal {
  bool  has[2];
  float t[2][3], r[2][3];
  char  timestamp[25];
};
```

then sets `pendingKind` last (same volatile-ordering rule the file already
documents). `wsTick()` computes coefficients, validates, applies to
`probeCoeffs[]`, and calls a new `saveCalibrationAll(...)` that rewrites the JSON
**once** for both probes.

Message shape (one message, either or both probes):

```json
{
  "type": "cal_save",
  "timestamp": "2026-09-11T14:30:00Z",
  "probes": [
    { "probe": 0, "points": [ {"temp_c": 0, "resistance": 336210}, ... ] },
    { "probe": 1, "points": [ ... ] }
  ]
}
```

Reply:

```json
{ "type": "cal_saved", "results": [ {"probe": 0, "saved": true}, {"probe": 1, "saved": false, "reason": "bad_points"} ] }
```

Drop the unused top-level `A`/`B`/`C` from the inbound message — the firmware
recomputes them anyway and ignoring them is misleading in the spec.

### 3.3 Tighter server-side validation *(cheap, catches real mistakes)*

Extend the existing `bad_points` check to:

- temperatures pairwise ≥ 0.5 °C apart (currently only exact equality is rejected);
- resistance strictly *decreasing* as temperature increases — an NTC that isn't
  monotonic means one probe wasn't in the bath, or the baths were mixed up;
- round-trip **all three** points through the new curve within ±3 °C, not just the
  middle one (it's the same loop, three iterations instead of one);
- return a `reason` that says which check failed and which point, so the UI can
  highlight the row instead of showing a generic error.

### 3.4 Keep calibration metadata in RAM and report it in `config`

Add a `CalInfo probeCal[2]` (`calibrated[25]`, `t[3]`, `r[3]`, `bool fromSd`)
populated by `loadCalibration()` and `cal_save`. `sendConfig()` adds:

```json
"calibration": {
  "0": { "calibrated": "2026-09-11T14:30:00Z", "source": "sd", "points": [ ... ] },
  "1": { "calibrated": null, "source": "default" }
}
```

That drives the badges (§1 #5) and lets the review step show *"previous: 0 °C @
336,400 Ω"* next to each new point for a sanity comparison.

### 3.5 Optional: 2-point mode

`config.h` shows the probes were in fact calibrated from **two** points with
`C = 0`. A `points` array of length 2 could be accepted, with
`B = (Y2−Y1)/(L2−L1)`, `A = Y1 − B·L1`, `C = 0`. Worth adding as a "Quick
(2-point)" option on the intro step, but it's independent of everything above,
so do it last or not at all.

---

## 4. Web UI changes

### 4.1 Markup (`index.html`)

Replace the two `.cal-probe` blocks with one `.cal-wizard` container:
progress dots, a step panel, the two live probe cards, a shared reference
input, and Back/Capture/Next controls. The four steps share the same panel;
only the heading, hint text, and default reference temperature change. Render
the review table from JS.

### 4.2 Logic (`app.js`)

- **Ring buffer per probe** of the last 8 `(c, r)` tick values, fed from the
  existing message handler (the `data.meat`/`data.grill` branch at
  `app.js:529`). Cost: two arrays of 8 objects.
- **Stability** = spread of the 8 buffered °C values. Stable when spread
  ≤ 0.3 °C and the buffer is full; the pill shows the spread and the direction
  of drift. Stability is computed from °C rather than Ω so the threshold means
  the same thing at 0 °C (300 kΩ) and 100 °C (7 kΩ).
- **Capture** = mean resistance of the buffered samples (not the last one), for
  both probes at once, tagged with the shared reference temperature and a
  `drifting: true/false` flag.
- **Auto-skip** a probe whose `r` is `null` for the current step.
- **Client-side validation** mirrors §3.3 so the user gets the message before
  the round trip: distinct temperatures, monotonic resistance across captured
  points, and a warning (not a block) when a capture is more than 5 °C from
  where the *current* curve says the probe is — that's the "did you actually put
  it in the ice" check.
- **Review step**: reuse `calcSHCoeffs()` to compute per-probe A/B/C, show
  residuals per point, and the live preview using the newest buffered `r`.
- **Save both** → single `cal_save` (§3.2); handle the per-probe results; on
  success update the badges from the `cal_saved` reply without waiting for a
  reconnect.

### 4.3 Persistence

Store wizard progress under `localStorage['bbq_cal_draft']`: step index,
reference temps, captured points per probe. Restore on load, offer "Resume
calibration (2 of 3 baths done)" on the collapsed header, clear on save or on
an explicit **Start over**. Follow the existing `try/catch` idiom used for
`bbq_unit`.

### 4.4 Styling (`style.css`)

Mostly reuse: `.cal-header`, `.btn-record` (→ Capture), `.btn-calculate`
(→ Save both), `.input-wrap`. New: progress dots, the two live cards (a
narrow variant of the dashboard probe cards), the stability pill in
`--success` / `--warning`, and the review table (wrap in `overflow-x:auto` for
phones).

### 4.5 Docs

- `arduino/FIRMWARE-SPEC.md`: replace the `cal_read`/`cal_save` sections and
  the "Calibration Workflow" list with the new messages and the four-step flow.
- `FRONTEND_IMPROVEMENTS.md §6` and `TODO.md`: strike the three calibration
  items this closes (persisted status, validation, collapse memory — the
  wizard's resume state supersedes the collapse-memory idea).

---

## 5. Order of work

Each step leaves the device usable; nothing depends on a later step.

| Step | Change | Files | Why this order |
|------|--------|-------|----------------|
| 1 | Add `r` to the tick/snapshot | `bbq-thermometer.ino`, `snapshot.h` | Tiny, safe, unlocks everything else. Verify with `/api/status`. |
| 2 | Live probe cards + stability pill in the *existing* calibration UI, Record captures the buffered mean instead of sending `cal_read` | `app.js`, `index.html`, `style.css` | Immediately improves the current flow before any restructuring; `cal_read` becomes dead code. |
| 3 | `cal_save` → deferred queue, multi-probe payload, validation, single SD write | `websocket_handler.h`, `logger.h`, `thermistor.h` | Firmware side of "both at once". Keep accepting the old single-probe shape for one release so step 2's UI still works during bring-up. |
| 4 | Calibration metadata in RAM + `config` message + badges | `logger.h`, `websocket_handler.h`, `app.js` | Independent of 3; small. |
| 5 | Wizard restructure: steps, shared reference input, review table, Save both | `index.html`, `app.js`, `style.css` | The visible payoff; everything it needs now exists. |
| 6 | Draft persistence / resume | `app.js` | Polish; trivial once state is centralised in step 5. |
| 7 | Remove `cal_read`, old single-probe `cal_save` shape, update specs | firmware + docs | Cleanup. |
| 8 | *(optional)* 2-point mode | `thermistor.h`, `app.js` | Only if wanted. |

Steps 1–2 are an afternoon and already fix the worst of it (blind single-sample
Record). Steps 3–5 are the bulk. Step 3 is the only one that touches the async
callback rules, so it's the one to review most carefully.

---

## 6. Test plan

- **Bench, no baths:** with both probes at room temperature, wizard shows both
  stable, Capture on step 1 with reference 0 °C triggers the "more than 5 °C
  from current reading" warning. Confirms the sanity check fires.
- **Unplug one probe** mid-step: its card greys out, Capture only captures the
  other, review shows one probe, Save saves one. Re-plug: card returns.
- **Real 3-bath run** with both probes: the review residuals should all be
  < 0.5 °C; after Save, both badges show today's date; `calibration.json` on the
  SD has both probes with the same timestamp; a reboot reloads them (serial
  log) and `config` reports `source: "sd"`.
- **Reload mid-wizard:** capture two baths, reload the page, header offers
  Resume, points are intact.
- **Bad data:** capture the same bath twice for step 2 and 3 → client blocks
  with "temperatures must differ"; force-send via devtools → firmware replies
  `bad_points` naming the point.
- **Back-pressure:** run the wizard with the phone on a weak signal; the
  ring buffer should just have fewer samples and the pill should say
  "waiting for readings" rather than showing stale "stable".
- **Regression:** normal cook still ticks at 1 s, `/api/status` includes `r`,
  heap doesn't drop after 20 saves in a row (the deferred path allocates
  nothing per save).
