#pragma once

#include "../../core/types/Types.h"
#include <cstdint>
#include <vector>
#include <cstring>

// Ring of STFT frames for temporal permutation (FileGranular mode).

struct SpectralFrame {
    std::vector<float> mag;
    std::vector<float> phase;
    std::vector<float> raw_phase;
    float rms = 0.0f;
    uint32_t index = 0;
};

class SpectralFrameStore {
public:
    void prepare(uint32_t half_n, uint32_t max_frames) {
        half_n_ = half_n + 1;
        max_frames_ = std::max(32u, max_frames);
        frames_.clear();
        frames_.reserve(max_frames_);
    }

    void clear() { frames_.clear(); }

    uint32_t push(const float* mag, const float* phase, const float* raw_phase,
                  float rms, uint32_t frame_index)
    {
        if (half_n_ == 0) return 0;

        if (frames_.size() >= max_frames_)
            frames_.erase(frames_.begin());

        SpectralFrame f;
        f.mag.resize(half_n_);
        f.phase.resize(half_n_);
        f.raw_phase.resize(half_n_);
        std::memcpy(f.mag.data(), mag, half_n_ * sizeof(float));
        std::memcpy(f.phase.data(), phase, half_n_ * sizeof(float));
        std::memcpy(f.raw_phase.data(), raw_phase, half_n_ * sizeof(float));
        f.rms = rms;
        f.index = frame_index;
        frames_.push_back(std::move(f));
        return static_cast<uint32_t>(frames_.size());
    }

    uint32_t num_frames() const {
        return static_cast<uint32_t>(frames_.size());
    }

    const SpectralFrame* frame_at(uint32_t idx) const {
        if (idx >= frames_.size()) return nullptr;
        return &frames_[idx];
    }

    void copy_frame(uint32_t idx, float* mag, float* phase, float* raw_phase) const {
        const auto* f = frame_at(idx);
        if (!f || !mag) return;
        std::memcpy(mag, f->mag.data(), half_n_ * sizeof(float));
        if (phase)
            std::memcpy(phase, f->phase.data(), half_n_ * sizeof(float));
        if (raw_phase)
            std::memcpy(raw_phase, f->raw_phase.data(), half_n_ * sizeof(float));
    }

private:
    uint32_t half_n_ = 0;
    uint32_t max_frames_ = 512;
    std::vector<SpectralFrame> frames_;
};
