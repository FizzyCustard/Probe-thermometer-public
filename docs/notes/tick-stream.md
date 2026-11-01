# BBQ Monitor: tick stream uses global `settings.meat_target` instead of the active cook's target

**Status: FIXED & VERIFIED ON HARDWARE** (firmware, 2026-09-06) — see
"Fix applied" at the bottom.

**Date found:** 2026-09-06
**Device:** BBQ Monitor, `http://bbq.local/` (<device-ip>)
**Discovered via:** an external client polling the device

## Summary

When a cook is started with a target different from the global `settings.meat_target`,
the per-second tick stream still reports the OLD global target, not the cook's target.
The watcher therefore monitors against the wrong temperature.

## Reproduction

1. Global setting: `settings.meat_target = 74` (from "Chicken family" favourite)
2. Start a cook named "Tesco brisket" with target 90
3. WebSocket `config` message correctly shows `cook_meat_target: 90`
4. But every subsequent `tick` message carries `meat_target: 74` (the stale global)

Observed payload snippets:

```json
// config — cook record is CORRECT
{"type":"config", "cook_meat_target": 90, "settings": {"meat_target": 74, ...}}

// tick — 10+ minutes into the cook, still showing the stale global value
{"ts":"...","meat":{"c":24.36},"grill":{"c":105.24},"meat_target":74,"cook_active":true,"cook_name":"Tesco brisket"}
```

## Expected behaviour

Ticks during an active cook should report the **cook's** target (90), since that's
what the UI and any monitoring client need. Either:

- set `settings.meat_target` when a cook starts, or
- have the tick stream read from the active cook record, or
- include `cook_meat_target` in every tick (most explicit)

## Workaround applied manually

User changed the global setting to 95, which synced the tick stream — but the
active cook record still says 90, so the app UI and tick stream can still disagree.

## Suggested permanent fix (watcher side, already discussed)

Prefer `cook_meat_target` from the `config` message over `meat_target` in ticks
when a cook is active. Not yet implemented in `bbq_watch.py`.

## Secondary observation

WiFi RSSI on the device degraded from -49 dBm to -87 dBm during the morning
(garden/distance?). Watcher auto-reconnects after silent drops (~6 min apart),
so connection gaps are survivable but worth knowing about.

---

## Fix applied — 2026-09-06

**Root cause was two-sided:**

1. `arduino/bbq-thermometer/websocket_handler.h` — `wsBroadcast()` emitted the
   globals unconditionally. `cook_start` writes its targets into `activeCook`
   and the cook CSV header only; it never touched the globals.
2. `device/data/roadmap.js` — `confirmStartCook()` updates the browser's local
   `settings` and puts them in the `cook_start` message, but never calls
   `sendSettings()`, so the device global was never told.

**Wider than reported:** `grill_low` / `grill_high` were stale in exactly the
same way, so any grill-band alarm was watching the wrong band too.

**Why the app never showed it:** the browser ignores `meat_target` in ticks
entirely and renders from its own local `settings`; late joiners get corrected
by `cook_meat_target` in the `config` message. Only external WS consumers saw
the stale value.

**Fix (firmware, `wsBroadcast`):** ticks now report the target *in force right
now* — the active cook's when a cook is running, the global otherwise — and
additionally echo `cook_meat_target` / `cook_grill_low` / `cook_grill_high`
while a cook is active, mirroring what `config` already sends.

Rejected the "set `settings.meat_target` on cook start" option: it costs an SD
write per cook start and permanently overwrites the user's default with a
one-off cook value — exactly the mess the manual 95 workaround created.

**No watcher change is required.** Note the watcher-side fix as originally
described would not have worked alone: `sendConfig()` is only ever sent to a
single client (on connect, `request_config`, or after a favourite write) and is
**not** broadcast on `cook_start`, so a watcher already connected when the cook
started would never have received a `config` carrying `cook_meat_target`.

### Still outstanding

- **Global left stale after the workaround.** `settings.meat_target = 95` is
  persisted on the SD card and is now the default for future cooks. Worth
  resetting via the settings UI.
- **Post-cook divergence (pre-existing, not introduced here).** After a cook
  stops, ticks revert to the global while the browser's local `settings` still
  holds the cook's targets, so the UI and the device disagree until reload.
- **No config broadcast on cook start/stop.** Not needed now that ticks carry
  the cook's targets, but state would be more coherent for multiple clients if
  `sendConfig` were broadcast on those transitions.

### Verified on device — 2026-09-06

Flashed to the D1 Mini over serial and confirmed live against the very cook in
this report. The device resumed "Tesco brisket" from SD after the reset, with
the cook target at 90 and the global still at 95 from the manual workaround —
exactly the divergent state that produced the bug:

```
CONFIG: cook_meat_target=90   settings.meat_target=95
TICK:   meat_target=90   cook_meat_target=90   cook='Tesco brisket'   active=True
```

`meat_target` in the tick now tracks the cook (90), not the stale global (95).
Pre-fix this read 95. No watcher-side change was needed.
