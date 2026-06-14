#include "../src/dsp/fft/FFTProcessor.h"
#include "../src/dsp/tracking/PartialTracker.h"
#include "../src/dsp/tracking/PeakUtils.h"
#include "../src/dsp/resynthesis/Resynthesizer.h"
#include "../src/dsp/scramble/SpectralFrameStore.h"
#include "../src/dsp/scramble/TemporalScrambler.h"
#include "../src/dsp/phase/PhaseManager.h"
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

void test_file_granular_fidelity() {
    printf("  file_granular_fidelity: ");
    constexpr uint32_t N = 2048;
    constexpr uint32_t H = 512;
    constexpr float SR = 48000.0f;
    constexpr float F0 = 330.0f;
    constexpr int FRAMES = 64;

    FFTProcessor fft;
    fft.prepare(N, H, SR);
    PartialPool pool;
    PartialTracker tracker;
    tracker.prepare(SR, N, H);
    tracker.set_coherence(1.0f);
    Resynthesizer resynth;
    resynth.prepare(SR, N, H);
    SpectralFrameStore store;
    store.prepare(fft.half_n(), 64);

    const uint32_t half_bins = fft.half_n() + 1;
    std::vector<float> work_mag(half_bins), work_phase(half_bins), work_raw(half_bins);
    std::vector<float> vm(half_bins), vp(half_bins), vr(half_bins);
    std::vector<float> acc_re(half_bins), acc_im(half_bins);

    constexpr uint32_t BUF = N * 8;
    std::vector<float> frame(BUF, 0.0f);
    fill_sine(frame.data(), BUF, F0, SR, 0.35f);

    Peak peaks[MAX_PEAKS];
    float tracked_f0 = 0.0f;
    float input_rms = 0.0f;
    float output_rms = 0.0f;
    uint64_t phase_seed = 0;

    for (int f = 0; f < FRAMES; ++f) {
        std::vector<float> hop(H);
        const uint32_t offset = (static_cast<uint32_t>(f) * H) % (BUF - H);
        for (uint32_t i = 0; i < H; ++i)
            hop[i] = frame[offset + i];

        float sum_sq = 0.0f;
        for (uint32_t i = 0; i < H; ++i)
            sum_sq += hop[i] * hop[i];
        input_rms = std::sqrt(sum_sq / static_cast<float>(H));

        fft.process(hop.data());
        store.push(fft.magnitude(), fft.phase(), fft.raw_phase(), input_rms, f);

        TemporalScrambler::mix_scrambled_voices(
            store, store.num_frames() - 1, 0.0f, 8, 42, 0.0f, 1,
            work_mag.data(), work_phase.data(), work_raw.data(), half_bins,
            vm.data(), vp.data(), vr.data(), acc_re.data(), acc_im.data());

        fft.override_spectrum(work_mag.data(), work_phase.data(), work_raw.data());

        uint32_t num_peaks = 0;
        float f0 = tracked_f0;
        const float* mag = fft.magnitude();
        float score = 0.0f;
        const float cand = PeakUtils::find_fundamental_hps(
            mag, fft.half_n(), SR, N, 0.001f, &score);
        tracked_f0 = PeakUtils::update_f0_ema(tracked_f0, cand, score);
        if (tracked_f0 >= 40.0f)
            f0 = tracked_f0;

        PeakUtils::build_harmonic_peaks(
            peaks, num_peaks, f0, mag, fft.raw_phase(), SR, N, fft.half_n(),
            false, 1.0f, 1.0f, MAX_PEAKS);

        tracker.sync_faithful(peaks, num_peaks, pool, static_cast<uint32_t>(f + 1), f0);

        ParticleSnapshot snap{};
        pool.write_snapshot(snap, static_cast<uint32_t>(f + 1));
        PhaseManager::apply_to_snapshot(snap, f0, 1.0f, phase_seed);

        resynth.render(snap, fft, 0.85f, 0.0f, 1.0f, 0.0f,
                       TransientMode::Protect, f0, hop.data(), input_rms, 1.0f, 0.0f);

        if (f >= 48) {
            float out_sq = 0.0f;
            for (uint32_t i = 0; i < H; ++i) {
                const float s = resynth.output_buffer()[i];
                out_sq += s * s;
            }
            output_rms = std::sqrt(out_sq / static_cast<float>(H));
        }
    }

    const float ratio = output_rms / (input_rms + 1e-9f);
    assert(tracked_f0 >= 280.0f && tracked_f0 <= 380.0f);
    assert(ratio > 0.40f && ratio < 1.65f);
    printf("OK (ratio=%.2f, f0=%.0f Hz)\n", ratio, tracked_f0);
}
