/*----------------------------------------------------------------------------
| File:
|   ptp_observer.c
|
| Description:
|   PTP observer/analyzer with XCP instrumentation
|   For testing PTP master stability and multi master ssynchronization
|   Supports IEEE 1588-2008 PTPv2 over UDP/IPv4 in E2E mode
|
|  Code released into public domain, no attribution required
|
 ----------------------------------------------------------------------------*/

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <math.h>     // for fabs
#include <signal.h>   // for signal handling
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t
#include <stdio.h>    // for printf
#include <stdlib.h>   // for malloc, free
#include <string.h>   // for sprintf

#include "ptp.h"

#ifdef OPTION_ENABLE_PTP_OBSERVER

#include "filter.h" // for average filter
#include "ptpHdr.h" // PTP protocol message structures
#include "ptp_observer.h"

#ifdef OPTION_ENABLE_XCP

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

#endif

extern uint8_t ptp_log_level;
#define DBG_LEVEL ptp_log_level
#include "dbg_print.h" // for DBG_PRINT_ERROR, DBG_PRINTF_WARNING, ...

//---------------------------------------------------------------------------------------
// Clock Analyzer

// Default analyzer parameter values
static tPtpObserverParameters observer_params = { //
    .t1_correction = 0,                           // Apply 3ns correction to t1 to compensate for VN56xx master timestamp rounding
    .delay_req_burst_len = 0,                     // Number of DELAY_REQ messages to send after each SYNC in active mode
    .avg_drift_filter_size = 30,                  // Size of the drift average filter
    .linreg_filter_size = 30,                     // Size of the linear regression filter
    .linreg_offset_filter_size = 10,              // Size of the linear regression offset filter
    .linreg_jitter_filter_size = 30,              // Size of the linear regression jitter filter
    .jitter_rms_filter_size = 30,                 // Size of the jitter RMS average filter
    .jitter_avg_filter_size = 30,                 // Size of the jitter average filter
#ifdef OBSERVER_SERVO
    .max_correction = 1000.0, // 1000ns maximum correction per SYNC interval
    .servo_p_gain = 1.0
#endif
};

static void analyzerInit(tPtpClockAnalyzer *a, tPtpObserverParameters *params) {

    // Init timing analysis state
    a->cycle_count = 0; // cycle counter
    a->is_sync = false;
    a->t1 = 0;
    a->t2 = 0;
    a->offset_raw = 0;
    a->t1_offset = 0;
    a->t2_offset = 0;
    a->t1_norm = 0;
    a->t2_norm = 0;
    a->offset_norm = 0;

    a->avg_drift = 0.0;
    a->avg_drift_drift = 0.0;
    average_filter_init(&a->avg_drift_filter, params->avg_drift_filter_size);
    average_filter_init(&a->avg_drift_drift_filter, params->avg_drift_filter_size);

    a->linreg_drift = 0.0;
    a->linreg_offset = 0.0;
    a->linreg_offset_avg = 0.0;
    a->linreg_drift_drift = 0.0;
    a->linreg_jitter = 0.0;
    a->linreg_jitter_avg = 0.0;
    linreg_filter_init(&a->linreg_filter, params->linreg_filter_size);
    average_filter_init(&a->linreg_offset_filter, params->linreg_offset_filter_size);
    average_filter_init(&a->linreg_jitter_filter, params->linreg_jitter_filter_size);

#ifdef OBSERVER_SERVO
    a->master_offset_compensation = 0;
    a->master_offset_detrended = 0;
    a->master_offset_detrended_filtered = 0;
#endif

    average_filter_init(&a->jitter_rms_filter, params->jitter_rms_filter_size);
    a->jitter_rms = 0;
    average_filter_init(&a->jitter_avg_filter, params->jitter_avg_filter_size);
    a->jitter_avg = 0;
}

// Update the PTP observer state with each new SYNC (t1,t2) timestamps
static void analyzerUpdate(const char *observer_name, const char *analyzer_name, tPtpClockAnalyzer *a, uint64_t t1_in, uint64_t t2_in) {

    assert(a != NULL);

    a->cycle_count++;

    // Master timestamps with correction applied
    uint64_t t1 = t1_in;
    a->t1 = t1;

    // Local timestamp
    uint64_t t2 = t2_in;
    a->t2 = t2;

    if (ptp_log_level >= 3) {
        printf("  Analyzer %s %s: t1 = %" PRIu64 ", t2 = %" PRIu64 "\n", observer_name, analyzer_name, t1, t2);
    }

    // Master offset raw value
    a->offset_raw = (int64_t)(t1 - t2); // Positive master_offset means master is ahead
    if (ptp_log_level >= 4) {
        printf("      offset_raw = %" PRIi64 " ns\n", a->offset_raw);
    }

    // First round, init
    if (a->t1_offset == 0 || a->t2_offset == 0) {

        a->t1_norm = 0; // corrected sync tx time on master clock
        a->t2_norm = 0; // sync rx time on slave clock

        // Normalization time offsets for t1,t2
        a->t1_offset = t1;
        a->t2_offset = t2;
#ifdef OBSERVER_SERVO
        obs->master_offset_compensation = 0;
#endif

        if (ptp_log_level >= 4)
            printf("      t1_offset = %" PRIu64 " ns, t2_offset = %" PRIu64 " ns\n", a->t1_offset, a->t2_offset);
    }

    // Analysis
    else {

        // Normalize t1,t2 to first round start values
        // Avoid conversion loss, when using double precision floating point calculations later
        // The largest uint64_t value that can be converted to double without loss of precision is 2^53 (9,007,199,254,740,992).
        int64_t t1_norm = (int64_t)(t1 - a->t1_offset);
        int64_t t2_norm = (int64_t)(t2 - a->t2_offset);
        assert(t1_norm >= 0 && t2_norm >= 0);                                 // Should never happen
        assert((t2_norm < 0x20000000000000) && (t1_norm < 0x20000000000000)); // Should never happen (loss of precision after ≈ 104.25 days
        if (ptp_log_level >= 4)
            printf("      t1_norm = %" PRIi64 " ns, t2_norm = %" PRIi64 " ns\n", t1_norm, t2_norm);

        // Calculate momentary master offset
        a->offset_norm = t1_norm - t2_norm; // Positive master_offset means master is ahead
        if (ptp_log_level >= 4) {
            printf("      offset_norm = %" PRIi64 " ns\n", a->offset_norm);
        }

        // Calculate cycle time on local (c2) and master clock (c1)
        int64_t c1 = t1_norm - a->t1_norm; // time since last sync on master clock
        int64_t c2 = t2_norm - a->t2_norm; // time since last sync on local clock

        // Drift and drift of drift calculation by floating average method
        {
            int64_t diff = c1 - c2; // Positive diff = master clock faster than local clock
            double drift = 0.0;
            double drift_drift = 0.0;
            if (diff < -200000 || diff > +200000) { // Plausibility checking of cycle drift (max 200us per cycle)
                DBG_PRINTF_WARNING("WARNING: Master drift too high! dt=%lld ns \n", diff);
            } else {
                double drift_raw = (double)diff * 1000000000 / c2;                   // Calculate drift in ppm instead of drift per cycle (drift is in ns/s (1/1000 ppm)
                double drift = average_filter_calc(&a->avg_drift_filter, drift_raw); // Filter drift
                a->avg_drift_drift = drift - a->avg_drift;                           // Calculate drift of drift in ns/(s*s) (should be close to zero when temperature is stable )
                a->avg_drift = drift;
            }
            if (ptp_log_level >= 3) {
                printf("  Analyzer %s %s: Average filter results at t2 = %lld ns: \n", observer_name, analyzer_name, t2_norm);
                printf("    avg drift        = %g ns/s\n", a->avg_drift);
                printf("    avg drift_drift  = %g ns/s2\n", a->avg_drift_drift);
            }
        }

        // Drift, drift of drift and jitter calculation by linear regression method
        // Drift of drift in ns/(s*s) (should be close to zero when temperature is stable )
        double linreg_slope = 0.0;
        double linreg_offset = 0.0;
        if (linreg_filter_calc(&a->linreg_filter, (double)t2_norm, (double)t1_norm, &linreg_slope, &linreg_offset)) {
            double linreg_drift = (linreg_slope - 1.0) * 1E9;
            a->linreg_drift_drift = linreg_drift - a->linreg_drift;
            a->linreg_drift = linreg_drift;
            a->linreg_offset = linreg_offset;
            // Jitter is the deviation from the linear regression line at t2 !!!!
            // While drift of drift is not zero, linreg_offset is not jitter!
            // Calculate average linreg offset to compensate offset for jitter calculation, avarage of jitter should be zero
            a->linreg_offset_avg = average_filter_calc(&a->linreg_offset_filter, linreg_offset);
            a->linreg_jitter = linreg_offset - a->linreg_offset_avg;
            a->linreg_jitter_avg = average_filter_calc(&a->linreg_jitter_filter, a->linreg_jitter);

            // Use the linear regression results when master is synchronized
            if (a->is_sync) {
                if (ptp_log_level >= 3) {
                    printf("  Analyzer %s %s: Linear regression results at t2 = %lld ns: \n", observer_name, analyzer_name, t2_norm);
                    printf("    linreg drift = %g ns/s\n", a->linreg_drift);
                    printf("    linreg drift_drift = %g ns/s2\n", a->linreg_drift_drift);
                    printf("    linreg offset = %g ns\n", a->linreg_offset);
                    printf("    linreg offset average = %g ns\n", a->linreg_offset_avg);
                    printf("    linreg jitter = %g ns\n", a->linreg_jitter);
                    printf("    linreg jitter average = %g ns\n", a->linreg_jitter_avg);
                }
            }

            else if (a->cycle_count > 8 && fabs(a->linreg_drift_drift) < 5.0) { // @@@@ TODO: parameterize in sync condition
                a->is_sync = true;
            } else {
                if (ptp_log_level >= 3)
                    printf("Analyzer %s %s: Warming up\n", observer_name, analyzer_name);
            }
        }

        a->drift = a->linreg_drift;             // Drift in 1000*ppm
        a->drift_drift = a->linreg_drift_drift; // Drift of the drift in 1000*ppm/s*s
        a->jitter = a->linreg_jitter;           // Jitter in ns

#ifdef OBSERVER_SERVO

        if (obs->master_offset_compensation == 0) {
            // Initialize compensation
            obs->master_offset_compensation = obs->offset_norm;
        } else {
            // Compensate drift
            // obs->master_offset_compensation -= ((obs->master_drift) * (double)obs->sync_cycle_time) / 1000000000;
            // Compensate drift and drift of drift
            double n = average_filter_count(&obs->drift_filter) / 2;
            obs->master_offset_compensation -= ((obs->master_drift + obs->master_drift_drift * n) * (double)obs->sync_cycle_time) / 1000000000;
        }
        obs->master_offset_detrended = (double)obs->offset_norm - obs->master_offset_compensation;
        obs->master_offset_detrended_filtered = average_filter_calc(&obs->master_jitter_avg_filter, obs->master_offset_detrended);
        if (ptp_log_level >= 4) {
            printf("    master_offset_comp  = %g ns\n", obs->master_offset_compensation);
        }
        if (ptp_log_level >= 3) {
            printf("    master_offset = %g ns (detrended)\n", obs->master_offset_detrended);
            printf("    master_offset = %g ns (filtered detrended)\n", obs->master_offset_detrended_filtered);
        }

        // PI Servo Controller to prevent offset runaway
        // The offset error should ideally be zero-mean jitter.
        // Any persistent non-zero mean indicates drift estimation error that needs correction.
        double correction = obs->master_offset_detrended_filtered;

        // P-term
        correction *= obs->params->servo_p_gain;

        // Correction rate limiting
        if (correction > obs->params->max_correction)
            correction = obs->params->max_correction;
        if (correction < -obs->params->max_correction)
            correction = -obs->params->max_correction;

        // Apply correction
        obs->master_offset_compensation += correction;
        average_filter_add(&obs->master_jitter_avg_filter, -correction);
        if (ptp_log_level >= 5)
            printf("Applied compensation correction: %g ns\n", correction);

        obs->master_jitter = obs->master_offset_detrended; // Jitter is the unfiltered detrended master offset
#endif

        // Jitter analysis
        a->jitter_avg = average_filter_calc(&a->jitter_avg_filter, a->jitter);                           // Filter jitter
        a->jitter_rms = sqrt((double)average_filter_calc(&a->jitter_rms_filter, a->jitter * a->jitter)); // Filter jitter and calculate RMS
        if (ptp_log_level >= 3) {
            printf("    Jitter analysis:\n");
            printf("      jitter       = %g ns\n", a->jitter);
            printf("      jitter_avg   = %g ns\n", a->jitter_avg);
            printf("      jitter_rms   = %g ns\n", a->jitter_rms);
        }

        // Remember last normalized input values
        a->t1_norm = t1_norm; // sync tx time on master clock
        a->t2_norm = t2_norm; // sync rx time on slave clock
    }
}

//---------------------------------------------------------------------------------------
// PTP observer initialization

// Reset the PTP observer state
static void observerReset(tPtpObserver *obs) {
    assert(obs != NULL);

    // Init protocol state
    obs->sync_local_time = 0;                                        // Local receive timestamp of SYNC
    obs->sync_local_time_last = 0;                                   // Local receive timestamp of previous SYNC
    obs->sync_cycle_time = 0;                                        // Master SYNC cycle time
    obs->sync_master_time = 0;                                       // SYNC timestamp
    obs->sync_correction = 0;                                        // SYNC correction
    obs->sync_sequenceId = 0;                                        // SYNC sequence Id
    obs->sync_steps = 0;                                             // SYNC steps removed
    obs->flup_master_time = 0;                                       // FOLLOW_UP timestamp
    obs->flup_correction = 0;                                        // FOLLOW_UP correction
    obs->flup_sequenceId = 0;                                        // FOLLOW_UP sequence Id
    obs->delay_req_system_time = clockGet() + 3 * CLOCK_TICKS_PER_S; // System time when last DELAY_REQ was sent
    obs->delay_req_local_time = 0;                                   // Local send timestamp of DELAY_REQ
    obs->delay_req_sequenceId = 0;                                   // Sequence Id of last DELAY_REQ message sent
    obs->delay_req_master_time = 0;                                  // DELAY_RESP timestamp
    obs->delay_resp_correction = 0;                                  // DELAY_RESP correction
    obs->delay_resp_sequenceId = 0;                                  // Sequence Id of last DELAY_RESP message received
    obs->delay_resp_logMessageInterval = 0;

    // Clock analyzer state
    analyzerInit(&obs->a12, obs->params); // Init timing analysis state for t1,t2 SYNC timestamps
    analyzerInit(&obs->a34, obs->params); // Init timing analysis state for t3,t4 DELAY timestamps

    // Clock analyzer results (from a12 in passive or a34 in active mode)
    obs->master_drift_drift = 0;
    obs->master_drift = 0;
    obs->master_jitter = 0;
    obs->master_jitter_rms = 0;
    obs->master_jitter_avg = 0;

    // Path delay and master offset
    obs->sync_sequenceId_last = 0;
    obs->delay_resp_sequenceId_last = 0;
    obs->path_delay = 0;    // Current path delay
    obs->master_offset = 0; // Current master offset

    obs->gm_last_update_time = clockGet(); // Timeout from now
}

// Print information on the grandmaster
static void observerPrintMaster(const tPtpObserver *obs) {
    assert(obs != NULL);
    const tPtpObserverMaster *m = &obs->gm;

    printf("    PTP Master:\n");
    const char *timesource = (m->a.timeSource == PTP_TIME_SOURCE_INTERNAL) ? "internal oscilator" : (m->a.timeSource == PTP_TIME_SOURCE_GPS) ? "GPS" : "Unknown";
    printf("      domain=%u, addr=%u.%u.%u.%u, clockId=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n"
           "      timesource=%s (%02X), utcOffset=%u, prio1=%u, class=%u, acc=%u, var=%u, prio2=%u, steps=%u, grandmasterId=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n",
           m->domain, m->addr[0], m->addr[1], m->addr[2], m->addr[3], m->uuid[0], m->uuid[1], m->uuid[2], m->uuid[3], m->uuid[4], m->uuid[5], m->uuid[6], m->uuid[7], timesource,
           m->a.timeSource, htons(m->a.utcOffset), m->a.priority1, m->a.clockClass, m->a.clockAccuraccy, htons(m->a.clockVariance), m->a.priority2, htons(m->a.stepsRemoved),
           m->a.grandmasterId[0], m->a.grandmasterId[1], m->a.grandmasterId[2], m->a.grandmasterId[3], m->a.grandmasterId[4], m->a.grandmasterId[5], m->a.grandmasterId[6],
           m->a.grandmasterId[7]);
}

// Print the current PTP observer state
void observerPrintState(tPtp *ptp, tPtpObserver *obs) {

    printf("  Observer '%s' (%u):\n", obs->name, obs->a12.cycle_count);
    if (obs->gmValid) {

        observerPrintMaster(obs);

        char ts[64];
        uint64_t t1 = obs->sync_master_time;
        printf("    t1 = %s (%" PRIu64 ")\n", clockGetString(ts, sizeof(ts), t1), t1);

        if (ptp_log_level >= 3) {
            if (obs->a12.is_sync) {
                printf("    t12 drift        = %g ns/s\n", obs->a12.drift);
                printf("    t12 drift_drift  = %g ns/s2\n", obs->a12.drift_drift);
                printf("    t12 jitter       = %g ns\n", obs->a12.jitter);
                printf("    t12 jitter_avg   = %g ns\n", obs->a12.jitter_avg);
                printf("    t12 jitter_rms   = %g ns\n", obs->a12.jitter_rms);
            }
            if (obs->activeMode) {
                if (obs->a34.is_sync) {
                    printf("    t34 drift        = %g ns/s\n", obs->a34.drift);
                    printf("    t34 drift_drift  = %g ns/s2\n", obs->a34.drift_drift);
                    printf("    t34 jitter       = %g ns\n", obs->a34.jitter);
                    printf("    t34 jitter_avg   = %g ns\n", obs->a34.jitter_avg);
                    printf("    t34 jitter_rms   = %g ns\n", obs->a34.jitter_rms);
                }
            }
        }
        if (obs->a12.is_sync || obs->a34.is_sync) {
            printf("    drift        = %g ns/s\n", obs->master_drift);
            printf("    drift_drift  = %g ns/s2\n", obs->master_drift_drift);
            printf("    jitter       = %g ns\n", obs->master_jitter);
            printf("    jitter_avg   = %g ns\n", obs->master_jitter_avg);
            printf("    jitter_rms   = %g ns\n", obs->master_jitter_rms);
        }
        if (obs->activeMode) {
            printf("    path_delay     = %" PRIu64 " ns\n", obs->path_delay);
            printf("    master_offset  = %" PRIi64 " ns\n", obs->master_offset);
        }

        for (int j = 0; j < ptp->observer_count; j++) {
            tPtpObserver *obs_other = ptp->observer_list[j];
            if (obs != obs_other && obs_other->gmValid) {
                printf("    to %s:\n", obs_other->name);
                printf("      offset diff: %" PRIi64 " ns (%g ms)\n", obs->offset_to[j], obs->offset_to[j] / 1000000.0);
                printf("      drift diff: %g ppm\n", obs->drift_to[j] / 1000.0);
            }
        }
    } else {
        printf("    No active PTP master yet\n");
    }
    printf("\n");
}

// Send delay requests in active mode
static bool observerSendDelayRequest(tPtp *ptp, tPtpObserver *obs) {
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);
    assert(obs != NULL);
    assert(obs->gmValid);
    assert(obs->activeMode);
    uint64_t now = clockGet();
    if (obs->delay_req_sequenceId != obs->delay_resp_sequenceId) {
        DBG_PRINTF_WARNING("PTP Observer %s: Skipping DELAY_REQ, previous request (seqID=%u) has not been answered yet\n", obs->name, obs->delay_req_sequenceId);
        return false;
    } else {
        // Send a delay request to our grandmaster
        if (!ptpSendDelayRequest(ptp, obs->gm.domain, obs->client_uuid, ++obs->delay_req_sequenceId, &obs->delay_req_local_time)) {
            DBG_PRINTF_ERROR("PTP Observer %s: Failed to send DELAY_REQ\n", obs->name);
            obs->delay_req_sequenceId--;
            return false;
        } else {
            obs->delay_req_system_time = now;
        }
    }

    return false;
}

// New t1,t2 pair available from SYNC message
static void observerSyncUpdate(tPtp *ptp, tPtpObserver *obs) {

    uint64_t t1 = (obs->sync_steps == 1) ? obs->sync_master_time : obs->flup_master_time; // master clock
    uint64_t t2 = obs->sync_local_time;                                                   // local clock
    uint64_t correction = obs->sync_correction;                                           // for master timestamp t1

    if (ptp_log_level >= 3) {
        printf("Observer %s: PTP SYNC cycle %u:\n", obs->name, obs->sync_sequenceId);
        char ts1[64], ts2[64];
        printf("  t1 (SYNC tx on master (via PTP))  = %s (%" PRIu64 ") (%08X)\n", clockGetString(ts1, sizeof(ts1), t1), t1, (uint32_t)t1);
        printf("  t2 (SYNC rx)  = %s (%" PRIu64 ") (%08X)\n", clockGetString(ts2, sizeof(ts2), t2), t2, (uint32_t)t2);
        printf("  correction    = %" PRIu64 "ns\n", correction);
        printf("  cycle time    = %" PRIu64 "ns\n", obs->sync_cycle_time);
    }

    // Update analyzer parameters (update XCP calibrations)
    // Single threaded access assumed, called from ptpThread319 (1 step mode) or ptpThread320 (2 step mode) only
#ifdef OPTION_ENABLE_XCP
    // Each instance holds its lock continuously, so it may take about a second to make calibration changes effective
    XcpUpdateCalSeg((void **)&obs->params);
#endif

    // Apply rounding correction to t1 ( Vector VN/VX PTP master has 8ns resolution, which leads to a systematic error )
    t1 += obs->params->t1_correction;

    analyzerUpdate(obs->name, "a12", &obs->a12, t1 + correction, t2);
    if (obs->a12.is_sync) {

        // Use as results in passive mode
        if (!obs->activeMode) {
            obs->master_drift = obs->a12.drift;
            obs->master_drift_drift = obs->a12.drift_drift;
            obs->master_jitter = obs->a12.jitter;
            obs->master_jitter_avg = obs->a12.jitter_avg;
            obs->master_jitter_rms = obs->a12.jitter_rms;
        }

        // Compare to other observers for different master clocks
        if (ptp->observer_count > 1) {
            if (ptp_log_level >= 3)
                printf("  Comparisons to other observers:\n");
            for (int j = 0; j < ptp->observer_count; j++) {
                tPtpObserver *obs_other = ptp->observer_list[j];
                if (obs != obs_other && obs_other->gmValid && obs_other->a12.is_sync) {
                    double dt = (int64_t)(obs->a12.t2 - obs_other->a12.t2);
                    if (obs->a12.t1 > obs_other->a12.t1) {
                        // Master offset to other observer
                        // t1 includes correction
                        // In active mode , compensate individual path delay
                        if (obs->activeMode) {
                            obs->offset_to[j] = (double)(obs->a12.t1 - obs->path_delay - obs_other->a12.t1 - obs_other->path_delay) // Compensate path_delay
                                                - dt - (((double)dt * obs_other->master_drift) / 1000000000.0);                     // Compensate for different local receive times
                        } else {
                            obs->offset_to[j] = (double)(obs->a12.t1 - obs_other->a12.t1) - dt -
                                                (((double)dt * obs_other->master_drift) / 1000000000.0); // Compensate for different local receive times
                        }
                        // Drift difference to other observer
                        obs->drift_to[j] = obs->master_drift - obs_other->master_drift;
                        if (ptp_log_level >= 3) {
                            printf("    offset     to %s: %" PRIi64 " ns (%g ms)\n", obs_other->name, obs->offset_to[j], obs->offset_to[j] / 1000000.0);
                            printf("    drift diff to %s: %g ns/s\n", obs_other->name, obs->drift_to[j]);
                        }
                    }
                }
            }
        }

        if (ptp_log_level >= 3)
            printf("\n");
    }

// XCP measurement event (relative addressing mode to observer instance)
#ifdef OPTION_ENABLE_XCP
    XcpEventExt_Var(obs->xcp_event, 1, (const uint8_t *)obs); // Base address 0 (addr ext = 2) is observer instance
#endif
}

// New t3,t4 pair available from DELAY_REQ/RESP messages
static void observerDelayUpdate(tPtpObserver *obs) {
    assert(obs != NULL);

    uint64_t t1 = (obs->sync_steps == 1) ? obs->sync_master_time : obs->flup_master_time; // master clock
    uint64_t t2 = obs->sync_local_time;                                                   // local clock
    uint64_t t3 = obs->delay_req_local_time;                                              // local clock
    uint64_t t4 = obs->delay_req_master_time;
    uint64_t correction = obs->delay_resp_correction; // for master timestamp t4

    analyzerUpdate(obs->name, "a34", &obs->a34, t4 - correction, t3);
    // Use as results in active mode
    if (obs->activeMode && obs->a34.is_sync) {
        obs->master_drift = obs->a34.drift;
        obs->master_drift_drift = obs->a12.drift_drift;
        obs->master_jitter = obs->a12.jitter;
        obs->master_jitter_avg = obs->a12.jitter_avg;
        obs->master_jitter_rms = obs->a12.jitter_rms;
    }

    // Update path_delay and master_offset only, when new data from SYNC and DELAY_REQ is available
    assert(t4 > t1);
    if (obs->delay_resp_sequenceId != obs->delay_resp_sequenceId_last && obs->sync_sequenceId != obs->sync_sequenceId_last) {

        obs->delay_resp_sequenceId_last = obs->delay_resp_sequenceId;
        obs->sync_sequenceId_last = obs->sync_sequenceId;

        /* PTP protocol
           Master   Client
               t1             t1 = SYNC master tx timestamp
                  \
                    t2        t2 = SYNC client rx timestamp
                    t3        t3 = DELAY_REQUEST client tx timestamp
                  /
               t4             t4 = DELAY_RESPONSE master rx timestamp
        */

        // Drift correction for t4
        int64_t t4_drift_correction = (int64_t)(t4 - t1) * obs->master_drift / 1.0E9;

        // Calculate mean path delay
        uint64_t t21 = (t2 - t1 - obs->sync_correction);
        uint64_t t43 = (t4 - t3 - obs->delay_resp_correction - t4_drift_correction);
        obs->path_delay = (t21 + t43) / 2;

        // Calculate master offset
        obs->master_offset = t21 - obs->path_delay;

        if (ptp_log_level >= 4) {
            printf("Observer %s: DELAY_REQ/RESP update:\n", obs->name);
            printf("  t1 (SYNC tx on master)         = %" PRIu64 " ns\n", t1);
            printf("  t2 (SYNC rx)                   = %" PRIu64 " ns\n", t2);
            printf("  t3 (DELAY_REQ tx)              = %" PRIu64 " ns\n", t3);
            printf("  t4 (DELAY_RESP rx on master)   = %" PRIu64 " ns\n", t4);
            printf("  t2 - t1                        = %" PRIu64 " ns\n", t2 - t1);
            printf("  sync_correction                = %u ns\n", obs->sync_correction);
            printf("  t2 - t1 - correction           = %" PRIu64 " ns\n", t21);
            printf("  t4 - t3                        = %" PRIu64 " ns\n", t4 - t3);
            printf("  delay_resp_correction          = %u ns\n", obs->delay_resp_correction);
            printf("  t4_drift_correction            = %" PRIu64 " ns\n", t4_drift_correction);
            printf("  t4 - t3 - correction           = %" PRIu64 " ns\n", t43);
            printf("  path_delay                     = %" PRIu64 " ns\n", obs->path_delay);
            printf("  master_offset                  = %" PRIi64 " ns\n", obs->master_offset);
            printf("\n");
        }
    }
}

// Handle PTP messages (SYNC, FOLLOW_UP, DELAY_RESP) for the PTP observer
bool observerHandleFrame(tPtp *ptp, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t rx_timestamp) {

    if (!(n >= 44 && n <= 64)) {
        DBG_PRINT_ERROR("Invalid PTP message size\n");
        return false; // PTP message too small or too large
    }

    for (int i = 0; i < ptp->observer_count; i++) {
        tPtpObserver *obs = ptp->observer_list[i];
        assert(obs != NULL);

        // Check if this observer is locked onto a grandmaster
        if (obs->gmValid) {

            // Check if message is from this observers master
            if (obs->gm.domain == ptp_msg->domain && (memcmp(obs->gm.uuid, ptp_msg->clockId, 8) == 0) && (memcmp(obs->gm.addr, addr, 4) == 0)) {

                mutexLock(&obs->mutex);

                // SYNC or FOLLOW_UP message from our master
                if (obs->gmValid && (ptp_msg->type == PTP_SYNC || ptp_msg->type == PTP_FOLLOW_UP)) {

                    obs->gm_last_update_time = clockGet(); // Last master activity in system time

                    if (ptp_msg->type == PTP_SYNC) {
                        assert(rx_timestamp != 0);
                        obs->sync_local_time_last = obs->sync_local_time;
                        obs->sync_local_time = rx_timestamp;
                        obs->sync_cycle_time = (obs->sync_local_time_last == 0) ? 0 : (obs->sync_local_time - obs->sync_local_time_last);
                        obs->sync_master_time = htonl(ptp_msg->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp_msg->timestamp.timestamp_ns);
                        obs->sync_sequenceId = htons(ptp_msg->sequenceId);
                        obs->sync_correction = (uint32_t)(htonll(ptp_msg->correction) >> 16);
                        obs->sync_steps = (htons(ptp_msg->flags) & PTP_FLAG_TWO_STEP) ? 2 : 1;

                        // 1 step sync update
                        if (obs->sync_steps == 1) {
                            observerSyncUpdate(ptp, obs);
                            if (obs->activeMode) {
                                // In active mode, request delay measurement after each SYNC
                                observerSendDelayRequest(ptp, obs);
                                obs->delay_req_burst_count = obs->params->delay_req_burst_len; // Send multiple delay requests per SYNC
                            }
                        }
                    }

                    else { // FOLLOW_UP

                        obs->flup_master_time = htonl(ptp_msg->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp_msg->timestamp.timestamp_ns);
                        obs->flup_sequenceId = htons(ptp_msg->sequenceId);
                        obs->flup_correction = (uint32_t)(htonll(ptp_msg->correction) >> 16);
                    }

                    // 2 step sync update, SYNC and FOLLOW_UP may be received in any order (thread319 and thread320)
                    if (obs->sync_steps == 2 && obs->sync_sequenceId == obs->flup_sequenceId) {

                        observerSyncUpdate(ptp, obs); // 2 step
                        if (obs->activeMode) {
                            // In active mode, request delay measurement after each FOLLOW_UP
                            observerSendDelayRequest(ptp, obs);
                            obs->delay_req_burst_count = obs->params->delay_req_burst_len; // Send multiple delay requests per SYNC
                        }
                    }
                }

                // DELAY_RESP message from our master
                else if (ptp_msg->type == PTP_DELAY_RESP) {

                    if (memcmp(obs->client_uuid, ptp_msg->u.r.clockId, 8) == 0) { // Check delay request response is for us
                        obs->delay_req_master_time = htonl(ptp_msg->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp_msg->timestamp.timestamp_ns);
                        obs->delay_resp_sequenceId = htons(ptp_msg->sequenceId);
                        if (obs->delay_resp_sequenceId != obs->delay_req_sequenceId) {
                            DBG_PRINTF_WARNING("Observer %s: DELAY_RESP sequenceId %u does not match last DELAY_REQ sequenceId %u\n", obs->name, obs->delay_resp_sequenceId,
                                               obs->delay_req_sequenceId);
                        }
                        obs->delay_resp_correction = (uint32_t)(htonll(ptp_msg->correction) >> 16);
                        obs->delay_resp_logMessageInterval = ptp_msg->logMessageInterval;

                        // update DELAY_REQ cycletime (DELAY_REQ has constant delay to SYNC (parameter delayReqDelayMs), logMessageInterval is realized by skipping SYNCs
                        // @@@@ Not implemented yet
                        // if (delayReqCycle == 0)
                        //     delayReqCycle = 1 << gPtpC.delay_resp_logMessageInterval;
                        if (ptp_log_level >= 3)
                            printf("Observer %s: DELAY_RESP received from %u.%u.%u.%u: seqID=%u, t3=%llu, t4=%llu, corr=%u\n", obs->name, addr[0], addr[1], addr[2], addr[3],
                                   obs->delay_resp_sequenceId, (unsigned long long)obs->delay_req_local_time, (unsigned long long)obs->delay_req_master_time,
                                   obs->delay_resp_correction);
                        observerDelayUpdate(obs);

                        // Send additional delay requests in active mode
                        if (obs->activeMode && obs->delay_req_burst_count > 1) {
                            observerSendDelayRequest(ptp, obs);
                            obs->delay_req_burst_count--;
                        }

                    } else {

                        if (ptp_log_level >= 4)
                            printf("Observer %s: DELAY_RESP received for another slave clock %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
                                   obs->name, //
                                   ptp_msg->u.r.clockId[0], ptp_msg->u.r.clockId[1], ptp_msg->u.r.clockId[2], ptp_msg->u.r.clockId[3], ptp_msg->u.r.clockId[4],
                                   ptp_msg->u.r.clockId[5], ptp_msg->u.r.clockId[6], ptp_msg->u.r.clockId[7]

                            );
                    }
                }

                // ANNOUNCE message from our master
                else if (ptp_msg->type == PTP_ANNOUNCE) {
                    // Update grandmaster info
                    obs->gm_last_update_time = clockGet(); // Last master activity in system time
                    obs->gm.a = ptp_msg->u.a;
                    if (ptp_log_level >= 3) {
                        printf("PTP Announce received from grandmaster of observer '%s'\n", obs->name);
                    }
                }

                // Unknown message type
                else {
                    DBG_PRINTF_WARNING("Observer %s: Unexpected PTP message type %u from grandmaster\n", obs->name, ptp_msg->type);
                }

                mutexUnlock(&obs->mutex);

                return true; // Message handled

            } // message for our master

        } // master locked

        // Observer has not yet seen his grandmaster
        else {
            // Check if announce messages from any master if match this observers master filter
            if (ptp_msg->type == PTP_ANNOUNCE) {

                // Check if domain, uuid (if specified) and addr (if specified) match
                if ((obs->target_domain == ptp_msg->domain) && (memcmp(obs->target_uuid, ptp_msg->clockId, 8) == 0 || memcmp(obs->target_uuid, "\0\0\0\0\0\0\0\0", 8) != 0) &&
                    (memcmp(obs->target_addr, addr, 4) == 0 || memcmp(obs->target_addr, "\0\0\0", 4) == 0)) {
                    if (ptp_log_level >= 3) {
                        printf("PTP Announce received from a master matching observer '%s' filter. ClockId = %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", obs->name,
                               ptp_msg->clockId[0], ptp_msg->clockId[1], ptp_msg->clockId[2], ptp_msg->clockId[3], ptp_msg->clockId[4], ptp_msg->clockId[5], ptp_msg->clockId[6],
                               ptp_msg->clockId[7]);
                    }

                    mutexLock(&obs->mutex);

                    obs->gmValid = true;
                    obs->gm.a = ptp_msg->u.a;
                    memcpy(obs->gm.addr, addr, 4);
                    memcpy(obs->gm.uuid, ptp_msg->clockId, 8);
                    observerReset(obs); // Reset observer state
                    observerPrintMaster(obs);
                    obs->gm_last_update_time = clockGet(); // Timeout from now
                    // Locked onto a grandmaster

                    mutexUnlock(&obs->mutex);

                    return true; // Message handled
                }
            }
        }

    } // for each observer

    // Check announce messages from unknown masters
    if (ptp_msg->type == PTP_ANNOUNCE) {
        if (ptp->auto_observer) {
            // Create new observer for this master
            // Generate a name
            char name[32];
            snprintf(name, sizeof(name), "obs_%u.%u_%u", addr[2], addr[3], ptp_msg->domain);
            ptpCreateObserver(ptp, name, ptp->auto_observer_active_mode, ptp_msg->domain, ptp_msg->clockId, addr);
        }
    }

    return true;
}

bool observerTask(tPtp *ptp) {

    for (int i = 0; i < ptp->observer_count; i++) {
        tPtpObserver *obs = ptp->observer_list[i];
        assert(obs != NULL);

        mutexLock(&obs->mutex);

        // Check for grandmaster timeout
        if (obs->gmValid) {
            uint64_t now = clockGet();
            uint64_t elapsed = now - obs->gm_last_update_time;
            if (elapsed / (double)CLOCK_TICKS_PER_S > PTP_OBSERVER_GM_TIMEOUT_S) {
                if (ptp_log_level >= 3) {
                    printf("PTP Observer %s: Grandmaster lost! timeout after %us. Last seen %gs ago\n", obs->name, PTP_OBSERVER_GM_TIMEOUT_S, elapsed / (double)CLOCK_TICKS_PER_S);
                }
                obs->gmValid = false;
            }
        }

#ifdef PTP_OBSERVER_DELAY_REQ_CYCLIC
        // Send delay requests
        if (obs->gmValid && obs->activeMode) {
            uint64_t now = clockGet();
            uint64_t elapsed = now - obs->delay_req_system_time;
            // double interval_s = 1 << obs->delay_req_logMessageInterval;
            if (elapsed / (double)CLOCK_TICKS_PER_S >= 1.0) { // Send DELAY_REQ every second @@@@ TODO: use logMessageInterval
                observerSendDelayRequest(ptp, obs);
            }
        }
#endif

        mutexUnlock(&obs->mutex);
    }

    return true;
}

tPtpObserver *ptpCreateObserver(tPtp *ptp, const char *name, bool active_mode, uint8_t target_domain, const uint8_t *target_uuid, const uint8_t *target_addr) {

    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    if (ptp->observer_count >= PTP_MAX_OBSERVERS) {
        DBG_PRINT_ERROR("PTP observer limit reached\n");
        return NULL;
    }

    // Create observer instance
    tPtpObserver *obs = (tPtpObserver *)malloc(sizeof(tPtpObserver));
    memset(obs, 0, sizeof(tPtpObserver));
    strncpy(obs->name, name, sizeof(obs->name) - 1);
    obs->activeMode = active_mode;
    obs->delay_req_burst_count = 0;

    // Grandmaster info
    obs->gmValid = false;
    obs->gm_last_update_time = 0;
    obs->target_domain = target_domain;
    if (target_uuid != NULL)
        memcpy(obs->target_uuid, target_uuid, 8);
    if (target_addr != NULL)
        memcpy(obs->target_addr, target_addr, 4);

    // If in active mode , generate a random client UUID
    // TODO: use MAC address or other unique identifier
    if (obs->activeMode) {
        for (int i = 0; i < 8; i++) {
            obs->client_uuid[i] = (uint8_t)(rand() & 0xFF);
        }
    }

    // Parameters
    obs->params = &observer_params;

    mutexInit(&obs->mutex, true, 1000); // Protect this observer instance state

    // XCP instrumentation
#ifdef OPTION_ENABLE_XCP

    // Create an individual XCP event for measurement of this instance
    obs->xcp_event = XcpCreateEvent(obs->name, 0, 0);
    assert(obs->xcp_event != XCP_UNDEFINED_EVENT_ID);

    // Create observer parameters
    // All observers share the same calibration segment
    obs->xcp_cal_seg = XcpCreateCalSeg("observer_params", &observer_params, sizeof(observer_params));
    assert(obs->xcp_cal_seg != XCP_UNDEFINED_CALSEG);
    obs->params = (tPtpObserverParameters *)XcpLockCalSeg(obs->xcp_cal_seg); // Initial lock of the calibration segment (to enable calibration persistence)

    A2lOnce() {

        A2lSetSegmentAddrMode(obs->xcp_cal_seg, observer_params);
        A2lCreateParameter(observer_params.t1_correction, "Correction for t1", "", -100, 100);
        A2lCreateParameter(observer_params.delay_req_burst_len, "Number of DELAY_REQ per SYNC", "", 1, 10);
        A2lCreateParameter(observer_params.avg_drift_filter_size, "Drift filter size", "", 1, 300);
        A2lCreateParameter(observer_params.linreg_filter_size, "Linear regression filter size", "", 1, 300);
        A2lCreateParameter(observer_params.linreg_offset_filter_size, "Linear regression offset filter size", "", 1, 300);
        A2lCreateParameter(observer_params.linreg_jitter_filter_size, "Linear regression jitter filter size", "", 1, 300);
#ifdef OBSERVER_SERVO
        A2lCreateParameter(observer_params.max_correction, "Maximum correction per cycle", "ns", 0.0, 1000.0);
        A2lCreateParameter(observer_params.servo_p_gain, "Proportional gain for servo", "", 0.0, 1.0);
#endif
        A2lCreateParameter(observer_params.jitter_rms_filter_size, "Jitter RMS filter size", "", 1.0, 300.0);
        A2lCreateParameter(observer_params.jitter_avg_filter_size, "Jitter average filter size", "", 1.0, 300.0);
    }

    // Create observer measurements
    // Each observer has its own set of measurements by relative addressing mode on the observer instance address
    tPtpObserver o;                                                    // Temporary instance for address calculations
    A2lSetRelativeAddrMode__i(obs->xcp_event, 0, (const uint8_t *)&o); // Use base address index 0
    A2lCreateMeasurementInstance(obs->name, o.gm.domain, "domain");
    A2lCreateMeasurementArrayInstance(obs->name, o.gm.uuid, "Grandmaster UUID");
    A2lCreateMeasurementArrayInstance(obs->name, o.gm.addr, "Grandmaster IP address");
    A2lCreateMeasurementInstance(obs->name, o.sync_local_time, "SYNC RX timestamp");
    A2lCreateMeasurementInstance(obs->name, o.sync_master_time, "SYNC timestamp");
    A2lCreatePhysMeasurementInstance(obs->name, o.sync_correction, "SYNC correction", "ns", 0, 1000000);
    A2lCreateMeasurementInstance(obs->name, o.sync_sequenceId, "SYNC sequence counter");
    A2lCreateMeasurementInstance(obs->name, o.sync_steps, "SYNC mode");
    A2lCreatePhysMeasurementInstance(obs->name, o.sync_cycle_time, "SYNC cycle time", "ns", 999999900, 1000000100);
    A2lCreateMeasurementInstance(obs->name, o.flup_master_time, "FOLLOW_UP timestamp");
    A2lCreateMeasurementInstance(obs->name, o.flup_sequenceId, "FOLLOW_UP sequence counter");
    A2lCreatePhysMeasurementInstance(obs->name, o.flup_correction, "FOLLOW_UP correction", "ns", 0, 1000000);

    A2lCreatePhysMeasurementInstance(obs->name, o.master_drift, "", "ppm*1000", -100, +100);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_jitter, "offset jitter raw value", "ns", -1000, +1000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_jitter_rms, "Jitter root mean square", "ns", -1000, +1000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_jitter_avg, "Jitter average", "ns", -1000, +1000);
    A2lCreatePhysMeasurementInstance(obs->name, o.path_delay, "Path delay", "ns", 0, 1000000000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_offset, "Master offset", "ns", -1000000000, +1000000000);
    A2lCreateMeasurementArrayInstance(obs->name, o.offset_to, "Offset to other observers");
    A2lCreateMeasurementArrayInstance(obs->name, o.drift_to, "Offset to other observers");

#endif

    observerReset(obs);

    // Register the observer instance
    ptp->observer_list[ptp->observer_count++] = obs;

    if (ptp_log_level >= 3) {
        printf("Created PTP observer instance %s, listening on domain %u, addr=%u.%u.%u.%u, uuid=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", obs->name, obs->target_domain,
               obs->target_addr[0], obs->target_addr[1], obs->target_addr[2], obs->target_addr[3], obs->target_uuid[0], obs->target_uuid[1], obs->target_uuid[2],
               obs->target_uuid[3], obs->target_uuid[4], obs->target_uuid[5], obs->target_uuid[6], obs->target_uuid[7]);
        if (obs->activeMode) {
            printf("    Client UUID: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", obs->client_uuid[0], obs->client_uuid[1], obs->client_uuid[2], obs->client_uuid[3],
                   obs->client_uuid[4], obs->client_uuid[5], obs->client_uuid[6], obs->client_uuid[7]);
        }
    }

    return obs;
}

void ptpObserverShutdown(tPtpObserver *obs) {
    assert(obs != NULL);

    // XCP instrumentation
#ifdef OPTION_ENABLE_XCP
    XcpUnlockCalSeg(obs->xcp_cal_seg);
#endif

    mutexDestroy(&obs->mutex);
    free(obs);
}

#ifdef PTP_OBSERVER_LIST

// Write the observer list to a simple ASCII file, where each line contains one observer index, name, domain, uuid and addr
bool ptpSaveObserverList(tPtp *ptp, const char *filename) {

    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    FILE *f = fopen(filename, "w");
    if (f == NULL) {
        DBG_PRINT_ERROR("Failed to open observer list file for writing\n");
        return false;
    }

    for (int i = 0; i < ptp->observer_count; i++) {
        tPtpObserver *obs = ptp->observer_list[i];
        fprintf(f, "%u: %s %u %02X%02X%02X%02X%02X%02X%02X%02X %u.%u.%u.%u\n", i, obs->name, obs->target_domain, obs->target_uuid[0], obs->target_uuid[1], obs->target_uuid[2],
                obs->target_uuid[3], obs->target_uuid[4], obs->target_uuid[5], obs->target_uuid[6], obs->target_uuid[7], obs->target_addr[0], obs->target_addr[1],
                obs->target_addr[2], obs->target_addr[3]);
    }

    fclose(f);
    return true;
}

// Read the observer list from a simple ASCII file, where each line contains one observer index, name, domain, uuid and addr
bool ptpLoadObserverList(tPtp *ptp, const char *filename, bool active_mode) {

    assert(ptp != NULL && ptp->magic == PTP_MAGIC);
    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        DBG_PRINTF_WARNING("Observer list file (%s) not found\n", filename);
        return false;
    }
    char line[128];
    while (fgets(line, sizeof(line), f) != NULL) {
        unsigned int index;
        char name[32];
        unsigned int domain;
        unsigned int uuid[8];
        unsigned int addr[4];
        int ret = sscanf(line, "%u: %31s %u %02X%02X%02X%02X%02X%02X%02X%02X %u.%u.%u.%u", &index, name, &domain, &uuid[0], &uuid[1], &uuid[2], &uuid[3], &uuid[4], &uuid[5],
                         &uuid[6], &uuid[7], &addr[0], &addr[1], &addr[2], &addr[3]);
        if (ret == 15) {
            uint8_t uuid_bytes[8];
            uint8_t addr_bytes[4];
            for (int i = 0; i < 8; i++) {
                uuid_bytes[i] = (uint8_t)uuid[i];
            }
            for (int i = 0; i < 4; i++) {
                addr_bytes[i] = (uint8_t)addr[i];
            }
            tPtpObserver *obs = ptpCreateObserver(ptp, name, active_mode, domain, uuid_bytes, addr_bytes);
            if (obs == NULL) {
                DBG_PRINT_ERROR("Failed to create observer from list\n");
            }
        } else {
            DBG_PRINTF_ERROR("Invalid line in observer list file (%u): %s\n", ret, line);
        }
    }
    fclose(f);
    return true;
}

#endif

#endif // OPTION_PTP_OBSERVER