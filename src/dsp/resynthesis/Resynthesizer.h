#pragma once

#include "../../core/types/Partial.h"
#include "../../core/types/Snapshot.h"
#include "../../core/memory/AdditiveBuffer.h"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <numbers>

// SPECS_07 — Hybrid resynthesis
// Phase I: additive oscillator bank (tonal)

class Resynthesizer {
public:
    void prepare(float sample_rate) {
        sample_rate_ = sample_rate;
        add_buf_.clear();
        std::memset(output_buf_, 0, sizeof(output_buf_));
        std::memset(prev_output_, 0, sizeof(prev_output_));
    }

    void render(const ParticleSnapshot& snap, float sample_rate,
                uint32_t hop_size, float tonal_gain = 1.0f,
                float spread = 0.0f)
    {
        sample_rate_ = sample_rate;
        hop_size_    = hop_size;
        tonal_gain_  = tonal_gain;
        spread_      = spread;

        add_buf_.from_snapshot(snap, sample_rate_);
        std::memset(output_buf_, 0, hop_size_ * sizeof(float));

        if (add_buf_.num_active > 0)
            render_additive(hop_size_);

        // Residual noise layer — fills the gap when tonal_gain is low
        float noise_gain = (1.0f - tonal_gain_) * 0.06f;
        if (noise_gain > 0.001f) {
            for (uint32_t i = 0; i < hop_size_; ++i) {
                noise_seed_ = noise_seed_ * 1664525u + 1013904223u;
                float n = (static_cast<float>(noise_seed_ & 0x7FFF)
                         / 16384.0f - 1.0f);
                output_buf_[i] += n * noise_gain;
            }
        }

        // Crossfade with previous frame (Hann-like)
        const float pi = std::numbers::pi_v<float>;
        for (uint32_t i = 0; i < hop_size_; ++i) {
            float fade_in  = 0.5f * (1.0f - std::cos(pi * i / hop_size_));
            float fade_out = 1.0f - fade_in;
            output_buf_[i] = output_buf_[i] * fade_in
                           + prev_output_[i] * fade_out;
        }

        std::memcpy(prev_output_, output_buf_, hop_size_ * sizeof(float));
    }

    const float* output_buffer() const { return output_buf_; }

private:
    void render_additive(uint32_t hop_size) {
        const float two_pi = 2.0f * std::numbers::pi_v<float>;
        float nyquist = sample_rate_ * 0.45f;

        for (uint32_t i = 0; i < add_buf_.num_active; ++i) {
            float detune = 1.0f + spread_ * static_cast<float>((static_cast<int>(i) % 7) - 3) * 0.05f;
            float freq = add_buf_.freq[i] * detune;
            float amp  = add_buf_.amp[i];
            float& ph  = add_buf_.phase[i];
            float env  = add_buf_.env[i];

            if (amp < 0.0001f || env < 0.001f || freq > nyquist)
                continue;

            float phase_inc = two_pi * freq / sample_rate_;

            for (uint32_t s = 0; s < hop_size; ++s) {
                output_buf_[s] += amp * env * std::sin(ph);
                ph += phase_inc;
                if (ph > two_pi) ph -= two_pi;
            }
        }

        if (add_buf_.num_active > 0) {
            float scale = tonal_gain_ / static_cast<float>(add_buf_.num_active);
            for (uint32_t i = 0; i < hop_size; ++i)
                output_buf_[i] *= scale;
        }
    }

    float    sample_rate_ = 48000.0f;
    uint32_t hop_size_    = 512;
    float    tonal_gain_  = 1.0f;
    float    spread_      = 0.0f;

    AdditiveBuffer add_buf_;
    uint32_t noise_seed_ = 12345;
    float output_buf_[RING_BUFFER_SIZE]  = {};
    float prev_output_[RING_BUFFER_SIZE] = {};
};
