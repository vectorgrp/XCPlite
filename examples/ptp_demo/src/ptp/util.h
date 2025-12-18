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

#define FILTER_MAX_SIZE 40

typedef struct average_filter {
    int64_t a[FILTER_MAX_SIZE]; // circular buffer for values
    int64_t as;                 // running sum
    uint8_t size;               // filter window size (max samples)
    uint8_t ai;                 // current index in circular buffer
    uint8_t count;              // current number of samples in buffer
} filter_average_t;

extern void average_init(filter_average_t *f, uint8_t size);
extern int64_t average_calc(filter_average_t *f, int64_t v);
extern void average_add(filter_average_t *f, int64_t offset);

//-------------------------------------------------------------------------------
// Median Filter

typedef struct median_filter {
    uint64_t a[FILTER_MAX_SIZE];
    uint8_t ai;
    uint8_t n;
} filter_median_t;

extern void median_init(filter_median_t *f, uint8_t n, uint64_t t);
extern uint64_t median_calc(filter_median_t *f, uint64_t v);
