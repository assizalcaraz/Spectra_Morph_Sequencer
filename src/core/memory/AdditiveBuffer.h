#pragma once

#include "../types/Partial.h"
#include "../types/Types.h"
#include <cmath>
#include <algorithm>
#include <cstring>

struct alignas(32) AdditiveBuffer {
    float freq[MAX_PARTIALS]   = {};
    float amp[MAX_PARTIALS]    = {};
    float phase[MAX_PARTIALS]  = {};
    float env[MAX_PARTIALS]    = {};

    uint32_t active_mask[(MAX_PARTIALS + 31) / 32] = {};
    uint32_t num_active = 0;
    float    sample_rate_ = 48000.0f;
    float    hop_size_    = 512.0f;

    void from_snapshot(const ParticleSnapshot& snap, float sample_rate,
                       uint32_t hop_size)
    {
        sample_rate_ = sample_rate;
        hop_size_    = static_cast<float>(hop_size);
        num_active   = snap.num_partials;
        std::memset(active_mask, 0, sizeof(active_mask));

        const float env_samples = sample_rate_ * 0.003f;

        for (uint32_t i = 0; i < num_active; ++i) {
            const auto& p = snap.partials[i];
            freq[i]  = p.frequency;
            amp[i]   = p.amplitude;
            phase[i] = p.phase;

            if (p.state == ParticleState::Alive) {
                env[i] = 1.0f;
            } else if (p.state == ParticleState::Dying) {
                const float t = std::min(p.age / env_samples, 1.0f);
                env[i] = 0.5f * (1.0f + std::cos(3.14159265f * t));
            } else {
                env[i] = 0.0f;
            }

            const uint32_t word = i / 32;
            const uint32_t bit  = i % 32;
            active_mask[word] |= (1u << bit);
        }
    }

    void clear() {
        std::memset(freq, 0, sizeof(freq));
        std::memset(amp, 0, sizeof(amp));
        std::memset(phase, 0, sizeof(phase));
        std::memset(env, 0, sizeof(env));
        std::memset(active_mask, 0, sizeof(active_mask));
        num_active = 0;
    }
};
