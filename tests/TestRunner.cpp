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
    extern void test_faithful_sync();
    test_fft_sine();
    test_harmonic_count_triangle();
    test_tracking_1to1();
    test_faithful_sync();

    printf("\nAll tests passed.\n");
    return 0;
}
