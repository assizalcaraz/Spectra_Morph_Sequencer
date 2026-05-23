#pragma once

#include "../../core/types/Types.h"
#include "../../core/types/Snapshot.h"
#include <cstdint>
#include <cmath>
#include <numbers>

// SPECS_08 — Phase locking strategies mapped from Coherence↔Chaos

namespace PhaseManager {

inline PhaseMode mode_from_coherence(float coherence) {
    coherence = std::clamp(coherence, 0.0f, 1.0f);
    if (coherence >= 0.85f) return PhaseMode::Lock;
    if (coherence >= 0.5f)  return PhaseMode::Lock;
    if (coherence >= 0.15f) return PhaseMode::Diffuse;
    if (coherence >= 0.05f) return PhaseMode::Scatter;
    return PhaseMode::Random;
}

inline float estimate_f0_from_snapshot(const ParticleSnapshot& snap) {
    float f0 = 0.0f;
    float best_score = 0.0f;
    for (uint32_t i = 0; i < snap.num_partials; ++i) {
        const auto& p = snap.partials[i];
        if (p.frequency < 40.0f) continue;
        const float score = p.energy / p.frequency;
        if (score > best_score) {
            best_score = score;
            f0 = p.frequency;
        }
    }
    return f0;
}

inline int harmonic_number(float freq, float f0) {
    if (f0 < 40.0f) return 0;
    return static_cast<int>(std::round(freq / f0));
}

inline void apply_to_snapshot(ParticleSnapshot& snap, float f0,
                              float coherence, uint64_t& seed)
{
    if (snap.num_partials == 0) return;

    const PhaseMode mode = mode_from_coherence(coherence);
    const float two_pi = 2.0f * std::numbers::pi_v<float>;
    const float chaos = 1.0f - std::clamp(coherence, 0.0f, 1.0f);

    if (f0 < 40.0f)
        f0 = estimate_f0_from_snapshot(snap);
    if (f0 < 40.0f) return;

    float phi_f0 = snap.partials[0].phase;
    for (uint32_t i = 0; i < snap.num_partials; ++i) {
        if (std::abs(snap.partials[i].frequency - f0) < f0 * 0.06f) {
            phi_f0 = snap.partials[i].phase;
            break;
        }
    }

    for (uint32_t i = 0; i < snap.num_partials; ++i) {
        auto& p = snap.partials[i];
        const int h = harmonic_number(p.frequency, f0);

        if (mode == PhaseMode::Lock && coherence >= 0.85f && h >= 1) {
            p.frequency = f0 * static_cast<float>(h);
            const float rel = p.phase - phi_f0 * static_cast<float>(h);
            p.phase = phi_f0 * static_cast<float>(h)
                    + std::fmod(rel + two_pi, two_pi);
        } else if (mode == PhaseMode::Diffuse || mode == PhaseMode::Scatter) {
            seed = seed * 1103515245ull + 12345ull;
            const float u = static_cast<float>(seed & 0xFFFF) / 65535.0f;
            const float diffusion = chaos * (mode == PhaseMode::Scatter ? 0.8f : 0.4f);
            p.phase += (u * 2.0f - 1.0f) * diffusion * std::numbers::pi_v<float>;
            p.phase = std::fmod(p.phase + two_pi, two_pi);
        } else if (mode == PhaseMode::Random) {
            seed = seed * 1103515245ull + 12345ull;
            p.phase = static_cast<float>(seed & 0xFFFF) / 65535.0f * two_pi;
        }
    }
}

} // namespace PhaseManager
