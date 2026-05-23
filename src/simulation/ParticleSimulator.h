#pragma once

#include "../core/types/Snapshot.h"
#include "../core/types/Types.h"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <numbers>

// SPECS_05 — Spectral particle physics (simulation thread, no RT alloc)

struct SimParams {
    float gravity = 1.0f;
    float motion  = 0.3f;
    float decay   = 0.5f;
    float spread  = 0.5f;
};

class ParticleSimulator {
public:
    void step(const ParticleSnapshot& input, ParticleSnapshot& output,
              const SimParams& params, uint64_t& rng_seed)
    {
        output = input;
        if (output.num_partials == 0) return;

        const float G = params.gravity * 0.06f;
        const float damping = 0.08f + params.decay * 0.12f;
        const float thermal = params.motion * 0.045f;
        const float repulsion = params.spread * 0.22f;
        const float energy_decay = 1.0f - params.decay * 0.06f;
        const float amp_decay = 1.0f - params.decay * 0.04f;

        for (uint32_t i = 0; i < output.num_partials; ++i) {
            auto& p = output.partials[i];
            if (p.amplitude < 1e-8f) continue;

            const float harmonic_pos = std::round(p.spectral_pos);
            const float pull = -G * p.harmonic_affinity
                             * (p.spectral_pos - harmonic_pos);
            p.velocity += pull;

            rng_seed = rng_seed * 1103515245ull + 12345ull;
            const float noise = (static_cast<float>(rng_seed & 0xFFFF) / 65535.0f)
                              * 2.0f - 1.0f;
            p.velocity += noise * thermal * p.temperature;

            p.velocity *= (1.0f - damping);
            p.spectral_pos += p.velocity * 0.14f;
            p.spectral_pos = std::clamp(p.spectral_pos, 0.0f, LOG_OCTAVES);
            p.frequency = 20.0f * std::pow(2.0f, p.spectral_pos);

            p.energy *= energy_decay;
            p.amplitude *= amp_decay;
            p.temperature = std::clamp(1.0f - p.coherence, 0.0f, 1.0f);
        }

        for (uint32_t i = 0; i < output.num_partials; ++i) {
            auto& pi = output.partials[i];
            for (uint32_t j = i + 1; j < output.num_partials; ++j) {
                auto& pj = output.partials[j];
                const float df = std::abs(pi.frequency - pj.frequency);
                if (df < 1.0f && df > 1e-6f) {
                    const float push = repulsion / (df * df + 0.01f);
                    const float sign = (pi.spectral_pos >= pj.spectral_pos) ? 1.0f : -1.0f;
                    pi.spectral_pos += push * sign * 0.003f;
                    pj.spectral_pos -= push * sign * 0.003f;
                }
            }
        }

        uint32_t write = 0;
        for (uint32_t i = 0; i < output.num_partials; ++i) {
            if (output.partials[i].energy > 1e-6f
                && output.partials[i].amplitude > 1e-7f) {
                if (write != i)
                    output.partials[write] = output.partials[i];
                ++write;
            }
        }
        output.num_partials = write;
    }
};
