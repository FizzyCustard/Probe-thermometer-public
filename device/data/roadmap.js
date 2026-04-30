// ═══════════════════════════════════════════════════════════════════════════
//  Probe Therm — Roadmap Prototype
//  Extends the existing demo dashboard with:
//    • Cook History (Start/Stop, sessions, replay)
//    • Favourites (save/load named configs)
//    • Cook Notes (timeline, mid-cook annotations)
//    • Preset Temperatures (chip selector)
// ═══════════════════════════════════════════════════════════════════════════

// ── Demo data: pre-populated history + favourites (so the prototype feels alive)
// stored in window so the Tweaks panel can reset / mutate them

window.__demoCookHistory = [
  {
    id: 'c-1',
    name: 'Sunday Brisket',
    preset: 'Brisket / pulled pork',
    startedAt: new Date('2026-04-26T08:14:00').getTime(),
    duration: 9 * 3600 + 42 * 60,
    targets: { meatTarget: 95, grillLow: 105, grillHigh: 130 },
    peakMeat: 96.4,
    peakGrill: 138.2,
    targetHitMin: 9 * 60 + 18,
    notes: [
      { ts: 0,        text: 'Started cook with apple wood, 2.4kg packer brisket. Trimmed fat cap to ~6mm.' },
      { ts: 76 * 60,  text: 'Stall hit at 68°C. Spritzing with apple juice every 20 min.' },
      { ts: 192 * 60, text: 'Wrapped in butcher paper at 75°C. Smelling incredible.' },
      { ts: 9 * 60 * 60 + 18 * 60, text: 'Probe slides like butter at 95°C. Resting in cooler for 2hrs.' },
    ],
    favourite: true,
  },
  {
    id: 'c-2',
    name: 'Tuesday Wings',
    preset: 'Chicken — safe minimum',
    startedAt: new Date('2026-04-23T18:22:00').getTime(),
    duration: 1 * 3600 + 8 * 60,
    targets: { meatTarget: 74, grillLow: 175, grillHigh: 200 },
    peakMeat: 78.1,
    peakGrill: 211.3,
    targetHitMin: 56,
    notes: [
      { ts: 0,        text: 'Buffalo wings, salt brined for 2hrs. Hot-and-fast.' },
      { ts: 24 * 60,  text: 'Grill spiked above 200 for a few mins — opened the lid to coast it down.' },
      { ts: 56 * 60,  text: 'Hit target. Tossed in sauce, back on for 5 min to crisp.' },
    ],
    favourite: false,
  },
  {
    id: 'c-3',
    name: 'Reverse Sear Ribeye',
    preset: 'Beef — medium rare',
    startedAt: new Date('2026-04-20T19:10:00').getTime(),
    duration: 52 * 60,
    targets: { meatTarget: 54, grillLow: 110, grillHigh: 130 },
    peakMeat: 57.8,
    peakGrill: 132.5,
    targetHitMin: 38,
    notes: [
      { ts: 0,       text: '2-inch ribeye, dry brined overnight.' },
      { ts: 38 * 60, text: 'Hit 54°C. Pulled, ripped grill to 280°C for the sear.' },
      { ts: 48 * 60, text: 'Seared 90s a side. Rested 8 min. Perfect.' },
    ],
    favourite: true,
  },
];

window.__demoFavourites = [
  {
    id: 'f-1',
    name: 'Sunday Brisket',
    targets: { meatTarget: 95, grillLow: 105, grillHigh: 130 },
    preset: 'Brisket / pulled pork',
  },
  {
    id: 'f-2',
    name: 'Reverse Sear Ribeye',
    targets: { meatTarget: 54, grillLow: 110, grillHigh: 130 },
    preset: 'Beef — medium rare',
  },
  {
    id: 'f-3',
    name: 'Low & Slow Pork Shoulder',
    targets: { meatTarget: 92, grillLow: 105, grillHigh: 125 },
    preset: 'Brisket / pulled pork',
  },
];

// ── Preset table from ROADMAP.md ─────────────────────────────────────────
window.__presets = [
  { name: 'Beef — rare',           c: 52 },
  { name: 'Beef — medium rare',    c: 57 },
  { name: 'Beef — medium',         c: 63 },
  { name: 'Beef — well done',      c: 71 },
  { name: 'Lamb — medium rare',    c: 57 },
  { name: 'Lamb — medium',         c: 63 },
  { name: 'Pork — safe minimum',   c: 71 },
  { name: 'Chicken — safe minimum',c: 74 },
  { name: 'Brisket / pulled pork', c: 95 },
];

// ── WS helper — forwards messages to a live device when connected ─────────
function wsSend(msg) {
  const connected = typeof isLive === 'function' ? isLive() : wsConnected;
  if (connected && wsSocket && wsSocket.readyState === WebSocket.OPEN) {
    wsSocket.send(JSON.stringify(msg));
  }
}

// ── App-level state for new features ─────────────────────────────────────
window.__cookState = {
  active: false,
  startedAt: null,
  name: '',
  preset: null,
  notes: [],          // [{ ts (seconds since cook start), text, system?, temp? }]
  history: window.__demoCookHistory.slice(),
  favourites: window.__demoFavourites.slice(),
  selectedHistoryId: null,
  view: 'live',       // 'live' | 'history'
};

// ─────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────

function formatDuration(seconds) {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  if (h > 0) return `${h}h ${String(m).padStart(2, '0')}m`;
  if (m > 0) return `${m}m ${String(s).padStart(2, '0')}s`;
  return `${s}s`;
}

function formatRelDate(ts) {
  const d = new Date(ts);
  const today = new Date();
  const midnightToday = new Date(today.getFullYear(), today.getMonth(), today.getDate());
  const midnightD = new Date(d.getFullYear(), d.getMonth(), d.getDate());
  const diffDays = Math.round((midnightToday - midnightD) / (24 * 3600 * 1000));
  if (diffDays === 0) return 'Today, ' + d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  if (diffDays === 1) return 'Yesterday';
  if (diffDays < 7)   return d.toLocaleDateString([], { weekday: 'long' });
  return d.toLocaleDateString([], { day: 'numeric', month: 'short' });
}

function initials(name) {
  return name.split(/\s+/).slice(0, 2).map(s => s[0]).join('').toUpperCase();
}

// ─────────────────────────────────────────────────────────────────────────
//  PRESETS — render chip row
// ─────────────────────────────────────────────────────────────────────────

function renderPresetChips() {
  const row = document.getElementById('preset-chips');
  if (!row) return;
  row.innerHTML = '';
  const inputEl = document.getElementById('s-meat');
  const inputTarget = inputEl ? (unit === 'F' ? fToC(parseFloat(inputEl.value)) : parseFloat(inputEl.value)) : settings.meatTarget;
  
  window.__presets.forEach(p => {
    const chip = document.createElement('button');
    chip.className = 'preset-chip';
    
    // Highlight if preset matches exactly, or if temperature matches the input value
    const isTempMatch = !isNaN(inputTarget) && Math.abs(p.c - inputTarget) < 0.5;
    if (window.__cookState.preset === p.name || (!window.__cookState.preset && isTempMatch)) {
      chip.classList.add('active');
    }
    
    const tempDisplay = unit === 'F' ? Math.round(cToF(p.c)) : p.c;
    chip.innerHTML = `
      <span>${p.name}</span>
      <span class="preset-temp">${tempDisplay}°${unit}</span>
    `;
    chip.onclick = () => {
      // Fill the meat target input only — the user still presses Update
      document.getElementById('s-meat').value = tempDisplay;
      window.__cookState.preset = p.name;
      renderPresetChips();
    };
    row.appendChild(chip);
  });
}

// ─────────────────────────────────────────────────────────────────────────
//  COOK BAR — Start / Stop, displays active cook info
// ─────────────────────────────────────────────────────────────────────────

function renderCookBar() {
  const bar = document.getElementById('cook-bar');
  if (!bar) return;
  const s = window.__cookState;

  if (!s.active) {
    bar.classList.remove('active');
    bar.innerHTML = `
      <div class="cook-bar-info">
        <span class="cook-bar-label">No active cook</span>
        <span class="cook-bar-time">Set targets, then start a session to log it.</span>
      </div>
      <div class="cook-bar-actions">
        <button class="cook-btn" onclick="openStartCookDialog()">Start cook</button>
      </div>
    `;
  } else {
    bar.classList.add('active');
    const elapsed = Math.floor((Date.now() - s.startedAt) / 1000);
    bar.innerHTML = `
      <div class="cook-bar-info">
        <span class="cook-bar-label">● Live cook</span>
        <span class="cook-bar-name">${s.name || 'Untitled cook'}</span>
        <span class="cook-bar-time" id="cook-elapsed">${formatDuration(elapsed)} elapsed</span>
      </div>
      <div class="cook-bar-actions">
        <button class="cook-btn stop" onclick="openStopCookDialog()">Stop</button>
      </div>
    `;
  }
}

function tickCookBar() {
  const s = window.__cookState;
  if (!s.active) return;
  const el = document.getElementById('cook-elapsed');
  if (!el) return;
  const elapsed = Math.floor((Date.now() - s.startedAt) / 1000);
  el.textContent = `${formatDuration(elapsed)} elapsed`;
}

// ─────────────────────────────────────────────────────────────────────────
//  START / STOP COOK DIALOGS
// ─────────────────────────────────────────────────────────────────────────

function openStartCookDialog() {
  document.getElementById('start-cook-overlay').classList.add('open');
  document.getElementById('start-meat-target').value = Math.round(unit === 'F' ? cToF(settings.meatTarget) : settings.meatTarget);
  document.getElementById('start-grill-low').value   = Math.round(unit === 'F' ? cToF(settings.grillLow)   : settings.grillLow);
  document.getElementById('start-grill-high').value  = Math.round(unit === 'F' ? cToF(settings.grillHigh)  : settings.grillHigh);
  ['start-meat-unit', 'start-grill-low-unit', 'start-grill-high-unit'].forEach(id => {
    document.getElementById(id).textContent = unitLabel();
  });
  setTimeout(() => document.getElementById('start-cook-name').focus(), 200);
}
window.openStartCookDialog = openStartCookDialog;

function closeStartCookDialog() {
  document.getElementById('start-cook-overlay').classList.remove('open');
}
window.closeStartCookDialog = closeStartCookDialog;

function confirmStartCook() {
  const name = document.getElementById('start-cook-name').value.trim() || 'Cook';
  const note = document.getElementById('start-cook-note').value.trim();

  // Apply targets from dialog inputs
  const rawMeat = parseFloat(document.getElementById('start-meat-target').value);
  const rawLow  = parseFloat(document.getElementById('start-grill-low').value);
  const rawHigh = parseFloat(document.getElementById('start-grill-high').value);
  if (![rawMeat, rawLow, rawHigh].some(isNaN)) {
    settings.meatTarget = unit === 'F' ? fToC(rawMeat) : rawMeat;
    settings.grillLow   = unit === 'F' ? fToC(rawLow)  : rawLow;
    settings.grillHigh  = unit === 'F' ? fToC(rawHigh) : rawHigh;
    
    if (window.__cookState.preset) {
      const p = window.__presets.find(x => x.name === window.__cookState.preset);
      if (!p || Math.abs(p.c - settings.meatTarget) >= 0.5) {
        window.__cookState.preset = null;
      }
    }
    
    syncSettingsInputs();
  }

  // Reset chart — fresh slate for this cook
  meatHistory.length  = 0;
  grillHistory.length = 0;
  rebuildChart();
  alertFired = false;

  const s = window.__cookState;
  s.active = true;
  s.startedAt = Date.now();
  s.name = name;
  s.notes = [
    { ts: 0, text: `Started cook · target ${fmtWithUnit(settings.meatTarget, 0)} · grill ${fmt(settings.grillLow,0)}–${fmtWithUnit(settings.grillHigh,0)}${s.preset ? ` · ${s.preset}` : ''}`, system: true },
  ];
  if (note) s.notes.push({ ts: 0, text: note });

  wsSend({
    type:        'cook_start',
    name:        s.name,
    preset:      s.preset || '',
    meat_target: settings.meatTarget,
    grill_low:   settings.grillLow,
    grill_high:  settings.grillHigh,
    ts:          new Date().toISOString().replace(/\.\d{3}Z$/, ''),
  });

  closeStartCookDialog();
  renderCookBar();
  renderNotesCard();
  toggleNotesVisibility();
}
window.confirmStartCook = confirmStartCook;

function openStopCookDialog() {
  document.getElementById('stop-cook-overlay').classList.add('open');
}
window.openStopCookDialog = openStopCookDialog;

function closeStopCookDialog() {
  document.getElementById('stop-cook-overlay').classList.remove('open');
}
window.closeStopCookDialog = closeStopCookDialog;

function confirmStopCook(saveAsFavourite) {
  const s = window.__cookState;
  if (!s.active) return;
  const duration = Math.floor((Date.now() - s.startedAt) / 1000);
  const lastMeat  = meatHistory.length  ? meatHistory[meatHistory.length - 1].c  : null;
  const lastGrill = grillHistory.length ? grillHistory[grillHistory.length - 1].c : null;
  const peakMeat  = meatHistory.length  ? Math.max(...meatHistory.map(d => d.c))  : lastMeat;
  const peakGrill = grillHistory.length ? Math.max(...grillHistory.map(d => d.c)) : lastGrill;

  const session = {
    id: 'c-' + Date.now(),
    name: s.name,
    preset: s.preset,
    startedAt: s.startedAt,
    duration,
    targets: { ...settings },
    peakMeat: Math.round(peakMeat * 10) / 10,
    peakGrill: Math.round(peakGrill * 10) / 10,
    targetHitMin: lastMeat >= settings.meatTarget ? Math.floor(duration / 60) : null,
    notes: s.notes.slice(),
    favourite: !!saveAsFavourite,
  };
  s.history.unshift(session);

  const stopTs = new Date().toISOString().replace(/\.\d{3}Z$/, '');

  if (saveAsFavourite) {
    const favId = 'f-' + Date.now();
    s.favourites.unshift({
      id: favId,
      name: s.name,
      targets: { ...settings },
      preset: s.preset,
    });
    wsSend({
      type:        'fav_save',
      id:          favId,
      name:        s.name,
      meat_target: settings.meatTarget,
      grill_low:   settings.grillLow,
      grill_high:  settings.grillHigh,
      preset:      s.preset || '',
    });
  }

  wsSend({ type: 'cook_stop', ts: stopTs });

  // Reset live cook state
  s.active = false;
  s.startedAt = null;
  s.name = '';
  s.notes = [];
  closeStopCookDialog();
  renderCookBar();
  renderNotesCard();
  toggleNotesVisibility();
  renderHistoryList();
  renderFavourites();
}
window.confirmStopCook = confirmStopCook;

// ─────────────────────────────────────────────────────────────────────────
//  COOK NOTES — render timeline + composer
// ─────────────────────────────────────────────────────────────────────────

function renderNotesCard() {
  const list = document.getElementById('notes-list');
  const empty = document.getElementById('notes-empty');
  if (!list) return;
  const s = window.__cookState;
  list.innerHTML = '';

  if (!s.notes.length) {
    empty.style.display = 'block';
    return;
  }
  empty.style.display = 'none';

  // Newest at top
  s.notes.slice().reverse().forEach(n => {
    const row = document.createElement('div');
    row.className = 'note-row' + (n.system ? ' system' : '');
    const minutes = Math.floor(n.ts / 60);
    const tStr = n.ts === 0 ? 'Start' : `+${formatDuration(n.ts)}`;
    row.innerHTML = `
      <div class="note-dot"></div>
      <div class="note-body">
        <div class="note-meta">
          <span>${tStr}</span>
          ${n.temp != null ? `<span class="note-temp">${fmtWithUnit(n.temp, 0)}</span>` : ''}
        </div>
        <div class="note-text">${escapeHtml(n.text)}</div>
      </div>
    `;
    list.appendChild(row);
  });
}

function escapeHtml(s) {
  return s.replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}

function addNote() {
  const input = document.getElementById('note-input');
  const text = input.value.trim();
  if (!text) return;
  const s = window.__cookState;
  if (!s.active) return;
  const ts = Math.floor((Date.now() - s.startedAt) / 1000);
  const lastMeat = meatHistory.length ? meatHistory[meatHistory.length - 1].c : null;
  s.notes.push({ ts, text, temp: lastMeat });
  wsSend({ type: 'cook_note', text, meat_c: lastMeat ?? 0 });
  input.value = '';
  renderNotesCard();
}
window.addNote = addNote;

function toggleNotesVisibility() {
  const card = document.getElementById('notes-card');
  if (!card) return;
  card.style.display = window.__cookState.active ? '' : 'none';
}

// ─────────────────────────────────────────────────────────────────────────
//  FAVOURITES — render + save/load/delete
// ─────────────────────────────────────────────────────────────────────────

function renderFavourites() {
  const list = document.getElementById('fav-list');
  if (!list) return;
  const s = window.__cookState;
  list.innerHTML = '';

  if (!s.favourites.length) {
    list.innerHTML = `<div class="fav-empty">No favourites yet.<br>Save a great cook to recreate it next time.</div>`;
    return;
  }

  s.favourites.forEach(f => {
    const row = document.createElement('div');
    row.className = 'fav-row';
    const meat = unit === 'F' ? Math.round(cToF(f.targets.meatTarget)) : f.targets.meatTarget;
    const lo   = unit === 'F' ? Math.round(cToF(f.targets.grillLow))   : f.targets.grillLow;
    const hi   = unit === 'F' ? Math.round(cToF(f.targets.grillHigh))  : f.targets.grillHigh;
    row.innerHTML = `
      <div class="fav-icon">${initials(f.name)}</div>
      <div class="fav-info">
        <div class="fav-name">${escapeHtml(f.name)}</div>
        <div class="fav-targets">${meat}°${unit} meat · ${lo}–${hi}°${unit} grill</div>
      </div>
      <div class="fav-actions">
        <button class="fav-btn" onclick="loadFavourite('${f.id}', event)">Load</button>
        <button class="fav-btn danger" onclick="deleteFavourite('${f.id}')" aria-label="Delete">✕</button>
      </div>
    `;
    list.appendChild(row);
  });
}

function loadFavourite(id, e) {
  const f = window.__cookState.favourites.find(x => x.id === id);
  if (!f) return;
  settings.meatTarget = f.targets.meatTarget;
  settings.grillLow   = f.targets.grillLow;
  settings.grillHigh  = f.targets.grillHigh;
  window.__cookState.preset = f.preset || null;
  syncSettingsInputs();
  renderPresetChips();
  rebuildChart();
  if (meatHistory.length) {
    updateUI(meatHistory[meatHistory.length - 1].c, grillHistory[grillHistory.length - 1].c);
  }
  if (typeof sendSettings === 'function') sendSettings();
  // Visual confirmation
  const btn = e && e.target;
  if (btn) {
    const orig = btn.textContent;
    btn.textContent = 'Loaded';
    setTimeout(() => { btn.textContent = orig; }, 1200);
  }
}
window.loadFavourite = loadFavourite;

function deleteFavourite(id) {
  window.__cookState.favourites = window.__cookState.favourites.filter(f => f.id !== id);
  wsSend({ type: 'fav_delete', id });
  renderFavourites();
}
window.deleteFavourite = deleteFavourite;

// ─────────────────────────────────────────────────────────────────────────
//  HISTORY VIEW — list + detail/replay
// ─────────────────────────────────────────────────────────────────────────

function setView(view) {
  window.__cookState.view = view;
  document.getElementById('tab-live').classList.toggle('active', view === 'live');
  document.getElementById('tab-history').classList.toggle('active', view === 'history');
  document.getElementById('view-live').style.display    = view === 'live'    ? 'flex' : 'none';
  document.getElementById('view-history').style.display = view === 'history' ? 'flex' : 'none';
  if (view === 'history') renderHistoryList();
}
window.setView = setView;

function makeSparkSVG(seed, peakMeat, target) {
  // Tiny sparkline — deterministic by seed
  const w = 56, h = 36, pts = 24;
  let d = '';
  for (let i = 0; i < pts; i++) {
    const x = (i / (pts - 1)) * w;
    const t = i / (pts - 1);
    // Sigmoid-ish climb to peak, slight noise
    const noise = Math.sin(seed + i * 0.7) * 1.2;
    const y = h - 2 - (h - 6) * (1 / (1 + Math.exp(-(t - 0.4) * 6))) + noise * 0.3;
    d += (i === 0 ? 'M' : 'L') + x.toFixed(1) + ',' + y.toFixed(1) + ' ';
  }
  return `<svg viewBox="0 0 ${w} ${h}" preserveAspectRatio="none">
    <line x1="0" y1="${h * 0.35}" x2="${w}" y2="${h * 0.35}" stroke="rgba(255,77,141,0.3)" stroke-width="0.8" stroke-dasharray="2,2"/>
    <path d="${d}" fill="none" stroke="#ff4d8d" stroke-width="1.4"/>
  </svg>`;
}

function renderHistoryList() {
  const list = document.getElementById('history-list');
  if (!list) return;
  const s = window.__cookState;
  list.innerHTML = '';

  if (!s.history.length) {
    list.innerHTML = `
      <div class="history-empty">
        <div class="history-empty-icon">🥩</div>
        <div class="history-empty-title">No cooks yet</div>
        <div class="history-empty-sub">Tap <b>Start cook</b> on the Live tab to begin logging your first session.</div>
      </div>
    `;
    return;
  }

  s.history.forEach((c, i) => {
    const row = document.createElement('div');
    row.className = 'history-row';
    const hit = c.targetHitMin != null ? `<span class="ok">Target hit</span>` : `<span>Stopped early</span>`;
    row.innerHTML = `
      <div class="history-thumb">${makeSparkSVG(i + 1, c.peakMeat, c.targets.meatTarget)}</div>
      <div class="history-info">
        <div class="history-name">${escapeHtml(c.name)}${c.favourite ? ' ★' : ''}</div>
        <div class="history-meta">
          <span>${formatRelDate(c.startedAt)}</span>
          <span class="dot">·</span>
          <span>${formatDuration(c.duration)}</span>
          <span class="dot">·</span>
          ${hit}
        </div>
      </div>
      <div class="history-arrow">›</div>
    `;
    row.onclick = () => openHistoryDetail(c.id);
    list.appendChild(row);
  });
}

let detailReplayChart = null;

function openHistoryDetail(id) {
  const c = window.__cookState.history.find(x => x.id === id);
  if (!c) return;
  window.__cookState.selectedHistoryId = id;
  document.getElementById('detail-overlay').classList.add('open');
  document.getElementById('detail-modal').classList.add('open');
  document.getElementById('detail-title').textContent = c.name;
  document.getElementById('detail-fav-btn').classList.toggle('active', c.favourite);

  // Stats
  document.getElementById('detail-stats').innerHTML = `
    <div class="stat-cell">
      <span class="stat-label">Duration</span>
      <span class="stat-value">${formatDuration(c.duration).split(' ')[0]}<span class="stat-unit">${c.duration >= 3600 ? 'h' : 'm'}</span></span>
    </div>
    <div class="stat-cell">
      <span class="stat-label">Peak meat</span>
      <span class="stat-value">${unit === 'F' ? Math.round(cToF(c.peakMeat)) : c.peakMeat.toFixed(1)}<span class="stat-unit">°${unit}</span></span>
    </div>
    <div class="stat-cell">
      <span class="stat-label">Peak grill</span>
      <span class="stat-value">${unit === 'F' ? Math.round(cToF(c.peakGrill)) : Math.round(c.peakGrill)}<span class="stat-unit">°${unit}</span></span>
    </div>
  `;

  // Notes
  const notesEl = document.getElementById('detail-notes');
  if (!c.notes.length) {
    notesEl.innerHTML = '<div class="notes-empty">No notes were taken during this cook.</div>';
  } else {
    notesEl.innerHTML = '<div class="notes-list">' + c.notes.slice().reverse().map(n => {
      const tStr = n.ts === 0 ? 'Start' : `+${formatDuration(n.ts)}`;
      return `<div class="note-row${n.system ? ' system' : ''}">
        <div class="note-dot"></div>
        <div class="note-body">
          <div class="note-meta"><span>${tStr}</span></div>
          <div class="note-text">${escapeHtml(n.text)}</div>
        </div>
      </div>`;
    }).join('') + '</div>';
  }

  // Load From / Targets summary
  document.getElementById('detail-targets').innerHTML = `
    <div class="dialog-target-row"><span>Meat target</span><span>${unit === 'F' ? Math.round(cToF(c.targets.meatTarget)) : c.targets.meatTarget}°${unit}</span></div>
    <div class="dialog-target-row"><span>Grill range</span><span>${unit === 'F' ? Math.round(cToF(c.targets.grillLow)) : c.targets.grillLow}–${unit === 'F' ? Math.round(cToF(c.targets.grillHigh)) : c.targets.grillHigh}°${unit}</span></div>
    ${c.preset ? `<div class="dialog-target-row"><span>Preset</span><span>${c.preset}</span></div>` : ''}
  `;

  // Replay chart — synthesize a reasonable curve from the metadata
  setTimeout(() => renderReplayChart(c), 50);
}
window.openHistoryDetail = openHistoryDetail;

function closeHistoryDetail() {
  document.getElementById('detail-overlay').classList.remove('open');
  document.getElementById('detail-modal').classList.remove('open');
  if (detailReplayChart) { detailReplayChart.destroy(); detailReplayChart = null; }
}
window.closeHistoryDetail = closeHistoryDetail;

function toggleDetailFavourite() {
  const id = window.__cookState.selectedHistoryId;
  const c = window.__cookState.history.find(x => x.id === id);
  if (!c) return;
  c.favourite = !c.favourite;
  document.getElementById('detail-fav-btn').classList.toggle('active', c.favourite);

  if (c.favourite) {
    if (!window.__cookState.favourites.find(f => f.name === c.name)) {
      const favId = 'f-' + Date.now();
      window.__cookState.favourites.unshift({
        id: favId,
        name: c.name,
        targets: { ...c.targets },
        preset: c.preset,
      });
      wsSend({
        type:        'fav_save',
        id:          favId,
        name:        c.name,
        meat_target: c.targets.meatTarget,
        grill_low:   c.targets.grillLow,
        grill_high:  c.targets.grillHigh,
        preset:      c.preset || '',
      });
      renderFavourites();
    }
  } else {
    const existing = window.__cookState.favourites.find(f => f.name === c.name);
    if (existing) {
      window.__cookState.favourites = window.__cookState.favourites.filter(f => f.name !== c.name);
      wsSend({ type: 'fav_delete', id: existing.id });
      renderFavourites();
    }
  }
  renderHistoryList();
}
window.toggleDetailFavourite = toggleDetailFavourite;

function loadFromHistory() {
  const id = window.__cookState.selectedHistoryId;
  const c = window.__cookState.history.find(x => x.id === id);
  if (!c) return;
  settings.meatTarget = c.targets.meatTarget;
  settings.grillLow   = c.targets.grillLow;
  settings.grillHigh  = c.targets.grillHigh;
  window.__cookState.preset = c.preset || null;
  syncSettingsInputs();
  renderPresetChips();
  rebuildChart();
  if (meatHistory.length) {
    updateUI(meatHistory[meatHistory.length - 1].c, grillHistory[grillHistory.length - 1].c);
  }
  if (typeof sendSettings === 'function') sendSettings();
  closeHistoryDetail();
  setView('live');
  // brief flash on cook bar
  const bar = document.getElementById('cook-bar');
  if (bar) {
    bar.style.borderColor = 'var(--primary)';
    setTimeout(() => bar.style.borderColor = '', 1400);
  }
}
window.loadFromHistory = loadFromHistory;

// ── Replay chart ─────────────────────────────────────────────────────────

function renderReplayChart(c) {
  const canvas = document.getElementById('replay-chart');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  if (detailReplayChart) detailReplayChart.destroy();

  const points = 60;
  const labels = [];
  const meatData = [];
  const grillData = [];
  const seed = (c.id.charCodeAt(2) || 7) * 13.7;

  for (let i = 0; i < points; i++) {
    const t = i / (points - 1);
    labels.push(`${Math.round((t * c.duration) / 60)}m`);
    // Meat: sigmoid climb → flatten near target
    const sig = 1 / (1 + Math.exp(-(t - 0.45) * 6));
    const mC = 18 + (c.peakMeat - 18) * sig + Math.sin(seed + i * 0.4) * 0.6;
    // Grill: oscillating around midpoint of range
    const mid = (c.targets.grillLow + c.targets.grillHigh) / 2;
    const gC = mid + Math.sin(seed * 0.3 + i * 0.18) * (c.peakGrill - mid) * 0.7
                   + Math.cos(seed * 0.7 + i * 0.34) * 6;
    meatData.push(unit === 'F' ? cToF(mC) : mC);
    grillData.push(unit === 'F' ? cToF(gC) : gC);
  }
  const target = unit === 'F' ? cToF(c.targets.meatTarget) : c.targets.meatTarget;
  const lo     = unit === 'F' ? cToF(c.targets.grillLow)   : c.targets.grillLow;
  const hi     = unit === 'F' ? cToF(c.targets.grillHigh)  : c.targets.grillHigh;

  detailReplayChart = new Chart(ctx, {
    type: 'line',
    data: {
      labels,
      datasets: [
        { label: 'Meat',  data: meatData,  borderColor: '#ff4d8d', backgroundColor: 'rgba(255,77,141,0.1)', borderWidth: 2, fill: true,  tension: 0.4, pointRadius: 0 },
        { label: 'Grill', data: grillData, borderColor: 'rgba(255,255,255,0.7)', borderWidth: 2, fill: false, tension: 0.4, pointRadius: 0 },
        { label: 'Target', data: Array(points).fill(target), borderColor: 'rgba(255,77,141,0.4)', borderWidth: 1.5, borderDash: [6,4], pointRadius: 0, fill: false },
        { label: 'Grill low',  data: Array(points).fill(lo), borderColor: 'rgba(255,200,74,0.3)', borderWidth: 1, borderDash: [4,4], pointRadius: 0, fill: false },
        { label: 'Grill high', data: Array(points).fill(hi), borderColor: 'rgba(255,200,74,0.3)', borderWidth: 1, borderDash: [4,4], pointRadius: 0, fill: false },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: false,
      interaction: { mode: 'index', intersect: false },
      scales: {
        x: { border: { display: false }, grid: { color: 'rgba(255,255,255,0.04)' }, ticks: { color: '#444', maxTicksLimit: 6, font: { size: 10 } } },
        y: { border: { display: false }, grid: { color: 'rgba(255,255,255,0.04)' }, ticks: { color: '#444', font: { size: 10 }, callback: v => v + '°' + unit } },
      },
      plugins: {
        legend: { display: true, position: 'top', align: 'end', labels: { color: '#555', font: { size: 10 }, boxWidth: 16, boxHeight: 2, padding: 12, filter: i => ['Meat','Grill'].includes(i.text) } },
        tooltip: { enabled: false },
      },
    },
  });
}

// ─────────────────────────────────────────────────────────────────────────
//  INIT — wire up the new bits after DOM is ready
// ─────────────────────────────────────────────────────────────────────────

function initRoadmap() {
  renderPresetChips();
  renderCookBar();
  renderFavourites();
  renderNotesCard();
  toggleNotesVisibility();
  renderHistoryList();
  setInterval(tickCookBar, 1000);

  // Note input — submit on Enter
  const ni = document.getElementById('note-input');
  if (ni) {
    ni.addEventListener('keydown', e => {
      if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault();
        addNote();
      }
    });
  }
}

// Patch the existing applySettings to also re-render preset chips
const _origApply = window.applySettings;
window.applySettings = function() {
  const inputEl = document.getElementById('s-meat');
  if (inputEl && window.__cookState.preset) {
    const rawMeat = parseFloat(inputEl.value);
    const meatC = unit === 'F' ? fToC(rawMeat) : rawMeat;
    const p = window.__presets.find(x => x.name === window.__cookState.preset);
    if (!p || Math.abs(p.c - meatC) >= 0.5) {
      window.__cookState.preset = null;
    }
  }

  _origApply.apply(this, arguments);
  renderPresetChips();
};

// Patch setUnit to refresh chips/favourites
const _origSetUnit = window.setUnit;
window.setUnit = function(u) {
  _origSetUnit.apply(this, arguments);
  renderPresetChips();
  renderFavourites();
};

document.addEventListener('DOMContentLoaded', () => {
  // Wait a tick for original init to finish
  setTimeout(initRoadmap, 100);
});
