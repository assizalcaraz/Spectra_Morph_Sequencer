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

    printf("\nAll tests passed.\n");
    return 0;
}
