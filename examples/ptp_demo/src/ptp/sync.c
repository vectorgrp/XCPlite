/*----------------------------------------------------------------------------
| File:
|   sync.c
|
| Description:
|     Clock synchronisation
|     Conversion of 2 clocks with drift and offset
|     Drift and offset may be readjusted, conversion function assures monotony
|
|   Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "platform.h"

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

#include "sync.h"
#include "util.h"

#ifdef _WIN64

#pragma pack(push, 1)
typedef union {
    uint64_t uint64[2];
    uint32_t uint32[4];
    uint16_t uint16[8];
} uint128_t;
#pragma pack(pop)

#include <intrin.h>
#pragma intrinsic(_umul128)
// (a.low + 2 * *64 * a.high)* (b.low + 2 * *64 * b.high) = a.low * b.low + 2 * *64 * (a.high * b.low + a.low * b.high) + 2 * *128 * a.high * b.high

static uint128_t umul128(uint64_t a, uint64_t b) {

    uint128_t r;

    r.uint64[0] = _mul128(a, b, (LONG64 *)&r.uint64[1]);
    return r;
}

// Init calculation state
// clockdrift in ns per s
void syncInit(sync_state_t *s, uint64_t clock2, int32_t clock2Drift, uint64_t clock1) {

    int32_t d = clock2Drift;
    if (d < 0)
        d = -d;
    uint64_t f = (((uint64_t)d) << 32) / 1000000000ull;
    if ((f & 0xFFFFFFFF00000000) != 0) {
        printf("ERROR: drift out of limits\n");
        return;
    }

    s->clock1Ref = clock1;
    s->clock2Ref = clock2;
    s->clock2Drift = clock2Drift;
    s->clock2DriftFract = f & 0xFFFFFFFF;

    if (gPtpDebugLevel >= 3) {
        printf("Init clock transformation:\n");
        printf("  clock2 = %" PRIu64 " clock1 = %" PRIu64 " diff = %" PRIi64 "\n", clock2, clock1, (int64_t)clock2 - (int64_t)clock1);
        printf("  drift of clock2 is %d ns per s (%g ppm)\n", s->clock2Drift, (double)s->clock2Drift / 1E3);
        printf("  32 bit fraction increment of clock2 per ns is %u >> 32 (%g ns)\n", s->clock2DriftFract, (double)s->clock2Drift / 1E9);
    }
    // Test
    uint64_t a, b;
    uint128_t r;
    a = 0xFFFFFFFF;
    b = 0xFFFFFFFF;
    r = umul128(a, b);
    assert(r.uint64[0] == 0xFFFFFFFF00000000 - 0xFFFFFFFF);
}

// Calculate clock2 from clock1 assuming constant drift
uint64_t syncGetClock(sync_state_t *s, uint64_t clock1) {

    uint64_t clock1_diff = clock1 - s->clock1Ref;

    // Calculate clock difference by drift since clock1Ref
    uint64_t clock2 = s->clock2Ref + clock1_diff;

    if (s->clock2Drift != 0) {
        uint128_t d;
        d.uint32[0] = 0;
        d.uint32[1] = clock1_diff & 0xFFFFFFFF;
        d.uint32[2] = clock1_diff >> 32;
        d.uint32[3] = 0;
        uint128_t f;
        f.uint32[0] = s->clock2DriftFract;
        f.uint32[1] = 0;
        f.uint32[2] = 0;
        f.uint32[3] = 0;
        uint128_t r = umul128(*(uint64_t *)&d.uint32[1], f.uint64[0]);
        if (s->clock2Drift > 0) {
            clock2 += *(uint64_t *)&r.uint32[1];
        } else {
            clock2 -= *(uint64_t *)&r.uint32[1];
        }
    }
    // Rounding error is r.uint32[0]/0x100000000 ns
    return clock2;
}

// Update to new parameters (clock pair and drift)
// Assures monotonic behaviour of syncGetClock()
// clock2Drift in ns per s
void syncUpdate(sync_state_t *s, uint64_t clock2, int32_t clock2Drift, uint64_t clock1) {

    int32_t d = clock2Drift;
    if (d < 0)
        d = -d;
    uint64_t f = (((uint64_t)d) << 32) / 1000000000ull;
    if ((f & 0xFFFFFFFF00000000) != 0) {
        printf("ERROR: drift out of limits\n");
        return;
    }

    uint64_t t1 = syncGetClock(s, clock1); // original value

    s->clock1Ref = clock1;
    s->clock2Ref = clock2;
    s->clock2Drift = clock2Drift;
    s->clock2DriftFract = f & 0xFFFFFFFF;

    // Test
    uint64_t t2 = syncGetClock(s, clock1); // new value
    if (t1 != t2) {                        // Error correction needed for monotony
        assert(0);
    }
}

#endif
