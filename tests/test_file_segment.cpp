#include "../src/plugin/FileSourceManager.h"
#include <cstdio>
#include <cassert>
#include <cmath>

void test_file_segment_bounds() {
    printf("  file_segment_bounds: ");
    FileSourceManager src;
    src.setSegmentNormalized(0.2f, 0.8f);
    assert(src.segmentStartNorm() >= 0.19f && src.segmentStartNorm() <= 0.21f);
    assert(src.segmentEndNorm() >= 0.79f && src.segmentEndNorm() <= 0.81f);

    src.setSegmentNormalized(0.9f, 0.1f);
    assert(src.segmentEndNorm() > src.segmentStartNorm());
    printf("OK\n");
}
