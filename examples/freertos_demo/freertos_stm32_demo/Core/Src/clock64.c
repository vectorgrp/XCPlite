// clock54.c - 64-bit microsecond-resolution clock using DWT cycle counter.

#include "clock64.h"
#include "stm32h7xx_hal.h"

#ifdef CONVERT_TO_US
// Ticks per microsecond — requires CLOCK_TICKS_PER_S to be an integer multiple of 1 MHz
#define TICKS_PER_US (CLOCK_TICKS_PER_S / 1000000UL)
// Microseconds per full 32-bit DWT overflow (compile-time constant)
// Truncation error: (2^32 % TICKS_PER_US) / TICKS_PER_US us per overflow -> < 0.2 ppm cumulative drift
#define OVERFLOW_US  (4294967296ULL / TICKS_PER_US)
#endif


// Written only by Clock64_Update() (single writer). Read by Clock64_Get()
static volatile uint32_t Clock64_HighWord = 0U;

// Written only by Clock64_Update(), but also read by Clock64_Get() for overflow-pending detection
static volatile uint32_t Clock64_PrevLow = 0U;

void Clock64_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
    Clock64_Update();
}

// Call from ONE dedicated context (one task or one timer callback)
// Must be called at least once per DWT rollover period (~8.9 s at 480 MHz)
void Clock64_Update(void)
{
    uint32_t lo = DWT->CYCCNT;
    if (lo < Clock64_PrevLow)
    {
        Clock64_HighWord++;
    }
    Clock64_PrevLow = lo;
}

// Lock-free read, safe from any task or ISR
/*
 * Double-read pattern: re-sample HighWord after reading CYCCNT. If it changed,
 * a rollover fired between the two reads — retry. In practice this loop runs
 * exactly once: a retry can only occur once per ~8.9 s.
 *
 * Additionally, if lo < prev, the DWT has already wrapped but Clock64_Update
 * has not yet been called — compensate by incrementing hi locally.
 *
 * With CONVERT_TO_US: converts to microseconds using compile-time OVERFLOW_US constant.
 * Rounding error is below 0.2 ppm, well within typical oscillator tolerance.
 */
uint64_t Clock64_Get(void)
{
    uint32_t hi, lo, prev;
    do
    {
        hi   = Clock64_HighWord;
        lo   = DWT->CYCCNT;
        prev = Clock64_PrevLow;
    } while (hi != Clock64_HighWord);

    /* Pending overflow: DWT wrapped but Clock64_Update hasn't run yet. */
    if (lo < prev)
    {
        hi++;
    }

#ifdef CONVERT_TO_US
    return (uint64_t)hi * OVERFLOW_US + lo / TICKS_PER_US;
#else
    return ((uint64_t)hi << 32) | lo;
#endif
}


