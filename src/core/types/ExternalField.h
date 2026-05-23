#pragma once

#include "Types.h"

// SPECS_09 / SPECS_05 — External fields (attractors, repellers, wind)
struct alignas(16) ExternalField {
    float     position  = 5.0f;   // spectral_pos (log)
    float     strength  = 0.0f;   // -10..10
    float     radius    = 1.0f;   // in octaves
    FieldType type      = FieldType::Attractor;
    uint8_t   _pad[3]   = {};
};

static_assert(sizeof(ExternalField) == 16, "ExternalField must be 16 bytes");
