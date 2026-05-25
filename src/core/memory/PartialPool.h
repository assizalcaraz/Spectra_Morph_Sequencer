#pragma once

#include "../types/Partial.h"
#include "../types/Snapshot.h"
#include "../types/Types.h"
#include <cstring>
#include <cassert>
#include <utility>
#include <algorithm>

// SPECS_09 — Lock-free partial pool with bitmask allocation
// All memory pre-allocated. No malloc in realtime.
class PartialPool {
public:
    PartialPool() {
        clear();
    }

    // Allocate a slot. Returns index or MAX_PARTIALS if full.
    uint32_t allocate() {
        for (uint32_t i = 0; i < MAX_PARTIALS; ++i) {
            uint32_t word = i / 32;
            uint32_t bit  = i % 32;
            if (!(free_mask_[word] & (1u << bit))) {
                free_mask_[word] |= (1u << bit);
                partials_[i].state = ParticleState::Alive;
                ++num_active_;
                return i;
            }
        }
        return MAX_PARTIALS;  // full
    }

    void free(uint32_t index) {
        if (index >= MAX_PARTIALS) return;
        partials_[index].state = ParticleState::Dead;
        uint32_t word = index / 32;
        uint32_t bit  = index % 32;
        free_mask_[word] &= ~(1u << bit);
        --num_active_;
    }

    bool is_alive(uint32_t index) const {
        if (index >= MAX_PARTIALS) return false;
        uint32_t word = index / 32;
        uint32_t bit  = index % 32;
        return (free_mask_[word] & (1u << bit)) != 0;
    }

    Partial& operator[](uint32_t index) {
        assert(index < MAX_PARTIALS);
        return partials_[index];
    }

    const Partial& operator[](uint32_t index) const {
        assert(index < MAX_PARTIALS);
        return partials_[index];
    }

    uint32_t num_active() const { return num_active_; }
    bool     full()       const { return num_active_ >= MAX_PARTIALS; }

    // Clear all slots
    void clear() {
        std::memset(free_mask_, 0, sizeof(free_mask_));
        std::memset(partials_, 0, sizeof(partials_));
        for (auto& p : partials_) {
            p.state = ParticleState::Dead;
        }
        num_active_ = 0;
    }

    // Write all active partials into a snapshot
    void write_snapshot(ParticleSnapshot& snap, uint32_t frame) const {
        snap.frame_number   = frame;
        snap.num_partials   = num_active_;
        snap.global_coherence = 0.0f;
        snap.total_energy   = 0.0f;
        snap.births_this_frame = 0;
        snap.deaths_this_frame = 0;

        float coherence_sum = 0.0f;
        uint32_t out_idx = 0;
        for (uint32_t i = 0; i < MAX_PARTIALS; ++i) {
            if (is_alive(i)) {
                snap.partials[out_idx++] = partials_[i];
                coherence_sum += std::clamp(
                    partials_[i].coherence, 0.0f, 1.0f);
                snap.total_energy += partials_[i].energy;
            }
        }
        if (out_idx > 0) {
            snap.global_coherence = std::clamp(
                coherence_sum / static_cast<float>(out_idx), 0.0f, 1.0f);
        }
    }

    // Prune weakest partials until count <= target
    uint32_t prune_to(uint32_t target) {
        if (num_active_ <= target) return 0;

        // Find indices of alive partials
        uint32_t alive[MAX_PARTIALS];
        uint32_t n_alive = 0;
        for (uint32_t i = 0; i < MAX_PARTIALS; ++i) {
            if (is_alive(i)) {
                alive[n_alive++] = i;
            }
        }

        // Sort by energy * coherence (weakest first)
        // Simple O(n²) selection — n is small, acceptable
        uint32_t to_kill = n_alive - target;
        for (uint32_t k = 0; k < to_kill; ++k) {
            uint32_t weakest = k;
            float weakest_score = partials_[alive[k]].energy
                                * partials_[alive[k]].coherence;
            for (uint32_t j = k + 1; j < n_alive; ++j) {
                float score = partials_[alive[j]].energy
                            * partials_[alive[j]].coherence;
                if (score < weakest_score) {
                    weakest = j;
                    weakest_score = score;
                }
            }
            std::swap(alive[k], alive[weakest]);
            free(alive[k]);
        }

        return to_kill;
    }

private:
    Partial  partials_[MAX_PARTIALS];
    uint32_t free_mask_[(MAX_PARTIALS + 31) / 32] = {};
    uint32_t num_active_ = 0;
};
