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
// Floating Average Filter with smooth startup

#define FILTER_MAX_SIZE 40

typedef struct average_filter {
    int64_t a[FILTER_MAX_SIZE];
    int64_t as;
    uint8_t ai;
    uint8_t size;
    uint8_t am;
} filter_average_t;

extern void average_init(filter_average_t *f, uint8_t size);
extern int64_t average_calc(filter_average_t *f, int64_t v);

typedef struct median_filter {
    uint64_t a[FILTER_MAX_SIZE];
    uint8_t ai;
    uint8_t n;
} filter_median_t;

extern void median_init(filter_median_t *f, uint8_t n, uint64_t t);
extern uint64_t median_calc(filter_median_t *f, uint64_t v);
