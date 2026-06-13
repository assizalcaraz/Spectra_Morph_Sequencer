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
#include <vector>
#include <numbers>

static void fill_triangle(float* buf, uint32_t n, float freq, float sr, float amp) {
    const float period = sr / freq;
    for (uint32_t i = 0; i < n; ++i) {
        const float t = std::fmod(static_cast<float>(i), period) / period;
        const float tri = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
        buf[i] = amp * tri;
    }
}

void test_triangle_rms_ratio() {
    printf("  triangle_rms_ratio: ");
    constexpr uint32_t N = 2048;
    constexpr uint32_t H = 512;
    constexpr float SR = 48000.0f;
    constexpr int FRAMES = 64;
    constexpr float F0 = 400.0f;

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
    fill_triangle(frame.data(), BUF, F0, SR, 0.4f);

    Peak peaks[MAX_PEAKS];
    uint32_t frame_counter = 0;
    float tracked_f0 = 0.0f;
    float input_rms = 0.0f;
    float output_rms = 0.0f;
    uint32_t num_partials = 0;
    uint64_t phase_seed = 0;

    for (int f = 0; f < FRAMES; ++f) {
        const uint32_t offset = (static_cast<uint32_t>(f) * H) % (BUF - H);

        std::vector<float> hop(H);
        for (uint32_t i = 0; i < H; ++i)
            hop[i] = frame[offset + i];

        float sum_sq = 0.0f;
        for (uint32_t i = 0; i < H; ++i)
            sum_sq += hop[i] * hop[i];
        input_rms = std::sqrt(sum_sq / static_cast<float>(H));

        fft.process(hop.data());

        uint32_t num_peaks = 0;
        const float* mag = fft.magnitude();
        float score = 0.0f;
        const float cand = PeakUtils::find_fundamental_hps(
            mag, fft.half_n(), SR, N, 0.001f, &score);
        tracked_f0 = PeakUtils::update_f0_ema(tracked_f0, cand, score);
        const float f0 = (tracked_f0 >= 40.0f) ? tracked_f0 : cand;

        PeakUtils::build_harmonic_peaks(
            peaks, num_peaks, f0, mag, fft.raw_phase(), SR, N, fft.half_n(),
            false, 1.0f, 1.0f, MAX_PEAKS);

        ++frame_counter;
        tracker.sync_faithful(peaks, num_peaks, pool, frame_counter, f0);

        ParticleSnapshot snap{};
        pool.write_snapshot(snap, frame_counter);
        num_partials = snap.num_partials;
        PhaseManager::apply_to_snapshot(snap, f0, 1.0f, phase_seed);

        resynth.render(snap, fft, 0.85f, 0.0f, 1.0f, 0.0f,
                       TransientMode::Protect, f0, hop.data(), input_rms, 1.0f);

        if (f >= 24)
            output_rms = SpectralMetrics::compute_rms(resynth.output_buffer(), H);
    }

    const float ratio = output_rms / (input_rms + 1e-9f);
    assert(num_partials >= 8u);
    assert(ratio > 0.35f);
    printf("OK (ratio=%.2f, partials=%u, f0=%.0f Hz)\n",
           ratio, num_partials, tracked_f0);
}
