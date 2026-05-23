#pragma once

#include "../types/Partial.h"
#include "../types/Types.h"
#include "../memory/PartialPool.h"
#include <cstdint>
#include <atomic>

// SPECS_10 — Scheduler & Degradation Strategy
// Runs as a state machine inside the DSP worker thread.
// Manages cadence, CPU pressure detection, and graceful degradation.

enum class PressureLevel : uint8_t {
    Nominal,
    Warming,
    High,
    Critical
};

struct DegradationState {
    // Dynamic params modified under pressure
    uint32_t dynamic_max_partials  = MAX_PARTIALS;
    uint32_t tracking_interval     = 1;   // every N frames
    uint32_t physics_interval      = 1;
    uint32_t ecology_interval      = 2;
    float    flocking_radius       = 3.0f;
    uint32_t hop_divisor           = 4;   // N / divisor
    bool     flocking_enabled      = true;
    bool     ecology_enabled       = true;

    PressureLevel pressure         = PressureLevel::Nominal;
    uint32_t frames_in_state       = 0;
};

class Scheduler {
public:
    Scheduler() = default;

    // Called every frame DSP (every H samples)
    // Returns true if tracking should run this frame
    bool tick(uint32_t frame_counter) {
        frame_counter_ = frame_counter;

        // Measure load via external timing (set before/after call)
        update_pressure();

        // Decouple cadences
        bool run_tracking  = (frame_counter % deg_.tracking_interval == 0);
        bool run_physics   = (frame_counter % deg_.physics_interval == 0);
        bool run_ecology   = (deg_.ecology_enabled &&
                              (frame_counter % deg_.ecology_interval == 0));

        // Store for query
        run_tracking_  = run_tracking;
        run_physics_   = run_physics;
        run_ecology_   = run_ecology;

        return run_tracking;
    }

    // ── Queries ────────────────────────────────────────────────────
    bool should_track()  const { return run_tracking_; }
    bool should_physics() const { return run_physics_; }
    bool should_ecology() const { return run_ecology_; }

    const DegradationState& state() const { return deg_; }
    PressureLevel pressure() const { return deg_.pressure; }
    float load_ema() const { return load_ema_; }

    // ── CPU timing ─────────────────────────────────────────────────
    void begin_frame()  { frame_start_ns_ = now_ns(); }
    void end_frame()    { frame_end_ns_   = now_ns(); }

    uint64_t frame_duration_ns() const {
        return frame_end_ns_ - frame_start_ns_;
    }

    // Deadline in ns for current DSP frame
    uint64_t deadline_ns(uint32_t hop_size, float sample_rate) const {
        return static_cast<uint64_t>(
            static_cast<double>(hop_size) / sample_rate * 1e9);
    }

    // ── Degradation ────────────────────────────────────────────────
    void set_pressure(PressureLevel p) {
        if (p != deg_.pressure) {
            deg_.frames_in_state = 0;
            deg_.pressure = p;
        }

        switch (p) {
        case PressureLevel::Nominal:
            deg_.dynamic_max_partials = MAX_PARTIALS;
            deg_.tracking_interval    = 1;
            deg_.physics_interval     = 1;
            deg_.hop_divisor          = 4;
            deg_.flocking_radius      = 3.0f;
            deg_.flocking_enabled     = true;
            break;

        case PressureLevel::Warming:
            deg_.dynamic_max_partials = 192;
            deg_.tracking_interval    = 2;
            deg_.physics_interval     = 2;
            break;

        case PressureLevel::High:
            deg_.dynamic_max_partials = 128;
            deg_.tracking_interval    = 2;
            deg_.physics_interval     = 4;
            deg_.hop_divisor          = 3;
            deg_.flocking_radius      = 1.0f;
            break;

        case PressureLevel::Critical:
            deg_.dynamic_max_partials = 64;
            deg_.tracking_interval    = 4;
            deg_.physics_interval     = 4;
            deg_.ecology_interval     = 8;
            deg_.flocking_enabled     = false;
            deg_.ecology_enabled      = false;
            break;
        }
    }

    // ── Partial budget ─────────────────────────────────────────────
    uint32_t max_partials() const { return deg_.dynamic_max_partials; }

    // Prune pool if needed to stay within dynamic budget
    uint32_t enforce_budget(PartialPool& pool) {
        if (pool.num_active() > deg_.dynamic_max_partials) {
            return pool.prune_to(deg_.dynamic_max_partials);
        }
        return 0;
    }

    // ── Frame skipping (SPECS_10 §8) ───────────────────────────────
    bool should_skip_simulation() const {
        return (deg_.pressure == PressureLevel::Critical)
            && ((frame_counter_ % 2) == 0);
    }

    // ── Hysteresis ─────────────────────────────────────────────────
    void update_pressure_with_hysteresis(float load) {
        // EMA
        load_ema_ = load_ema_ * 0.9f + load * 0.1f;

        ++deg_.frames_in_state;

        constexpr uint32_t HYSTERESIS_UP   = 4;
        constexpr uint32_t HYSTERESIS_DOWN = 8;

        switch (deg_.pressure) {
        case PressureLevel::Nominal:
            if (load_ema_ > 0.75f && deg_.frames_in_state > HYSTERESIS_UP)
                set_pressure(PressureLevel::Warming);
            break;
        case PressureLevel::Warming:
            if (load_ema_ > 0.85f && deg_.frames_in_state > HYSTERESIS_UP)
                set_pressure(PressureLevel::High);
            else if (load_ema_ < 0.65f && deg_.frames_in_state > HYSTERESIS_DOWN)
                set_pressure(PressureLevel::Nominal);
            break;
        case PressureLevel::High:
            if (load_ema_ > 0.95f && deg_.frames_in_state > HYSTERESIS_UP)
                set_pressure(PressureLevel::Critical);
            else if (load_ema_ < 0.7f && deg_.frames_in_state > HYSTERESIS_DOWN)
                set_pressure(PressureLevel::Warming);
            break;
        case PressureLevel::Critical:
            if (load_ema_ < 0.75f && deg_.frames_in_state > 16)
                set_pressure(PressureLevel::High);
            break;
        }
    }

    void reset() {
        deg_ = DegradationState{};
        load_ema_ = 0.0f;
        frame_counter_ = 0;
    }

private:
    void update_pressure() {
        uint64_t dur  = frame_duration_ns();
        uint64_t dl   = last_deadline_ns_;
        if (dl == 0) return;

        float load = static_cast<float>(dur) / static_cast<float>(dl);
        update_pressure_with_hysteresis(load);
    }

    static uint64_t now_ns() {
        return static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }

    DegradationState deg_;
    float load_ema_ = 0.0f;

    uint32_t frame_counter_    = 0;
    uint64_t frame_start_ns_   = 0;
    uint64_t frame_end_ns_     = 0;
    uint64_t last_deadline_ns_ = 0;

    bool run_tracking_  = true;
    bool run_physics_   = true;
    bool run_ecology_   = true;
};
