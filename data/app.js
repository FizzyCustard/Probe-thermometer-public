// Helpers
const cToF = c => c * 9 / 5 + 32;
const fToC = f => (f - 32) * 5 / 9;

// State
let unit = localStorage.getItem('bbq_unit') || 'C';
let settings = { meatTarget: 74, grillLow: 160, grillHigh: 200 };
let meatHistory = []; // { ts: Date, c: number }
let grillHistory = [];
let alertFired = false;
let chart;
let simElapsed = 0; // seconds from "now" (history covers -3600 to 0)

// ── Simulation ──────────────────────────────────────────────────────────────
//
// History starts 60 min ago. At t=0 (now), meat is ~48°C heading to 74°C.
// Grill hovers around 178°C with natural fluctuation, occasionally leaving
// the 160–200°C window so we can see the warning states.

function simMeat(s) {
  const minElapsed = 60 + s / 60; // 0 at start of cook, 60 at "now"
  const base = Math.min(18 + minElapsed * 0.5, 74);
  const noise = Math.sin(s * 0.08) * 0.5 + Math.cos(s * 0.15) * 0.3;
  return Math.round((base + noise) * 10) / 10;
}

function simGrill(s) {
  const slow  = Math.sin(s * 0.010) * 18;
  const med   = Math.cos(s * 0.035) * 8;
  const noise = Math.sin(s * 0.100) * 2;
  return Math.round((178 + slow + med + noise) * 10) / 10;
}

function buildHistory() {
  const now = Date.now();
  for (let s = -3600; s <= 0; s += 30) {
    const ts = new Date(now + s * 1000);
    meatHistory.push({ ts, c: simMeat(s) });
    grillHistory.push({ ts, c: simGrill(s) });
  }
}

// ── Display helpers ─────────────────────────────────────────────────────────

function unitLabel() { return unit === 'F' ? '°F' : '°C'; }
function toDisplay(c) { return unit === 'F' ? cToF(c) : c; }
function fmt(c, dp = 1) { return toDisplay(c).toFixed(dp); }
function fmtWithUnit(c, dp = 1) { return fmt(c, dp) + unitLabel(); }

function timeLabel(date) {
  return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

// ── Chart ───────────────────────────────────────────────────────────────────

function makeLine(label, data, color, fill) {
  return {
    label, data,
    borderColor: color,
    backgroundColor: fill || 'transparent',
    borderWidth: 2,
    fill: !!fill,
    tension: 0.4,
    pointRadius: 0,
    pointHoverRadius: 4,
    pointHoverBackgroundColor: color,
  };
}

function makeDash(label, data, color) {
  return {
    label, data,
    borderColor: color,
    borderWidth: 1.5,
    borderDash: [6, 4],
    pointRadius: 0,
    fill: false,
    tension: 0,
  };
}

function chartDatasets() {
  const { meatTarget, grillLow, grillHigh } = settings;
  const len = meatHistory.length;
  return [
    makeLine('Meat',  meatHistory.map(d => toDisplay(d.c)),  '#ff4d8d', 'rgba(255,77,141,0.08)'),
    makeLine('Grill', grillHistory.map(d => toDisplay(d.c)), 'rgba(255,255,255,0.7)', null),
    makeDash('Meat target', Array(len).fill(toDisplay(meatTarget)), 'rgba(255,77,141,0.4)'),
    makeDash('Grill low',   Array(len).fill(toDisplay(grillLow)),   'rgba(255,200,74,0.35)'),
    makeDash('Grill high',  Array(len).fill(toDisplay(grillHigh)),  'rgba(255,200,74,0.35)'),
  ];
}

function chartYBounds() {
  return unit === 'F'
    ? { min: 60,  max: 430 }
    : { min: 0,   max: 220 };
}

function initChart() {
  const ctx = document.getElementById('chart').getContext('2d');
  const bounds = chartYBounds();
  chart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: meatHistory.map(d => timeLabel(d.ts)),
      datasets: chartDatasets(),
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: false,
      interaction: { mode: 'index', intersect: false },
      scales: {
        x: {
          border: { display: false },
          grid:   { color: 'rgba(255,255,255,0.04)' },
          ticks:  { color: '#444', maxTicksLimit: 7, font: { size: 11 } },
        },
        y: {
          border: { display: false },
          grid:   { color: 'rgba(255,255,255,0.04)' },
          min: bounds.min,
          max: bounds.max,
          ticks: {
            color: '#444',
            font: { size: 11 },
            callback: v => v + unitLabel(),
          },
        },
      },
      plugins: {
        legend: {
          display: true,
          position: 'top',
          align: 'end',
          labels: {
            color: '#555',
            font: { size: 11 },
            boxWidth: 20,
            boxHeight: 2,
            padding: 16,
            filter: item => ['Meat', 'Grill'].includes(item.text),
          },
        },
        tooltip: {
          backgroundColor: '#1a1a1a',
          borderColor: 'rgba(255,255,255,0.08)',
          borderWidth: 1,
          titleColor: '#555',
          bodyColor: '#ddd',
          padding: 10,
          filter: item => ['Meat', 'Grill'].includes(item.dataset.label),
          callbacks: {
            label: ctx => ` ${ctx.dataset.label}: ${ctx.parsed.y.toFixed(1)}${unitLabel()}`,
          },
        },
      },
    },
  });
}

function rebuildChart() {
  const { meatTarget, grillLow, grillHigh } = settings;
  const bounds = chartYBounds();
  const len = meatHistory.length;

  // Update in-place so Chart.js doesn't tear down and recreate dataset
  // objects, which triggers a full resize cycle that can overshoot on iOS
  chart.data.labels = meatHistory.map(d => timeLabel(d.ts));
  chart.data.datasets[0].data = meatHistory.map(d => toDisplay(d.c));
  chart.data.datasets[1].data = grillHistory.map(d => toDisplay(d.c));
  chart.data.datasets[2].data = Array(len).fill(toDisplay(meatTarget));
  chart.data.datasets[3].data = Array(len).fill(toDisplay(grillLow));
  chart.data.datasets[4].data = Array(len).fill(toDisplay(grillHigh));

  chart.options.scales.y.min = bounds.min;
  chart.options.scales.y.max = bounds.max;
  chart.options.scales.y.ticks.callback = v => v + unitLabel();
  chart.options.plugins.tooltip.callbacks.label =
    ctx => ` ${ctx.dataset.label}: ${ctx.parsed.y.toFixed(1)}${unitLabel()}`;

  chart.update('none'); // no animation — keeps resize instantaneous
  chart.resize();       // snap canvas back to container size before iOS notices
}

function appendChartPoint(meatC, grillC) {
  const { meatTarget, grillLow, grillHigh } = settings;
  chart.data.labels.push(timeLabel(new Date()));
  chart.data.datasets[0].data.push(toDisplay(meatC));
  chart.data.datasets[1].data.push(toDisplay(grillC));
  chart.data.datasets[2].data.push(toDisplay(meatTarget));
  chart.data.datasets[3].data.push(toDisplay(grillLow));
  chart.data.datasets[4].data.push(toDisplay(grillHigh));
  // Rolling 60-min window: 120 points × 30 s each
  if (chart.data.labels.length > 120) {
    chart.data.labels.shift();
    chart.data.datasets.forEach(ds => ds.data.shift());
  }
  chart.update('none');
}

// ── ETA ─────────────────────────────────────────────────────────────────────

function calcETA(currentC) {
  if (currentC >= settings.meatTarget) return 0;
  const recent = meatHistory.slice(-20); // last ~10 min of 30-s samples
  if (recent.length < 4) return null;
  const t0 = recent[0].ts.getTime();
  const n = recent.length;
  let sx = 0, sy = 0, sxy = 0, sx2 = 0;
  recent.forEach(d => {
    const x = (d.ts.getTime() - t0) / 60000;
    sx += x; sy += d.c; sxy += x * d.c; sx2 += x * x;
  });
  const slope = (n * sxy - sx * sy) / (n * sx2 - sx * sx);
  if (slope < 0.02) return null;
  return Math.round((settings.meatTarget - currentC) / slope);
}

// ── UI updates ───────────────────────────────────────────────────────────────

function updateUI(meatC, grillC) {
  const { meatTarget, grillLow, grillHigh } = settings;

  // Temperatures
  document.getElementById('meat-temp').textContent = fmt(meatC);
  document.getElementById('grill-temp').textContent = fmt(grillC);
  document.getElementById('meat-unit').textContent = unitLabel();
  document.getElementById('grill-unit').textContent = unitLabel();

  // Progress bar (meat only)
  const startC = 18;
  const pct = Math.min(100, Math.max(0, (meatC - startC) / (meatTarget - startC) * 100));
  document.getElementById('meat-progress-fill').style.width = pct.toFixed(1) + '%';
  document.getElementById('meat-progress-pct').textContent = Math.round(pct) + '%';

  // ETA
  const etaEl = document.getElementById('eta-badge');
  if (meatC >= meatTarget) {
    etaEl.textContent = 'Done!';
    etaEl.dataset.state = 'done';
  } else {
    const eta = calcETA(meatC);
    etaEl.textContent = eta === null ? 'ETA --' : eta < 1 ? 'ETA < 1 min' : `ETA ${eta} min`;
    etaEl.dataset.state = 'active';
  }

  // Meat footer label
  document.getElementById('meat-target-label').textContent = `Target ${fmtWithUnit(meatTarget)}`;

  // Meat reached alert
  const meatCard = document.getElementById('meat-card');
  if (meatC >= meatTarget && !alertFired) {
    alertFired = true;
    meatCard.classList.add('state-reached');
    document.getElementById('meat-pill').textContent = 'TARGET REACHED';
    document.getElementById('meat-pill').className = 'status-pill reached';
    showBanner('Target temperature reached — time to check the meat!');
  }

  // Grill status
  const grillCard  = document.getElementById('grill-card');
  const grillBadge = document.getElementById('grill-badge');
  if (grillC < grillLow) {
    grillBadge.textContent = 'TOO LOW';
    grillBadge.className   = 'status-pill warning';
    grillCard.className    = 'card probe-card state-warning';
  } else if (grillC > grillHigh) {
    grillBadge.textContent = 'TOO HIGH';
    grillBadge.className   = 'status-pill warning';
    grillCard.className    = 'card probe-card state-warning';
  } else {
    grillBadge.textContent = 'IN RANGE';
    grillBadge.className   = 'status-pill ok';
    grillCard.className    = 'card probe-card';
  }

  // Grill range label
  document.getElementById('grill-range-label').textContent =
    `Range ${fmt(grillLow, 0)}–${fmt(grillHigh, 0)}${unitLabel()}`;
}

// ── Banner ──────────────────────────────────────────────────────────────────

function showBanner(msg) {
  document.getElementById('banner-msg').textContent = msg;
  document.getElementById('banner').classList.remove('hidden');
}

function closeBanner() {
  document.getElementById('banner').classList.add('hidden');
}

// ── Unit toggle ─────────────────────────────────────────────────────────────

function setUnit(u) {
  if (unit === u) return;
  unit = u;
  localStorage.setItem('bbq_unit', unit);
  document.getElementById('btn-c').classList.toggle('active', u === 'C');
  document.getElementById('btn-f').classList.toggle('active', u === 'F');
  syncSettingsInputs();
  rebuildChart();
  if (meatHistory.length) {
    const m = meatHistory[meatHistory.length - 1].c;
    const g = grillHistory[grillHistory.length - 1].c;
    updateUI(m, g);
  }
}

// ── Settings ─────────────────────────────────────────────────────────────────

function syncSettingsInputs() {
  const { meatTarget, grillLow, grillHigh } = settings;
  document.getElementById('s-meat').value      = Math.round(unit === 'F' ? cToF(meatTarget) : meatTarget);
  document.getElementById('s-grill-low').value  = Math.round(unit === 'F' ? cToF(grillLow)   : grillLow);
  document.getElementById('s-grill-high').value = Math.round(unit === 'F' ? cToF(grillHigh)  : grillHigh);
  ['s-meat-unit', 's-grill-low-unit', 's-grill-high-unit'].forEach(id => {
    document.getElementById(id).textContent = unitLabel();
  });
}

function applySettings() {
  const rawMeat = parseFloat(document.getElementById('s-meat').value);
  const rawLow  = parseFloat(document.getElementById('s-grill-low').value);
  const rawHigh = parseFloat(document.getElementById('s-grill-high').value);
  if ([rawMeat, rawLow, rawHigh].some(isNaN)) return;
  settings.meatTarget = unit === 'F' ? fToC(rawMeat) : rawMeat;
  settings.grillLow   = unit === 'F' ? fToC(rawLow)  : rawLow;
  settings.grillHigh  = unit === 'F' ? fToC(rawHigh) : rawHigh;

  // Reset meat alert so it can fire again against the new target
  alertFired = false;
  document.getElementById('meat-card').classList.remove('state-reached');
  document.getElementById('meat-pill').textContent = '';
  document.getElementById('meat-pill').className   = 'status-pill';

  rebuildChart();
  if (meatHistory.length) {
    updateUI(meatHistory[meatHistory.length - 1].c, grillHistory[grillHistory.length - 1].c);
  }

  const btn = document.getElementById('btn-update');
  btn.textContent = 'Updated ✓';
  setTimeout(() => { btn.textContent = 'Update Targets'; }, 1800);
}

// ── Main simulation tick ─────────────────────────────────────────────────────

function tick() {
  simElapsed += 1;
  const meatC  = simMeat(simElapsed);
  const grillC = simGrill(simElapsed);

  if (simElapsed % 30 === 0) {
    const ts = new Date();
    meatHistory.push({ ts, c: meatC });
    grillHistory.push({ ts, c: grillC });
    // Cap at 2 hours of history
    if (meatHistory.length > 240) { meatHistory.shift(); grillHistory.shift(); }
    appendChartPoint(meatC, grillC);
  }

  updateUI(meatC, grillC);
}

// ── Init ─────────────────────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
  document.getElementById('btn-c').classList.toggle('active', unit === 'C');
  document.getElementById('btn-f').classList.toggle('active', unit === 'F');
  if (unit === 'F') syncSettingsInputs();

  buildHistory();
  initChart();
  tick();
  setInterval(tick, 1000);
});
