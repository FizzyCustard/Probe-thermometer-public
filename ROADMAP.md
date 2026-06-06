# BBQ Monitor — Feature Roadmap

A place to capture ideas and future features before they become code.

---

## ✅ Cook History

**Goal:** Save a complete record of each cook to the SD card so previous sessions can be browsed and replayed.

**Completed.** Cook sessions are written to `/BBQ/cooks/cook_YYYY-MM-DDTHH-MM-SS.csv` on the SD card. Temperature samples are logged every 30 seconds. The history screen lists all past sessions fetched from the SD card on connect, and tapping a session opens a detail view with a replay chart.

---

## ✅ Favourites

**Goal:** Save the settings from a great cook so it can be recreated exactly.

**Completed.** Favourites are saved to `/BBQ/config/favourites.csv` on the SD card. The UI shows a favourites list; tapping one loads the name, meat target, grill range, and preset instantly. Favourites can be saved from the Start Cook dialog or after a cook, and deleted from the list.

---

## ✅ Cook Notes

**Goal:** Attach free-text notes to a cook session (what rub, wood, weather, tweaks).

**Completed.** A notes field appears in the Start Cook dialog for an opening note. Mid-cook notes can be added at any time; each is timestamped and appended to the cook's CSV file as `#note` entries. Notes are shown in a timeline alongside the active cook bar.

---

## ✅ Preset Temperatures

**Goal:** Quick-select common target temperatures instead of typing a number.

**Completed.** A scrollable chip row appears above the meat target input. Tapping a chip fills the input and highlights the selection. Presets are loaded from the device on connect and can be extended. Available presets:

| Preset | Meat target |
|---|---|
| Beef — rare | 52 °C |
| Beef — medium rare | 57 °C |
| Beef — medium | 63 °C |
| Beef — well done | 71 °C |
| Lamb — medium rare | 57 °C |
| Lamb — medium | 63 °C |
| Pork — safe minimum | 71 °C |
| Chicken — safe minimum | 74 °C |
| Brisket / pulled pork | 95 °C |

---

## ✅ Cook State Persistence (new tab / page refresh)

**Goal:** Opening the device in a new browser tab or refreshing the page should show the same active cook state as the original tab.

**Completed** (commit `dcb7ba3`). The device now includes full active cook state in the config payload sent to every new WebSocket client on connect. The browser restores cook name, elapsed time, targets, and preset from this immediately on load. See `COOK-STATE-NOTES.md` for full details and caveats.

---

## ✅ Dual Y-Axis Chart

**Goal:** Meat and grill temperatures occupy very different ranges — plotting them on a shared axis cramped the meat line into the bottom quarter of the chart.

**Completed** (commit `071c1d1`). The live chart now uses two independent Y-axes: meat on the left (0–160 °C), grill on the right (0–320 °C). Tick colours match the probe line colours. Both axes rescale correctly when switching between °C and °F.
