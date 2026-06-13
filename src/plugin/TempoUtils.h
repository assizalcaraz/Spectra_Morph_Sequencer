#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace TempoUtils {

inline int64_t quarterNoteSamples(float bpm, double sample_rate)
{
    bpm = std::clamp(bpm, 40.0f, 240.0f);
    const double sec = (60.0 / static_cast<double>(bpm)) * 0.25;
    return static_cast<int64_t>(std::lround(sec * sample_rate));
}

inline float bpmFromTapIntervals(const std::vector<double>& intervals_sec)
{
    if (intervals_sec.empty())
        return 120.0f;

    std::vector<double> sorted = intervals_sec;
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted[sorted.size() / 2];
    if (median <= 1e-6)
        return 120.0f;

    const float bpm = static_cast<float>(60.0 / median);
    return std::clamp(bpm, 40.0f, 240.0f);
}

inline void snapSegmentToQuarterNote(float bpm, double sample_rate,
                                     float& seg_start, float& seg_end)
{
    const int64_t quarter = quarterNoteSamples(bpm, sample_rate);
    if (quarter <= 0)
        return;

    const float len_norm = static_cast<float>(quarter) / static_cast<float>(sample_rate);
    seg_end = std::min(1.0f, seg_start + len_norm);
    if (seg_end <= seg_start + 0.001f)
        seg_end = std::min(1.0f, seg_start + 0.001f);
}

} // namespace TempoUtils
