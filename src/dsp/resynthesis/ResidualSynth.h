#pragma once

#include "../../core/types/Partial.h"
#include "../../core/types/Snapshot.h"
#include "../../core/types/Types.h"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <numbers>

// SPECS_07 — Tonal magnitude subtraction + residual spectrum

constexpr float kTonalSubtractAlpha = 0.78f;

class ResidualSynth {
public:
    void prepare(uint32_t fft_size, uint32_t half_n) {
        fft_size_ = fft_size;
        half_n_   = half_n + 1;
        std::memset(tonal_mag_.data(), 0, tonal_mag_.size() * sizeof(float));
        std::memset(residual_mag_.data(), 0, residual_mag_.size() * sizeof(float));
        std::memset(residual_phase_.data(), 0, residual_phase_.size() * sizeof(float));
    }

    void build_tonal_magnitude(const ParticleSnapshot& snap,
                               float sample_rate, uint32_t fft_size)
    {
        std::memset(tonal_mag_.data(), 0, tonal_mag_.size() * sizeof(float));
        const float bin_hz = sample_rate / static_cast<float>(fft_size);

        for (uint32_t i = 0; i < snap.num_partials; ++i) {
            const auto& p = snap.partials[i];
            if (p.amplitude < 1e-8f) continue;

            const float bin_pos = p.frequency / bin_hz;
            const int center = static_cast<int>(bin_pos);
            constexpr int radius = 4;
            for (int d = -radius; d <= radius; ++d) {
                const int b = center + d;
                if (b < 0 || static_cast<uint32_t>(b) >= half_n_) continue;
                const float w = std::exp(-0.5f * static_cast<float>(d * d));
                tonal_mag_[static_cast<uint32_t>(b)] += p.amplitude * w;
            }
        }
    }

    void subtract(const float* input_mag, const float* input_phase,
                  const float* raw_phase, float coherence,
                  float transient_strength, uint64_t& phase_seed)
    {
        const bool use_raw = transient_strength > 0.3f;

        for (uint32_t k = 0; k < half_n_; ++k) {
            const float tonal_est = tonal_mag_[k] * kTonalSubtractAlpha;
            residual_mag_[k] = input_mag[k] > tonal_est
                ? input_mag[k] - tonal_est
                : 0.0f;

            if (use_raw && raw_phase != nullptr)
                residual_phase_[k] = raw_phase[k];
            else if (coherence > 0.75f)
                residual_phase_[k] = input_phase[k];
            else {
                phase_seed = phase_seed * 1103515245ull + 12345ull;
                const float u = static_cast<float>(phase_seed & 0xFFFF)
                              / 65535.0f;
                residual_phase_[k] = u * 2.0f * std::numbers::pi_v<float>;
            }
        }
    }

    const float* tonal_magnitude() const { return tonal_mag_.data(); }
    const float* residual_magnitude() const { return residual_mag_.data(); }
    const float* residual_phase() const { return residual_phase_.data(); }

private:
    uint32_t fft_size_ = 2048;
    uint32_t half_n_   = 1025;

    std::array<float, FFT_MAX_SIZE / 2 + 1> tonal_mag_{};
    std::array<float, FFT_MAX_SIZE / 2 + 1> residual_mag_{};
    std::array<float, FFT_MAX_SIZE / 2 + 1> residual_phase_{};
};
