#pragma once

#include "../../core/types/Partial.h"
#include "../../core/types/Snapshot.h"
#include "../../core/memory/AdditiveBuffer.h"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <numbers>

// SPECS_07 — Hybrid resynthesis (tonal partials + optional residual)

class Resynthesizer {
public:
    void prepare(float sample_rate) {
        sample_rate_ = sample_rate;
        add_buf_.clear();
        std::memset(output_buf_, 0, sizeof(output_buf_));
        std::memset(prev_output_, 0, sizeof(prev_output_));
    }

    void render(const ParticleSnapshot& snap, float sample_rate,
                uint32_t hop_size, float tonal_gain, float spread,
                float coherence, const float* input_hop,
                const float* mag, uint32_t half_n, uint32_t fft_size)
    {
        sample_rate_ = sample_rate;
        hop_size_    = hop_size;
        tonal_gain_  = tonal_gain;
        spread_      = spread * (1.0f - coherence);
        coherence_   = coherence;
        fft_size_    = fft_size;

        add_buf_.from_snapshot(snap, sample_rate_);
        std::memset(output_buf_, 0, hop_size_ * sizeof(float));

        if (add_buf_.num_active > 0)
            render_additive(hop_size_);

        // Spectral residual: fill energy not captured by partials
        if (input_hop != nullptr && mag != nullptr && half_n > 0) {
            const float residual_gain = (1.0f - tonal_gain) * (0.15f + coherence * 0.55f);
            if (residual_gain > 0.01f)
                add_spectral_residual(input_hop, mag, half_n, residual_gain);
        }

        // Frame crossfade — lighter at high coherence (sharper spectrum)
        const float pi = std::numbers::pi_v<float>;
        const float xfade_strength = 0.25f + (1.0f - coherence) * 0.75f;
        for (uint32_t i = 0; i < hop_size_; ++i) {
            float fade_in  = 0.5f * (1.0f - std::cos(pi * static_cast<float>(i) / hop_size_));
            float fade_out = 1.0f - fade_in;
            output_buf_[i] = output_buf_[i] * (fade_in * (1.0f - xfade_strength) + xfade_strength)
                           + prev_output_[i] * (fade_out * (1.0f - xfade_strength));
        }

        std::memcpy(prev_output_, output_buf_, hop_size_ * sizeof(float));
    }

    const float* output_buffer() const { return output_buf_; }

private:
    void render_additive(uint32_t hop_size) {
        const float two_pi = 2.0f * std::numbers::pi_v<float>;
        const float nyquist = sample_rate_ * 0.45f;
        // Hann-windowed FFT peak → sinusoid amplitude correction
        const float amp_scale = tonal_gain_ * 4.0f / static_cast<float>(fft_size_);

        for (uint32_t i = 0; i < add_buf_.num_active; ++i) {
            const float detune = 1.0f + spread_ * static_cast<float>((static_cast<int>(i) % 7) - 3) * 0.015f;
            const float freq = add_buf_.freq[i] * detune;
            const float amp  = add_buf_.amp[i] * amp_scale;
            float& ph        = add_buf_.phase[i];
            const float env  = add_buf_.env[i];

            if (amp < 0.0001f || env < 0.001f || freq > nyquist)
                continue;

            const float phase_inc = two_pi * freq / sample_rate_;

            for (uint32_t s = 0; s < hop_size; ++s) {
                output_buf_[s] += amp * env * std::sin(ph);
                ph += phase_inc;
                if (ph > two_pi) ph -= two_pi;
            }
        }
    }

    // Time-domain residual from input minus synthesis (OLA-friendly)
    void add_spectral_residual(const float* input_hop, const float* /*mag*/,
                               uint32_t /*half_n*/, float gain)
    {
        for (uint32_t i = 0; i < hop_size_; ++i) {
            const float diff = input_hop[i] - output_buf_[i];
            output_buf_[i] += diff * gain;
        }
    }

    float    sample_rate_ = 48000.0f;
    uint32_t hop_size_    = 512;
    uint32_t fft_size_    = 2048;
    float    tonal_gain_  = 1.0f;
    float    spread_      = 0.0f;
    float    coherence_   = 0.8f;

    AdditiveBuffer add_buf_;
    float output_buf_[RING_BUFFER_SIZE]  = {};
    float prev_output_[RING_BUFFER_SIZE] = {};
};
