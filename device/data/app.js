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
    { ...makeLine('Meat',  meatHistory.map(d => toDisplay(d.c)),  '#ff4d8d', 'rgba(255,77,141,0.08)'), yAxisID: 'y'  },
    { ...makeLine('Grill', grillHistory.map(d => toDisplay(d.c)), 'rgba(255,255,255,0.7)', null),       yAxisID: 'y2' },
    { ...makeDash('Meat target', Array(len).fill(toDisplay(meatTarget)), 'rgba(255,77,141,0.4)'),        yAxisID: 'y'  },
    { ...makeDash('Grill low',   Array(len).fill(toDisplay(grillLow)),   'rgba(255,200,74,0.35)'),       yAxisID: 'y2' },
    { ...makeDash('Grill high',  Array(len).fill(toDisplay(grillHigh)),  'rgba(255,200,74,0.35)'),       yAxisID: 'y2' },
  ];
}

function chartYBoundsMeat() {
  return unit === 'F' ? { min: 32, max: 320 } : { min: 0, max: 160 };
}
function chartYBoundsGrill() {
  return unit === 'F' ? { min: 32, max: 610 } : { min: 0, max: 320 };
}

function initChart() {
  const ctx = document.getElementById('chart').getContext('2d');
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
          position: 'left',
          border: { display: false },
          grid:   { color: 'rgba(255,255,255,0.04)' },
          min: chartYBoundsMeat().min,
          max: chartYBoundsMeat().max,
          ticks: {
            color: 'rgba(255,77,141,0.6)',
            font: { size: 11 },
            callback: v => v + unitLabel(),
          },
        },
        y2: {
          position: 'right',
          border: { display: false },
          grid:   { drawOnChartArea: false },
          min: chartYBoundsGrill().min,
          max: chartYBoundsGrill().max,
          ticks: {
            color: 'rgba(255,255,255,0.35)',
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
  const len = meatHistory.length;

  // Update in-place so Chart.js doesn't tear down and recreate dataset
  // objects, which triggers a full resize cycle that can overshoot on iOS
  chart.data.labels = meatHistory.map(d => timeLabel(d.ts));
  chart.data.datasets[0].data = meatHistory.map(d => toDisplay(d.c));
  chart.data.datasets[1].data = grillHistory.map(d => toDisplay(d.c));
  chart.data.datasets[2].data = Array(len).fill(toDisplay(meatTarget));
  chart.data.datasets[3].data = Array(len).fill(toDisplay(grillLow));
  chart.data.datasets[4].data = Array(len).fill(toDisplay(grillHigh));

  chart.options.scales.y.min  = chartYBoundsMeat().min;
  chart.options.scales.y.max  = chartYBoundsMeat().max;
  chart.options.scales.y.ticks.callback  = v => v + unitLabel();
  chart.options.scales.y2.min = chartYBoundsGrill().min;
  chart.options.scales.y2.max = chartYBoundsGrill().max;
  chart.options.scales.y2.ticks.callback = v => v + unitLabel();
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

  // ── Meat probe ──
  if (meatC != null) {
    document.getElementById('meat-temp').textContent = fmt(meatC);
    document.getElementById('meat-unit').textContent = unitLabel();

    const startC = 18;
    const pct = Math.min(100, Math.max(0, (meatC - startC) / (meatTarget - startC) * 100));
    document.getElementById('meat-progress-fill').style.width = pct.toFixed(1) + '%';
    document.getElementById('meat-progress-pct').textContent = Math.round(pct) + '%';

    const etaEl = document.getElementById('eta-badge');
    if (meatC >= meatTarget) {
      etaEl.textContent = 'Done!';
      etaEl.dataset.state = 'done';
    } else {
      const eta = calcETA(meatC);
      etaEl.textContent = eta === null ? 'ETA --' : eta < 1 ? 'ETA < 1 min' : `ETA ${eta} min`;
      etaEl.dataset.state = 'active';
    }

    document.getElementById('meat-target-label').textContent = `Target ${fmtWithUnit(meatTarget)}`;

    const meatCard = document.getElementById('meat-card');
    if (meatC >= meatTarget && !alertFired) {
      alertFired = true;
      meatCard.classList.add('state-reached');
      document.getElementById('meat-pill').textContent = 'TARGET REACHED';
      document.getElementById('meat-pill').className = 'status-pill reached';
      showBanner('Target temperature reached — time to check the meat!');
      showMeatTargetNotification(settings.meatTarget, unit);
    }
  } else {
    document.getElementById('meat-temp').textContent = '--';
    document.getElementById('meat-unit').textContent = '';
    document.getElementById('meat-progress-fill').style.width = '0%';
    document.getElementById('meat-progress-pct').textContent = '0%';
    document.getElementById('eta-badge').textContent = 'ETA --';
    document.getElementById('eta-badge').dataset.state = 'idle';
    document.getElementById('meat-target-label').textContent = 'No probe';
    document.getElementById('meat-pill').textContent = '';
    document.getElementById('meat-pill').className = 'status-pill';
  }

  // ── Grill probe ──
  const grillCard  = document.getElementById('grill-card');
  const grillBadge = document.getElementById('grill-badge');
  if (grillC != null) {
    document.getElementById('grill-temp').textContent = fmt(grillC);
    document.getElementById('grill-unit').textContent = unitLabel();

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
    document.getElementById('grill-range-label').textContent =
      `Range ${fmt(grillLow, 0)}–${fmt(grillHigh, 0)}${unitLabel()}`;
  } else {
    document.getElementById('grill-temp').textContent = '--';
    document.getElementById('grill-unit').textContent = '';
    grillBadge.textContent = 'NO PROBE';
    grillBadge.className   = 'status-pill';
    grillCard.className    = 'card probe-card';
    document.getElementById('grill-range-label').textContent = 'No probe';
  }
}

// ── Banner ──────────────────────────────────────────────────────────────────

function showBanner(msg) {
  document.getElementById('banner-msg').textContent = msg;
  document.getElementById('banner').classList.remove('hidden');
}

function closeBanner() {
  document.getElementById('banner').classList.add('hidden');
}

// ── Notifications ────────────────────────────────────────────────────────────

async function requestNotificationPermission() {
  if (!('Notification' in window)) return;            // API not supported
  if (Notification.permission === 'granted') return;  // already granted
  if (Notification.permission === 'denied') return;   // user blocked — respect it
  try {
    await Notification.requestPermission();
  } catch (e) {
    // Absorbs legacy callback-based browsers (Safari < 15) or any throw
  }
}

function showMeatTargetNotification(meatTargetC, currentUnit) {
  if (!('Notification' in window)) return;
  if (Notification.permission !== 'granted') return;

  const displayTemp = currentUnit === 'F'
    ? Math.round(cToF(meatTargetC))
    : meatTargetC;
  const unitStr = currentUnit === 'F' ? '°F' : '°C';

  const n = new Notification('BBQ Monitor — Target Reached!', {
    body: `Meat hit ${displayTemp}${unitStr} — time to check!`,
    icon: '/favicon.ico',        // silently ignored if absent on the device
    tag:  'bbq-meat-target',     // collapses any duplicate notifications
    requireInteraction: false,   // auto-dismiss; user doesn't need to clear it
  });

  n.onclick = () => { window.focus(); n.close(); };
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
  document.getElementById('settings-overlay').classList.add('open');
}

function closeSettings() {
  document.getElementById('settings-sheet').classList.remove('open');
  document.getElementById('settings-sheet').setAttribute('aria-hidden', 'true');
  document.getElementById('settings-overlay').classList.remove('open');
  const passField = document.getElementById('wifi-pass');
  if (passField) passField.value = '';
  const scanResults = document.getElementById('wifi-scan-results');
  if (scanResults) scanResults.classList.add('hidden');
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

let wsSocket       = null;
let connState      = 'offline'; // 'live' | 'stale' | 'offline'
let lastChartAdd   = 0;
let lastMessageTs  = 0;
let reconnectTimer = null;
let reconnectStep  = 0;
let offlineSince   = 0;
let bannerShown    = false;
const RECONNECT_DELAYS_MS = [2000, 4000, 8000, 16000, 30000];
const STALE_THRESHOLD_MS  = 5000;
const BANNER_THRESHOLD_MS = 15000;
const pendingCalTemp = { meat: [null, null, null], grill: [null, null, null] };

function isLive() { return connState === 'live'; }

function setConnState(state) {
  if (state === connState) return;
  connState = state;

  const pill = document.getElementById('connection-pill');
  if (pill) {
    pill.classList.toggle('live',    state === 'live');
    pill.classList.toggle('stale',   state === 'stale');
    pill.classList.toggle('offline', state === 'offline');
    pill.textContent =
      state === 'live'  ? 'LIVE' :
      state === 'stale' ? 'STALE · TAP TO RECONNECT' :
                          'OFFLINE · TAP TO RECONNECT';
  }

  document.body.classList.toggle('app-stale', state !== 'live');

  if (state === 'offline') {
    if (offlineSince === 0) offlineSince = Date.now();
  } else {
    offlineSince = 0;
    if (bannerShown) { closeBanner(); bannerShown = false; }
  }

  if (state !== 'live') updateSignalStrength(null);
}

function setPillReconnecting() {
  const pill = document.getElementById('connection-pill');
  if (!pill) return;
  pill.textContent = 'RECONNECTING…';
}

function updateSignalStrength(rssi) {
  const el = document.getElementById('signal-icon');
  if (!el) return;
  if (rssi == null) {
    el.classList.remove('visible', 's1', 's2', 's3', 's4');
    el.title = '';
    return;
  }
  const tier = rssi >= -55 ? 's4'
             : rssi >= -65 ? 's3'
             : rssi >= -75 ? 's2'
             :               's1';
  el.classList.remove('s1', 's2', 's3', 's4');
  el.classList.add('visible', tier);
  el.title = `WiFi: ${rssi} dBm`;
}

function scheduleReconnect() {
  clearTimeout(reconnectTimer);
  const delay = RECONNECT_DELAYS_MS[Math.min(reconnectStep, RECONNECT_DELAYS_MS.length - 1)];
  reconnectStep++;
  reconnectTimer = setTimeout(connectWS, delay);
}

function manualReconnect() {
  clearTimeout(reconnectTimer);
  reconnectStep = 0;
  if (wsSocket && wsSocket.readyState !== WebSocket.CLOSED) {
    try { wsSocket.close(); } catch {}
  }
  setPillReconnecting();
  connectWS();
}

function connectWS() {
  try {
    wsSocket = new WebSocket(`ws://${location.host}/ws`);
  } catch (e) {
    scheduleReconnect();
    return;
  }
  setPillReconnecting();

  wsSocket.addEventListener('open', () => {
    reconnectStep = 0;
    lastMessageTs = Date.now();
    setConnState('live');
    sendSettings();
    wsSocket.send(JSON.stringify({ type: 'request_config' }));
    wsSocket.send(JSON.stringify({ type: 'request_cook_list' }));
  });

  wsSocket.addEventListener('message', e => {
    lastMessageTs = Date.now();
    setConnState('live');

    let data;
    try { data = JSON.parse(e.data); } catch { return; }

    if (data.type === 'cal_reading')   { handleCalReading(data);   return; }
    if (data.type === 'cal_saved')     { handleCalSaved(data);     return; }
    if (data.type === 'config')        { handleConfig(data);        return; }
    if (data.type === 'cook_list')     { handleCookList(data);      return; }
    if (data.type === 'cook_data')     { handleCookData(data);      return; }
    if (data.type === 'wifi_networks') { handleWifiNetworks(data);  return; }
    if (data.type === 'wifi_saved')    { handleWifiSaved(data);     return; }
    if (data.type === 'cook_started' || data.type === 'cook_stopped') return;

    if (!data.meat || !data.grill) return;

    // Sync cook state from broadcast
    if (data.cook_active !== undefined && window.__cookState) {
      window.__cookState.active = data.cook_active;
      if (data.cook_name) window.__cookState.name = data.cook_name;
    }

    // AP mode flag — show setup banner
    if (data.ap_mode) setAPModeBanner(true);

    // Temperature broadcast from firmware (null = probe not connected)
    const meatC  = data.meat.c  != null ? parseFloat(data.meat.c)  : null;
    const grillC = data.grill.c != null ? parseFloat(data.grill.c) : null;
    const now    = Date.now();

    // Add a chart point every 30 s (only for connected probes)
    if (now - lastChartAdd >= 30000) {
      lastChartAdd = now;
      const ts = new Date();
      if (meatC  != null) meatHistory.push({ ts, c: meatC });
      if (grillC != null) grillHistory.push({ ts, c: grillC });
      if (meatHistory.length  > 240) meatHistory.shift();
      if (grillHistory.length > 240) grillHistory.shift();
      appendChartPoint(meatC, grillC);
    }

    if (data.rssi != null) updateSignalStrength(data.rssi);
    updateUI(meatC, grillC);
  });

  wsSocket.addEventListener('close', () => {
    setConnState('offline');
    // If wifi save was in-flight when the connection dropped, unblock the button
    const wifiBtn = document.getElementById('wifi-save-btn');
    if (wifiBtn && wifiBtn.disabled && wifiBtn.textContent === 'Saving…') {
      wifiBtn.textContent = 'Save & Connect';
      wifiBtn.disabled = false;
      const wifiStatus = document.getElementById('wifi-save-status');
      if (wifiStatus) wifiStatus.textContent = 'Connection lost — please try again.';
    }
    scheduleReconnect();
  });
  wsSocket.addEventListener('error', () => { /* close fires next */ });
}

function tickConnState() {
  const now = Date.now();
  if (connState === 'live' && now - lastMessageTs > STALE_THRESHOLD_MS) {
    setConnState('stale');
  }
  if (connState === 'offline' && !bannerShown
      && offlineSince && now - offlineSince >= BANNER_THRESHOLD_MS) {
    showBanner('Lost connection to device — tap the pill to reconnect.');
    bannerShown = true;
  }
}

function sendSettings() {
  if (!isLive() || !wsSocket) return;
  wsSocket.send(JSON.stringify({
    type:        'settings',
    meat_target: settings.meatTarget,
    grill_low:   settings.grillLow,
    grill_high:  settings.grillHigh,
  }));
}

function handleConfig(data) {
  if (data.settings) {
    settings.meatTarget = data.settings.meat_target;
    settings.grillLow   = data.settings.grill_low;
    settings.grillHigh  = data.settings.grill_high;
    syncSettingsInputs();
    rebuildChart();
  }
  if (data.presets && data.presets.length && window.__presets !== undefined) {
    window.__presets = data.presets.map(p => ({ name: p.name, c: p.c }));
    if (typeof renderPresetChips === 'function') renderPresetChips();
  }
  if (data.favourites !== undefined && window.__cookState) {
    window.__cookState.favourites = data.favourites.map(f => ({
      id:      f.id,
      name:    f.name,
      targets: { meatTarget: f.meat_target, grillLow: f.grill_low, grillHigh: f.grill_high },
      preset:  f.preset,
    }));
    if (typeof renderFavourites === 'function') renderFavourites();
  }

  if (data.cook_active && window.__cookState) {
    const s = window.__cookState;
    s.active    = true;
    s.name      = data.cook_name || '';
    s.preset    = data.cook_preset || null;
    s.startedAt = Date.now() - (data.cook_elapsed_s || 0) * 1000;
    if (!s.notes.length) {
      s.notes = [{
        ts: 0,
        text: `Cook already in progress · joined at ${formatDuration(data.cook_elapsed_s || 0)} elapsed`,
        system: true,
      }];
    }
    if (data.cook_meat_target) settings.meatTarget = data.cook_meat_target;
    if (data.cook_grill_low)   settings.grillLow   = data.cook_grill_low;
    if (data.cook_grill_high)  settings.grillHigh  = data.cook_grill_high;
    if (typeof renderCookBar         === 'function') renderCookBar();
    if (typeof renderNotesCard       === 'function') renderNotesCard();
    if (typeof toggleNotesVisibility === 'function') toggleNotesVisibility();
  } else if (data.cook_active === false && window.__cookState) {
    window.__cookState.active = false;
  }

  if (data.ap_mode) setAPModeBanner(true);
  updateWifiStatusLine();
}

function handleCookData(data) {
  if (!window.__cookState || !data.file) return;
  const cook = window.__cookState.history.find(c => c.id === data.file);
  if (!cook) return;

  cook.points    = Array.isArray(data.points) ? data.points : [];
  cook.peakMeat  = data.peak_meat_c  != null ? data.peak_meat_c  : cook.peakMeat;
  cook.peakGrill = data.peak_grill_c != null ? data.peak_grill_c : cook.peakGrill;
  cook.targetHitMin = data.target_hit_s != null ? Math.round(data.target_hit_s / 60) : cook.targetHitMin;
  cook.notes     = Array.isArray(data.notes) ? data.notes : [];

  if (window.__cookState.selectedHistoryId === data.file) {
    if (typeof renderReplayChart      === 'function') renderReplayChart(cook, cook.points);
    if (typeof renderHistoryDetailStats === 'function') renderHistoryDetailStats(cook);
    if (typeof renderHistoryDetailNotes === 'function') renderHistoryDetailNotes(cook);
  }
}

function handleCookList(data) {
  if (!window.__cookState || !Array.isArray(data.cooks)) return;
  window.__cookState.history = data.cooks.map(c => ({
    id:         c.file,
    name:       c.name   || c.file,
    preset:     c.preset || null,
    startedAt:  c.started_at ? new Date(c.started_at).getTime() : 0,
    duration: (() => {
      const d = (c.stopped_at && c.started_at)
        ? Math.round((new Date(c.stopped_at) - new Date(c.started_at)) / 1000)
        : NaN;
      return isFinite(d) && d > 0 ? d : (c.duration_s || 0);
    })(),
    targets: {
      meatTarget: c.meat_target_c != null ? c.meat_target_c : 74,
      grillLow:   c.grill_low_c   != null ? c.grill_low_c   : 160,
      grillHigh:  c.grill_high_c  != null ? c.grill_high_c  : 200,
    },
    peakMeat:     null,
    peakGrill:    null,
    targetHitMin: null,
    notes:        [],
    favourite:    false,
  }));
  if (typeof renderHistoryList === 'function') renderHistoryList();
  const el = document.getElementById('tab-history-count');
  if (el) el.textContent = window.__cookState.history.length || '';
}

// ── Init ─────────────────────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
  document.getElementById('btn-c').classList.toggle('active', unit === 'C');
  document.getElementById('btn-f').classList.toggle('active', unit === 'F');
  if (unit === 'F') syncSettingsInputs();

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

  connectWS();
  setInterval(tickConnState, 1000);
});

// ── Calibration ──────────────────────────────────────────────────────────────

const calState = {
  meat:  [null, null, null],
  grill: [null, null, null],
};

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

  if (!isLive() || !wsSocket) {
    btn.textContent = 'Not connected';
    btn.disabled = false;
    return;
  }
  pendingCalTemp[probe][index] = tempC;
  wsSocket.send(JSON.stringify({
    type:  'cal_read',
    probe: probe === 'meat' ? 0 : 1,
    point: index,
  }));
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

  if (isLive() && wsSocket) {
    wsSocket.send(JSON.stringify({
      type:      'cal_save',
      probe:     probe === 'meat' ? 0 : 1,
      A, B, C,
      points:    points.map(p => ({ temp_c: p.tempC, resistance: p.resistance })),
      timestamp: new Date().toISOString(),
    }));
  }
}

// ── WiFi setup ────────────────────────────────────────────────────────────────

let _apMode = false;

function setAPModeBanner(on) {
  _apMode = on;
  const banner = document.getElementById('banner');
  const msg    = document.getElementById('banner-msg');
  if (!banner || !msg) return;
  if (on) {
    msg.textContent = 'Device is in setup mode — open Settings to connect to WiFi.';
    banner.classList.remove('hidden');
  }
}

function updateWifiStatusLine() {
  const el = document.getElementById('wifi-status-line');
  if (!el) return;
  el.textContent = _apMode
    ? 'Not connected to WiFi. Enter your network details below.'
    : 'Connected. Enter new details to switch networks.';
}

function wifiScan() {
  if (!isLive() || !wsSocket) return;
  const btn = document.getElementById('wifi-scan-btn');
  if (btn) { btn.textContent = 'Scanning…'; btn.disabled = true; }
  wsSocket.send(JSON.stringify({ type: 'wifi_scan' }));
}

function handleWifiNetworks(data) {
  const btn = document.getElementById('wifi-scan-btn');
  if (btn) { btn.textContent = 'Scan'; btn.disabled = false; }

  const container = document.getElementById('wifi-scan-results');
  if (!container || !Array.isArray(data.networks)) return;

  container.innerHTML = '';
  if (data.networks.length === 0) {
    const p = document.createElement('p');
    p.className = 'wifi-scan-empty';
    p.textContent = 'No networks found';
    container.appendChild(p);
  } else {
    const sorted = [...data.networks].sort((a, b) => b.rssi - a.rssi);
    sorted.forEach(n => {
      const chip = document.createElement('button');
      chip.className = 'wifi-network-chip';
      chip.textContent = n.ssid;
      const rssiSpan = document.createElement('span');
      rssiSpan.className = 'wifi-rssi';
      rssiSpan.textContent = ` ${n.rssi} dBm`;
      chip.appendChild(rssiSpan);
      chip.addEventListener('click', () => wifiSelectNetwork(n.ssid));
      container.appendChild(chip);
    });
  }
  container.classList.remove('hidden');
}

function wifiSelectNetwork(ssid) {
  const input = document.getElementById('wifi-ssid');
  if (input) { input.value = ssid; }
  document.getElementById('wifi-scan-results').classList.add('hidden');
  document.getElementById('wifi-pass').focus();
}

function wifiSave() {
  const ssid = (document.getElementById('wifi-ssid').value || '').trim();
  const pass =  document.getElementById('wifi-pass').value || '';
  if (!ssid) { alert('Please enter a network name.'); return; }
  if (!isLive() || !wsSocket) return;

  const btn = document.getElementById('wifi-save-btn');
  if (btn) { btn.textContent = 'Saving…'; btn.disabled = true; }

  wsSocket.send(JSON.stringify({ type: 'wifi_config', ssid, password: pass }));
}

function handleWifiSaved(data) {
  const el  = document.getElementById('wifi-save-status');
  const btn = document.getElementById('wifi-save-btn');
  if (data.saved) {
    if (el)  el.textContent = 'Saved — device is restarting…';
    if (btn) { btn.textContent = 'Restarting…'; btn.disabled = true; }
  } else {
    if (el)  el.textContent = 'Save failed — check the device SD card.';
    if (btn) { btn.textContent = 'Save & Connect'; btn.disabled = false; }
  }
}
