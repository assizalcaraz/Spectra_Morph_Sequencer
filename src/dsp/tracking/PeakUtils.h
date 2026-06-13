#pragma once

#include "../../core/types/Peak.h"
#include "../../core/types/Snapshot.h"
#include "../../core/types/Types.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>

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

inline float phase_at(float freq_hz, const float* phase,
                    float sample_rate, uint32_t fft_size, uint32_t half_n)
{
    if (phase == nullptr || freq_hz < 40.0f) return 0.0f;
    const float bin_hz = sample_rate / static_cast<float>(fft_size);
    const float bin_pos = freq_hz / bin_hz;
    const uint32_t k = static_cast<uint32_t>(bin_pos);
    if (k < 1 || k >= half_n - 1)
        return (k < half_n) ? phase[k] : 0.0f;
    const float frac = bin_pos - static_cast<float>(k);
    return phase[k] * (1.0f - frac) + phase[k + 1] * frac;
}

inline float harmonic_score(float f0, const float* mag, uint32_t half_n,
                            float sample_rate, uint32_t fft_size)
{
    if (f0 < 40.0f) return 0.0f;

    float harm_sum = 0.0f;
    float total = 1e-12f;
    const float nyquist = sample_rate * 0.45f;
    const int max_h = std::max(1, static_cast<int>(nyquist / f0));

    for (int h = 1; h <= max_h; ++h) {
        const float hf = f0 * static_cast<float>(h);
        harm_sum += magnitude_at(hf, mag, sample_rate, fft_size, half_n);
    }

    const float bin_hz = sample_rate / static_cast<float>(fft_size);
    const uint32_t max_bin = std::min(half_n,
        static_cast<uint32_t>(2000.0f / bin_hz));
    for (uint32_t k = 1; k < max_bin; ++k)
        total += mag[k];

    return harm_sum / total;
}

inline float compute_harmonic_affinity(float freq, float f0)
{
    if (f0 < 40.0f || freq < 40.0f) return 0.0f;
    const float ratio = freq / f0;
    const float nearest = std::round(ratio);
    if (nearest < 1.0f) return 0.0f;
    const float err = std::abs(ratio - nearest) / nearest;
    return std::clamp(1.0f - err * 8.0f, 0.0f, 1.0f);
}

inline float update_f0_ema(float prev_f0, float candidate_f0, float score,
                           float min_score = 0.12f)
{
    if (candidate_f0 < 40.0f || score < min_score)
        return prev_f0;
    if (prev_f0 < 40.0f)
        return candidate_f0;
    return prev_f0 * 0.92f + candidate_f0 * 0.08f;
}

// Harmonic Product Spectrum with subharmonic rejection + harmonic validation
inline float find_fundamental_hps(const float* mag, uint32_t half_n,
                                   float sample_rate, uint32_t fft_size,
                                   float threshold, float* out_score = nullptr)
{
    const float bin_hz = sample_rate / static_cast<float>(fft_size);
    const uint32_t lo_bin = std::max(1u, static_cast<uint32_t>(40.0f / bin_hz));
    const uint32_t hi_bin = std::min(half_n - 1,
        static_cast<uint32_t>(2000.0f / bin_hz));

    float best_f0 = 0.0f;
    float best_metric = threshold;
    uint32_t best_bin = 0;
    const int max_harmonics = 10;

    for (uint32_t b = lo_bin; b < hi_bin; ++b) {
        if (mag[b] < mag[b - 1] || mag[b] < mag[b + 1])
            continue;

        float hps_score = mag[b];
        for (int h = 2; h <= max_harmonics; ++h) {
            const uint32_t hb = b * static_cast<uint32_t>(h);
            if (hb >= half_n) break;
            hps_score += mag[hb] * 0.85f;
        }

        const float f0_cand = refine_frequency(b, mag, sample_rate, fft_size);

        float sub_penalty = 0.0f;
        for (int div = 2; div <= 4; ++div) {
            const uint32_t sub_b = b / static_cast<uint32_t>(div);
            if (sub_b >= lo_bin && mag[sub_b] > mag[b] * 0.7f)
                sub_penalty += mag[sub_b];
        }

        const float metric = hps_score - sub_penalty * 1.5f;
        if (metric > best_metric) {
            best_metric = metric;
            best_bin = b;
            best_f0 = f0_cand;
        }
    }

    if (best_bin == 0) {
        if (out_score) *out_score = 0.0f;
        return 0.0f;
    }

    const float refined = find_fundamental_bin(mag, half_n, sample_rate, fft_size, threshold);
    if (refined > 40.0f) {
        const float hps_score = harmonic_score(best_f0, mag, half_n, sample_rate, fft_size);
        const float ref_score = harmonic_score(refined, mag, half_n, sample_rate, fft_size);
        if (ref_score > hps_score * 1.08f)
            best_f0 = refined;
    }

    const float score = harmonic_score(best_f0, mag, half_n, sample_rate, fft_size);
    float final_f0 = best_f0;
    float final_score = score;

    for (int div = 2; div <= 4; ++div) {
        const float sub = best_f0 / static_cast<float>(div);
        if (sub < 40.0f) continue;
        const float sub_score = harmonic_score(sub, mag, half_n, sample_rate, fft_size);
        if (sub_score >= final_score * 0.82f) {
            final_f0 = sub;
            final_score = sub_score;
        }
    }

    for (int guard = 0; guard < 3; ++guard) {
        const float half = final_f0 * 0.5f;
        if (half < 40.0f) break;
        const float half_score = harmonic_score(
            half, mag, half_n, sample_rate, fft_size);
        if (half_score >= final_score * 0.78f) {
            final_f0 = half;
            final_score = half_score;
        } else {
            break;
        }
    }

    const float nyquist = sample_rate * 0.45f;
    for (int pass = 0; pass < 4; ++pass) {
        const float fund_mag = magnitude_at(
            final_f0, mag, sample_rate, fft_size, half_n);
        int boost = 1;
        float boost_mag = fund_mag;
        for (int h = 2; h <= 16; ++h) {
            const float hf = final_f0 * static_cast<float>(h);
            if (hf > nyquist) break;
            const float hm = magnitude_at(hf, mag, sample_rate, fft_size, half_n);
            if (hm > boost_mag * 1.12f) {
                boost_mag = hm;
                boost = h;
            }
        }
        if (boost > 1) {
            final_f0 *= static_cast<float>(boost);
        } else {
            break;
        }
    }
    final_score = harmonic_score(
        final_f0, mag, half_n, sample_rate, fft_size);

    const float strongest = find_fundamental_bin(
        mag, half_n, sample_rate, fft_size, threshold * 0.05f);
    if (strongest >= 40.0f) {
        const float ratio = final_f0 / strongest;
        if (ratio > 1.35f || ratio < 0.72f) {
            final_f0 = strongest;
            final_score = harmonic_score(
                final_f0, mag, half_n, sample_rate, fft_size);
        }
    }

    if (out_score) *out_score = final_score;
    return final_f0;
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

// High-coherence path: one partial per harmonic; depth = harmonic range, density = pruning
inline void build_harmonic_peaks(Peak* peaks, uint32_t& num_peaks,
                                  float f0, const float* mag, const float* phase,
                                  float sample_rate, uint32_t fft_size,
                                  uint32_t half_n, bool odd_only,
                                  float density, float depth, uint32_t max_peaks)
{
    num_peaks = 0;
    if (f0 < 40.0f) return;

    density = std::clamp(density, 0.0f, 1.0f);
    depth   = std::clamp(depth, 0.0f, 1.0f);

    const float nyquist = sample_rate * 0.45f;
    const int max_h_full = std::max(1, static_cast<int>(nyquist / f0));
    const int max_h = std::max(1, static_cast<int>(std::round(
        static_cast<float>(max_h_full) * (0.06f + depth * 0.94f))));
    const float bin_hz = sample_rate / static_cast<float>(fft_size);

    float peak_mag = 0.0f;
    for (int h = 1; h <= max_h && num_peaks < max_peaks; ++h) {
        if (odd_only && (h % 2 == 0)) continue;

        const float hf = f0 * static_cast<float>(h);
        const float bin_pos = hf / bin_hz;
        const uint32_t bin = static_cast<uint32_t>(bin_pos);

        if (bin < 1 || bin >= half_n - 2) continue;

        const float frac = bin_pos - static_cast<float>(bin);
        const float interp_mag = cubic_interpolate(
            frac, mag[bin - 1], mag[bin], mag[bin + 1], mag[bin + 2]);
        if (interp_mag < 1e-8f) continue;

        peak_mag = std::max(peak_mag, interp_mag);

        auto& p = peaks[num_peaks++];
        p.bin_index = static_cast<uint16_t>(bin);
        p.frequency = hf;
        p.magnitude = interp_mag;
        p.phase     = phase_at(hf, phase, sample_rate, fft_size, half_n);
    }

    if (num_peaks == 0) return;

    std::sort(peaks, peaks + num_peaks,
        [](const Peak& a, const Peak& b) { return a.frequency < b.frequency; });

    // Relative floor: low density drops weak upper harmonics
    const float rel_floor = 0.002f + density * 0.028f;
    const float abs_floor = peak_mag * rel_floor;
    uint32_t write = 0;
    for (uint32_t i = 0; i < num_peaks; ++i) {
        if (peaks[i].magnitude >= abs_floor)
            peaks[write++] = peaks[i];
    }
    num_peaks = write;
    if (num_peaks == 0) return;

    const uint32_t hard_cap = 2u + static_cast<uint32_t>(
        density * static_cast<float>(max_peaks - 2));
    if (num_peaks > hard_cap) {
        std::sort(peaks, peaks + num_peaks,
            [](const Peak& a, const Peak& b) {
                return a.magnitude > b.magnitude;
            });
        num_peaks = hard_cap;
    }
}

inline float semitones_to_ratio(float semitones) {
    return std::pow(2.0f, semitones / 12.0f);
}

// Resample magnitude/phase bins by pitch ratio (ratio > 1 = shift up).
inline void shift_spectrum(float* mag, float* phase, float* raw_phase,
                           uint32_t half_n, float ratio)
{
    ratio = std::clamp(ratio, 0.25f, 4.0f);
    if (std::abs(ratio - 1.0f) < 0.001f)
        return;

    std::vector<float> tm(half_n), tp(half_n), tr(half_n);
    std::memcpy(tm.data(), mag, half_n * sizeof(float));
    std::memcpy(tp.data(), phase, half_n * sizeof(float));
    if (raw_phase)
        std::memcpy(tr.data(), raw_phase, half_n * sizeof(float));

    std::memset(mag, 0, half_n * sizeof(float));
    std::memset(phase, 0, half_n * sizeof(float));
    if (raw_phase)
        std::memset(raw_phase, 0, half_n * sizeof(float));

    mag[0] = tm[0];
    phase[0] = tp[0];
    if (raw_phase)
        raw_phase[0] = tr[0];

    for (uint32_t k = 1; k < half_n; ++k) {
        const float src = static_cast<float>(k) / ratio;
        if (src < 1.0f || src >= static_cast<float>(half_n - 1))
            continue;

        const int k0 = static_cast<int>(src);
        const float frac = src - static_cast<float>(k0);
        const float m = tm[static_cast<uint32_t>(k0)] * (1.0f - frac)
                      + tm[static_cast<uint32_t>(k0 + 1)] * frac;
        const float p = tp[static_cast<uint32_t>(k0)] * (1.0f - frac)
                      + tp[static_cast<uint32_t>(k0 + 1)] * frac;
        mag[k] = m;
        phase[k] = p;
        if (raw_phase) {
            const float r = tr[static_cast<uint32_t>(k0)] * (1.0f - frac)
                          + tr[static_cast<uint32_t>(k0 + 1)] * frac;
            raw_phase[k] = r;
        }
    }
}

inline void apply_pitch_to_snapshot(ParticleSnapshot& snap, float ratio) {
    if (std::abs(ratio - 1.0f) < 0.001f)
        return;
    for (uint32_t i = 0; i < snap.num_partials; ++i)
        snap.partials[i].frequency *= ratio;
}

} // namespace PeakUtils
