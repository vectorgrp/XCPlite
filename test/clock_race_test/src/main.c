// clock_race_test - concurrent clockGet() calls must not return another thread's timestamp
//
// Several threads call clockGet() and clockGetMonotonicNs() concurrently for about 2 s. Each thread checks that its own sequence
// of values never goes backwards. clockGet() is called for every DAQ event and twice per span, from all instrumented threads.
// A clock implementation that passes its result through shared state (as the POSIX clock did in V2.0.1 to V2.4.2 with a static
// struct timespec) returns timestamps of other threads, which are not monotonic for the calling thread.
//
// Only checks clockGet() for monotonicity when the clock epoch is arbitrary (OPTION_CLOCK_EPOCH_ARB, monotonic clock source).
// With OPTION_CLOCK_EPOCH_PTP, the clock is CLOCK_REALTIME, which may legitimately be stepped back by NTP or PTP.
//
// Run: ctest --test-dir build -R '^clock_race_test$' -V
//      ./build/clock_race_test [seconds]

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../support/check.h"
#include "platform.h" // for clockGet, clockGetMonotonicNs, create_thread, join_thread, sleepMs, atomics

#define THREAD_COUNT 8
#define DEFAULT_DURATION_MS 2000

typedef struct {
    uint32_t index;
    uint64_t calls;
    uint64_t clock_backwards;
    uint64_t clock_max_back;
    uint64_t mono_backwards;
    uint64_t mono_max_back;
} tWorkerResult;

static ATOMIC_BOOL gStop;
static tWorkerResult gResults[THREAD_COUNT];

static inline void check_step(uint64_t last, uint64_t t, uint64_t *backwards, uint64_t *max_back) {
    if (t < last) {
        (*backwards)++;
        if (last - t > *max_back)
            *max_back = last - t;
    }
}

static THREAD_FUNC_RETURN worker(void *p) {
    tWorkerResult *r = (tWorkerResult *)p;
    uint64_t last_clock = clockGet();
    uint64_t last_mono = clockGetMonotonicNs();
    while (!atomic_load_explicit(&gStop, memory_order_relaxed)) {
        uint64_t t = clockGet();
#ifdef OPTION_CLOCK_EPOCH_ARB
        check_step(last_clock, t, &r->clock_backwards, &r->clock_max_back);
#endif
        last_clock = t;
        t = clockGetMonotonicNs();
        check_step(last_mono, t, &r->mono_backwards, &r->mono_max_back);
        last_mono = t;
        r->calls++;
    }
    THREAD_FUNC_END;
}

int main(int argc, char **argv) {
    uint32_t duration_ms = DEFAULT_DURATION_MS;
    if (argc > 1)
        duration_ms = (uint32_t)(atof(argv[1]) * 1000.0);

    CHECK(clockInit(), "clockInit failed");

    printf("clock_race_test: %u threads, %u ms\n", THREAD_COUNT, duration_ms);
    atomic_store_explicit(&gStop, false, memory_order_relaxed);
    THREAD_HANDLE threads[THREAD_COUNT];
    for (uint32_t i = 0; i < THREAD_COUNT; i++) {
        gResults[i].index = i;
        CHECK(create_thread(&threads[i], NULL, worker, &gResults[i]) == 0, "create_thread failed");
    }

    sleepMs(duration_ms);
    atomic_store_explicit(&gStop, true, memory_order_relaxed);
    for (uint32_t i = 0; i < THREAD_COUNT; i++) {
        join_thread(threads[i]);
    }

    uint64_t clock_backwards = 0, mono_backwards = 0;
    for (uint32_t i = 0; i < THREAD_COUNT; i++) {
        const tWorkerResult *r = &gResults[i];
        printf("  thread %u: %" PRIu64 " calls, clockGet %" PRIu64 " backwards (max %" PRIu64 " ticks), clockGetMonotonicNs %" PRIu64 " backwards (max %" PRIu64 " ns)\n", r->index,
               r->calls, r->clock_backwards, r->clock_max_back, r->mono_backwards, r->mono_max_back);
        CHECK(r->calls > 0, "thread %u did not run", r->index);
        clock_backwards += r->clock_backwards;
        mono_backwards += r->mono_backwards;
    }

    CHECK(clock_backwards == 0, "clockGet() went backwards %" PRIu64 " times within a thread", clock_backwards);
    CHECK(mono_backwards == 0, "clockGetMonotonicNs() went backwards %" PRIu64 " times within a thread", mono_backwards);

    printf("PASSED\n");
    return 0;
}
