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

// Catmull-Rom cubic interpolation: x in [0,1) between y1 and y2
inline float cubic_interpolate(float x, float y0, float y1, float y2, float y3)
{
    const float x2 = x * x;
    const float x3 = x2 * x;
    return 0.5f * ((2.0f * y1)
        + (-y0 + y2) * x
        + (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3) * x2
        + (-y0 + 3.0f * y1 - 3.0f * y2 + y3) * x3);
}

// Interpolated magnitude at arbitrary frequency (cubic, sub-bin accuracy)
inline float magnitude_at(float freq_hz, const float* mag,
                           float sample_rate, uint32_t fft_size,
                           uint32_t half_n)
{
    const float bin_hz = sample_rate / static_cast<float>(fft_size);
    const float bin_pos = freq_hz / bin_hz;
    const uint32_t bin = static_cast<uint32_t>(bin_pos);
    if (bin < 1 || bin >= half_n - 2) return 0.0f;
    const float frac = bin_pos - static_cast<float>(bin);
    return cubic_interpolate(frac, mag[bin - 1], mag[bin],
                                   mag[bin + 1], mag[bin + 2]);
}

// Harmonic Product Spectrum: find F0 by accumulating harmonic energy
inline float find_fundamental_hps(const float* mag, uint32_t half_n,
                                   float sample_rate, uint32_t fft_size,
                                   float threshold)
{
    const float bin_hz = sample_rate / static_cast<float>(fft_size);
    const uint32_t lo_bin = std::max(1u, static_cast<uint32_t>(40.0f / bin_hz));
    const uint32_t hi_bin = std::min(half_n - 1,
        static_cast<uint32_t>(2000.0f / bin_hz));

    float best_f0 = 0.0f;
    float best_score = threshold;
    const int max_harmonics = 5;

    for (uint32_t b = lo_bin; b < hi_bin; ++b) {
        float score = mag[b];
        if (score < 1e-8f) continue;
        for (int h = 2; h <= max_harmonics; ++h) {
            const uint32_t hb = b * h;
            if (hb >= half_n) break;
            score += mag[hb];
        }
        if (score > best_score) {
            best_score = score;
            best_f0 = static_cast<float>(b) * bin_hz;
        }
    }

    return best_f0;
}

inline bool peak_near(const Peak* peaks, uint32_t n, float freq, float tolerance_hz)
{
    for (uint32_t i = 0; i < n; ++i) {
        if (std::abs(peaks[i].frequency - freq) < tolerance_hz)
            return true;
    }
    return false;
}

// Fill missing harmonics of f0 for tonal sources — uses interpolated magnitudes
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
        const float interp_mag = magnitude_at(hf, mag, sample_rate, fft_size, half_n);
        if (interp_mag < local_thresh)
            continue;

        auto& p = peaks[num_peaks++];
        p.bin_index = static_cast<uint16_t>(bin);
        p.frequency = hf;
        p.magnitude = interp_mag;
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

// Triangle / square: odd harmonics dominate
inline bool prefer_odd_harmonics(const float* mag, float f0,
                                 float sample_rate, uint32_t fft_size,
                                 uint32_t half_n)
{
    if (f0 < 40.0f) return false;

    auto mag_at = [&](int h) -> float {
        const uint32_t bin = static_cast<uint32_t>(std::round(
            f0 * static_cast<float>(h) * static_cast<float>(fft_size) / sample_rate));
        if (bin < 1 || bin >= half_n - 1) return 0.0f;
        return mag[bin];
    };

    const float even_sum = mag_at(2) + mag_at(4) + mag_at(6);
    const float odd_sum  = mag_at(1) + mag_at(3) + mag_at(5) + mag_at(7);
    return odd_sum > even_sum * 1.8f;
}

// High-coherence path: one partial per harmonic with cubically interpolated magnitudes
inline void build_harmonic_peaks(Peak* peaks, uint32_t& num_peaks,
                                  float f0, const float* mag, const float* phase,
                                  float sample_rate, uint32_t fft_size,
                                  uint32_t half_n, bool odd_only,
                                  uint32_t max_peaks)
{
    num_peaks = 0;
    if (f0 < 40.0f) return;

    const float nyquist = sample_rate * 0.45f;
    const int max_h = static_cast<int>(nyquist / f0);
    const float bin_hz = sample_rate / static_cast<float>(fft_size);

    for (int h = 1; h <= max_h && num_peaks < max_peaks; ++h) {
        if (odd_only && (h % 2 == 0)) continue;

        const float hf = f0 * static_cast<float>(h);
        const float bin_pos = hf / bin_hz;
        const uint32_t bin = static_cast<uint32_t>(bin_pos);

        if (bin < 1 || bin >= half_n - 2) continue;

        // Interpolated magnitude at exact harmonic frequency
        const float frac = bin_pos - static_cast<float>(bin);
        const float interp_mag = cubic_interpolate(
            frac, mag[bin - 1], mag[bin], mag[bin + 1], mag[bin + 2]);
        if (interp_mag < 1e-8f) continue;

        auto& p = peaks[num_peaks++];
        p.bin_index = static_cast<uint16_t>(bin);
        p.frequency = hf;
        p.magnitude = interp_mag;
        p.phase     = phase[bin];
    }
}

inline float find_fundamental_bin(const float* mag, uint32_t half_n,
                                  float sample_rate, uint32_t fft_size,
                                  float threshold)
{
    const float bin_hz = sample_rate / static_cast<float>(fft_size);
    uint32_t best_bin = 0;
    float best_mag = threshold;

    const uint32_t max_bin = std::min(half_n - 1,
        static_cast<uint32_t>(2000.0f / bin_hz));

    for (uint32_t i = 1; i < max_bin; ++i) {
        if (mag[i] > best_mag
            && mag[i] > mag[i - 1]
            && mag[i] >= mag[i + 1]) {
            best_mag = mag[i];
            best_bin = i;
        }
    }

    if (best_bin == 0) return 0.0f;
    return refine_frequency(best_bin, mag, sample_rate, fft_size);
}

} // namespace PeakUtils
