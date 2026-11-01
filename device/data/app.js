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
  if (!chart) return;
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
  if (!chart) return;
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
    // A probe in boiling water during calibration is not a finished brisket
    if (meatC >= meatTarget && !alertFired && !calIsOpen()) {
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
  if (calIsOpen()) calRender();
}

// ── Settings sheet ───────────────────────────────────────────────────────────

function openSettings() {
  document.getElementById('settings-sheet').classList.add('open');
  document.getElementById('settings-sheet').setAttribute('aria-hidden', 'false');
  document.getElementById('settings-overlay').classList.add('open');
  if (_apMode) wifiScan();
}

function closeSettings() {
  document.getElementById('settings-sheet').classList.remove('open');
  document.getElementById('settings-sheet').setAttribute('aria-hidden', 'true');
  document.getElementById('settings-overlay').classList.remove('open');
  const passField = document.getElementById('wifi-pass');
  if (passField) { passField.value = ''; passField.type = 'password'; }
  const passToggle = document.getElementById('wifi-pass-toggle');
  if (passToggle) passToggle.style.opacity = '';
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

  const lowC  = unit === 'F' ? fToC(rawLow)  : rawLow;
  const highC = unit === 'F' ? fToC(rawHigh) : rawHigh;
  // An inverted range makes updateUI() read every grill temperature as in-range.
  if (lowC >= highC) {
    alert('Grill low must be below grill high.');
    return;
  }

  settings.meatTarget = unit === 'F' ? fToC(rawMeat) : rawMeat;
  settings.grillLow   = lowC;
  settings.grillHigh  = highC;
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
let sdWarningShown = false;
const RECONNECT_DELAYS_MS = [2000, 4000, 8000, 16000, 30000];
const STALE_THRESHOLD_MS  = 5000;
const BANNER_THRESHOLD_MS = 15000;

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
    // Note: do NOT push local settings here — the device is the source of
    // truth. Pushing browser defaults on connect used to silently overwrite
    // the targets persisted on the SD card.
    wsSocket.send(JSON.stringify({ type: 'request_config' }));
    wsSocket.send(JSON.stringify({ type: 'request_cook_list' }));
  });

  wsSocket.addEventListener('message', e => {
    lastMessageTs = Date.now();
    setConnState('live');

    let data;
    try { data = JSON.parse(e.data); } catch { return; }

    if (data.type === 'cal_saved')     { handleCalSaved(data);     return; }
    if (data.type === 'config')        { handleConfig(data);        return; }
    if (data.type === 'cook_list')     { handleCookList(data);      return; }
    if (data.type === 'cook_data')     { handleCookData(data);      return; }
    if (data.type === 'wifi_networks') { handleWifiNetworks(data);  return; }
    if (data.type === 'wifi_saved')    { handleWifiSaved(data);     return; }
    if (data.type === 'wifi_testing')  { handleWifiTesting(data);   return; }
    if (data.type === 'fav_result')    { handleFavResult(data);     return; }
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

    // Raw resistances feed the calibration wizard's settling/capture buffers
    calFeed(meatC, data.meat.r, grillC, data.grill.r);

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

    // Firmware reports SD health — warn the moment recording stops working
    if (data.sd === false && !sdWarningShown) {
      sdWarningShown = true;
      showBanner('SD card problem — temperatures are NOT being recorded. Check the card.');
    } else if (data.sd === true && sdWarningShown) {
      sdWarningShown = false;
      closeBanner();
    }

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

  if (data.calibration) calSetInfo(data.calibration);

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
  // Firmware caps the list to keep the payload inside the ESP8266's heap and
  // sends truncated:true when older cooks were left off.
  window.__cookState.historyTruncated = data.truncated === true;
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

  // Guarded so a missing/corrupt chart.js can degrade the chart, never the
  // app — the WebSocket layer below must always come up (AP-mode setup
  // depends on it).
  if (typeof Chart !== 'undefined') {
    initChart();

    const wrap = document.querySelector('.chart-wrap');
    chart.canvas.style.width  = wrap.clientWidth  + 'px';
    chart.canvas.style.height = wrap.clientHeight + 'px';
    chart.resize();

    let resizeTimer;
    window.addEventListener('resize', () => {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(() => { if (chart) chart.resize(); }, 400);
    });
  } else {
    const wrap = document.querySelector('.chart-wrap');
    if (wrap) wrap.innerHTML =
      '<div style="display:flex;align-items:center;justify-content:center;height:100%;color:var(--text-muted);font-size:12px;">Chart unavailable — chart.js failed to load</div>';
  }

  connectWS();
  setInterval(tickConnState, 1000);
});

// ── Calibration wizard ───────────────────────────────────────────────────────
//
// Four steps: three baths (both probes at once) then review & save. Every 1 s
// tick carries each probe's median-filtered resistance, so the wizard watches
// those live, shows when a probe has settled, and captures the mean of the
// last few seconds rather than a single sample. The firmware recomputes and
// validates the coefficients itself; the maths here only drives the preview.

const CAL_PROBES     = ['meat', 'grill'];
const CAL_BUF_LEN    = 8;      // seconds of readings behind the stability check
const CAL_STABLE_C   = 0.3;    // °C spread across the buffer that counts as settled
const CAL_MIN_SAMPLES = 3;     // readings needed before a capture is allowed
const CAL_DRAFT_KEY  = 'bbq_cal_draft';

const CAL_STEPS = [
  { title: 'Ice bath', defaultC: 0,
    hint: 'Fill a cup with crushed ice, top up with just enough cold water to cover it, and stir. Put BOTH probe tips in the middle of the ice, not touching the cup, and wait for the readings to settle.' },
  { title: 'Room temperature', defaultC: 25,
    hint: 'Leave both probes in still air or a glass of room-temperature water next to a thermometer you trust. Enter what that thermometer reads — any temperature well between the other two baths is fine.' },
  { title: 'Boiling water', defaultC: 100,
    hint: 'Bring a pan to a rolling boil and hold both tips in the water, clear of the bottom. At sea level that is 100 °C; knock off roughly 1 °C for every 300 m of altitude.' },
  { title: 'Review & save' },
];

const cal = {
  open:    false,
  step:    0,
  refC:    CAL_STEPS.slice(0, 3).map(s => s.defaultC),
  points:  { meat: [null, null, null], grill: [null, null, null] }, // {tempC, resistance, drifting}
  live:    { meat: [], grill: [] },                                   // ring buffers of {c, r}
  info:    { meat: null, grill: null },                               // from config: {calibrated, source, points}
  results: null,                                                      // last cal_saved reply, keyed by probe
  saving:  false,
};

function calIsOpen() { return cal.open; }

// ── Maths (mirrors thermistor.h) ──

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

function shToC(coeffs, resistance) {
  if (!(resistance > 0)) return NaN;
  const L = Math.log(resistance);
  const invT = coeffs.A + coeffs.B * L + coeffs.C * L * L * L;
  return invT > 0 ? 1 / invT - 273.15 : NaN;
}

// Same checks, same order and same reason strings as calibrateFromPoints()
// in the firmware, so the review can flag a bad point before the round trip.
function calValidate(pts) {
  for (let i = 0; i < 3; i++) {
    const p = pts[i];
    if (!p || !isFinite(p.tempC) || !isFinite(p.resistance) || p.resistance <= 0 ||
        p.tempC < -50 || p.tempC > 350) return { reason: 'bad_input', point: i };
  }
  const idx = [0, 1, 2].sort((a, b) => pts[a].tempC - pts[b].tempC);
  for (let k = 0; k < 2; k++) {
    const lo = pts[idx[k]], hi = pts[idx[k + 1]];
    if (hi.tempC - lo.tempC < 0.5)     return { reason: 'temps_too_close', point: idx[k + 1] };
    if (hi.resistance >= lo.resistance) return { reason: 'not_monotonic',   point: idx[k + 1] };
  }
  const coeffs = calcSHCoeffs(pts);
  if (![coeffs.A, coeffs.B, coeffs.C].every(isFinite)) return { reason: 'degenerate' };
  for (let i = 0; i < 3; i++) {
    const t = shToC(coeffs, pts[i].resistance);
    if (!isFinite(t) || Math.abs(t - pts[i].tempC) > 3) return { reason: 'roundtrip', point: i };
  }
  return { ok: true, coeffs };
}

const CAL_REASONS = {
  bad_input:       'One of the points is not a valid number.',
  temps_too_close: 'Two reference temperatures are within 0.5 °C of each other.',
  not_monotonic:   'Resistance did not fall as temperature rose. Was the probe actually in that bath?',
  degenerate:      'These points cannot be solved. Re-capture and try again.',
  roundtrip:       'One point does not fit the curve through the other two.',
  sd_write_failed: 'Applied and in use now, but saving to the SD card failed. It will be lost on reboot.',
};

// ── Live readings ──

function calFeed(meatC, meatR, grillC, grillR) {
  const push = (probe, c, r) => {
    const buf = cal.live[probe];
    buf.push({ c: c != null ? c : null, r: r != null ? parseFloat(r) : null });
    if (buf.length > CAL_BUF_LEN) buf.shift();
  };
  push('meat',  meatC,  meatR);
  push('grill', grillC, grillR);
  if (cal.open && cal.step < 3) calRenderLive();
  else if (cal.open)            calRenderReviewLive();
}

function calStats(probe) {
  const samples = cal.live[probe].filter(s => s.r != null && s.c != null);
  const last    = cal.live[probe][cal.live[probe].length - 1];
  if (!isLive() || !last)       return { state: 'waiting', n: 0 };
  if (last.r == null)           return { state: 'off',     n: 0 };
  if (samples.length < CAL_MIN_SAMPLES) return { state: 'waiting', n: samples.length, c: last.c, r: last.r };

  const cs = samples.map(s => s.c);
  const spread = Math.max(...cs) - Math.min(...cs);
  const drift  = cs[cs.length - 1] - cs[0];
  const meanR  = samples.reduce((a, s) => a + s.r, 0) / samples.length;
  const full   = samples.length >= CAL_BUF_LEN;
  return {
    state: full && spread <= CAL_STABLE_C ? 'stable' : 'settling',
    n: samples.length, c: last.c, r: last.r, spread, drift, meanR,
  };
}

// ── Open / close ──

function toggleCal() {
  const body    = document.getElementById('cal-body');
  const chevron = document.getElementById('cal-chevron');
  const btn     = document.getElementById('cal-toggle-btn');
  cal.open      = body.hidden;
  body.hidden   = !cal.open;
  chevron.textContent = cal.open ? '▴' : '▾';
  btn.setAttribute('aria-expanded', String(cal.open));
  if (cal.open) calRender();
}

// ── Rendering ──

function calRender() {
  const isReview = cal.step === 3;
  const stepDef  = CAL_STEPS[cal.step];

  document.getElementById('cal-step-count').textContent = `Step ${cal.step + 1} of ${CAL_STEPS.length}`;
  document.getElementById('cal-step-title').textContent = stepDef.title;
  document.getElementById('cal-step-hint').textContent  = stepDef.hint || '';

  const dots = document.getElementById('cal-progress');
  dots.innerHTML = '';
  CAL_STEPS.forEach((_, i) => {
    const d = document.createElement('span');
    d.className = 'cal-dot' + (i === cal.step ? ' current' : '') + (i < 3 && calStepCaptured(i) ? ' done' : '');
    dots.appendChild(d);
  });

  document.getElementById('cal-bath-panel').hidden   = isReview;
  document.getElementById('cal-review-panel').hidden = !isReview;
  document.getElementById('cal-back-btn').disabled   = cal.step === 0;
  document.getElementById('cal-next-btn').hidden     = isReview;

  if (isReview) {
    calRenderReview();
  } else {
    const ref = document.getElementById('cal-ref');
    ref.value = unit === 'F' ? cToF(cal.refC[cal.step]).toFixed(1).replace(/\.0$/, '') : String(cal.refC[cal.step]);
    document.getElementById('cal-ref-unit').textContent = unitLabel();
    calRenderLive();
  }
}

function calStepCaptured(step) {
  return CAL_PROBES.some(p => cal.points[p][step] !== null);
}

function calRefChanged() {
  const raw = parseFloat(document.getElementById('cal-ref').value);
  if (isNaN(raw)) return;
  cal.refC[cal.step] = unit === 'F' ? fToC(raw) : raw;
  calPersist();
  calRenderLive();
}

function calRenderLive() {
  if (cal.step > 2) return;
  const step = cal.step;
  let anyCapturable = false;
  let anyDrifting   = false;

  CAL_PROBES.forEach(probe => {
    const st   = calStats(probe);
    const card = document.getElementById(`cal-live-${probe}`);
    const temp = document.getElementById(`cal-live-${probe}-temp`);
    const res  = document.getElementById(`cal-live-${probe}-res`);
    const pill = document.getElementById(`cal-live-${probe}-pill`);
    const cap  = document.getElementById(`cal-live-${probe}-cap`);

    card.classList.toggle('off', st.state === 'off');
    pill.dataset.state = st.state;

    if (st.state === 'off') {
      temp.textContent = '--';
      res.textContent  = 'No probe';
      pill.textContent = 'unplugged · skipped';
    } else if (st.state === 'waiting') {
      temp.textContent = st.c != null ? fmtWithUnit(st.c) : '--';
      res.textContent  = st.r != null ? Math.round(st.r).toLocaleString() + ' Ω' : '-- Ω';
      pill.textContent = isLive() ? 'waiting for readings…' : 'not connected';
    } else {
      temp.textContent = fmtWithUnit(st.c);
      res.textContent  = Math.round(st.r).toLocaleString() + ' Ω';
      const dir = st.drift > 0.05 ? '▲' : st.drift < -0.05 ? '▼' : '';
      pill.textContent = st.state === 'stable'
        ? `stable · ±${fmt(st.spread / 2, 1)}°`
        : `settling ${dir} ${fmt(st.spread, 1)}° over ${st.n}s`;
      anyCapturable = true;
      if (st.state === 'settling') anyDrifting = true;
    }

    const pt = cal.points[probe][step];
    if (pt) {
      cap.textContent = `Captured ${Math.round(pt.resistance).toLocaleString()} Ω @ ${fmtWithUnit(pt.tempC)}` +
                        (pt.drifting ? ' · while drifting' : '');
      cap.dataset.state = pt.drifting ? 'drifting' : 'ok';
    } else {
      cap.textContent = '';
      cap.dataset.state = '';
    }
  });

  const btn = document.getElementById('cal-capture-btn');
  const connected = CAL_PROBES.filter(p => calStats(p).state !== 'off' && calStats(p).state !== 'waiting');
  btn.disabled = !anyCapturable;
  btn.classList.toggle('drifting', anyDrifting);
  btn.textContent = connected.length === 2 ? (calStepCaptured(step) ? 'Re-capture both' : 'Capture both')
                  : connected.length === 1 ? (calStepCaptured(step) ? `Re-capture ${connected[0]}` : `Capture ${connected[0]}`)
                  : 'Capture';

  // Sanity hint: is the reference plausible given what the current curve says?
  const msg = document.getElementById('cal-msg');
  const off = CAL_PROBES.map(p => calStats(p)).filter(s => s.c != null && s.state !== 'off')
                        .map(s => Math.abs(s.c - cal.refC[step]));
  const dupe = cal.refC.some((t, i) => i !== step && calStepCaptured(i) && Math.abs(t - cal.refC[step]) < 0.5);
  if (dupe) {
    msg.textContent = 'This reference temperature is already used by another bath. Each bath needs a different temperature.';
    msg.dataset.state = 'error';
    btn.disabled = true;
  } else if (off.length && Math.min(...off) > 5) {
    msg.textContent = `Probes currently read ${fmt(Math.min(...off), 0)}° away from the reference. Give them longer to settle, or check they are in the bath.`;
    msg.dataset.state = 'warn';
  } else {
    msg.textContent = '';
    msg.dataset.state = '';
  }

  document.getElementById('cal-next-btn').disabled = !calStepCaptured(step);
}

function calCapture() {
  const step = cal.step;
  let captured = 0;
  CAL_PROBES.forEach(probe => {
    const st = calStats(probe);
    if (st.state !== 'stable' && st.state !== 'settling') return;
    cal.points[probe][step] = {
      tempC:      cal.refC[step],
      resistance: Math.round(st.meanR),
      drifting:   st.state === 'settling',
    };
    captured++;
  });
  if (!captured) return;
  cal.results = null;
  calPersist();
  calRender();
}

function calNext() {
  if (cal.step >= 3) return;
  cal.step++;
  calPersist();
  calRender();
}

function calBack() {
  if (cal.step === 0) return;
  cal.step--;
  calPersist();
  calRender();
}

function calStartOver() {
  if (CAL_PROBES.some(p => cal.points[p].some(Boolean)) &&
      !confirm('Discard the captured points and start again?')) return;
  cal.step    = 0;
  cal.refC    = CAL_STEPS.slice(0, 3).map(s => s.defaultC);
  cal.points  = { meat: [null, null, null], grill: [null, null, null] };
  cal.results = null;
  calClearDraft();
  calRender();
}

// ── Review ──

function calProbeReview(probe) {
  const pts = cal.points[probe];
  const n   = pts.filter(Boolean).length;
  if (n === 0)  return { probe, n, complete: false };
  if (n < 3)    return { probe, n, complete: false };
  const v = calValidate(pts);
  return { probe, n, complete: true, ...v };
}

function calRenderReview() {
  const body = document.getElementById('cal-review-body');
  body.innerHTML = '';
  let saveable = 0;

  CAL_PROBES.forEach(probe => {
    const rv   = calProbeReview(probe);
    const sec  = document.createElement('div');
    sec.className = 'cal-review-probe';
    const prevInfo = cal.info[probe];
    const prevPts  = prevInfo && Array.isArray(prevInfo.points) ? prevInfo.points : [];

    let html = `<div class="cal-review-head"><span class="cal-live-name">${probe.toUpperCase()}</span>` +
               `<span class="cal-review-prev">${calBadgeText(probe)}</span></div>`;

    if (rv.n === 0) {
      html += `<p class="cal-review-note">Not captured in any bath. Plug it in and go back to include it, or save the other probe alone.</p>`;
    } else if (!rv.complete) {
      const missing = CAL_STEPS.slice(0, 3).map((s, i) => cal.points[probe][i] ? null : s.title).filter(Boolean);
      html += `<p class="cal-review-note warn">Only ${rv.n} of 3 baths captured. Missing: ${missing.join(', ')}. Go back to capture it, or this probe will be left as is.</p>`;
    } else {
      const bad = rv.ok ? -1 : (rv.point ?? -1);
      html += `<div class="cal-table-wrap"><table class="cal-table"><thead><tr>` +
              `<th>Bath</th><th>Reference</th><th>Measured</th><th>Fit</th><th>Previous</th></tr></thead><tbody>`;
      cal.points[probe].forEach((pt, i) => {
        const fit  = rv.ok ? shToC(rv.coeffs, pt.resistance) : NaN;
        const prev = prevPts.find(p => Math.abs(p.temp_c - pt.tempC) < 2.5);
        html += `<tr class="${i === bad ? 'bad' : ''}">` +
                `<td>${CAL_STEPS[i].title}${pt.drifting ? ' <span class="cal-flag" title="captured while still settling">~</span>' : ''}</td>` +
                `<td>${fmtWithUnit(pt.tempC)}</td>` +
                `<td>${Math.round(pt.resistance).toLocaleString()} Ω</td>` +
                `<td>${isFinite(fit) ? fmtWithUnit(fit) : '--'}</td>` +
                `<td>${prev ? Math.round(prev.resistance).toLocaleString() + ' Ω' : '--'}</td></tr>`;
      });
      html += `</tbody></table></div>`;
      if (rv.ok) {
        const c = rv.coeffs;
        html += `<div class="cal-coeffs"><span class="cal-coeff-item">A <span>${c.A.toExponential(4)}</span></span>` +
                `<span class="cal-coeff-item">B <span>${c.B.toExponential(4)}</span></span>` +
                `<span class="cal-coeff-item">C <span>${c.C.toExponential(4)}</span></span></div>`;
        html += `<p class="cal-review-note" id="cal-preview-${probe}"></p>`;
        saveable++;
      } else {
        html += `<p class="cal-review-note error">${CAL_REASONS[rv.reason] || rv.reason}</p>`;
      }
    }

    const res = cal.results && cal.results[probe];
    if (res) {
      const text = res.saved   ? 'Saved to SD ✓'
                 : res.applied ? CAL_REASONS.sd_write_failed
                 : `Rejected by device: ${CAL_REASONS[res.reason] || res.reason}`;
      html += `<p class="cal-review-note ${res.saved ? 'ok' : 'error'}">${text}</p>`;
    }

    sec.innerHTML = html;
    body.appendChild(sec);
  });

  const btn = document.getElementById('cal-save-btn');
  const done = cal.results && Object.values(cal.results).some(r => r.saved);
  btn.disabled = !saveable || cal.saving || !isLive() || done;
  btn.textContent = done ? 'Saved ✓' : cal.saving ? 'Saving…' : saveable === 2 ? 'Save both' : 'Save';
  const msg = document.getElementById('cal-save-msg');
  msg.textContent = !isLive() ? 'Not connected to the device. Reconnect to save.' : '';
  calRenderReviewLive();
}

function calRenderReviewLive() {
  CAL_PROBES.forEach(probe => {
    const el = document.getElementById(`cal-preview-${probe}`);
    if (!el) return;
    const rv = calProbeReview(probe);
    const st = calStats(probe);
    if (!rv.ok || st.r == null) { el.textContent = ''; return; }
    const now = shToC(rv.coeffs, st.r);
    el.textContent = `Right now this probe would read ${fmtWithUnit(now)} (currently ${fmtWithUnit(st.c)}).`;
  });
}

// ── Save ──

function calSave() {
  if (!isLive() || !wsSocket || cal.saving) return;
  const probes = CAL_PROBES.map((p, i) => ({ p, i, rv: calProbeReview(p) }))
                           .filter(x => x.rv.ok)
                           .map(x => ({
                             probe:  x.i,
                             points: cal.points[x.p].map(pt => ({ temp_c: pt.tempC, resistance: pt.resistance })),
                           }));
  if (!probes.length) return;
  cal.saving  = true;
  cal.results = null;
  calRenderReview();
  wsSocket.send(JSON.stringify({ type: 'cal_save', timestamp: new Date().toISOString(), probes }));
}

function handleCalSaved(data) {
  cal.saving  = false;
  cal.results = {};
  (data.results || []).forEach(r => {
    const probe = CAL_PROBES[r.probe];
    if (!probe) return;
    cal.results[probe] = r;
    if (r.applied) {
      cal.info[probe] = {
        source:     r.saved ? 'sd' : 'ram',
        calibrated: data.calibrated || new Date().toISOString(),
        points:     cal.points[probe].map(pt => ({ temp_c: pt.tempC, resistance: pt.resistance })),
      };
    }
  });
  if (Object.values(cal.results).some(r => r.saved)) calClearDraft();
  calRenderBadges();
  if (cal.open) calRender();
}

// ── Status badges + persisted draft ──

function calSetInfo(calibration) {
  CAL_PROBES.forEach((probe, i) => {
    const c = calibration[String(i)];
    cal.info[probe] = c ? { source: c.source, calibrated: c.calibrated, points: c.points || [] } : null;
  });
  calRenderBadges();
}

function calBadgeText(probe) {
  const info = cal.info[probe];
  if (!info || info.source === 'default' || !info.calibrated) return 'defaults';
  const d = new Date(info.calibrated);
  const when = isNaN(d) ? 'calibrated' : d.toLocaleDateString([], { day: 'numeric', month: 'short', year: 'numeric' });
  return `✓ ${when}`;
}

function calRenderBadges() {
  const el = document.getElementById('cal-badges');
  if (!el) return;
  const captured = [0, 1, 2].filter(calStepCaptured).length;
  const parts = CAL_PROBES.map(p => `${p[0].toUpperCase() + p.slice(1)} ${calBadgeText(p)}`);
  el.textContent = captured && !(cal.results && Object.values(cal.results).some(r => r.saved))
    ? `In progress · ${captured} of 3 baths captured`
    : parts.join(' · ');
}

function calPersist() {
  try {
    localStorage.setItem(CAL_DRAFT_KEY, JSON.stringify({ step: cal.step, refC: cal.refC, points: cal.points }));
  } catch {}
  calRenderBadges();
}

function calClearDraft() {
  try { localStorage.removeItem(CAL_DRAFT_KEY); } catch {}
}

function calRestore() {
  let d = null;
  try { d = JSON.parse(localStorage.getItem(CAL_DRAFT_KEY) || 'null'); } catch {}
  if (!d || !d.points || !Array.isArray(d.refC)) return;
  const valid = arr => Array.isArray(arr) && arr.length === 3;
  if (!valid(d.refC) || !valid(d.points.meat) || !valid(d.points.grill)) return;
  cal.refC   = d.refC.map((t, i) => isFinite(t) ? t : CAL_STEPS[i].defaultC);
  cal.points = { meat: d.points.meat.map(p => p || null), grill: d.points.grill.map(p => p || null) };
  cal.step   = Math.min(3, Math.max(0, d.step | 0));
  calRenderBadges();
}

document.addEventListener('DOMContentLoaded', calRestore);

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
    const sheet = document.getElementById('settings-sheet');
    if (sheet && sheet.classList.contains('open')) wifiScan();
  }
}

function updateWifiStatusLine() {
  const textEl = document.getElementById('wifi-status-text');
  const dot    = document.getElementById('wifi-status-dot');
  if (!textEl) return;
  if (_apMode) {
    textEl.textContent = 'Not connected to WiFi. Enter your network details below.';
    if (dot) dot.dataset.state = 'ap';
  } else {
    textEl.textContent = 'Connected. Enter new details to switch networks.';
    if (dot) dot.dataset.state = 'connected';
  }
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
      const row = document.createElement('button');
      row.className = 'wifi-network-row';
      row.addEventListener('click', () => wifiSelectNetwork(n.ssid));

      const nameSpan = document.createElement('span');
      nameSpan.className = 'wifi-network-name';
      nameSpan.textContent = n.ssid;

      const bars = document.createElement('span');
      bars.className = 'wifi-signal-bars';
      bars.setAttribute('aria-label', n.rssi + ' dBm');
      const level = n.rssi >= -60 ? 4 : n.rssi >= -70 ? 3 : n.rssi >= -80 ? 2 : 1;
      ['▂', '▄', '▆', '█'].forEach((ch, i) => {
        const b = document.createElement('span');
        b.textContent = ch;
        b.className = i < level ? 'bar-filled' : 'bar-empty';
        bars.appendChild(b);
      });

      row.appendChild(nameSpan);
      row.appendChild(bars);
      container.appendChild(row);
    });
  }
  container.classList.remove('hidden');
}

function toggleWifiPass() {
  const input  = document.getElementById('wifi-pass');
  const toggle = document.getElementById('wifi-pass-toggle');
  if (!input) return;
  const showing = input.type === 'text';
  input.type = showing ? 'password' : 'text';
  if (toggle) {
    toggle.setAttribute('aria-label', showing ? 'Show password' : 'Hide password');
    toggle.style.opacity = showing ? '' : '1';
  }
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

function handleWifiTesting(data) {
  const el = document.getElementById('wifi-save-status');
  if (el) el.textContent = `Trying to join “${data.ssid || 'network'}”…`;
}

function handleWifiSaved(data) {
  const el  = document.getElementById('wifi-save-status');
  const btn = document.getElementById('wifi-save-btn');
  if (data.saved) {
    if (el) {
      el.textContent = data.ip
        ? `Connected! Device is restarting — rejoin your WiFi and open http://${data.ip} (or http://bbq.local).`
        : 'Saved — device is restarting…';
    }
    if (btn) { btn.textContent = 'Restarting…'; btn.disabled = true; }
  } else {
    const reasons = {
      join_failed:     'Couldn’t join that network — check the password and try again.',
      busy:            'Device is busy checking a network — try again in a few seconds.',
      no_ssid:         'Please enter a network name.',
      fs_write_failed: 'Connected, but saving failed — check the device storage.',
    };
    if (el)  el.textContent = reasons[data.reason] || 'Save failed — please try again.';
    if (btn) { btn.textContent = 'Save & Connect'; btn.disabled = false; }
  }
}

function handleFavResult(data) {
  if (!data.saved && data.reason === 'full') {
    showBanner('Favourites list is full — delete one in Settings to save another.');
  }
}
