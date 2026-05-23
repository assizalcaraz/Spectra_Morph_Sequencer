#pragma once

#include "Types.h"
#include <cstdint>

// SPECS_09 — 16 bytes
struct alignas(8) Peak {
    float    frequency  = 0.0f;
    float    magnitude  = 0.0f;
    float    phase      = 0.0f;
    uint16_t bin_index  = 0;
    uint8_t  _pad[2]    = {};
};

static_assert(sizeof(Peak) == 16, "Peak must be 16 bytes");
