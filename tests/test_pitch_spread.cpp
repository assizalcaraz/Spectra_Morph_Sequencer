#include "../src/dsp/scramble/TemporalScrambler.h"
#include "../src/dsp/tracking/PeakUtils.h"
#include <cstdio>
#include <cassert>
#include <cmath>

namespace {

float voicePitchRatio(uint64_t base_seed, uint32_t v, uint32_t frame,
                      float base_st, float spread_min, float spread_max)
{
    const float lo = std::min(spread_min, spread_max);
    const float hi = std::max(spread_min, spread_max);
    uint64_t s = TemporalScrambler::mix_seed(base_seed, v + frame * 997u);
    const float t = TemporalScrambler::unit_rand(s);
    const float semi = base_st + lo + t * (hi - lo);
    return PeakUtils::semitones_to_ratio(semi);
}

} // namespace

void test_pitch_spread() {
    printf("  pitch_spread: ");

    const float min_st = -6.0f;
    const float max_st = 6.0f;
    const float min_ratio = PeakUtils::semitones_to_ratio(min_st);
    const float max_ratio = PeakUtils::semitones_to_ratio(max_st);

    const float r0 = voicePitchRatio(42, 0, 1, 0.0f, min_st, max_st);
    const float r1 = voicePitchRatio(99, 1, 1, 0.0f, min_st, max_st);

    assert(r0 >= min_ratio * 0.999f && r0 <= max_ratio * 1.001f);
    assert(r1 >= min_ratio * 0.999f && r1 <= max_ratio * 1.001f);
    assert(std::abs(r0 - r1) > 1e-4f);

    const float same = voicePitchRatio(42, 0, 0, 0.0f, 0.0f, 0.0f);
    assert(std::abs(same - 1.0f) < 1e-4f);

    printf("OK\n");
}
