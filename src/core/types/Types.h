#pragma once

#include <cstdint>

// ── Hard limits (SPECS_09) ───────────────────────────────────────────
constexpr uint32_t MAX_PARTIALS          = 256;
constexpr uint32_t MAX_PEAKS             = 512;
constexpr uint32_t MAX_BIRTHS_PER_FRAME  = 32;
constexpr uint32_t MAX_NEIGHBORS         = 16;
constexpr uint32_t MAX_HISTORY           = 16;
constexpr uint32_t MAX_CLUSTERS          = 16;
constexpr uint32_t MAX_ATTRACTORS        = 8;
constexpr uint32_t MAX_THREADS           = 4;
constexpr uint32_t SNAPSHOT_COUNT        = 2;
constexpr uint32_t RING_BUFFER_SIZE      = 8192;
constexpr uint32_t FFT_MAX_SIZE          = 8192;
constexpr float    LOG_OCTAVES           = 10.0f;

// ── Plugin processing mode (SPECS_13) ───────────────────────────────
enum class ProcessMode : uint8_t {
    LiveInsert   = 0,
    FileGranular = 1
};

// ── Lifecycle states ─────────────────────────────────────────────────
enum class ParticleState : uint8_t {
    Alive,
    Dying,
    Dead,
    Frozen
};

// ── Niches (SPECS_06) ────────────────────────────────────────────────
enum class Niche : uint8_t {
    None,
    Drone,
    Scavenger,
    Predator,
    Absorber,
    Harmonizer,
    Parasite,
    TransientFeeder
};

// ── Spectral niche (N4, post-MVP) ────────────────────────────────────
enum class ClusterState : uint8_t {
    Forming,
    Stable,
    Dissolving,
    Dead
};

// ── Phase modes (SPECS_08) ───────────────────────────────────────────
enum class PhaseMode : uint8_t {
    Lock,
    Diffuse,
    Scatter,
    Random
};

// ── Transient modes (SPECS_08) ───────────────────────────────────────
enum class TransientMode : uint8_t {
    Protect,
    Diffuse,
    Trigger,
    Kill
};

// ── Macro-states (SPECS_06) ─────────────────────────────────────────
enum class MacroState : uint8_t {
    Stable,
    Bloom,
    Migration,
    Collapse,
    Frozen,
    Turbulence,
    Extinction
};

// ── External field types (SPECS_05) ──────────────────────────────────
enum class FieldType : uint8_t {
    Attractor,
    Repeller,
    Wind
};
