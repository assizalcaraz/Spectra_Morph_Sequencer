#include "../src/dsp/fft/FFTProcessor.h"
#include "../src/dsp/tracking/PartialTracker.h"
#include "../src/dsp/tracking/PeakUtils.h"
#include "../src/dsp/resynthesis/Resynthesizer.h"
#include "../src/dsp/debug/SpectralMetrics.h"
#include "../src/dsp/phase/PhaseManager.h"
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

void test_sine_snr() {
    printf("  sine_snr: ");
    constexpr uint32_t N = 2048;
    constexpr uint32_t H = 512;
    constexpr float SR = 48000.0f;
    constexpr int FRAMES = 64;

    FFTProcessor fft;
    fft.prepare(N, H, SR);
    PartialPool pool;
    PartialTracker tracker;
    tracker.prepare(SR, N, H);
    tracker.set_coherence(1.0f);
    Resynthesizer resynth;
    resynth.prepare(SR, N, H);

    constexpr uint32_t BUF = N * 8;
    std::vector<float> frame(BUF, 0.0f);
    fill_sine(frame.data(), BUF, 440.0f, SR, 0.5f);

    float input_rms = 0.0f;
    float output_rms = 0.0f;
    Peak peaks[MAX_PEAKS];
    uint32_t frame_counter = 0;
    float tracked_f0 = 0.0f;
    float f0 = 440.0f;
    float best_snr = -999.0f;
    uint64_t phase_seed = 0;

    for (int f = 0; f < FRAMES; ++f) {
        const uint32_t offset = (static_cast<uint32_t>(f) * H) % (BUF - H);

        std::vector<float> hop(H);
        for (uint32_t i = 0; i < H; ++i)
            hop[i] = frame[offset + i];

        float sum_sq = 0.0f;
        for (uint32_t i = 0; i < H; ++i)
            sum_sq += hop[i] * hop[i];
        const float in_rms = std::sqrt(sum_sq / static_cast<float>(H));
        input_rms = in_rms;

        fft.process(hop.data());

        uint32_t num_peaks = 0;
        const float* mag = fft.magnitude();
        float score = 0.0f;
        const float cand = PeakUtils::find_fundamental_hps(
            mag, fft.half_n(), SR, N, 0.001f, &score);
        tracked_f0 = PeakUtils::update_f0_ema(tracked_f0, cand, score);
        f0 = (tracked_f0 >= 40.0f) ? tracked_f0 : cand;
        PeakUtils::build_harmonic_peaks(
            peaks, num_peaks, f0, mag, fft.raw_phase(), SR, N, fft.half_n(),
            false, 1.0f, MAX_PEAKS);

        ++frame_counter;
        tracker.sync_faithful(peaks, num_peaks, pool, frame_counter, f0);

        ParticleSnapshot snap{};
        pool.write_snapshot(snap, frame_counter);
        PhaseManager::apply_to_snapshot(snap, f0, 1.0f, phase_seed);

        resynth.render(snap, fft, 1.0f, 0.0f, 1.0f, 0.0f,
                       TransientMode::Protect, f0, hop.data(), in_rms);

        if (f >= 24) {
            const float* out = resynth.output_buffer();
            FFTProcessor out_fft;
            out_fft.prepare(N, H, SR);
            out_fft.process(const_cast<float*>(out));
            const float* out_mag = out_fft.magnitude();
            const uint32_t bin440 = static_cast<uint32_t>(440.0f * N / SR);
            float peak_mag = 0.0f;
            for (int d = -2; d <= 2; ++d) {
                const int k = static_cast<int>(bin440) + d;
                if (k > 0 && static_cast<uint32_t>(k) < out_fft.half_n())
                    peak_mag = std::max(peak_mag, out_mag[static_cast<uint32_t>(k)]);
            }
            float side = 0.0f;
            for (int d = -8; d <= 8; ++d) {
                if (d >= -2 && d <= 2) continue;
                const int k = static_cast<int>(bin440) + d;
                if (k > 0 && static_cast<uint32_t>(k) < out_fft.half_n())
                    side += out_mag[static_cast<uint32_t>(k)];
            }
            const float snr = SpectralMetrics::compute_snr_db(
                peak_mag, side / 7.0f + 1e-9f);
            best_snr = std::max(best_snr, snr);
        }
    }

    const float* out = resynth.output_buffer();
    output_rms = SpectralMetrics::compute_rms(out, H);

    const float snr = best_snr;
    assert(snr > 5.0f);
    assert(output_rms > 0.01f);
    printf("OK (spectral_SNR=%.1f dB, f0=%.0f Hz)\n", snr, f0);
}

void test_partial_stability() {
    printf("  partial_stability: ");
    constexpr uint32_t N = 2048;
    constexpr uint32_t H = 512;
    constexpr float SR = 48000.0f;

    FFTProcessor fft;
    fft.prepare(N, H, SR);
    PartialPool pool;
    PartialTracker tracker;
    tracker.prepare(SR, N, H);
    tracker.set_coherence(1.0f);

    constexpr uint32_t BUF = N * 8;
    std::vector<float> frame(BUF, 0.0f);
    fill_sine(frame.data(), BUF, 440.0f, SR, 0.5f);

    Peak peaks[MAX_PEAKS];
    float prev_lowest = 440.0f;
    uint32_t stable_frames = 0;
    const int FRAMES = 100;

    for (int f = 0; f < FRAMES; ++f) {
        const uint32_t offset = (static_cast<uint32_t>(f) * H) % (BUF - H);
        std::vector<float> hop(H);
        for (uint32_t i = 0; i < H; ++i)
            hop[i] = frame[offset + i];

        fft.process(hop.data());
        uint32_t num_peaks = 0;
        const float* mag = fft.magnitude();
        float score = 0.0f;
        static float tracked_f0 = 0.0f;
        const float cand = PeakUtils::find_fundamental_hps(
            mag, fft.half_n(), SR, N, 0.001f, &score);
        tracked_f0 = PeakUtils::update_f0_ema(tracked_f0, cand, score);
        const float f0 = (tracked_f0 >= 40.0f) ? tracked_f0 : cand;
        PeakUtils::build_harmonic_peaks(
            peaks, num_peaks, f0, mag, fft.raw_phase(), SR, N, fft.half_n(),
            false, 1.0f, MAX_PEAKS);

        tracker.sync_faithful(
            peaks, num_peaks, pool, static_cast<uint32_t>(f + 1), f0);

        float lowest = 1e9f;
        for (uint32_t i = 0; i < MAX_PARTIALS; ++i) {
            if (!pool.is_alive(i)) continue;
            lowest = std::min(lowest, pool[i].frequency);
        }
        if (f > 0 && lowest < 500.0f && std::abs(lowest - prev_lowest) < 2.0f)
            ++stable_frames;
        prev_lowest = lowest;
    }

    const float ratio = static_cast<float>(stable_frames)
                      / static_cast<float>(FRAMES - 1);
    assert(ratio > 0.85f);
    printf("OK (stable=%.0f%%)\n", ratio * 100.0f);
}

void test_silence_floor() {
    printf("  silence_floor: ");
    constexpr uint32_t N = 2048;
    constexpr uint32_t H = 512;
    constexpr float SR = 48000.0f;

    FFTProcessor fft;
    fft.prepare(N, H, SR);
    Resynthesizer resynth;
    resynth.prepare(SR, N, H);

    std::vector<float> hop(H, 0.0f);
    ParticleSnapshot snap{};

    float max_rms = 0.0f;
    for (int f = 0; f < 32; ++f) {
        fft.process(hop.data());
        resynth.render(snap, fft, 0.0f, 0.0f, 1.0f, 0.0f,
                       TransientMode::Protect, 0.0f, hop.data(), 0.0f);
        const float rms = SpectralMetrics::compute_rms(resynth.output_buffer(), H);
        max_rms = std::max(max_rms, rms);
    }

    const float dbfs = 20.0f * std::log10(max_rms + 1e-12f);
    assert(dbfs < -80.0f);
    printf("OK (%.1f dBFS)\n", dbfs);
}

void test_transient_detect() {
    printf("  transient_detect: ");
    constexpr uint32_t N = 2048;
    constexpr uint32_t H = 512;
    constexpr float SR = 48000.0f;

    FFTProcessor fft;
    fft.prepare(N, H, SR);

    std::vector<float> hop(H, 0.0f);
    std::vector<float> impulse(H, 0.0f);
    impulse[0] = 1.0f;

    bool detected = false;
    for (int f = 0; f < 8; ++f) {
        const float* in = (f == 2) ? impulse.data() : hop.data();
        fft.process(in);
        if (fft.is_transient())
            detected = true;
    }

    assert(detected);
    printf("OK\n");
}
