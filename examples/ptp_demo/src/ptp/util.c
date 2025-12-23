/*----------------------------------------------------------------------------
| File:
|   util.c
|
| Description:
|   Some helper functions
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

#include "util.h"

/**************************************************************************/
// Pseudo random
/**************************************************************************/

// Pseudo random unsigned int 0-15
static unsigned int r = 0;

void seed16(unsigned int seed) { r = seed; }

unsigned int random16() {
    r = 36969 * (r & 65535) + (r >> 16);
    return r & 0xF;
}

/**************************************************************************/
// Filter
/**************************************************************************/

// Moving Average Filter
// Calculate average over last <size> values
void average_filter_init(tAverageFilter *f, size_t size) {

    if (size > AVERAGE_FILTER_MAX_SIZE) {
        size = AVERAGE_FILTER_MAX_SIZE;
        printf("WARNING: average_filter_init: size %zu too large, limiting to %d\n", size, AVERAGE_FILTER_MAX_SIZE);
    }
    f->size = size;
    f->ai = 0;
    f->as = 0;
    f->count = 0;
    for (size_t i = 0; i < AVERAGE_FILTER_MAX_SIZE; i++)
        f->a[i] = 0;
}

size_t average_filter_count(tAverageFilter *f) { return f->count; }

tAverageFilterValue average_filter_calc(tAverageFilter *f, tAverageFilterValue v) {

    // Subtract the oldest value from sum (only if buffer is full)
    if (f->count == f->size) {
        f->as -= f->a[f->ai];
    } else {
        f->count++;
    }

    // Add new value to buffer and sum
    f->a[f->ai] = v;
    f->as += v;

    // Advance circular buffer index
    if (++f->ai >= f->size)
        f->ai = 0;

    // Return average (sum divided by actual count)
    return f->as / f->count;
}

// Add an offset correction to the current filter state
void average_filter_add(tAverageFilter *f, tAverageFilterValue offset) {

    for (size_t i = 0; i < f->count; i++) {
        f->a[i] += offset;
    }
    f->as += offset * f->count;
}
