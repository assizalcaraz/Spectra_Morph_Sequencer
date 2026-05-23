#include "../src/dsp/fft/FFTProcessor.h"
#include "../src/dsp/tracking/PeakUtils.h"
#include "../src/dsp/tracking/PartialTracker.h"
#include "../src/core/memory/PartialPool.h"
#include <cmath>
#include <cstdio>
#include <cassert>
#include <vector>
#include <numbers>

static void fill_sine(float* buf, uint32_t n, float freq, float sr, float amp) {
    const float two_pi = 2.0f * std::numbers::pi_v<float>;
    for (uint32_t i = 0; i < n; ++i)
        buf[i] = amp * std::sin(two_pi * freq * static_cast<float>(i) / sr);
}

void test_fft_sine() {
    printf("  fft_sine: ");
    constexpr uint32_t N = 2048;
    constexpr uint32_t H = 512;
    constexpr float SR = 48000.0f;

    FFTProcessor fft;
    fft.prepare(N, H, SR);

    std::vector<float> hop(H, 0.0f);
    std::vector<float> frame(N, 0.0f);
    fill_sine(frame.data(), N, 440.0f, SR, 0.5f);

    for (uint32_t offset = 0; offset + H <= N; offset += H)
        fft.process(frame.data() + offset);

    const float* mag = fft.magnitude();
    const uint32_t bin440 = static_cast<uint32_t>(440.0f * N / SR);
    float peak_mag = 0.0f;
    uint32_t peak_bin = 0;
    for (uint32_t k = 1; k < fft.half_n(); ++k) {
        if (mag[k] > peak_mag) {
            peak_mag = mag[k];
            peak_bin = k;
        }
    }

    assert(std::abs(static_cast<int>(peak_bin) - static_cast<int>(bin440)) <= 2);
    assert(peak_mag > 0.01f);

    float side_energy = 0.0f;
    for (int d = -5; d <= 5; ++d) {
        if (d == 0) continue;
        const int k = static_cast<int>(peak_bin) + d;
        if (k > 0 && static_cast<uint32_t>(k) < fft.half_n())
            side_energy += mag[static_cast<uint32_t>(k)];
    }
    assert(side_energy < peak_mag * 0.5f);

    printf("OK\n");
}

void test_harmonic_count_triangle() {
    printf("  harmonic_triangle: ");
    constexpr uint32_t N = 2048;
    constexpr float SR = 48000.0f;
    constexpr float F0 = 861.0f;
    const uint32_t half_n = N / 2;

    std::vector<float> mag(half_n + 1, 0.0f);
    std::vector<float> phase(half_n + 1, 0.0f);

    for (int h = 1; h <= 40; h += 2) {
        const uint32_t bin = static_cast<uint32_t>(
            std::round(F0 * static_cast<float>(h) * N / SR));
        if (bin < half_n)
            mag[bin] = 1.0f / static_cast<float>(h);
    }

    Peak peaks[MAX_PEAKS];
    uint32_t num_peaks = 0;
    PeakUtils::build_harmonic_peaks(
        peaks, num_peaks, F0, mag.data(), phase.data(),
        SR, N, half_n, false, MAX_PEAKS);

    assert(num_peaks >= 15);

    printf("OK (%u peaks)\n", num_peaks);
}

void test_tracking_1to1() {
    printf("  tracking_1to1: ");
    PartialPool pool;
    PartialTracker tracker;
    tracker.prepare(48000.0f, 2048, 512);
    tracker.set_coherence(1.0f);

    Peak peaks[2];
    peaks[0].frequency = 440.0f;
    peaks[1].frequency = 880.0f;
    peaks[0].magnitude = 0.5f;
    peaks[1].magnitude = 0.3f;
    peaks[0].phase = 0.0f;
    peaks[1].phase = 0.0f;

    tracker.track(peaks, 2, pool, 1);
    assert(pool.num_active() == 2);

    peaks[0].frequency = 445.0f;
    peaks[1].frequency = 875.0f;
    tracker.track(peaks, 2, pool, 2);
    assert(pool.num_active() == 2);

    float f0 = pool[0].frequency;
    float f1 = pool[1].frequency;
    assert(std::abs(f0 - 445.0f) < 20.0f);
    assert(std::abs(f1 - 875.0f) < 20.0f);
    assert(std::abs(f0 - f1) > 100.0f);

    printf("OK\n");
}

void test_faithful_sync() {
    printf("  faithful_sync: ");
    PartialPool pool;
    PartialTracker tracker;
    tracker.prepare(48000.0f, 2048, 512);

    Peak peaks[8];
    for (uint32_t i = 0; i < 8; ++i) {
        peaks[i].frequency = 220.0f * static_cast<float>(i + 1);
        peaks[i].magnitude = 0.2f;
        peaks[i].phase = 0.0f;
    }

    tracker.sync_faithful(peaks, 8, pool, 1);
    assert(pool.num_active() == 8);

    printf("OK\n");
}
