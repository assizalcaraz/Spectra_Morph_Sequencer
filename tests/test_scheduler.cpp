#include "../src/core/scheduler/Scheduler.h"
#include <cstdio>
#include <cassert>

void test_scheduler() {
    printf("  scheduler: ");

    Scheduler sched;
    assert(sched.pressure() == PressureLevel::Nominal);
    assert(sched.max_partials() == MAX_PARTIALS);
    assert(sched.state().tracking_interval == 1);

    // Hysteresis — need enough iterations for EMA to converge
    // With alpha=0.1, load=0.8: EMA passes 0.75 after ~20 iterations
    for (int i = 0; i < 30; ++i) {
        sched.update_pressure_with_hysteresis(0.8f);
        sched.tick(static_cast<uint32_t>(i));
    }
    assert(sched.pressure() == PressureLevel::Warming);
    assert(sched.max_partials() < MAX_PARTIALS);

    // Recovery
    for (int i = 0; i < 30; ++i) {
        sched.update_pressure_with_hysteresis(0.1f);
        sched.tick(static_cast<uint32_t>(i + 100));
    }
    assert(sched.pressure() == PressureLevel::Nominal);

    // Cadence
    assert(sched.tick(0) == true);   // frame 0 → tracking
    assert(sched.tick(1) == true);   // frame 1 → tracking (interval=1)
    assert(sched.should_track());

    sched.reset();
    assert(sched.pressure() == PressureLevel::Nominal);

    printf("OK\n");
}
