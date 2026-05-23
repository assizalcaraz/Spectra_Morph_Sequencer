#pragma once

#include "../../core/types/Peak.h"
#include "../../core/types/Types.h"
#include <cmath>
#include <algorithm>

namespace PeakUtils {

// Parabolic interpolation of peak frequency (sub-bin accuracy)
inline float refine_frequency(uint32_t bin, const float* mag,
                              float sample_rate, uint32_t fft_size)
{
    const float bin_hz = sample_rate / static_cast<float>(fft_size);
    if (bin < 1 || mag == nullptr)
        return static_cast<float>(bin) * bin_hz;

    const float a = mag[bin - 1];
    const float b = mag[bin];
    const float c = mag[bin + 1];
    const float denom = a - 2.0f * b + c;
    if (std::abs(denom) < 1e-12f)
        return static_cast<float>(bin) * bin_hz;

    const float p = std::clamp(0.5f * (a - c) / denom, -0.5f, 0.5f);
    return (static_cast<float>(bin) + p) * bin_hz;
}

inline bool peak_near(const Peak* peaks, uint32_t n, float freq, float tolerance_hz)
{
    for (uint32_t i = 0; i < n; ++i) {
        if (std::abs(peaks[i].frequency - freq) < tolerance_hz)
            return true;
    }
    return false;
}

// Fill missing harmonics of f0 for tonal sources (saw, sine, etc.)
inline void enrich_harmonics(Peak* peaks, uint32_t& num_peaks,
                             float f0, const float* mag, const float* phase,
                             float sample_rate, uint32_t fft_size,
                             uint32_t half_n, float threshold,
                             float coherence, uint32_t max_peaks)
{
    if (f0 < 40.0f || coherence < 0.55f || num_peaks >= max_peaks)
        return;

    const float bin_hz = sample_rate / static_cast<float>(fft_size);
    const float tolerance = bin_hz * 2.0f;
    const float nyquist = sample_rate * 0.45f;
    const int max_harmonic = static_cast<int>(nyquist / f0);

    for (int h = 1; h <= max_harmonic && num_peaks < max_peaks; ++h) {
        const float hf = f0 * static_cast<float>(h);
        if (peak_near(peaks, num_peaks, hf, tolerance))
            continue;

        const uint32_t bin = static_cast<uint32_t>(std::round(
            hf * static_cast<float>(fft_size) / sample_rate));
        if (bin < 1 || bin >= half_n - 1)
            continue;

        const float local_thresh = threshold * (0.15f + 0.85f / static_cast<float>(h));
        if (mag[bin] < local_thresh)
            continue;

        auto& p = peaks[num_peaks++];
        p.bin_index = static_cast<uint16_t>(bin);
        p.frequency = refine_frequency(bin, mag, sample_rate, fft_size);
        p.magnitude = mag[bin];
        p.phase     = phase[bin];
    }
}

inline float estimate_fundamental(const Peak* peaks, uint32_t num_peaks)
{
    if (num_peaks == 0)
        return 0.0f;

    float best_f0 = peaks[0].frequency;
    float best_score = peaks[0].magnitude;

    for (uint32_t i = 0; i < num_peaks; ++i) {
        const float candidate = peaks[i].frequency;
        if (candidate < 40.0f)
            continue;

        float score = peaks[i].magnitude;
        for (uint32_t j = 0; j < num_peaks; ++j) {
            if (i == j) continue;
            const float ratio = peaks[j].frequency / candidate;
            const float nearest = std::round(ratio);
            if (nearest >= 1.0f && nearest <= 64.0f) {
                const float err = std::abs(ratio - nearest) / nearest;
                if (err < 0.04f)
                    score += peaks[j].magnitude * 0.5f;
            }
        }

        if (score > best_score) {
            best_score = score;
            best_f0 = candidate;
        }
    }

    return best_f0;
}

} // namespace PeakUtils
