#pragma once

#include "../../core/types/Types.h"
#include <juce_dsp/juce_dsp.h>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <array>
#include <memory>
#include <numbers>

// SPECS_03 — STFT analysis (real FFT, per-bin phase vocoder, noise floor)

class FFTProcessor {
public:
    FFTProcessor() = default;

    void prepare(uint32_t fft_size, uint32_t hop_size, float sample_rate) {
        fft_size_    = fft_size;
        hop_size_    = hop_size;
        sample_rate_ = sample_rate;
        half_n_      = fft_size / 2;

        const uint32_t order = static_cast<uint32_t>(std::log2(fft_size));
        fft_ = std::make_unique<juce::dsp::FFT>(static_cast<int>(order));

        window_sum_ = 0.0f;
        for (uint32_t i = 0; i < fft_size_; ++i) {
            window_[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float>
                           * static_cast<float>(i) / static_cast<float>(fft_size_)));
            window_sum_ += window_[i];
        }
        window_gain_ = (window_sum_ > 1e-6f)
            ? (2.0f / window_sum_)
            : 1.0f;

        std::memset(frame_buf_.data(), 0, fft_size_ * sizeof(float));
        std::memset(fft_buf_.data(), 0, fft_buf_.size() * sizeof(float));
        std::memset(mag_.data(), 0, mag_.size() * sizeof(float));
        std::memset(phase_.data(), 0, phase_.size() * sizeof(float));
        std::memset(prev_phase_.data(), 0, prev_phase_.size() * sizeof(float));
        std::memset(noise_floor_bins_.data(), 0, noise_floor_bins_.size() * sizeof(float));
        noise_floor_ = 0.0f;
    }

    void process(const float* input) {
        const uint32_t shift = fft_size_ - hop_size_;
        std::memmove(frame_buf_.data(), frame_buf_.data() + hop_size_,
                     shift * sizeof(float));
        std::memcpy(frame_buf_.data() + shift, input, hop_size_ * sizeof(float));

        std::memset(fft_buf_.data(), 0, fft_buf_.size() * sizeof(float));
        for (uint32_t i = 0; i < fft_size_; ++i)
            fft_buf_[i] = frame_buf_[i] * window_[i];

        fft_->performRealOnlyForwardTransform(fft_buf_.data(), false);

        const float two_pi = 2.0f * std::numbers::pi_v<float>;
        const float phase_scale = two_pi * static_cast<float>(hop_size_)
                                / static_cast<float>(fft_size_);

        mag_[0] = std::abs(fft_buf_[0]);
        phase_[0] = (fft_buf_[0] >= 0.0f) ? 0.0f : std::numbers::pi_v<float>;

        if (half_n_ > 0) {
            mag_[half_n_] = std::abs(fft_buf_[1]);
            phase_[half_n_] = 0.0f;
        }

        for (uint32_t k = 1; k < half_n_; ++k) {
            const float re = fft_buf_[k * 2];
            const float im = fft_buf_[k * 2 + 1];
            mag_[k] = std::sqrt(re * re + im * im);
            float ph = std::atan2(im, re);

            const float expected = prev_phase_[k] + phase_scale * static_cast<float>(k);
            float delta = ph - expected;
            delta = std::fmod(delta + std::numbers::pi_v<float>, two_pi)
                  - std::numbers::pi_v<float>;
            ph = expected + delta;
            phase_[k] = ph;
            prev_phase_[k] = ph;
        }

        update_noise_floor();
    }

    void inverse_transform(const float* mag_in, const float* phase_in,
                           float* output_time)
    {
        std::memset(fft_buf_.data(), 0, fft_buf_.size() * sizeof(float));

        fft_buf_[0] = mag_in[0];
        if (half_n_ > 0)
            fft_buf_[1] = mag_in[half_n_];

        for (uint32_t k = 1; k < half_n_; ++k) {
            const float ph = phase_in[k];
            fft_buf_[k * 2]     = mag_in[k] * std::cos(ph);
            fft_buf_[k * 2 + 1] = mag_in[k] * std::sin(ph);
        }

        fft_->performRealOnlyInverseTransform(fft_buf_.data());

        for (uint32_t i = 0; i < fft_size_; ++i)
            output_time[i] = fft_buf_[i] * window_[i] * window_gain_;
    }

    const float* magnitude() const { return mag_.data(); }
    const float* phase()     const { return phase_.data(); }
    const float* noise_floor_bins() const { return noise_floor_bins_.data(); }

    uint32_t fft_size()    const { return fft_size_; }
    uint32_t hop_size()    const { return hop_size_; }
    uint32_t half_n()      const { return half_n_; }
    float    sample_rate() const { return sample_rate_; }
    float    window_gain() const { return window_gain_; }

    float bin_freq(uint32_t bin) const {
        return static_cast<float>(bin) * sample_rate_ / static_cast<float>(fft_size_);
    }

    float noise_floor() const { return noise_floor_; }

private:
    void update_noise_floor() {
        float sum = 0.0f;
        for (uint32_t k = 0; k < half_n_; ++k) {
            noise_floor_bins_[k] = noise_floor_bins_[k] * 0.9f + mag_[k] * 0.1f;
            sum += noise_floor_bins_[k];
        }
        noise_floor_ = (half_n_ > 0) ? sum / static_cast<float>(half_n_) : 0.0f;
    }

    uint32_t fft_size_    = 2048;
    uint32_t hop_size_    = 512;
    uint32_t half_n_      = 1024;
    float    sample_rate_ = 48000.0f;
    float    noise_floor_ = 0.0f;
    float    window_sum_  = 0.0f;
    float    window_gain_ = 1.0f;

    std::unique_ptr<juce::dsp::FFT> fft_;

    std::array<float, FFT_MAX_SIZE>         frame_buf_{};
    std::array<float, FFT_MAX_SIZE>         window_{};
    std::array<float, FFT_MAX_SIZE * 2>     fft_buf_{};
    std::array<float, FFT_MAX_SIZE / 2 + 1> mag_{};
    std::array<float, FFT_MAX_SIZE / 2 + 1> phase_{};
    std::array<float, FFT_MAX_SIZE / 2 + 1> prev_phase_{};
    std::array<float, FFT_MAX_SIZE / 2 + 1> noise_floor_bins_{};
};
