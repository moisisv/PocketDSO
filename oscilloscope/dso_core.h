/*******************************************************************************
 * dso_core.h — PocketDSO platform-independent core logic
 * =============================================================================
 * Everything in this header is pure C++ (no Arduino / ESP-IDF dependencies) so
 * that it can be compiled and unit-tested on a desktop host
 * (see test/host/test_core.cpp) AND compiled into the ESP32-C6 firmware.
 *
 * Contents:
 *   - Display geometry & UI layout constants (shared by renderer + hit-testing)
 *   - Scale tables (time/div, volts/div, sample rate, probe, cal generator ...)
 *   - Settings struct + validation / comparison
 *   - Signal analysis (DC, AC RMS, min/max, Vpp, frequency, duty cycle)
 *   - Auto-scale (auto timebase with low-frequency "hunting", auto volts,
 *     auto trigger level)
 *   - Trigger search with 10 % pre-trigger and bounds-safe resampling
 *   - Volts <-> pixel mapping, screen trace builder
 *   - Battery LiPo state-of-charge curve
 *   - Human-readable formatters
 *   - Touch UI state machine: hit-testing + action dispatch
 *   - Web/WebSocket parameter parser ("key=value&key2=value2")
 *
 * NOTE: Arduino defines function-like macros such as min/max/abs/constrain/
 * round and constants such as RISING/FALLING/B1. None of those identifiers are
 * used here on purpose.
 ******************************************************************************/
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define DSO_FW_VERSION "2.2.0"

namespace dso {

// =============================================================================
// 1. GEOMETRY (landscape 320 x 172)
// =============================================================================
constexpr int16_t LCD_W     = 320;
constexpr int16_t LCD_H     = 172;
constexpr int16_t TOP_BAR_H = 24;
constexpr int16_t BOT_BAR_H = 38;
constexpr int16_t CHART_Y   = TOP_BAR_H;
constexpr int16_t CHART_H   = LCD_H - TOP_BAR_H - BOT_BAR_H; // 110 px
constexpr int16_t GRID_COLS = 10;
constexpr int16_t GRID_ROWS = 6;

constexpr size_t   CAPTURE_SAMPLES  = 1024;
constexpr uint16_t WEB_POINTS       = 320;
constexpr float    PRE_TRIGGER_FRAC = 0.10f; // trigger point sits 1 div from the left

// --- Main screen layout ---
// The glass has rounded corners and the bezel hides the top rows, so the top
// bar controls keep SAFE_X px from the side edges and start at TOP_BTN_Y.
constexpr int16_t SAFE_X     = 10;
constexpr int16_t TOP_BTN_Y  = 4, TOP_BTN_H = 18;
constexpr int16_t AUTO_BADGE_X = SAFE_X, AUTO_BADGE_W = 34;
constexpr int16_t FFT_BTN_X   = 166, FFT_BTN_W = 32;
constexpr int16_t BAT_ZONE_X  = 200, BAT_ZONE_W = 24; // tap -> power / sys page
constexpr int16_t WIFI_ZONE_X = 226, WIFI_ZONE_W = 26; // tap -> network page
constexpr int16_t MENU_BTN_X  = LCD_W - SAFE_X - 54, MENU_BTN_Y = TOP_BTN_Y, MENU_BTN_W = 54, MENU_BTN_H = TOP_BTN_H;
constexpr int16_t STATUS_ICONS_X = BAT_ZONE_X;
constexpr int16_t FFT_BACK_BTN_X = SAFE_X, FFT_BACK_BTN_W = 56; // FFT screen "< SCOPE"
constexpr int16_t BOT_BTN_Y = LCD_H - BOT_BAR_H + 2;
constexpr int16_t BOT_BTN_H = 34;
constexpr int16_t BOT_BTN_X[4] = { 6, 84, 162, 240 };
constexpr int16_t BOT_BTN_W[4] = { 74, 74, 74, 74 };
// Touch boundaries between the bottom buttons (midpoints of the gaps)
constexpr int16_t BOT_SPLIT[3] = { 82, 160, 238 };

// --- Settings modal layout ---
constexpr int16_t TAB_H      = 24;
constexpr int16_t TAB_X0     = SAFE_X;
constexpr int16_t TAB_W      = 47;  // pitch; drawn 44 wide
constexpr int16_t TAB_DRAW_W = 44;
constexpr int16_t DONE_BTN_X = 250, DONE_BTN_W = LCD_W - SAFE_X - 250;
constexpr int16_t TILE_COL_X[2] = { 6, 162 };
constexpr int16_t TILE_ROW_Y[3] = { 30, 76, 122 };
constexpr int16_t TILE_W = 152;
constexpr int16_t TILE_H = 42;
constexpr int16_t NET_BTN_X = 204, NET_BTN_Y = 134, NET_BTN_W = 102, NET_BTN_H = 28;

// =============================================================================
// COLOR PALETTE (RGB565 tailored for 262K-Color JD9853 IPS Display)
// =============================================================================
namespace colors {
  constexpr uint16_t BG_DARK        = 0x0821; // Deep obsidian blue #080C14
  constexpr uint16_t BG_PANEL       = 0x10A3; // Dark slate panel #101726
  constexpr uint16_t BG_CARD        = 0x18E5; // Elevated surface #182234
  constexpr uint16_t GRID_MINOR     = 0x1945; // Subtle reticle line #182838
  constexpr uint16_t GRID_MAJOR     = 0x31E9; // Crosshair center line #304258
  constexpr uint16_t GRID_TICK      = 0x4A6D; // Center axis tick mark #485A74

  // Neon Signal Traces (high-contrast for 262K IPS screen)
  constexpr uint16_t CH1_TRACE      = 0xFFE0; // Neon Electric Yellow #FFE600
  constexpr uint16_t CH1_GLOW       = 0xCE60; // Secondary yellow glow
  constexpr uint16_t CH2_TRACE      = 0x07FF; // Neon Cyan / Aqua #00F0FF
  constexpr uint16_t CH2_GLOW       = 0x0418; // Secondary cyan glow
  constexpr uint16_t DIG_D1         = 0x07E0; // Neon Emerald Mint Green #00FF88
  constexpr uint16_t TRIG_LINE      = 0xF81F; // Vivid Magenta #FF00FF
  constexpr uint16_t TRIG_MARKER    = 0xFC18; // Bright Hot Pink #FF2D85

  // Status Badges & Accents
  constexpr uint16_t BADGE_RUN      = 0x05E8; // Vibrant Emerald Green #00BD66
  constexpr uint16_t BADGE_HOLD     = 0xE9A6; // Crimson Red #EA3838
  constexpr uint16_t BADGE_AUTO     = 0x04FF; // Electric Blue #009DFF
  constexpr uint16_t BADGE_WIFI     = 0x3D3F; // Sky Blue #38AAFF
  constexpr uint16_t BADGE_WARN     = 0xFD20; // Amber Alert #FF9900
  constexpr uint16_t BADGE_LOOP     = 0xD380; // Loopback Gold #D27B00
  // FFT Spectrum Modern Electric Blue Palette (matching user reference)
  constexpr uint16_t FFT_BAR_CAP    = 0x963F; // Luminous Sky Blue cap #93C5FD
  constexpr uint16_t FFT_BAR_TOP    = 0x653F; // Vibrant Electric Blue #60A5FA
  constexpr uint16_t FFT_BAR_MID    = 0x24BD; // Deep Cobalt Blue #2563EB
  constexpr uint16_t FFT_BAR_BOT    = 0x1172; // Deep Navy #162C54
  constexpr uint16_t VIOLET         = 0x7A5F; // Electric Violet #7C4DFF
  constexpr uint16_t MAGENTA        = 0xE21E; // Vivid Magenta #E040FB
  constexpr uint16_t CYAN           = 0x07FF; // Neon Cyan #00E5FF

  // UI Buttons & Tiles
  constexpr uint16_t BTN_DARK       = 0x2126; // Button background
  constexpr uint16_t BTN_BORDER     = 0x4A8D; // Subtle button border
  constexpr uint16_t BTN_ACTIVE     = 0x1B0E; // Active tile fill
  constexpr uint16_t BORDER_ACTIVE  = 0x067E; // Glowing border

  // Text Colors
  constexpr uint16_t TEXT_WHITE     = 0xFFFF; // Crisp Pure White #FFFFFF
  constexpr uint16_t TEXT_MUTED     = 0x9CD3; // Light Slate Grey #9BA4B2
  constexpr uint16_t TEXT_DIM       = 0x5AEB; // Dim Grey #5B6370
}

// Legacy color aliases for backwards compatibility
constexpr uint16_t COL_BG          = colors::BG_DARK;
constexpr uint16_t COL_BAR_BG      = colors::BG_PANEL;
constexpr uint16_t COL_GRID_MINOR  = colors::GRID_MINOR;
constexpr uint16_t COL_GRID_MAJOR  = colors::GRID_MAJOR;
constexpr uint16_t COL_CH1         = colors::CH1_TRACE;
constexpr uint16_t COL_CH2         = colors::CH2_TRACE;
constexpr uint16_t COL_TRIG        = colors::TRIG_LINE;
constexpr uint16_t COL_BTN_AUTO    = colors::BADGE_AUTO;
constexpr uint16_t COL_BTN_DARK    = colors::BTN_DARK;
constexpr uint16_t COL_BTN_RUN     = colors::BADGE_RUN;
constexpr uint16_t COL_BTN_HOLD    = colors::BADGE_HOLD;
constexpr uint16_t COL_TILE_ON     = colors::BTN_ACTIVE;
constexpr uint16_t COL_BORDER_ON   = colors::BORDER_ACTIVE;
constexpr uint16_t COL_BORDER_DIM  = colors::BTN_BORDER;
constexpr uint16_t COL_TEXT_WHITE  = colors::TEXT_WHITE;
constexpr uint16_t COL_TEXT_MUTED  = colors::TEXT_MUTED;

// =============================================================================
// 2. SCALE TABLES
// =============================================================================
constexpr float TIME_BASES_SEC[] = {
  20e-6f, 50e-6f, 100e-6f, 200e-6f, 500e-6f,
  1e-3f,  2e-3f,  5e-3f,   10e-3f,  20e-3f,  50e-3f
};
constexpr const char* TIME_BASE_LABELS[] = {
  "20us", "50us", "100us", "200us", "500us",
  "1ms",  "2ms",  "5ms",   "10ms",  "20ms",  "50ms"
};
constexpr uint8_t NUM_TIME_BASES = sizeof(TIME_BASES_SEC) / sizeof(TIME_BASES_SEC[0]);
constexpr uint8_t TB_DEFAULT_IDX = 3;  // 200 us/div
constexpr uint8_t TB_DC_IDX      = 5;  // 1 ms/div when no signal
constexpr uint8_t TB_HUNT_MAX    = NUM_TIME_BASES - 2; // auto-hunting never goes beyond 20 ms/div

// 5 V and 10 V/div are needed for the 10x probe (0..33 V range)
constexpr float V_DIVS[] = { 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f };
constexpr const char* V_DIV_LABELS[] = { "0.1V", "0.2V", "0.5V", "1V", "2V", "5V", "10V" };
constexpr uint8_t NUM_V_DIVS = sizeof(V_DIVS) / sizeof(V_DIVS[0]);

// Target (maximum) sample rates. The real rate is measured every capture and
// reported as "Fs" because analogRead() timing limits the achievable rate.
constexpr uint32_t SAMPLE_RATES_HZ[] = { 50000, 100000, 200000, 250000 };
constexpr const char* SAMPLE_RATE_LABELS[] = { "50kS/s", "100kS/s", "200kS/s", "250kS/s" };
constexpr uint8_t NUM_SAMPLE_RATES = sizeof(SAMPLE_RATES_HZ) / sizeof(SAMPLE_RATES_HZ[0]);

constexpr float PROBE_SCALES[] = { 1.0f, 2.0f, 4.0f, 10.0f };
constexpr const char* PROBE_LABELS[] = { "1x 3.3V", "2x 6.6V", "4x 13.2V", "10x 33V" };
constexpr uint8_t NUM_PROBES = sizeof(PROBE_SCALES) / sizeof(PROBE_SCALES[0]);
constexpr uint8_t PROBE_CH1_DEFAULT_IDX = 1; // 2x (0..6.6V max) default for Probe A (<5.5V high accuracy)
constexpr uint8_t PROBE_CH2_DEFAULT_IDX = 2; // 4x (0..13.2V max) default for Probe B (12V rail measurement)

constexpr uint32_t CAL_FREQS_HZ[] = { 0, 1000, 10000, 50000 };
constexpr const char* CAL_FREQ_LABELS[] = { "OFF", "1kHz", "10kHz", "50kHz" };
constexpr uint8_t NUM_CAL_FREQS = sizeof(CAL_FREQS_HZ) / sizeof(CAL_FREQS_HZ[0]);

constexpr float HYST_VOLTS[] = { 0.02f, 0.05f, 0.10f, 0.20f };
constexpr const char* HYST_LABELS[] = { "20mV", "50mV", "100mV", "200mV" };
constexpr uint8_t NUM_HYST = sizeof(HYST_VOLTS) / sizeof(HYST_VOLTS[0]);

constexpr uint8_t BRIGHTNESS_PCT[] = { 15, 40, 70, 100 };
constexpr const char* BRIGHTNESS_LABELS[] = { "15%", "40%", "70%", "100%" };
constexpr uint8_t NUM_BRIGHTNESS = sizeof(BRIGHTNESS_PCT) / sizeof(BRIGHTNESS_PCT[0]);

// FFT X-axis zoom: AUTO fits the detected peaks, FULL shows 0..Nyquist,
// Nx shows 1/N of the span centered on the strongest peak.
enum FftZoom : uint8_t { FFT_ZOOM_AUTO = 0, FFT_ZOOM_FULL = 1, FFT_ZOOM_X2, FFT_ZOOM_X4, FFT_ZOOM_X8, NUM_FFT_ZOOMS };
constexpr const char* FFT_ZOOM_LABELS[] = { "AUTO", "FULL", "2x", "4x", "8x" };

enum Ch2Mode : uint8_t { CH2_OFF = 0, CH2_ANALOG = 1, CH2_DIGITAL = 2, CH2_MODE_COUNT = 3 };
constexpr const char* CH2_MODE_LABELS[] = { "OFF", "Analog", "Digital" };

enum TrigMode : uint8_t { TRIG_RISING = 0, TRIG_FALLING = 1, TRIG_FREE = 2, TRIG_SINGLE = 3, TRIG_MODE_COUNT = 4 };
constexpr const char* TRIG_MODE_LABELS[] = { "Rising", "Falling", "Free run", "Single" };

// =============================================================================
// 3. SETTINGS
// =============================================================================
struct Settings {
  float   trigVolts     = 1.65f;
  uint8_t timeBaseIdx   = TB_DEFAULT_IDX;
  uint8_t vDivIdx       = 2;      // 0.5 V/div
  uint8_t sampleRateIdx = 2;      // 200 kS/s target
  uint8_t probeIdx      = PROBE_CH1_DEFAULT_IDX; // Probe A (CH1) default: 2x (0..6.6V max)
  uint8_t probeBIdx     = PROBE_CH2_DEFAULT_IDX; // Probe B (CH2) default: 4x (0..13.2V max)
  uint8_t calFreqIdx    = 1;      // 1 kHz on GPIO7
  uint8_t ch2Mode       = CH2_ANALOG;
  uint8_t trigMode      = TRIG_RISING;
  uint8_t hystIdx       = 1;      // 50 mV
  uint8_t brightnessIdx = NUM_BRIGHTNESS - 1;
  uint8_t fftSpanIdx    = 1;      // 50 kHz default Nyquist span
  bool    fftLogScale   = true;   // true: Log dB magnitude, false: Linear magnitude
  uint8_t fftZoomIdx    = FFT_ZOOM_AUTO;
  bool    autoTimebase  = true;
  bool    autoVolts     = true;
  bool    running       = true;
  bool    acCoupled     = false;
  bool    selfTest      = false;
  bool    userSetTrig   = false;
};

inline bool settingsEqual(const Settings& a, const Settings& b) {
  return a.trigVolts == b.trigVolts && a.timeBaseIdx == b.timeBaseIdx &&
         a.vDivIdx == b.vDivIdx && a.sampleRateIdx == b.sampleRateIdx &&
         a.probeIdx == b.probeIdx && a.probeBIdx == b.probeBIdx &&
         a.calFreqIdx == b.calFreqIdx && a.ch2Mode == b.ch2Mode &&
         a.trigMode == b.trigMode && a.hystIdx == b.hystIdx &&
         a.brightnessIdx == b.brightnessIdx &&
         a.fftSpanIdx == b.fftSpanIdx && a.fftLogScale == b.fftLogScale &&
         a.fftZoomIdx == b.fftZoomIdx &&
         a.autoTimebase == b.autoTimebase && a.autoVolts == b.autoVolts &&
         a.running == b.running && a.acCoupled == b.acCoupled &&
         a.selfTest == b.selfTest && a.userSetTrig == b.userSetTrig;
}

// Clamp every field into range (protects against corrupted NVS data).
// Returns true if anything had to be fixed.
inline bool validateSettings(Settings& s) {
  bool fixed = false;
  auto fixIdx = [&fixed](uint8_t& v, uint8_t count, uint8_t def) {
    if (v >= count) { v = def; fixed = true; }
  };
  const Settings d;
  fixIdx(s.timeBaseIdx, NUM_TIME_BASES, d.timeBaseIdx);
  fixIdx(s.vDivIdx, NUM_V_DIVS, d.vDivIdx);
  fixIdx(s.sampleRateIdx, NUM_SAMPLE_RATES, d.sampleRateIdx);
  fixIdx(s.probeIdx, NUM_PROBES, PROBE_CH1_DEFAULT_IDX);
  fixIdx(s.probeBIdx, NUM_PROBES, PROBE_CH2_DEFAULT_IDX);
  fixIdx(s.calFreqIdx, NUM_CAL_FREQS, d.calFreqIdx);
  fixIdx(s.ch2Mode, CH2_MODE_COUNT, d.ch2Mode);
  fixIdx(s.trigMode, TRIG_MODE_COUNT, d.trigMode);
  fixIdx(s.hystIdx, NUM_HYST, d.hystIdx);
  fixIdx(s.brightnessIdx, NUM_BRIGHTNESS, d.brightnessIdx);
  fixIdx(s.fftSpanIdx, NUM_SAMPLE_RATES, d.fftSpanIdx);
  fixIdx(s.fftZoomIdx, NUM_FFT_ZOOMS, d.fftZoomIdx);
  if (!isfinite(s.trigVolts) || fabsf(s.trigVolts) > 100.0f) { s.trigVolts = d.trigVolts; fixed = true; }
  return fixed;
}

// --- Versioned, checksummed byte serialization for NVS persistence ---------
// (Raw struct dumps are fragile: padding, bool representation, field order.)
constexpr size_t  SETTINGS_BLOB_SIZE = 24;
constexpr uint8_t SETTINGS_MAGIC     = 0xD5;
constexpr uint8_t SETTINGS_VERSION   = 3;

inline uint8_t blobChecksum(const uint8_t* p, size_t n) {
  uint8_t c = 0x5A;
  for (size_t i = 0; i < n; ++i) c = (uint8_t)((c << 1) | (c >> 7)) ^ p[i];
  return c;
}

inline void serializeSettings(const Settings& s, uint8_t out[SETTINGS_BLOB_SIZE]) {
  memset(out, 0, SETTINGS_BLOB_SIZE);
  out[0] = SETTINGS_MAGIC;
  out[1] = SETTINGS_VERSION;
  memcpy(&out[2], &s.trigVolts, 4);
  out[6]  = s.timeBaseIdx;  out[7]  = s.vDivIdx;   out[8]  = s.sampleRateIdx;
  out[9]  = s.probeIdx;     out[10] = s.calFreqIdx; out[11] = s.ch2Mode;
  out[12] = s.trigMode;     out[13] = s.hystIdx;   out[14] = s.brightnessIdx;
  out[15] = (uint8_t)((s.autoTimebase ? 1 : 0) | (s.autoVolts ? 2 : 0) | (s.acCoupled ? 4 : 0) |
                      (s.selfTest ? 8 : 0) | (s.userSetTrig ? 16 : 0));
  out[16] = s.probeBIdx;
  out[17] = s.fftSpanIdx;
  out[18] = (uint8_t)(s.fftLogScale ? 1 : 0);
  out[19] = s.fftZoomIdx; // older blobs have 0 here = AUTO
  out[SETTINGS_BLOB_SIZE - 1] = blobChecksum(out, SETTINGS_BLOB_SIZE - 1);
}

// Returns false (and leaves `s` untouched) if the blob is missing/corrupt.
// `running` is intentionally not persisted: the scope always boots in RUN.
inline bool deserializeSettings(const uint8_t* in, size_t len, Settings& s) {
  if (in == nullptr || len != SETTINGS_BLOB_SIZE) return false;
  if (in[0] != SETTINGS_MAGIC || in[1] != SETTINGS_VERSION) return false;
  if (in[SETTINGS_BLOB_SIZE - 1] != blobChecksum(in, SETTINGS_BLOB_SIZE - 1)) return false;
  Settings t;
  memcpy(&t.trigVolts, &in[2], 4);
  t.timeBaseIdx = in[6];  t.vDivIdx = in[7];     t.sampleRateIdx = in[8];
  t.probeIdx = in[9];     t.calFreqIdx = in[10]; t.ch2Mode = in[11];
  t.trigMode = in[12];    t.hystIdx = in[13];    t.brightnessIdx = in[14];
  t.autoTimebase = (in[15] & 1) != 0;  t.autoVolts   = (in[15] & 2) != 0;
  t.acCoupled    = (in[15] & 4) != 0;  t.selfTest    = (in[15] & 8) != 0;
  t.userSetTrig  = (in[15] & 16) != 0;
  t.probeBIdx    = in[16];
  t.fftSpanIdx   = in[17];
  t.fftLogScale  = (in[18] != 0);
  t.fftZoomIdx   = in[19];
  t.running = true;
  if (t.trigMode == TRIG_SINGLE) t.trigMode = TRIG_RISING; // never boot "armed"
  validateSettings(t);
  s = t;
  return true;
}

// =============================================================================
// 4. SMALL MATH HELPERS (macro-safe names)
// =============================================================================
inline float   clampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int32_t clampI(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float   fullScaleVolts(const Settings& s) { return V_DIVS[s.vDivIdx] * (float)GRID_ROWS; }
inline float   probeMaxVolts(const Settings& s, uint8_t ch = 0) {
  uint8_t idx = (ch == 1) ? s.probeBIdx : s.probeIdx;
  return 3.3f * PROBE_SCALES[idx];
}

// =============================================================================
// 5. SIGNAL ANALYSIS
// =============================================================================
struct Metrics {
  float dc   = 0;   // mean (V)
  float rms  = 0;   // AC RMS = std-dev around the mean (V)
  float vmin = 0;
  float vmax = 0;
  float vpp  = 0;
  float freq = 0;   // Hz, 0 = not detected
  float duty = -1;  // %, -1 = not available
};

constexpr float MIN_SIGNAL_VPP = 0.06f;

inline Metrics analyze(const float* buf, size_t n, float sampleRateHz, float hystMin) {
  Metrics m;
  if (buf == nullptr || n == 0) return m;
  // Single precision throughout: the ESP32-C6 has no FPU and soft-double is
  // roughly 2x slower than soft-float. Two-pass variance keeps accuracy.
  float sum = 0.0f;
  float mn = buf[0], mx = buf[0];
  for (size_t i = 0; i < n; ++i) {
    float v = buf[i];
    sum += v;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
  }
  const float invN = 1.0f / (float)n;
  m.dc = sum * invN;
  m.vmin = mn;
  m.vmax = mx;
  m.vpp = mx - mn;

  float acc = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    float d = buf[i] - m.dc;
    acc += d * d;
  }
  m.rms = sqrtf(acc * invN);

  if (m.vpp < MIN_SIGNAL_VPP || n < 4 || !(sampleRateHz > 0.0f)) return m;

  // Schmitt-trigger crossing detector around the midpoint with sub-sample
  // linear interpolation for accurate period estimation.
  const float mid  = 0.5f * (mx + mn);
  const float hyst = fmaxf(hystMin, 0.15f * m.vpp);
  bool  armed = false;
  float first = -1.0f, last = -1.0f;
  uint32_t crossings = 0;
  for (size_t i = 1; i < n; ++i) {
    if (!armed) {
      if (buf[i] < mid - hyst) armed = true;
    } else if (buf[i - 1] < mid && buf[i] >= mid) {
      float dv = buf[i] - buf[i - 1];
      float frac = (dv > 1e-6f) ? (mid - buf[i - 1]) / dv : 0.0f;
      float pos = (float)(i - 1) + frac;
      if (first < 0.0f) first = pos;
      last = pos;
      ++crossings;
      armed = false;
    }
  }
  if (crossings >= 2 && last > first) {
    float period = (last - first) / (float)(crossings - 1);
    if (period > 1.5f) {
      m.freq = sampleRateHz / period;
      // Duty cycle over an integer number of periods
      size_t a = (size_t)ceilf(first), b = (size_t)floorf(last);
      if (b > a && b <= n) {
        size_t hi = 0;
        for (size_t i = a; i < b; ++i) if (buf[i] >= mid) ++hi;
        m.duty = 100.0f * (float)hi / (float)(b - a);
      }
    }
  }
  return m;
}

// =============================================================================
// 5B. FAST FOURIER TRANSFORM (1024-point radix-2 Cooley-Tukey FFT)
// =============================================================================
// Uses the whole capture buffer so the FFT sees the same time window as the
// scope trace (and the same window the Web UI spectrum is built from).
constexpr size_t FFT_POINTS = CAPTURE_SAMPLES;
constexpr size_t FFT_BINS   = FFT_POINTS / 2;
// Bins 0..1 are dominated by DC residue, drift and capture-start transients;
// they are still drawn but never picked as the "peak".
constexpr size_t FFT_MIN_PEAK_BIN = 2;

struct FftResult {
  float bins[FFT_BINS];     // Magnitude (volts, single-sided amplitude)
  float peakFreq = 0.0f;    // Dominant peak frequency (Hz)
  float peakMag  = 0.0f;    // Peak magnitude
  float maxMag   = 0.0f;    // Largest magnitude of bins >= 1 (for Y autoscale)
  size_t peakBin = 0;
  float binHz    = 0.0f;    // Frequency resolution per bin (Hz)
};

inline void computeFft(const float* signal, size_t n, float sampleRateHz, FftResult& out) {
  memset(&out, 0, sizeof(out));
  if (signal == nullptr || n < FFT_POINTS || !(sampleRateHz > 0.0f)) {
    return;
  }
  static_assert((FFT_POINTS & (FFT_POINTS - 1)) == 0, "FFT_POINTS must be a power of two");
  static float real[FFT_POINTS];
  static float imag[FFT_POINTS];
  static float window[FFT_POINTS];
  static float twCos[FFT_POINTS / 2];   // exp(-j*2*pi*k/N), computed once
  static float twSin[FFT_POINTS / 2];
  static bool  tablesReady = false;
  if (!tablesReady) {
    for (size_t i = 0; i < FFT_POINTS; ++i) {
      window[i] = 0.5f * (1.0f - cosf((2.0f * (float)M_PI * (float)i) / (float)(FFT_POINTS - 1)));
    }
    for (size_t k = 0; k < FFT_POINTS / 2; ++k) {
      const float a = -2.0f * (float)M_PI * (float)k / (float)FFT_POINTS;
      twCos[k] = cosf(a);
      twSin[k] = sinf(a);
    }
    tablesReady = true;
  }

  // Remove DC + linear trend (least squares), then apply Hann window
  float sumY = 0.0f, sumXY = 0.0f;
  const float xMid = 0.5f * (float)(FFT_POINTS - 1);
  for (size_t i = 0; i < FFT_POINTS; ++i) {
    const float y = signal[i];
    sumY  += y;
    sumXY += ((float)i - xMid) * y;
  }
  const float mean = sumY / (float)FFT_POINTS;
  // Sum of (i - xMid)^2 over i = 0..N-1 has the closed form N(N^2-1)/12
  constexpr float sumXX = (float)FFT_POINTS * ((float)FFT_POINTS * (float)FFT_POINTS - 1.0f) / 12.0f;
  const float slope = sumXY / sumXX;

  for (size_t i = 0; i < FFT_POINTS; ++i) {
    const float trend = mean + slope * ((float)i - xMid);
    real[i] = (signal[i] - trend) * window[i];
  }

  // Bit-reversal permutation (input is real, so only real[] needs swapping)
  size_t j = 0;
  for (size_t i = 0; i < FFT_POINTS - 1; ++i) {
    if (i < j) {
      float tr = real[i]; real[i] = real[j]; real[j] = tr;
    }
    size_t k = FFT_POINTS / 2;
    while (k <= j) {
      j -= k;
      k /= 2;
    }
    j += k;
  }
  memset(imag, 0, sizeof(imag));

  // Radix-2 Cooley-Tukey FFT with table twiddles (faster and no accumulated
  // rounding error from the recursive twiddle update)
  for (size_t len = 2, twStep = FFT_POINTS / 2; len <= FFT_POINTS; len <<= 1, twStep >>= 1) {
    const size_t half = len >> 1;
    for (size_t i = 0; i < FFT_POINTS; i += len) {
      for (size_t k = 0, t = 0; k < half; ++k, t += twStep) {
        const float w_r = twCos[t];
        const float w_i = twSin[t];
        const size_t a = i + k;
        const size_t b = a + half;
        const float v_r = real[b] * w_r - imag[b] * w_i;
        const float v_i = real[b] * w_i + imag[b] * w_r;
        const float u_r = real[a];
        const float u_i = imag[a];
        real[a] = u_r + v_r;
        imag[a] = u_i + v_i;
        real[b] = u_r - v_r;
        imag[b] = u_i - v_i;
      }
    }
  }

  out.binHz = sampleRateHz / (float)FFT_POINTS;
  out.peakMag = 0.0f;
  out.peakFreq = 0.0f;
  size_t peakBin = 0;

  constexpr float magScale = 4.0f / (float)FFT_POINTS;
  for (size_t i = 0; i < FFT_BINS; ++i) {
    float mag = sqrtf(real[i] * real[i] + imag[i] * imag[i]) * magScale;
    out.bins[i] = mag;
    if (i > 0 && mag > out.maxMag) out.maxMag = mag;
    if (i >= FFT_MIN_PEAK_BIN && mag > out.peakMag) {
      out.peakMag = mag;
      peakBin = i;
    }
  }
  out.peakBin = peakBin;

  // Parabolic (quadratic) peak interpolation for sub-bin frequency accuracy:
  if (peakBin > 0 && peakBin < FFT_BINS - 1 && out.peakMag > 1e-4f) {
    float y_left   = out.bins[peakBin - 1];
    float y_center = out.bins[peakBin];
    float y_right  = out.bins[peakBin + 1];
    float denom = (y_left - 2.0f * y_center + y_right);
    float delta = 0.0f;
    if (fabsf(denom) > 1e-7f) {
      delta = 0.5f * (y_left - y_right) / denom;
      if (delta < -0.7f) delta = -0.7f;
      if (delta > 0.7f) delta = 0.7f;
    }
    out.peakFreq = ((float)peakBin + delta) * out.binHz;
  } else {
    out.peakFreq = (float)peakBin * out.binHz;
  }
}

// -----------------------------------------------------------------------------
// Multi-peak detection: up to FFT_MAX_PEAKS strongest local maxima whose
// magnitude is >= thresholdPct % of the strongest peak.
// -----------------------------------------------------------------------------
constexpr size_t  FFT_MAX_PEAKS            = 10;
constexpr uint8_t FFT_PEAK_THRESHOLD_DEF   = 10;  // % of strongest peak
constexpr size_t  FFT_PEAK_MIN_SEPARATION  = 3;   // bins (Hann main lobe is +/-2 bins)

struct FftPeak {
  float  freq = 0.0f;  // Hz (parabolically interpolated)
  float  mag  = 0.0f;  // volts
  size_t bin  = 0;
};

inline float interpolatePeakFreq(const FftResult& r, size_t bin) {
  if (bin == 0 || bin >= FFT_BINS - 1) return (float)bin * r.binHz;
  float yl = r.bins[bin - 1], yc = r.bins[bin], yr = r.bins[bin + 1];
  float denom = yl - 2.0f * yc + yr;
  float delta = 0.0f;
  if (fabsf(denom) > 1e-7f) {
    delta = 0.5f * (yl - yr) / denom;
    if (delta < -0.7f) delta = -0.7f;
    if (delta > 0.7f) delta = 0.7f;
  }
  return ((float)bin + delta) * r.binHz;
}

// Returns number of peaks written to `out` (sorted by magnitude, strongest first).
inline size_t findFftPeaks(const FftResult& r, uint8_t thresholdPct, FftPeak* out, size_t maxPeaks) {
  if (out == nullptr || maxPeaks == 0 || r.peakMag <= 1e-4f) return 0;
  if (maxPeaks > FFT_MAX_PEAKS) maxPeaks = FFT_MAX_PEAKS;
  const float minMag = r.peakMag * ((float)thresholdPct / 100.0f);
  size_t count = 0;

  for (size_t i = FFT_MIN_PEAK_BIN; i < FFT_BINS - 1; ++i) {
    float m = r.bins[i];
    if (m < minMag || m < r.bins[i - 1] || m < r.bins[i + 1]) continue;
    if (m == r.bins[i - 1] && i > FFT_MIN_PEAK_BIN) continue; // plateau: keep first bin only

    // Too close to an existing peak: keep the stronger one
    bool skip = false;
    for (size_t k = 0; k < count; ++k) {
      size_t d = (out[k].bin > i) ? (out[k].bin - i) : (i - out[k].bin);
      if (d < FFT_PEAK_MIN_SEPARATION) {
        if (m > out[k].mag) {
          out[k].bin = i; out[k].mag = m; out[k].freq = interpolatePeakFreq(r, i);
        }
        skip = true;
        break;
      }
    }
    if (skip) continue;

    FftPeak p;
    p.bin = i; p.mag = m; p.freq = interpolatePeakFreq(r, i);
    if (count < maxPeaks) {
      out[count++] = p;
    } else {
      size_t weakest = 0;
      for (size_t k = 1; k < count; ++k) if (out[k].mag < out[weakest].mag) weakest = k;
      if (p.mag > out[weakest].mag) out[weakest] = p;
    }
  }

  // Sort strongest first (insertion sort, n <= 10)
  for (size_t a = 1; a < count; ++a) {
    FftPeak t = out[a];
    size_t b = a;
    while (b > 0 && out[b - 1].mag < t.mag) { out[b] = out[b - 1]; --b; }
    out[b] = t;
  }
  return count;
}

// -----------------------------------------------------------------------------
// FFT X-axis zoom window: the bin range [b0, b1) shown across the chart.
// Widths are powers of two and b0 snaps to a quarter-window grid, so the view
// stays steady while the spectrum updates. Pass the previous window as `prev`
// so AUTO only zooms in when the peaks fit in a much narrower window
// (prevents flicker when a weak harmonic hovers around the threshold).
// -----------------------------------------------------------------------------
constexpr size_t FFT_ZOOM_MIN_BINS = 32;
constexpr size_t FFT_ZOOM_PAD_BINS = 4;

struct FftWindow {
  size_t b0 = 0;
  size_t b1 = FFT_BINS;
  size_t width() const { return b1 - b0; }
};

inline FftWindow fftZoomWindow(uint8_t zoom, const FftPeak* peaks, size_t count,
                               size_t peakBin, const FftWindow* prev = nullptr) {
  FftWindow w;
  if (zoom == FFT_ZOOM_FULL || zoom >= NUM_FFT_ZOOMS) return w;

  size_t width, lo, hi;
  const bool isAuto = (zoom == FFT_ZOOM_AUTO);
  if (isAuto) {
    if (peaks == nullptr || count == 0) return w;
    lo = hi = peaks[0].bin;
    for (size_t k = 1; k < count; ++k) {
      if (peaks[k].bin < lo) lo = peaks[k].bin;
      if (peaks[k].bin > hi) hi = peaks[k].bin;
    }
    lo = (lo > FFT_ZOOM_PAD_BINS) ? lo - FFT_ZOOM_PAD_BINS : 0;
    hi = hi + FFT_ZOOM_PAD_BINS + 1;
    if (hi > FFT_BINS) hi = FFT_BINS;
    width = FFT_ZOOM_MIN_BINS;
    while (width < hi - lo && width < FFT_BINS) width <<= 1;
    if (prev != nullptr && prev->b1 <= FFT_BINS && prev->b0 < prev->b1 &&
        prev->b0 <= lo && prev->b1 >= hi && prev->width() <= 2 * width) {
      return *prev;
    }
  } else {
    width = FFT_BINS >> (zoom - FFT_ZOOM_FULL); // 2x -> 256, 4x -> 128, 8x -> 64 bins
    const size_t half = width / 2;
    lo = (peakBin > half) ? peakBin - half : 0;
    hi = lo + width;
  }

  while (width < FFT_BINS) {
    const size_t q = width / 4;
    size_t b0 = (lo / q) * q;
    if (b0 <= q) b0 = 0;                       // close to DC: start at 0 Hz
    if (b0 + width > FFT_BINS) b0 = FFT_BINS - width;
    if (!isAuto || b0 + width >= hi) {
      w.b0 = b0;
      w.b1 = b0 + width;
      return w;
    }
    width <<= 1;
  }
  return w; // full span
}

// =============================================================================
// 5C. NETWORK STATISTICS & WI-FI METRICS
// =============================================================================
struct NetworkStats {
  char     ssid[33]     = "";
  char     ip[16]       = "0.0.0.0";
  char     gw[16]       = "0.0.0.0";
  char     mask[16]     = "0.0.0.0";
  char     mac[18]      = "";
  int8_t   rssi         = 0;      // dBm
  uint8_t  quality      = 0;      // 0..100%
  uint8_t  bars         = 0;      // 0..4 signal bars
  bool     isStatic     = false;  // target was static IP (.100)
  bool     fallbackUsed = false;  // true if fallback from .100 occurred
  uint8_t  activeWs     = 0;      // active WebSocket clients
  uint32_t httpReqs     = 0;      // total HTTP requests served
  uint32_t wsFrames     = 0;      // total WS frames sent
  bool     connected    = false;
};

inline uint8_t rssiToQuality(int8_t rssi) {
  if (rssi <= -100) return 0;
  if (rssi >= -50)  return 100;
  return (uint8_t)(2 * (rssi + 100));
}

inline uint8_t rssiToBars(int8_t rssi) {
  if (rssi >= -60) return 4;
  if (rssi >= -70) return 3;
  if (rssi >= -80) return 2;
  if (rssi >= -90) return 1;
  return 0;
}

// =============================================================================
// 6. AUTO-SCALE
// =============================================================================
inline uint8_t bestTimebaseFor(float freqHz) {
  // ~3 periods across the 10 horizontal divisions, nearest in log scale
  float target = (3.0f / freqHz) / (float)GRID_COLS;
  // Minimising |log(r)| is equivalent to minimising max(r, 1/r): no logf needed.
  uint8_t best = 0;
  float bestErr = 1e30f;
  for (uint8_t i = 0; i < NUM_TIME_BASES; ++i) {
    float r = TIME_BASES_SEC[i] / target;
    float err = (r >= 1.0f) ? r : 1.0f / r;
    if (err < bestErr) { bestErr = err; best = i; }
  }
  return best;
}

inline uint8_t bestVdivFor(float spanVolts) {
  float target = spanVolts / (float)GRID_ROWS;
  for (uint8_t i = 0; i < NUM_V_DIVS; ++i) {
    if (V_DIVS[i] >= target) return i;
  }
  return NUM_V_DIVS - 1;
}

// Called a few times per second. Mutates only the "auto" controlled fields.
// m2 (optional): CH2 metrics, so Auto Volts never clips the CH2 trace.
inline void autoScale(Settings& s, const Metrics& m, const Metrics* m2 = nullptr) {
  if (s.autoVolts) {
    const float vpp  = m2 ? fmaxf(m.vpp,  m2->vpp)  : m.vpp;
    const float vmax = m2 ? fmaxf(m.vmax, m2->vmax) : m.vmax;
    float span = s.acCoupled ? (vpp * 1.35f) : (fmaxf(vmax, 0.5f) * 1.25f);
    s.vDivIdx = bestVdivFor(span);
  }
  // Trigger level follows the waveform midpoint unless the user placed it.
  if (!s.userSetTrig && m.vpp >= MIN_SIGNAL_VPP) {
    s.trigVolts = 0.5f * (m.vmin + m.vmax);
  }
  if (s.autoTimebase) {
    if (m.freq >= 0.5f) {
      s.timeBaseIdx = bestTimebaseFor(m.freq);
    } else if (m.vpp >= 0.35f) {
      // Real large-amplitude signal present with no full period in window -> hunt slower
      if (s.timeBaseIdx < TB_HUNT_MAX) s.timeBaseIdx++;
    } else {
      s.timeBaseIdx = TB_DC_IDX; // flat DC / open probe noise: fast 1 ms/div sweep
    }
  }
}

// =============================================================================
// 7. TRIGGER + VIEW WINDOW
// =============================================================================
struct View {
  size_t start     = 0;     // first sample shown at x = 0
  float  span      = 8.0f;  // samples across the full screen width
  bool   triggered = false;
  float  trigFrac  = 0.0f;  // trigger position across the screen (0..1)
};

// Bounds-safe linear interpolation (never reads buf[n]).
inline float sampleAt(const float* buf, size_t n, float pos) {
  if (n == 0) return 0.0f;
  if (!(pos > 0.0f)) return buf[0];
  if (pos >= (float)(n - 1)) return buf[n - 1];
  size_t i = (size_t)pos;
  float f = pos - (float)i;
  return buf[i] + f * (buf[i + 1] - buf[i]);
}

inline View computeView(const float* buf, size_t n, float sampleRateHz,
                        const Settings& s, float vpp) {
  View v;
  if (buf == nullptr || n < 16) { v.span = (n > 1) ? (float)(n - 1) : 1.0f; return v; }
  float span = TIME_BASES_SEC[s.timeBaseIdx] * (float)GRID_COLS * sampleRateHz;
  if (!(span >= 8.0f)) span = 8.0f;
  if (span > (float)(n - 1)) span = (float)(n - 1);
  v.span = span;
  if (s.trigMode == TRIG_FREE || vpp < 0.05f) return v;

  const size_t pre  = (size_t)(span * PRE_TRIGGER_FRAC);
  const size_t post = (size_t)ceilf(span) - pre;
  if (n < post + 2) return v;
  const size_t lastIdx = n - 1 - post;
  const float lvl  = s.trigVolts;
  const float hyst = HYST_VOLTS[s.hystIdx];
  const bool falling = (s.trigMode == TRIG_FALLING);
  bool armed = false;
  for (size_t i = 1; i <= lastIdx; ++i) {
    float a = buf[i - 1], b = buf[i];
    bool cross;
    if (falling) {
      if (a > lvl + hyst) armed = true;
      cross = armed && a > lvl && b <= lvl;
    } else {
      if (a < lvl - hyst) armed = true;
      cross = armed && a < lvl && b >= lvl;
    }
    if (cross) {
      if (i >= pre) {
        v.start = i - pre;
        v.triggered = true;
        v.trigFrac = (float)pre / span;
        return v;
      }
      armed = false; // too close to the buffer start; wait for the next edge
    }
  }
  return v; // not triggered -> auto (free-running) display
}

// =============================================================================
// 8. VOLTS <-> PIXELS + SCREEN TRACE
// =============================================================================
// Precomputed volts -> pixel row mapping (all divisions hoisted out of the
// per-sample path; matters on the FPU-less ESP32-C6).
struct YMapper {
  float offs, invFull, base, gain, lo, hi;
  YMapper(float vdiv, bool ac, float dc, int16_t top, int16_t h) {
    const float full = vdiv * (float)GRID_ROWS;
    invFull = 1.0f / full;
    offs    = ac ? (0.5f * full - dc) : 0.0f;
    base    = (float)(top + h - 1);
    gain    = (float)(h - 2);
    lo      = (float)(top + 1);
    hi      = (float)(top + h - 2);
  }
  int16_t operator()(float v) const {
    float norm = (v + offs) * invFull;
    if (!isfinite(norm)) norm = 0.0f;
    const float py = clampF(base - norm * gain, lo, hi);
    return (int16_t)(py + 0.5f); // py >= lo > 0, so this rounds to nearest
  }
};

inline int16_t voltsToY(float v, float vdiv, bool ac, float dc, int16_t top, int16_t h) {
  return YMapper(vdiv, ac, dc, top, h)(v);
}

inline float yToVolts(int16_t y, float vdiv, bool ac, float dc, int16_t top, int16_t h) {
  float norm = (float)((top + h - 1) - y) / (float)(h - 2);
  norm = clampF(norm, 0.0f, 1.0f);
  float full = vdiv * (float)GRID_ROWS;
  return ac ? (dc + (norm - 0.5f) * full) : (norm * full);
}

struct Trace {
  int16_t y1[LCD_W];
  int16_t y2[LCD_W];
  uint8_t d2[LCD_W];
  uint8_t ch2Mode   = CH2_OFF;
  bool    triggered = false;
  int16_t trigX     = 0;
  int16_t trigY     = CHART_Y + CHART_H / 2;
};

inline void buildTrace(Trace& t, const float* ch1, const float* ch2, const uint8_t* dig,
                       size_t n, const View& v, const Settings& s, float dc1, float dc2) {
  const float vdiv = V_DIVS[s.vDivIdx];
  const YMapper map1(vdiv, s.acCoupled, dc1, CHART_Y, CHART_H);
  const YMapper map2(vdiv, s.acCoupled, dc2, CHART_Y, CHART_H);
  t.ch2Mode = s.ch2Mode;
  t.triggered = v.triggered;
  t.trigX = (int16_t)lroundf(v.trigFrac * (float)(LCD_W - 1));
  t.trigY = map1(s.trigVolts);

  const bool  analog2  = (s.ch2Mode == CH2_ANALOG) && ch2;
  const bool  digital2 = (s.ch2Mode == CH2_DIGITAL) && dig && n > 0;
  const float start    = (float)v.start;
  const float step     = v.span / (float)(LCD_W - 1);
  if (!analog2) {
    for (int16_t x = 0; x < LCD_W; ++x) t.y2[x] = CHART_Y + CHART_H / 2;
  }
  if (!digital2) memset(t.d2, 0, sizeof(t.d2));

  for (int16_t x = 0; x < LCD_W; ++x) {
    const float pos = start + (float)x * step;
    t.y1[x] = map1(sampleAt(ch1, n, pos));
    if (analog2) t.y2[x] = map2(sampleAt(ch2, n, pos));
    if (digital2) {
      size_t idx = (size_t)(pos + 0.5f);
      if (idx >= n) idx = n - 1;
      t.d2[x] = dig[idx] ? 1 : 0;
    }
  }
}

// =============================================================================
// 9. BATTERY (typical 1-cell LiPo open-circuit curve)
// =============================================================================
inline uint8_t batteryPercent(float v) {
  static const float V[]  = { 3.27f, 3.61f, 3.69f, 3.71f, 3.73f, 3.75f, 3.77f, 3.79f, 3.80f, 3.82f,
                              3.84f, 3.85f, 3.87f, 3.91f, 3.95f, 3.98f, 4.02f, 4.08f, 4.11f, 4.15f, 4.20f };
  const size_t N = sizeof(V) / sizeof(V[0]);
  if (!(v > V[0])) return 0;
  if (v >= V[N - 1]) return 100;
  for (size_t i = 1; i < N; ++i) {
    if (v < V[i]) {
      float p0 = (float)(i - 1) * 5.0f;
      float f = (v - V[i - 1]) / (V[i] - V[i - 1]);
      return (uint8_t)lroundf(p0 + f * 5.0f);
    }
  }
  return 100;
}
constexpr float BATTERY_PRESENT_MIN_V = 2.5f; // below this: USB power without a cell

// =============================================================================
// 10. FORMATTERS
// =============================================================================
inline void fmtVolts(char* o, size_t n, float v) {
  if (!isfinite(v)) { snprintf(o, n, "--"); return; }
  float a = fabsf(v);
  if (a < 0.9995f)      snprintf(o, n, "%ldmV", lroundf(v * 1000.0f));
  else if (a < 9.995f)  snprintf(o, n, "%.2fV", v);
  else                  snprintf(o, n, "%.1fV", v);
}

inline void fmtFreq(char* o, size_t n, float f) {
  if (!(f > 0.0f) || !isfinite(f)) { snprintf(o, n, "--"); return; }
  if (f >= 1e6f)       snprintf(o, n, "%.2fMHz", f / 1e6f);
  else if (f >= 1e5f)  snprintf(o, n, "%.0fkHz", f / 1e3f);
  else if (f >= 1e4f)  snprintf(o, n, "%.1fkHz", f / 1e3f);
  else if (f >= 1e3f)  snprintf(o, n, "%.2fkHz", f / 1e3f);
  else if (f >= 100.f) snprintf(o, n, "%.0fHz", f);
  else if (f >= 10.f)  snprintf(o, n, "%.1fHz", f);
  else                 snprintf(o, n, "%.2fHz", f);
}

inline void fmtTime(char* o, size_t n, float sec) {
  if (!(sec > 0.0f) || !isfinite(sec)) { snprintf(o, n, "--"); return; }
  if (sec >= 1.0f)        snprintf(o, n, "%.2fs", sec);
  else if (sec >= 1e-3f)  snprintf(o, n, "%.2fms", sec * 1e3f);
  else                    snprintf(o, n, "%.1fus", sec * 1e6f);
}

inline void fmtRate(char* o, size_t n, float hz) {
  if (!(hz > 0.0f)) { snprintf(o, n, "--"); return; }
  if (hz >= 1e6f)       snprintf(o, n, "%.1fMS/s", hz / 1e6f);
  else if (hz >= 1e3f)  snprintf(o, n, "%.0fkS/s", hz / 1e3f);
  else                  snprintf(o, n, "%.0fS/s", hz);
}

inline void fmtUptime(char* o, size_t n, uint32_t s) {
  uint32_t d = s / 86400u; s %= 86400u;
  uint32_t h = s / 3600u;  s %= 3600u;
  uint32_t m = s / 60u;    s %= 60u;
  if (d) snprintf(o, n, "%lud %02lu:%02lu:%02lu", (unsigned long)d, (unsigned long)h, (unsigned long)m, (unsigned long)s);
  else   snprintf(o, n, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
}

// =============================================================================
// 11. TOUCH UI STATE MACHINE
// =============================================================================
enum Screen : uint8_t { SCR_MAIN = 0, SCR_SETTINGS = 1, SCR_FFT = 2 };
enum Tab : uint8_t { TAB_SCOPE = 0, TAB_TRIG, TAB_IO, TAB_NET, TAB_SYS, TAB_COUNT };
constexpr const char* TAB_LABELS[] = { "SCOPE", "TRIG", "I/O", "WIFI", "SYS" };

struct UiState {
  uint8_t  screen          = SCR_MAIN;
  uint8_t  tab             = TAB_SCOPE;
  uint32_t resetArmedUntil = 0;
  char     toast[40]       = "";
  uint32_t toastUntil      = 0;
  uint16_t toastColor      = 0xFFFF;
};

inline void showToast(UiState& ui, uint32_t now, const char* msg, uint16_t color, uint32_t durMs = 2500) {
  snprintf(ui.toast, sizeof(ui.toast), "%s", msg);
  ui.toastColor = color;
  ui.toastUntil = now + durMs;
}

enum Action : uint8_t {
  ACT_NONE = 0,
  ACT_CHART_TOUCH,       // drag inside waveform area -> trigger level
  ACT_OPEN_MENU, ACT_OPEN_NET, ACT_CLOSE_MENU,
  ACT_OPEN_FFT, ACT_CLOSE_FFT,
  ACT_FFT_SPAN_NEXT, ACT_FFT_SCALE_TOGGLE, ACT_FFT_ZOOM_NEXT,
  ACT_TAB_SCOPE, ACT_TAB_TRIG, ACT_TAB_IO, ACT_TAB_NET, ACT_TAB_SYS,
  ACT_TOGGLE_AUTO,
  ACT_TB_PREV, ACT_TB_NEXT, ACT_VDIV_PREV, ACT_VDIV_NEXT,
  ACT_RUN_TOGGLE,
  ACT_AUTO_T, ACT_AUTO_V, ACT_RATE_NEXT, ACT_PROBE_NEXT, ACT_PROBE_B_NEXT,
  ACT_TRIG_MODE_NEXT, ACT_COUPLING, ACT_TRIG_LEVEL_AUTO, ACT_HYST_NEXT, ACT_SINGLE_ARM,
  ACT_CH2_NEXT, ACT_SOURCE_TOGGLE, ACT_CAL_NEXT,
  ACT_NET_RECONNECT,
  ACT_BRIGHT_NEXT, ACT_FACTORY_RESET,
  ACT_OPEN_SYS,
  ACT_COUNT
};

// Side-effect flags returned by applyAction()/applyParam() (hardware work is
// performed by the firmware, not by this pure logic).
enum : uint32_t {
  FX_NONE          = 0,
  FX_CAL           = 1u << 0,  // re-program the GPIO7 calibration generator
  FX_BACKLIGHT     = 1u << 1,  // re-apply backlight PWM
  FX_RECONNECT     = 1u << 2,  // restart Wi-Fi connection / IP selection
  FX_FACTORY_RESET = 1u << 3,
  FX_REBOOT        = 1u << 4,
  FX_INVALID       = 1u << 30, // a parameter value was rejected
  FX_UNKNOWN_KEY   = 1u << 31  // a parameter key was not recognised
};

inline bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

inline Action hitTestMain(int16_t x, int16_t y) {
  if (y < TOP_BAR_H) {
    if (x >= MENU_BTN_X - 2) return ACT_OPEN_MENU;
    if (x >= WIFI_ZONE_X) return ACT_OPEN_NET;
    if (x >= BAT_ZONE_X) return ACT_OPEN_SYS;
    if (x >= FFT_BTN_X - 8) return ACT_OPEN_FFT;
    if (x < AUTO_BADGE_X + AUTO_BADGE_W + 2) return ACT_TOGGLE_AUTO;
    return ACT_NONE;
  }
  if (y >= LCD_H - BOT_BAR_H) {
    if (x < BOT_SPLIT[0]) return ACT_TOGGLE_AUTO;
    if (x < BOT_SPLIT[1]) return (x < BOT_BTN_X[1] + BOT_BTN_W[1] / 2) ? ACT_TB_PREV : ACT_TB_NEXT;
    if (x < BOT_SPLIT[2]) return (x < BOT_BTN_X[2] + BOT_BTN_W[2] / 2) ? ACT_VDIV_PREV : ACT_VDIV_NEXT;
    return ACT_RUN_TOGGLE;
  }
  return ACT_CHART_TOUCH;
}

inline Action hitTestFft(int16_t x, int16_t y) {
  if (y < TOP_BAR_H) {
    if (x < FFT_BACK_BTN_X + FFT_BACK_BTN_W + 6) return ACT_CLOSE_FFT; // [< SCOPE]
    if (x >= MENU_BTN_X - 2) return ACT_OPEN_MENU;
    if (x >= WIFI_ZONE_X) return ACT_OPEN_NET;
    if (x >= BAT_ZONE_X) return ACT_OPEN_SYS;
    return ACT_NONE;
  }
  if (y >= LCD_H - BOT_BAR_H) {
    if (x < BOT_SPLIT[0]) return ACT_FFT_ZOOM_NEXT;    // Button 0: X zoom
    if (x < BOT_SPLIT[1]) return ACT_FFT_SPAN_NEXT;    // Button 1: span
    if (x < BOT_SPLIT[2]) return ACT_FFT_SCALE_TOGGLE; // Button 2: LOG / LIN
    return ACT_RUN_TOGGLE;                             // Button 3: RUN / HOLD
  }
  return ACT_NONE;
}

// Tile action table per tab: [row*2 + col] -> {leftHalf, rightHalf}
struct TileActs { Action left, right; };
inline TileActs tileActions(uint8_t tab, uint8_t tile) {
  static const TileActs T[TAB_COUNT][6] = {
    { // SCOPE
      { ACT_AUTO_T, ACT_AUTO_T },        { ACT_AUTO_V, ACT_AUTO_V },
      { ACT_TB_PREV, ACT_TB_NEXT },      { ACT_VDIV_PREV, ACT_VDIV_NEXT },
      { ACT_RATE_NEXT, ACT_RATE_NEXT },  { ACT_PROBE_NEXT, ACT_PROBE_B_NEXT } },
    { // TRIG
      { ACT_TRIG_MODE_NEXT, ACT_TRIG_MODE_NEXT }, { ACT_COUPLING, ACT_COUPLING },
      { ACT_TRIG_LEVEL_AUTO, ACT_TRIG_LEVEL_AUTO }, { ACT_HYST_NEXT, ACT_HYST_NEXT },
      { ACT_SINGLE_ARM, ACT_SINGLE_ARM },  { ACT_RUN_TOGGLE, ACT_RUN_TOGGLE } },
    { // I/O
      { ACT_CH2_NEXT, ACT_CH2_NEXT },    { ACT_SOURCE_TOGGLE, ACT_SOURCE_TOGGLE },
      { ACT_CAL_NEXT, ACT_CAL_NEXT },    { ACT_NONE, ACT_NONE },
      { ACT_NONE, ACT_NONE },            { ACT_NONE, ACT_NONE } },
    { // NET (info page, button handled separately)
      { ACT_NONE, ACT_NONE }, { ACT_NONE, ACT_NONE }, { ACT_NONE, ACT_NONE },
      { ACT_NONE, ACT_NONE }, { ACT_NONE, ACT_NONE }, { ACT_NONE, ACT_NONE } },
    { // SYS (row 0 is the read-only power / uptime stats panel)
      { ACT_NONE, ACT_NONE }, { ACT_NONE, ACT_NONE },
      { ACT_BRIGHT_NEXT, ACT_BRIGHT_NEXT }, { ACT_FACTORY_RESET, ACT_FACTORY_RESET },
      { ACT_NONE, ACT_NONE }, { ACT_NONE, ACT_NONE } },
  };
  if (tab >= TAB_COUNT || tile >= 6) return { ACT_NONE, ACT_NONE };
  return T[tab][tile];
}

inline Action hitTestSettings(uint8_t tab, int16_t x, int16_t y) {
  // Generous touch zone for the DONE button in the top-right corner
  if (x >= (DONE_BTN_X - 10) && y <= 32) return ACT_CLOSE_MENU;
  if (y < TAB_H + 4) {
    if (x >= DONE_BTN_X - 2) return ACT_CLOSE_MENU;
    int16_t t = (x < TAB_X0) ? 0 : (x - TAB_X0) / TAB_W;
    if (t >= TAB_COUNT) t = TAB_COUNT - 1;
    return (Action)(ACT_TAB_SCOPE + t);
  }
  if (tab == TAB_NET) {
    return inRect(x, y, NET_BTN_X - 4, NET_BTN_Y - 4, NET_BTN_W + 8, NET_BTN_H + 8) ? ACT_NET_RECONNECT : ACT_NONE;
  }
  // rows split at the midpoint of the gaps between tiles
  uint8_t row = (y < TILE_ROW_Y[1] - 2) ? 0 : ((y < TILE_ROW_Y[2] - 2) ? 1 : 2);
  uint8_t col = (x < TILE_COL_X[1] - 2) ? 0 : 1;
  TileActs ta = tileActions(tab, (uint8_t)(row * 2 + col));
  int16_t mid = TILE_COL_X[col] + TILE_W / 2;
  return (x < mid) ? ta.left : ta.right;
}

// Applies a UI action to the settings / UI state. Pure; returns FX_* flags.
inline uint32_t applyAction(Settings& s, UiState& ui, Action a, uint32_t now) {
  switch (a) {
    case ACT_OPEN_MENU:   ui.screen = SCR_SETTINGS; return FX_NONE;
    case ACT_OPEN_NET:    ui.screen = SCR_SETTINGS; ui.tab = TAB_NET; return FX_NONE;
    case ACT_OPEN_SYS:    ui.screen = SCR_SETTINGS; ui.tab = TAB_SYS; return FX_NONE;
    case ACT_CLOSE_MENU:  ui.screen = SCR_MAIN; ui.resetArmedUntil = 0; return FX_NONE;
    case ACT_OPEN_FFT:    ui.screen = SCR_FFT; return FX_NONE;
    case ACT_CLOSE_FFT:   ui.screen = SCR_MAIN; return FX_NONE;
    case ACT_FFT_SPAN_NEXT:   s.fftSpanIdx = (uint8_t)((s.fftSpanIdx + 1) % NUM_SAMPLE_RATES); return FX_NONE;
    case ACT_FFT_SCALE_TOGGLE: s.fftLogScale = !s.fftLogScale; return FX_NONE;
    case ACT_FFT_ZOOM_NEXT:    s.fftZoomIdx = (uint8_t)((s.fftZoomIdx + 1) % NUM_FFT_ZOOMS); return FX_NONE;
    case ACT_TAB_SCOPE: case ACT_TAB_TRIG: case ACT_TAB_IO: case ACT_TAB_NET: case ACT_TAB_SYS:
      ui.tab = (uint8_t)(a - ACT_TAB_SCOPE); ui.resetArmedUntil = 0; return FX_NONE;
    case ACT_TOGGLE_AUTO: {
      bool next = !(s.autoTimebase || s.autoVolts);
      s.autoTimebase = next; s.autoVolts = next;
      if (next) s.userSetTrig = false;
      return FX_NONE;
    }
    case ACT_TB_PREV:   s.autoTimebase = false; s.timeBaseIdx = (uint8_t)((s.timeBaseIdx + NUM_TIME_BASES - 1) % NUM_TIME_BASES); return FX_NONE;
    case ACT_TB_NEXT:   s.autoTimebase = false; s.timeBaseIdx = (uint8_t)((s.timeBaseIdx + 1) % NUM_TIME_BASES); return FX_NONE;
    case ACT_VDIV_PREV: s.autoVolts = false; s.vDivIdx = (uint8_t)((s.vDivIdx + NUM_V_DIVS - 1) % NUM_V_DIVS); return FX_NONE;
    case ACT_VDIV_NEXT: s.autoVolts = false; s.vDivIdx = (uint8_t)((s.vDivIdx + 1) % NUM_V_DIVS); return FX_NONE;
    case ACT_RUN_TOGGLE: s.running = !s.running; return FX_NONE;
    case ACT_AUTO_T:    s.autoTimebase = !s.autoTimebase; return FX_NONE;
    case ACT_AUTO_V:    s.autoVolts = !s.autoVolts; if (s.autoVolts) s.userSetTrig = false; return FX_NONE;
    case ACT_RATE_NEXT: s.sampleRateIdx = (uint8_t)((s.sampleRateIdx + 1) % NUM_SAMPLE_RATES); return FX_NONE;
    case ACT_PROBE_NEXT: s.probeIdx = (uint8_t)((s.probeIdx + 1) % NUM_PROBES); return FX_NONE;
    case ACT_PROBE_B_NEXT: s.probeBIdx = (uint8_t)((s.probeBIdx + 1) % NUM_PROBES); return FX_NONE;
    case ACT_TRIG_MODE_NEXT:
      s.trigMode = (uint8_t)((s.trigMode + 1) % TRIG_MODE_COUNT);
      if (s.trigMode == TRIG_SINGLE) s.running = true; // arm
      return FX_NONE;
    case ACT_COUPLING:  s.acCoupled = !s.acCoupled; return FX_NONE;
    case ACT_TRIG_LEVEL_AUTO: s.userSetTrig = false; return FX_NONE;
    case ACT_HYST_NEXT: s.hystIdx = (uint8_t)((s.hystIdx + 1) % NUM_HYST); return FX_NONE;
    case ACT_SINGLE_ARM: s.trigMode = TRIG_SINGLE; s.running = true; return FX_NONE;
    case ACT_CH2_NEXT:  s.ch2Mode = (uint8_t)((s.ch2Mode + 1) % CH2_MODE_COUNT); return FX_NONE;
    case ACT_SOURCE_TOGGLE: s.selfTest = !s.selfTest; return FX_NONE;
    case ACT_CAL_NEXT:  s.calFreqIdx = (uint8_t)((s.calFreqIdx + 1) % NUM_CAL_FREQS); return FX_CAL;
    case ACT_NET_RECONNECT: return FX_RECONNECT;
    case ACT_BRIGHT_NEXT: s.brightnessIdx = (uint8_t)((s.brightnessIdx + 1) % NUM_BRIGHTNESS); return FX_BACKLIGHT;
    case ACT_FACTORY_RESET:
      if (ui.resetArmedUntil != 0 && (int32_t)(ui.resetArmedUntil - now) > 0) {
        ui.resetArmedUntil = 0;
        return FX_FACTORY_RESET;
      }
      ui.resetArmedUntil = now + 3000; // second tap within 3 s confirms
      return FX_NONE;
    default: return FX_NONE;
  }
}

// Finger drag inside the chart moves the trigger level.
inline void dragTrigger(Settings& s, int16_t y, float dc1) {
  s.trigVolts = yToVolts(y, V_DIVS[s.vDivIdx], s.acCoupled, dc1, CHART_Y, CHART_H);
  s.userSetTrig = true;
}

// =============================================================================
// 12. WEB / WEBSOCKET PARAMETER PARSER
// =============================================================================
inline bool parseLongStrict(const char* str, long& out) {
  if (str == nullptr || *str == '\0') return false;
  char* end = nullptr;
  long v = strtol(str, &end, 10);
  if (end == str || *end != '\0') return false;
  out = v;
  return true;
}

inline bool parseFloatStrict(const char* str, float& out) {
  if (str == nullptr || *str == '\0') return false;
  char* end = nullptr;
  float v = strtof(str, &end);
  if (end == str || *end != '\0' || !isfinite(v)) return false;
  out = v;
  return true;
}

inline bool parseBool(const char* str, bool& out) {
  if (str == nullptr) return false;
  if (!strcmp(str, "1") || !strcmp(str, "true") || !strcmp(str, "on"))  { out = true;  return true; }
  if (!strcmp(str, "0") || !strcmp(str, "false") || !strcmp(str, "off")) { out = false; return true; }
  return false;
}

// Applies one "key=value" setting. Returns FX_* flags.
inline uint32_t applyParam(Settings& s, const char* key, const char* val) {
  if (key == nullptr || val == nullptr) return FX_INVALID;
  bool b; long i; float f;
  auto idxParam = [&](uint8_t& field, uint8_t count) -> bool {
    if (!parseLongStrict(val, i) || i < 0 || i >= (long)count) return false;
    field = (uint8_t)i;
    return true;
  };

  if (!strcmp(key, "running"))      { if (!parseBool(val, b)) return FX_INVALID; s.running = b; return FX_NONE; }
  if (!strcmp(key, "autoTimebase")) { if (!parseBool(val, b)) return FX_INVALID; s.autoTimebase = b; return FX_NONE; }
  if (!strcmp(key, "autoVolts"))    { if (!parseBool(val, b)) return FX_INVALID; s.autoVolts = b; if (b) s.userSetTrig = false; return FX_NONE; }
  if (!strcmp(key, "acCoupled"))    { if (!parseBool(val, b)) return FX_INVALID; s.acCoupled = b; return FX_NONE; }
  if (!strcmp(key, "selfTest"))     { if (!parseBool(val, b)) return FX_INVALID; s.selfTest = b; return FX_NONE; }
  if (!strcmp(key, "enableCh2"))    { if (!parseBool(val, b)) return FX_INVALID; s.ch2Mode = b ? CH2_ANALOG : CH2_OFF; return FX_NONE; }
  if (!strcmp(key, "timeBaseIdx"))  { if (!idxParam(s.timeBaseIdx, NUM_TIME_BASES)) return FX_INVALID; s.autoTimebase = false; return FX_NONE; }
  if (!strcmp(key, "vDivIdx"))      { if (!idxParam(s.vDivIdx, NUM_V_DIVS)) return FX_INVALID; s.autoVolts = false; return FX_NONE; }
  if (!strcmp(key, "sampleRateIdx")){ return idxParam(s.sampleRateIdx, NUM_SAMPLE_RATES) ? FX_NONE : FX_INVALID; }
  if (!strcmp(key, "probeIdx") || !strcmp(key, "probeAIdx")) { return idxParam(s.probeIdx, NUM_PROBES) ? FX_NONE : FX_INVALID; }
  if (!strcmp(key, "probeBIdx"))     { return idxParam(s.probeBIdx, NUM_PROBES) ? FX_NONE : FX_INVALID; }
  if (!strcmp(key, "ch2Mode"))      { return idxParam(s.ch2Mode, CH2_MODE_COUNT) ? FX_NONE : FX_INVALID; }
  if (!strcmp(key, "hystIdx"))      { return idxParam(s.hystIdx, NUM_HYST) ? FX_NONE : FX_INVALID; }
  if (!strcmp(key, "trigMode"))     {
    if (!idxParam(s.trigMode, TRIG_MODE_COUNT)) return FX_INVALID;
    if (s.trigMode == TRIG_SINGLE) s.running = true;
    return FX_NONE;
  }
  if (!strcmp(key, "calFreqIdx"))   { return idxParam(s.calFreqIdx, NUM_CAL_FREQS) ? FX_CAL : FX_INVALID; }
  if (!strcmp(key, "brightness"))   { return idxParam(s.brightnessIdx, NUM_BRIGHTNESS) ? FX_BACKLIGHT : FX_INVALID; }
  if (!strcmp(key, "trigVolts"))    {
    if (!parseFloatStrict(val, f) || fabsf(f) > 100.0f) return FX_INVALID;
    s.trigVolts = f; s.userSetTrig = true; return FX_NONE;
  }
  if (!strcmp(key, "trigAuto"))     { if (!parseBool(val, b)) return FX_INVALID; if (b) s.userSetTrig = false; return FX_NONE; }
  if (!strcmp(key, "single"))       { if (!parseBool(val, b)) return FX_INVALID; if (b) { s.trigMode = TRIG_SINGLE; s.running = true; } return FX_NONE; }
  if (!strcmp(key, "autoset") || !strcmp(key, "auto")) {
    if (!parseBool(val, b)) return FX_INVALID;
    s.autoTimebase = b;
    s.autoVolts = b;
    if (b) {
      s.userSetTrig = false;
      s.running = true;
      if (s.trigMode == TRIG_SINGLE || s.trigMode == TRIG_FREE) s.trigMode = TRIG_RISING;
    }
    return FX_NONE;
  }
  if (!strcmp(key, "autoT") || !strcmp(key, "autoTimebase")) {
    if (!parseBool(val, b)) return FX_INVALID;
    s.autoTimebase = b;
    return FX_NONE;
  }
  if (!strcmp(key, "autoV") || !strcmp(key, "autoVolts")) {
    if (!parseBool(val, b)) return FX_INVALID;
    s.autoVolts = b;
    if (b) s.userSetTrig = false;
    return FX_NONE;
  }
  if (!strcmp(key, "reconnect"))    { if (!parseBool(val, b)) return FX_INVALID; return b ? FX_RECONNECT : FX_NONE; }
  if (!strcmp(key, "fftLog"))       { if (!parseBool(val, b)) return FX_INVALID; s.fftLogScale = b; return FX_NONE; }
  if (!strcmp(key, "fftSpan"))      { return idxParam(s.fftSpanIdx, NUM_SAMPLE_RATES) ? FX_NONE : FX_INVALID; }
  if (!strcmp(key, "fftZoom"))      { return idxParam(s.fftZoomIdx, NUM_FFT_ZOOMS) ? FX_NONE : FX_INVALID; }
  return FX_UNKNOWN_KEY;
}

inline int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// In-place URL decoding (%XX and '+'). Returns false on malformed escapes.
inline bool urlDecode(char* s) {
  char* w = s;
  for (char* r = s; *r; ++r) {
    if (*r == '+') { *w++ = ' '; }
    else if (*r == '%') {
      int h = hexNibble(r[1]), l = (h >= 0) ? hexNibble(r[2]) : -1;
      if (h < 0 || l < 0) { *w = '\0'; return false; }
      *w++ = (char)((h << 4) | l);
      r += 2;
    } else { *w++ = *r; }
  }
  *w = '\0';
  return true;
}

// Applies "k1=v1&k2=v2..." (used by WebSocket commands). Max 256 chars.
inline uint32_t applyQuery(Settings& s, const char* query) {
  if (query == nullptr) return FX_INVALID;
  char buf[257];
  size_t len = strlen(query);
  if (len >= sizeof(buf)) return FX_INVALID;
  memcpy(buf, query, len + 1);
  uint32_t fx = FX_NONE;
  char* save = buf;
  while (save && *save) {
    char* pair = save;
    char* amp = strchr(pair, '&');
    if (amp) { *amp = '\0'; save = amp + 1; } else { save = nullptr; }
    if (*pair == '\0') continue;
    char* eq = strchr(pair, '=');
    if (!eq) { fx |= FX_INVALID; continue; }
    *eq = '\0';
    char* key = pair;
    char* val = eq + 1;
    if (!urlDecode(key) || !urlDecode(val)) { fx |= FX_INVALID; continue; }
    fx |= applyParam(s, key, val);
  }
  return fx;
}

} // namespace dso
