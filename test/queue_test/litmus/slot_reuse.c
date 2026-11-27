// Litmus test for the slot reuse ordering of queue64f.c ("S" shape), see docs/QUEUE.md chapter 10.1
//
// Not part of the build. Two threads run the instruction sequences of the consumer (queueRelease)
// and of a producer (queueAcquire, queuePush) on the same slot, on many independent slots per round:
//
//   T0 (consumer):   A: store entry_header = 2 (reset)    B: tail += 1
//   T1 (producer):   C: r = load tail    CAS head    D: store entry_header = 1 (reserved)
//                    [write payload      E: store entry_header = 3 (committed)]
//
// Forbidden outcome, if the reset of a slot is ordered before its reuse:
//   r == 1 (T1 has seen the tail update B) and final entry_header == 2 (the reset A arrived last)
//
// Build and run:
//   cc  -O2 -std=c11 -o slot_reuse slot_reuse.c                                         (macOS, LSE atomics)
//   gcc -O2 -std=gnu11 -pthread -march=armv8.2-a -o slot_reuse slot_reuse.c             (Linux, LSE atomics)
//   gcc -O2 -std=gnu11 -pthread -march=armv8-a -mno-outline-atomics -o slot_reuse slot_reuse.c   (Linux, LL/SC atomics)
//   ./slot_reuse <variant> [seconds] [same_order: 1|0]
//
// Variants (A, B | C, D):
//    0  positive control: plain store, plain store | plain load, plain store
//    1  removed queue64f.c scheme: release store, relaxed fetch_add | relaxed load, CAS, release store
//    2  queue64f.c:                relaxed store, release fetch_add | acquire load, CAS, release store
//    3  release store, plain store    | as 1
//    4  plain store, relaxed fetch_add | as 1
//    5  as 1 | as 0
//    6  release store, plain store    | as 0
//    7  plain store, relaxed fetch_add | as 0
//    8  as 1, producer writes 24 bytes payload and commits (E)
//    9  as 1, producer writes 248 bytes payload and commits (E)
//   10  as 2, producer writes 24 bytes payload and commits (E)
//   11  as 2, producer writes 248 bytes payload and commits (E)

#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 2048
#define LINE 256 // slot size, multiple of the cache line (64 or 128)

typedef struct {
    _Alignas(LINE) atomic_uint_least32_t x; // entry_header
    uint8_t pay[252];                       // slot payload, directly behind the header
    _Alignas(LINE) atomic_uint_fast64_t y;  // tail
    _Alignas(LINE) atomic_uint_fast64_t h;  // head
    _Alignas(LINE) uint64_t r;              // T1 result
} cell_t;

static cell_t cells[N];
static int variant;
static _Alignas(LINE) atomic_uint round_no;
static _Alignas(LINE) atomic_uint done0;
static _Alignas(LINE) atomic_uint done1;
static atomic_bool stop;
static unsigned perm0[N], perm1[N];

static inline uint64_t rnd(uint64_t *s) {
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

static void reset_cells(void) {
    for (int i = 0; i < N; i++) {
        atomic_store_explicit(&cells[i].x, 0, memory_order_relaxed);
        atomic_store_explicit(&cells[i].y, 0, memory_order_relaxed);
        atomic_store_explicit(&cells[i].h, 0, memory_order_relaxed);
        cells[i].r = 0;
    }
}

__attribute__((noinline)) static void t0_body(int v, cell_t *c) {
    switch (v) {
    case 0:
        atomic_store_explicit(&c->x, 2, memory_order_relaxed);
        atomic_store_explicit(&c->y, 1, memory_order_relaxed);
        break;
    case 1:
        atomic_store_explicit(&c->x, 2, memory_order_release);
        atomic_fetch_add_explicit(&c->y, 1, memory_order_relaxed);
        break;
    case 3:
    case 6:
        atomic_store_explicit(&c->x, 2, memory_order_release);
        atomic_store_explicit(&c->y, 1, memory_order_relaxed);
        break;
    case 7:
    case 4:
        atomic_store_explicit(&c->x, 2, memory_order_relaxed);
        atomic_fetch_add_explicit(&c->y, 1, memory_order_relaxed);
        break;
    case 5:
    case 8:
    case 9:
        atomic_store_explicit(&c->x, 2, memory_order_release);
        atomic_fetch_add_explicit(&c->y, 1, memory_order_relaxed);
        break;
    default:
        atomic_store_explicit(&c->x, 2, memory_order_relaxed);
        atomic_fetch_add_explicit(&c->y, 1, memory_order_release);
        break;
    }
}

__attribute__((noinline)) static void t1_body(int v, cell_t *c) {
    uint64_t r, head = 0;
    switch (v) {
    case 0:
    case 5:
    case 6:
    case 7:
        r = atomic_load_explicit(&c->y, memory_order_relaxed);
        atomic_store_explicit(&c->x, 1, memory_order_relaxed);
        break;
    case 1:
    case 3:
    case 4:
        r = atomic_load_explicit(&c->y, memory_order_relaxed);
        atomic_compare_exchange_weak_explicit(&c->h, &head, 256, memory_order_relaxed, memory_order_relaxed);
        atomic_store_explicit(&c->x, 1, memory_order_release);
        break;
    case 8:
    case 9:
    case 10:
    case 11: {
        size_t n = (v == 8 || v == 10) ? 24 : 248;
        r = atomic_load_explicit(&c->y, (v >= 10) ? memory_order_acquire : memory_order_relaxed);
        atomic_compare_exchange_weak_explicit(&c->h, &head, 256, memory_order_relaxed, memory_order_relaxed);
        atomic_store_explicit(&c->x, 1, memory_order_release); // reserved
        memset(c->pay + 4, (int)(r + 0x55), n);                // payload behind the 4 byte transport layer header
        atomic_store_explicit(&c->x, 3, memory_order_release); // committed
    } break;
    default:
        r = atomic_load_explicit(&c->y, memory_order_acquire);
        atomic_compare_exchange_weak_explicit(&c->h, &head, 256, memory_order_relaxed, memory_order_relaxed);
        atomic_store_explicit(&c->x, 1, memory_order_release);
        break;
    }
    c->r = r;
}

static void *thread_fn(void *arg) {
    int id = (int)(intptr_t)arg;
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    uint64_t seed = 0x9E3779B97F4A7C15ULL * (uint64_t)(id + 1);
    unsigned seen = 0;
    for (;;) {
        unsigned r;
        while ((r = atomic_load_explicit(&round_no, memory_order_acquire)) == seen) {
            if (atomic_load_explicit(&stop, memory_order_relaxed))
                return NULL;
        }
        seen = r;
        // Random phase shift between the two threads
        for (volatile unsigned k = (unsigned)(rnd(&seed) & 0x3F); k > 0; k--) {
        }
        const unsigned *perm = id == 0 ? perm0 : perm1;
        if (id == 0) {
            for (int i = 0; i < N; i++)
                t0_body(variant, &cells[perm[i]]);
            atomic_store(&done0, seen);
        } else {
            for (int i = 0; i < N; i++)
                t1_body(variant, &cells[perm[i]]);
            atomic_store(&done1, seen);
        }
    }
}

int main(int argc, char **argv) {
    variant = argc > 1 ? atoi(argv[1]) : 1;
    double seconds = argc > 2 ? atof(argv[2]) : 10.0;
    int same_order = argc > 3 ? atoi(argv[3]) : 1;

    uint64_t seed = 12345;
    for (unsigned i = 0; i < N; i++)
        perm0[i] = perm1[i] = i;
    if (!same_order) { // T1 walks the cells in a different order than T0
        for (unsigned i = N - 1; i > 0; i--) {
            unsigned j = (unsigned)(rnd(&seed) % (i + 1));
            unsigned t = perm1[i];
            perm1[i] = perm1[j];
            perm1[j] = t;
        }
    }

    pthread_t t0, t1;
    reset_cells();
    pthread_create(&t0, NULL, thread_fn, (void *)(intptr_t)0);
    pthread_create(&t1, NULL, thread_fn, (void *)(intptr_t)1);

    uint64_t total = 0, saw_tail = 0, bad = 0, final2 = 0;
    struct timespec ts0, ts;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    for (unsigned round = 1;; round++) {
        atomic_store(&round_no, round);
        while (atomic_load(&done0) != round || atomic_load(&done1) != round) {
        }
        for (int i = 0; i < N; i++) {
            uint32_t x = atomic_load(&cells[i].x);
            uint64_t r = cells[i].r;
            total++;
            if (r == 1)
                saw_tail++;
            if (x == 2)
                final2++;
            if (r == 1 && x == 2)
                bad++;
        }
        reset_cells();
        if ((round & 0xFF) == 0) {
            clock_gettime(CLOCK_MONOTONIC, &ts);
            if ((double)(ts.tv_sec - ts0.tv_sec) + 1e-9 * (double)(ts.tv_nsec - ts0.tv_nsec) > seconds)
                break;
        }
    }
    atomic_store(&stop, true);
    pthread_join(t0, NULL);
    pthread_join(t1, NULL);

    printf("variant %d order=%s: cells=%" PRIu64 "  T1 saw tail=%" PRIu64 "  final x==2: %" PRIu64 "  FORBIDDEN (saw tail, x==2): %" PRIu64 "\n", variant,
           same_order ? "same" : "shuffled", total, saw_tail, final2, bad);
    return 0;
}
