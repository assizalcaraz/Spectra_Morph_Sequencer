#pragma once

#include "../../core/types/Snapshot.h"
#include "../../core/memory/AdditiveBuffer.h"
#include "../fft/FFTProcessor.h"
#include "../phase/PhaseManager.h"
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
                float transient_strength, TransientMode transient_mode,
                float f0, const float* input_hop, float input_rms)
    {
        const float* mag       = fft.magnitude();
        const float* phase     = fft.phase();
        const float* raw_phase = fft.raw_phase();

        tonal_gain_  = std::clamp(tonal_gain, 0.0f, 1.0f);
        spread_      = spread * (0.2f + (1.0f - coherence) * 0.8f);
        coherence_   = coherence;
        transient_strength_ = std::clamp(transient_strength, 0.0f, 1.0f);
        transient_mode_ = transient_mode;
        f0_ = f0;

        std::memset(grain_buf_.data(), 0, grain_buf_.size() * sizeof(float));

        add_buf_.from_snapshot(snap, sample_rate_, hop_size_);
        render_additive(hop_size_, fft.window_gain());

        float residual_knob = 1.0f - tonal_gain_;
        float residual_mix = residual_knob
            * (0.15f + (1.0f - coherence_) * 0.55f)
            + tonal_gain_ * coherence_ * 0.05f;

        if (transient_strength_ > 0.3f)
            residual_mix *= (1.0f - transient_strength_ * 0.85f);

        if (residual_mix > 0.01f && mag != nullptr) {
            residual_.build_tonal_magnitude(snap, sample_rate_, fft_size_);
            residual_.subtract(mag, phase, raw_phase, coherence_,
                               transient_strength_, phase_seed_);
            fft.inverse_transform(residual_.residual_magnitude(),
                                  residual_.residual_phase(),
                                  grain_buf_.data());
            for (uint32_t i = 0; i < fft_size_; ++i)
                grain_buf_[i] *= residual_mix;
        }

        for (uint32_t i = 0; i < hop_size_; ++i)
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

        const bool skip_xfade = (coherence_ >= 0.9f) || (transient_strength_ > 0.3f);
        if (!skip_xfade && coherence_ < 0.9f) {
            const float xfade = (1.0f - coherence_) * 0.4f;
            const float pi = std::numbers::pi_v<float>;
            for (uint32_t i = 0; i < hop_size_; ++i) {
                const float fade_in = 0.5f * (1.0f - std::cos(
                    pi * static_cast<float>(i) / static_cast<float>(hop_size_)));
                output_buf_[i] = output_buf_[i] * (1.0f - xfade + fade_in * xfade)
                               + prev_output_[i] * (1.0f - fade_in) * xfade;
            }
        }

        if (transient_strength_ > 0.3f && input_hop != nullptr
            && transient_mode_ == TransientMode::Protect) {
            const float w = transient_strength_;
            for (uint32_t i = 0; i < hop_size_; ++i)
                output_buf_[i] = output_buf_[i] * (1.0f - w) + input_hop[i] * w;
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
        const PhaseMode mode = PhaseManager::mode_from_coherence(coherence_);
        const float chaos = 1.0f - coherence_;

        std::memset(output_buf_, 0, sizeof(output_buf_));

        float phi_f0 = add_buf_.num_active > 0 ? add_buf_.phase[0] : 0.0f;
        if (f0_ >= 40.0f) {
            for (uint32_t i = 0; i < add_buf_.num_active; ++i) {
                if (std::abs(add_buf_.freq[i] - f0_) < f0_ * 0.06f) {
                    phi_f0 = add_buf_.phase[i];
                    break;
                }
            }
        }

        for (uint32_t i = 0; i < add_buf_.num_active; ++i) {
            const float detune = 1.0f + spread_
                * static_cast<float>((static_cast<int>(i) % 7) - 3) * 0.01f;
            float freq = add_buf_.freq[i] * detune;
            float amp  = add_buf_.amp[i] * amp_scale;
            float ph   = add_buf_.phase[i];
            const float env = add_buf_.env[i];

            if (mode == PhaseMode::Lock && coherence_ >= 0.85f && f0_ >= 40.0f) {
                const int h = PhaseManager::harmonic_number(freq, f0_);
                if (h >= 1) {
                    freq = f0_ * static_cast<float>(h);
                    ph = phi_f0 * static_cast<float>(h)
                       + std::fmod(add_buf_.phase[i] - phi_f0 * static_cast<float>(h)
                                   + two_pi, two_pi);
                }
            } else if (mode == PhaseMode::Diffuse || mode == PhaseMode::Scatter) {
                phase_seed_ = phase_seed_ * 1103515245ull + 12345ull;
                const float u = static_cast<float>(phase_seed_ & 0xFFFF) / 65535.0f;
                ph += (u * 2.0f - 1.0f) * chaos * 0.3f * std::numbers::pi_v<float>;
            }

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
    float    transient_strength_ = 0.0f;
    float    f0_          = 0.0f;
    TransientMode transient_mode_ = TransientMode::Protect;
    uint64_t phase_seed_  = 12345;

    AdditiveBuffer add_buf_;
    ResidualSynth  residual_;

    std::array<float, RING_BUFFER_SIZE> grain_buf_{};
    std::array<float, RING_BUFFER_SIZE> ola_sum_{};
    std::array<float, RING_BUFFER_SIZE> ola_norm_{};
    float output_buf_[RING_BUFFER_SIZE]  = {};
    float prev_output_[RING_BUFFER_SIZE] = {};
};
