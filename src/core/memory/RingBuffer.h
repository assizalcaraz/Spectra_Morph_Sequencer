#pragma once

#include "../types/Types.h"
#include <cstdint>
#include <atomic>
#include <cstring>

// SPECS_09 / SPECS_12 — Lock-free single-producer single-consumer ring buffer
template<typename T, uint32_t Capacity = RING_BUFFER_SIZE>
class SPSCRingBuffer {
public:
    SPSCRingBuffer() = default;

    bool write(const T& value) {
        uint32_t current_write = write_pos_.load(std::memory_order_relaxed);
        uint32_t current_read  = read_pos_.load(std::memory_order_acquire);
        uint32_t next_write    = (current_write + 1) % Capacity;

        if (next_write == current_read) {
            return false;  // full
        }

        buffer_[current_write] = value;
        write_pos_.store(next_write, std::memory_order_release);
        return true;
    }

    bool read(T& value) {
        uint32_t current_read  = read_pos_.load(std::memory_order_relaxed);
        uint32_t current_write = write_pos_.load(std::memory_order_acquire);

        if (current_read == current_write) {
            return false;  // empty
        }

        value = buffer_[current_read];
        read_pos_.store((current_read + 1) % Capacity, std::memory_order_release);
        return true;
    }

    uint32_t size() const {
        uint32_t w = write_pos_.load(std::memory_order_acquire);
        uint32_t r = read_pos_.load(std::memory_order_acquire);
        if (w >= r) return w - r;
        return Capacity - r + w;
    }

    bool empty() const {
        return read_pos_.load(std::memory_order_acquire)
            == write_pos_.load(std::memory_order_acquire);
    }

    void clear() {
        read_pos_.store(0, std::memory_order_relaxed);
        write_pos_.store(0, std::memory_order_relaxed);
    }

private:
    T buffer_[Capacity] = {};
    std::atomic<uint32_t> write_pos_ {0};
    std::atomic<uint32_t> read_pos_  {0};
};

// ── Float ring buffer for audio samples ──────────────────────────────
using AudioRingBuffer = SPSCRingBuffer<float, RING_BUFFER_SIZE>;

// ── Visual state ring buffer ─────────────────────────────────────────
struct VisualState {
    uint32_t frame_number     = 0;
    uint32_t num_partials     = 0;
    float    global_coherence = 1.0f;
    float    total_energy     = 0.0f;
    MacroState macro_state    = MacroState::Stable;

    // Partial data for rendering (subset of Partial)
    struct VisualPartial {
        float frequency;
        float amplitude;
        float spectral_pos;
        float harmonic_affinity;
        float coherence;
        float spatial_x;
        float spatial_y;
    };

    VisualPartial partials[MAX_PARTIALS] = {};
    uint32_t births_this_frame = 0;
    uint32_t deaths_this_frame = 0;
    float    cpu_load          = 0.0f;
};

using VisualRingBuffer = SPSCRingBuffer<VisualState, 64>;
