#pragma once

#include "Partial.h"
#include "Types.h"

// SPECS_09 — Snapshot of complete particle state for lock-free transfer
struct alignas(64) ParticleSnapshot {
    uint32_t frame_number     = 0;
    uint32_t num_partials     = 0;
    float    global_coherence = 1.0f;
    float    total_energy     = 0.0f;

    Partial partials[MAX_PARTIALS] = {};

    // Visual telemetry
    uint32_t births_this_frame = 0;
    uint32_t deaths_this_frame = 0;
    uint8_t  macro_state       = 0;  // MacroState as uint8 for alignment
    uint8_t  _pad[3]           = {};
};
