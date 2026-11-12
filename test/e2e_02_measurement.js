/**
 * E2E 02 — Acquisition & measurement validation using the deterministic
 * loopback (selfTest) source. No physical signal needed.
 *
 * Validated: frequency, duty, Vpp/DC levels, auto-timebase adaptation,
 * CH2 readouts in /api/metrics, CH2 mode gating.
 *
 *   node e2e_02_measurement.js [--host pocketdso.local]
 */
'use strict';
const { Dso, Tester, sleep } = require('./dso_remote');

const host = (process.argv.find((a, i) => process.argv[i - 1] === '--host') || 'pocketdso.local');
const dso = new Dso(host);
const t = new Tester('02 measurement (loopback)');

// Known calibration table (dso_core.h)
const CAL_HZ = [0, 1000, 10000, 50000];
const TB_US  = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000];
// Probe A default 2x -> loopback square is 0 / 6.6 V
const FULL_SCALE = 6.6;

(async () => {
  await dso.waitUp();

  // Deterministic baseline for the tests
  await dso.set({
    probeIdx: 1, ch2Mode: 1, trigMode: 0, acCoupled: 0,
    calFreqIdx: 1, selfTest: 1, autoT: 1, autoV: 1, running: 1,
  });
  await sleep(2500);
  let m = await dso.metrics();
  let st = await dso.state();

  // --- 1. 1 kHz loopback ------------------------------------------------------
  t.near(m.dc, FULL_SCALE / 2, 0.35, 'loopback 1k: DC ≈ half rail', `dc=${m.dc}`);
  t.near(m.vpp, FULL_SCALE, 0.35, 'loopback 1k: Vpp ≈ 6.6 V', `vpp=${m.vpp}`);
  t.near(m.freq, 1000, 25, 'loopback 1k: freq ≈ 1 kHz (±2.5%)', `freq=${m.freq}`);
  t.near(m.duty, 50, 3, 'loopback 1k: duty ≈ 50%', `duty=${m.duty}`);
  // autoT: 1 kHz -> best timebase 200 µs/div (idx 3; table: 0=20us..3=200us).
  // Poll: a single glitched freq estimate can transiently pick 500 µs (idx 4).
  let autoTOk = false, lastTb = -1;
  for (let i = 0; i < 5 && !autoTOk; ++i) {
    await sleep(700);
    st = await dso.state();
    lastTb = st.timeBaseIdx;
    autoTOk = (lastTb === 3);
  }
  t.ok(autoTOk, 'autoT settled at 200us/div for 1 kHz', `tb=${lastTb}`);
  // autoV: vmax 6.6 * 1.25 / 6 div -> 2 V/div (idx 4)
  t.eq(st.vDivIdx, 4, 'autoV picked 2 V/div', `vd=${st.vDivIdx}`);
  // Adaptive fs: fast timebase -> fs well above the 1 kHz Nyquist floor
  t.ok(m.fs > 50000, 'adaptive fs ramps up on fast timebase', `fs=${m.fs}`);

  // --- 2. 10 kHz loopback ------------------------------------------------------
  // The estimator can emit one glitched estimate right after the cal switch
  // (e.g. 7.6 kHz), then settles within ~1 s — poll until in tolerance.
  await dso.set({ calFreqIdx: 2 });
  let freq10 = NaN, ok10 = false;
  for (let i = 0; i < 6 && !ok10; ++i) {
    await sleep(900);
    m = await dso.metrics(); st = await dso.state();
    freq10 = m.freq;
    ok10 = Math.abs(freq10 - 10000) <= 300;
  }
  t.ok(ok10, 'loopback 10k: freq ≈ 10 kHz (±3%)', `freq=${freq10}`);
  t.ok(st.timeBaseIdx <= 2, 'autoT picked <= 200us/div for 10 kHz', `tb=${st.timeBaseIdx}`);

  // --- 3. CH2 readouts in /api/metrics (analog loopback) ----------------------
  t.ok(m.ch2vpp > 0.5, 'ch2vpp non-zero in analog mode', `ch2vpp=${m.ch2vpp}`);
  t.ok(m.ch2dc > 0.5, 'ch2dc non-zero in analog mode', `ch2dc=${m.ch2dc}`);

  // --- 4. CH2 OFF gates the readouts ------------------------------------------
  await dso.set({ ch2Mode: 0 });
  await sleep(1200);
  m = await dso.metrics();
  t.eq(m.ch2vpp, 0, 'ch2vpp = 0 when CH2 OFF');
  t.eq(m.ch2dc, 0, 'ch2dc = 0 when CH2 OFF');

  // --- 5. CH2 digital mode readouts -------------------------------------------
  await dso.set({ ch2Mode: 2 });
  await sleep(1500);
  m = await dso.metrics();
  t.ok(m.ch2vpp > 3, 'ch2 digital: vpp ≈ 3.3 V logic swing', `ch2vpp=${m.ch2vpp}`);
  t.ok(m.ch2dc > 1 && m.ch2dc < 2.3, 'ch2 digital: dc ≈ mid rail', `ch2dc=${m.ch2dc}`);

  // --- 6. Coupling AC: top-bar semantics — AC RMS exposure via ac metric ------
  await dso.set({ ch2Mode: 1, acCoupled: 1 });
  await sleep(1500);
  m = await dso.metrics();
  t.near(m.ac, FULL_SCALE / 2, 0.4, 'AC coupling: rms ≈ half of square swing', `ac=${m.ac}`);
  await dso.set({ acCoupled: 0 });

  // --- 7. Nyquist guard data point (documented limitation) --------------------
  await dso.set({ calFreqIdx: 3 }); // 50 kHz, near the ~151 kS/s real-rate ceiling
  let freq50 = NaN, ok50 = false;
  for (let i = 0; i < 6 && !ok50; ++i) {
    await sleep(900);
    m = await dso.metrics();
    freq50 = m.freq;
    ok50 = Math.abs(freq50 - 50000) <= 1500;
  }
  t.ok(ok50, 'loopback 50k: freq ≈ 50 kHz (±3%)', `freq=${freq50} fs=${m.fs}`);

  // --- Restore ----------------------------------------------------------------
  await dso.set({ calFreqIdx: 1, selfTest: 0, ch2Mode: 1, trigMode: 0, autoT: 1, autoV: 1 });
  await sleep(800);

  t.finish();
})().catch((e) => { console.error('FATAL:', e.message); process.exit(1); });
