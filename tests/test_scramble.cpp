#include "../src/dsp/scramble/TemporalScrambler.h"
#include "../src/dsp/scramble/SpectralFrameStore.h"
#include <cstdio>
#include <cassert>
#include <vector>

void test_scrambler_identity() {
    printf("  scrambler_identity: ");
    const uint32_t mapped = TemporalScrambler::map_frame_index(
        10, 32, 0.0f, 8, 42);
    assert(mapped == 10);
    printf("OK\n");
}

void test_scrambler_chaos() {
    printf("  scrambler_chaos: ");
    uint32_t diff = 0;
    const uint32_t n = 32;
    for (uint32_t t = 0; t < n; ++t) {
        const uint32_t m = TemporalScrambler::map_frame_index(
            t, n, 1.0f, 4, 12345);
        if (m != t) ++diff;
    }
    assert(diff >= n / 3);
    printf("OK (%u/%u permuted)\n", diff, n);
}

void test_scrambler_voice_count() {
    printf("  scrambler_voice_count: ");
    assert(TemporalScrambler::voice_count(2, 0.5f) == 2);
    assert(TemporalScrambler::voice_count(8, 0.5f) == 8);
    assert(TemporalScrambler::voice_count(4, 0.0f) == 1);
    printf("OK\n");
}

void test_scrambler_voice_mix() {
    printf("  scrambler_voice_mix: ");
    SpectralFrameStore store;
    store.prepare(8, 16);

    const uint32_t half = 9;
    std::vector<float> mag(half), phase(half), raw(half);
    for (uint32_t f = 0; f < 8; ++f) {
        for (uint32_t k = 0; k < half; ++k) {
            mag[k]  = static_cast<float>(k + 1);
            phase[k] = static_cast<float>(f) * 0.1f;
            raw[k]   = phase[k];
        }
        store.push(mag.data(), phase.data(), raw.data(), 0.1f, f);
    }

    std::vector<float> out_mag(half), out_phase(half), out_raw(half);
    std::vector<float> vm(half), vp(half), vr(half), re(half), im(half);

    TemporalScrambler::mix_scrambled_voices(
        store, 7, 0.8f, 4, 99, 0.0f, 4,
        out_mag.data(), out_phase.data(), out_raw.data(), half,
        vm.data(), vp.data(), vr.data(), re.data(), im.data());

    float sum = 0.0f;
    for (uint32_t k = 0; k < half; ++k)
        sum += out_mag[k];
    assert(sum > 0.0f);
    printf("OK\n");
}
