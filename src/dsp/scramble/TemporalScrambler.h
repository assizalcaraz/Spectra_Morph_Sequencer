#pragma once

#include "../../core/types/Types.h"
#include "../tracking/PeakUtils.h"
#include "SpectralFrameStore.h"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

// SPECS_13 — Permute STFT frame indices; scramble 0 = ordered, 1 = max disorder.

constexpr uint32_t kFileGranularMaxVoices = 8;
constexpr float    kFileVoiceGainDb       = -6.0f;
constexpr float    kFileVoiceGainLin      = 0.50118723362727225f; // 10^(-6/20)

class TemporalScrambler {
public:
    static uint64_t mix_seed(uint64_t base, uint32_t frame) {
        return base ^ (static_cast<uint64_t>(frame) * 2654435761ull);
    }

    static float unit_rand(uint64_t& seed) {
        seed = seed * 1103515245ull + 12345ull;
        return static_cast<float>(seed & 0xFFFF) / 65535.0f;
    }

    // scramble: 0 = consecutive frames, 1 = max temporal permutation within fragments.
    static uint32_t map_frame_index(uint32_t write_index,
                                    uint32_t num_frames,
                                    float scramble,
                                    uint32_t frames_per_fragment,
                                    uint64_t seed)
    {
        scramble = std::clamp(scramble, 0.0f, 1.0f);
        if (num_frames == 0) return 0;
        if (write_index >= num_frames) write_index = num_frames - 1;

        if (scramble <= 0.05f)
            return write_index;

        const uint32_t frag = std::max(2u, frames_per_fragment);
        const uint32_t block = write_index / frag;
        const uint32_t block_start = block * frag;
        const uint32_t block_end = std::min(block_start + frag, num_frames);
        const uint32_t block_len = block_end - block_start;
        if (block_len <= 1)
            return write_index;

        if (scramble <= 0.5f) {
            uint64_t s = mix_seed(seed, write_index);
            const float jitter_scale = scramble / 0.5f;
            const int jitter = static_cast<int>((unit_rand(s) * 2.0f - 1.0f)
                * static_cast<float>(frag) * jitter_scale * 2.0f);
            int idx = static_cast<int>(write_index) + jitter;
            const int lo = static_cast<int>(block_start);
            const int hi = static_cast<int>(block_end) - 1;
            if (lo >= hi)
                return write_index;
            idx = std::clamp(idx, lo, hi);
            return static_cast<uint32_t>(idx);
        }

        std::vector<uint32_t> order(block_len);
        for (uint32_t i = 0; i < block_len; ++i)
            order[i] = block_start + i;

        uint64_t s = mix_seed(seed, block);
        for (uint32_t i = block_len; i > 1; --i) {
            const uint32_t j = static_cast<uint32_t>(unit_rand(s) * static_cast<float>(i));
            std::swap(order[i - 1], order[j]);
        }

        const uint32_t local = write_index - block_start;
        uint32_t mapped = order[std::min(local, block_len - 1)];

        if (scramble < 1.0f) {
            const float blend = (scramble - 0.5f) / 0.5f;
            if (unit_rand(s) > blend)
                mapped = write_index;
        }

        return mapped;
    }

    static void scatter_bins(float* mag, float* phase, float* raw_phase,
                             uint32_t half_n, float amount, uint64_t seed)
    {
        if (amount < 0.01f || half_n < 4) return;

        std::vector<uint32_t> perm(half_n);
        for (uint32_t k = 0; k < half_n; ++k)
            perm[k] = k;

        uint64_t s = seed;
        for (uint32_t i = half_n; i > 2; --i) {
            const uint32_t j = 1u + static_cast<uint32_t>(
                unit_rand(s) * static_cast<float>(i - 2));
            std::swap(perm[i - 1], perm[j]);
        }
        perm[0] = 0;

        std::vector<float> tm(half_n), tp(half_n), tr(half_n);
        std::memcpy(tm.data(), mag, half_n * sizeof(float));
        std::memcpy(tp.data(), phase, half_n * sizeof(float));
        if (raw_phase)
            std::memcpy(tr.data(), raw_phase, half_n * sizeof(float));

        const float mix = std::clamp(amount, 0.0f, 1.0f);
        for (uint32_t k = 1; k < half_n; ++k) {
            const uint32_t src = perm[k];
            mag[k]   = tm[k]   * (1.0f - mix) + tm[src] * mix;
            phase[k] = tp[k]   * (1.0f - mix) + tp[src] * mix;
            if (raw_phase)
                raw_phase[k] = tr[k] * (1.0f - mix) + tr[src] * mix;
        }
    }

    static uint32_t voice_count(uint32_t requested, float scramble) {
        scramble = std::clamp(scramble, 0.0f, 1.0f);
        if (scramble <= 0.15f)
            return 1;
        if (requested < 1)
            return 1;
        return std::min(requested, kFileGranularMaxVoices);
    }

    // Sum scrambled frames in complex domain (-6 dB per voice).
    static uint32_t mix_scrambled_voices(
        const SpectralFrameStore& store,
        uint32_t write_idx,
        float scramble,
        uint32_t frames_per_fragment,
        uint64_t base_seed,
        float bin_scatter,
        uint32_t num_voices,
        float* out_mag,
        float* out_phase,
        float* out_raw_phase,
        uint32_t half_bins,
        float* voice_mag,
        float* voice_phase,
        float* voice_raw_phase,
        float* acc_re,
        float* acc_im,
        const float* voice_pitch_ratios = nullptr)
    {
        scramble = std::clamp(scramble, 0.0f, 1.0f);
        const uint32_t n_frames = store.num_frames();
        if (n_frames == 0 || half_bins == 0)
            return 0;

        num_voices = voice_count(num_voices, scramble);

        const bool identity = scramble <= 0.01f
                           && bin_scatter <= 0.01f
                           && num_voices <= 1u
                           && voice_pitch_ratios == nullptr;
        if (identity) {
            const uint32_t idx = write_idx >= n_frames ? n_frames - 1u : write_idx;
            store.copy_frame(idx, out_mag, out_phase, out_raw_phase);
            return idx;
        }

        std::memset(acc_re, 0, half_bins * sizeof(float));
        std::memset(acc_im, 0, half_bins * sizeof(float));

        uint32_t primary_read = write_idx;

        for (uint32_t v = 0; v < num_voices; ++v) {
            const uint64_t seed_v = mix_seed(base_seed, 0x9E3779B97F4A7C15ull * (v + 1ull));
            uint32_t read_idx = map_frame_index(
                write_idx, n_frames, scramble, frames_per_fragment, seed_v);

            if (num_voices > 1 && n_frames > 1) {
                const uint32_t offset = (v * std::max(1u, frames_per_fragment)) / num_voices;
                read_idx = (read_idx + offset) % n_frames;
            }

            if (v == 0)
                primary_read = read_idx;

            store.copy_frame(read_idx, voice_mag, voice_phase, voice_raw_phase);

            if (bin_scatter > 0.01f) {
                scatter_bins(voice_mag, voice_phase, voice_raw_phase,
                             half_bins, bin_scatter,
                             mix_seed(seed_v, read_idx));
            }

            if (voice_pitch_ratios != nullptr) {
                const float ratio = voice_pitch_ratios[v];
                if (std::abs(ratio - 1.0f) > 0.001f) {
                    PeakUtils::shift_spectrum(
                        voice_mag, voice_phase, voice_raw_phase,
                        half_bins, ratio);
                }
            }

            const float voice_gain = kFileVoiceGainLin
                / std::sqrt(static_cast<float>(num_voices));
            for (uint32_t k = 0; k < half_bins; ++k) {
                const float ph = voice_phase[k];
                acc_re[k] += voice_mag[k] * std::cos(ph) * voice_gain;
                acc_im[k] += voice_mag[k] * std::sin(ph) * voice_gain;
            }
        }

        for (uint32_t k = 0; k < half_bins; ++k) {
            out_mag[k] = std::sqrt(acc_re[k] * acc_re[k] + acc_im[k] * acc_im[k]);
            out_phase[k] = std::atan2(acc_im[k], acc_re[k]);
            if (out_raw_phase)
                out_raw_phase[k] = out_phase[k];
        }

        return primary_read;
    }
};
