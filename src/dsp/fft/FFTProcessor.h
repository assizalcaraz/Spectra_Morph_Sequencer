#pragma once

#include "../../core/types/Types.h"
#include "../../core/types/Peak.h"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <array>
#include <memory>
#include <numbers>

// SPECS_03 — FFT Pipeline
// Handles windowing, FFT, magnitude/phase extraction.

class FFTProcessor {
public:
    FFTProcessor() = default;
    ~FFTProcessor() = default;

    void prepare(uint32_t fft_size, uint32_t hop_size, float sample_rate) {
        fft_size_    = fft_size;
        hop_size_    = hop_size;
        sample_rate_ = sample_rate;
        half_n_      = fft_size / 2;

        // Recreate FFT with correct size
        uint32_t order = static_cast<uint32_t>(std::log2(fft_size));
        fft_ = std::make_unique<juce::dsp::FFT>(static_cast<int>(order));

        // Pre-calculate Hann window
        window_.resize(fft_size_);
        for (uint32_t i = 0; i < fft_size_; ++i) {
            window_[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float>
                           * static_cast<float>(i) / fft_size_));
        }

        // Reset state
        std::memset(frame_buf_.data(), 0, fft_size_ * sizeof(float));
        std::memset(prev_phase_.data(), 0, half_n_ * sizeof(float));
        noise_floor_ = 0.0f;
    }

    // Process one frame. Reads hop_size new samples from input.
    void process(const float* input) {
        // Shift frame buffer
        uint32_t shift = fft_size_ - hop_size_;
        std::memmove(frame_buf_.data(), frame_buf_.data() + hop_size_,
                     shift * sizeof(float));

        // Copy new samples
        std::memcpy(frame_buf_.data() + shift, input,
                    hop_size_ * sizeof(float));

        // Apply window
        for (uint32_t i = 0; i < fft_size_; ++i) {
            windowed_buf_[i] = frame_buf_[i] * window_[i];
        }

        // FFT
        using Complex = juce::dsp::Complex<float>;
        auto* in  = reinterpret_cast<Complex*>(windowed_buf_.data());
        auto* out = reinterpret_cast<Complex*>(complex_buf_.data());
        fft_->perform(in, out, false);

        // Magnitude and phase
        for (uint32_t i = 0; i < half_n_; ++i) {
            float re = complex_buf_[i * 2];
            float im = complex_buf_[i * 2 + 1];
            mag_[i]   = std::sqrt(re * re + im * im);
            phase_[i] = std::atan2(im, re);
        }

        // Update adaptive noise floor
        update_noise_floor();
    }

    // ── Accessors ──────────────────────────────────────────────────
    const float* magnitude() const { return mag_.data(); }
    const float* phase()     const { return phase_.data(); }

    uint32_t fft_size()   const { return fft_size_; }
    uint32_t hop_size()   const { return hop_size_; }
    uint32_t half_n()     const { return half_n_; }
    float    sample_rate() const { return sample_rate_; }

    float bin_freq(uint32_t bin) const {
        return static_cast<float>(bin) * sample_rate_ / fft_size_;
    }

    float noise_floor() const { return noise_floor_; }

private:
    void update_noise_floor() {
        float sum = 0.0f;
        for (uint32_t i = 0; i < half_n_; ++i)
            sum += mag_[i];
        float avg = sum / static_cast<float>(half_n_);
        noise_floor_ = noise_floor_ * 0.9f + avg * 0.1f;
    }

    uint32_t fft_size_    = 2048;
    uint32_t hop_size_    = 512;
    uint32_t half_n_      = 1024;
    float    sample_rate_ = 48000.0f;
    float    noise_floor_ = 0.0f;

    std::vector<float>    window_;
    std::unique_ptr<juce::dsp::FFT> fft_;

    // Pre-allocated buffers (max size = FFT_MAX_SIZE)
    std::array<float, FFT_MAX_SIZE>         frame_buf_{};
    std::array<float, FFT_MAX_SIZE>         windowed_buf_{};
    std::array<float, FFT_MAX_SIZE * 2>     complex_buf_{};
    std::array<float, FFT_MAX_SIZE / 2>     mag_{};
    std::array<float, FFT_MAX_SIZE / 2>     phase_{};
    std::array<float, FFT_MAX_SIZE / 2>     prev_phase_{};
};
