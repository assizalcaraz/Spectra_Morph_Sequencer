#pragma once

#include "../../core/types/Partial.h"
#include "../../core/types/Snapshot.h"
#include "../../core/types/Types.h"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <numbers>

// SPECS_07 — Tonal magnitude subtraction + residual spectrum

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
            const uint32_t bin = static_cast<uint32_t>(bin_pos);
            if (bin >= half_n_) continue;

            tonal_mag_[bin] += p.amplitude;
            if (bin > 0)
                tonal_mag_[bin - 1] += p.amplitude * 0.25f;
            if (bin + 1 < half_n_)
                tonal_mag_[bin + 1] += p.amplitude * 0.25f;
        }
    }

    void subtract(const float* input_mag, const float* input_phase,
                  float coherence, uint64_t& phase_seed)
    {
        for (uint32_t k = 0; k < half_n_; ++k) {
            residual_mag_[k] = input_mag[k] > tonal_mag_[k]
                ? input_mag[k] - tonal_mag_[k]
                : 0.0f;

            if (coherence > 0.75f)
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
