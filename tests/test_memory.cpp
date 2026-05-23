#include "../src/core/types/Partial.h"
#include "../src/core/types/Peak.h"
#include "../src/core/types/Snapshot.h"
#include "../src/core/types/ExternalField.h"
#include "../src/core/memory/PartialPool.h"
#include "../src/core/memory/AdditiveBuffer.h"
#include <cstdio>
#include <cassert>

void test_memory() {
    printf("  memory: ");

    // Partial size
    assert(sizeof(Partial) == 96);
    assert(sizeof(Peak) == 16);
    assert(sizeof(ExternalField) == 16);

    // PartialPool
    PartialPool pool;
    assert(pool.num_active() == 0);
    assert(!pool.is_alive(0));

    // Allocate
    uint32_t id = pool.allocate();
    assert(id == 0);
    assert(pool.num_active() == 1);
    assert(pool.is_alive(0));

    pool[0].frequency = 440.0f;
    pool[0].amplitude = 0.5f;
    assert(pool[0].frequency == 440.0f);

    // Free
    pool.free(0);
    assert(pool.num_active() == 0);
    assert(!pool.is_alive(0));

    // Fill to capacity
    uint32_t count = 0;
    while (!pool.full()) {
        pool.allocate();
        ++count;
    }
    assert(count == MAX_PARTIALS);
    assert(pool.num_active() == MAX_PARTIALS);

    // Prune
    uint32_t killed = pool.prune_to(128);
    assert(killed == MAX_PARTIALS - 128);
    assert(pool.num_active() == 128);

    pool.clear();
    assert(pool.num_active() == 0);

    // Snapshot
    ParticleSnapshot snap;
    pool.write_snapshot(snap, 42);
    assert(snap.frame_number == 42);
    assert(snap.num_partials == 0);

    // Additive buffer
    AdditiveBuffer abuf;
    assert(abuf.num_active == 0);
    abuf.from_snapshot(snap, 48000.0f);
    assert(abuf.num_active == 0);

    printf("OK\n");
}
