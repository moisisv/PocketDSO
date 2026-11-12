/**
 * PocketDSO remote test client (shared library for e2e tests).
 * Node >= 18, no dependencies. Targets the firmware's HTTP API.
 *
 *   const { Dso, BUTTONS } = require('./dso_remote');
 *   const dso = new Dso('pocketdso.local');
 *   await dso.tapName('menu');
 *   const st = await dso.state();
 */
'use strict';

// Button centers in landscape screen coordinates (320x172), validated 2026-11-12.
const BUTTONS = {
  // Scope screen — top bar
  auto: [27, 13], fft: [182, 13], batSys: [212, 13], wifiNet: [239, 13], menu: [283, 13],
  // Scope screen — bottom bar
  bAuto: [43, 155], tbPrev: [60, 155], tbNext: [110, 155], vdPrev: [180, 155],
  vdNext: [240, 155], bRun: [277, 155],
  // FFT screen
  fftBack: [38, 13], fZoom: [43, 155], fSpan: [123, 155], fMag: [200, 155], fRun: [277, 155],
  // Settings modal — tabs & done
  tabScope: [33, 16], tabTrig: [80, 16], tabIo: [127, 16], tabWifi: [174, 16],
  tabSys: [221, 16], done: [280, 16],
};
// Settings tiles: [tabName, tileName, x, y]
const TILES = {
  'scope:autoT':   [84, 51],  'scope:autoV':  [238, 51],
  'scope:tbPrev':  [60, 97],  'scope:tbNext': [110, 97],
  'scope:vdPrev':  [180, 97], 'scope:vdNext': [240, 97],
  'scope:rate':    [84, 143], 'scope:probeA': [200, 143], 'scope:probeB': [270, 143],
  'trig:edge':     [84, 51],  'trig:coupling': [238, 51],
  'trig:level':    [84, 97],  'trig:hyst':    [238, 97],
  'trig:arm':      [84, 143], 'trig:run':     [238, 143],
  'io:ch2':        [84, 51],  'io:source':    [238, 51],  'io:calGen': [84, 97],
  'sys:bright':    [84, 97],  'sys:reset':    [238, 97],
};

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

class Dso {
  constructor(host = 'pocketdso.local') {
    this.host = String(host).replace(/^https?:\/\//, '').replace(/\/+$/, '');
    this.log = [];
  }

  async tap(x, y) {
    const r = await fetch(`http://${this.host}/api/tap?x=${x}&y=${y}`);
    const j = await r.json();
    this.log.push({ tap: [x, y], resp: j });
    if (!j.ok) throw new Error(`tap(${x},${y}) failed: ${JSON.stringify(j)}`);
    return j;
  }

  async tapName(name) {
    const b = BUTTONS[name];
    if (!b) throw new Error(`unknown button "${name}"`);
    return this.tap(b[0], b[1]);
  }

  async tapTile(name) {
    const t = TILES[name];
    if (!t) throw new Error(`unknown tile "${name}"`);
    const resp = await this.tap(t[0], t[1]);
    // Clicking a tile is only possible in the settings modal; callers manage tabs.
    return resp;
  }

  async metrics() {
    const r = await fetch(`http://${this.host}/api/metrics`);
    if (!r.ok) throw new Error(`metrics HTTP ${r.status}`);
    return await r.json();
  }

  async state() {
    const r = await fetch(`http://${this.host}/api/state`);
    if (!r.ok) throw new Error(`state HTTP ${r.status}`);
    return await r.json();
  }

  async set(params) {
    const q = new URLSearchParams(params).toString();
    const r = await fetch(`http://${this.host}/api/set?${q}`);
    const t = await r.text();
    if (!t.startsWith('OK')) throw new Error(`set(${q}) -> ${t}`);
    return t;
  }

  async save() {
    const r = await fetch(`http://${this.host}/api/save`);
    return await r.text();
  }

  async reboot(withSave = true) {
    const r = await fetch(`http://${this.host}/api/reboot${withSave ? '?save=1' : ''}`);
    const t = await r.text();
    await sleep(1000);
    await this.waitUp(40); // boot + Wi-Fi + web server
    return t;
  }

  async waitUp(timeoutS = 30) {
    const t0 = Date.now();
    for (;;) {
      try {
        await this.metrics();
        return true;
      } catch (e) { /* not up yet */ }
      if ((Date.now() - t0) / 1000 > timeoutS) throw new Error('device did not come back up');
      await sleep(800);
    }
  }

  /** Screenshot -> PNG file, returns {w,h,path}. */
  async shot(name, outPath) {
    const path = require('path');
    const m = require(path.join(__dirname, '..', 'screenshot.js'));
    const fs = require('fs');
    const s = await m.fetchRaw(this.host);
    const rot = m.rotateRows(m.toRgbRows(s.fbw, s.fbh, s.palette, s.fb), s.fbw, s.fbh, m.defaultRotationCcW(s.rot));
    const p = outPath || (path.join(require('os').tmpdir(), 'opencode', name + '.png'));
    fs.mkdirSync(path.dirname(p), { recursive: true });
    fs.writeFileSync(p, m.encodePng(rot.w, rot.h, rot.rows));
    return { w: rot.w, h: rot.h, path: p };
  }

  /** Zoomed crop screenshot (nearest-neighbor scale) for visual assertions. */
  async shotCrop(name, cx, cy, cw, ch, scale = 6) {
    const path = require('path');
    const m = require(path.join(__dirname, '..', 'screenshot.js'));
    const fs = require('fs');
    const s = await m.fetchRaw(this.host);
    const { rows, w, h } = m.rotateRows(m.toRgbRows(s.fbw, s.fbh, s.palette, s.fb), s.fbw, s.fbh, m.defaultRotationCcW(s.rot));
    const out = [];
    for (let y = 0; y < ch * scale; ++y) {
      const row = Buffer.alloc(cw * scale * 3);
      const src = rows[Math.min(h - 1, cy + (y / scale | 0))];
      for (let x = 0; x < cw * scale; ++x) {
        const sx = Math.min(w - 1, cx + (x / scale | 0));
        src.copy(row, x * 3, sx * 3, sx * 3 + 3);
      }
      out.push(row);
    }
    const p = path.join(require('os').tmpdir(), 'opencode', name + '.png');
    fs.mkdirSync(path.dirname(p), { recursive: true });
    fs.writeFileSync(p, m.encodePng(cw * scale, ch * scale, out));
    return p;
  }

  async closeMenu() { return this.tapName('done'); }
}

/** Tiny assert helpers with test-run summary. */
class Tester {
  constructor(suite) { this.suite = suite; this.pass = 0; this.fail = []; }
  ok(cond, label, detail) {
    if (cond) { this.pass++; console.log(`  PASS ${label}`); }
    else { this.fail.push(label); console.log(`  FAIL ${label}${detail ? ' — ' + detail : ''}`); }
  }
  eq(a, b, label) { this.ok(a === b, label, `got ${JSON.stringify(a)}, want ${JSON.stringify(b)}`); }
  near(a, b, tol, label) { this.ok(Math.abs(a - b) <= tol, label, `got ${a}, want ${b} ±${tol}`); }
  finish() {
    console.log(`\n[${this.suite}] ${this.pass} passed, ${this.fail.length} failed`);
    if (this.fail.length) { console.log('  failed: ' + this.fail.join(' | ')); process.exit(1); }
  }
}

module.exports = { Dso, Tester, BUTTONS, TILES, sleep };
