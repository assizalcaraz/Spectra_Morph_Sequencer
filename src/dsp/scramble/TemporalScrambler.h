#pragma once

#include "../../core/types/Types.h"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

// SPECS_13 — Permute STFT frame indices and optional bin scatter by coherence.

class TemporalScrambler {
public:
    static uint64_t mix_seed(uint64_t base, uint32_t frame) {
        return base ^ (static_cast<uint64_t>(frame) * 2654435761ull);
    }

    static float unit_rand(uint64_t& seed) {
        seed = seed * 1103515245ull + 12345ull;
        return static_cast<float>(seed & 0xFFFF) / 65535.0f;
    }

    static uint32_t map_frame_index(uint32_t write_index,
                                    uint32_t num_frames,
                                    float coherence,
                                    uint32_t frames_per_fragment,
                                    uint64_t seed)
    {
        if (num_frames == 0) return 0;
        if (write_index >= num_frames) write_index = num_frames - 1;

        if (coherence >= 0.85f)
            return write_index;

        const uint32_t frag = std::max(2u, frames_per_fragment);
        const uint32_t block = write_index / frag;
        const uint32_t block_start = block * frag;
        const uint32_t block_end = std::min(block_start + frag, num_frames);
        const uint32_t block_len = block_end - block_start;
        if (block_len <= 1)
            return write_index;

        if (coherence >= 0.5f) {
            uint64_t s = mix_seed(seed, write_index);
            const int jitter = static_cast<int>((unit_rand(s) * 2.0f - 1.0f)
                * static_cast<float>(frag) * (1.0f - coherence) * 2.0f);
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
        return order[std::min(local, block_len - 1)];
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
};
