# TODO — Deferred Bug Fixes

One small deferred issue plus one thing to confirm on real hardware. Everything else from the
original bug-audit list has since been fixed or overtaken by the redesign — see git history.

---

## LOW

### AP mode banner text not actionable
**File:** `device/data/app.js` (~line 1232)
"open Settings to connect to WiFi" is ambiguous for a first-time user — it doesn't say where.
**Fix:** Change banner text to: `"Tap ⚙ Settings below to connect to a WiFi network."`

---

## Verify on hardware

- **Notes in cook history.** The write → parse → render path is complete in code, but the
  original "notes never appear" report was never confirmed fixed on a real cook. Run one cook
  with a note and open its history detail.

- **Cook list cap.** `sendCookList` now keeps only the 30 newest cooks. The selection was
  property-tested off-device, but the "Showing the 30 most recent cooks" notice has never
  rendered against a real card with more than 30 cook files on it.
