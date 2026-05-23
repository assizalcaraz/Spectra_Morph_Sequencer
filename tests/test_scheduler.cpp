#include "../src/core/scheduler/Scheduler.h"
#include <cstdio>
#include <cassert>

void test_scheduler() {
    printf("  scheduler: ");

    Scheduler sched;
    sched.set_audio_params(48000.0f, 512);
    assert(sched.pressure() == PressureLevel::Nominal);
    assert(sched.max_partials() == MAX_PARTIALS);
    assert(sched.state().tracking_interval == 1);

    // Hysteresis — DSP thread pattern: begin_frame → work → end_frame → tick
    // update_pressure_with_hysteresis simulates the load measurement
    for (int i = 0; i < 30; ++i) {
        sched.begin_frame();
        sched.end_frame();
        sched.tick(static_cast<uint32_t>(i));
        sched.update_pressure_with_hysteresis(0.8f);
    }
    assert(sched.pressure() == PressureLevel::Warming);
    assert(sched.max_partials() < MAX_PARTIALS);

    // Recovery
    for (int i = 0; i < 30; ++i) {
        sched.begin_frame();
        sched.end_frame();
        sched.tick(static_cast<uint32_t>(i + 100));
        sched.update_pressure_with_hysteresis(0.1f);
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
