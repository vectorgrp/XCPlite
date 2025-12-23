#pragma once

/* util.h */
/*
| Code released into public domain, no attribution required
*/

//-------------------------------------------------------------------------------
// Fast pseudo random

extern void seed16(unsigned int seed);
extern unsigned int random16();

//-------------------------------------------------------------------------------
// Moving Average Filter

#define AVERAGE_FILTER_MAX_SIZE 120

typedef double tAverageFilterValue;

typedef struct average_filter {
    tAverageFilterValue a[AVERAGE_FILTER_MAX_SIZE]; // circular buffer for values
    tAverageFilterValue as;                         // running sum
    size_t size;                                    // filter window size (max samples)
    size_t ai;                                      // current index in circular buffer
    size_t count;                                   // current number of samples in buffer
} tAverageFilter;

extern void average_filter_init(tAverageFilter *f, size_t size);
extern tAverageFilterValue average_filter_calc(tAverageFilter *f, tAverageFilterValue v);
extern size_t average_filter_count(tAverageFilter *f);
extern void average_filter_add(tAverageFilter *f, tAverageFilterValue offset);
