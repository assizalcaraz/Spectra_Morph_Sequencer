#pragma once

#include "../types/Partial.h"
#include "../types/Types.h"

// SPECS_09 — SoA layout for the additive oscillator bank
// Ensures cache-friendly iteration over all partials
struct alignas(32) AdditiveBuffer {
    float freq[MAX_PARTIALS]   = {};
    float amp[MAX_PARTIALS]    = {};
    float phase[MAX_PARTIALS]  = {};
    float env[MAX_PARTIALS]    = {};  // birth/death envelope

    // Bitmask of active indices (8 x uint32)
    uint32_t active_mask[(MAX_PARTIALS + 31) / 32] = {};
    uint32_t num_active = 0;

    // Desnapshot: flatten AoS Partial[] into SoA buffers
    void from_snapshot(const ParticleSnapshot& snap, float sample_rate) {
        num_active = snap.num_partials;
        std::memset(active_mask, 0, sizeof(active_mask));

        for (uint32_t i = 0; i < num_active; ++i) {
            const auto& p = snap.partials[i];
            freq[i]  = p.frequency;
            amp[i]   = p.amplitude;
            phase[i] = p.phase;
            env[i]   = (p.state == ParticleState::Alive) ? 1.0f
                     : (p.state == ParticleState::Dying) ? 0.3f
                     : 0.0f;

            uint32_t word = i / 32;
            uint32_t bit  = i % 32;
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
