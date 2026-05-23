#pragma once

#include "Types.h"
#include <cstdint>

// SPECS_09 — 96 bytes, aligned to 16 for SIMD
struct alignas(16) Partial {
    // Identity (read-only after birth)
    uint32_t id            = 0;
    uint32_t birth_frame   = 0;
    uint32_t lineage_id    = 0;
    uint32_t parent_id     = 0;

    // Spectral core (updated by DSP worker + simulation)
    float frequency        = 440.0f;
    float amplitude        = 0.0f;
    float phase            = 0.0f;
    float energy           = 0.0f;

    // Temporal (updated by scheduler + simulation)
    float age              = 0.0f;
    float lifetime_remaining = 2.0f;
    float stability        = 1.0f;
    float coherence        = 1.0f;

    // Behavioral (updated by simulation + ecology)
    float harmonic_affinity = 0.5f;
    float mass              = 1.0f;
    float drift             = 0.0f;
    float temperature       = 0.0f;   // 1 - coherence

    // Spatial (log-frequency space, 0..10 octaves)
    float spectral_pos     = 5.0f;
    float velocity         = 0.0f;
    float spatial_x        = 0.0f;
    float spatial_y        = 0.0f;

    // Lifecycle
    ParticleState state    = ParticleState::Alive;
    Niche niche            = Niche::None;
    uint8_t hold_counter   = 0;
    uint8_t _pad           = 0;

    // Padding to 96 bytes (6 cache lines of 16)
    uint8_t _pad2[8]       = {};
};

static_assert(sizeof(Partial) == 96, "Partial must be 96 bytes");
