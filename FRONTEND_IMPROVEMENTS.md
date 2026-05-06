# BBQ Monitor — Frontend Improvements Audit

A thorough review of the current UI/UX across the web dashboard (`data/` and `device/data/`). Items are grouped by area and loosely ordered by impact within each section.

---

## 1. UI / Visual Design

### Probe card asymmetry
The Meat card has a progress bar, target label, and ETA badge. The Grill card has none of these — it only shows the temperature and a plain range label. Adding a visual indicator of where the grill sits within its target range (e.g. a small range bar with a marker) would bring parity and give the user a quicker at-a-glance read.

### Temperature readout could scale up on larger screens
The `probe-temp` font size is fixed rather than fluid. On a tablet or wide monitor the cards occupy a lot of empty space while the temperature number stays small. A `clamp()`-based fluid size would use that space better.

### No favicon or PWA web app manifest
The app has no `<link rel="icon">` and no `manifest.json`. Adding both would improve the experience when the page is bookmarked or added to a home screen — the user sees a blank page icon today.

### Settings button is easy to miss
The fixed bottom `⚙ Settings` button is a low-contrast ghost pill. Users exploring the UI for the first time can miss it. Giving it a more prominent style or adding a brief tooltip/hint on first visit would reduce friction.

### No light mode
The app is dark-only. Users in bright outdoor environments (which is where BBQs happen) sometimes struggle with dark UIs. A system-preference-aware light mode (`prefers-color-scheme: light`) and/or a manual toggle would help.

### Connection pill has no visual weight
The `DEMO` / `LIVE` pill in the header uses a small text tag that blends into the brand area. Differentiating the colour between demo (e.g. amber) and live (e.g. green) would let users confirm connection state at a glance without reading.

### Status pill colours are colour-only
`IN RANGE`, `TOO LOW`, `TOO HIGH`, `TARGET REACHED` rely entirely on colour to communicate state. Adding a small icon alongside each status (e.g. ✓, ↓, ↑, 🎯) makes the indicator readable for colour-blind users and on low-brightness screens.

### No loading skeleton for the chart
On initial load (especially on the live device where data may take a moment to arrive) the chart area is blank. A simple pulsing skeleton placeholder would prevent the layout from looking broken while data is loading.

---

## 2. UX & Interactions

### No connecting / loading state
When the page loads or the WebSocket reconnects, there is no explicit indication that the app is trying to establish a connection. The user sees `DEMO` mode immediately but there is no "Connecting…" transition state before the fallback kicks in. A brief connecting animation before switching to demo mode would reduce confusion.

### No input validation feedback
Settings inputs (meat target, grill low/high) have no visible validation. Users can enter impossible values (e.g. grill low > grill high, or a meat target of 500 °C) and the form silently accepts them. Inline error messages under the fields would catch this before the values are sent to the device.

### No undo after applying settings
The "Update Targets" button applies immediately with only a brief "Updated ✓" text flip. There is no way to revert a change if the user made a typo. A brief toast with an "Undo" action (keeping the previous values in memory for ~5 s) would reduce mistakes during a cook.

### History tab badge only updates on page load
The `tab-count` badge on the History tab is set once in the `DOMContentLoaded` handler. If the user completes a cook and navigates to History without reloading, the count may be stale. It should update whenever a cook is added to history.

### No keyboard navigation for the settings sheet
The settings sheet traps focus badly — there is no explicit focus trap, so Tab can escape the sheet into the hidden main content. Implementing a proper focus trap (`inert` on the background or manual Tab intercept) would fix keyboard accessibility.

### Swipe gesture to switch views not implemented
On mobile, switching between Live and History requires tapping the tab bar. A horizontal swipe gesture between the two panels would feel more native and is already expected by smartphone users.

### No confirmation before deleting a favourite
Tapping ✕ on a favourite deletes it immediately with no undo. A single extra confirmation step or brief undo toast would prevent accidental deletions.

### Start-cook dialog pre-fills targets from settings, but no visual confirmation
The user might change settings and then start a cook — the dialog pre-fills the current targets, but there is nothing drawing the user's eye to verify them. A summary line ("Using meat 74 °C · grill 160–200 °C") before the inputs would help.

---

## 3. Data Visualisation

### No reference lines on the live chart
The live temperature history chart shows two lines (meat, grill) but no horizontal reference lines for the meat target or the grill low/high bounds. Adding these as dashed annotation lines would let the user see proximity to target without looking away from the chart.

### No zoom or pan on the live chart
The chart is locked to a rolling 60-minute window. If the user wants to inspect a specific period (e.g. right when they added a spritz) they cannot zoom in. Chart.js supports zoom/pan via a plugin; enabling it would make the chart much more useful on longer cooks.

### Grill and meat use the same Y axis
Meat and grill temperatures often occupy very different ranges (e.g. meat 40–80 °C, grill 150–220 °C). Plotting both on a single axis means one line is often cramped into a small band. A dual Y-axis (left for meat, right for grill) would give each line the full visual range.

### Replay chart in History is synthetic
The history replay chart reconstructs the curve mathematically from just peak values and duration — it is not real recorded data. Storing actual temperature readings to the SD card during a cook and replaying those would give a genuine trace. This is a firmware change as much as a UI one, but the UI should be designed to accommodate real data.

### No chart export
There is no way to save or share a chart. A "Save image" button that calls `canvas.toDataURL()` and triggers a download would let users keep a record of a cook.

### Sparklines in history list have no axis labels
The small sparkline SVGs in the history rows give a rough shape of the cook but have no temperature scale or time axis. Even a min/max label at the ends of the Y axis would add interpretability.

---

## 4. Cook Notes

### No quick-action note buttons
Users frequently add the same notes during a cook (e.g. "Spritzed", "Wrapped in foil", "Lid opened", "Added wood"). A row of tap-to-insert chip buttons above the note input would speed up note-taking, especially with smoky hands.

### No note editing or deletion
Once a note is submitted it is permanent. A long-press or swipe-to-reveal action to edit or delete a note would give users control over their session log.

### Notes card hidden until cook is active
The Notes card only appears after `Start cook` is tapped. For a user who just loaded the page mid-cook (e.g. on a second device) the notes they have already added are not visible until they know to look in History. Consider showing the notes card (or a summary) any time a cook is active, even if the page was freshly loaded with data from the device.

### No note export
There is no way to copy or export the notes from a cook. A "Copy notes" button or export to text/markdown would be useful for sharing cook logs.

---

## 5. History & Analytics

### No filter or sort on the history list
History is shown in reverse-chronological order with no way to filter by date, preset, cook name, or favourite status. A simple sort-by selector and a search input would make the list useful as the number of cooks grows.

### No cook comparison
There is no way to open two cook histories side-by-side. A "Compare" mode where the user picks two cooks and sees both replay charts overlaid would help them iterate on technique.

### No aggregate statistics
There is no summary view showing things like: total cooks recorded, average cook duration, most-used preset, longest cook. A simple stats card at the top of the History tab would surface this data without extra navigation.

### No data export
History data lives only in device memory / SD card. There is no export to CSV, JSON, or a shareable link. An export function would let users back up their cooks and analyse them in a spreadsheet.

### Favourite star in history detail is easy to miss
The `★` button in the detail modal header is small and borderless. Making it more prominent (e.g. a pill button reading "★ Favourite") or moving it to a more predictable location would improve discoverability.

---

## 6. Settings & Calibration

### No validation that grill low < grill high
The two grill range inputs accept any number independently. If `grill-low` is set higher than `grill-high` the range indicator will invert silently. A validation check with an inline error message would prevent this.

### Calibration status is not persisted visually
After a successful calibration the user sees the coefficients briefly, but on the next page load there is no indicator that the probe has been calibrated. A small "Calibrated ✓ [date]" badge per probe would give confidence that calibration is in effect.

### No validation that calibration temperatures are monotonically increasing
The three calibration reference points (typically 0 °C, 25 °C, 100 °C) are entered freely. If the user enters them in the wrong order or with duplicates, the Steinhart-Hart calculation will produce bad coefficients silently. Validating the ordering before allowing "Calculate & Save" would prevent corrupted calibration data.

### Calibration collapse is not remembered
Every time the user opens Settings the calibration section is collapsed. If a user is actively going through the calibration workflow they have to re-expand it each visit. Persisting the open/closed state in `localStorage` would reduce clicks.

### Favourites have no edit-in-place
Saved favourites can only be loaded or deleted. There is no way to rename a favourite or adjust its target temperatures without deleting and re-saving it. An edit flow (tap name to rename, tap targets to adjust) would make favourites more useful.

### No "Reset to defaults" option in Settings
There is no way to clear all favourites and return settings to factory defaults without manually deleting each favourite and re-entering default targets. A single destructive "Reset settings" action (with confirmation) would be useful for troubleshooting.

---

## 7. Connectivity & Device State

### No signal strength display
The header shows `DEMO` or `LIVE` but there is no WiFi signal strength indicator. On a live device session, a small signal strength icon (available from the device via WebSocket) would help the user know if they are at risk of losing connection as they move around.

### No explicit sync status
When the user taps "Update Targets" the values are sent to the device, but there is no confirmation that the device received and applied them beyond the "Updated ✓" text flip. A tick that changes to a warning icon if the WebSocket ack is not received within a few seconds would give genuine feedback.

### No offline / reconnect handling
If the WebSocket drops mid-cook, the UI falls back to simulation with no visible indication that it has lost the live feed. A persistent warning banner while in a disconnected-but-was-live state would prevent the user from trusting simulated data as real.

### No conflict resolution if device config diverges
If the user changes settings from two devices simultaneously (phone + tablet both connected), there is no merge strategy. The last writer wins silently. At minimum the UI should note "Settings updated by another device" when it receives a config push it did not initiate.

---

## 8. Accessibility

### Missing `aria-label` on several interactive elements
- The `★` favourite toggle buttons have no `aria-label` that changes based on state ("Add to favourites" / "Remove from favourites")
- The history sparkline containers have no `aria-label` describing what they represent
- The note `Add` button could carry more context (`aria-label="Add cook note"`)

### No visible focus ring on card-level buttons
Custom buttons (fav rows, preset chips, history rows) suppress the default browser focus ring without providing a custom one. Keyboard users have no visible indication of focus position. Adding `:focus-visible` outlines restores this.

### Colour-only status communication (see also Visual Design §7)
`aria-label` on status pills should include the status text even if an icon is added, so screen readers announce it correctly.

### Settings sheet does not implement a focus trap
Tabbing through the settings sheet can move focus to elements behind it, which are visually hidden. A focus trap (`inert` attribute or manual Tab intercept) is required for modal accessibility compliance.

---

## 9. Performance & PWA

### No service worker
The app has no service worker, so it cannot be used offline or cached for fast repeat loads. On the live device (ESP8266), a service worker caching the static assets would reduce re-download overhead and allow the dashboard to load even if the device is briefly unreachable.

### No web app manifest
Without a `manifest.json` (name, icons, `display: standalone`, theme colour), adding the page to the home screen produces a generic browser bookmark. A manifest would enable a proper installed-app experience.

### Chart resize debounce could be tighter
The window resize handler rebuilds the Chart.js instance after a 400 ms debounce. On rapid resize (e.g. split-screen on iPad) this causes visible flicker. Separating canvas resize (immediate) from full chart rebuild (debounced) would reduce the flash.

### CDN dependency for Chart.js
Chart.js is loaded from `cdn.jsdelivr.net`. On a local device network with no internet access, this request will fail and the chart will not render. Bundling or self-hosting Chart.js in the firmware flash would make the dashboard work offline.

---

## 10. Missing Features

### Alarm / push notification on target reached
When the meat hits its target temperature the UI pulses pink, but there is no sound or system notification. Implementing a Web Audio API beep and/or the Web Notifications API (with a permission prompt on first cook start) would let the user walk away from the screen without missing the target.

### Temperature trend indicator
The ETA badge shows estimated minutes to target but there is no simple rising/falling arrow on each probe card. A small ↑ / → / ↓ indicator (based on the last few readings) would immediately convey whether the temperature is climbing, plateauing, or falling.

### Cook timer countdown
The cook bar shows elapsed time. A companion countdown ("~2 h 14 m to target") alongside the ETA badge on the probe card, displayed more prominently, would be useful for planning.

### Battery level indicator (if device supports it)
If the ESP8266 firmware can report a battery ADC reading, surfacing a battery level icon in the header would help users know when to recharge the device before a long cook.

### Multiple probe support
The current UI is hardcoded to exactly two probes (Meat, Grill). Designing the probe grid to dynamically render N probe cards from a config array would future-proof the UI for multi-probe setups without a full rewrite.

### Recipe / preset builder
The current presets are hardcoded in JavaScript. A UI that lets users create and name their own presets (meat target + grill range + default cook name) would replace the need to re-enter the same targets for repeat cooks.

### Quick temperature reference panel
A collapsible reference table of common doneness temperatures (rare, medium-rare, well-done for various meats) accessible from the main screen — without leaving the live view — would reduce the need to look things up externally.

---

*Generated from a full codebase audit of `data/index.html`, `data/app.js`, `data/roadmap.js`, and `data/style.css`.*
