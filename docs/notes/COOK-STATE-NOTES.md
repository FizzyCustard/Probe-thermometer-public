# Cook State Persistence — Investigation Notes

## The Problem

When a cook was started, the browser set `window.__cookState.active = true` and stored all cook metadata (name, preset, `startedAt`, notes) purely in browser RAM. Opening the device in a new tab or refreshing the page showed **"No active cook"** even though the device was still logging to the SD card — because `window.__cookState` was re-initialised to `{ active: false }` on every page load.

---

## What Was Already in Place (Pre-Fix)

| Behaviour | Status |
|---|---|
| Cook CSV written to SD card every 30 s during active cook | ✅ Working |
| Cook history (past cooks) fetched from SD card on WS connect via `request_cook_list` | ✅ Working |
| 1-second broadcast included `cook_active`, `cook_name`, `cook_elapsed_s` | ✅ Working |
| Browser synced `active` and `name` from broadcast | ✅ Partial |
| Browser set `startedAt` from broadcast | ❌ Missing |
| `renderCookBar()` called after broadcast sync | ❌ Missing |
| Active cook state included in `sendConfig` (sent on connect) | ❌ Missing |

The broadcast partial-sync meant `window.__cookState.name` was eventually set (within ~1 s), but `startedAt` was never populated so the elapsed timer always read `NaN` or `0`, and the cook bar was never re-rendered to show the active state.

---

## The Fix

Three files changed:

### `arduino/bbq-thermometer/cook_session.h`
- Added `char startedAt[24]` to the `CookSession` struct
- In `cookStart()`, stores the ISO timestamp in `activeCook.startedAt` (it was already written to the CSV header but discarded from RAM)

### `arduino/bbq-thermometer/websocket_handler.h`
- Extended `sendConfig()` to include active cook state when `activeCook.active`:
  - `cook_active`, `cook_name`, `cook_preset`
  - `cook_elapsed_s`, `cook_started_at`
  - `cook_meat_target`, `cook_grill_low`, `cook_grill_high`
- `sendConfig` fires on every `WS_EVT_CONNECT`, so every new browser session gets this immediately

### `device/data/app.js`
- Extended `handleConfig()` to restore `window.__cookState` when `data.cook_active` is true:
  - Sets `active`, `name`, `preset`, `startedAt = Date.now() - elapsed_s * 1000`
  - Adds a system note: *"Cook already in progress · joined at X elapsed"*
  - Calls `renderCookBar()`, `renderNotesCard()`, `toggleNotesVisibility()`
- Added an `else if` guard: if device says `cook_active: false`, browser clears its own active state (handles device-restart edge case)

---

## Important Caveat

The fix restores state from **device RAM** (the `activeCook` struct), not directly from the SD card file. If the device itself restarts mid-cook:
- `activeCook` is cleared from RAM
- The SD card CSV still exists (with data up to the restart) but has no `#stopped_at` line
- The browser would show "No active cook" after reconnecting

Fully recovering from a device restart would require detecting incomplete cook files on boot (has `#started_at` but no `#stopped_at`) and re-populating `activeCook` from the CSV header. That is out of scope for this fix.
