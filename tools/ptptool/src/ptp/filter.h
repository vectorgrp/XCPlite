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

void average_filter_init(tAverageFilter *f, size_t size);
tAverageFilterValue average_filter_calc(tAverageFilter *f, tAverageFilterValue v);
size_t average_filter_size(tAverageFilter *f);
size_t average_filter_count(tAverageFilter *f);
void average_filter_add(tAverageFilter *f, tAverageFilterValue offset);

//-------------------------------------------------------------------------------------
// Linreg filter

#define LINREG_FILTER_MAX_SIZE 120

typedef struct linreg_filter {
    double x[LINREG_FILTER_MAX_SIZE]; // circular buffer for x values
    double y[LINREG_FILTER_MAX_SIZE]; // circular buffer for y values
    size_t size;                      // filter window size (max samples)
    size_t ai;                        // current index in circular buffer
    size_t count;                     // current number of samples in buffer

    // State variables for interpolation
    double y_out; // last calculated y output value
    double slope; // last calculated slope
} tLinregFilter;

void linreg_filter_init(tLinregFilter *f, size_t size);
// slope_out is the calculated slope
// y_out is the interpolated y value at x (not the intercept!)
bool linreg_filter_calc(tLinregFilter *f, double x, double y, double *slope_out, double *y_out);
bool linreg_filter_compare(tLinregFilter *f1, tLinregFilter *f2, double x, double *slope_diff, double *y_diff);
size_t linreg_filter_size(tLinregFilter *f);
size_t linreg_filter_count(tLinregFilter *f);

#ifdef __cplusplus
}
#endif