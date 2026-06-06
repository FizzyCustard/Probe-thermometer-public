# TODO — Deferred Bug Fixes

Items below were identified in the bug audit but deferred. They are real issues, just lower
priority than the CRITICAL/HIGH items already fixed. Pick them up in a future session.

---

## MEDIUM

### Trailing `\r` in cook CSV timestamp fields
**File:** `arduino/bbq-thermometer/websocket_handler.h` lines 176–177  
On Windows-line-ended files, `f.readStringUntil('\n')` leaves `\r` on the extracted substring.
`started_at` / `stopped_at` arrive at the browser as e.g. `"2025-07-14T08:14:00\r"`, causing
`new Date(…)` to return `Invalid Date` and breaking duration display.  
**Fix:** `cook["started_at"] = line.substring(12).trim();` (same for `stopped_at`).

---

### Magic offset substrings in cook metadata parser
**File:** `arduino/bbq-thermometer/websocket_handler.h` lines 173–175  
`line.substring(15)` for `#meat_target_c,` etc. are hardcoded magic numbers. A one-character
change to the CSV prefix silently parses the wrong value.  
**Fix:** Replace with `line.substring(line.indexOf(',') + 1).toFloat()` for each numeric field.

---

### No `grillLow < grillHigh` validation
**Files:** `device/data/app.js` (`applySettings`) + `device/data/roadmap.js` (`confirmStartCook`)  
An inverted grill range (low=200, high=160) is accepted silently. `updateUI()` then
misclassifies grill temperature as always in-range.  
**Fix:** After unit conversion, check `if (lowC >= highC) { alert(...); return; }` in both functions.

---

### Calibration accepts duplicate temperatures
**File:** `device/data/app.js` (`recordPoint`)  
Steinhart-Hart solving requires three distinct temperatures. Submitting the same temperature
twice causes a divide-by-zero in `calcSHCoeffs()`.  
**Fix:** Before recording, verify the new temperature differs by > 0.5 °C from all already-recorded
points; alert and return if not.

---

### History duration incorrect for in-progress cooks
**File:** `device/data/roadmap.js` (`handleCookList`)  
Active cooks have no `stopped_at`; duration calculation falls back to `c.duration_s` which may
be absent, leaving the duration as 0.  
**Fix:**
```js
duration: c.stopped_at && c.started_at
  ? Math.round((new Date(c.stopped_at) - new Date(c.started_at)) / 1000)
  : c.started_at
    ? Math.round((Date.now() - new Date(c.started_at).getTime()) / 1000)
    : (c.duration_s || 0)
```

---

### No confirmation before deleting a favourite
**File:** `device/data/roadmap.js` (`deleteFavourite`)  
Single tap immediately removes the favourite with no undo — easy to fat-finger on mobile.  
**Fix:** Add `if (!confirm('Delete this favourite?')) return;` at the top of `deleteFavourite()`.

---

## LOW

### Cook list `truncated` flag never shown in UI
**File:** `device/data/roadmap.js` (`handleCookList`)  
Firmware sends `truncated: true` when > 30 cook files exist. The frontend ignores this field —
user silently sees only 30 cooks.  
**Fix:** When `data.truncated`, append a notice below the history list:
"Showing 30 most recent cooks — older files are on the SD card."

---

### Cook name overflow on mobile
**File:** `device/data/style.css`  
`.cook-bar-name` uses `max-width: 180px` hardcoded. Long names (> ~20 chars) clip without
ellipsis on ≤ 375 px screens.  
**Fix:** `max-width: clamp(80px, 45vw, 180px); overflow: hidden; text-overflow: ellipsis; white-space: nowrap;`

---

### AP mode banner text not actionable
**File:** `device/data/app.js`  
"open Settings" is ambiguous for a first-time user.  
**Fix:** Change banner text to: `"Tap ⚙ Settings below to connect to a WiFi network."`
