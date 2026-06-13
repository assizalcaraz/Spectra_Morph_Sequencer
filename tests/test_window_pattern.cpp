#include "../src/plugin/WindowPatternController.h"
#include "../src/plugin/FileSourceManager.h"
#include <cstdio>
#include <cassert>
#include <vector>

void test_window_pattern() {
    printf("  window_pattern: ");

    assert(WindowPattern::stepCount(48000, 6000) == 8u);
    assert(WindowPattern::stepCount(48000, 48000) == 1u);
    assert(WindowPattern::stepCount(0, 100) == 1u);
    assert(WindowPattern::stepCount(100000, 1000, 32) == 32u);

    const uint32_t mask = 0b10101u;
    assert(WindowPattern::activeStepCount(mask, 5) == 3u);
    assert(WindowPattern::nextActiveStep(0, mask, 5) == 2);
    assert(WindowPattern::nextActiveStep(2, mask, 5) == 4);
    assert(WindowPattern::nextActiveStep(4, mask, 5) == 0);
    assert(WindowPattern::prevActiveStep(0, mask, 5) == 4);

    const uint32_t all_off = 0u;
    assert(WindowPattern::activeStepCount(all_off, 4) == 0u);
    assert(WindowPattern::nextActiveStep(1, all_off, 4) == 1);

    FileSourceManager src;
    assert(!WindowPattern::setSegmentByWindowIndex(src, 0));

    printf("OK\n");
}
