# PocketDSO — Unified Test Suite

This directory contains the complete verification suite for PocketDSO, covering host-based C++ DSP unit testing, offline Web UI simulation, and remote hardware-in-the-loop (HITL) end-to-end testing over Wi-Fi.

---

## 1. Test Architecture Overview

| Test Layer | Target | Files | Runtime Requirements |
|:---|:---|:---|:---|
| **Host Unit Tests** | DSP algorithms, FFT, auto-scaling, settings NVS serialization, touch hit-testing | `test_core.cpp` | Clang++ or G++ (`C++17`) |
| **Offline Web UI Simulation** | Frontend HTML5 canvas, WebSocket protocol, responsive layout | `test_web_server.js`, `web_ui_test.html` | Node.js (Zero npm dependencies) |
| **Hardware-in-the-Loop E2E** | Live ESP32-C6 firmware, REST API, state persistence across reboot, measurement accuracy, UI automation | `dso_remote.js`, `e2e_01_*.js`, `e2e_02_*.js`, `e2e_03_*.js` | Node.js >= 18 (Zero npm dependencies), Device on Wi-Fi |

---

## 2. Host-Based C++ Unit Tests (`test_core.cpp`)

Validates core DSP math and logic on your desktop computer without flashing or wiring hardware:

- **Signal Metrics**: Frequency estimation, true AC RMS, peak-to-peak ($V_{pp}$), DC average, pulse duty cycle.
- **FFT Math**: Cooley-Tukey 256-point Radix-2 FFT, Hann windowing, bin resolution, dominant peak detection.
- **Auto-Scale Heuristics**: Automatic timebase and vertical voltage scale step calculations.
- **Touch Geometry**: Bounding box hit-testing for top bar, bottom control buttons, and modal settings tabs.
- **NVS Configuration**: Binary struct packing, CRC validation, and settings version migration.

### Run Unit Tests
```bash
# From repository root
clang++ -std=c++17 -I"oscilloscope" test/test_core.cpp -o test/test_core
./test/test_core
```

---

## 3. Offline Web UI Simulation

Allows testing the browser-based CRT phosphor display and controls on your desktop without an ESP32:

```bash
# Start local mock WebSocket / HTTP server
node test/test_web_server.js
# Open http://localhost:8080 in your browser
```

---

## 4. End-to-End Hardware-in-the-Loop Test Suite

Remote automated test suite that exercises physical PocketDSO hardware over Wi-Fi through its HTTP REST API. All measurements use the built-in deterministic loopback generator (`selfTest=1`), so no external signal generator or jumper wiring is required.

### Requirements
- Node.js >= 18 (uses built-in `fetch`, no npm packages required)
- PocketDSO powered and connected to Wi-Fi (`pocketdso.local` or known IP address)

### Suites
| Suite | What It Proves |
|:---|:---|
| `e2e_01_settings_persistence.js` | Complete schema verification of `/api/state`; validates `/api/set` dynamic tuning; verifies that settings persist across `/api/reboot` (both explicit `/api/save` and 1.5s debounced auto-save); verifies default factory restore. |
| `e2e_02_measurement.js` | Internal loopback validation: $1.00\text{ kHz}$ fundamental frequency, duty cycle, $V_{pp}$, and DC level tolerances; auto horizontal/vertical scale selection; $10\text{ kHz}$ accuracy; CH2 dual-trace and logic analyzer readouts; AC coupling RMS isolation. |
| `e2e_03_ui_tour.js` | Programmatic UI navigation: visits every screen, tab, and toggle via `/api/tap`, verifying returned `ACT_*` action IDs; captures screenshot visual evidence into `test/captures/` for human inspection. |

### Run E2E Tests
```bash
cd test

# 1. State schema, remote configuration & NVS reboot persistence
node e2e_01_settings_persistence.js [--host pocketdso.local]

# 2. Acquisition accuracy & auto-scaling
node e2e_02_measurement.js          [--host pocketdso.local]

# 3. Touchscreen UI navigation tour & screenshot evidence capture
node e2e_03_ui_tour.js              [--host pocketdso.local] [--out captures]
```

Each test exits with code `0` on success and `1` on failure.

---

## 5. Firmware Release Checklist

Run this checklist before tagging any firmware release:

1. `clang++ -std=c++17 -I"oscilloscope" test/test_core.cpp -o test/test_core && ./test/test_core`
2. `node test/e2e_01_settings_persistence.js`
3. `node test/e2e_02_measurement.js`
4. `node test/e2e_03_ui_tour.js`
5. Inspect generated screenshots in `test/captures/*.png` (verify battery/USB icon, text alignment, FFT frequency labels).
6. Bump `SETTINGS_VERSION` in `oscilloscope/dso_core.h` if the `Settings` struct layout has changed.

