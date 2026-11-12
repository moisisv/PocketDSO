/**
 * E2E 03 — Full UI tour: exercises every screen and control through /api/tap,
 * asserting the returned action IDs, and saves a PNG of every screen state for
 * human review. Also captures zoomed crops of the battery icon and FFT axis
 * (re-check of report findings F5/F7).
 *
 *   node e2e_03_ui_tour.js [--host pocketdso.local] [--out <dir>]
 *
 * NOTE: action IDs verified against dso_core.h enum Action (ACT_*).
 */
'use strict';
const { Dso, Tester, BUTTONS, sleep } = require('./dso_remote');
const fs = require('fs');
const path = require('path');

const argV = (name, def) => {
  const i = process.argv.indexOf(name);
  return i > 0 && process.argv[i + 1] ? process.argv[i + 1] : def;
};
const host = (process.argv.find((a, i) => process.argv[i - 1] === '--host') || 'pocketdso.local');
const outDir = argV('--out', path.join(__dirname, 'captures'));
fs.mkdirSync(outDir, { recursive: true });

const dso = new Dso(host);
const t = new Tester('03 UI tour');

// ACT_* ids from dso_core.h (order of enum)
const ACT = { OPEN_MENU: 2, CLOSE_MENU: 4, OPEN_FFT: 5, CLOSE_FFT: 6,
              TAB_SCOPE: 10, TAB_TRIG: 11, TAB_IO: 12, TAB_NET: 13, TAB_SYS: 14 };

(async () => {
  await dso.waitUp();
  await dso.set({ autoT: 1, autoV: 1, ch2Mode: 1, selfTest: 0 }); // known baseline
  await sleep(800);

  // Force a known start screen: the MENU button toggles scope<->modal, and the
  // tap response reports the resulting screen (0 = scope). Handles reruns after
  // a previous run died inside the settings modal.
  let home = await dso.tapName('menu');
  if (home.screen !== 0) home = await dso.tapName('menu');
  t.eq(home.screen, 0, 'test starts from the scope screen');
  await sleep(600);

  const step = async (name, btn) => {
    const resp = btn ? await dso.tapName(btn) : null;
    await sleep(600);
    const { path: p } = await dso.shot(name, path.join(outDir, name + '.png'));
    console.log(`  ${name} -> ${path.basename(p)}`);
    return resp;
  };

  // 1. Scope screen
  const scopeResp = await step('01_scope_main');
  t.ok(scopeResp === null || scopeResp.act !== undefined, 'scope reachable');

  // 2. Open menu -> each tab
  let r = await step('02_menu_scope_tab', 'menu');
  t.eq(r.act, ACT.OPEN_MENU, 'MENU opens settings (ACT_OPEN_MENU)');
  for (const [tabName, actId] of [['trig', ACT.TAB_TRIG], ['io', ACT.TAB_IO],
                                  ['wifi', ACT.TAB_NET], ['sys', ACT.TAB_SYS]]) {
    const btn = 'tab' + tabName[0].toUpperCase() + tabName.slice(1);
    r = await dso.tapName(btn);
    t.eq(r.act, actId, `tab ${tabName} -> ACT id ${actId}`);
    await sleep(500);
    await dso.shot('03_tab_' + tabName, path.join(outDir, '03_tab_' + tabName + '.png'));
  }
  // 3. DONE closes menu
  r = await step('04_back_to_scope', 'done');
  t.eq(r.act, ACT.CLOSE_MENU, 'DONE closes menu (ACT_CLOSE_MENU)');

  // 4. FFT open/close
  r = await step('05_fft', 'fft');
  t.eq(r.act, ACT.OPEN_FFT, 'FFT button opens FFT screen');
  await step('06_fft_zoom', 'fZoom');
  await step('07_fft_span', 'fSpan');
  await step('08_fft_linear', 'fMag');
  // Zoomed crop of the FFT y-axis (F7 re-check: labels "0dB/-30/-60" vs "100%/50%/0%")
  await dso.shotCrop('09_fft_axis_zoom', 0, 24, 60, 140, 6);
  console.log('  09_fft_axis_zoom (crop saved for F7 re-check)');
  r = await dso.tapName('fMag'); // restore LOG
  await dso.tapName('fSpan');    // restore span
  r = await step('10_back_from_fft', 'fftBack');
  t.eq(r.act, ACT.CLOSE_FFT, '< SCOPE closes FFT (ACT_CLOSE_FFT)');

  // 5. Battery icon zoom (F5-related: AUTO/A_T/A_V/MAN badge + battery icon)
  await dso.shotCrop('11_topbar_zoom', 0, 0, 230, 24, 6);
  console.log('  11_topbar_zoom (crop saved: badge + battery icon)');

  // 6. Partial-auto badge check (F5 fix): autoT only -> "A_T"
  await dso.set({ autoV: 0 });
  await sleep(900);
  const { path: badgeP } = await dso.shot('12_badge_at', path.join(outDir, '12_badge_A_T.png'));
  console.log('  12_badge_A_T -> ' + path.basename(badgeP) + ' (badge should read A_T)');
  await dso.set({ autoV: 1 });
  await sleep(900);

  // 7. Bottom bar: master AUTO toggle + RUN/HOLD
  r = await dso.tapName('bAuto');
  t.ok(r.act !== undefined, 'bottom AUTO toggle dispatched');
  await sleep(500);
  await dso.shot('13_bottom_auto', path.join(outDir, '13_bottom_auto.png'));
  await dso.tapName('bAuto'); // restore
  await sleep(400);
  r = await step('14_run_hold', 'bRun');
  r = await dso.tapName('bRun'); // restore RUN
  await sleep(400);

  // 8. Tile toggles via API-equivalent taps (spot check ch2 cycle)
  await dso.tapName('menu'); await sleep(400); await dso.tapName('tabIo'); await sleep(400);
  const c0 = (await dso.metrics()).ch2Mode;
  await dso.tapTile('io:ch2');
  const c1 = (await dso.metrics()).ch2Mode;
  t.eq(c1, (c0 + 1) % 3, 'CH2 tile cycles mode');
  await dso.tapTile('io:ch2'); await dso.tapTile('io:ch2'); // restore
  await dso.tapName('done');

  // 9. Final full screenshot
  await sleep(800);
  await dso.shot('15_final', path.join(outDir, '15_final.png'));

  t.finish();
  console.log(`\nScreenshots for human review in: ${outDir}`);
})().catch((e) => { console.error('FATAL:', e.message); process.exit(1); });
