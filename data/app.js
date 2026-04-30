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
      responsive: false,
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

  chart.update('none');
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

// ── Settings sheet ───────────────────────────────────────────────────────────

function openSettings() {
  document.getElementById('settings-sheet').classList.add('open');
  document.getElementById('settings-sheet').setAttribute('aria-hidden', 'false');
}

function closeSettings() {
  document.getElementById('settings-sheet').classList.remove('open');
  document.getElementById('settings-sheet').setAttribute('aria-hidden', 'true');
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
  sendSettings(); // push to device if connected

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

// ── WebSocket (live device) ──────────────────────────────────────────────────

let wsSocket     = null;
let wsConnected  = false;
let lastChartAdd = 0;
const pendingCalTemp = { meat: [null, null, null], grill: [null, null, null] };

function setConnectionState(live) {
  wsConnected = live;
  const pill = document.getElementById('connection-pill');
  if (!pill) return;
  pill.textContent = live ? 'LIVE' : 'DEMO';
  pill.classList.toggle('live', live);
}

function connectWS() {
  try {
    wsSocket = new WebSocket(`ws://${location.host}/ws`);
  } catch (e) { return; }

  wsSocket.addEventListener('open', () => {
    setConnectionState(true);
    // Sync current targets to device on connect
    sendSettings();
  });

  wsSocket.addEventListener('message', e => {
    let data;
    try { data = JSON.parse(e.data); } catch { return; }

    if (data.type === 'cal_reading') { handleCalReading(data); return; }
    if (data.type === 'cal_saved')   { handleCalSaved(data);   return; }

    // Temperature broadcast from firmware
    const meatC  = parseFloat(data.meat.c);
    const grillC = parseFloat(data.grill.c);
    const now    = Date.now();

    // Add a chart point every 30 s
    if (now - lastChartAdd >= 30000) {
      lastChartAdd = now;
      const ts = new Date();
      meatHistory.push({ ts, c: meatC });
      grillHistory.push({ ts, c: grillC });
      if (meatHistory.length > 240) { meatHistory.shift(); grillHistory.shift(); }
      appendChartPoint(meatC, grillC);
    }

    updateUI(meatC, grillC);
  });

  wsSocket.addEventListener('close',  () => { setConnectionState(false); setTimeout(connectWS, 3000); });
  wsSocket.addEventListener('error',  () => { /* close fires next */ });
}

function sendSettings() {
  if (!wsConnected || !wsSocket) return;
  wsSocket.send(JSON.stringify({
    type:        'settings',
    meat_target: settings.meatTarget,
    grill_low:   settings.grillLow,
    grill_high:  settings.grillHigh,
  }));
}

// ── Main simulation tick ─────────────────────────────────────────────────────

function tick() {
  if (wsConnected) return;   // live mode: firmware drives updates via WebSocket

  simElapsed += 1;
  const meatC  = simMeat(simElapsed);
  const grillC = simGrill(simElapsed);

  if (simElapsed % 30 === 0) {
    const ts = new Date();
    meatHistory.push({ ts, c: meatC });
    grillHistory.push({ ts, c: grillC });
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

  const wrap = document.querySelector('.chart-wrap');
  chart.canvas.style.width  = wrap.clientWidth  + 'px';
  chart.canvas.style.height = wrap.clientHeight + 'px';
  chart.resize();

  let resizeTimer;
  window.addEventListener('resize', () => {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(() => chart.resize(), 400);
  });

  // Attempt live device connection; simulation runs as fallback
  connectWS();
  tick();
  setInterval(tick, 1000);
});

// ── Calibration ──────────────────────────────────────────────────────────────

const calState = {
  meat:  [null, null, null],
  grill: [null, null, null],
};

function simResistance(tempC) {
  const R0 = 100000, B = 3950, T0 = 298.15;
  return R0 * Math.exp(B * (1 / (tempC + 273.15) - 1 / T0));
}

function calcSHCoeffs(points) {
  const [p1, p2, p3] = points;
  const T1 = p1.tempC + 273.15, T2 = p2.tempC + 273.15, T3 = p3.tempC + 273.15;
  const L1 = Math.log(p1.resistance), L2 = Math.log(p2.resistance), L3 = Math.log(p3.resistance);
  const Y1 = 1 / T1, Y2 = 1 / T2, Y3 = 1 / T3;
  const g2 = (Y2 - Y1) / (L2 - L1);
  const g3 = (Y3 - Y1) / (L3 - L1);
  const C  = (g3 - g2) / ((L3 - L2) * (L3 + L2 + L1));
  const B  = g2 - C * (L2 * L2 + L2 * L1 + L1 * L1);
  const A  = Y1 - (B + C * L1 * L1) * L1;
  return { A, B, C };
}

function toggleCal() {
  const body    = document.getElementById('cal-body');
  const chevron = document.getElementById('cal-chevron');
  const btn     = document.getElementById('cal-toggle-btn');
  const open    = body.hidden;
  body.hidden   = !open;
  chevron.textContent = open ? '▴' : '▾';
  btn.setAttribute('aria-expanded', String(open));
}

function recordPoint(probe, index) {
  const tempC = parseFloat(document.getElementById(`cal-${probe}-t${index + 1}`).value);
  if (isNaN(tempC)) return;

  const btn = document.getElementById(`cal-${probe}-btn-${index}`);
  btn.textContent = 'Reading…';
  btn.disabled = true;

  if (wsConnected && wsSocket) {
    // Live mode — ask the firmware to read the ADC right now
    pendingCalTemp[probe][index] = tempC;
    wsSocket.send(JSON.stringify({
      type:  'cal_read',
      probe: probe === 'meat' ? 0 : 1,
      point: index,
    }));
  } else {
    // Demo mode — simulate a plausible resistance
    const resistance = simResistance(tempC);
    calState[probe][index] = { tempC, resistance };
    updateCalPointUI(probe, index, resistance);
  }
}

function updateCalPointUI(probe, index, resistance) {
  document.getElementById(`cal-${probe}-r${index}`).textContent =
    Math.round(resistance).toLocaleString() + ' Ω';
  const btn = document.getElementById(`cal-${probe}-btn-${index}`);
  btn.textContent = 'Recorded ✓';
  btn.disabled    = false;
  btn.classList.add('recorded');
  if (calState[probe].every(p => p !== null)) {
    document.getElementById(`cal-${probe}-calc`).disabled = false;
  }
}

function handleCalReading(data) {
  // { type:'cal_reading', probe:0, point:0, resistance:336400 }
  const probe = data.probe === 0 ? 'meat' : 'grill';
  const index = data.point;
  const tempC = pendingCalTemp[probe][index] ?? 0;
  calState[probe][index] = { tempC, resistance: data.resistance };
  updateCalPointUI(probe, index, data.resistance);
}

function handleCalSaved(data) {
  const probe = data.probe === 0 ? 'meat' : 'grill';
  const btn   = document.getElementById(`cal-${probe}-calc`);
  btn.textContent = data.saved ? 'Saved to SD ✓' : 'Save failed ✗';
  setTimeout(() => { btn.textContent = 'Calculate & Save'; }, 2500);
}

function calculateCoeffs(probe) {
  const points = calState[probe];
  if (!points.every(p => p !== null)) return;

  const { A, B, C } = calcSHCoeffs(points);

  document.getElementById(`cal-${probe}-A`).textContent = A.toExponential(4);
  document.getElementById(`cal-${probe}-B`).textContent = B.toExponential(4);
  document.getElementById(`cal-${probe}-C`).textContent = C.toExponential(4);
  document.getElementById(`cal-${probe}-result`).classList.remove('hidden');

  if (wsConnected && wsSocket) {
    wsSocket.send(JSON.stringify({
      type:      'cal_save',
      probe:     probe === 'meat' ? 0 : 1,
      A, B, C,
      points:    points.map(p => ({ temp_c: p.tempC, resistance: p.resistance })),
      timestamp: new Date().toISOString(),
    }));
  } else {
    // Demo mode — no device to save to, just show confirmation
    const btn = document.getElementById(`cal-${probe}-calc`);
    btn.textContent = 'Saved ✓';
    setTimeout(() => { btn.textContent = 'Calculate & Save'; }, 2500);
  }
}
