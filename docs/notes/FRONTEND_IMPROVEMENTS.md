# BBQ Monitor — Frontend Improvements

Open ideas for the web dashboard (`device/data/`). Pruned on 2026-09-11 against
the current code: anything already built, contradicted by the hardware, or not
worth the complexity was removed (see the log at the bottom). Items are grouped
by area and loosely ordered by value within each section.

Each item is tagged **[P1]**, **[P2]** or **[P3]**: P1 is a change you would
notice and value in daily use, P2 is worth doing but not urgent, P3 is a nice
to have that could slip from the next push without anyone missing it.

---

## 1. Live view

### Grill card has no range indicator [P1]
The Meat card has a progress bar, target label and ETA badge. The Grill card
only shows the temperature and a "Range 160–200°C" label. A small horizontal
band with a marker showing where the grill sits inside (or outside) its range
would give the same at-a-glance read the meat card already has, and make
TOO LOW / TOO HIGH obvious without reading the pill.

### Temperature trend arrow [P1]
The ETA badge says how long to target but nothing says whether a probe is
climbing, flat, or falling right now. A small ↑ / → / ↓ on each card, from the
slope of the last few readings (the ETA code already fits one), would make a
stall or a lid-open dip visible instantly.

### Status pills are colour-only [P2]
`IN RANGE`, `TOO LOW`, `TOO HIGH` and `TARGET REACHED` differ only by colour.
Add a glyph (✓ ↓ ↑ 🎯) so they read in bright sunlight and for colour-blind
users, and keep the text in the pill's accessible name.

### Audible alarm [P1]
Target-reached already fires a system notification, but there is no sound, and
the grill going out of range fires nothing at all. A short Web Audio beep on
target reached, and a notification + beep when the grill leaves its range for
more than a minute, would let the user walk away from the screen. Needs a
mute toggle and a first-interaction unlock (browsers block audio until a tap).

### Settings button is easy to miss [P3]
The fixed bottom `⚙ Settings` pill is low-contrast and blends into the page.
Slightly more weight (filled background, or the primary colour on the icon)
would help first-time users find calibration and WiFi setup.

---

## 2. Settings & inputs

### No validation of target inputs [P2]
Both the Settings sheet and the Start Cook dialog accept any numbers: grill
low above grill high, a meat target of 500 °C. The firmware silently drops an
invalid `settings` message, so the UI shows "Updated ✓" for a change that
never happened. Validate before sending (low < high, sane ranges) and show an
inline error under the field. Also listed in `TODO.md`.

### No acknowledgement that the device applied a settings change [P2]
"Updated ✓" flips as soon as the message is sent. The firmware sends no reply
for `settings`, so a dropped or rejected update looks identical to a success.
Add a `settings_saved` reply in the firmware and turn the tick into a warning
if it does not arrive within a few seconds.

### Targets changed on another device are not reflected [P2]
The 1 s tick carries the targets in force (`meat_target`, `grill_low`,
`grill_high`) but the UI ignores them and only reads targets from the `config`
message on connect. Change the target on a phone and a tablet showing the same
cook keeps drawing the old target line until it reconnects. Adopt the tick's
values when they differ, and show a brief "Targets updated from another
device" note.

### Favourites cannot be edited [P2]
A saved favourite can only be loaded or deleted. Renaming or nudging its
targets means delete and re-save. An edit flow (tap to rename, adjust targets)
would make favourites more useful. Needs a `fav_update` message in the
firmware or delete-and-recreate under the same id.

### Deleting a favourite has no confirmation or undo [P2]
The ✕ on a favourite row deletes immediately. Either a confirm step or a short
undo toast (hold the deleted favourite in memory for a few seconds before
sending `fav_delete`) would prevent fat-finger losses.

### Presets can only be edited on the SD card [P3]
Doneness presets load from `/BBQ/config/presets.csv`; there is no way to add
or rename one in the UI. Low priority because favourites cover the common
"same targets every time" case, but a small add/remove list in Settings would
finish the feature.

---

## 3. Chart

### Zoom and pan on the live chart [P1] **[Approved]**
The chart is locked to a rolling 60-minute window, so a specific moment (the
spritz, the wrap) cannot be inspected. Chart.js supports this via
`chartjs-plugin-zoom`. The plugin must be bundled into `device/data` like
Chart.js is (the device has no internet, and every extra KB comes out of
LittleFS), and pinch-zoom must not fight the page scroll on phones.

### Chart export [P3]
There is no way to save or share a chart. A "Save image" button using
`canvas.toDataURL()` is a few lines; the useful part is drawing the cook name,
date and targets onto the exported image first.

### History sparklines are synthetic [P2]
The small curves in the history list are generated from a seed and the peak
temperature; they are not the cook's data. Either draw them from real
downsampled points (costly: one `cook_data` request per row on an ESP8266) or
drop them, since a fake shape next to real numbers is misleading.

---

## 4. Cook notes

### Quick-action note chips [P1]
The same notes recur every cook: "Spritzed", "Wrapped", "Lid opened", "Added
wood". A row of tap-to-insert chips above the note input would speed this up
with smoky hands. The chips could be the last N distinct notes across history,
so they learn the user's vocabulary.

### Notes are invisible on a freshly loaded page mid-cook [P2]
Open the dashboard on a second device during a cook and the notes card only
shows a "cook already in progress" system line; the notes already written
are not sent until the cook appears in History. The firmware could include the
active cook's notes in `config` (it already parses `#note` lines for
`cook_data`).

### Notes cannot be edited or deleted [P3]
Notes are appended to the cook's CSV as `#note` lines, so this is a firmware
change (rewrite the file without the line) as much as a UI one. Worth having
for typos, but only after the two items above.

---

## 5. History

### No filter or sort [P3]
History is reverse-chronological only. As the list grows, a name search and a
sort (date, duration, favourites first) would keep it usable. Purely
client-side; the list is already in memory.

### Cook comparison [P1]
Overlaying two replay charts (this brisket vs last month's) is the single most
useful thing history could do for improving technique. Pick two cooks, align
on start time, draw both meat lines with the grill lines faded.

### Cook export [P2]
Every cook is already a CSV on the SD card. A download route in the firmware
(e.g. `GET /api/cooks/<file>`) plus a "Download CSV" button in the detail view
would let users back up and analyse cooks in a spreadsheet. Must be served in
chunks; a whole file in one `String` will not fit in RAM.

### History badge goes stale after a cook is stopped [P3]
Stopping a cook adds it to the local history list, but the count on the
History tab is only refreshed when the cook list is fetched on connect.
Re-request the cook list (or bump the count) on `cook_stopped`.

### Favourite star in the detail view is easy to miss [P3]
The ★ in the detail header is small and borderless. A labelled pill
("★ Favourite" / "★ Favourited") would be clearer, and its `aria-label`
should change with state.

---

## 6. Accessibility

### Settings sheet has no focus trap [P3]
Tab can move focus from the sheet into the hidden dashboard behind it. Set
`inert` on the main content while the sheet is open (or intercept Tab) so
keyboard and screen-reader users stay inside the modal. The same applies to
the Start/Stop Cook and history detail dialogs.

### No visible focus ring on custom buttons [P3]
Favourite rows, preset chips and history rows suppress the browser focus ring
and provide none of their own, so keyboard users cannot see where they are. A
`:focus-visible` outline in the accent colour fixes this globally.

### Missing or static `aria-label`s [P3]
- the detail-view ★ button is always labelled "Favourite" regardless of state;
- the history sparklines and the live chart canvas have no accessible
  description;
- the favourite delete ✕ says "Delete" without naming which favourite.

---

## 7. Performance

### Service worker for static assets [P2]
The ESP8266 serves `index.html`, `app.js`, `roadmap.js`, `style.css` and a
~200 KB Chart.js on every load, which is slow on a weak signal and competes
with the WebSocket. A service worker that caches those files (never `/ws` or
`/api/*`) would make reloads instant and let the dashboard shell open even
when the device is briefly unreachable. Cache-bust on firmware version.

---

## 8. Larger changes

### Multiple probe support [cant happen till i upate the hardware right now only 2 probes]
Firmware, wiring and UI all hardcode exactly two probes. The ADS1115 has two
spare channels (A2, A3) and the case has room for two more jacks. Rendering N
probe cards from a config array, and generalising the tick payload, would
allow a second meat probe. This touches everything (tick, cook CSV columns,
calibration, chart) and should only be done once the two-probe version is
stable.

---

## Removed on 2026-09-11

Done already: favicon and manifest; fluid temperature font (`clamp()`);
coloured connection pill; connecting / reconnecting state; stale and offline
handling with banner; signal-strength icon; chart reference lines for targets;
dual Y axis; real recorded data in the history replay; Chart.js self-hosted;
system notification on target reached; calibration badges, validation and
resume; start-cook dialog shows the targets it will use.

Not worth it or wrong: light mode (marked ignore); undo toast for settings
(the inputs stay visible, retype them); swipe between tabs (conflicts with the
approved chart pan); chart loading skeleton; aggregate statistics; note
export (covered by cook export); reset-to-defaults (delete the config files
on the SD card); cook countdown (the ETA badge already does this); battery
indicator (the device is USB powered); quick temperature reference (the
preset chips are that); chart-resize debounce note (the code resizes rather
than rebuilds); multi-device conflict "merge strategy" (rewritten as the
tick-sync item in §2, which is the actual gap).
