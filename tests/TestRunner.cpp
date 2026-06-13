#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    printf("SpectraMorph tests\n");
    printf("==================\n");

    // Test memory
    extern void test_memory();
    test_memory();

    // Test scheduler
    extern void test_scheduler();
    test_scheduler();

    extern void test_fft_sine();
    extern void test_harmonic_count_triangle();
    extern void test_tracking_1to1();
    extern void test_density_scales_harmonics();
    extern void test_faithful_sync();
    test_fft_sine();
    test_harmonic_count_triangle();
    test_tracking_1to1();
    test_density_scales_harmonics();
    test_faithful_sync();

    extern void test_sine_snr();
    extern void test_partial_stability();
    extern void test_silence_floor();
    extern void test_transient_detect();
    test_sine_snr();
    test_partial_stability();
    test_silence_floor();
    test_transient_detect();

    extern void test_f0_sine_440();
    extern void test_f0_sine_187();
    extern void test_f0_triangle();
    extern void test_harmonic_affinity();
    extern void test_gravity_pull();
    test_f0_sine_440();
    test_f0_sine_187();
    test_f0_triangle();
    test_harmonic_affinity();
    test_gravity_pull();

    extern void test_scrambler_identity();
    extern void test_scrambler_chaos();
    extern void test_file_segment_bounds();
    test_scrambler_identity();
    test_scrambler_chaos();
    test_file_segment_bounds();

    printf("\nAll tests passed.\n");
    return 0;
}
