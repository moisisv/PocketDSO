/**
 * E2E 01 — Settings state parity & NVS persistence.
 * Requires firmware with /api/state, /api/save, /api/reboot.
 *
 *   node e2e_01_settings_persistence.js [--host pocketdso.local]
 */
'use strict';
const { Dso, Tester, sleep } = require('./dso_remote');

const host = (process.argv.find((a, i) => process.argv[i - 1] === '--host') || 'pocketdso.local');
const dso = new Dso(host);
const t = new Tester('01 state & persistence');

const idxOk = (v, n) => Number.isInteger(v) && v >= 0 && v < n;

(async () => {
  // --- 1. /api/state returns a complete, in-range snapshot -------------------
  await dso.waitUp();
  const st = await dso.state();
  console.log('  state:', JSON.stringify(st));
  t.ok(st.fw && st.fw.length > 0, 'state.fw present');
  t.ok(idxOk(st.timeBaseIdx, 11), 'timeBaseIdx in range');
  t.ok(idxOk(st.vDivIdx, 7), 'vDivIdx in range');
  t.ok(idxOk(st.sampleRateIdx, 4), 'sampleRateIdx in range');
  t.ok(idxOk(st.probeIdx, 4) && idxOk(st.probeBIdx, 4), 'probeIdx/probeBIdx in range');
  t.ok(idxOk(st.calFreqIdx, 4), 'calFreqIdx in range');
  t.ok(idxOk(st.ch2Mode, 3), 'ch2Mode in range (0=off 1=analog 2=digital)');
  t.ok(idxOk(st.trigMode, 4), 'trigMode in range');
  t.ok(idxOk(st.hystIdx, 4), 'hystIdx in range');
  t.ok(idxOk(st.brightnessIdx, 4), 'brightnessIdx in range');
  t.ok(idxOk(st.fftSpanIdx, 4) && idxOk(st.fftZoomIdx, 5), 'fft settings in range');
  t.ok(st.actualFs > 1000, 'actualFs plausible', 'fs=' + st.actualFs);

  // --- 2. /api/set is reflected in /api/state --------------------------------
  await dso.set({ hystIdx: 2 });            // 100 mV
  await sleep(300);
  t.eq((await dso.state()).hystIdx, 2, '/api/set hystIdx=2 reflected in state');
  await dso.set({ hystIdx: 1 });            // restore 50 mV

  // --- 3. Persistence: distinctive state -> save -> reboot -> compare ---------
  const distinctive = {
    timeBaseIdx: 8,   // 10 ms/div
    vDivIdx: 4,       // 2 V/div
    probeIdx: 2,      // 4x
    ch2Mode: 2,       // digital
    hystIdx: 3,       // 200 mV
    autoT: 0, autoV: 0,
  };
  await dso.set(distinctive);
  await dso.save();
  await sleep(300);
  await dso.reboot(false); // already saved explicitly; reboot must not lose it
  const after = await dso.state();
  t.eq(after.timeBaseIdx, 8, 'timeBaseIdx persisted across reboot');
  t.eq(after.vDivIdx, 4, 'vDivIdx persisted across reboot');
  t.eq(after.probeIdx, 2, 'probeIdx persisted across reboot');
  t.eq(after.ch2Mode, 2, 'ch2Mode persisted across reboot');
  t.eq(after.hystIdx, 3, 'hystIdx persisted across reboot');
  t.eq(after.autoTimebase, 0, 'autoTimebase=0 persisted across reboot');
  t.eq(after.autoVolts, 0, 'autoVolts=0 persisted across reboot');
  t.eq(after.running, undefined, 'running not persisted (always boots RUN)');

  // --- 4. Debounced auto-save: change via /api/set WITHOUT /api/save ---------
  await dso.set({ hystIdx: 2 });            // no explicit save; auto-commit in ~1.5 s
  await sleep(2500);                        // > debounce window
  await dso.reboot(false);
  t.eq((await dso.state()).hystIdx, 2, 'debounced auto-save committed hystIdx across reboot');

  // --- 5. Restore validated defaults -----------------------------------------
  await dso.set({
    timeBaseIdx: 7, vDivIdx: 4, sampleRateIdx: 2, probeIdx: 1, probeBIdx: 2,
    ch2Mode: 1, trigMode: 0, hystIdx: 1, calFreqIdx: 1, brightness: 3,
    fftSpan: 1, fftLog: 1, fftZoom: 0,
    autoT: 1, autoV: 1, acCoupled: 0, selfTest: 0,
  });
  await dso.save();
  await sleep(400);
  const fin = await dso.state();
  t.eq(fin.hystIdx, 1, 'defaults restored (hystIdx=1)');
  t.eq(fin.ch2Mode, 1, 'defaults restored (ch2Mode=analog)');
  t.eq(fin.autoTimebase, 1, 'defaults restored (autoT on)');

  t.finish();
})().catch((e) => { console.error('FATAL:', e.message); process.exit(1); });
