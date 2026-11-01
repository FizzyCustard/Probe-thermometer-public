// Browser tests for the web UI (device/data) against a simulated device: the
// real page runs in headless Chromium with its WebSocket answered from here.
//
// Run: npm test
const { chromium } = require('playwright-core');
const http = require('http');
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..', 'device', 'data');
const PORT = 8931;
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.svg': 'image/svg+xml', '.json': 'application/json' };

const server = http.createServer((req, res) => {
  const url = req.url.split('?')[0];
  fs.readFile(path.join(ROOT, url === '/' ? 'index.html' : url), (err, data) => {
    if (err) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(url)] || 'text/html' });
    res.end(data);
  });
});

let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${ok ? '' : '  — ' + detail}`);
  if (!ok) failures++;
}

const CONFIG = {
  type: 'config',
  settings: { meat_target: 74, grill_low: 160, grill_high: 200 },
  favourites: [{ id: "x');window.__xss=1;//", name: 'Odd id', meat_target: 74, grill_low: 160, grill_high: 200, preset: '' }],
};
let cookActive = false;
const cooks = [];
const tick = (meat, grill) => ({
  type: 'tick', ts: '2026-11-01T12:00:00',
  meat: { c: meat, r: 7500 }, grill: { c: grill, r: 3900 },
  sd: true, cook_active: cookActive, cook_name: cookActive ? cooks[0].name : undefined,
});

(async () => {
  await new Promise(r => server.listen(PORT, r));
  const browser = await chromium.launch(
    process.env.CHROMIUM_PATH ? { executablePath: process.env.CHROMIUM_PATH } : {}
  );
  const page = await browser.newPage();
  const pageErrors = [];
  page.on('pageerror', e => pageErrors.push(String(e)));

  // Simulated device
  const sent = [];
  let device;
  await page.routeWebSocket(/\/ws$/, ws => {
    device = ws;
    ws.onMessage(raw => {
      const msg = JSON.parse(raw);
      sent.push(msg);
      if (msg.type === 'cook_start') cookActive = true;
      if (msg.type === 'cook_stop') cookActive = false;
      if (msg.type === 'cook_start') {
        // Like cookStart(): one file per cook, commas stored as semicolons
        cooks.unshift({ file: `cook_${cooks.length + 1}.csv`, name: msg.name.replace(/,/g, ';'), duration_s: 60 });
      }
      if (msg.type === 'cook_stop') ws.send(JSON.stringify({ type: 'cook_stopped', file: cooks[0].file }));
      if (msg.type === 'request_cook_list') ws.send(JSON.stringify({ type: 'cook_list', cooks }));
    });
    ws.send(JSON.stringify(CONFIG));
  });

  const dialogs = [];
  page.on('dialog', d => { dialogs.push(d.message()); d.accept(); });

  await page.goto(`http://localhost:${PORT}/`);
  await page.waitForFunction(() => typeof chart !== 'undefined' && chart);

  const push = async (meat, grill) => {
    await page.evaluate(() => { lastChartAdd = 0; });
    device.send(JSON.stringify(tick(meat, grill)));
    await page.waitForTimeout(50);
  };

  // ── One probe missing ────────────────────────────────────────────────────
  await push(50, null);
  await push(55, null);
  await page.evaluate(() => {
    document.getElementById('s-meat').value = 70;
    document.getElementById('s-grill-low').value = 150;
    document.getElementById('s-grill-high').value = 190;
    applySettings();
  });
  await push(null, 170);
  const shape = await page.evaluate(() => ({
    meat: meatHistory.length, grill: grillHistory.length,
    labels: chart.data.labels.length, d0: chart.data.datasets[0].data.length,
    d1: chart.data.datasets[1].data.length,
  }));
  check('no crash with a probe missing', pageErrors.length === 0, pageErrors.join('; '));
  check('chart labels and datasets stay aligned',
    shape.meat === shape.grill && shape.labels === shape.d0 && shape.d0 === shape.d1, JSON.stringify(shape));

  const ghost = await page.evaluate(() => {
    unit = 'F';
    rebuildChart();
    const v = chart.data.datasets[1].data[0];
    unit = 'C';
    rebuildChart();
    return v;
  });
  check('disconnected probe is a chart gap in Fahrenheit, not 32°F', ghost === null, String(ghost));

  // ── Target validation ────────────────────────────────────────────────────
  const settingsSent = () => sent.filter(m => m.type === 'settings').length;
  const trySettings = async (meat, low, high) => {
    const before = settingsSent();
    await page.evaluate(([m, l, h]) => {
      document.getElementById('s-meat').value = m;
      document.getElementById('s-grill-low').value = l;
      document.getElementById('s-grill-high').value = h;
      applySettings();
    }, [meat, low, high]);
    await page.waitForTimeout(50);
    return settingsSent() > before;
  };
  check('out-of-range meat target is refused', !(await trySettings(200, 150, 190)) && dialogs.length === 1, dialogs.join('; '));
  check('out-of-range grill range is refused', !(await trySettings(70, 150, 500)) && dialogs.length === 2, dialogs.join('; '));
  check('inverted grill range is refused', !(await trySettings(70, 190, 150)) && dialogs.length === 3, dialogs.join('; '));
  check('valid targets are sent to the device', await trySettings(70, 150, 190));

  // ── Cook lifecycle ───────────────────────────────────────────────────────
  await page.evaluate(() => openStartCookDialog());
  check('start dialog shows the food-safety note',
    await page.locator('.dialog-disclaimer').isVisible());
  await page.evaluate(() => {
    document.getElementById('start-cook-name').value = 'Ribs, Sunday';
    confirmStartCook();
  });
  await page.waitForTimeout(50);
  await push(60, null);
  await push(72, 170);   // reaches the 70° target...
  await push(65, 170);   // ...then rests back below it
  await push(null, 170); // ...and the probe drops out
  await page.evaluate(() => confirmStopCook(false));
  await page.waitForFunction(() => window.__cookState.history[0]?.id === 'cook_1.csv');
  const cook = await page.evaluate(() => window.__cookState.history[0]);
  check('stopping a cook picks up the device file name', cook.id === 'cook_1.csv', cook.id);
  check('target hit survives a later dip', cook.targetHitMin === 0, String(cook.targetHitMin));
  check('peak ignores the gaps', cook.peakMeat === 72, String(cook.peakMeat));
  const row = await page.evaluate(() => document.getElementById('history-list').textContent);
  check('history row shows the target was hit', row.includes('Target hit'), row);

  await page.evaluate(() => {
    openStartCookDialog();
    document.getElementById('start-cook-name').value = 'Second cook';
    confirmStartCook();
  });
  await page.waitForTimeout(50);
  await push(40, 170);
  await page.evaluate(() => confirmStopCook(false));
  await page.waitForFunction(() => window.__cookState.history[0]?.id === 'cook_2.csv');
  const earlier = await page.evaluate(() => window.__cookState.history[1]);
  check('a later cook keeps earlier stats', earlier.targetHitMin === 0 && earlier.peakMeat === 72, JSON.stringify(earlier));

  // ── Untrusted device strings ─────────────────────────────────────────────
  await page.evaluate(() => renderFavourites());
  await page.locator('.fav-btn').first().evaluate(b => b.click());
  await page.waitForTimeout(50);
  check('a hostile favourite id does not execute', (await page.evaluate(() => window.__xss)) === undefined);

  const injected = await page.evaluate(() => {
    cal.open = true;
    cal.step = CAL_STEPS.length - 1;
    cal.results = { meat: { saved: false, applied: false, reason: '<img id="xss-probe" src="x">' } };
    calRender();
    const found = !!document.getElementById('xss-probe');
    cal.open = false;
    return found;
  });
  check('device rejection text is escaped', injected === false);

  check('no uncaught page errors', pageErrors.length === 0, pageErrors.join('; '));
  await browser.close();
  server.close();
  console.log(failures ? `FAILED (${failures})` : 'OK');
  process.exit(failures ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
