#include "../src/dsp/scramble/TemporalScrambler.h"
#include <cstdio>
#include <cassert>

void test_scrambler_identity() {
    printf("  scrambler_identity: ");
    const uint32_t mapped = TemporalScrambler::map_frame_index(
        10, 32, 1.0f, 8, 42);
    assert(mapped == 10);
    printf("OK\n");
}

void test_scrambler_chaos() {
    printf("  scrambler_chaos: ");
    uint32_t diff = 0;
    const uint32_t n = 32;
    for (uint32_t t = 0; t < n; ++t) {
        const uint32_t m = TemporalScrambler::map_frame_index(
            t, n, 0.0f, 4, 12345);
        if (m != t) ++diff;
    }
    assert(diff >= n / 3);
    printf("OK (%u/%u permuted)\n", diff, n);
}
