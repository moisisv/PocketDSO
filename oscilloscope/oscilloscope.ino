/*******************************************************************************
 * PocketDSO — 262K Color Touchscreen Digital Storage Oscilloscope (DC & AC)
 * =============================================================================
 * Target Board : Waveshare ESP32-C6-Touch-LCD-1.47 / ESP32-C6-Touch-LCD-1.47-M
 * MCU          : ESP32-C6FH8 (RISC-V 160 MHz, 512 KB SRAM, 8 MB Flash)
 * Display      : 1.47" IPS LCD, 172 × 320 px, 262K Colors (JD9853 SPI driver)
 * Touch Panel  : AXS5106L Capacitive Touch Controller (I2C @ 0x63)
 *
 * Verified Hardware Pinout (Waveshare Official Schematic):
 *   - LCD SPI    : SCK=GPIO1, MOSI=GPIO2, MISO=GPIO3, CS=GPIO14, DC=GPIO15,
 *                  RST=GPIO22, BL=GPIO23
 *   - TF Card CS : GPIO4 (driven HIGH on boot to prevent SPI bus conflict)
 *   - Touch I2C  : SDA=GPIO18, SCL=GPIO19, RST=GPIO20, INT=GPIO21
 *   - Probe CH1  : GPIO6 (ADC1_CH6) via 1 kΩ series protection resistor
 *   - Probe CH2  : GPIO5 (ADC1_CH5 or Digital D1) via 1 kΩ series protection resistor
 *   - Battery ADC: GPIO0 (ADC1_CH0, onboard 200k/100k 3:1 divider)
 *   - Cal Gen Out: GPIO7 (Built-in PWM square-wave calibration output: 1 kHz)
 *
 * Major Upgrades & Enhancements:
 *   1. 262K-Color Enhanced UI: Vibrant 16-bit RGB565 palette tailored for the
 *      1.47" JD9853 IPS panel with neon yellow/cyan traces, reticle tick marks,
 *      gradient status bars, real 4-bar Wi-Fi signal icon, and battery / uptime
 *      stats on the SYS tab.
 *   2. Smart IP Allocation (.100 Default): Configured to default to .100 IP
 *      (e.g. 192.168.1.142). If .100 is reserved or occupied, the firmware probes
 *      the network, steps to .101, .102... and gracefully falls back to DHCP.
 *   3. On-Screen Network Statistics Dashboard: Dedicated [WIFI] tab displays
 *      connected IP, network SSID, RSSI dBm with 4 signal bars, Gateway, MAC,
 *      active Web clients, and a touchscreen [RECONNECT] button.
 *   4. Next-Gen Oscilloscope Web App: Retina-sharp HTML5 canvas, phosphor trace
 *      glow, interactive draggable trigger level, measurement cursors (ΔT, ΔV, f),
 *      live FFT frequency spectrum, CSV export, and 1-click PNG screen capture.
 *   5. NVS Flash Persistence: Remembers timebase, V/div, trigger level, coupling,
 *      probe attenuation, and LCD brightness across reboots.
 *   6. Modular Tested Architecture: Platform-independent core in dso_core.h
 *      tested with host C++ unit test suite.
 ******************************************************************************/

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <math.h>
#include <stdarg.h>

#include "dso_core.h"
#include "wifi_config.h"

// Web server (set to 1 to enable Wi-Fi remote Web UI & WebSocket streaming)
#define ENABLE_WEB_UI 1

#if ENABLE_WEB_UI
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <ESPmDNS.h>
extern "C" {
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
}

WebServer server(80);

// Next-Generation Responsive Dark-Theme Oscilloscope Web Application
const char index_html[] PROGMEM = R"EOS(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>PocketDSO — 262K Color Touchscreen Oscilloscope</title>
  <style>
    :root {
      --bg: #090d16;
      --card-bg: #121826;
      --border: #232d42;
      --text: #e2e8f0;
      --text-muted: #8e9bb0;
      --ch1: #ffe600;
      --ch2: #00e5ff;
      --d1: #00ff9d;
      --trig: #ff2a85;
      --primary: #3b82f6;
      --success: #10b981;
      --danger: #ef4444;
      --amber: #f59e0b;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", sans-serif; background: var(--bg); color: var(--text); padding: 12px; font-size: 14px; }
    .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); padding-bottom: 10px; margin-bottom: 12px; flex-wrap: wrap; gap: 8px; }
    .brand { display: flex; align-items: center; gap: 10px; }
    .logo { font-size: 1.25rem; font-weight: 800; background: linear-gradient(135deg, #60a5fa, #38bdf8, #a855f7); -webkit-background-clip: text; -webkit-text-fill-color: transparent; letter-spacing: -0.5px; }
    .badges { display: flex; align-items: center; gap: 6px; flex-wrap: wrap; }
    .badge { padding: 4px 8px; border-radius: 6px; font-size: 0.72rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.5px; display: inline-flex; align-items: center; gap: 4px; }
    .badge-run { background: #064e3b; color: #34d399; border: 1px solid #059669; }
    .badge-hold { background: #7f1d1d; color: #f87171; border: 1px solid #dc2626; }
    .badge-ws { background: #1e3a8a; color: #93c5fd; border: 1px solid #2563eb; }
    .badge-poll { background: #1e293b; color: #94a3b8; border: 1px solid #334155; }
    .badge-ip { background: #312e81; color: #c7d2fe; border: 1px solid #4f46e5; }
    .badge-wifi { background: #0f2e24; color: #6ee7b7; border: 1px solid #10b981; }
    .btn-bar { display: flex; gap: 6px; flex-wrap: wrap; }
    button { background: #1e293b; color: #e2e8f0; border: 1px solid var(--border); border-radius: 6px; padding: 7px 12px; font-size: 0.8rem; font-weight: 600; cursor: pointer; transition: all 0.15s ease; display: inline-flex; align-items: center; gap: 5px; }
    button:hover { background: #334155; border-color: #475569; }
    button:active { transform: scale(0.98); }
    button.btn-primary { background: #2563eb; border-color: #3b82f6; color: #fff; }
    button.btn-primary:hover { background: #1d4ed8; }
    button.btn-danger { background: #dc2626; border-color: #ef4444; color: #fff; }
    button.btn-active { background: #059669; border-color: #10b981; color: #fff; }
    button.btn-outline { background: #0f172a; border-color: #334155; color: #94a3b8; }
    button.btn-outline:hover { background: #1e293b; color: #e2e8f0; }
    .canvas-card { background: var(--card-bg); border: 1px solid var(--border); border-radius: 10px; padding: 10px; margin-bottom: 12px; position: relative; box-shadow: 0 4px 20px rgba(0,0,0,0.5); }
    canvas { width: 100%; aspect-ratio: 320/120; max-height: 400px; display: block; border-radius: 6px; background: #070c14; touch-action: none; cursor: crosshair; }
    .fft-canvas { aspect-ratio: 320/100; max-height: 320px; margin-top: 8px; border: 1px dashed #1e293b; }
    .cursor-panel { background: #0d131f; border: 1px solid #1f293d; border-radius: 6px; padding: 6px 12px; font-size: 0.78rem; margin-top: 8px; display: flex; justify-content: space-around; flex-wrap: wrap; gap: 8px; }
    .cur-item { display: flex; align-items: center; gap: 4px; }
    .cur-val { font-weight: 700; }
    .val-t { color: #60a5fa; }
    .val-v { color: #c084fc; }
    .val-f { color: #34d399; }
    .telemetry-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 8px; margin-bottom: 12px; }
    .card { background: var(--card-bg); border: 1px solid var(--border); border-radius: 8px; padding: 8px 10px; text-align: center; }
    .card-label { font-size: 0.68rem; color: var(--text-muted); text-transform: uppercase; font-weight: 600; letter-spacing: 0.5px; margin-bottom: 2px; }
    .card-val { font-size: 1.15rem; font-weight: 800; font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }
    .ch1-text { color: var(--ch1); text-shadow: 0 0 10px rgba(255, 230, 0, 0.3); }
    .ch2-text { color: var(--ch2); text-shadow: 0 0 10px rgba(0, 229, 255, 0.3); }
    .d1-text { color: var(--d1); }
    .trig-text { color: var(--trig); }
    .ctrl-section { margin-bottom: 12px; }
    .ctrl-title { font-size: 0.75rem; font-weight: 700; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 6px; }
    .ctrl-grid { display: flex; flex-wrap: wrap; gap: 6px; }
    .modal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.75); backdrop-filter: blur(4px); z-index: 1000; align-items: center; justify-content: center; }
    .modal-content { background: var(--card-bg); border: 1px solid var(--border); border-radius: 12px; max-width: 480px; width: 90%; padding: 20px; box-shadow: 0 10px 40px rgba(0,0,0,0.8); }
    .modal-header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); padding-bottom: 10px; margin-bottom: 12px; }
    .modal-title { font-size: 1.1rem; font-weight: 700; color: #60a5fa; }
    .stat-row { display: flex; justify-content: space-between; padding: 6px 0; border-bottom: 1px solid #1a2234; font-size: 0.85rem; }
    .stat-lbl { color: var(--text-muted); }
    .stat-val { font-weight: 700; font-family: monospace; }
    .footer { font-size: 0.72rem; color: #475569; text-align: center; margin-top: 16px; border-top: 1px solid var(--border); padding-top: 10px; }
  </style>
</head>
<body>
  <div class="header">
    <div class="brand">
      <div class="logo">⚡ PocketDSO</div>
      <div class="badges">
        <div id="badgeRun" class="badge badge-run">▶ RUN</div>
        <div id="badgeWs" class="badge badge-poll">HTTP POLL</div>
        <div id="badgeIp" class="badge badge-ip" onclick="openNetModal()" style="cursor:pointer;" title="Click for network diagnostics">IP: --</div>
        <div id="badgeWifi" class="badge badge-wifi">📶 -- dBm</div>
        <div id="badgeBat" class="badge" style="background:#1e293b;border:1px solid #334155;">🔋 --</div>
      </div>
    </div>
    <div class="btn-bar">
      <button onclick="toggleRun()" id="btnRunMain" class="btn-danger">⏸ HOLD</button>
      <button onclick="toggleAuto()" id="btnAutoMain" class="btn-primary">⚡ AUTO: ON</button>
      <button onclick="toggleCursors()" id="btnCursor">📏 Cursors: OFF</button>
      <button onclick="toggleFft()" id="btnFft">📊 FFT: OFF</button>
      <button onclick="captureScreen()">📷 Capture</button>
      <button onclick="exportCsv()">📥 CSV</button>
      <button onclick="openNetModal()">⚙ Wi-Fi</button>
    </div>
  </div>

  <div class="canvas-card">
    <canvas id="scopeCanvas" width="640" height="240"></canvas>
    <div id="cursorPanel" class="cursor-panel" style="display:none;">
      <div class="cur-item">&Delta;T: <span id="curDt" class="cur-val val-t">--</span></div>
      <div class="cur-item">Freq: <span id="curDf" class="cur-val val-f">--</span></div>
      <div class="cur-item">&Delta;V: <span id="curDv" class="cur-val val-v">--</span></div>
      <div class="cur-item" style="color:#64748b;">(Drag X1/X2 for Time, Y1/Y2 for Volts, 'T' marker on left edge for Trigger Level)</div>
    </div>
    <canvas id="fftCanvas" class="fft-canvas" width="640" height="200" style="display:none;"></canvas>
    <div id="fftPanel" class="cursor-panel" style="display:none;">
      <div class="cur-item"><button onclick="nextFftZoom()" title="AUTO fits the detected peaks; Nx zooms around the strongest peak">🔍 X Zoom: <span id="lblFftZoom" class="cur-val val-f">AUTO</span></button></div>
      <div class="cur-item">Peak threshold:
        <input id="fftThr" type="range" min="1" max="100" value="10" oninput="document.getElementById('fftThrVal').textContent=this.value+'%'" onchange="setParam('fftThr', this.value)">
        <span id="fftThrVal" class="cur-val val-f">10%</span> of strongest (top 10)
      </div>
      <div id="fftPeakList" class="cur-item" style="font-family:monospace;color:#e9d5ff;">--</div>
    </div>
  </div>

  <div class="telemetry-grid">
    <div class="card"><div class="card-label">CH1 DC Mean</div><div id="vDc" class="card-val ch1-text">-- V</div></div>
    <div class="card"><div class="card-label">CH1 AC RMS</div><div id="vAc" class="card-val ch1-text">-- V</div></div>
    <div class="card"><div class="card-label">CH1 Peak-to-Peak</div><div id="vPp" class="card-val ch1-text">-- V</div></div>
    <div class="card"><div class="card-label">CH1 Frequency</div><div id="vFreq" class="card-val" style="color:#34d399;">-- Hz</div></div>
    <div class="card"><div class="card-label">Timebase</div><div id="tDiv" class="card-val">--</div></div>
    <div class="card"><div class="card-label">Volts / Div</div><div id="vDiv" class="card-val">--</div></div>
    <div class="card"><div class="card-label">Trigger</div><div id="vTrig" class="card-val trig-text">-- V</div></div>
    <div class="card"><div class="card-label">Sample Rate</div><div id="vFs" class="card-val" style="color:#93c5fd;">-- kS/s</div></div>
  </div>

  <div class="ctrl-section">
    <div class="ctrl-title">Horizontal & Vertical Controls</div>
    <div class="ctrl-grid">
      <button onclick="stepParam('timeBaseIdx', -1, 11)">◀ T/div</button>
      <button onclick="stepParam('timeBaseIdx', 1, 11)">T/div ▶</button>
      <button onclick="stepParam('vDivIdx', -1, 7)">◀ V/div</button>
      <button onclick="stepParam('vDivIdx', 1, 7)">V/div ▶</button>
      <button onclick="toggleCoupling()">Coupling: <span id="lblCoupling">DC+AC</span></button>
      <button onclick="stepParam('ch2Mode', 1, 3)">CH2: <span id="lblCh2" class="ch2-text">Analog</span></button>
      <button onclick="stepParam('probeIdx', 1, 4)">Probe A: <span id="lblProbeA">2x (6.6V)</span></button>
      <button onclick="stepParam('probeBIdx', 1, 4)">Probe B: <span id="lblProbeB">4x (13.2V)</span></button>
    </div>
  </div>

  <div class="ctrl-section">
    <div class="ctrl-title">Trigger & Generator</div>
    <div class="ctrl-grid">
      <button onclick="stepParam('trigMode', 1, 4)">Edge: <span id="lblTrig">Rising ^</span></button>
      <button onclick="setParam('trigAuto', 1)">🎯 Auto Trig Level</button>
      <button onclick="setParam('single', 1)">⚡ Single Shot Arm</button>
      <button onclick="stepParam('calFreqIdx', 1, 4)">Cal Gen: <span id="lblCal">1 kHz</span></button>
      <button onclick="toggleSource()">Source: <span id="lblSrc">Probe Pins</span></button>
    </div>
  </div>

  <div id="netModal" class="modal">
    <div class="modal-content">
      <div class="modal-header">
        <div class="modal-title">📶 Wi-Fi & Network Diagnostics</div>
        <button onclick="closeNetModal()">✕</button>
      </div>
      <div class="stat-row"><span class="stat-lbl">Connected IP:</span><span id="netIp" class="stat-val" style="color:#60a5fa;">--</span></div>
      <div class="stat-row"><span class="stat-lbl">Allocation Mode:</span><span id="netMode" class="stat-val">DHCP (Router assigned)</span></div>
      <div class="stat-row"><span class="stat-lbl">Network SSID:</span><span id="netSsid" class="stat-val">--</span></div>
      <div class="stat-row"><span class="stat-lbl">Signal Strength:</span><span id="netRssi" class="stat-val">-- dBm</span></div>
      <div class="stat-row"><span class="stat-lbl">Gateway / Subnet:</span><span id="netGw" class="stat-val">--</span></div>
      <div class="stat-row"><span class="stat-lbl">MAC Address:</span><span id="netMac" class="stat-val">--</span></div>
      <div class="stat-row"><span class="stat-lbl">Active WebSocket Clients:</span><span id="netClients" class="stat-val">--</span></div>
      <div class="stat-row"><span class="stat-lbl">HTTP Requests Handled:</span><span id="netHttp" class="stat-val">--</span></div>
      <div style="margin-top:14px;display:flex;justify-content:flex-end;gap:8px;">
        <button class="btn-primary" onclick="setParam('reconnect', 1)">🔄 Re-probe & Reconnect</button>
        <button onclick="closeNetModal()">Close</button>
      </div>
    </div>
  </div>

  <div class="footer">
    PocketDSO v2.0 &bull; Waveshare ESP32-C6 262K Color IPS Touchscreen MSO &bull; DHCP Leased
  </div>

  <script>
    const TIME_BASES_SEC = [20e-6, 50e-6, 100e-6, 200e-6, 500e-6, 1e-3, 2e-3, 5e-3, 10e-3, 20e-3, 50e-3];
    const TIME_BASE_LABELS = ["20us", "50us", "100us", "200us", "500us", "1ms", "2ms", "5ms", "10ms", "20ms", "50ms"];
    const V_DIVS_VAL = [0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0];
    const V_DIV_LABELS = ["0.1V", "0.2V", "0.5V", "1V", "2V", "5V", "10V"];
    const TRIG_NAMES = ["Rising ^", "Falling v", "Free Run", "Single !"];
    const CH2_LABELS = ["OFF", "ANALOG", "DIGITAL (D1)"];
    const PROBE_LABELS = ["1x (3.3V)", "2x (6.6V)", "4x (13.2V)", "10x (33V)"];
    const CAL_LABELS = ["OFF", "1 kHz", "10 kHz", "50 kHz"];

    let g_state = {
      running: 1, autoT: 1, autoV: 1, timeBaseIdx: 3, vDivIdx: 2,
      ch2Mode: 1, probeIdx: 1, probeBIdx: 2, trigMode: 0, calFreqIdx: 1, acCoupled: 0, selfTest: 0,
      trigVolts: 1.65, dc: 0, ac: 0, vpp: 0, freq: 0, fs: 200000,
      batVolts: 0, batPct: 100, ip: "--", ssid: "--", rssi: -60, bars: 4
    };
    let g_lastWave = null;
    let g_cursorsOn = false, g_fftOn = false;
    let curX1 = 0.25, curX2 = 0.75, curY1 = 0.30, curY2 = 0.70;
    let dragging = null;

    const canvas = document.getElementById('scopeCanvas');
    const ctx = canvas.getContext('2d');
    const fftCanvas = document.getElementById('fftCanvas');
    const fftCtx = fftCanvas.getContext('2d');

    function toggleRun() {
      const next = g_state.running ? 0 : 1;
      g_state.running = next;
      updateUi(g_state);
      setParam('running', next);
    }
    function toggleAuto() {
      const isAuto = !(g_state.autoT || g_state.autoV);
      g_state.autoT = isAuto ? 1 : 0;
      g_state.autoV = isAuto ? 1 : 0;
      updateUi(g_state);
      setParam('autoset', isAuto ? 1 : 0);
    }
    function toggleCoupling() {
      const next = g_state.acCoupled ? 0 : 1;
      g_state.acCoupled = next;
      updateUi(g_state);
      setParam('acCoupled', next);
    }
    function toggleSource() {
      const next = g_state.selfTest ? 0 : 1;
      g_state.selfTest = next;
      updateUi(g_state);
      setParam('selfTest', next);
    }
    function toggleCursors() {
      g_cursorsOn = !g_cursorsOn;
      document.getElementById('btnCursor').textContent = g_cursorsOn ? '📏 Cursors: ON' : '📏 Cursors: OFF';
      document.getElementById('btnCursor').className = g_cursorsOn ? 'btn-active' : '';
      document.getElementById('cursorPanel').style.display = g_cursorsOn ? 'flex' : 'none';
      if (g_lastWave) renderScope(g_lastWave);
    }
    function toggleFft() {
      g_fftOn = !g_fftOn;
      document.getElementById('btnFft').textContent = g_fftOn ? '📊 FFT: ON' : '📊 FFT: OFF';
      document.getElementById('btnFft').className = g_fftOn ? 'btn-active' : '';
      fftCanvas.style.display = g_fftOn ? 'block' : 'none';
      document.getElementById('fftPanel').style.display = g_fftOn ? 'flex' : 'none';
      if (g_fftOn) { renderFft(); pollFft(); }
    }
    function openNetModal() { document.getElementById('netModal').style.display = 'flex'; }
    function closeNetModal() { document.getElementById('netModal').style.display = 'none'; }

    function updateCursorReadouts() {
      if (!g_cursorsOn) return;
      const totalSec = (TIME_BASES_SEC[g_state.timeBaseIdx] || 200e-6) * 10.0;
      const dtSec = Math.abs(curX2 - curX1) * totalSec;
      const dtUs = dtSec * 1e6;
      const dfHz = (dtSec > 1e-9) ? (1.0 / dtSec) : 0;
      const fullScaleV = (V_DIVS_VAL[g_state.vDivIdx] || 0.5) * 6.0;
      const dv = Math.abs(curY2 - curY1) * fullScaleV;

      document.getElementById('curDt').textContent = (dtUs >= 1000) ? (dtUs/1000).toFixed(2) + ' ms' : dtUs.toFixed(1) + ' us';
      document.getElementById('curDf').textContent = (dfHz >= 1000) ? (dfHz/1000).toFixed(2) + ' kHz' : dfHz.toFixed(1) + ' Hz';
      document.getElementById('curDv').textContent = (dv < 1.0) ? (dv*1000).toFixed(0) + ' mV' : dv.toFixed(2) + ' V';
    }

    function renderScope(data) {
      g_lastWave = data;
      const w = canvas.width, h = canvas.height;

      // Dark background with gradient
      const bgGrad = ctx.createLinearGradient(0, 0, 0, h);
      bgGrad.addColorStop(0, '#060a12');
      bgGrad.addColorStop(1, '#0b121e');
      ctx.fillStyle = bgGrad;
      ctx.fillRect(0, 0, w, h);

      // Grid: 10 horizontal divisions x 6 vertical divisions
      ctx.lineWidth = 1;
      for (let c = 1; c < 10; ++c) {
        const x = (c * w) / 10;
        ctx.strokeStyle = (c === 5) ? '#28384f' : '#141e2e';
        ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
        ctx.strokeStyle = '#3d5272';
        for (let t = 1; t < 5; ++t) {
          const ty = (h / 2) + (t - 2.5) * 4;
          ctx.beginPath(); ctx.moveTo(x - 2, ty); ctx.lineTo(x + 2, ty); ctx.stroke();
        }
      }
      for (let r = 1; r < 6; ++r) {
        const y = (r * h) / 6;
        ctx.strokeStyle = (r === 3) ? '#28384f' : '#141e2e';
        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
        ctx.strokeStyle = '#3d5272';
        for (let t = 1; t < 5; ++t) {
          const tx = (w / 2) + (t - 2.5) * 4;
          ctx.beginPath(); ctx.moveTo(tx, y - 2); ctx.lineTo(tx, y + 2); ctx.stroke();
        }
      }

      // Trigger line (magenta with dashed trace)
      let trigPy = (data.trigY !== undefined) ? (data.trigY / 110.0) * h : h / 2;
      ctx.strokeStyle = 'rgba(255, 42, 133, 0.45)';
      ctx.setLineDash([6, 6]);
      ctx.beginPath(); ctx.moveTo(0, trigPy); ctx.lineTo(w, trigPy); ctx.stroke();
      ctx.setLineDash([]);

      // 'T' marker on left edge
      ctx.fillStyle = '#ff2a85';
      ctx.beginPath();
      ctx.moveTo(0, trigPy - 7); ctx.lineTo(10, trigPy); ctx.lineTo(0, trigPy + 7);
      ctx.fill();
      ctx.fillStyle = '#fff';
      ctx.font = 'bold 9px monospace';
      ctx.fillText('T', 2, trigPy + 3);

      // CH2 Trace (Analog Cyan or Digital D1 Mint Green)
      if (data.ch2 && data.ch2.length > 1) {
        if (data.ch2Mode === 2) { // Digital D1
          const y0 = h - 14, y1 = h - 42;
          ctx.strokeStyle = '#00ff9d';
          ctx.lineWidth = 2;
          ctx.shadowColor = '#00ff9d';
          ctx.shadowBlur = 6;
          ctx.beginPath();
          const step = w / (data.ch2.length - 1);
          let prevBit = data.ch2[0] ? 1 : 0;
          let prevY = prevBit ? y1 : y0;
          ctx.moveTo(0, prevY);
          for (let i = 1; i < data.ch2.length; ++i) {
            const curBit = data.ch2[i] ? 1 : 0;
            const curY = curBit ? y1 : y0;
            const x = i * step;
            if (curY !== prevY) { ctx.lineTo(x, prevY); ctx.lineTo(x, curY); }
            else { ctx.lineTo(x, curY); }
            prevY = curY;
          }
          ctx.stroke();
          ctx.shadowBlur = 0;
          ctx.fillStyle = '#121826'; ctx.fillRect(4, y1 - 12, 24, 14);
          ctx.strokeStyle = '#00ff9d'; ctx.strokeRect(4, y1 - 12, 24, 14);
          ctx.fillStyle = '#00ff9d'; ctx.fillText('D1', 8, y1 - 2);
        } else if (data.ch2Mode === 1) { // Analog Cyan
          ctx.strokeStyle = '#00e5ff';
          ctx.lineWidth = 2;
          ctx.shadowColor = '#00e5ff';
          ctx.shadowBlur = 6;
          ctx.beginPath();
          const step = w / (data.ch2.length - 1);
          for (let i = 0; i < data.ch2.length; ++i) {
            const y = (data.ch2[i] / 110.0) * h;
            if (i === 0) ctx.moveTo(0, y); else ctx.lineTo(i * step, y);
          }
          ctx.stroke();
          ctx.shadowBlur = 0;
        }
      }

      // CH1 Neon Yellow Trace (with phosphor glow!)
      if (data.ch1 && data.ch1.length > 1) {
        ctx.strokeStyle = '#ffe600';
        ctx.lineWidth = 2.5;
        ctx.shadowColor = 'rgba(255, 230, 0, 0.8)';
        ctx.shadowBlur = 8;
        ctx.beginPath();
        const step = w / (data.ch1.length - 1);
        for (let i = 0; i < data.ch1.length; ++i) {
          const y = (data.ch1[i] / 110.0) * h;
          if (i === 0) ctx.moveTo(0, y); else ctx.lineTo(i * step, y);
        }
        ctx.stroke();
        ctx.shadowBlur = 0;
      }

      // Measurement Cursors
      if (g_cursorsOn) {
        const x1 = curX1 * w, x2 = curX2 * w;
        const y1 = curY1 * h, y2 = curY2 * h;

        // X1 (Blue)
        ctx.strokeStyle = '#3b82f6'; ctx.lineWidth = 1.5; ctx.setLineDash([4, 4]);
        ctx.beginPath(); ctx.moveTo(x1, 0); ctx.lineTo(x1, h); ctx.stroke();
        ctx.fillStyle = '#2563eb'; ctx.fillRect(x1 - 10, 2, 20, 13);
        ctx.fillStyle = '#fff'; ctx.font = '9px monospace'; ctx.fillText('X1', x1 - 6, 12);

        // X2 (Orange)
        ctx.strokeStyle = '#f59e0b';
        ctx.beginPath(); ctx.moveTo(x2, 0); ctx.lineTo(x2, h); ctx.stroke();
        ctx.fillStyle = '#d97706'; ctx.fillRect(x2 - 10, 2, 20, 13);
        ctx.fillStyle = '#000'; ctx.fillText('X2', x2 - 6, 12);

        // Y1 (Green)
        ctx.strokeStyle = '#10b981';
        ctx.beginPath(); ctx.moveTo(0, y1); ctx.lineTo(w, y1); ctx.stroke();
        ctx.fillStyle = '#059669'; ctx.fillRect(2, y1 - 7, 20, 13);
        ctx.fillStyle = '#fff'; ctx.fillText('Y1', 5, y1 + 3);

        // Y2 (Purple)
        ctx.strokeStyle = '#a855f7';
        ctx.beginPath(); ctx.moveTo(0, y2); ctx.lineTo(w, y2); ctx.stroke();
        ctx.fillStyle = '#7e22ce'; ctx.fillRect(2, y2 - 7, 20, 13);
        ctx.fillStyle = '#fff'; ctx.fillText('Y2', 5, y2 + 3);

        ctx.setLineDash([]);
        updateCursorReadouts();
      }

      if (g_fftOn) renderFft();
    }

    // Spectrum comes from the device (/api/fft): same 1024-pt FFT + peak as the LCD.
    let g_fftData = null, g_busyFft = false;
    function pollFft() {
      if (!g_fftOn || g_busyFft) return;
      g_busyFft = true;
      fetch('/api/fft').then(r => r.json()).then(d => { g_fftData = d; renderFft(); })
        .catch(() => {}).finally(() => { g_busyFft = false; });
    }

    const FFT_ZOOM_LABELS = ["AUTO", "FULL", "2x", "4x", "8x"];
    function nextFftZoom() {
      const cur = (g_fftData && g_fftData.zoom !== undefined) ? g_fftData.zoom : 0;
      const next = (cur + 1) % FFT_ZOOM_LABELS.length;
      if (g_fftData) g_fftData.zoom = next;
      document.getElementById('lblFftZoom').textContent = FFT_ZOOM_LABELS[next];
      setParam('fftZoom', next);
      setTimeout(pollFft, 120);
    }

    function renderFft() {
      if (!fftCanvas) return;
      const w = fftCanvas.width, h = fftCanvas.height;
      const axisH = 16;
      const base = h - axisH;
      fftCtx.fillStyle = '#070c14';
      fftCtx.fillRect(0, 0, w, h);
      fftCtx.fillStyle = '#0d131f';
      fftCtx.fillRect(0, base, w, axisH);

      fftCtx.strokeStyle = '#1e293b';
      for (let f = 1; f < 5; ++f) {
        fftCtx.beginPath(); fftCtx.moveTo((f * w) / 5, 0); fftCtx.lineTo((f * w) / 5, base); fftCtx.stroke();
      }
      const d = g_fftData;
      if (!d || !d.bins || !d.bins.length) return;

      const zl = document.getElementById('lblFftZoom');
      if (zl && d.zoom !== undefined) zl.textContent = FFT_ZOOM_LABELS[d.zoom] || 'AUTO';

      // Bins are already autoscaled 0..1000 to the largest bin, covering [f0, f1)
      const n = d.bins.length;
      const barW = w / n;
      const top = 22;
      const plotH = base - top;
      const gradient = fftCtx.createLinearGradient(0, top, 0, base);
      gradient.addColorStop(0, '#c084fc');
      gradient.addColorStop(0.5, '#9333ea');
      gradient.addColorStop(1, '#3b0764');
      fftCtx.fillStyle = gradient;
      const gap = barW >= 4 ? 1 : 0.5;
      for (let i = 0; i < n; ++i) {
        const barH = Math.max(1, (d.bins[i] / 1000) * plotH);
        fftCtx.fillRect(i * barW, base - barH, Math.max(1, barW - gap), barH);
      }

      const fSig = d.peakFreq || 0;
      const fmtF = f => (f >= 1000) ? (f / 1000).toFixed(2) + ' kHz' : f.toFixed(0) + ' Hz';
      const pkStr = fmtF(fSig);
      const peaks = d.peaks || [];
      const f0 = d.f0 || 0;
      const f1 = d.f1 || ((d.fs || g_state.fs || 0) / 2);

      // Frequency axis
      fftCtx.font = '10px monospace';
      fftCtx.fillStyle = '#8e9bb0';
      const step = (f1 - f0) / 5;
      const fmtAxis = f => (f < 1000) ? f.toFixed(0)
        : (step < 100 ? (f / 1000).toFixed(2) : (f < 10000 || step < 1000 ? (f / 1000).toFixed(1) : (f / 1000).toFixed(0))) + 'k';
      for (let k = 0; k <= 5; ++k) {
        const txt = fmtAxis(f0 + step * k) + (k === 5 ? 'Hz' : '');
        const tw = fftCtx.measureText(txt).width;
        const x = (k * w) / 5;
        const lx = k === 0 ? 3 : (k === 5 ? w - tw - 3 : x - tw / 2);
        fftCtx.fillText(txt, lx, h - 4);
      }

      // Threshold line
      if (peaks.length && d.thr) {
        const strongest = Math.max(...peaks.map(p => p.m));
        const thrY = base - (strongest / 1000) * (d.thr / 100) * plotH;
        fftCtx.strokeStyle = 'rgba(255, 42, 133, 0.6)';
        fftCtx.setLineDash([5, 5]);
        fftCtx.beginPath(); fftCtx.moveTo(0, thrY); fftCtx.lineTo(w, thrY); fftCtx.stroke();
        fftCtx.setLineDash([]);
      }

      // Label each peak (strongest first), skipping overlapping positions
      fftCtx.font = 'bold 10px monospace';
      const boxes = [];
      peaks.forEach((p, k) => {
        const px = (p.i + 0.5) * barW;
        const barTop = base - Math.max(1, (p.m / 1000) * plotH);
        const col = k === 0 ? '#ffe600' : '#e9d5ff';
        fftCtx.fillStyle = col;
        fftCtx.beginPath();
        fftCtx.moveTo(px, barTop - 1); fftCtx.lineTo(px - 4, barTop - 7); fftCtx.lineTo(px + 4, barTop - 7);
        fftCtx.fill();

        const txt = fmtF(p.f);
        const tw = fftCtx.measureText(txt).width;
        const lx = Math.min(w - tw - 2, Math.max(2, px - tw / 2));
        let ly = Math.max(top + 10, barTop - 10);
        for (let tries = 0; tries < 12; ++tries) {
          const hit = boxes.some(b => lx < b.x1 + 3 && lx + tw + 3 > b.x0 && ly - 10 < b.y1 && ly > b.y0);
          if (!hit) break;
          ly += 12;
        }
        if (ly > base - 2) return;
        boxes.push({ x0: lx, x1: lx + tw, y0: ly - 10, y1: ly });
        fftCtx.fillStyle = 'rgba(7, 12, 20, 0.8)';
        fftCtx.fillRect(lx - 2, ly - 10, tw + 4, 12);
        fftCtx.fillStyle = col;
        fftCtx.fillText(txt, lx, ly);
      });

      const list = document.getElementById('fftPeakList');
      if (list) {
        list.textContent = peaks.length
          ? peaks.map((p, k) => `#${k + 1} ${fmtF(p.f)} (${(p.m / 10).toFixed(0)}%)`).join('  ')
          : 'No peaks above threshold';
      }
      const thrInput = document.getElementById('fftThr');
      if (thrInput && d.thr && document.activeElement !== thrInput) {
        thrInput.value = d.thr;
        document.getElementById('fftThrVal').textContent = d.thr + '%';
      }
      const fs = d.fs || g_state.fs || 0;
      fftCtx.fillStyle = '#e9d5ff';
      fftCtx.font = '10px monospace';
      const zoomLbl = FFT_ZOOM_LABELS[d.zoom] || 'AUTO';
      fftCtx.fillText(`FFT 1024-pt | Peak: ${pkStr} | View: ${fmtF(f0)} - ${fmtF(f1)} (${zoomLbl}) | Span: ${(fs / 2000).toFixed(1)} kHz | ${d.binHz.toFixed(1)} Hz/bar`, 10, 13);
    }

    function updateUi(d) {
      Object.assign(g_state, d);
      document.getElementById('vDc').textContent = (d.dc !== undefined ? d.dc.toFixed(2) : '--') + ' V';
      document.getElementById('vAc').textContent = (d.ac !== undefined ? d.ac.toFixed(2) : '--') + ' V';
      document.getElementById('vPp').textContent = (d.vpp !== undefined ? d.vpp.toFixed(2) : '--') + ' V';
      document.getElementById('vFreq').textContent = (d.freq >= 1000) ? (d.freq/1000).toFixed(2)+' kHz' : (d.freq !== undefined ? d.freq.toFixed(1)+' Hz' : '--');
      document.getElementById('tDiv').textContent = (d.tDiv || TIME_BASE_LABELS[d.timeBaseIdx] || '--') + (d.autoT ? ' (AUTO)' : '');
      document.getElementById('vDiv').textContent = (d.vDiv || V_DIV_LABELS[d.vDivIdx] || '--') + (d.autoV ? ' (AUTO)' : '');
      document.getElementById('vTrig').textContent = (d.trigVolts !== undefined ? d.trigVolts.toFixed(2) : '--') + ' V';
      document.getElementById('vFs').textContent = ((d.fs || 200000)/1000).toFixed(0) + ' kS/s';

      document.getElementById('lblCoupling').textContent = d.acCoupled ? 'AC Only' : 'DC+AC';
      document.getElementById('lblCh2').textContent = CH2_LABELS[d.ch2Mode] || 'OFF';
      const elProbeA = document.getElementById('lblProbeA');
      if (elProbeA) elProbeA.textContent = PROBE_LABELS[d.probeIdx] || '2x';
      const elProbeB = document.getElementById('lblProbeB');
      if (elProbeB) elProbeB.textContent = PROBE_LABELS[d.probeBIdx] || '4x';
      document.getElementById('lblTrig').textContent = TRIG_NAMES[d.trigMode] || 'Rising ^';
      document.getElementById('lblCal').textContent = CAL_LABELS[d.calFreqIdx] || 'OFF';
      document.getElementById('lblSrc').textContent = d.selfTest ? 'Loopback (0W)' : 'Probe Pins';

      const runBadge = document.getElementById('badgeRun');
      const btnRun = document.getElementById('btnRunMain');
      if (d.running) {
        runBadge.className = 'badge badge-run'; runBadge.textContent = '▶ RUN';
        btnRun.className = 'btn-danger'; btnRun.textContent = '⏸ HOLD';
      } else {
        runBadge.className = 'badge badge-hold'; runBadge.textContent = '⏸ HOLD';
        btnRun.className = 'btn-primary'; btnRun.textContent = '▶ RUN';
      }

      const isAuto = !!(d.autoT || d.autoV);
      const btnAuto = document.getElementById('btnAutoMain');
      if (btnAuto) {
        btnAuto.className = isAuto ? 'btn-primary' : 'btn-outline';
        btnAuto.textContent = isAuto ? '⚡ AUTO: ON' : '⚡ AUTO: OFF';
      }

      if (d.ip) {
        document.getElementById('badgeIp').textContent = 'IP: ' + d.ip;
        document.getElementById('netIp').textContent = d.ip;
      }
      if (d.rssi !== undefined) {
        document.getElementById('badgeWifi').textContent = '📶 ' + d.rssi + ' dBm';
        document.getElementById('netRssi').textContent = d.rssi + ' dBm (' + (d.bars || 4) + ' bars)';
      }
      if (d.batVolts !== undefined) {
        const batStr = (d.batVolts < 2.5) ? '⚡ USB (5V)' : `🔋 ${d.batPct}% (${d.batVolts.toFixed(2)}V)`;
        document.getElementById('badgeBat').textContent = batStr;
      }
      if (d.ssid) document.getElementById('netSsid').textContent = d.ssid;
      if (d.gw) document.getElementById('netGw').textContent = d.gw;
      if (d.mac) document.getElementById('netMac').textContent = d.mac;
      if (d.clients !== undefined) document.getElementById('netClients').textContent = d.clients;
      if (d.httpReqs !== undefined) document.getElementById('netHttp').textContent = d.httpReqs;

      updateCursorReadouts();
    }

    let ws = null;
    function connectWs() {
      document.getElementById('badgeWs').textContent = 'HTTP LIVE';
      document.getElementById('badgeWs').className = 'badge badge-ws';
      pollMetrics();
      pollWaveform();
    }

    function fetchWithTimeout(url, ms = 2500) {
      const controller = new AbortController();
      const id = setTimeout(() => controller.abort(), ms);
      return fetch(url, { signal: controller.signal })
        .finally(() => clearTimeout(id));
    }

    function setParam(name, val) {
      fetchWithTimeout(`/api/set?${name}=${encodeURIComponent(val)}`, 2500)
        .then(() => {
          setTimeout(pollMetrics, 40);
          setTimeout(pollWaveform, 80);
        })
        .catch(e => console.warn('setParam error:', e));
    }

    function stepParam(name, dir, count) {
      let cur = (g_state[name] !== undefined) ? g_state[name] : 0;
      let next = (cur + dir + count) % count;
      g_state[name] = next;
      updateUi(g_state);
      setParam(name, next);
    }

    let g_busyMetrics = false, g_busyWave = false;
    function pollMetrics() {
      if (g_busyMetrics) return;
      g_busyMetrics = true;
      fetchWithTimeout('/api/metrics', 2000)
        .then(r => r.json())
        .then(d => { if (d) updateUi(d); })
        .catch(e => {})
        .finally(() => { g_busyMetrics = false; });
    }
    function pollWaveform() {
      if (g_busyWave) return;
      g_busyWave = true;
      fetchWithTimeout('/api/waveform', 2000)
        .then(r => r.json())
        .then(d => { if (d) renderScope(d); })
        .catch(e => {})
        .finally(() => { g_busyWave = false; });
    }

    function exportCsv() {
      if (!g_lastWave || !g_lastWave.ch1 || !g_lastWave.ch1.length) {
        alert("No waveform captured yet."); return;
      }
      const ch1 = g_lastWave.ch1;
      const ch2 = g_lastWave.ch2 || [];
      const totalSec = (TIME_BASES_SEC[g_state.timeBaseIdx] || 200e-6) * 10.0;
      const dtUs = (totalSec / (ch1.length - 1)) * 1e6;
      const fullV = (V_DIVS_VAL[g_state.vDivIdx] || 0.5) * 6.0;

      let csv = "# PocketDSO Waveform Export\r\n";
      csv += `# Timebase: ${TIME_BASE_LABELS[g_state.timeBaseIdx]}, Volts/div: ${V_DIV_LABELS[g_state.vDivIdx]}\r\n`;
      csv += `# DC Mean: ${g_state.dc}V, AC RMS: ${g_state.ac}V, Freq: ${g_state.freq}Hz\r\n`;
      csv += "Index,Time_us,CH1_Volts,CH2_Signal\r\n";
      for (let i = 0; i < ch1.length; ++i) {
        const t = (i * dtUs).toFixed(3);
        const v1 = ((1.0 - (ch1[i] / 110.0)) * fullV).toFixed(3);
        let v2 = "";
        if (ch2.length > i) {
          v2 = (g_state.ch2Mode === 2) ? (ch2[i] ? "1" : "0") : ((1.0 - (ch2[i] / 110.0)) * fullV).toFixed(3);
        }
        csv += `${i},${t},${v1},${v2}\r\n`;
      }
      const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' });
      const link = document.createElement("a");
      link.href = URL.createObjectURL(blob);
      link.download = `pocketdso_${Date.now()}.csv`;
      link.click();
    }

    function captureScreen() {
      const snapCanvas = document.createElement('canvas');
      snapCanvas.width = canvas.width;
      snapCanvas.height = canvas.height + 40;
      const sCtx = snapCanvas.getContext('2d');
      sCtx.fillStyle = '#090d16';
      sCtx.fillRect(0, 0, snapCanvas.width, snapCanvas.height);
      sCtx.drawImage(canvas, 0, 0);

      sCtx.fillStyle = '#fff';
      sCtx.font = 'bold 12px sans-serif';
      sCtx.fillText('PocketDSO (262K Color IPS) — Capture', 12, snapCanvas.height - 18);
      sCtx.font = '11px monospace';
      sCtx.fillStyle = '#38bdf8';
      sCtx.fillText(`T: ${TIME_BASE_LABELS[g_state.timeBaseIdx]} | V: ${V_DIV_LABELS[g_state.vDivIdx]} | Freq: ${g_state.freq}Hz | ${new Date().toLocaleTimeString()}`, 280, snapCanvas.height - 18);

      const link = document.createElement('a');
      link.href = snapCanvas.toDataURL('image/png');
      link.download = `pocketdso_snapshot_${Date.now()}.png`;
      link.click();
    }

    function getPointerPos(evt) {
      const rect = canvas.getBoundingClientRect();
      const cx = evt.touches ? evt.touches[0].clientX : evt.clientX;
      const cy = evt.touches ? evt.touches[0].clientY : evt.clientY;
      return {
        x: Math.max(0, Math.min(1, (cx - rect.left) / rect.width)),
        y: Math.max(0, Math.min(1, (cy - rect.top) / rect.height))
      };
    }

    canvas.addEventListener('mousedown', e => {
      const p = getPointerPos(e);
      if (p.x < 0.08) { dragging = 'trig'; }
      else if (g_cursorsOn) {
        const tol = 0.04;
        if (Math.abs(p.x - curX1) < tol) dragging = 'x1';
        else if (Math.abs(p.x - curX2) < tol) dragging = 'x2';
        else if (Math.abs(p.y - curY1) < tol) dragging = 'y1';
        else if (Math.abs(p.y - curY2) < tol) dragging = 'y2';
      }
    });

    window.addEventListener('mousemove', e => {
      if (!dragging) return;
      const p = getPointerPos(e);
      if (dragging === 'trig') {
        const fullV = (V_DIVS_VAL[g_state.vDivIdx] || 0.5) * 6.0;
        const v = (1.0 - p.y) * fullV;
        setParam('trigVolts', v.toFixed(2));
      } else if (dragging === 'x1') curX1 = p.x;
      else if (dragging === 'x2') curX2 = p.x;
      else if (dragging === 'y1') curY1 = p.y;
      else if (dragging === 'y2') curY2 = p.y;
      if (g_lastWave) renderScope(g_lastWave);
    });

    window.addEventListener('mouseup', () => { dragging = null; });

    window.addEventListener('keydown', e => {
      if (e.target.tagName === 'INPUT') return;
      if (e.code === 'Space') { e.preventDefault(); toggleRun(); }
      else if (e.key === 'a' || e.key === 'A') toggleAuto();
      else if (e.key === 'c' || e.key === 'C') toggleCursors();
      else if (e.key === 'f' || e.key === 'F') toggleFft();
    });

    connectWs();
    setInterval(pollMetrics, 500);
    setInterval(pollWaveform, 150);
    setInterval(pollFft, 300);
  </script>
</body>
</html>
)EOS";
#endif

// =============================================================================
// 1. HARDWARE PIN DEFINITIONS (Waveshare ESP32-C6-Touch-LCD-1.47-M)
// =============================================================================

// --- Display (JD9853 172x320 SPI) ---
constexpr int8_t PIN_LCD_SCK  = 1;
constexpr int8_t PIN_LCD_MOSI = 2;
constexpr int8_t PIN_LCD_MISO = 3;
constexpr int8_t PIN_SD_CS    = 4;   // Deselect TF card on shared SPI bus
constexpr int8_t PIN_LCD_CS   = 14;
constexpr int8_t PIN_LCD_DC   = 15;
constexpr int8_t PIN_LCD_RST  = 22;
constexpr int8_t PIN_LCD_BL   = 23;

// --- Capacitive Touch (AXS5106L I2C @ 0x63) ---
constexpr int8_t  PIN_TOUCH_SDA  = 18;
constexpr int8_t  PIN_TOUCH_SCL  = 19;
constexpr int8_t  PIN_TOUCH_RST  = 20;
constexpr int8_t  PIN_TOUCH_INT  = 21;
constexpr uint8_t AXS5106L_ADDR  = 0x63;

// --- Oscilloscope Probe Inputs & Calibration Generator (22-Pin Header) ---
constexpr int8_t PIN_SCOPE_CH1   = 6;   // ADC1_CH6 (Primary Probe A - DC/AC)
constexpr int8_t PIN_SCOPE_CH2   = 5;   // ADC1_CH5 (Secondary Probe B - DC/AC)
constexpr int8_t PIN_BAT_ADC     = 0;   // ADC1_CH0 (Onboard 3:1 Battery Sense)
constexpr int8_t PIN_CAL_OUT     = 7;   // Built-in PWM Test/Calibration Output

// --- State Variables ---
static dso::Settings      g_settings;
static dso::UiState       g_ui;
static dso::NetworkStats  g_netStats;
static dso::Metrics       g_metricsCh1;
static dso::Metrics       g_metricsCh2;
static dso::Trace         g_trace;

static float   g_ch1Volts[dso::CAPTURE_SAMPLES];
static float   g_ch2Volts[dso::CAPTURE_SAMPLES];
static uint8_t g_ch2Digital[dso::CAPTURE_SAMPLES];
static float   g_actualSampleRateHz = 200000.0f;
static uint32_t g_captureSeq = 0;
static float   g_batVolts = 0.0f;
static uint8_t g_batPct = 100;

Preferences prefs;

// =============================================================================
// 2. DISPLAY DRIVER (JD9853 172x320 SPI + Full-Screen Double-Buffered Canvas)
// =============================================================================

// JD9853 accepts ST7789 commands only after Waveshare's register load.
// Axis-swap (landscape MADCTL) does not address this glass: a 320-wide
// window comes out white with a few live pixels. The canvas is therefore
// the native 172x320 portrait buffer, rotated so the UI is drawn 320x172
// and flushed into the portrait window the panel actually shows.
Arduino_DataBus *bus = new Arduino_HWSPI(
  PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_MOSI, PIN_LCD_MISO
);

Arduino_GFX *panel = new Arduino_ST7789(
  bus, PIN_LCD_RST, 0 /* rotation */, false /* IPS */,
  172 /* width */, 320 /* height */,
  34 /* col_offset1 */, 0 /* row_offset1 */,
  34 /* col_offset2 */, 0 /* row_offset2 */
);

// Use 8-bit indexed 16-bit-color canvas (55 KB instead of 110 KB) so ESP32-C6
// has plenty of contiguous internal DRAM for the Wi-Fi 6 + TCP/IP stack.
Arduino_Canvas_Indexed *gfx = new Arduino_Canvas_Indexed(172, 320, panel, 0, 0, 1);

static void initJD9853Registers() {
  static const uint8_t init_ops[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11, // Sleep Out
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_C8_D16, 0xDF, 0x98, 0x53,
    WRITE_C8_D8,  0xB2, 0x23,

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 4, 0x00, 0x47, 0x00, 0x6F,

    WRITE_COMMAND_8, 0xBB,
    WRITE_BYTES, 6, 0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,

    WRITE_C8_D16, 0xC0, 0x44, 0xA4,
    WRITE_C8_D8,  0xC1, 0x16,

    WRITE_COMMAND_8, 0xC3,
    WRITE_BYTES, 8, 0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,

    WRITE_COMMAND_8, 0xC4,
    WRITE_BYTES, 12, 0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A,
                     0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,

    WRITE_COMMAND_8, 0xC8,
    WRITE_BYTES, 32,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
      0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
      0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,

    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 5, 0x04, 0x06, 0x6B, 0x0F, 0x00,

    WRITE_C8_D16, 0xD7, 0x00, 0x30,
    WRITE_C8_D8,  0xE6, 0x14,
    WRITE_C8_D8,  0xDE, 0x01,

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 5, 0x03, 0x13, 0xEF, 0x35, 0x35,

    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 3, 0x14, 0x15, 0xC0,

    WRITE_C8_D16, 0xC2, 0x06, 0x3A,
    WRITE_C8_D16, 0xC4, 0x72, 0x12,
    WRITE_C8_D8,  0xBE, 0x00,
    WRITE_C8_D8,  0xDE, 0x02,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3, 0x00, 0x02, 0x00,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3, 0x01, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x35, 0x00,
    WRITE_C8_D8, 0x3A, 0x05, // JD9853 16-bit color

    WRITE_COMMAND_8, 0x2A,
    WRITE_BYTES, 4, 0x00, 0x22, 0x00, 0xCD,

    WRITE_COMMAND_8, 0x2B,
    WRITE_BYTES, 4, 0x00, 0x00, 0x01, 0x3F,

    WRITE_C8_D8, 0xDE, 0x02,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3, 0x00, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x36, 0x00,
    WRITE_COMMAND_8, 0x21, // inversion on
    END_WRITE,

    DELAY, 10,
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29, // Display ON
    END_WRITE
  };
  bus->batchOperation(init_ops, sizeof(init_ops));
}

// =============================================================================
// 3. CAPACITIVE TOUCH DRIVER (AXS5106L I2C @ 0x63)
// =============================================================================

static void initTouchAXS5106L() {
  pinMode(PIN_TOUCH_RST, OUTPUT);
  pinMode(PIN_TOUCH_INT, INPUT_PULLUP);

  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(20);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(60);

  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  Wire.setClock(400000);
}

static bool readTouchLandscape(uint16_t &outX, uint16_t &outY) {
  Wire.beginTransmission(AXS5106L_ADDR);
  Wire.write(0x01);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom(AXS5106L_ADDR, (uint8_t)6, (uint8_t)true) < 6) return false;

  uint8_t buf[6];
  for (uint8_t i = 0; i < 6; ++i) buf[i] = Wire.read();

  uint8_t points = buf[1] & 0x0F;
  if (points == 0 || points > 2) return false;

  uint16_t rawX = ((uint16_t)(buf[2] & 0x0F) << 8) | buf[3];
  uint16_t rawY = ((uint16_t)(buf[4] & 0x0F) << 8) | buf[5];
  if (rawX >= 4090 || rawY >= 4090) return false;

  int32_t lx = (int32_t)rawY;
  int32_t ly = (int32_t)rawX;
  outX = (uint16_t)constrain(lx, 0, dso::LCD_W - 1);
  outY = (uint16_t)constrain(ly, 0, dso::LCD_H - 1);
  return true;
}

// =============================================================================
// 4. HARDWARE PERIPHERALS (LEDC Generator, Backlight PWM, ADC)
// =============================================================================

static bool g_calAttached = false;
static bool g_blAttached  = false;

static void updateCalibrationGenerator() {
  uint32_t freq = dso::CAL_FREQS_HZ[g_settings.calFreqIdx];
  if (g_calAttached) {
    ledcDetach(PIN_CAL_OUT);
    g_calAttached = false;
  }
  if (freq == 0) {
    pinMode(PIN_CAL_OUT, INPUT);
  } else {
    if (ledcAttach(PIN_CAL_OUT, freq, 8)) {
      g_calAttached = true;
      ledcWrite(PIN_CAL_OUT, 128); // 50% duty square wave
    }
  }
}

static void updateBacklight() {
  uint8_t pct = dso::BRIGHTNESS_PCT[g_settings.brightnessIdx];
  if (pct >= 100) {
    if (g_blAttached) {
      ledcDetach(PIN_LCD_BL);
      g_blAttached = false;
    }
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);
  } else {
    if (!g_blAttached) {
      g_blAttached = ledcAttach(PIN_LCD_BL, 5000, 8);
    }
    if (g_blAttached) {
      ledcWrite(PIN_LCD_BL, (pct * 255) / 100);
    }
  }
}

static void setupADC() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_SCOPE_CH1, ADC_11db);
  analogSetPinAttenuation(PIN_SCOPE_CH2, ADC_11db);
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
  pinMode(PIN_SCOPE_CH2, INPUT);
}

// =============================================================================
// 5. NVS FLASH SETTINGS PERSISTENCE
// =============================================================================

static void loadSettings() {
  prefs.begin("pocketdso", true);
  uint8_t blob[dso::SETTINGS_BLOB_SIZE];
  if (prefs.getBytes("cfg", blob, sizeof(blob)) == sizeof(blob)) {
    if (dso::deserializeSettings(blob, sizeof(blob), g_settings)) {
      Serial.println("[NVS] Settings restored from flash.");
    }
  }
  prefs.end();
}

static void saveSettings() {
  prefs.begin("pocketdso", false);
  uint8_t blob[dso::SETTINGS_BLOB_SIZE];
  dso::serializeSettings(g_settings, blob);
  prefs.putBytes("cfg", blob, sizeof(blob));
  prefs.end();
  Serial.println("[NVS] Settings saved to flash.");
}

// =============================================================================
// 6. SMART NETWORK MANAGER (Default .100 IP with Collision Fallback)
// =============================================================================

#if ENABLE_WEB_UI
static bool g_webServerStarted = false;

static void startWebServerOnce();

#if WIFI_USE_STATIC_IP
// Broadcasts an ARP Who-Has request on the active Wi-Fi LAN for `target` and
// checks lwIP's ARP table to see if another device (e.g. a phone) owns that IP.
static bool isIpOccupiedOnLan(const IPAddress &target) {
  ip4_addr_t ip4;
  IP4_ADDR(&ip4, target[0], target[1], target[2], target[3]);

  // Trigger ARP resolution on the active Wi-Fi interface + send a UDP probe
  LOCK_TCPIP_CORE();
  if (netif_default != nullptr) {
    etharp_request(netif_default, &ip4);
  }
  UNLOCK_TCPIP_CORE();

  WiFiUDP udp;
  udp.beginPacket(target, 33434);
  uint8_t dummy = 0;
  udp.write(&dummy, 1);
  udp.endPacket();

  // Poll ARP cache for up to 350 ms for a reply from any device owning `target`
  uint32_t t0 = millis();
  while (millis() - t0 < 350) {
    delay(35);
    LOCK_TCPIP_CORE();
    struct eth_addr *eth_ret = nullptr;
    const ip4_addr_t *ip_ret = nullptr;
    ssize_t idx = etharp_find_addr(nullptr, &ip4, &eth_ret, &ip_ret);
    UNLOCK_TCPIP_CORE();
    if (idx >= 0 && eth_ret != nullptr) {
      Serial.printf("[WiFi] ARP reply: %s is OCCUPIED by MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                    target.toString().c_str(),
                    eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
                    eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
      return true;
    }
    // Re-send one ARP request halfway through in case the first broadcast was missed
    if (millis() - t0 >= 140 && millis() - t0 < 180) {
      LOCK_TCPIP_CORE();
      if (netif_default != nullptr) {
        etharp_request(netif_default, &ip4);
      }
      UNLOCK_TCPIP_CORE();
    }
  }
  return false;
}
#endif

static bool connectWifi() {
  if (strcmp(WIFI_SSID, "YOUR_SSID") == 0) {
    Serial.println("[WiFi] Placeholder 'YOUR_SSID' detected -> Standalone offline mode.");
    g_netStats.connected = false;
    return false;
  }

  // Draw an immediate visual status on the LCD so the user sees Wi-Fi probing
  if (g_ui.screen == dso::SCR_SETTINGS && g_ui.tab == dso::TAB_NET) {
    gfx->fillRoundRect(40, 68, 240, 36, 6, dso::colors::BADGE_AUTO);
    gfx->drawRoundRect(40, 68, 240, 36, 6, dso::colors::TEXT_WHITE);
    gfx->setTextColor(dso::colors::TEXT_WHITE);
    gfx->setTextSize(1);
    gfx->setCursor(62, 82);
    gfx->printf("Connecting to %s...", WIFI_SSID);
    gfx->flush();
  }

  WiFi.useStaticBuffers(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("PocketDSO");
  delay(50);

  Serial.printf("[WiFi] Connecting to '%s' via DHCP (free heap: %u)...\n",
                WIFI_SSID, (unsigned)ESP.getFreeHeap());

  // 1. Request IP via DHCP from the router
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0 < WIFI_CONNECT_TIMEOUT_MS)) {
    delay(150);
    Serial.print('.');
  }
  Serial.println();

  g_netStats.isStatic = false;
  g_netStats.fallbackUsed = false;

  #if WIFI_USE_STATIC_IP
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress dhcpIp = WiFi.localIP();
    IPAddress gw     = WiFi.gatewayIP();
    IPAddress mask   = WiFi.subnetMask();
    IPAddress dns    = WiFi.dnsIP(0);
    if (dns == IPAddress(0, 0, 0, 0)) dns = gw;

    uint8_t targetOctet4 = WIFI_STATIC_IP_OCTET4;
    for (uint8_t step = 0; step <= WIFI_FALLBACK_MAX_STEPS; ++step) {
      // Use the router's actual subnet prefix (e.g. 192.168.1.x)
      IPAddress candidate(gw[0], gw[1], gw[2], targetOctet4);

      if (candidate == dhcpIp) {
        // Router already leased us this exact IP!
        g_netStats.isStatic = true;
        g_netStats.fallbackUsed = (step > 0);
        break;
      }

      Serial.printf("[WiFi] ARP-probing %s on LAN to check if free...\n", candidate.toString().c_str());
      if (!isIpOccupiedOnLan(candidate)) {
        Serial.printf("[WiFi] %s is FREE -> binding static IP.\n", candidate.toString().c_str());
        if (WiFi.config(candidate, gw, mask, dns)) {
          g_netStats.isStatic = true;
          g_netStats.fallbackUsed = (step > 0);
        }
        break;
      }

      targetOctet4++;
      if (targetOctet4 >= 254) targetOctet4 = 100;
    }
  }
  #endif

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false);
    g_netStats.connected = true;
    snprintf(g_netStats.ssid, sizeof(g_netStats.ssid), "%s", WiFi.SSID().c_str());
    snprintf(g_netStats.ip, sizeof(g_netStats.ip), "%s", WiFi.localIP().toString().c_str());
    snprintf(g_netStats.gw, sizeof(g_netStats.gw), "%s", WiFi.gatewayIP().toString().c_str());
    snprintf(g_netStats.mask, sizeof(g_netStats.mask), "%s", WiFi.subnetMask().toString().c_str());
    snprintf(g_netStats.mac, sizeof(g_netStats.mac), "%s", WiFi.macAddress().c_str());
    g_netStats.rssi = WiFi.RSSI();
    g_netStats.bars = dso::rssiToBars(g_netStats.rssi);
    g_netStats.quality = dso::rssiToQuality(g_netStats.rssi);

    if (MDNS.begin("pocketdso")) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("[WiFi] mDNS responder active: http://pocketdso.local");
    }

    startWebServerOnce();

    Serial.printf("[WiFi] CONNECTED!\n  IP: %s\n  Mode: %s\n  SSID: %s\n  RSSI: %d dBm (%d bars)\n  URL: http://%s\n  mDNS: http://pocketdso.local\n",
                  g_netStats.ip,
                  g_netStats.isStatic ? (g_netStats.fallbackUsed ? "Static Fallback" : "Static IP") : "DHCP (Router assigned)",
                  g_netStats.ssid, g_netStats.rssi, g_netStats.bars, g_netStats.ip);
    return true;
  } else {
    Serial.println("[WiFi] Connection timed out. Operating in standalone offline mode.");
    g_netStats.connected = false;
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    return false;
  }
}
#endif

// =============================================================================
// 7. ADC ACQUISITION PIPELINE
// =============================================================================

static void captureWaveforms() {
  float totalWindowSec = dso::TIME_BASES_SEC[g_settings.timeBaseIdx] * (float)dso::GRID_COLS;
  float requiredSampleRate = (float)dso::CAPTURE_SAMPLES / (totalWindowSec * 1.25f);
  uint32_t maxRate = dso::SAMPLE_RATES_HZ[g_settings.sampleRateIdx];
  uint32_t targetRate = (uint32_t)min((float)maxRate, max(200.0f, requiredSampleRate));
  if (g_ui.screen == dso::SCR_FFT) {
    targetRate = dso::SAMPLE_RATES_HZ[g_settings.fftSpanIdx];
  }

  const uint32_t stepUs = 1000000UL / targetRate;
  const float    scale1 = (3.3f / 4095.0f) * dso::PROBE_SCALES[g_settings.probeIdx];
  const float    scale2 = (3.3f / 4095.0f) * dso::PROBE_SCALES[g_settings.probeBIdx];

  static uint16_t raw1[dso::CAPTURE_SAMPLES];
  static uint16_t raw2[dso::CAPTURE_SAMPLES];

  uint32_t tStart = micros();
  uint32_t nextDeadline = tStart;

  uint32_t calFreq = dso::CAL_FREQS_HZ[g_settings.calFreqIdx];
  if (calFreq == 0) calFreq = 1000;
  float calPeriodUs = 1000000.0f / (float)calFreq;

  if (g_settings.selfTest) {
    for (size_t i = 0; i < dso::CAPTURE_SAMPLES; ++i) {
      uint32_t elapsed = micros() - tStart;
      float phase = fmodf((float)elapsed, calPeriodUs) / calPeriodUs;
      raw1[i] = (phase < 0.5f) ? 4095 : 0;
      if (g_settings.ch2Mode == dso::CH2_ANALOG) {
        raw2[i] = (phase < 0.5f) ? 2048 : 0;
      } else if (g_settings.ch2Mode == dso::CH2_DIGITAL) {
        g_ch2Digital[i] = (phase < 0.5f) ? 1 : 0;
      }
      nextDeadline += stepUs;
      while ((int32_t)(micros() - nextDeadline) < 0) { }
      if ((int32_t)(micros() - nextDeadline) > (int32_t)stepUs) nextDeadline = micros();
    }
  } else if (g_settings.ch2Mode == dso::CH2_ANALOG) {
    for (size_t i = 0; i < dso::CAPTURE_SAMPLES; ++i) {
      raw1[i] = analogRead(PIN_SCOPE_CH1);
      raw2[i] = analogRead(PIN_SCOPE_CH2);
      nextDeadline += stepUs;
      while ((int32_t)(micros() - nextDeadline) < 0) { }
      if ((int32_t)(micros() - nextDeadline) > (int32_t)stepUs) nextDeadline = micros();
    }
  } else if (g_settings.ch2Mode == dso::CH2_DIGITAL) {
    for (size_t i = 0; i < dso::CAPTURE_SAMPLES; ++i) {
      raw1[i] = analogRead(PIN_SCOPE_CH1);
      g_ch2Digital[i] = (uint8_t)digitalRead(PIN_SCOPE_CH2);
      nextDeadline += stepUs;
      while ((int32_t)(micros() - nextDeadline) < 0) { }
      if ((int32_t)(micros() - nextDeadline) > (int32_t)stepUs) nextDeadline = micros();
    }
  } else {
    for (size_t i = 0; i < dso::CAPTURE_SAMPLES; ++i) {
      raw1[i] = analogRead(PIN_SCOPE_CH1);
      nextDeadline += stepUs;
      while ((int32_t)(micros() - nextDeadline) < 0) { }
      if ((int32_t)(micros() - nextDeadline) > (int32_t)stepUs) nextDeadline = micros();
    }
  }

  uint32_t elapsedUs = max(1UL, micros() - tStart);
  g_actualSampleRateHz = (float)((1000000ULL * dso::CAPTURE_SAMPLES) / elapsedUs);

  for (size_t i = 0; i < dso::CAPTURE_SAMPLES; ++i) g_ch1Volts[i] = raw1[i] * scale1;
  if (g_settings.ch2Mode == dso::CH2_ANALOG) {
    for (size_t i = 0; i < dso::CAPTURE_SAMPLES; ++i) g_ch2Volts[i] = raw2[i] * scale2;
  } else if (g_settings.ch2Mode == dso::CH2_DIGITAL) {
    for (size_t i = 0; i < dso::CAPTURE_SAMPLES; ++i) g_ch2Volts[i] = g_ch2Digital[i] ? 3.3f : 0.0f;
  } else {
    memset(g_ch2Volts, 0, sizeof(g_ch2Volts));
  }
  g_captureSeq++;
}

// Single shared FFT of the latest capture, used by both the LCD spectrum and
// the Web UI (/api/fft), so both always show the same spectrum and peak.
static dso::FftResult g_fft;
static float          g_fftPeakFreq = 0.0f;
static uint32_t       g_fftSeq = UINT32_MAX;
static dso::FftPeak   g_fftPeaks[dso::FFT_MAX_PEAKS];
static size_t         g_fftPeakCount = 0;
static uint8_t        g_fftPeakThrPct = dso::FFT_PEAK_THRESHOLD_DEF; // set via /api/set?fftThr=N

// X-axis zoom window shared by the LCD and /api/fft
static dso::FftWindow g_fftWin;
static uint8_t        g_fftWinZoom = 0xFF;

static void ensureFft() {
  const bool fresh = (g_fftSeq != g_captureSeq);
  if (fresh) {
    g_fftSeq = g_captureSeq;
    dso::computeFft(g_ch1Volts, dso::CAPTURE_SAMPLES, g_actualSampleRateHz, g_fft);
    g_fftPeakCount = dso::findFftPeaks(g_fft, g_fftPeakThrPct, g_fftPeaks, dso::FFT_MAX_PEAKS);

    // Refine with the time-domain cycle-count frequency when it agrees with the FFT peak
    g_fftPeakFreq = g_fft.peakFreq;
    if (g_metricsCh1.freq >= 1.0f && g_metricsCh1.vpp >= 0.05f && g_fftPeakFreq > 0.0f &&
        fabsf(g_fftPeakFreq - g_metricsCh1.freq) <= fmaxf(2.0f * g_fft.binHz, 0.1f * g_fftPeakFreq)) {
      g_fftPeakFreq = g_metricsCh1.freq;
    }
  }
  if (fresh || g_fftWinZoom != g_settings.fftZoomIdx) {
    const bool sameZoom = (g_fftWinZoom == g_settings.fftZoomIdx);
    g_fftWin = dso::fftZoomWindow(g_settings.fftZoomIdx, g_fftPeaks, g_fftPeakCount,
                                  g_fft.peakBin, sameZoom ? &g_fftWin : nullptr);
    g_fftWinZoom = g_settings.fftZoomIdx;
  }
}

// =============================================================================
// 8. 262K-COLOR LCD RENDERING ENGINE
// =============================================================================

namespace pal = dso::colors;

static int16_t textWidth(const char *s, uint8_t size) {
  return (int16_t)strlen(s) * 6 * size;
}

static void drawText(int16_t x, int16_t y, const char *s, uint8_t size, uint16_t color) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(s);
}

static void drawTextCentered(int16_t cx, int16_t y, const char *s, uint8_t size, uint16_t color) {
  drawText(cx - (textWidth(s, size) - size) / 2, y, s, size, color);
}

// Single-label button (top bar, tabs)
static void drawButton(int16_t x, int16_t y, int16_t w, int16_t h,
                       uint16_t fill, uint16_t border, const char *label,
                       uint16_t textCol = pal::TEXT_WHITE) {
  gfx->fillRoundRect(x, y, w, h, 5, fill);
  gfx->drawRoundRect(x, y, w, h, 5, border);
  drawTextCentered(x + w / 2, y + (h - 7) / 2, label, 1, textCol);
}

// Two-line control: small caption on top, large value below. The value uses
// the 2x font when it fits and falls back to 1x otherwise. `arrows` marks
// controls whose left / right halves step down / up.
static void drawTile(int16_t x, int16_t y, int16_t w, int16_t h,
                     uint16_t fill, uint16_t border,
                     const char *caption, const char *value,
                     uint16_t valueCol = pal::TEXT_WHITE,
                     uint16_t captionCol = pal::TEXT_MUTED,
                     bool arrows = false) {
  gfx->fillRoundRect(x, y, w, h, 6, fill);
  gfx->drawRoundRect(x, y, w, h, 6, border);
  const bool    tall = (h >= 40);
  const int16_t cx   = x + w / 2;
  const int16_t capY = y + (tall ? 6 : 4);
  drawTextCentered(cx, capY, caption, 1, captionCol);
  if (arrows) {
    const int16_t ay = capY + 3;
    gfx->fillTriangle(x + 5, ay, x + 9, ay - 3, x + 9, ay + 3, captionCol);
    gfx->fillTriangle(x + w - 6, ay, x + w - 10, ay - 3, x + w - 10, ay + 3, captionCol);
  }
  const bool big = textWidth(value, 2) <= w - 10;
  const int16_t valY = big ? (y + h - (tall ? 22 : 18)) : (y + h - (tall ? 16 : 13));
  drawTextCentered(cx, valY, value, big ? 2 : 1, valueCol);
}

static void drawWifiIcon(int16_t zoneX, int16_t zoneW, uint8_t bars, bool connected,
                         int16_t baseY = dso::TOP_BTN_Y + dso::TOP_BTN_H - 3) {
  const int16_t iconW = 18; // 4 bars x 3 px + 3 gaps x 2 px
  const int16_t x0    = zoneX + (zoneW - iconW) / 2;
  const uint16_t activeCol = (bars >= 3) ? pal::BADGE_RUN : ((bars == 2) ? pal::BADGE_WARN : pal::BADGE_HOLD);
  for (uint8_t b = 0; b < 4; ++b) {
    const int16_t bh = 4 + b * 3;
    const uint16_t c = (connected && b < bars) ? activeCol : pal::GRID_MAJOR;
    gfx->fillRect(x0 + b * 5, baseY - bh + 1, 3, bh, c);
  }
  if (!connected) {
    gfx->drawLine(x0, baseY - 12, x0 + 6, baseY - 6, pal::BADGE_HOLD);
    gfx->drawLine(x0 + 1, baseY - 12, x0 + 7, baseY - 6, pal::BADGE_HOLD);
    gfx->drawLine(x0 + 6, baseY - 12, x0, baseY - 6, pal::BADGE_HOLD);
    gfx->drawLine(x0 + 7, baseY - 12, x0 + 1, baseY - 6, pal::BADGE_HOLD);
  }
}

static void drawMenuButton() {
  const int16_t x = dso::MENU_BTN_X, y = dso::MENU_BTN_Y, w = dso::MENU_BTN_W, h = dso::MENU_BTN_H;
  gfx->fillRoundRect(x, y, w, h, 5, pal::BTN_DARK);
  gfx->drawRoundRect(x, y, w, h, 5, pal::CH2_TRACE);
  for (int16_t i = 0; i < 3; ++i) gfx->drawFastHLine(x + 7, y + 5 + i * 4, 9, pal::CH2_TRACE);
  drawText(x + 21, y + 6, "MENU", 1, pal::TEXT_WHITE);
}

static void drawTopBarBackground() {
  gfx->fillRect(0, 0, dso::LCD_W, dso::TOP_BAR_H, pal::BG_PANEL);
  gfx->drawFastHLine(0, dso::TOP_BAR_H - 1, dso::LCD_W, pal::GRID_MAJOR);
}

static void drawBottomBarBackground() {
  const int16_t by = dso::LCD_H - dso::BOT_BAR_H;
  gfx->fillRect(0, by, dso::LCD_W, dso::BOT_BAR_H, pal::BG_PANEL);
  gfx->drawFastHLine(0, by, dso::LCD_W, pal::GRID_MAJOR);
}

static void renderScopeScreen() {
  gfx->fillScreen(pal::BG_DARK);

  // --- 1. Top Status Bar: [AUTO] (LOOP) DC  freq ... [FFT]  wifi  [MENU] ---
  drawTopBarBackground();

  const bool isAuto = (g_settings.autoTimebase || g_settings.autoVolts);
  drawButton(dso::AUTO_BADGE_X, dso::TOP_BTN_Y, dso::AUTO_BADGE_W, dso::TOP_BTN_H,
             isAuto ? pal::BADGE_AUTO : pal::BTN_DARK,
             isAuto ? pal::BORDER_ACTIVE : pal::BTN_BORDER,
             isAuto ? "AUTO" : "MAN");

  int16_t textX = dso::AUTO_BADGE_X + dso::AUTO_BADGE_W + 6;
  if (g_settings.selfTest) {
    drawButton(textX, dso::TOP_BTN_Y, 32, dso::TOP_BTN_H, pal::BTN_ACTIVE, pal::BORDER_ACTIVE, "LOOP", pal::CH1_TRACE);
    textX += 38;
  }

  char buf[16];
  const int16_t textY = dso::TOP_BTN_Y + 6;
  dso::fmtVolts(buf, sizeof(buf), g_metricsCh1.dc);
  drawText(textX, textY, buf, 1, pal::CH1_TRACE);
  textX += textWidth(buf, 1) + 6;
  if (g_metricsCh1.freq >= 1.0f) {
    dso::fmtFreq(buf, sizeof(buf), g_metricsCh1.freq);
    drawText(textX, textY, buf, 1, pal::DIG_D1);
  } else {
    drawText(textX, textY, "pp", 1, pal::TEXT_MUTED);
    dso::fmtVolts(buf, sizeof(buf), g_metricsCh1.vpp);
    drawText(textX + 14, textY, buf, 1, pal::TEXT_WHITE);
  }

  drawButton(dso::FFT_BTN_X, dso::TOP_BTN_Y, dso::FFT_BTN_W, dso::TOP_BTN_H,
             pal::BTN_DARK, pal::CH2_TRACE, "FFT", pal::CH2_TRACE);
  drawWifiIcon(dso::WIFI_ZONE_X, dso::WIFI_ZONE_W, g_netStats.bars, g_netStats.connected);
  drawMenuButton();

  // --- 2. Waveform Grid (320 x 110 px) with Center Ticks ---
  for (uint16_t c = 1; c < dso::GRID_COLS; ++c) {
    uint16_t gx = (c * dso::LCD_W) / dso::GRID_COLS;
    uint16_t col = (c == dso::GRID_COLS / 2) ? pal::GRID_MAJOR : pal::GRID_MINOR;
    gfx->drawFastVLine(gx, dso::CHART_Y, dso::CHART_H, col);
    if (c != dso::GRID_COLS / 2) {
      uint16_t cy = dso::CHART_Y + dso::CHART_H / 2;
      gfx->drawFastVLine(gx, cy - 2, 5, pal::GRID_TICK);
    }
  }
  for (uint16_t r = 1; r < dso::GRID_ROWS; ++r) {
    uint16_t gy = dso::CHART_Y + (r * dso::CHART_H) / dso::GRID_ROWS;
    uint16_t col = (r == dso::GRID_ROWS / 2) ? pal::GRID_MAJOR : pal::GRID_MINOR;
    gfx->drawFastHLine(0, gy, dso::LCD_W, col);
    if (r != dso::GRID_ROWS / 2) {
      uint16_t cx = dso::LCD_W / 2;
      gfx->drawFastHLine(cx - 2, gy, 5, pal::GRID_TICK);
    }
  }

  // Trigger level dashed line (magenta)
  int16_t trigY = dso::voltsToY(g_settings.trigVolts, dso::V_DIVS[g_settings.vDivIdx],
                                g_settings.acCoupled, g_metricsCh1.dc, dso::CHART_Y, dso::CHART_H);
  for (uint16_t x = 0; x < dso::LCD_W; x += 6) {
    gfx->drawFastHLine(x, trigY, 3, pal::TRIG_LINE);
  }
  int16_t markerY = constrain(trigY, (int16_t)(dso::CHART_Y + 4), (int16_t)(dso::CHART_Y + dso::CHART_H - 5));
  gfx->fillTriangle(0, markerY - 4, 0, markerY + 4, 6, markerY, pal::TRIG_MARKER);

  // --- 3. Build & Draw Waveform Traces ---
  dso::View view = dso::computeView(g_ch1Volts, dso::CAPTURE_SAMPLES, g_actualSampleRateHz, g_settings, g_metricsCh1.vpp);
  dso::buildTrace(g_trace, g_ch1Volts, g_ch2Volts, g_ch2Digital, dso::CAPTURE_SAMPLES, view, g_settings, g_metricsCh1.dc, g_metricsCh2.dc);

  // CH2 Trace (Analog Cyan or Digital D1 Mint Green)
  if (g_settings.ch2Mode == dso::CH2_ANALOG) {
    for (uint16_t x = 1; x < dso::LCD_W; ++x) {
      gfx->drawLine(x - 1, g_trace.y2[x - 1], x, g_trace.y2[x], pal::CH2_TRACE);
    }
  } else if (g_settings.ch2Mode == dso::CH2_DIGITAL) {
    const int16_t logY0 = dso::CHART_Y + dso::CHART_H - 6;
    const int16_t logY1 = dso::CHART_Y + dso::CHART_H - 22;

    gfx->fillRect(2, logY1 - 2, 18, 10, pal::BG_PANEL);
    gfx->drawRect(2, logY1 - 2, 18, 10, pal::DIG_D1);
    drawText(4, logY1, "D1", 1, pal::DIG_D1);

    int16_t prevYd = g_trace.d2[20] ? logY1 : logY0;
    for (uint16_t x = 20; x < dso::LCD_W; ++x) {
      int16_t yd = g_trace.d2[x] ? logY1 : logY0;
      if (yd != prevYd) {
        gfx->drawFastVLine(x, min(yd, prevYd), abs(yd - prevYd) + 1, pal::DIG_D1);
      }
      gfx->drawPixel(x, yd, pal::DIG_D1);
      prevYd = yd;
    }
  }

  // CH1 Neon Yellow Trace (dual-pixel stroke for bright phosphor look)
  for (uint16_t x = 1; x < dso::LCD_W; ++x) {
    gfx->drawLine(x - 1, g_trace.y1[x - 1], x, g_trace.y1[x], pal::CH1_TRACE);
    gfx->drawLine(x - 1, min((int16_t)(dso::CHART_Y + dso::CHART_H - 2), (int16_t)(g_trace.y1[x - 1] + 1)),
                  x,     min((int16_t)(dso::CHART_Y + dso::CHART_H - 2), (int16_t)(g_trace.y1[x] + 1)), pal::CH1_GLOW);
  }

  // Ground 1▶ marker
  int16_t gndY = dso::voltsToY(0.0f, dso::V_DIVS[g_settings.vDivIdx], g_settings.acCoupled, g_metricsCh1.dc, dso::CHART_Y, dso::CHART_H);
  gndY = constrain(gndY, (int16_t)(dso::CHART_Y + 4), (int16_t)(dso::CHART_Y + dso::CHART_H - 5));
  gfx->fillTriangle(0, gndY - 3, 0, gndY + 3, 5, gndY, pal::CH1_TRACE);

  // --- 4. Bottom Control Bar (caption + large value buttons) ---
  drawBottomBarBackground();

  drawTile(dso::BOT_BTN_X[0], dso::BOT_BTN_Y, dso::BOT_BTN_W[0], dso::BOT_BTN_H,
           isAuto ? pal::BADGE_AUTO : pal::BTN_DARK,
           isAuto ? pal::BORDER_ACTIVE : pal::BTN_BORDER,
           "AUTOSCALE", isAuto ? "ON" : "OFF",
           pal::TEXT_WHITE, isAuto ? pal::TEXT_WHITE : pal::TEXT_MUTED);

  drawTile(dso::BOT_BTN_X[1], dso::BOT_BTN_Y, dso::BOT_BTN_W[1], dso::BOT_BTN_H,
           pal::BTN_DARK, pal::CH2_TRACE,
           g_settings.autoTimebase ? "T/div:A" : "T/div",
           dso::TIME_BASE_LABELS[g_settings.timeBaseIdx], pal::CH2_TRACE, pal::TEXT_MUTED, true);

  drawTile(dso::BOT_BTN_X[2], dso::BOT_BTN_Y, dso::BOT_BTN_W[2], dso::BOT_BTN_H,
           pal::BTN_DARK, pal::CH1_TRACE,
           g_settings.autoVolts ? "V/div:A" : "V/div",
           dso::V_DIV_LABELS[g_settings.vDivIdx], pal::CH1_TRACE, pal::TEXT_MUTED, true);

  const char *trigTag = (g_settings.ch2Mode == dso::CH2_DIGITAL) ? "MSO D1"
                      : (g_settings.trigMode == dso::TRIG_SINGLE) ? "SINGLE"
                      : (g_settings.acCoupled ? "AC" : "DC+AC");
  drawTile(dso::BOT_BTN_X[3], dso::BOT_BTN_Y, dso::BOT_BTN_W[3], dso::BOT_BTN_H,
           g_settings.running ? pal::BADGE_RUN : pal::BADGE_HOLD, pal::TEXT_WHITE,
           trigTag, g_settings.running ? "RUN" : "HOLD", pal::TEXT_WHITE, pal::TEXT_WHITE);

  gfx->flush();
}

// SYS tab, first row: battery gauge + uptime
static void drawPowerStatsPanel(int16_t x, int16_t y, int16_t w, int16_t h) {
  gfx->fillRoundRect(x, y, w, h, 6, pal::BG_PANEL);
  gfx->drawRoundRect(x, y, w, h, 6, pal::GRID_MAJOR);

  const bool hasCell = (g_batVolts >= dso::BATTERY_PRESENT_MIN_V);
  const uint16_t lvlCol = !hasCell ? pal::CH2_TRACE
                        : (g_batPct > 40) ? pal::BADGE_RUN
                        : (g_batPct > 15) ? pal::BADGE_WARN : pal::BADGE_HOLD;

  // Gauge (34 x 18 body + terminal nub)
  const int16_t gx = x + 10, gy = y + (h - 18) / 2, gw = 34, gh = 18;
  const uint16_t frameCol = hasCell ? pal::TEXT_MUTED : pal::CH2_TRACE;
  gfx->drawRoundRect(gx, gy, gw, gh, 3, frameCol);
  gfx->fillRect(gx + gw, gy + 5, 3, gh - 10, frameCol);
  if (hasCell) {
    const int16_t fw = ((gw - 4) * g_batPct) / 100;
    if (fw > 0) gfx->fillRect(gx + 2, gy + 2, fw, gh - 4, lvlCol);
  } else {
    // Lightning bolt = external (USB) power
    gfx->fillTriangle(gx + 20, gy + 2, gx + 12, gy + 10, gx + 18, gy + 10, pal::CH2_TRACE);
    gfx->fillTriangle(gx + 16, gy + 8, gx + 22, gy + 8, gx + 14, gy + 16, pal::CH2_TRACE);
  }

  char buf[24];
  const int16_t tx = gx + gw + 12;
  drawText(tx, y + 6, "BATTERY", 1, pal::TEXT_MUTED);
  if (hasCell) {
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)g_batPct);
    drawText(tx, y + 20, buf, 2, lvlCol);
    const int16_t vx = tx + textWidth(buf, 2) + 6;
    snprintf(buf, sizeof(buf), "%.2fV", g_batVolts);
    drawText(vx, y + 27, buf, 1, pal::TEXT_WHITE);
  } else {
    drawText(tx, y + 20, "USB", 2, pal::CH2_TRACE);
    drawText(tx + textWidth("USB", 2) + 6, y + 27, "no cell", 1, pal::TEXT_MUTED);
  }

  const int16_t dx = x + w / 2 + 10;
  gfx->drawFastVLine(dx, y + 6, h - 12, pal::GRID_MAJOR);
  drawText(dx + 10, y + 6, "UPTIME", 1, pal::TEXT_MUTED);
  dso::fmtUptime(buf, sizeof(buf), millis() / 1000UL);
  const bool big = textWidth(buf, 2) <= (x + w - 8) - (dx + 10);
  drawText(dx + 10, big ? y + 20 : y + 25, buf, big ? 2 : 1, pal::TEXT_WHITE);
}

// SCOPE tab: left half = Probe A, right half = Probe B
static void drawProbeTile(int16_t x, int16_t y, int16_t w, int16_t h) {
  gfx->fillRoundRect(x, y, w, h, 6, pal::BTN_DARK);
  gfx->drawRoundRect(x, y, w, h, 6, pal::BTN_BORDER);
  gfx->drawFastVLine(x + w / 2, y + 6, h - 12, pal::GRID_MAJOR);

  const uint8_t  idx[2] = { g_settings.probeIdx, g_settings.probeBIdx };
  const uint16_t col[2] = { pal::CH1_TRACE, pal::CH2_TRACE };
  const char    *cap[2] = { "Probe A", "Probe B" };
  for (uint8_t i = 0; i < 2; ++i) {
    const int16_t cx = x + w / 4 + i * (w / 2);
    drawTextCentered(cx, y + 6, cap[i], 1, pal::TEXT_MUTED);
    const float scale = dso::PROBE_SCALES[idx[i]];
    char sc[8], rng[10];
    snprintf(sc, sizeof(sc), "%gx", (double)scale);
    snprintf(rng, sizeof(rng), "%.3gV", (double)(3.3f * scale));
    const int16_t sw = textWidth(sc, 2);
    const int16_t sx = cx - (sw + 4 + textWidth(rng, 1)) / 2;
    drawText(sx, y + 20, sc, 2, col[i]);
    drawText(sx + sw + 4, y + 27, rng, 1, pal::TEXT_WHITE);
  }
}

static void renderSettingsModal() {
  gfx->fillScreen(pal::BG_DARK);

  // Tab Header Bar (26 px), inset from the rounded corners
  gfx->fillRect(0, 0, dso::LCD_W, 26, pal::BG_PANEL);
  gfx->drawFastHLine(0, 25, dso::LCD_W, pal::GRID_MAJOR);
  for (uint8_t t = 0; t < dso::TAB_COUNT; ++t) {
    const int16_t tx = dso::TAB_X0 + t * dso::TAB_W;
    const bool active = (g_ui.tab == t);
    drawButton(tx, dso::TOP_BTN_Y, dso::TAB_DRAW_W, dso::TOP_BTN_H,
               active ? pal::BADGE_AUTO : pal::BTN_DARK,
               active ? pal::BORDER_ACTIVE : pal::BTN_BORDER,
               dso::TAB_LABELS[t], active ? pal::TEXT_WHITE : pal::TEXT_MUTED);
  }
  drawButton(dso::DONE_BTN_X, dso::TOP_BTN_Y, dso::DONE_BTN_W, dso::TOP_BTN_H,
             pal::BADGE_RUN, pal::TEXT_WHITE, "DONE");

  // --- Tab 3: WIFI / NETWORK STATISTICS DASHBOARD ---
  if (g_ui.tab == dso::TAB_NET) {
    gfx->fillRoundRect(6, 30, 308, 136, 6, pal::BG_PANEL);
    gfx->drawRoundRect(6, 30, 308, 136, 6, pal::GRID_MAJOR);

    drawText(14, 36, "NETWORK & WI-FI", 1, pal::CH2_TRACE);
    drawWifiIcon(278, 28, g_netStats.bars, g_netStats.connected, 46);

    drawText(14, 54, "IP", 1, pal::TEXT_MUTED);
    drawText(34, 50, g_netStats.connected ? g_netStats.ip : "Disconnected", 2,
             g_netStats.connected ? pal::BADGE_RUN : pal::BADGE_HOLD);

    drawText(14, 74, "Mode", 1, pal::TEXT_MUTED);
    const char *mode = !g_netStats.connected ? "Offline"
                     : g_netStats.isStatic ? (g_netStats.fallbackUsed ? "Static fallback" : "Static IP")
                     : "DHCP";
    drawText(50, 74, mode, 1, pal::TEXT_WHITE);

    drawText(164, 74, "SSID", 1, pal::TEXT_MUTED);
    drawText(194, 74, g_netStats.connected ? g_netStats.ssid : "--", 1, pal::CH1_TRACE);

    drawText(14, 90, "Gateway", 1, pal::TEXT_MUTED);
    drawText(62, 90, g_netStats.connected ? g_netStats.gw : "--", 1, pal::TEXT_WHITE);

    drawText(164, 90, "Signal", 1, pal::TEXT_MUTED);
    char buf[40];
    if (g_netStats.connected) snprintf(buf, sizeof(buf), "%d dBm (%u/4)", g_netStats.rssi, (unsigned)g_netStats.bars);
    else snprintf(buf, sizeof(buf), "No signal");
    drawText(206, 90, buf, 1, g_netStats.connected ? pal::BADGE_RUN : pal::TEXT_MUTED);

    drawText(14, 106, "MAC", 1, pal::TEXT_MUTED);
    drawText(62, 106, g_netStats.connected ? g_netStats.mac : "--", 1, pal::TEXT_WHITE);

    drawText(14, 122, "Web UI", 1, pal::TEXT_MUTED);
    if (g_netStats.connected) snprintf(buf, sizeof(buf), "http://%s", g_netStats.ip);
    else snprintf(buf, sizeof(buf), "Offline");
    drawText(62, 122, buf, 1, pal::CH2_TRACE);

    drawButton(dso::NET_BTN_X, dso::NET_BTN_Y, dso::NET_BTN_W, dso::NET_BTN_H,
               pal::BADGE_AUTO, pal::TEXT_WHITE, "RECONNECT");

    gfx->flush();
    return;
  }

  // --- Other Tabs (SCOPE, TRIG, I/O, SYS) rendered using 2x3 Tiles ---
  const int16_t colX[2] = { dso::TILE_COL_X[0], dso::TILE_COL_X[1] };
  const int16_t rowY[3] = { dso::TILE_ROW_Y[0], dso::TILE_ROW_Y[1], dso::TILE_ROW_Y[2] };
  const int16_t tileW   = dso::TILE_W;
  const int16_t tileH   = dso::TILE_H;

  auto onOffTile = [&](int16_t x, int16_t y, const char *caption, bool on) {
    drawTile(x, y, tileW, tileH,
             on ? pal::BTN_ACTIVE : pal::BTN_DARK,
             on ? pal::BORDER_ACTIVE : pal::BTN_BORDER,
             caption, on ? "ON" : "OFF",
             on ? pal::TEXT_WHITE : pal::TEXT_MUTED);
  };

  if (g_ui.tab == dso::TAB_SCOPE) {
    onOffTile(colX[0], rowY[0], "Auto Timebase", g_settings.autoTimebase);
    onOffTile(colX[1], rowY[0], "Auto Volts", g_settings.autoVolts);

    drawTile(colX[0], rowY[1], tileW, tileH, pal::BTN_DARK, pal::CH2_TRACE,
             "Timebase / div", dso::TIME_BASE_LABELS[g_settings.timeBaseIdx],
             pal::CH2_TRACE, pal::TEXT_MUTED, true);

    drawTile(colX[1], rowY[1], tileW, tileH, pal::BTN_DARK, pal::CH1_TRACE,
             "Volts / div", dso::V_DIV_LABELS[g_settings.vDivIdx],
             pal::CH1_TRACE, pal::TEXT_MUTED, true);

    drawTile(colX[0], rowY[2], tileW, tileH, pal::BTN_DARK, pal::BORDER_ACTIVE,
             "Max Sample Rate", dso::SAMPLE_RATE_LABELS[g_settings.sampleRateIdx]);

    drawProbeTile(colX[1], rowY[2], tileW, tileH);

  } else if (g_ui.tab == dso::TAB_TRIG) {
    drawTile(colX[0], rowY[0], tileW, tileH, pal::BTN_DARK, pal::TRIG_LINE,
             "Trigger Edge", dso::TRIG_MODE_LABELS[g_settings.trigMode], pal::TRIG_LINE);

    drawTile(colX[1], rowY[0], tileW, tileH,
             g_settings.acCoupled ? pal::BTN_ACTIVE : pal::BTN_DARK, pal::BORDER_ACTIVE,
             "Input Coupling", g_settings.acCoupled ? "AC" : "DC+AC");

    char trigStr[16];
    dso::fmtVolts(trigStr, sizeof(trigStr), g_settings.trigVolts);
    drawTile(colX[0], rowY[1], tileW, tileH, pal::BTN_DARK, pal::TRIG_LINE,
             g_settings.userSetTrig ? "Trig Level (tap=auto)" : "Trig Level (auto)",
             trigStr, pal::TRIG_LINE);

    drawTile(colX[1], rowY[1], tileW, tileH, pal::BTN_DARK, pal::BORDER_ACTIVE,
             "Noise Hysteresis", dso::HYST_LABELS[g_settings.hystIdx]);

    const bool armed = (g_settings.trigMode == dso::TRIG_SINGLE && g_settings.running);
    drawTile(colX[0], rowY[2], tileW, tileH, pal::BADGE_AUTO, pal::TEXT_WHITE,
             "Single Shot", armed ? "ARMED" : "ARM", pal::TEXT_WHITE, pal::TEXT_WHITE);

    drawTile(colX[1], rowY[2], tileW, tileH,
             g_settings.running ? pal::BADGE_RUN : pal::BADGE_HOLD, pal::TEXT_WHITE,
             "Acquisition", g_settings.running ? "RUN" : "HOLD", pal::TEXT_WHITE, pal::TEXT_WHITE);

  } else if (g_ui.tab == dso::TAB_IO) {
    const bool dig = (g_settings.ch2Mode == dso::CH2_DIGITAL);
    drawTile(colX[0], rowY[0], tileW, tileH,
             (g_settings.ch2Mode != dso::CH2_OFF) ? pal::BTN_ACTIVE : pal::BTN_DARK,
             dig ? pal::DIG_D1 : pal::CH2_TRACE,
             "Channel 2 Mode", dso::CH2_MODE_LABELS[g_settings.ch2Mode],
             dig ? pal::DIG_D1 : pal::CH2_TRACE);

    drawTile(colX[1], rowY[0], tileW, tileH,
             g_settings.selfTest ? pal::BTN_ACTIVE : pal::BTN_DARK,
             g_settings.selfTest ? pal::BORDER_ACTIVE : pal::BTN_BORDER,
             "Input Source", g_settings.selfTest ? "Loopback" : "Header",
             g_settings.selfTest ? pal::CH1_TRACE : pal::TEXT_WHITE);

    drawTile(colX[0], rowY[1], tileW, tileH,
             (g_settings.calFreqIdx > 0) ? pal::BTN_ACTIVE : pal::BTN_DARK, pal::BORDER_ACTIVE,
             "GPIO7 Cal Gen", dso::CAL_FREQ_LABELS[g_settings.calFreqIdx]);

  } else if (g_ui.tab == dso::TAB_SYS) {
    drawPowerStatsPanel(colX[0], rowY[0], colX[1] + tileW - colX[0], tileH);

    drawTile(colX[0], rowY[1], tileW, tileH, pal::BTN_DARK, pal::BORDER_ACTIVE,
             "LCD Brightness", dso::BRIGHTNESS_LABELS[g_settings.brightnessIdx]);

    const bool armed = (g_ui.resetArmedUntil > millis());
    drawTile(colX[1], rowY[1], tileW, tileH,
             armed ? pal::BADGE_HOLD : pal::BTN_DARK,
             armed ? pal::TEXT_WHITE : pal::BADGE_HOLD,
             armed ? "Tap again to confirm" : "Factory Reset",
             armed ? "CONFIRM" : "RESET",
             armed ? pal::TEXT_WHITE : pal::BADGE_HOLD,
             armed ? pal::TEXT_WHITE : pal::TEXT_MUTED);

    drawTile(colX[0], rowY[2], tileW, tileH, pal::BG_PANEL, pal::GRID_MAJOR,
             "Firmware", "v" DSO_FW_VERSION, pal::CH2_TRACE);

    drawTile(colX[1], rowY[2], tileW, tileH, pal::BG_PANEL, pal::GRID_MAJOR,
             "RISC-V 160MHz", "ESP32-C6", pal::DIG_D1);
  }

  gfx->flush();
}

// Compact frequency label for the FFT X axis; precision follows the label step
static void fmtAxisFreq(char *o, size_t n, float f, float stepHz) {
  if (f < 999.5f)                         snprintf(o, n, "%.0f", f);
  else if (stepHz < 100.0f)               snprintf(o, n, "%.2fk", f / 1000.0f);
  else if (f < 9999.5f || stepHz < 1000.0f) snprintf(o, n, "%.1fk", f / 1000.0f);
  else                                    snprintf(o, n, "%.0fk", f / 1000.0f);
}

// Standalone Full-Screen FFT Frequency Spectrum with X-axis zoom
static void renderFftScreen() {
  gfx->fillScreen(pal::BG_DARK);

  // 1. Shared FFT of the full 1024-sample capture + zoom window
  ensureFft();
  const dso::FftResult &fftResult = g_fft;
  const dso::FftWindow  win       = g_fftWin;
  const size_t          winBins   = win.width();

  // 2. Top Bar: [< SCOPE]  PEAK <freq>  wifi  [MENU]
  drawTopBarBackground();
  drawButton(dso::FFT_BACK_BTN_X, dso::TOP_BTN_Y, dso::FFT_BACK_BTN_W, dso::TOP_BTN_H,
             pal::BTN_DARK, pal::CH2_TRACE, "< SCOPE", pal::CH2_TRACE);

  char buf[20];
  const int16_t pkX = dso::FFT_BACK_BTN_X + dso::FFT_BACK_BTN_W + 8;
  drawText(pkX, dso::TOP_BTN_Y + 6, "PEAK", 1, pal::TEXT_MUTED);
  dso::fmtFreq(buf, sizeof(buf), (g_fftPeakCount > 0) ? g_fftPeakFreq : 0.0f);
  drawText(pkX + 28, dso::TOP_BTN_Y + 2, buf, 2, pal::CH1_TRACE);

  drawWifiIcon(dso::WIFI_ZONE_X, dso::WIFI_ZONE_W, g_netStats.bars, g_netStats.connected);
  drawMenuButton();

  // 3. Chart geometry: bars on top, frequency axis strip at the bottom
  constexpr int16_t AXIS_H = 11;
  const int16_t baseY    = dso::CHART_Y + dso::CHART_H - AXIS_H - 1;
  const int16_t chartTop = dso::CHART_Y + 1;
  const int16_t maxH     = baseY - (dso::CHART_Y + 2);

  for (uint16_t r = 1; r < dso::GRID_ROWS; ++r) {
    int16_t gy = dso::CHART_Y + (r * (baseY - dso::CHART_Y)) / dso::GRID_ROWS;
    gfx->drawFastHLine(0, gy, dso::LCD_W, pal::GRID_MINOR);
  }
  for (uint16_t c = 1; c < dso::GRID_COLS; ++c) {
    int16_t gx = (c * dso::LCD_W) / dso::GRID_COLS;
    gfx->drawFastVLine(gx, dso::CHART_Y, baseY - dso::CHART_Y + 1, pal::GRID_MINOR);
  }
  gfx->fillRect(0, baseY + 2, dso::LCD_W, dso::CHART_Y + dso::CHART_H - (baseY + 2), pal::BG_PANEL);
  gfx->drawFastHLine(0, baseY + 1, dso::LCD_W, pal::GRID_MAJOR);

  // Left-Axis dB / Linear Scale Labels
  const int16_t midLblY = (dso::CHART_Y + baseY) / 2 - 3;
  drawText(2, dso::CHART_Y + 3, g_settings.fftLogScale ? "0dB" : "100%", 1, pal::TEXT_MUTED);
  drawText(2, midLblY, g_settings.fftLogScale ? "-30" : "50%", 1, pal::TEXT_MUTED);
  drawText(2, baseY - 9, g_settings.fftLogScale ? "-60" : "0%", 1, pal::TEXT_MUTED);

  // 4. Spectrum bars over the zoom window
  const float refMag = fmaxf(fftResult.maxMag, 1e-4f);
  auto magToH = [&](float mag) -> int16_t {
    float norm = mag / refMag;
    int16_t h;
    if (g_settings.fftLogScale) {
      float db = 20.0f * log10f(fmaxf(norm, 1e-4f));
      if (db > 0.0f) db = 0.0f;
      if (db < -60.0f) db = -60.0f;
      h = (int16_t)(((db + 60.0f) / 60.0f) * (float)maxH);
    } else {
      if (norm > 1.0f) norm = 1.0f;
      h = (int16_t)(norm * (float)maxH);
    }
    return (h < 1) ? 1 : h;
  };

  // Each column shows the max of the bins it covers; when zoomed in far enough
  // that a bin spans >= 3 columns, its last column is left blank as a separator.
  static int16_t colH[dso::LCD_W];
  const bool binGaps = (winBins * 3 <= (size_t)dso::LCD_W);
  for (int16_t bx = 0; bx < (int16_t)dso::LCD_W; ++bx) {
    const size_t q0 = ((size_t)bx * winBins) / dso::LCD_W;
    const size_t q1 = ((size_t)(bx + 1) * winBins) / dso::LCD_W;
    size_t b0 = win.b0 + q0;
    size_t b1 = win.b0 + q1;
    if (b1 <= b0) b1 = b0 + 1;
    const size_t bs = (b0 < 1) ? 1 : b0; // skip bin 0 (DC)
    float mag = 0.0f;
    for (size_t b = bs; b < b1 && b < dso::FFT_BINS; ++b) mag = fmaxf(mag, fftResult.bins[b]);
    const int16_t h = magToH(mag);
    colH[bx] = h;
    if (binGaps && q1 != q0) continue;

    const int16_t barTop = baseY - h;
    const int16_t midY1  = baseY - (h * 2) / 3;
    const int16_t midY2  = baseY - (h / 3);
    gfx->drawFastVLine(bx, midY2, baseY - midY2 + 1, pal::FFT_BAR_BOT);
    if (midY1 < midY2) gfx->drawFastVLine(bx, midY1, midY2 - midY1 + 1, pal::FFT_BAR_MID);
    if (barTop + 1 < midY1) gfx->drawFastVLine(bx, barTop + 1, midY1 - (barTop + 1) + 1, pal::FFT_BAR_TOP);
    gfx->drawPixel(bx, barTop, pal::FFT_BAR_CAP);
  }

  // Peak-detection threshold (dashed line)
  if (g_fftPeakCount > 0) {
    int16_t thrY = baseY - magToH(fftResult.peakMag * ((float)g_fftPeakThrPct / 100.0f));
    for (int16_t x = 24; x < (int16_t)dso::LCD_W; x += 6) {
      gfx->drawFastHLine(x, thrY, 3, pal::TRIG_LINE);
    }
  }

  struct LabelBox { int16_t x0, y0, x1, y1; };
  LabelBox placed[dso::FFT_MAX_PEAKS + 1];
  size_t placedCount = 0;

  // Zoom overview: where the visible window sits inside the full span
  if (winBins < dso::FFT_BINS) {
    const int16_t ovW = 64, ovX = dso::LCD_W - 8 - ovW, ovY = dso::CHART_Y + 5;
    gfx->fillRect(ovX, ovY, ovW, 3, pal::GRID_MAJOR);
    const int16_t sx = ovX + (int16_t)((win.b0 * ovW) / dso::FFT_BINS);
    int16_t sw = (int16_t)((winBins * ovW) / dso::FFT_BINS);
    if (sw < 2) sw = 2;
    gfx->fillRect(sx, ovY - 1, sw, 5, pal::CH2_TRACE);
    placed[placedCount++] = { (int16_t)(ovX - 2), (int16_t)(ovY - 3), (int16_t)(ovX + ovW + 2), (int16_t)(ovY + 6) };
  }

  // Frequency labels on the top-N peaks inside the window (strongest first, no overlaps)
  const int16_t lblH = 8;
  for (size_t k = 0; k < g_fftPeakCount; ++k) {
    const dso::FftPeak &pk = g_fftPeaks[k];
    if (pk.bin < win.b0 || pk.bin >= win.b1) continue;
    const float f = (k == 0) ? g_fftPeakFreq : pk.freq;
    int16_t px = (int16_t)((((float)(pk.bin - win.b0) + 0.5f) * (float)dso::LCD_W) / (float)winBins);
    px = constrain(px, (int16_t)0, (int16_t)(dso::LCD_W - 1));
    const int16_t barTop = baseY - colH[px];

    char txt[12];
    if (f >= 10000.0f)     snprintf(txt, sizeof(txt), "%.1fk", f / 1000.0f);
    else if (f >= 1000.0f) snprintf(txt, sizeof(txt), "%.2fk", f / 1000.0f);
    else                   snprintf(txt, sizeof(txt), "%.0f", f);
    const int16_t txtW = textWidth(txt, 1);
    const int16_t lx = constrain((int16_t)(px - txtW / 2), (int16_t)24, (int16_t)(dso::LCD_W - txtW - 1));

    // Try just above the bar, then step down through rows inside the chart
    bool ok = false;
    int16_t ly = barTop - lblH - 3;
    if (ly < chartTop) ly = chartTop;
    for (int16_t tries = 0; tries < 10 && !ok; ++tries) {
      ok = true;
      for (size_t q = 0; q < placedCount; ++q) {
        if (lx < placed[q].x1 + 2 && lx + txtW + 2 > placed[q].x0 &&
            ly < placed[q].y1 && ly + lblH > placed[q].y0) { ok = false; break; }
      }
      if (!ok) {
        ly += lblH + 1;
        if (ly + lblH > baseY - 2) break;
      }
    }

    const uint16_t col = (k == 0) ? pal::CH1_TRACE : pal::FFT_BAR_CAP;
    if (barTop - 5 >= chartTop) {
      gfx->fillTriangle(px, barTop - 1, px - 2, barTop - 4, px + 2, barTop - 4, col);
    } else {
      gfx->drawFastHLine(px - 2, barTop, 5, col);
    }
    if (!ok || placedCount >= sizeof(placed) / sizeof(placed[0])) continue;

    gfx->fillRect(lx - 1, ly - 1, txtW + 1, lblH + 1, pal::BG_DARK);
    drawText(lx, ly, txt, 1, col);
    placed[placedCount++] = { (int16_t)(lx - 1), (int16_t)(ly - 1), (int16_t)(lx + txtW), (int16_t)(ly + lblH) };
  }

  // 5. Frequency axis labels (every 2 divisions)
  const float binHz  = fftResult.binHz;
  const float f0     = (float)win.b0 * binHz;
  const float stepHz = (float)winBins * binHz / 5.0f;
  for (int16_t i = 0; i <= 5; ++i) {
    const int16_t x = (i * dso::LCD_W) / 5;
    fmtAxisFreq(buf, sizeof(buf), f0 + stepHz * (float)i, stepHz);
    if (i == 5) strncat(buf, "Hz", sizeof(buf) - strlen(buf) - 1);
    const int16_t tw = textWidth(buf, 1);
    const int16_t lx = (i == 0) ? 2 : ((i == 5) ? (dso::LCD_W - tw - 2) : (x - tw / 2));
    if (i > 0 && i < 5) gfx->drawFastVLine(x, baseY + 1, 3, pal::GRID_TICK);
    drawText(lx, baseY + 3, buf, 1, (binHz > 0.0f) ? pal::TEXT_MUTED : pal::TEXT_DIM);
  }

  // 6. Bottom Control Bar
  drawBottomBarBackground();

  const bool zoomed = (winBins < dso::FFT_BINS);
  drawTile(dso::BOT_BTN_X[0], dso::BOT_BTN_Y, dso::BOT_BTN_W[0], dso::BOT_BTN_H,
           zoomed ? pal::BTN_ACTIVE : pal::BTN_DARK, pal::CH2_TRACE,
           "X ZOOM", dso::FFT_ZOOM_LABELS[g_settings.fftZoomIdx], pal::CH2_TRACE);

  char spanStr[12];
  snprintf(spanStr, sizeof(spanStr), "%.0fk", (g_actualSampleRateHz * 0.5f) / 1000.0f);
  drawTile(dso::BOT_BTN_X[1], dso::BOT_BTN_Y, dso::BOT_BTN_W[1], dso::BOT_BTN_H,
           pal::BTN_DARK, pal::BORDER_ACTIVE, "SPAN (Hz)", spanStr);

  drawTile(dso::BOT_BTN_X[2], dso::BOT_BTN_Y, dso::BOT_BTN_W[2], dso::BOT_BTN_H,
           pal::BTN_DARK, pal::FFT_BAR_TOP,
           "MAGNITUDE", g_settings.fftLogScale ? "dB" : "LIN", pal::FFT_BAR_TOP);

  drawTile(dso::BOT_BTN_X[3], dso::BOT_BTN_Y, dso::BOT_BTN_W[3], dso::BOT_BTN_H,
           g_settings.running ? pal::BADGE_AUTO : pal::BADGE_HOLD,
           g_settings.running ? pal::BORDER_ACTIVE : pal::BADGE_HOLD,
           "SPECTRUM", g_settings.running ? "RUN" : "HOLD", pal::TEXT_WHITE, pal::TEXT_WHITE);

  gfx->flush();
}

// =============================================================================
// 9. TOUCH INPUT DISPATCHER
// =============================================================================

static void handleTouchInput() {
  static bool     wasPressed   = false;
  static bool     touchInChart = false;
  static uint32_t lastTapMs    = 0;

  uint16_t tx = 0, ty = 0;
  bool pressed = readTouchLandscape(tx, ty);

  if (!pressed) {
    wasPressed   = false;
    touchInChart = false;
    return;
  }

  uint32_t now = millis();

  // Vertical finger drag inside the waveform grid adjusts trigger level
  if (g_ui.screen == dso::SCR_MAIN && (touchInChart || (ty >= dso::CHART_Y && ty < (dso::CHART_Y + dso::CHART_H)))) {
    touchInChart = true;
    wasPressed   = true;
    dso::dragTrigger(g_settings, ty, g_metricsCh1.dc);
    return;
  }

  if (wasPressed || (now - lastTapMs < 180)) return;
  wasPressed = true;
  lastTapMs  = now;

  dso::Action act = dso::ACT_NONE;
  if (g_ui.screen == dso::SCR_MAIN) {
    act = dso::hitTestMain(tx, ty);
  } else if (g_ui.screen == dso::SCR_FFT) {
    act = dso::hitTestFft(tx, ty);
  } else {
    act = dso::hitTestSettings(g_ui.tab, tx, ty);
  }

  uint32_t fx = dso::applyAction(g_settings, g_ui, act, now);

  if (fx & dso::FX_CAL) updateCalibrationGenerator();
  if (fx & dso::FX_BACKLIGHT) updateBacklight();
#if ENABLE_WEB_UI
  if (fx & dso::FX_RECONNECT) connectWifi();
#endif
  if (fx & dso::FX_FACTORY_RESET) {
    g_settings = dso::Settings();
    saveSettings();
    updateCalibrationGenerator();
    updateBacklight();
  }

  // Save settings when user exits menu
  if (act == dso::ACT_CLOSE_MENU) {
    saveSettings();
  }
}

// =============================================================================
// 10. BUILT-IN HTTP WEB SERVER (ESP-IDF 5 / Arduino-ESP32 3.x Native)
// =============================================================================

// Set to 1 for bare-metal LCD bring-up (diagonal X + border, no Canvas/Wi-Fi/ADC).
// Set to 0 for normal oscilloscope operation.
#define LCD_DIAG_TEST 0

#if ENABLE_WEB_UI
// Allocation-free JSON writer into one static buffer (handlers run
// sequentially from loop()). Replaces String concatenation, which caused
// hundreds of heap allocations per request and fragmented the Wi-Fi heap.
static char g_jsonBuf[3072];

struct JsonOut {
  size_t len = 0;

  void ch(char c) {
    if (len + 1 < sizeof(g_jsonBuf)) g_jsonBuf[len++] = c;
  }
  void raw(const char *s) {
    while (*s) ch(*s++);
  }
  void u32(uint32_t v) {
    char tmp[10];
    uint8_t n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) ch(tmp[--n]);
  }
  void i32(int32_t v) {
    if (v < 0) { ch('-'); u32((uint32_t)(-(int64_t)v)); }
    else u32((uint32_t)v);
  }
  void str(const char *s) {
    ch('"');
    for (; *s; ++s) {
      char c = *s;
      if (c == '"' || c == '\\') { ch('\\'); ch(c); }
      else if ((uint8_t)c < 0x20) ch(' ');
      else ch(c);
    }
    ch('"');
  }
  void fmt(const char *f, ...) __attribute__((format(printf, 2, 3))) {
    size_t room = sizeof(g_jsonBuf) - len;
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(g_jsonBuf + len, room, f, ap);
    va_end(ap);
    if (r < 0) return;
    len += ((size_t)r < room) ? (size_t)r : room - 1;
  }
  void send() {
    g_jsonBuf[len] = '\0';
    server.send_P(200, "application/json", g_jsonBuf, len);
  }
};

static void startWebServerOnce() {
  if (g_webServerStarted) return;
  g_webServerStarted = true;

  server.enableCORS(true);

  server.on("/", HTTP_GET, [](){
    g_netStats.httpReqs++;
    server.send_P(200, "text/html; charset=utf-8", index_html);
  });

  server.on("/index.html", HTTP_GET, [](){
    g_netStats.httpReqs++;
    server.send_P(200, "text/html; charset=utf-8", index_html);
  });

  // Rapidly satisfy Safari / WebKit icon probes to prevent browser delays
  server.on("/favicon.ico", HTTP_GET, [](){
    server.send(204); // No Content
  });
  server.on("/apple-touch-icon.png", HTTP_GET, [](){
    server.send(204);
  });
  server.on("/apple-touch-icon-precomposed.png", HTTP_GET, [](){
    server.send(204);
  });

  server.on("/api/metrics", HTTP_GET, [](){
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Connection", "close");
    g_netStats.httpReqs++;
    g_netStats.activeWs = 1;

    float dc   = isfinite(g_metricsCh1.dc) ? g_metricsCh1.dc : 0.0f;
    float ac   = isfinite(g_metricsCh1.rms) ? g_metricsCh1.rms : 0.0f;
    float vpp  = isfinite(g_metricsCh1.vpp) ? g_metricsCh1.vpp : 0.0f;
    float freq = isfinite(g_metricsCh1.freq) ? g_metricsCh1.freq : 0.0f;
    float duty = isfinite(g_metricsCh1.duty) ? g_metricsCh1.duty : 0.0f;
    float trigV = isfinite(g_settings.trigVolts) ? g_settings.trigVolts : 1.65f;
    float batV  = isfinite(g_batVolts) ? g_batVolts : 0.0f;

    JsonOut j;
    j.fmt("{\"dc\":%.2f,\"ac\":%.2f,\"vpp\":%.2f,\"freq\":%.1f,\"duty\":%.1f,\"fs\":%.0f,",
          dc, ac, vpp, freq, duty, g_actualSampleRateHz);
    j.fmt("\"timeBaseIdx\":%u,\"timeBaseStr\":\"%s\",\"vDivIdx\":%u,\"vDivStr\":\"%s\",",
          g_settings.timeBaseIdx, dso::TIME_BASE_LABELS[g_settings.timeBaseIdx],
          g_settings.vDivIdx, dso::V_DIV_LABELS[g_settings.vDivIdx]);
    j.fmt("\"autoT\":%d,\"autoV\":%d,\"running\":%d,\"acCoupled\":%d,\"ch2Mode\":%u,"
          "\"probeIdx\":%u,\"probeBIdx\":%u,\"selfTest\":%d,\"trigMode\":%u,\"trigVolts\":%.2f,"
          "\"calFreqIdx\":%u,\"batVolts\":%.2f,\"batPct\":%u,",
          g_settings.autoTimebase ? 1 : 0, g_settings.autoVolts ? 1 : 0,
          g_settings.running ? 1 : 0, g_settings.acCoupled ? 1 : 0, g_settings.ch2Mode,
          g_settings.probeIdx, g_settings.probeBIdx, g_settings.selfTest ? 1 : 0,
          g_settings.trigMode, trigV, g_settings.calFreqIdx, batV, g_batPct);
    j.raw("\"ip\":");   j.str(g_netStats.ip);
    j.raw(",\"ssid\":"); j.str(g_netStats.ssid);
    j.fmt(",\"rssi\":%d,\"bars\":%u,", g_netStats.rssi, g_netStats.bars);
    j.raw("\"gw\":");   j.str(g_netStats.gw);
    j.raw(",\"mac\":"); j.str(g_netStats.mac);
    j.fmt(",\"clients\":1,\"httpReqs\":%lu}", (unsigned long)g_netStats.httpReqs);
    j.send();
  });

  server.on("/api/waveform", HTTP_GET, [](){
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Connection", "close");
    g_netStats.httpReqs++;
    g_netStats.wsFrames++;
    JsonOut j;
    j.raw("{\"trigY\":");   j.i32(g_trace.trigY - dso::CHART_Y);
    j.raw(",\"ch2Mode\":"); j.u32(g_settings.ch2Mode);
    j.raw(",\"ch1\":[");
    for (uint16_t i = 0; i < dso::LCD_W; ++i) {
      if (i > 0) j.ch(',');
      j.i32(g_trace.y1[i] - dso::CHART_Y);
    }
    j.ch(']');
    if (g_settings.ch2Mode == dso::CH2_ANALOG) {
      j.raw(",\"ch2\":[");
      for (uint16_t i = 0; i < dso::LCD_W; ++i) {
        if (i > 0) j.ch(',');
        j.i32(g_trace.y2[i] - dso::CHART_Y);
      }
      j.ch(']');
    } else if (g_settings.ch2Mode == dso::CH2_DIGITAL) {
      j.raw(",\"ch2\":[");
      for (uint16_t i = 0; i < dso::LCD_W; ++i) {
        if (i > 0) j.ch(',');
        j.ch(g_trace.d2[i] ? '1' : '0');
      }
      j.ch(']');
    }
    j.ch('}');
    j.send();
  });

  // Same FFT + zoom window the LCD shows: up to 256 max-pooled bins covering
  // [f0, f1), autoscaled 0..1000 to the largest bin. Peak "i" is the index
  // into "bins"; peaks outside the window are omitted.
  server.on("/api/fft", HTTP_GET, [](){
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Connection", "close");
    g_netStats.httpReqs++;
    ensureFft();
    const dso::FftWindow win = g_fftWin;
    const size_t winBins = win.width();
    const size_t outBins = (winBins < 256) ? winBins : 256;
    const float refMag = fmaxf(g_fft.maxMag, 1e-4f);
    const float toPermille = 1000.0f / refMag;
    JsonOut j;
    j.fmt("{\"peakFreq\":%.1f,\"peakMag\":%.4f,\"binHz\":%.3f,\"fs\":%.0f,\"thr\":%u,"
          "\"zoom\":%u,\"f0\":%.1f,\"f1\":%.1f,\"peaks\":[",
          g_fftPeakFreq, g_fft.peakMag, g_fft.binHz * (float)winBins / (float)outBins,
          g_actualSampleRateHz, g_fftPeakThrPct, g_settings.fftZoomIdx,
          (float)win.b0 * g_fft.binHz, (float)win.b1 * g_fft.binHz);
    bool first = true;
    for (size_t k = 0; k < g_fftPeakCount; ++k) {
      const size_t bin = g_fftPeaks[k].bin;
      if (bin < win.b0 || bin >= win.b1) continue;
      if (!first) j.ch(',');
      first = false;
      float f = (k == 0) ? g_fftPeakFreq : g_fftPeaks[k].freq;
      j.fmt("{\"f\":%.1f,\"m\":%ld,\"i\":%u}", f,
            lroundf(fminf(1000.0f, g_fftPeaks[k].mag * toPermille)),
            (unsigned)(((bin - win.b0) * outBins) / winBins));
    }
    j.raw("],\"bins\":[");
    for (size_t o = 0; o < outBins; ++o) {
      size_t b0 = win.b0 + (o * winBins) / outBins;
      size_t b1 = win.b0 + ((o + 1) * winBins) / outBins;
      if (b0 < 1) b0 = 1; // skip DC bin
      float m = 0.0f;
      for (size_t b = b0; b < b1 && b < dso::FFT_BINS; ++b) m = fmaxf(m, g_fft.bins[b]);
      if (o > 0) j.ch(',');
      j.i32((int32_t)(fminf(1000.0f, m * toPermille) + 0.5f));
    }
    j.raw("]}");
    j.send();
  });

  server.on("/api/set", HTTP_ANY, [](){
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.sendHeader("Connection", "close");
    if (server.method() == HTTP_OPTIONS) {
      server.send(204);
      return;
    }
    g_netStats.httpReqs++;
    uint32_t fx = dso::FX_NONE;
    for (int i = 0; i < server.args(); ++i) {
      String key = server.argName(i);
      String val = server.arg(i);
      if (key == "fftThr") {
        g_fftPeakThrPct = (uint8_t)constrain(val.toInt(), 1L, 100L);
        g_fftSeq = UINT32_MAX; // re-run peak detection with the new threshold
        continue;
      }
      fx |= dso::applyParam(g_settings, key.c_str(), val.c_str());
      Serial.printf("[API] Set %s = %s (ch2Mode=%d, autoT=%d, autoV=%d)\n",
                    key.c_str(), val.c_str(), g_settings.ch2Mode, g_settings.autoTimebase, g_settings.autoVolts);
    }
    if (fx & dso::FX_CAL) updateCalibrationGenerator();
    if (fx & dso::FX_BACKLIGHT) updateBacklight();
    server.send(200, "text/plain", "OK");
    if (fx & dso::FX_RECONNECT) connectWifi();
  });

  server.onNotFound([](){
    if (server.method() == HTTP_GET) {
      g_netStats.httpReqs++;
      server.send_P(200, "text/html; charset=utf-8", index_html);
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });

  server.begin();
  Serial.printf("[WiFi] Web UI available at: http://%s\n", g_netStats.ip);
}
#endif

static void runLcdDiagnostic() {
  // Step 2: Test the 320x172 landscape canvas (gfx) flushing into the working 172x320 panel
  const int16_t w = dso::LCD_W; // 320
  const int16_t h = dso::LCD_H; // 172

  gfx->fillScreen(RGB565_BLACK);

  // Top & bottom bar bands (like the scope layout)
  gfx->fillRect(0, 0, w, 24, 0x10A3);
  gfx->fillRect(0, h - 38, w, 38, 0x10A3);

  // Outer 1-px white frame around 320x172
  gfx->drawRect(0, 0, w, h, 0xFFFF);

  // Diagonal X across the full 320x172 landscape frame: RED (\) and GREEN (/)
  gfx->drawLine(0, 0, w - 1, h - 1, 0xF800);
  gfx->drawLine(0, 1, w - 2, h - 1, 0xF800);
  gfx->drawLine(w - 1, 0, 0, h - 1, 0x07E0);
  gfx->drawLine(w - 2, 0, 0, h - 2, 0x07E0);

  // Corner squares: TL=Red, TR=Green, BL=Blue, BR=Yellow
  gfx->fillRect(2,      2,      12, 12, 0xF800);
  gfx->fillRect(w - 14, 2,      12, 12, 0x07E0);
  gfx->fillRect(2,      h - 14, 12, 12, 0x001F);
  gfx->fillRect(w - 14, h - 14, 12, 12, 0xFFE0);

  gfx->setTextColor(0xFFFF);
  gfx->setTextSize(2);
  gfx->setCursor(60, h / 2 - 8);
  gfx->print("STEP 2: 320x172");

  gfx->flush();
}

void setup() {
  Serial.begin(115200);

  // 1. Deselect TF Card to prevent SPI bus conflicts
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  // 2. Initialize JD9853 LCD (ST7789-compatible) and turn the backlight on
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

#if LCD_DIAG_TEST
  // Step 2: Initialize gfx canvas + panel at 20 MHz and flush one landscape test frame
  gfx->begin(20000000);
  initJD9853Registers();
  panel->setRotation(0);
  panel->invertDisplay(true);
  runLcdDiagnostic();
  Serial.println("[DIAG] Step 2: 320x172 canvas flushed to panel.");
  return;
#endif

  // Use the verified 20 MHz SPI clock + JD9853 init + rotation 0 + inversion
  gfx->begin(20000000);
  initJD9853Registers();
  panel->setRotation(0);          // native 172 x 320 window, column offset 34
  panel->invertDisplay(true);     // JD9853 shows a negative image until this
  panel->fillScreen(RGB565_BLACK);

  // 3. Initialize Touch Controller
  initTouchAXS5106L();

  // 4. Initialize ADC & Cal Generator & Backlight
  setupADC();
  loadSettings();
  updateCalibrationGenerator();
  updateBacklight();

  // 5. Capture and render the first oscilloscope frame immediately
  captureWaveforms();
  g_metricsCh1 = dso::analyze(g_ch1Volts, dso::CAPTURE_SAMPLES, g_actualSampleRateHz, dso::HYST_VOLTS[g_settings.hystIdx]);
  renderScopeScreen();

  Serial.println("PocketDSO v" DSO_FW_VERSION " (262K Color IPS) Ready!");

#if ENABLE_WEB_UI && WIFI_AUTO_CONNECT_AT_BOOT
  if (strcmp(WIFI_SSID, "YOUR_SSID") != 0) {
    Serial.println("[WiFi] Auto-connecting to Wi-Fi on boot...");
    connectWifi();
  }
#endif
}

void loop() {
#if LCD_DIAG_TEST
  delay(500);
  return;
#endif

#if ENABLE_WEB_UI
  if (g_webServerStarted) {
    server.handleClient();
  }
#endif

  uint32_t now = millis();

  // 1. Poll touch input at 100 Hz (each poll is an I2C transaction; polling on
  //    every loop pass only burned CPU and bus time without improving response)
  static uint32_t lastTouchMs = 0;
  if (now - lastTouchMs >= 10) {
    lastTouchMs = now;
    handleTouchInput();
  }

  // 2. Battery telemetry check (every 2000 ms via GPIO0)
  static uint32_t lastBatMs = 0;
  if (now - lastBatMs >= 2000) {
    lastBatMs = now;
    uint16_t rawBat = analogRead(PIN_BAT_ADC);
    float vBat = (rawBat * 3.3f / 4095.0f) * 3.0f;
    if (g_batVolts < 0.1f) g_batVolts = vBat;
    else g_batVolts = 0.85f * g_batVolts + 0.15f * vBat;

    if (g_batVolts >= dso::BATTERY_PRESENT_MIN_V) {
      g_batPct = dso::batteryPercent(g_batVolts);
    } else {
      g_batPct = 0; // USB powered
    }

#if ENABLE_WEB_UI
    if (g_netStats.connected && WiFi.status() == WL_CONNECTED) {
      g_netStats.rssi = WiFi.RSSI();
      g_netStats.bars = dso::rssiToBars(g_netStats.rssi);
      g_netStats.quality = dso::rssiToQuality(g_netStats.rssi);
    }
#endif
  }

  // 3. Acquire ADC burst and render display once per 40 ms frame (~25 FPS)
  static uint32_t lastFrameMs = 0;
  if (now - lastFrameMs >= 40) {
    lastFrameMs = now;
    bool captured = false;

    if (g_settings.running && g_ui.screen != dso::SCR_SETTINGS) {
      captured = true;
      captureWaveforms();
      g_metricsCh1 = dso::analyze(g_ch1Volts, dso::CAPTURE_SAMPLES, g_actualSampleRateHz, dso::HYST_VOLTS[g_settings.hystIdx]);
      if (g_settings.ch2Mode != dso::CH2_OFF) {
        g_metricsCh2 = dso::analyze(g_ch2Volts, dso::CAPTURE_SAMPLES, g_actualSampleRateHz, dso::HYST_VOLTS[g_settings.hystIdx]);
      }
      dso::autoScale(g_settings, g_metricsCh1);

      // Single-shot freeze
      if (g_settings.trigMode == dso::TRIG_SINGLE && g_trace.triggered) {
        g_settings.running = false;
      }
    }

    // Skip redundant redraws (a full-frame SPI flush takes ~44 ms at 20 MHz):
    // redraw on new data or any visible state change, else refresh every 500 ms
    // so battery / RSSI / web-side changes still appear.
    static dso::Settings lastDrawn;
    static uint8_t  lastScreen = 0xFF, lastTab = 0xFF;
    static bool     lastArmed = false;
    static uint32_t lastRenderMs = 0;
    const bool armed = (g_ui.resetArmedUntil > now);
    const bool needRender = captured || g_ui.screen != lastScreen || g_ui.tab != lastTab ||
                            armed != lastArmed || !dso::settingsEqual(lastDrawn, g_settings) ||
                            (now - lastRenderMs >= 500);
    if (needRender) {
      if (g_ui.screen == dso::SCR_SETTINGS) {
        renderSettingsModal();
      } else if (g_ui.screen == dso::SCR_FFT) {
        renderFftScreen();
      } else {
        renderScopeScreen();
      }
      lastDrawn    = g_settings;
      lastScreen   = g_ui.screen;
      lastTab      = g_ui.tab;
      lastArmed    = armed;
      lastRenderMs = now;
    }
#if ENABLE_WEB_UI
    if (g_webServerStarted) {
      server.handleClient();
    }
#endif
  }
}
