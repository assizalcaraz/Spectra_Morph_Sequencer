#include "../src/plugin/TempoUtils.h"
#include <cstdio>
#include <cassert>
#include <vector>

void test_tempo_utils_quarter() {
    assert(TempoUtils::quarterNoteSamples(120.0f, 48000.0) == 6000);
    assert(TempoUtils::quarterNoteSamples(60.0f, 44100.0) == 11025);

    const std::vector<double> intervals { 0.5, 0.5 };
    const float bpm = TempoUtils::bpmFromTapIntervals(intervals);
    assert(bpm >= 119.0f && bpm <= 121.0f);

    float s0 = 0.1f;
    float s1 = 0.9f;
    TempoUtils::snapSegmentToQuarterNote(120.0f, 48000.0, s0, s1);
    assert(s1 > s0);
    assert(s1 - s0 > 0.0001f);
}

void test_tempo_utils() {
    printf("  tempo_utils: ");
    test_tempo_utils_quarter();
    printf("OK\n");
}
