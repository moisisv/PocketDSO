#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include "../oscilloscope/dso_core.h"

using namespace dso;

void test_metrics_sine() {
    std::cout << "[Test] Signal Metrics (Sine Wave)... ";
    const size_t N = 1024;
    float buf[N];
    float fs = 200000.0f; // 200 kHz
    float f_sig = 1000.0f; // 1 kHz
    for (size_t i = 0; i < N; ++i) {
        float t = (float)i / fs;
        buf[i] = 1.65f + 1.0f * sinf(2.0f * M_PI * f_sig * t); // 1.65V DC, 2.0Vpp, 1kHz
    }
    Metrics m = analyze(buf, N, fs, 0.05f);
    assert(fabsf(m.dc - 1.65f) < 0.05f);
    assert(fabsf(m.vpp - 2.00f) < 0.05f);
    assert(fabsf(m.rms - 0.707f) < 0.05f); // 1.0 / sqrt(2) ~ 0.707
    assert(fabsf(m.freq - 1000.0f) < 20.0f);
    std::cout << "PASSED (DC=" << m.dc << "V, Vpp=" << m.vpp << "V, Freq=" << m.freq << "Hz)\n";
}

void test_metrics_square() {
    std::cout << "[Test] Signal Metrics (Square Wave & Duty)... ";
    const size_t N = 1024;
    float buf[N];
    float fs = 200000.0f;
    float f_sig = 2500.0f; // 2.5 kHz
    for (size_t i = 0; i < N; ++i) {
        float t = (float)i / fs;
        float phase = fmodf(t * f_sig, 1.0f);
        buf[i] = (phase < 0.30f) ? 3.3f : 0.0f; // 30% duty cycle
    }
    Metrics m = analyze(buf, N, fs, 0.05f);
    assert(fabsf(m.vpp - 3.3f) < 0.1f);
    assert(fabsf(m.freq - 2500.0f) < 50.0f);
    assert(m.duty > 25.0f && m.duty < 35.0f);
    std::cout << "PASSED (Freq=" << m.freq << "Hz, Duty=" << m.duty << "%)\n";
}

void test_autoscale() {
    std::cout << "[Test] Auto-scale logic... ";
    Settings s;
    s.autoTimebase = true;
    s.autoVolts = true;
    Metrics m;
    m.freq = 1000.0f; // 1 kHz -> target period ~ 1ms. 3 periods = 3ms across 10 div => 300us/div -> 200us or 500us
    m.vpp = 3.0f;
    m.vmax = 3.3f;
    m.vmin = 0.3f;
    autoScale(s, m);
    assert(s.vDivIdx >= 2); // At least 0.5V or 1V/div
    assert(s.timeBaseIdx == 3 || s.timeBaseIdx == 4); // 200us or 500us
    std::cout << "PASSED (Timebase=" << TIME_BASE_LABELS[s.timeBaseIdx] << ", VDiv=" << V_DIV_LABELS[s.vDivIdx] << ")\n";
}

void test_trigger_view() {
    std::cout << "[Test] Trigger & View Window... ";
    const size_t N = 1024;
    float buf[N];
    float fs = 200000.0f;
    float f_sig = 1000.0f;
    for (size_t i = 0; i < N; ++i) {
        float t = (float)i / fs;
        buf[i] = 1.65f + 1.0f * sinf(2.0f * M_PI * f_sig * t);
    }
    Settings s;
    s.trigMode = TRIG_RISING;
    s.trigVolts = 1.65f;
    s.timeBaseIdx = 3; // 200us/div -> 2ms total screen
    View v = computeView(buf, N, fs, s, 2.0f);
    assert(v.triggered == true);
    assert(v.start > 0);
    std::cout << "PASSED (Triggered at sample " << v.start << ", span=" << v.span << ")\n";
}

void test_query_parser() {
    std::cout << "[Test] Web Query & Parameter Parser... ";
    Settings s;
    s.running = true;
    s.vDivIdx = 0;
    uint32_t fx = applyQuery(s, "running=0&vDivIdx=3&trigVolts=2.15&ch2Mode=2&probeIdx=1&probeBIdx=2");
    assert(s.running == false);
    assert(s.vDivIdx == 3);
    assert(fabsf(s.trigVolts - 2.15f) < 0.01f);
    assert(s.ch2Mode == CH2_DIGITAL);
    assert(s.probeIdx == 1);
    assert(s.probeBIdx == 2);
    assert((fx & FX_INVALID) == 0);
    std::cout << "PASSED\n";
}

void test_serialization() {
    std::cout << "[Test] Settings NVS Serialization... ";
    Settings s1, s2;
    s1.trigVolts = 2.45f;
    s1.timeBaseIdx = 5;
    s1.vDivIdx = 4;
    s1.probeIdx = 1;  // Probe A (2x default)
    s1.probeBIdx = 2; // Probe B (4x default)
    s1.fftSpanIdx = 2;
    s1.fftLogScale = false;
    s1.ch2Mode = CH2_DIGITAL;
    s1.autoTimebase = false;
    uint8_t blob[SETTINGS_BLOB_SIZE];
    serializeSettings(s1, blob);
    bool ok = deserializeSettings(blob, sizeof(blob), s2);
    assert(ok == true);
    assert(fabsf(s1.trigVolts - s2.trigVolts) < 0.001f);
    assert(s1.timeBaseIdx == s2.timeBaseIdx);
    assert(s1.vDivIdx == s2.vDivIdx);
    assert(s1.probeIdx == s2.probeIdx);
    assert(s1.probeBIdx == s2.probeBIdx);
    assert(s1.fftSpanIdx == s2.fftSpanIdx);
    assert(s1.fftLogScale == s2.fftLogScale);
    assert(s1.ch2Mode == s2.ch2Mode);
    assert(s1.autoTimebase == s2.autoTimebase);
    std::cout << "PASSED (ProbeA=" << PROBE_LABELS[s2.probeIdx] << ", ProbeB=" << PROBE_LABELS[s2.probeBIdx] << ", FFTLog=" << (s2.fftLogScale ? "1" : "0") << ")\n";
}

// Additional edge‑case test functions
void test_metrics_extremes() {
    std::cout << "[Test] Signal Metrics – Low Amplitude & Frequency... ";
    const size_t N = 2048;
    float buf[N];
    float fs = 20000.0f; // 20 kHz
    float f_sig = 50.0f; // 50 Hz
    for (size_t i = 0; i < N; ++i) {
        float t = (float)i / fs;
        buf[i] = 1.0f + 0.05f * sinf(2.0f * M_PI * f_sig * t); // 1.0V DC, 0.10Vpp
    }
    Metrics m = analyze(buf, N, fs, 0.02f);
    assert(fabsf(m.dc - 1.0f) < 0.02f);
    assert(fabsf(m.vpp - 0.10f) < 0.02f);
    assert(fabsf(m.freq - 50.0f) < 5.0f);
    std::cout << "PASSED\n";
}

void test_metrics_saturation() {
    std::cout << "[Test] Signal Metrics – Flat DC / Saturation... ";
    const size_t N = 1024;
    float buf[N];
    float fs = 200000.0f;
    for (size_t i = 0; i < N; ++i) buf[i] = 3.3f;
    Metrics m = analyze(buf, N, fs, 0.05f);
    assert(fabsf(m.dc - 3.3f) < 0.02f);
    assert(fabsf(m.vpp) < 0.02f);
    assert(fabsf(m.rms) < 0.02f); // AC ripple on flat DC line is 0.0V
    std::cout << "PASSED\n";
}

void test_fft_multi_tone() {
    std::cout << "[Test] FFT – Multi‑tone analysis... ";
    const size_t N = 2048;
    float buf[N];
    float fs = 50000.0f; // 50 kHz sample rate
    for (size_t i = 0; i < N; ++i) {
        float t = (float)i / fs;
        buf[i] = 1.0f * sinf(2.0f * M_PI * 2000.0f * t) +
                 0.3f * sinf(2.0f * M_PI * 8000.0f * t);
    }
    FftResult fft;
    computeFft(buf, N, fs, fft);
    assert(fabsf(fft.peakFreq - 2000.0f) < 200.0f);
    std::cout << "PASSED\n";
}

void test_query_invalid() {
    std::cout << "[Test] Query parser – malformed input detection... ";
    Settings s;
    s.vDivIdx = 0;
    // Missing '=', extra '&', unknown key
    uint32_t fx = applyQuery(s, "running&vDivIdx=2&&unknown=5");
    assert(s.vDivIdx == 2);
    assert((fx & FX_INVALID) != 0);
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== Running PocketDSO Core Unit Tests ===\n";
    test_metrics_sine();
    test_metrics_square();
    test_autoscale();
    test_trigger_view();
    test_query_parser();
    test_serialization();
    test_metrics_extremes();
    test_metrics_saturation();
    test_fft_multi_tone();
    test_query_invalid();


    std::cout << "[Test] FFT Spectrum Analysis... ";
    {
        const size_t N = 1024;
        float buf[N];
        float fs = 50000.0f; // 50 kHz
        float f_sig = 5000.0f; // 5 kHz peak
        for (size_t i = 0; i < N; ++i) {
            float t = (float)i / fs;
            buf[i] = sinf(2.0f * M_PI * f_sig * t);
        }
        FftResult fft;
        computeFft(buf, N, fs, fft);
        assert(fft.binHz > 0.0f);
        assert(fabsf(fft.peakFreq - 5000.0f) < 400.0f);
        assert(fft.peakMag > 0.5f);
        std::cout << "PASSED (PeakFreq=" << fft.peakFreq << "Hz, BinWidth=" << fft.binHz << "Hz)\n";
    }

    std::cout << "[Test] Network Stats & RSSI calculation... ";
    {
        assert(rssiToBars(-55) == 4);
        assert(rssiToBars(-65) == 3);
        assert(rssiToBars(-75) == 2);
        assert(rssiToBars(-85) == 1);
        assert(rssiToBars(-95) == 0);
        assert(rssiToQuality(-50) == 100);
        assert(rssiToQuality(-100) == 0);
        assert(rssiToQuality(-75) == 50);
        std::cout << "PASSED\n";
    }

    std::cout << "[Test] Touch Hit-testing... ";
    {
        // Top bar buttons
        assert(hitTestMain(270, 10) == ACT_OPEN_MENU);
        assert(hitTestMain(230, 10) == ACT_OPEN_NET);
        assert(hitTestMain(210, 10) == ACT_OPEN_SYS);
        assert(hitTestMain(180, 10) == ACT_OPEN_FFT);
        // Chart drag
        assert(hitTestMain(100, 50) == ACT_CHART_TOUCH);
        // Bottom buttons
        assert(hitTestMain(20, 150) == ACT_TOGGLE_AUTO);
        assert(hitTestMain(260, 150) == ACT_RUN_TOGGLE);
        // Settings tabs
        assert(hitTestSettings(TAB_SCOPE, 10, 10) == ACT_TAB_SCOPE);
        assert(hitTestSettings(TAB_SCOPE, 60, 10) == ACT_TAB_TRIG);
        assert(hitTestSettings(TAB_SCOPE, 160, 10) == ACT_TAB_NET);
        assert(hitTestSettings(TAB_SCOPE, 280, 10) == ACT_CLOSE_MENU);
        assert(hitTestSettings(TAB_SCOPE, 250, 28) == ACT_CLOSE_MENU);

        // Standalone FFT Screen hit-testing
        assert(hitTestFft(20, 10) == ACT_CLOSE_FFT);          // [◀ SCOPE] top
        assert(hitTestFft(20, 150) == ACT_FFT_ZOOM_NEXT);     // [% ZOOM] bottom button 0
        assert(hitTestFft(120, 150) == ACT_FFT_SPAN_NEXT);    // [SPAN] bottom button 1
        assert(hitTestFft(200, 150) == ACT_FFT_SCALE_TOGGLE); // [LOG/LIN] bottom button 2
        assert(hitTestFft(280, 150) == ACT_RUN_TOGGLE);       // [RUN/HOLD] bottom button 3
        std::cout << "PASSED\n";
    }

    std::cout << "[Test] Battery SoC Curve... ";
    {
        assert(batteryPercent(3.0f) == 0);
        assert(batteryPercent(3.27f) == 0);
        assert(batteryPercent(4.20f) == 100);
        assert(batteryPercent(4.35f) == 100);
        uint8_t midPct = batteryPercent(3.80f);
        assert(midPct >= 35 && midPct <= 55);
        std::cout << "PASSED (3.80V -> " << (int)midPct << "%)\n";
    }

    std::cout << "[Test] Trace Generation & Volt-to-Pixel Mapping... ";
    {
        Settings s;
        s.vDivIdx = 2; // 0.5V/div => fullScale = 3.0V
        int16_t y0 = voltsToY(0.0f, 0.5f, false, 0.0f, CHART_Y, CHART_H);
        int16_t yMax = voltsToY(3.0f, 0.5f, false, 0.0f, CHART_Y, CHART_H);
        assert(y0 > yMax); // Y=0 is bottom (higher pixel number), Y=3V is top
        float vReconstructed = yToVolts(y0, 0.5f, false, 0.0f, CHART_Y, CHART_H);
        assert(fabsf(vReconstructed - 0.0f) < 0.05f);

        Trace t;
        float ch1[100];
        for (int i = 0; i < 100; ++i) ch1[i] = 1.5f;
        View v;
        v.start = 0; v.span = 100; v.triggered = true; v.trigFrac = 0.1f;
        buildTrace(t, ch1, nullptr, nullptr, 100, v, s, 1.5f, 0.0f);
        assert(t.y1[0] == t.y1[LCD_W - 1]);
        std::cout << "PASSED\n";
    }

    std::cout << "=== ALL TESTS PASSED SUCCESSFULLY! ===\n";
    return 0;
}
