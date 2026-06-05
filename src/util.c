/*----------------------------------------------------------------------------
| File:
|   util.c
|
| Description:
|   A set of utility functions
|   1. Average and median filter
|   2. Linear regression filter
|   3. Pseudo random number generator
|   4. Clock synchronizer
|
|   Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "util.h"

#include <assert.h>   // for assert
#include <inttypes.h> // PRId64, PRIu64
#include <math.h>     // for sqrt
#include <signal.h>   // for signal handling
#include <stdbool.h>  // bool
#include <stdint.h>   // for uintxx_t
#include <stdio.h>    // for printf
#include <string.h>   // for sprintf, memset

#include "dbg_print.h" // for DBG_PRINTF
#include "platform.h"

/**************************************************************************/
// Simple pseudo random generator
/**************************************************************************/

// Pseudo random unsigned int 0-15
static unsigned int r = 0;

void seed16(unsigned int seed) { r = seed; }

unsigned int random16(void) {
    r = 36969 * (r & 65535) + (r >> 16);
    return r & 0xF;
}

/**************************************************************************/
// Fast pseudo random number generator (splitmix64)
/**************************************************************************/

#if defined(_LINUX) || defined(_MACOS)

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h> // for _umul128
#endif

// Uses splitmix64 - passes all TestU01 BigCrush tests, same speed class as xorshift64.
// State is thread-local so no locking needed in multi-threaded code.
// Range reduction via Lemire's method: (uint128 * max) >> 64 - avoids integer division, bias < max/2^64 (negligible).

static THREAD_LOCAL uint64_t fast_rand_state = 0x9e3779b97f4a7c15ULL;

void fast_rand_seed(uint64_t seed) { fast_rand_state = seed ? seed : 0x9e3779b97f4a7c15ULL; }

static inline uint64_t mul_hi_u64(uint64_t a, uint64_t b) {
#if defined(_MSC_VER) && defined(_M_X64)
    unsigned __int64 hi;
    _umul128(a, b, &hi);
    return hi;
#elif defined(__SIZEOF_INT128__)
    return (uint64_t)(((__uint128_t)a * b) >> 64);
#else
#error "mul_hi_u64 requires _umul128 (MSVC x64) or __int128 support"
#endif
}

uint64_t fast_rand(uint64_t max) {
    if (max == 0)
        return 0;
    uint64_t z = (fast_rand_state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    z ^= (z >> 31);
    return mul_hi_u64(z, max);
}

#endif

/**************************************************************************/
// Integer Median Filter
// (ptp4l-style sorted-index approach)
/**************************************************************************/

// Maintains two fixed-size arrays:
//   samples[]  ring buffer storing values in insertion order
//   order[]    indices into samples[], kept in sorted (ascending) order
//
// On each call to median_filter_calc():
//   - The oldest sample is removed from order[] with an O(N) index shift.
//   - The new sample is inserted into order[] at the correct sorted position
//     with an O(N) backwards scan -- no full re-sort.
//   - The median is returned directly from samples[order[count/2]].
//
// Total cost per call: O(N).
// (Adapted from the linuxptp mmedian filter, using static arrays.)
//
// Window size should be odd for an unambiguous middle element.
// With an even window size the average of the two middle elements is returned.

void median_filter_init(tMedianFilter *f, size_t size) {
    assert(f != NULL);
    if (size == 0)
        size = 1;
    if ((int)size > MEDIAN_FILTER_MAX_SIZE) {
        printf("WARNING: median_filter_init: size %zu too large, limiting to %d\n", size, MEDIAN_FILTER_MAX_SIZE);
        size = MEDIAN_FILTER_MAX_SIZE;
    }
    memset(f, 0, sizeof(*f));
    f->size = (int)size;
}

size_t median_filter_size(tMedianFilter *f) {
    assert(f != NULL);
    return (size_t)f->size;
}
size_t median_filter_count(tMedianFilter *f) {
    assert(f != NULL);
    return (size_t)f->count;
}

/*
 * Insert v and return the current median.
 *
 * order[] holds indices into samples[], always kept in ascending order of
 * the values they point to.  Updating it costs two O(N) passes:
 *   1. Remove the index of the replaced slot from order[] (shift left).
 *   2. Walk backwards from the end to find the insertion point for the new
 *      index, shifting elements right, then place the new index.
 * No scratch buffer, no full re-sort.
 */
int64_t median_filter_calc(tMedianFilter *f, int64_t v) {
    assert(f != NULL);

    f->samples[f->idx] = v;

    if (f->count < f->size) {
        /* Buffer still filling: append new index to order[] then sort it in. */
        f->count++;
    } else {
        /* Buffer full: remove the order[] entry that pointed to the slot we
         * are about to overwrite (idx), shifting the rest left. */
        int i;
        for (i = 0; i < f->count; i++)
            if (f->order[i] == f->idx)
                break;
        for (; i + 1 < f->count; i++)
            f->order[i] = f->order[i + 1];
        /* count stays the same; the last slot will be overwritten below. */
    }

    /* Insert f->idx into order[] at the correct sorted position.
     * Walk backwards: shift right while the predecessor's value is larger. */
    int i;
    for (i = f->count - 1; i > 0; i--) {
        if (f->samples[f->order[i - 1]] <= v)
            break;
        f->order[i] = f->order[i - 1];
    }
    f->order[i] = f->idx;

    f->idx = (f->idx + 1) % f->size;

    if (f->count % 2)
        return f->samples[f->order[f->count / 2]];
    else
        return (f->samples[f->order[f->count / 2 - 1]] + f->samples[f->order[f->count / 2]]) / 2;
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
#define SIMPLE_LINEAR_REGRESSION_ERROR_INPUT_VALUE (-2)
#define SIMPLE_LINEAR_REGRESSION_ERROR_NUMERIC (-3)

static int linreg(const double *x, const double *y, size_t n, double *slope_out, double *intercept_out, double *r2_out, double *mae_out, double *mse_out, double *rmse_out) {
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

// #define LINREG_TEST
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

    f->y_out = 0.0;
    f->slope = 0.0;
}

size_t linreg_filter_size(tLinregFilter *f) { return f->size; }
size_t linreg_filter_count(tLinregFilter *f) { return f->count; }

// slope_out is the calculated slope
// y_out is the interpolated y value at x
bool linreg_filter_calc(tLinregFilter *f, double x, double y, double *slope_out, double *y_out) {

    if (f->count < f->size) {
        f->count++;
    }
    f->x[f->ai] = x;
    f->y[f->ai] = y;
    if (++f->ai >= f->size)
        f->ai = 0;

    // First entry has not enough data
    if (f->count < 2) {
        if (slope_out)
            *slope_out = 1.0;
        if (y_out)
            *y_out = y;
        return false;
    }

    // Normalize values to current x,y to improve numeric stability
    double x_norm[LINREG_FILTER_MAX_SIZE];
    double y_norm[LINREG_FILTER_MAX_SIZE];
    for (size_t i = 0; i < f->count; i++) {
        x_norm[i] = f->x[i] - x;
        y_norm[i] = f->y[i] - y;
    }

    double rmse = 0.0;
    double r2 = 0.0;
    double mae = 0.0;
    double mse = 0.0;
    double s = 0.0;
    double o = 0.0;
    int res = linreg(x_norm, y_norm, f->count, &s, &o, &r2, &mae, &mse, &rmse);
    if (res < 0) {
        printf("ERROR: linreg failed, error = %d\n", res);
        return false;
    }
    f->slope = s;
    f->y_out = o;
    if (slope_out)
        *slope_out = s;
    if (y_out)
        *y_out = o;

    // printf("r2: %f\n", r2);
    // printf("mae: %f\n", mae);
    // printf("mse: %f\n", mse);
    // printf("rmse: %f\n", rmse);
    return true;
}

// Compare two linear regression filters at position x
// slope_diff: difference of slopes f1 - f2
// y_diff: difference of interpolated y values at x, f1 - f2
// Assuming that both filters have the same time scale and epoch for x
bool linreg_filter_compare(tLinregFilter *f1, tLinregFilter *f2, double x, double *slope_diff, double *y_diff) {

    assert(f1 != NULL);
    assert(f2 != NULL);

    if (f1->count < 3 || f2->count < 3) {
        return false;
    }

    // Last inserted point
    // Use this as reference for interpolation and normalization of y_out
    size_t i_1 = (f1->ai + f1->size - 1) % f1->size;
    size_t i_2 = (f2->ai + f2->size - 1) % f2->size;
    double x_last_1 = f1->x[i_1];
    double x_last_2 = f2->x[i_2];
    double y_last_1 = f1->y[i_1];
    double y_last_2 = f2->y[i_2];

    // Interpolated offset diff at x
    double y_out_1 = f1->y_out;
    double y_out_2 = f2->y_out;
    double slope_1 = f1->slope;
    double slope_2 = f2->slope;
    double dx_1 = x - x_last_1;
    double dx_2 = x - x_last_2;
    double diff = (y_out_1 + slope_1 * dx_1) - (y_out_2 + slope_2 * dx_2);
    diff = diff + y_last_1 - y_last_2;
    if (y_diff)
        *y_diff = diff;

    // Slope diff
    if (slope_diff)
        *slope_diff = slope_1 - slope_2;
    return true;
}

/**************************************************************************/
// Lightweight clock synchronizer
/**************************************************************************/

/*
|
|   Interpolates a target clock (t1) from a reference clock (t2) using
|   consecutive (t1, t2) timestamp pairs, e.g. hardware/software timestamp
|   pairs obtained via Linux SO_TIMESTAMPING on an Ethernet interface.
|
|   Epoch independence:
|     t1 and t2 do NOT need to share the same epoch.
|
|   Algorithm (SYNC_MODE_DEFAULT):
|     After two pairs the drift ratio is estimated as:
|
|       drift_ppb = (dt1 - dt2) * 1_000_000_000 / dt2
|
|     where dt1 = t1_new - t1_old  and  dt2 = t2_new - t2_old.
|
|     Interpolation from an arbitrary t2 is then:
|
|       t1_est = t1_anchor + dt2 + dt2 * drift_ppb / 1_000_000_000
|
|     All arithmetic is in int64_t.  dt2 is always a small inter-packet
|     interval (< ~10 s), so there is no overflow risk.
|
|   Algorithm (SYNC_MODE_PI):
|     A PI (proportional-integral) servo maintains drift_ppb as the sum
|     of two terms:
|
|       drift_ppb = pi_drift  +  error_ns / 2^kp_shift
|                  (integral)    (proportional)
|
|     where  error_ns = t1_actual - t1_predicted  (offset error in ns).
|
|     The integral accumulator is updated each cycle:
|
|       pi_error_accum += error_ns              (raw ns, avoids dead zone)
|       pi_drift        = pi_error_accum / 2^ki_shift
|
|     Accumulating raw errors before dividing eliminates the dead zone that
|     would occur if small errors were truncated to zero before accumulation.
|
|     The proportional term reacts immediately to offset errors;
|     the integral term accumulates and tracks slowly changing drift
|     (e.g. due to oscillator temperature coefficient).
|
|     Bootstrap: the first two pairs use the DEFAULT 2-point estimator
|     to seed pi_drift; PI updates begin from the third pair onwards.
|
|     Overflow analysis (SYNC_MODE_PI):
|       error_ns    <= ~100,000 ns  (100 us -- extreme SW timestamp jitter)
|       pi_drift    <= 500,000 ppb  (clamped at 500 ppm)
|       drift_ppb   <= 600,000 ppb  (integral + proportional)
|       dt2*drift   <= 10e9 * 6e5 = 6e15  << INT64_MAX  -> safe
*/

// Initialize the synchronizer state.
// mode: SYNC_MODE_DEFAULT or SYNC_MODE_PI
void syncInit(tClockSynchronizer *s, uint8_t mode, size_t median_window) {
    assert(s != NULL);
    memset(s, 0, sizeof(*s));
    s->mode = mode;
    if (mode == SYNC_MODE_PI) {
        s->kp_shift = SYNC_PI_KP_SHIFT_DEFAULT;
        s->ki_shift = SYNC_PI_KI_SHIFT_DEFAULT;
    }
    // Always initialize the median filter, even if median_window is zero.
    median_filter_init(&s->median_filter, median_window);
    s->use_median = (median_window > 0);
}

// Return true if the synchronizer has received enough valid pairs to produce estimates.
bool syncState(const tClockSynchronizer *s) {
    assert(s != NULL);
    return s->is_sync;
}

// Interpolate t1 (target clock) from t2 (reference clock).
// All arithmetic in int64_t for precision and performance.
//
// Overflow analysis for  dt2 * drift_ppb / 1e9:
//   dt2          <= ~10 s  = 10_000_000_000 ns  (normal call rate >> 0.1 Hz)
//   |drift_ppb|  <=  1_000_000 ppb  (1000 ppm -- extreme clock error)
//   product      <= 10e9 * 1e6 = 1e16  << INT64_MAX (9.2e18)  -> safe
uint64_t syncInterpolateT1(tClockSynchronizer *s, uint64_t t2) {
    assert(s != NULL);
    assert(s->is_sync);

    /* Elapsed time on the reference clock since the anchor in t2 */
    int64_t dt2 = (int64_t)(t2 - s->t2);

    /*
     * Scale dt2 by the clock ratio:
     *   dt1 = dt2 * (1 + drift_ppb / 1e9)
     *       = dt2 + dt2 * drift_ppb / 1_000_000_000
     */
    int64_t dt1 = dt2 + dt2 * s->drift_ppb / 1000000000LL;
    uint64_t t1_est = (uint64_t)((int64_t)s->t1 + dt1);

    /* Guarantee monotonically increasing output */
    if (t1_est <= s->last_t1_out) {
        t1_est = s->last_t1_out + 1;
        DBG_PRINTF_WARNING("syncInterpolateT1FromT2: non-monotonic output adjusted to %" PRIu64 "\n", t1_est);
    }
    s->last_t1_out = t1_est;
    return t1_est;
}

// Feed a new (t1, t2) timestamp pair.
// Calculate drift and update the interpolation anchor.
//
// On the first call only the anchor is stored.
// On the second call is_sync is set to true (both modes).
// From the third call onwards, SYNC_MODE_PI drives drift_ppb via the PI servo.
// Invalid pairs (non-monotonic timestamps) are silently dropped.
void syncUpdate(tClockSynchronizer *s, uint64_t t1, uint64_t t2) {
    assert(s != NULL);

    if (s->cycle_count == 0) {
        /* First call: store anchor only, nothing to compute yet. */
        s->t1 = t1;
        s->t2 = t2;
        s->cycle_count++;
        return;
    }

    int64_t dt2 = (int64_t)(t2 - s->t2);
    int64_t dt1 = (int64_t)(t1 - s->t1);

    /* Reject non-monotonic pairs. */
    if (dt2 <= 0 || dt1 <= 0)
        return;

    if (!s->is_sync) {
        /*
         * Bootstrap (both modes): compute drift from the first valid pair
         * using the simple 2-point estimator.
         *
         *   drift_ppb = (dt1 - dt2) * 1_000_000_000 / dt2
         *
         * For SYNC_MODE_PI, pi_drift is seeded with this value so that the
         * servo starts from a good initial state rather than from zero.
         */
        int64_t raw_drift = (dt1 - dt2) * 1000000000LL / dt2;
        s->drift_ppb = raw_drift;
        if (s->mode == SYNC_MODE_PI) {
            s->pi_drift = raw_drift;
            /* Seed the raw accumulator consistently with pi_drift so that the
             * first PI update starts from the right state.                  */
            s->pi_error_accum = raw_drift * (1LL << s->ki_shift);
        }
        s->is_sync = true;

    } else if (s->mode == SYNC_MODE_PI) {
        /*
         * PI servo update.
         *
         * Compute the offset error: how far is the actual t1 from the value
         * our current model would have predicted at this t2?
         *
         *   t1_predicted = t1_anchor + dt2 + dt2 * drift_ppb / 1e9
         *   error_ns     = t1_actual - t1_predicted
         *                = dt1 - (dt2 + dt2 * drift_ppb / 1e9)
         *
         * Positive error: t1 is ahead of prediction (t1 runs faster than
         * our current drift model; drift_ppb should increase).
         */
        int64_t dt1_pred = dt2 + dt2 * s->drift_ppb / 1000000000LL;
        int64_t error_ns = dt1 - dt1_pred;

        /*
         * Optional median pre-filter: reject outlier pairs before they enter
         * the PI servo.  A single wildly wrong hardware timestamp has zero
         * effect as long as fewer than window/2 consecutive pairs are bad.
         */
        if (s->use_median)
            error_ns = median_filter_calc(&s->median_filter, error_ns);

        /*
         * Integral term: accumulate raw error in ns, then derive pi_drift.
         *
         * Accumulating before dividing avoids a dead zone: with divide-first,
         * any |error_ns| < 2^ki_shift would truncate to zero and a small but
         * persistent offset (e.g. from temperature-induced drift) would never
         * move the integral.  Accumulating first means even a 1 ns persistent
         * error shifts pi_drift after enough cycles.
         *
         * Anti-windup (back-calculation): clamp the accumulator to the range
         * that corresponds to +/- SYNC_PI_INTEGRAL_CLAMP ppb in pi_drift.
         * Clamping the accumulator (not just the output) prevents it from
         * winding further in saturation.
         */
        int64_t accum_clamp = SYNC_PI_INTEGRAL_CLAMP * (1LL << s->ki_shift);
        s->pi_error_accum += error_ns;
        if (s->pi_error_accum > accum_clamp)
            s->pi_error_accum = accum_clamp;
        if (s->pi_error_accum < -accum_clamp)
            s->pi_error_accum = -accum_clamp;
        s->pi_drift = s->pi_error_accum / (1LL << s->ki_shift);

        /*
         * Proportional term: immediate correction for current offset error.
         * Does not accumulate; adds noise proportional to jitter.
         */
        s->drift_ppb = s->pi_drift + error_ns / (1LL << s->kp_shift);

    } else {
        /*
         * SYNC_MODE_DEFAULT: re-estimate drift from every consecutive pair.
         * No filtering -- optimal for very low-jitter sources.
         *
         * Optional median pre-filter: feed the raw per-pair drift estimate
         * through the median filter.  The median of the last N estimates is
         * used as drift_ppb, rejecting occasional outlier pairs.
         */
        int64_t raw_drift = (dt1 - dt2) * 1000000000LL / dt2;
        DBG_PRINTF3("syncUpdate: dt1 = %" PRId64 ", dt2 = %" PRId64 ", raw_drift = %" PRId64 " ppb\n", dt1, dt2, raw_drift);
        s->drift_ppb = s->use_median ? median_filter_calc(&s->median_filter, raw_drift) : raw_drift;
    }

    /* Advance the interpolation anchor to the new pair. */
    s->t1 = t1;
    s->t2 = t2;
    s->cycle_count++;
}

// Feed a new (t1,t2) timestamp anchor and set drift parameters directly.
// Used for testing and simulation of clock properties
void syncSet(tClockSynchronizer *s, uint64_t t1, int64_t t2, int64_t drift_ppb) {
    assert(s != NULL);
    s->t1 = t1;
    s->t2 = t2;
    s->drift_ppb = drift_ppb;
    s->is_sync = true;
}
