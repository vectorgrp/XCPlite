/*----------------------------------------------------------------------------
| File:
|   filter.c
|
| Description:
|   Floating point moving average filter implementation
|
|   Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include <assert.h> // for assert
#include <math.h>
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "platform.h"

#include "filter.h"

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
// Floating Average Filter
/**************************************************************************/

// Moving Average Filter
// Calculate average over last <size> values
void average_filter_init(tAverageFilter *f, size_t size) {

    if (size > AVERAGE_FILTER_MAX_SIZE) {
        printf("WARNING: average_filter_init: size %zu too large, limiting to %d\n", size, AVERAGE_FILTER_MAX_SIZE);
        size = AVERAGE_FILTER_MAX_SIZE;
    }
    f->size = size;
    f->ai = 0;
    f->as = 0;
    f->count = 0;
    for (size_t i = 0; i < AVERAGE_FILTER_MAX_SIZE; i++)
        f->a[i] = 0;
}

size_t average_filter_size(tAverageFilter *f) { return f->size; }
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

/**************************************************************************/
// Linear Regression Filter
/**************************************************************************/

/*
 * x and y must point to contiguous arrays with n elements
 *
 * all _out parameters are optional and may be NULL
 *
 * r2_out: R2 line fitting metric
 * mae_out: mean average error
 * mse_out: mean square error
 * rmse_out: root mean square error
 *
 * returns: 0 on success or < 0 on error, see below for error codes
 *
 * * Copyright (c) 2020 Torkel Danielsson
 * https://github.com/torkeldanielsson/simple_linear_regression
 * (c.f. e.g. https://en.wikipedia.org/wiki/Simple_linear_regression)
 * MIT License
 *
 */

/* Error codes */
#define SIMPLE_LINEAR_REGRESSION_ERROR_INPUT_VALUE -2
#define SIMPLE_LINEAR_REGRESSION_ERROR_NUMERIC -3

static int linreg(const double *x, const double *y, const int n, double *slope_out, double *intercept_out, double *r2_out, double *mae_out, double *mse_out, double *rmse_out) {
    double sum_x = 0.0;
    double sum_xx = 0.0;
    double sum_xy = 0.0;
    double sum_y = 0.0;
    double sum_yy = 0.0;
    double n_real = (double)(n);
    int i = 0;
    double slope = 0.0;
    double intercept = 0.0;
    double denominator = 0.0;
    double err = 0.0;
    double ack = 0.0;

    if (x == NULL || y == NULL || n < 2) {
        return SIMPLE_LINEAR_REGRESSION_ERROR_INPUT_VALUE;
    }

    for (i = 0; i < n; ++i) {
        sum_x += x[i];
        sum_xx += x[i] * x[i];
        sum_xy += x[i] * y[i];
        sum_y += y[i];
        sum_yy += y[i] * y[i];
    }

    denominator = n_real * sum_xx - sum_x * sum_x;
    if (denominator == 0.0) {
        return SIMPLE_LINEAR_REGRESSION_ERROR_NUMERIC;
    }
    slope = (n_real * sum_xy - sum_x * sum_y) / denominator;

    if (slope_out != NULL) {
        *slope_out = slope;
    }

    intercept = (sum_y - slope * sum_x) / n_real;
    if (intercept_out != NULL) {
        *intercept_out = intercept;
    }

    if (r2_out != NULL) {
        denominator = ((n_real * sum_xx) - (sum_x * sum_x)) * ((n_real * sum_yy) - (sum_y * sum_y));
        if (denominator == 0.0) {
            return SIMPLE_LINEAR_REGRESSION_ERROR_NUMERIC;
        }
        *r2_out = ((n_real * sum_xy) - (sum_x * sum_y)) * ((n_real * sum_xy) - (sum_x * sum_y)) / denominator;
    }

    if (mae_out != NULL) {
        for (i = 0; i < n; ++i) {
            err = intercept + x[i] * slope - y[i];
            ack += fabs(err);
        }
        *mae_out = ack / n_real;
    }

    if (mse_out != NULL || rmse_out != NULL) {
        ack = 0.0;
        for (i = 0; i < n; ++i) {
            err = intercept + x[i] * slope - y[i];
            ack += err * err;
        }
        if (mse_out != NULL) {
            *mse_out = ack / n_real;
        }
        if (rmse_out != NULL) {
            *rmse_out = sqrt(ack / n_real);
        }
    }

    return 0;
}

#define LINREG_TEST
#ifdef LINREG_TEST

#define DATA_POINTS (64)
#define TRUE_SLOPE (0.9)
#define I_OFFSET (1E9)
#define TRUE_INTERCEPT (9.0)
#define RAND_SCALE (0)

#include <stdlib.h> // for rand
static double random_n1_1() { return 2.0 * (double)(rand()) / (double)(RAND_MAX)-1.0; }

int linreg_test() {
    int i = 0;
    int res = -1;
    double x[DATA_POINTS] = {0};
    double y[DATA_POINTS] = {0};
    double i_real = 0.0;
    double slope = 0.0;
    double intercept = 0.0;
    double r2 = 0.0;
    double mae = 0.0;
    double mse = 0.0;
    double rmse = 0.0;

    srand((unsigned int)(time(NULL)));

    printf("Test of simple_linear_regression.h, using random test data:\n\n");

    for (i = 0; i < DATA_POINTS; ++i) {
        i_real = (double)i + I_OFFSET;
        x[i] = i_real; // + random_n1_1();
        y[i] = TRUE_INTERCEPT + TRUE_SLOPE * i_real + RAND_SCALE * random_n1_1();

        printf("%f, %f\n", x[i], y[i]);
    }

    res = linreg(x, y, DATA_POINTS, &slope, &intercept, &r2, &mae, &mse, &rmse);
    if (res < 0) {
        printf("error %d\n", res);
        return res;
    }

    printf("\nslope: %f\n", slope);
    printf("intercept: %f\n", intercept);
    printf("r2: %f\n", r2);
    printf("mae: %f\n", mae);
    printf("mse: %f\n", mse);
    printf("rmse: %f\n", rmse);

    return 0;
}

#endif

void linreg_filter_init(tLinregFilter *f, size_t size) {

#ifdef LINREG_TEST
    linreg_test();
#endif

    if (size > LINREG_FILTER_MAX_SIZE) {
        printf("WARNING: linreg_filter_init: size %zu too large, limiting to %d\n", size, LINREG_FILTER_MAX_SIZE);
        size = LINREG_FILTER_MAX_SIZE;
    }
    f->size = size;
    f->ai = 0;
    f->count = 0;
    for (size_t i = 0; i < LINREG_FILTER_MAX_SIZE; i++)
        f->x[i] = 0;
    for (size_t i = 0; i < LINREG_FILTER_MAX_SIZE; i++)
        f->y[i] = 0;
}

size_t linreg_filter_size(tLinregFilter *f) { return f->size; }
size_t linreg_filter_count(tLinregFilter *f) { return f->count; }

bool linreg_filter_calc(tLinregFilter *f, double x, double y, double *slope_out, double *intercept_out) {

    if (f->count < f->size) {
        f->count++;
    }
    f->x[f->ai] = x;
    f->y[f->ai] = y;
    if (++f->ai >= f->size)
        f->ai = 0;

    double rmse = 0.0;
    double r2 = 0.0;
    double mae = 0.0;
    double mse = 0.0;
    int res = linreg(f->x, f->y, f->count, slope_out, intercept_out, &r2, &mae, &mse, &rmse);
    if (res < 0) {
        printf("ERROR: linreg failed, error = %d\n", res);
        return false;
    }

    // printf("r2: %f\n", r2);
    // printf("mae: %f\n", mae);
    // printf("mse: %f\n", mse);
    // printf("rmse: %f\n", rmse);
    return true;
}
