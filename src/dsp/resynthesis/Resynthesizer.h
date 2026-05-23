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
                const float* mag, uint32_t half_n, uint32_t fft_size,
                float input_rms = 0.0f)
    {
        sample_rate_ = sample_rate;
        hop_size_    = hop_size;
        tonal_gain_  = tonal_gain;
        spread_      = spread * (1.0f - coherence);
        coherence_   = coherence;
        fft_size_    = fft_size;

        add_buf_.from_snapshot(snap, sample_rate_);
        std::memset(output_buf_, 0, hop_size_ * sizeof(float));

        // Always render additive (at a minimum floor to preserve tonal framework)
        render_additive(hop_size_);

        // Noise residual: shaped by input envelope, never passes original signal
        if (input_hop != nullptr && mag != nullptr && half_n > 0) {
            const float noise_gain = (1.0f - tonal_gain) * (0.3f + coherence * 0.5f);
            if (noise_gain > 0.01f)
                add_residual_noise(input_hop, noise_gain);
        }

        // Frame crossfade — reduced at high coherence to avoid smearing
        const float xfade_strength = 0.04f + (1.0f - coherence_) * 0.55f;
        if (xfade_strength > 0.001f) {
            const float pi = std::numbers::pi_v<float>;
            for (uint32_t i = 0; i < hop_size_; ++i) {
                const float fade_in = 0.5f * (1.0f - std::cos(
                    pi * static_cast<float>(i) / static_cast<float>(hop_size_)));
                const float fade_out = 1.0f - fade_in;
                output_buf_[i] = output_buf_[i]
                    * (fade_in * (1.0f - xfade_strength) + xfade_strength)
                    + prev_output_[i] * (fade_out * xfade_strength);
            }
        }

        // Amplitude envelope matching: scale output RMS to match input RMS
        if (input_rms > 0.001f) {
            float sum_sq = 0.0f;
            for (uint32_t i = 0; i < hop_size_; ++i)
                sum_sq += output_buf_[i] * output_buf_[i];
            const float output_rms = std::sqrt(sum_sq / static_cast<float>(hop_size_));
            if (output_rms > 0.0001f) {
                const float gain = std::clamp(
                    input_rms / output_rms, 0.1f, 10.0f);
                for (uint32_t i = 0; i < hop_size_; ++i)
                    output_buf_[i] *= gain;
            }
        }

        std::memcpy(prev_output_, output_buf_, hop_size_ * sizeof(float));
    }

    const float* output_buffer() const { return output_buf_; }

private:
    void render_additive(uint32_t hop_size) {
        const float two_pi = 2.0f * std::numbers::pi_v<float>;
        const float nyquist = sample_rate_ * 0.45f;
        // Hann-windowed FFT peak → sinusoid amplitude correction
        // Floor: always keep at least 15% additive to preserve tonal framework
        const float effective_gain = 0.15f + tonal_gain_ * 0.85f;
        const float amp_scale = effective_gain * 4.0f / static_cast<float>(fft_size_);

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

    // Noise residual: shaped by input envelope, never passes original signal
    void add_residual_noise(const float* input_hop, float gain) {
        if (gain < 0.001f) return;
        for (uint32_t i = 0; i < hop_size_; ++i) {
            noise_seed_ = noise_seed_ * 1103515245 + 12345;
            float noise = (static_cast<float>(noise_seed_ & 0x7fffffff)
                          * (1.0f / 536870912.0f)) - 1.0f;
            float env = std::abs(input_hop[i]) * gain;
            output_buf_[i] += noise * env * 0.5f;
        }
    }

    float    sample_rate_ = 48000.0f;
    uint32_t hop_size_    = 512;
    uint32_t fft_size_    = 2048;
    float    tonal_gain_  = 1.0f;
    float    spread_      = 0.0f;
    float    coherence_   = 0.8f;
    uint64_t noise_seed_  = 12345;

    AdditiveBuffer add_buf_;
    float output_buf_[RING_BUFFER_SIZE]  = {};
    float prev_output_[RING_BUFFER_SIZE] = {};
};
