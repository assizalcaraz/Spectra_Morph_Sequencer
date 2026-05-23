#pragma once

#include "../../core/types/Partial.h"
#include "../../core/types/Peak.h"
#include "../../core/types/Types.h"
#include "../../core/memory/PartialPool.h"
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <numbers>

// SPECS_03 — Partial Tracking
// Nearest-peak matching. Manages N2→N3 lifecycle.

class PartialTracker {
public:
    void prepare(float sample_rate, uint32_t fft_size, uint32_t hop_size) {
        sample_rate_        = sample_rate;
        fft_size_           = fft_size;
        hop_size_           = hop_size;
        max_freq_deviation_ = 0.5f * (sample_rate / fft_size);
        max_hold_frames_    = 3;
    }

    void track(const Peak* peaks, uint32_t num_peaks,
               PartialPool& pool, uint32_t frame_counter)
    {
        births_ = 0;
        deaths_ = 0;

        // Phase 1: match peaks → existing partials
        bool peak_matched[MAX_PEAKS] = {};

        for (uint32_t p_idx = 0; p_idx < num_peaks; ++p_idx) {
            int best_partial = -1;
            float best_dist  = max_freq_deviation_;

            for (uint32_t i = 0; i < MAX_PARTIALS; ++i) {
                if (!pool.is_alive(i)) continue;
                auto& part = pool[i];
                float dist = std::abs(peaks[p_idx].frequency - part.frequency);
                if (dist < best_dist) {
                    best_dist    = dist;
                    best_partial = static_cast<int>(i);
                }
            }

            if (best_partial >= 0) {
                peak_matched[p_idx] = true;
                auto idx = static_cast<uint32_t>(best_partial);
                update_partial(pool[idx], peaks[p_idx], pool[idx].hold_counter == 0);
                pool[idx].hold_counter = 0;
            }
        }

        // Phase 2: birth new partials from unmatched peaks
        for (uint32_t p_idx = 0; p_idx < num_peaks; ++p_idx) {
            if (peak_matched[p_idx]) continue;
            if (pool.full()) break;

            uint32_t id = pool.allocate();
            if (id >= MAX_PARTIALS) break;

            auto& p = pool[id];
            float freq = peaks[p_idx].frequency;
            float mag  = peaks[p_idx].magnitude;

            p.id                = (frame_counter << 16) | id;
            p.birth_frame       = frame_counter;
            p.frequency         = freq;
            p.amplitude         = mag;
            p.phase             = peaks[p_idx].phase;
            p.energy            = mag * mag;
            p.age               = 0.0f;
            p.lifetime_remaining = 2.0f * sample_rate_ / hop_size_;
            p.stability         = 1.0f;
            p.coherence         = 1.0f;
            p.harmonic_affinity = 0.5f;
            p.mass              = 1.0f;
            p.drift             = 0.0f;
            p.temperature       = 0.0f;
            p.spectral_pos      = std::log2(freq / 20.0f);
            p.velocity          = 0.0f;
            p.spatial_x         = 0.0f;
            p.spatial_y         = 0.0f;
            p.state             = ParticleState::Alive;
            p.niche             = Niche::None;
            p.hold_counter      = 0;
            ++births_;
        }

        // Phase 3: death — expire unmatched/old partials
        for (uint32_t i = 0; i < MAX_PARTIALS; ++i) {
            if (!pool.is_alive(i)) continue;
            auto& p = pool[i];
            if (p.state == ParticleState::Dead) continue;

            // Age
            p.age += 1.0f;

            // Energy decay
            p.energy *= 0.999f;
            if (p.energy < 0.0f) p.energy = 0.0f;

            // Stability: if not matched this frame, increment hold
            bool matched_this_frame = false;
            for (uint32_t p_idx = 0; p_idx < num_peaks; ++p_idx) {
                if (std::abs(peaks[p_idx].frequency - p.frequency)
                    < max_freq_deviation_) {
                    matched_this_frame = true;
                    break;
                }
            }

            if (!matched_this_frame) {
                ++p.hold_counter;
                p.stability *= 0.95f;
            }

            // Death conditions
            if (p.hold_counter > max_hold_frames_
                || p.energy < 0.0001f
                || p.age > p.lifetime_remaining)
            {
                p.state = ParticleState::Dying;
                p.energy *= 0.5f;
                if (p.energy < 0.0001f || ++p.hold_counter > max_hold_frames_ + 2) {
                    pool.free(i);
                    ++deaths_;
                }
            }
        }
    }

    uint32_t births() const { return births_; }
    uint32_t deaths() const { return deaths_; }

    // coherence 0 = chaos, 1 = maximum fidelity / stable tracking
    void set_coherence(float coherence) {
        coherence_ = std::clamp(coherence, 0.0f, 1.0f);
        max_hold_frames_ = 3u + static_cast<uint32_t>(coherence_ * 10.0f);
        max_freq_deviation_ = (0.35f + (1.0f - coherence_) * 0.15f)
                            * (sample_rate_ / static_cast<float>(fft_size_));
    }

private:
    void update_partial(Partial& p, const Peak& peak, bool was_continuous) {
        const float two_pi = 2.0f * std::numbers::pi_v<float>;

        // Phase vocoder
        float expected = p.phase + two_pi * p.frequency * hop_size_ / sample_rate_;
        float delta = peak.phase - expected;
        delta = std::fmod(delta + std::numbers::pi_v<float>,
                          two_pi) - std::numbers::pi_v<float>;

        float inst_freq = p.frequency
                        + delta * sample_rate_ / (two_pi * hop_size_);

        // High coherence → follow peaks closely; chaos → sluggish / smeared
        const float chaos = 1.0f - coherence_;
        const float smooth = was_continuous
            ? (0.05f + chaos * 0.65f)
            : (0.12f + chaos * 0.68f);
        p.frequency    = p.frequency * (1.0f - smooth) + inst_freq * smooth;
        p.amplitude    = p.amplitude * (smooth * 0.25f) + peak.magnitude * (1.0f - smooth * 0.25f);
        p.phase        = peak.phase;
        p.energy       = peak.magnitude * peak.magnitude;
        p.coherence    = 1.0f - std::abs(delta) / std::numbers::pi_v<float>;
        p.spectral_pos = std::log2(p.frequency / 20.0f);
    }

    float    sample_rate_        = 48000.0f;
    uint32_t fft_size_           = 2048;
    uint32_t hop_size_           = 512;
    float    max_freq_deviation_ = 11.71875f;
    uint32_t max_hold_frames_    = 3;

    uint32_t births_ = 0;
    uint32_t deaths_ = 0;
    float    coherence_ = 0.8f;
};
