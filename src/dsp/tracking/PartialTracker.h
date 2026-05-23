#pragma once

#include "../../core/types/Partial.h"
#include "../../core/types/Peak.h"
#include "../../core/types/Types.h"
#include "../../core/memory/PartialPool.h"
#include "PeakUtils.h"
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <numbers>

// SPECS_03 — Partial tracking with 1:1 peak-to-partial matching

class PartialTracker {
public:
    void prepare(float sample_rate, uint32_t fft_size, uint32_t hop_size) {
        sample_rate_         = sample_rate;
        fft_size_            = fft_size;
        hop_size_            = hop_size;
        max_freq_deviation_  = 0.5f * (sample_rate / fft_size);
        max_amp_deviation_   = 0.5f;
        max_hold_frames_     = 3;
    }

    void set_fundamental(float f0) { fundamental_f0_ = f0; }

    void track(const Peak* peaks, uint32_t num_peaks,
               PartialPool& pool, uint32_t frame_counter, float f0 = 0.0f)
    {
        births_ = 0;
        deaths_ = 0;
        if (f0 >= 40.0f) fundamental_f0_ = f0;

        bool peak_used[MAX_PEAKS] = {};
        bool partial_used[MAX_PARTIALS] = {};

        struct Match {
            int peak_idx;
            int partial_idx;
            float cost;
        };
        Match matches[MAX_PEAKS];
        uint32_t num_matches = 0;

        for (uint32_t p_idx = 0; p_idx < num_peaks; ++p_idx) {
            for (uint32_t i = 0; i < MAX_PARTIALS; ++i) {
                if (!pool.is_alive(i)) continue;

                const auto& part = pool[i];
                const float df = std::abs(peaks[p_idx].frequency - part.frequency);
                const float da = std::abs(peaks[p_idx].magnitude - part.amplitude);
                if (df >= max_freq_deviation_ || da >= max_amp_deviation_)
                    continue;

                const float cost = df / max_freq_deviation_
                                 + da / max_amp_deviation_;
                if (num_matches < MAX_PEAKS) {
                    matches[num_matches++] = {
                        static_cast<int>(p_idx),
                        static_cast<int>(i),
                        cost
                    };
                }
            }
        }

        std::sort(matches, matches + num_matches,
            [](const Match& a, const Match& b) { return a.cost < b.cost; });

        for (uint32_t m = 0; m < num_matches; ++m) {
            const int pi = matches[m].peak_idx;
            const int parti = matches[m].partial_idx;
            if (pi < 0 || parti < 0) continue;
            if (peak_used[static_cast<uint32_t>(pi)]
                || partial_used[static_cast<uint32_t>(parti)])
                continue;

            peak_used[static_cast<uint32_t>(pi)] = true;
            partial_used[static_cast<uint32_t>(parti)] = true;
            auto& part = pool[static_cast<uint32_t>(parti)];
            update_partial(part, peaks[static_cast<uint32_t>(pi)],
                           part.hold_counter == 0);
            part.hold_counter = 0;
        }

        for (uint32_t p_idx = 0; p_idx < num_peaks; ++p_idx) {
            if (peak_used[p_idx]) continue;
            if (pool.full()) break;

            const uint32_t id = pool.allocate();
            if (id >= MAX_PARTIALS) break;

            auto& p = pool[id];
            const auto& peak = peaks[p_idx];
            p.id                 = (frame_counter << 16) | id;
            p.birth_frame        = frame_counter;
            p.frequency          = peak.frequency;
            p.amplitude          = peak.magnitude;
            p.phase              = peak.phase;
            p.energy             = peak.magnitude * peak.magnitude;
            p.age                = 0.0f;
            p.lifetime_remaining = 4.0f * sample_rate_ / hop_size_;
            p.stability          = 1.0f;
            p.coherence          = 1.0f;
            p.harmonic_affinity  = PeakUtils::compute_harmonic_affinity(
                peak.frequency, fundamental_f0_);
            p.mass               = 1.0f;
            p.drift              = 0.0f;
            p.temperature        = 0.0f;
            p.spectral_pos       = std::log2(peak.frequency / 20.0f);
            p.velocity           = 0.0f;
            p.spatial_x          = 0.0f;
            p.spatial_y          = 0.0f;
            p.state              = ParticleState::Alive;
            p.niche              = Niche::None;
            p.hold_counter       = 0;
            ++births_;
        }

        for (uint32_t i = 0; i < MAX_PARTIALS; ++i) {
            if (!pool.is_alive(i)) continue;
            if (partial_used[i]) continue;

            auto& p = pool[i];
            if (p.state == ParticleState::Dead) continue;

            p.age += 1.0f;
            p.energy *= 0.9995f;
            ++p.hold_counter;
            p.stability *= 0.92f;

            if (p.hold_counter > max_hold_frames_
                || p.energy < 0.00005f
                || p.age > p.lifetime_remaining)
            {
                pool.free(i);
                ++deaths_;
            }
        }
    }

    // Faithful mode: 1:1 tracking without pool reset (preserves phase / particle state)
    void sync_faithful(const Peak* peaks, uint32_t num_peaks,
                       PartialPool& pool, uint32_t frame_counter, float f0 = 0.0f)
    {
        const float prev_coherence = coherence_;
        coherence_ = 1.0f;
        max_hold_frames_ = 24u;
        max_freq_deviation_ = 0.15f * (sample_rate_ / static_cast<float>(fft_size_));
        max_amp_deviation_  = 0.6f;
        track(peaks, num_peaks, pool, frame_counter, f0);
        coherence_ = prev_coherence;
        set_coherence(prev_coherence);
    }

    uint32_t births() const { return births_; }
    uint32_t deaths() const { return deaths_; }

    void set_coherence(float coherence) {
        coherence_ = std::clamp(coherence, 0.0f, 1.0f);
        max_hold_frames_ = 3u + static_cast<uint32_t>(coherence_ * 12.0f);
        max_freq_deviation_ = (0.3f + (1.0f - coherence_) * 0.2f)
                            * (sample_rate_ / static_cast<float>(fft_size_));
        max_amp_deviation_ = 0.3f + (1.0f - coherence_) * 0.4f;
    }

    void set_transient_strength(float strength) {
        transient_strength_ = std::clamp(strength, 0.0f, 1.0f);
    }

private:
    void update_partial(Partial& p, const Peak& peak, bool was_continuous) {
        const float two_pi = 2.0f * std::numbers::pi_v<float>;

        if (transient_strength_ > 0.3f) {
            p.frequency = peak.frequency;
            p.amplitude = peak.magnitude;
            p.phase     = peak.phase;
            p.energy    = peak.magnitude * peak.magnitude;
            p.coherence = 1.0f;
            p.spectral_pos = std::log2(p.frequency / 20.0f);
            p.harmonic_affinity = PeakUtils::compute_harmonic_affinity(
                p.frequency, fundamental_f0_);
            return;
        }

        float expected = p.phase + two_pi * p.frequency * hop_size_ / sample_rate_;
        float delta = peak.phase - expected;
        delta = std::fmod(delta + std::numbers::pi_v<float>,
                          two_pi) - std::numbers::pi_v<float>;

        float inst_freq = p.frequency
                        + delta * sample_rate_ / (two_pi * hop_size_);

        const float chaos = 1.0f - coherence_;
        const float smooth = was_continuous
            ? (0.04f + chaos * 0.5f)
            : (0.1f + chaos * 0.55f);

        p.frequency = p.frequency * (1.0f - smooth) + inst_freq * smooth;
        p.amplitude = p.amplitude * (smooth * 0.15f)
                    + peak.magnitude * (1.0f - smooth * 0.15f);
        p.phase     = peak.phase;
        p.energy    = peak.magnitude * peak.magnitude;
        p.coherence = std::clamp(
            1.0f - std::abs(delta) / std::numbers::pi_v<float>, 0.0f, 1.0f);
        p.spectral_pos = std::log2(p.frequency / 20.0f);
        p.harmonic_affinity = PeakUtils::compute_harmonic_affinity(
            p.frequency, fundamental_f0_);
    }

    float    sample_rate_        = 48000.0f;
    uint32_t fft_size_           = 2048;
    uint32_t hop_size_           = 512;
    float    max_freq_deviation_ = 11.71875f;
    float    max_amp_deviation_  = 0.5f;
    uint32_t max_hold_frames_    = 3;
    uint32_t births_ = 0;
    uint32_t deaths_ = 0;
    float    coherence_ = 0.8f;
    float    transient_strength_ = 0.0f;
    float    fundamental_f0_ = 0.0f;
};
