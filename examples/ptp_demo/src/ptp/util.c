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

// Average Filter with smooth startup
// Calculate average over last <size> values
void average_init(filter_average_t *f, uint8_t size) {

    uint8_t i;
    if (size > FILTER_MAX_SIZE)
        size = FILTER_MAX_SIZE;
    f->size = f->am = size;
    f->ai = 0;
    f->as = 0;
    for (i = 0; i < FILTER_MAX_SIZE; i++)
        f->a[i] = 0;
}

int64_t average_calc(filter_average_t *f, int64_t v) {

    uint8_t i, n;
    for (n = 0; n < f->am; n++) {
        i = f->ai;
        f->as -= f->a[i];
        f->as += v;
        f->a[i] = v;
        if (++i >= f->size)
            i = 0;
        f->ai = i;
    }
    if (f->am > 1) {
        f->am /= 2;
    }
    return (int64_t)(f->as / f->size);
}

// Median filter
// Calculate average between current value[0] and value[-n]
void median_init(filter_median_t *f, uint8_t n, uint64_t t) {

    uint8_t i;
    if (n > FILTER_MAX_SIZE)
        n = FILTER_MAX_SIZE;
    f->n = n;
    f->ai = 0;
    for (i = 0; i < FILTER_MAX_SIZE; i++)
        f->a[i] = t;
}

uint64_t median_calc(filter_median_t *f, uint64_t v) {

    uint8_t i;

    i = f->ai;
    f->a[i] = v;
    if (++i >= f->n)
        i = 0;
    f->ai = i;
    return f->a[i] + (v - f->a[i]) / 2;
}
