# Bug: Horizontal Scroll Triggered by iOS Keyboard on BBQ Monitor UI

## Status
Open — root cause identified, fix not yet implemented.

## Environment
- Device: iPhone (iOS Safari)
- Page: `data/index.html` (BBQ Monitor demo UI)
- Reproduced by: tapping any number input in the Targets section, then tapping "Update Targets"

---

## Symptom

After tapping a number input (which raises the iOS keyboard) and then tapping "Update Targets" to dismiss the keyboard, the page becomes horizontally scrollable. It stays scrollable until a full page reload.

---

## Root Cause

### 1. Chart.js has a ResizeObserver running at all times

`responsive: true` is set on the chart. Chart.js uses a `ResizeObserver` internally that watches the chart container for any dimension change and immediately writes a new pixel width to `canvas.style.width`.

### 2. The iOS keyboard triggers layout shifts

When the keyboard appears or dismisses, iOS Safari:
- Shrinks/expands the visual viewport height
- Scrolls the page to keep the focused element in view
- Runs a layout transition over ~250ms

During this transition, any `clientWidth` measurement on a container may return a temporarily incorrect value.

### 3. The canvas gets a bad width mid-transition

The ResizeObserver fires during the keyboard animation. Chart.js reads the container `clientWidth` (which is mid-transition and potentially wrong) and writes that value as an inline `style.width` on the canvas. If that value is even 1px wider than the viewport, iOS flags the document as horizontally scrollable.

### 4. The flag sticks

iOS Safari's horizontal scroll decision (`document.scrollWidth > document.clientWidth`) is set during layout. Once triggered, it persists until a full page reload — even after the canvas snaps back to the correct size.

### 5. `chart.resize()` doesn't help

The `chart.resize()` call added in commit `981121a` fires synchronously during `applySettings()`, which runs while the keyboard is still in mid-dismissal. It reads the same bad `clientWidth` and writes the same bad width back.

---

## What Was Already Tried

| Commit | Change | Result |
|---|---|---|
| `76c1532` | `overflow-x: hidden` on html/body, `min-width: 0` on flex/grid items | Helped general case, not iOS keyboard |
| `d8b08d3` | `overflow: hidden` on cards, `minmax(0,1fr)` on grid, `max-width: 100% !important` on canvas | Helped general case, not iOS keyboard |
| `981121a` | Update chart datasets in-place, call `chart.resize()` after `chart.update('none')` | `chart.resize()` still fires mid-transition |

---

## Planned Fix

**Replace `responsive: true` with a debounced manual resize listener.**

1. Set `responsive: false` on the chart at init — disables Chart.js's internal ResizeObserver entirely.
2. Add a `window.addEventListener('resize', ...)` handler that is debounced by **400ms** (longer than the ~250ms iOS keyboard animation).
3. The debounced handler calls `chart.resize()` once the layout has fully settled.
4. This means the chart never touches `clientWidth` during a keyboard transition.

### Pseudocode

```javascript
// In initChart options:
responsive: false,
maintainAspectRatio: false,

// After initChart:
let resizeTimer;
window.addEventListener('resize', () => {
  clearTimeout(resizeTimer);
  resizeTimer = setTimeout(() => chart.resize(), 400);
});
```

The canvas should also be given an explicit initial size matching its container so Chart.js doesn't start with a zero-size canvas:

```javascript
function setInitialChartSize() {
  const wrap = document.querySelector('.chart-wrap');
  chart.canvas.style.width  = wrap.clientWidth  + 'px';
  chart.canvas.style.height = wrap.clientHeight + 'px';
  chart.resize();
}
```

---

## Files to Change

- `data/app.js` — `initChart()` options and add debounced resize listener
- No CSS changes needed

---

## Testing Checklist

- [ ] Tap a number input → keyboard appears → no horizontal scroll
- [ ] Tap "Update Targets" → keyboard dismisses → no horizontal scroll
- [ ] Rotate device → chart resizes correctly after debounce settles
- [ ] °C/°F toggle still works and chart re-renders correctly
- [ ] Targets update correctly on the chart reference lines
