#pragma once

/* filter.h */

/*
 * Simple floating average and linear regression sequential filters
 */

#ifdef __cplusplus
extern "C" {
#endif

//-------------------------------------------------------------------------------------
// Average filter

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
extern size_t average_filter_size(tAverageFilter *f);
extern size_t average_filter_count(tAverageFilter *f);
extern void average_filter_add(tAverageFilter *f, tAverageFilterValue offset);

//-------------------------------------------------------------------------------------
// Linreg filter

#define LINREG_FILTER_MAX_SIZE 120

typedef struct linreg_filter {
    double x[LINREG_FILTER_MAX_SIZE]; // circular buffer for x values
    double y[LINREG_FILTER_MAX_SIZE]; // circular buffer for y values
    size_t size;                      // filter window size (max samples)
    size_t ai;                        // current index in circular buffer
    size_t count;                     // current number of samples in buffer
} tLinregFilter;

extern void linreg_filter_init(tLinregFilter *f, size_t size);
// slope_out is the calculated slope
// y_out is the interpolated y value at x (not the intercept!)
extern bool linreg_filter_calc(tLinregFilter *f, double x, double y, double *slope_out, double *y_out);
extern size_t linreg_filter_size(tLinregFilter *f);
extern size_t linreg_filter_count(tLinregFilter *f);

#ifdef __cplusplus
}
#endif