#include "../src/dsp/fft/FFTProcessor.h"
#include "../src/dsp/tracking/PartialTracker.h"
#include "../src/dsp/tracking/PeakUtils.h"
#include "../src/simulation/ParticleSimulator.h"
#include "../src/core/memory/PartialPool.h"
#include <cmath>
#include <cstdio>
#include <cassert>
#include <algorithm>
#include <vector>
#include <numbers>

static void fill_sine(float* buf, uint32_t n, float freq, float sr, float amp) {
    const float two_pi = 2.0f * std::numbers::pi_v<float>;
    for (uint32_t i = 0; i < n; ++i)
        buf[i] = amp * std::sin(two_pi * freq * static_cast<float>(i) / sr);
}

static void fill_triangle(float* buf, uint32_t n, float freq, float sr, float amp) {
    const float period = sr / freq;
    for (uint32_t i = 0; i < n; ++i) {
        const float t = std::fmod(static_cast<float>(i), period) / period;
        const float tri = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
        buf[i] = amp * tri;
    }
}

void test_f0_sine_440() {
    printf("  f0_sine_440: ");
    constexpr uint32_t N = 2048;
    constexpr uint32_t H = 512;
    constexpr float SR = 48000.0f;

    FFTProcessor fft;
    fft.prepare(N, H, SR);

    constexpr uint32_t BUF = N * 8;
    std::vector<float> frame(BUF, 0.0f);
    fill_sine(frame.data(), BUF, 440.0f, SR, 0.5f);

    float tracked_f0 = 0.0f;
    for (int f = 0; f < 32; ++f) {
        std::vector<float> hop(H);
        const uint32_t offset = (static_cast<uint32_t>(f) * H) % (BUF - H);
        for (uint32_t i = 0; i < H; ++i)
            hop[i] = frame[offset + i];

        fft.process(hop.data());
        const float* mag = fft.magnitude();
        float score = 0.0f;
        const float cand = PeakUtils::find_fundamental_hps(
            mag, fft.half_n(), SR, N, 0.001f, &score);
        tracked_f0 = PeakUtils::update_f0_ema(tracked_f0, cand, score);
    }

    assert(tracked_f0 >= 430.0f && tracked_f0 <= 450.0f);
    printf("OK (f0=%.1f Hz)\n", tracked_f0);
}

void test_f0_triangle() {
    printf("  f0_triangle: ");
    constexpr uint32_t N = 2048;
    constexpr uint32_t H = 512;
    constexpr float SR = 48000.0f;
    const uint32_t half_n = N / 2;

    FFTProcessor fft;
    fft.prepare(N, H, SR);

    constexpr uint32_t BUF = N * 8;
    std::vector<float> frame(BUF, 0.0f);
    fill_triangle(frame.data(), BUF, 400.0f, SR, 0.4f);

    float tracked_f0 = 0.0f;
    for (int f = 0; f < 24; ++f) {
        std::vector<float> hop(H);
        const uint32_t offset = (static_cast<uint32_t>(f) * H) % (BUF - H);
        for (uint32_t i = 0; i < H; ++i)
            hop[i] = frame[offset + i];

        fft.process(hop.data());
        const float* mag = fft.magnitude();
        float score = 0.0f;
        const float cand = PeakUtils::find_fundamental_hps(
            mag, fft.half_n(), SR, N, 0.001f, &score);
        tracked_f0 = PeakUtils::update_f0_ema(tracked_f0, cand, score);
    }

    assert(tracked_f0 >= 320.0f && tracked_f0 <= 480.0f);

    std::vector<float> mag(half_n + 1, 0.0f);
    std::vector<float> phase(half_n + 1, 0.0f);
    for (int h = 1; h <= 40; h += 2) {
        const uint32_t bin = static_cast<uint32_t>(
            std::round(400.0f * static_cast<float>(h) * N / SR));
        if (bin < 1 || bin >= half_n - 1) continue;
        const float m = 1.0f / static_cast<float>(h);
        mag[bin] = m;
        mag[bin - 1] = m * 0.25f;
        mag[bin + 1] = m * 0.25f;
    }

    Peak peaks[MAX_PEAKS];
    uint32_t num_peaks = 0;
    PeakUtils::build_harmonic_peaks(
        peaks, num_peaks, 400.0f, mag.data(), phase.data(),
        SR, N, half_n, true, 1.0f, MAX_PEAKS);

    assert(num_peaks >= 12u);
    printf("OK (f0=%.0f Hz, peaks=%u)\n", tracked_f0, num_peaks);
}

void test_harmonic_affinity() {
    printf("  harmonic_affinity: ");
    constexpr uint32_t N = 2048;
    constexpr uint32_t H = 512;
    constexpr float SR = 48000.0f;

    FFTProcessor fft;
    fft.prepare(N, H, SR);
    PartialPool pool;
    PartialTracker tracker;
    tracker.prepare(SR, N, H);

    constexpr uint32_t BUF = N * 8;
    std::vector<float> frame(BUF, 0.0f);
    fill_sine(frame.data(), BUF, 440.0f, SR, 0.5f);

    Peak peaks[MAX_PEAKS];
    float tracked_f0 = 0.0f;

    for (int f = 0; f < 16; ++f) {
        std::vector<float> hop(H);
        const uint32_t offset = (static_cast<uint32_t>(f) * H) % (BUF - H);
        for (uint32_t i = 0; i < H; ++i)
            hop[i] = frame[offset + i];

        fft.process(hop.data());
        uint32_t num_peaks = 0;
        float score = 0.0f;
        const float cand = PeakUtils::find_fundamental_hps(
            fft.magnitude(), fft.half_n(), SR, N, 0.001f, &score);
        tracked_f0 = PeakUtils::update_f0_ema(tracked_f0, cand, score);

        PeakUtils::build_harmonic_peaks(
            peaks, num_peaks, tracked_f0, fft.magnitude(), fft.raw_phase(),
            SR, N, fft.half_n(), false, 1.0f, MAX_PEAKS);

        tracker.sync_faithful(
            peaks, num_peaks, pool, static_cast<uint32_t>(f + 1), tracked_f0);
    }

    uint32_t strong = 0;
    uint32_t total = 0;
    for (uint32_t i = 0; i < MAX_PARTIALS; ++i) {
        if (!pool.is_alive(i)) continue;
        ++total;
        if (pool[i].harmonic_affinity > 0.7f)
            ++strong;
    }

    assert(total > 0);
    assert(static_cast<float>(strong) / static_cast<float>(total) > 0.8f);
    printf("OK (%u/%u affinity>0.7)\n", strong, total);
}

void test_gravity_pull() {
    printf("  gravity_pull: ");
    ParticleSnapshot snap{};
    snap.frame_number = 1;
    snap.num_partials = 1;
    snap.partials[0].frequency = 440.0f;
    snap.partials[0].amplitude = 0.5f;
    snap.partials[0].spectral_pos = std::log2(440.0f / 20.0f) + 0.35f;
    snap.partials[0].harmonic_affinity = 0.95f;
    snap.partials[0].coherence = 0.9f;
    snap.partials[0].energy = 0.5f;
    snap.partials[0].temperature = 0.1f;
    snap.partials[0].velocity = 0.0f;

    const float start_pos = snap.partials[0].spectral_pos;
    const float target = std::round(start_pos);

    SimParams params;
    params.gravity = 10.0f;
    params.motion  = 0.0f;
    params.decay   = 0.0f;
    params.spread  = 0.0f;

    ParticleSimulator sim;
    uint64_t seed = 1;
    ParticleSnapshot out{};

    for (int i = 0; i < 24; ++i)
        sim.step(snap, out, params, seed);

    const float end_pos = out.partials[0].spectral_pos;
    const float moved_toward = std::abs(end_pos - target)
                             < std::abs(start_pos - target);
    assert(moved_toward);
    printf("OK (pos %.3f -> %.3f, target %.0f)\n", start_pos, end_pos, target);
}
