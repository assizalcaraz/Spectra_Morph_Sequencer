#pragma once

#include "FileSourceManager.h"
#include <algorithm>
#include <cstdint>

enum class PatternPlayMode : int {
    Linear = 0,
    // Random = 1,  // phase 2
    // Simon  = 2,  // phase 2
};

inline constexpr uint32_t kMaxPatternSteps = 32;

namespace WindowPattern {

inline uint32_t stepCount(juce::int64 total_samples, juce::int64 window_len,
                          uint32_t max_steps = kMaxPatternSteps)
{
    if (total_samples <= 0 || window_len <= 0)
        return 1u;
    const juce::int64 n = std::max(juce::int64{1}, total_samples / window_len);
    return static_cast<uint32_t>(std::clamp<juce::int64>(n, 1, max_steps));
}

inline bool isStepActive(uint32_t step, uint32_t mask, uint32_t step_count)
{
    if (step >= step_count)
        return false;
    return (mask & (1u << step)) != 0u;
}

inline uint32_t activeStepCount(uint32_t mask, uint32_t step_count)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < step_count; ++i)
        if (isStepActive(i, mask, step_count))
            ++n;
    return n;
}

inline int nextActiveStep(int current, uint32_t mask, uint32_t step_count)
{
    if (step_count == 0)
        return 0;
    current = ((current % static_cast<int>(step_count)) + static_cast<int>(step_count))
            % static_cast<int>(step_count);
    for (uint32_t k = 1; k <= step_count; ++k) {
        const int idx = (current + static_cast<int>(k)) % static_cast<int>(step_count);
        if (isStepActive(static_cast<uint32_t>(idx), mask, step_count))
            return idx;
    }
    return current;
}

inline int prevActiveStep(int current, uint32_t mask, uint32_t step_count)
{
    if (step_count == 0)
        return 0;
    current = ((current % static_cast<int>(step_count)) + static_cast<int>(step_count))
            % static_cast<int>(step_count);
    for (uint32_t k = 1; k <= step_count; ++k) {
        const int idx = (current - static_cast<int>(k) + static_cast<int>(step_count) * 2)
                      % static_cast<int>(step_count);
        if (isStepActive(static_cast<uint32_t>(idx), mask, step_count))
            return idx;
    }
    return current;
}

inline int currentWindowIndex(const FileSourceManager& src)
{
    const juce::int64 len = src.segmentLengthSamples();
    if (len <= 0)
        return 0;
    return static_cast<int>(src.segmentStartSample() / len);
}

inline bool setSegmentByWindowIndex(FileSourceManager& src, int index)
{
    if (!src.hasFile() || index < 0)
        return false;
    const juce::int64 n = src.totalSamples();
    const juce::int64 len = src.segmentLengthSamples();
    if (len <= 0 || len >= n)
        return false;

    const juce::int64 ns = static_cast<juce::int64>(index) * len;
    if (ns >= n)
        return false;

    juce::int64 ne = ns + len;
    if (ne > n) {
        ne = n;
        if (ne <= ns)
            return false;
    }

    const juce::int64 max_start = std::max(juce::int64{0}, n - 1);
    const float s0 = (max_start > 0)
        ? static_cast<float>(ns) / static_cast<float>(max_start) : 0.0f;
    const float s1 = static_cast<float>(ne) / static_cast<float>(n);
    src.setSegmentNormalized(s0, s1);
    return true;
}

} // namespace WindowPattern
