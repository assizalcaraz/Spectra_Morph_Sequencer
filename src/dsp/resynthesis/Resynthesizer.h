#pragma once

#include "../../core/types/Snapshot.h"
#include "../../core/memory/AdditiveBuffer.h"
#include "../fft/FFTProcessor.h"
#include "ResidualSynth.h"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <numbers>

// SPECS_07 — Hybrid resynthesis: additive partials + spectral residual (iFFT + OLA)

class Resynthesizer {
public:
    void prepare(float sample_rate, uint32_t fft_size, uint32_t hop_size) {
        sample_rate_ = sample_rate;
        fft_size_    = fft_size;
        hop_size_    = hop_size;
        add_buf_.clear();
        residual_.prepare(fft_size, fft_size / 2);
        std::memset(output_buf_, 0, sizeof(output_buf_));
        std::memset(prev_output_, 0, sizeof(prev_output_));
        std::memset(ola_sum_.data(), 0, ola_sum_.size() * sizeof(float));
        std::memset(ola_norm_.data(), 0, ola_norm_.size() * sizeof(float));
        std::memset(grain_buf_.data(), 0, grain_buf_.size() * sizeof(float));
        phase_seed_ = 12345;
    }

    void render(const ParticleSnapshot& snap, FFTProcessor& fft,
                float tonal_gain, float spread, float coherence,
                const float* input_hop, float input_rms)
    {
        const uint32_t half_n = fft.half_n();
        const float* mag   = fft.magnitude();
        const float* phase = fft.phase();

        tonal_gain_  = tonal_gain;
        spread_      = spread * (1.0f - coherence);
        coherence_   = coherence;

        std::memset(grain_buf_.data(), 0, grain_buf_.size() * sizeof(float));

        add_buf_.from_snapshot(snap, sample_rate_, hop_size_);
        render_additive(hop_size_, fft.window_gain());

        const float residual_mix = (1.0f - tonal_gain) * (0.25f + coherence * 0.65f)
                                 + tonal_gain * coherence * 0.1f;

        if (residual_mix > 0.01f && mag != nullptr) {
            residual_.build_tonal_magnitude(snap, sample_rate_, fft_size_);
            residual_.subtract(mag, phase, coherence, phase_seed_);
            fft.inverse_transform(residual_.residual_magnitude(),
                                  residual_.residual_phase(),
                                  grain_buf_.data());
            for (uint32_t i = 0; i < fft_size_; ++i)
                grain_buf_[i] *= residual_mix;
        }

        for (uint32_t i = 0; i < fft_size_; ++i)
            grain_buf_[i] += output_buf_[i];

        ola_synthesize(grain_buf_.data(), fft_size_, hop_size_);

        if (input_rms > 0.001f) {
            float sum_sq = 0.0f;
            for (uint32_t i = 0; i < hop_size_; ++i)
                sum_sq += output_buf_[i] * output_buf_[i];
            const float out_rms = std::sqrt(sum_sq / static_cast<float>(hop_size_));
            if (out_rms > 0.0001f) {
                const float g = std::clamp(input_rms / out_rms, 0.25f, 4.0f);
                for (uint32_t i = 0; i < hop_size_; ++i)
                    output_buf_[i] *= g;
            }
        }

        if (coherence_ < 0.9f) {
            const float xfade = (1.0f - coherence_) * 0.4f;
            const float pi = std::numbers::pi_v<float>;
            for (uint32_t i = 0; i < hop_size_; ++i) {
                const float fade_in = 0.5f * (1.0f - std::cos(
                    pi * static_cast<float>(i) / static_cast<float>(hop_size_)));
                output_buf_[i] = output_buf_[i] * (1.0f - xfade + fade_in * xfade)
                               + prev_output_[i] * (1.0f - fade_in) * xfade;
            }
        }

        std::memcpy(prev_output_, output_buf_, hop_size_ * sizeof(float));
    }

    void render_passthrough(const float* input_hop, uint32_t hop_size) {
        hop_size_ = hop_size;
        for (uint32_t i = 0; i < hop_size; ++i)
            output_buf_[i] = input_hop[i];
    }

    const float* output_buffer() const { return output_buf_; }

private:
    void render_additive(uint32_t hop_size, float window_gain) {
        const float two_pi = 2.0f * std::numbers::pi_v<float>;
        const float nyquist = sample_rate_ * 0.45f;
        const float effective_gain = (tonal_gain_ > 0.95f)
            ? tonal_gain_
            : (0.05f + tonal_gain_ * 0.95f);
        const float amp_scale = effective_gain * window_gain * 2.0f;

        std::memset(output_buf_, 0, hop_size * sizeof(float));

        for (uint32_t i = 0; i < add_buf_.num_active; ++i) {
            const float detune = 1.0f + spread_
                * static_cast<float>((static_cast<int>(i) % 7) - 3) * 0.01f;
            const float freq = add_buf_.freq[i] * detune;
            float amp  = add_buf_.amp[i] * amp_scale;
            float ph   = add_buf_.phase[i];
            const float env = add_buf_.env[i];

            if (amp < 1e-8f || env < 0.001f || freq > nyquist)
                continue;

            const float phase_inc = two_pi * freq / sample_rate_;

            for (uint32_t s = 0; s < hop_size; ++s) {
                output_buf_[s] += amp * env * std::sin(ph);
                ph += phase_inc;
                if (ph > two_pi) ph -= two_pi;
            }
            add_buf_.phase[i] = ph;
        }
    }

    void ola_synthesize(const float* grain, uint32_t grain_len, uint32_t hop) {
        const float pi = std::numbers::pi_v<float>;
        for (uint32_t i = 0; i < grain_len; ++i) {
            const float w = 0.5f * (1.0f - std::cos(
                pi * static_cast<float>(i) / static_cast<float>(grain_len - 1)));
            ola_sum_[i]  += grain[i] * w;
            ola_norm_[i] += w * w;
        }

        for (uint32_t i = 0; i < hop; ++i) {
            const float norm = ola_norm_[i] > 1e-6f ? ola_norm_[i] : 1.0f;
            output_buf_[i] = ola_sum_[i] / norm;
        }

        const uint32_t remain = grain_len - hop;
        std::memmove(ola_sum_.data(), ola_sum_.data() + hop, remain * sizeof(float));
        std::memmove(ola_norm_.data(), ola_norm_.data() + hop, remain * sizeof(float));
        std::memset(ola_sum_.data() + remain, 0, hop * sizeof(float));
        std::memset(ola_norm_.data() + remain, 0, hop * sizeof(float));
    }

    float    sample_rate_ = 48000.0f;
    uint32_t fft_size_    = 2048;
    uint32_t hop_size_    = 512;
    float    tonal_gain_  = 1.0f;
    float    spread_      = 0.0f;
    float    coherence_   = 0.8f;
    uint64_t phase_seed_  = 12345;

    AdditiveBuffer add_buf_;
    ResidualSynth  residual_;

    std::array<float, RING_BUFFER_SIZE> grain_buf_{};
    std::array<float, RING_BUFFER_SIZE> ola_sum_{};
    std::array<float, RING_BUFFER_SIZE> ola_norm_{};
    float output_buf_[RING_BUFFER_SIZE]  = {};
    float prev_output_[RING_BUFFER_SIZE] = {};
};
