# BBQ Monitor — Feature Roadmap

A place to capture ideas and future features before they become code.

---

## Cook History

**Goal:** Save a complete record of each cook to the SD card so previous sessions can be browsed and replayed.

### What's needed

- **Start / Stop cook button** in the web UI
- On Start: create a new dated session file on the SD card (e.g. `/cooks/2025-07-14_pulled-pork.json` or `.csv`)
- On Stop: close the session and write a summary (duration, peak temps, target hit time)
- Log entries written at the existing `LOG_INTERVAL_MS` cadence, tied to the active session
- Cook history screen in the web UI: list past sessions, tap one to view a replay chart

### SD file structure idea

```
/cooks/
  2025-07-14T14-30_cook.csv    ← timestamped per session
  2025-07-14T14-30_meta.json   ← summary: name, notes, duration, targets
```

---

## Favourites

**Goal:** Save the settings from a great cook so it can be recreated exactly.

- Give a cook a name at the end ("Sunday brisket")
- Saves: meat target, grill range, probe coefficients used, any notes
- Favourites list in the UI — tap one to load those settings instantly
- Stored in `/favourites.json` on the SD card

---

## Cook Notes

**Goal:** Attach free-text notes to a cook session (what rub, wood, weather, tweaks).

- Notes field in the Start Cook dialog
- Can also add mid-cook notes (timestamped entries appended to the session)
- Shown alongside the history chart when reviewing a past cook

---

## Preset Temperatures

**Goal:** Quick-select common target temperatures instead of typing a number.

Suggested presets (all in °C, UI converts to °F if needed):

| Preset | Meat target |
|---|---|
| Beef — rare | 52 |
| Beef — medium rare | 57 |
| Beef — medium | 63 |
| Beef — well done | 71 |
| Lamb — medium rare | 57 |
| Lamb — medium | 63 |
| Pork — safe minimum | 71 |
| Chicken — safe minimum | 74 |
| Brisket / pulled pork | 95 |

- Shown as a scrollable chip row above the meat target input in the Settings sheet
- Tapping a chip fills the meat target input; user still presses Update Targets to apply
- Chips are editable / user can add their own (stored in `localStorage` or SD)
