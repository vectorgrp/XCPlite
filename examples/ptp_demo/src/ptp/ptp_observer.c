/*----------------------------------------------------------------------------
| File:
|   ptp_master.c
|
| Description:
|   PTP master with XCP instrumentation
|   For testing PTP client stability
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

#include "dbg_print.h" // for DBG_PRINT_ERROR, DBG_PRINTF_WARNING, ...
#include "filter.h"    // for average filter
#include "platform.h"  // from xcplib for SOCKET, socketSendTo, socketGetSendTime, ...

#include "ptp.h"
#include "ptpHdr.h" // PTP protocol message structures
#include "ptp_observer.h"

#ifdef OPTION_ENABLE_XCP

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

#endif

//---------------------------------------------------------------------------------------

// Default observer parameter values
static const observer_parameters_t observer_params = { //
    .reset = 0,
    .t1_correction = 0, // Apply 3ns correction to t1 to compensate for VN56xx master timestamp rounding
    .drift_filter_size = 30,
    .jitter_rms_filter_size = 30,
    .jitter_avg_filter_size = 30,
#ifdef OBSERVER_LINREG
    .linreg_filter_size = 30,
    .linreg_offset_filter_size = 10,
    .linreg_jitter_filter_size = 30,
#endif
#ifdef OBSERVER_SERVO
    .max_correction = 1000.0, // 1000ns maximum correction per SYNC interval
    .servo_p_gain = 1.0
#endif

};

//---------------------------------------------------------------------------------------
// PTP observer initialization

// Reset the PTP observer state
void observerReset(tPtpObserver *obs) {
    assert(obs != NULL);

    // Init protocol state
    obs->sync_local_time = 0;  // Local receive timestamp of SYNC
    obs->sync_master_time = 0; // SYNC timestamp
    obs->sync_correction = 0;  // SYNC correction
    obs->sync_sequenceId = 0;  // SYNC sequence Id
    obs->sync_cycle_time = 0;  // Master SYNC cycle time
    obs->sync_steps = 0;       // SYNC steps removed
    obs->flup_master_time = 0; // FOLLOW_UP timestamp
    obs->flup_correction = 0;  // FOLLOW_UP correction
    obs->flup_sequenceId = 0;  // FOLLOW_UP sequence Id

    // Init timing analysis state
    obs->cycle_count = 0; // SYNC cycle counter
    obs->master_sync = false;
    obs->t1 = 0;
    obs->t2 = 0;
    obs->master_offset_raw = 0;
    obs->t1_offset = 0;
    obs->t2_offset = 0;
    obs->t1_norm = 0;
    obs->t2_norm = 0;
    obs->master_offset_norm = 0;
    obs->master_drift_raw = 0;
    obs->master_drift_drift = 0;
    obs->master_drift = 0;
    average_filter_init(&obs->master_drift_filter, obs->params->drift_filter_size);
    average_filter_init(&obs->master_drift_drift_filter, obs->params->drift_filter_size);

#ifdef OBSERVER_LINREG
    obs->linreg_drift = 0.0;
    obs->linreg_offset = 0.0;
    obs->linreg_offset_avg = 0.0;
    obs->linreg_drift_drift = 0.0;
    obs->linreg_jitter = 0.0;
    obs->linreg_jitter_avg = 0.0;
    linreg_filter_init(&obs->linreg_filter, obs->params->linreg_filter_size);
    average_filter_init(&obs->linreg_offset_filter, obs->params->linreg_offset_filter_size);
    average_filter_init(&obs->linreg_jitter_filter, obs->params->linreg_jitter_filter_size);
#endif

#ifdef OBSERVER_SERVO
    obs->master_drift_raw = 0;
    obs->master_offset_compensation = 0;
    obs->master_offset_detrended = 0;
    obs->master_offset_detrended_filtered = 0;
#endif

    obs->master_jitter = 0;
    average_filter_init(&obs->master_jitter_rms_filter, obs->params->jitter_rms_filter_size);
    obs->master_jitter_rms = 0;
    average_filter_init(&obs->master_jitter_avg_filter, obs->params->jitter_avg_filter_size);
    obs->master_jitter_avg = 0;
}

// Initialize the PTP observer state
void observerInit(tPtpObserver *obs, uint8_t domain, const uint8_t *uuid, const uint8_t *addr) {

    assert(obs != NULL);

    obs->params = &observer_params;

    // XCP instrumentation
#ifdef OPTION_ENABLE_XCP

    // Create an individual XCP event for measurement of this instance
    obs->xcp_event = XcpCreateEvent(obs->name, 0, 0);
    assert(obs->xcp_event != XCP_UNDEFINED_EVENT_ID);

    // Create observer parameters
    // All observers share the same calibration segment
    tXcpCalSegIndex h = XcpCreateCalSeg("observer_params", &observer_params, sizeof(observer_params));
    assert(h != XCP_UNDEFINED_CALSEG);
    obs->params = (const observer_parameters_t *)XcpLockCalSeg(h); // Initial lock of the calibration segment (to enable calibration persistence)

    A2lOnce() {

        A2lSetSegmentAddrMode(h, observer_params);
        A2lCreateParameter(observer_params.reset, "Reset PTP observer state", "", 0, 1);
        A2lCreateParameter(observer_params.t1_correction, "Correction for t1", "", -100, 100);
        A2lCreateParameter(observer_params.drift_filter_size, "Drift filter size", "", 1, 300);
#ifdef OBSERVER_LINREG
        A2lCreateParameter(observer_params.linreg_filter_size, "Linear regression filter size", "", 1, 300);
        A2lCreateParameter(observer_params.linreg_offset_filter_size, "Linear regression offset filter size", "", 1, 300);
        A2lCreateParameter(observer_params.linreg_jitter_filter_size, "Linear regression jitter filter size", "", 1, 300);
#endif
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

    A2lCreatePhysMeasurementInstance(obs->name, o.t1_norm, "t1 normalized to startup reference time t1_offset", "ns", 0, +1000000);
    A2lCreatePhysMeasurementInstance(obs->name, o.t2_norm, "t2 normalized to startup reference time t2_offset", "ns", 0, +1000000);

    A2lCreatePhysMeasurementInstance(obs->name, o.master_drift, "", "ppm*1000", -100, +100);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_jitter, "offset jitter raw value", "ns", -1000, +1000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_jitter_rms, "Jitter root mean square", "ns", -1000, +1000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_jitter_avg, "Jitter average", "ns", -1000, +1000);

#ifdef OBSERVER_SERVO
    A2lCreatePhysMeasurementInstance(obs->name, o.master_drift_raw, "", "ppm*1000", -100, +100);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_drift_drift, "", "ppm*1000", -10, +10);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_offset_raw, "t1-t2 raw value (not used)", "ns", -1000000, +1000000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_offset_compensation, "offset for detrending", "ns", -1000, +1000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_offset_detrended, "detrended master offset", "ns", -1000, +1000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_offset_detrended_filtered, "filtered detrended master offset", "ns", -1000, +1000);
#endif

    A2lCreateMeasurementArrayInstance(obs->name, o.offset_to, "Offset to other observers");
    A2lCreateMeasurementArrayInstance(obs->name, o.drift_to, "Offset to other observers");

#endif

    // Grandmaster info
    obs->gmValid = false;
    obs->gm_last_update_time = 0;
    obs->domain = domain;
    if (uuid != NULL)
        memcpy(obs->uuid, uuid, 8);
    if (addr != NULL)
        memcpy(obs->addr, addr, 4);

    observerReset(obs);
}

// Print information on the grandmaster
static void observerPrintMaster(const tPtpObserver *obs) {
    assert(obs != NULL);
    const tPtpObserverMaster *m = &obs->gm;

    printf("    PTP Master:\n");
    const char *timesource = (m->a.timeSource == PTP_TIME_SOURCE_INTERNAL) ? "internal oscilator" : (m->a.timeSource == PTP_TIME_SOURCE_GPS) ? "GPS" : "Unknown";
    printf("      domain=%u, addr=%u.%u.%u.%u, id=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n"
           "      timesource=%s (%02X), utcOffset=%u, prio1=%u, class=%u, acc=%u, var=%u, prio2=%u, steps=%u\n",
           m->domain, m->addr[0], m->addr[1], m->addr[2], m->addr[3], m->uuid[0], m->uuid[1], m->uuid[2], m->uuid[3], m->uuid[4], m->uuid[5], m->uuid[6], m->uuid[7], timesource,
           m->a.timeSource, htons(m->a.utcOffset), m->a.priority1, m->a.clockClass, m->a.clockAccuraccy, htons(m->a.clockVariance), m->a.priority2, htons(m->a.stepsRemoved));
}

// Print the current PTP observer state
void observerPrintState(tPtp *ptp, tPtpObserver *obs) {

    printf("  Observer '%s' (%u):\n", obs->name, obs->cycle_count);
    if (obs->gmValid) {
        observerPrintMaster(obs);
        printf("    drift        = %g ns/s\n", obs->master_drift);
        printf("    drift_drift  = %g ns/s2\n", obs->master_drift_drift);
        printf("    jitter       = %g ns\n", obs->master_jitter);
        printf("    jitter_avg   = %g ns\n", obs->master_jitter_avg);
        printf("    jitter_rms   = %g ns\n", obs->master_jitter_rms);
        for (int j = 0; j < ptp->observer_count; j++) {
            tPtpObserver *obs_other = ptp->observer_list[j];
            if (obs != obs_other && obs_other->gmValid) {
                printf(".   to %s:\n", obs_other->name);
                printf("      offset diff: %" PRIi64 " ns (%g ms)\n", obs->offset_to[j], obs->offset_to[j] / 1000000.0);
                printf("      drift diff: %g ppm\n", obs->drift_to[j] / 1000.0);
            }
        }

        printf("\n");
    } else {
        printf("    No active PTP master detected\n");
    }
}

// Update the PTP observer state with each new SYNC (t1,t2) timestamps
static void observerUpdate(tPtp *ptp, tPtpObserver *obs, uint64_t t1_in, uint64_t correction, uint64_t t2_in) {

    assert(obs != NULL);

    char ts1[64], ts2[64];

    // Update observer parameters (update XCP calibrations)
    // Single threaded access assumed, called from ptpThread319 (1 step mode) or ptpThread320 (2 step mode) only
#ifdef OPTION_ENABLE_XCP
    // Each instance holds its lock continuously, so it may take about a second to make calibration changes effective
    XcpUpdateCalSeg((void **)&obs->params);
#endif

    obs->cycle_count++;

    if (obs->log_level >= 2)
        printf("Observer %s: PTP SYNC cycle %u:\n", obs->name, obs->cycle_count);
    if (obs->log_level >= 3) {
        printf("  t1 (SYNC tx on master (via PTP))  = %s (%" PRIu64 ") (%08X)\n", clockGetString(ts1, sizeof(ts1), t1_in), t1_in, (uint32_t)t1_in);
        printf("  t2 (SYNC rx)  = %s (%" PRIu64 ") (%08X)\n", clockGetString(ts2, sizeof(ts2), t2_in), t2_in, (uint32_t)t2_in);
        printf("  correction    = %" PRIu64 "ns\n", correction);
        printf("  cycle time    = %" PRIu64 "ns\n", obs->sync_cycle_time);
    }

    // Master timestamps with correction applied
    uint64_t t1 = t1_in;
    // Apply rounding correction to t1 ( Vector VN/VX PTP master has 8ns resolution, which leads to a systematic error )
    t1 += obs->params->t1_correction;
    // Apply clock correction to t1
    t1 += correction;
    obs->t1 = t1;

    // Local timestamp
    uint64_t t2 = t2_in;
    obs->t2 = t2;

    // Master offset raw value
    obs->master_offset_raw = (int64_t)(t1 - t2); // Positive master_offset means master is ahead
    if (obs->log_level >= 4) {
        printf("    master_offset_raw = %" PRIi64 " ns\n", obs->master_offset_raw);
    }

    // First round, init
    if (obs->t1_offset == 0 || obs->t2_offset == 0) {

        obs->t1_norm = 0; // corrected sync tx time on master clock
        obs->t2_norm = 0; // sync rx time on slave clock

        // Normalization time offsets for t1,t2
        obs->t1_offset = t1;
        obs->t2_offset = t2;

#ifdef OBSERVER_SERVO
        obs->master_offset_compensation = 0;
#endif

        if (obs->log_level >= 3)
            printf("  Initial offsets: t1_offset = %" PRIu64 " ns, t2_offset = %" PRIu64 " ns\n", obs->t1_offset, obs->t2_offset);
    }

    // Analysis
    else {

        // Normalize t1,t2 to first round start values
        // Avoid conversion loss, when using double precision floating point calculations later
        // The largest uint64_t value that can be converted to double without loss of precision is 2^53 (9,007,199,254,740,992).
        int64_t t1_norm = (int64_t)(t1 - obs->t1_offset);
        int64_t t2_norm = (int64_t)(t2 - obs->t2_offset);
        assert(t1_norm >= 0 && t2_norm >= 0);                                 // Should never happen
        assert((t2_norm < 0x20000000000000) && (t1_norm < 0x20000000000000)); // Should never happen (loss of precision after ≈ 104.25 days

        if (obs->log_level >= 4)
            printf("  Normalized time: t1_norm = %" PRIi64 " ns, t2_norm = %" PRIi64 " ns\n", t1_norm, t2_norm);

        // Calculate momentary master offset
        obs->master_offset_norm = t1_norm - t2_norm; // Positive master_offset means master is ahead
        if (obs->log_level >= 4) {
            printf("    master_offset_norm = %" PRIi64 " ns\n", obs->master_offset_norm);
        }

        // Drift calculation by floating average method
        int64_t c1 = t1_norm - obs->t1_norm; // time since last sync on master clock
        int64_t c2 = t2_norm - obs->t2_norm; // time since last sync on local clock
        obs->sync_cycle_time = c2;           // Update last cycle time
        int64_t diff = c1 - c2;              // Positive diff = master clock faster than local clock
        double master_drift = 0.0;
        double master_drift_drift = 0.0;
        if (diff < -200000 || diff > +200000) { // Plausibility checking of cycle drift (max 200us per cycle)
            printf("WARNING: Master drift too high! dt=%lld ns \n", diff);
        } else {
            obs->master_drift_raw = (double)diff * 1000000000 / c2; // Calculate drift in ppm instead of drift per cycle (drift is in ns/s (1/1000 ppm)
            master_drift = average_filter_calc(&obs->master_drift_filter, obs->master_drift_raw); // Filter drift
            master_drift_drift = master_drift - obs->master_drift; // Calculate drift of drift in ns/(s*s) (should be close to zero when temperature is stable )
            obs->master_drift_drift = master_drift_drift;
            obs->master_drift = master_drift;
        }
        if (obs->log_level >= 3) {
            printf("  Average filtered drift calculation results at t2 = %lld ns: \n", t2_norm);
            printf("    master_drift        = %g ns/s\n", obs->master_drift);
            printf("    master_drift_drift  = %g ns/s2\n", obs->master_drift_drift);
        }

#ifdef OBSERVER_LINREG
        // Drift, drift of drift and jitter calculation by linear regression method
        // Drift of drift in ns/(s*s) (should be close to zero when temperature is stable )
        double linreg_slope = 0.0;
        double linreg_offset = 0.0;
        linreg_filter_calc(&obs->linreg_filter, (double)t2_norm, (double)t1_norm, &linreg_slope, &linreg_offset);
        double linreg_drift = (linreg_slope - 1.0) * 1E9;
        obs->linreg_drift_drift = linreg_drift - obs->linreg_drift;
        obs->linreg_drift = linreg_drift;
        obs->linreg_offset = linreg_offset;

        // Jitter is the deviation from the linear regression line at t2 !!!!
        // While drift of drift is not zero, linreg_offset is not jitter!
        // Calculate average linreg offset to compensate offset for jitter calculation, avarage of jitter should be zero
        obs->linreg_offset_avg = average_filter_calc(&obs->linreg_offset_filter, linreg_offset);
        obs->linreg_jitter = linreg_offset - obs->linreg_offset_avg;
        obs->linreg_jitter_avg = average_filter_calc(&obs->linreg_jitter_filter, obs->linreg_jitter);

        if (obs->log_level >= 3) {
            printf("  Linear regression results at t2 = %lld ns: \n", t2_norm);
            printf("    linreg drift = %g ns/s\n", obs->linreg_drift);
            printf("    linreg drift_drift = %g ns/s2\n", obs->linreg_drift_drift);
            printf("    linreg offset = %g ns\n", obs->linreg_offset);
            printf("    linreg offset average = %g ns\n", obs->linreg_offset_avg);
            printf("    linreg jitter = %g ns\n", obs->linreg_jitter);
            printf("    linreg jitter average = %g ns\n", obs->linreg_jitter_avg);
        }

        // Use the linear regression results when master is synchronized
        if (obs->master_sync) {
            obs->master_drift = obs->linreg_drift;
            obs->master_drift_drift = obs->linreg_drift_drift;
            obs->master_jitter = obs->linreg_jitter;                              // Jitter is the deviation from the linear regression line !!!!
        } else if (obs->cycle_count > 8 && fabs(obs->linreg_drift_drift) < 5.0) { // @@@@ TODO: parameterize in sync condition
            obs->master_sync = true;
        } else {
            if (obs->log_level >= 2)
                printf("Observer %s: Warming up\n", obs->name);
        }

#endif

#if OBSERVER_SERVO

        if (obs->master_offset_compensation == 0) {
            // Initialize compensation
            obs->master_offset_compensation = obs->master_offset_norm;
        } else {
            // Compensate drift
            // obs->master_offset_compensation -= ((obs->master_drift) * (double)obs->sync_cycle_time) / 1000000000;
            // Compensate drift and drift of drift
            double n = average_filter_count(&obs->master_drift_filter) / 2;
            obs->master_offset_compensation -= ((obs->master_drift + obs->master_drift_drift * n) * (double)obs->sync_cycle_time) / 1000000000;
        }
        obs->master_offset_detrended = (double)obs->master_offset_norm - obs->master_offset_compensation;
        obs->master_offset_detrended_filtered = average_filter_calc(&obs->master_jitter_avg_filter, obs->master_offset_detrended);
        if (obs->log_level >= 4) {
            printf("    master_offset_comp  = %g ns\n", obs->master_offset_compensation);
        }
        if (obs->log_level >= 3) {
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
        if (obs->log_level >= 5)
            printf("Applied compensation correction: %g ns\n", correction);

        obs->master_jitter = obs->master_offset_detrended; // Jitter is the unfiltered detrended master offset
#endif

        // Jitter analysis
        obs->master_jitter_avg = average_filter_calc(&obs->master_jitter_avg_filter, obs->master_jitter);                                    // Filter jitter
        obs->master_jitter_rms = sqrt((double)average_filter_calc(&obs->master_jitter_rms_filter, obs->master_jitter * obs->master_jitter)); // Filter jitter and calculate RMS
        if (obs->log_level >= 2) {
            printf("  Jitter analysis:\n");
            printf("    master_jitter       = %g ns\n", obs->master_jitter);
            printf("    master_jitter_avg   = %g ns\n", obs->master_jitter_avg);
            printf("    master_jitter_rms   = %g ns\n", obs->master_jitter_rms);
        }

        // Calculate offsets to other observers with different master clocks
        if (ptp->observer_count > 1) {
            printf("  Comparisons to other observers:\n");
            for (int j = 0; j < ptp->observer_count; j++) {
                tPtpObserver *obs_other = ptp->observer_list[j];
                if (obs != obs_other && obs_other->gmValid) {
                    double dt = (int64_t)(obs->t2 - obs_other->t2);
                    if (obs->t1 > obs_other->t1) {
                        obs->offset_to[j] =
                            (double)(obs->t1 - obs_other->t1) - dt - (((double)dt * obs_other->linreg_drift) / 1000000000.0); // Compensate for different local receive times
                        obs->drift_to[j] = obs->master_drift - obs_other->master_drift;
                        if (obs->log_level >= 2) {
                            printf("    offset to %s: %" PRIi64 " ns (%g ms)\n", obs_other->name, obs->offset_to[j], obs->offset_to[j] / 1000000.0);
                            printf("    drift diff to %s: %g ns/s\n", obs_other->name, obs->drift_to[j]);
                        }
                    }
                }
            }
        }

        if (obs->log_level >= 2)
            printf("\n");

        // Remember last normalized input values
        obs->t1_norm = t1_norm; // sync tx time on master clock
        obs->t2_norm = t2_norm; // sync rx time on slave clock
    }

// XCP measurement event (relative addressing mode to observer instance)
#ifdef OPTION_ENABLE_XCP
    XcpEventExt_Var(obs->xcp_event, 1, (const uint8_t *)obs); // Base address 0 (addr ext = 2) is observer instance
#endif
}

bool observerHandleFrame(tPtp *ptp, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t timestamp) {

    if (!(n >= 44 && n <= 64)) {
        DBG_PRINT_ERROR("Invalid PTP message size\n");
        return false; // PTP message too small or too large
    }

    for (int i = 0; i < ptp->observer_count; i++) {
        tPtpObserver *obs = ptp->observer_list[i];

        // Check if this observer is locked onto a grandmaster
        if (obs->gmValid) {

            // Check if SYNC or FOLLOW_UP match this observers master
            if (obs->domain == ptp_msg->domain && (memcmp(obs->gm.uuid, ptp_msg->clockId, 8) == 0) && (memcmp(obs->gm.addr, addr, 4) == 0)) {

                if (obs->gmValid && (ptp_msg->type == PTP_SYNC || ptp_msg->type == PTP_FOLLOW_UP)) {

                    obs->gm_last_update_time = clockGet(); // Last update in system time

                    if (ptp_msg->type == PTP_SYNC) {
                        if (timestamp == 0) {
                            DBG_PRINTF_WARNING("Observer %s: PTP SYNC received without timestamp!\n", obs->name);
                            return false;
                        }
                        obs->sync_local_time = timestamp;
                        obs->sync_master_time = htonl(ptp_msg->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp_msg->timestamp.timestamp_ns);
                        obs->sync_sequenceId = htons(ptp_msg->sequenceId);
                        obs->sync_correction = (uint32_t)(htonll(ptp_msg->correction) >> 16);
                        obs->sync_steps = (htons(ptp_msg->flags) & PTP_FLAG_TWO_STEP) ? 2 : 1;

                        // 1 step sync update
                        if (obs->sync_steps == 1) {
                            observerUpdate(ptp, obs, obs->sync_master_time, obs->sync_correction, obs->sync_local_time);
                        }
                    } else {

                        obs->flup_master_time = htonl(ptp_msg->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp_msg->timestamp.timestamp_ns);
                        obs->flup_sequenceId = htons(ptp_msg->sequenceId);
                        obs->flup_correction = (uint32_t)(htonll(ptp_msg->correction) >> 16);
                    }

                    // 2 step sync update, SYNC and FOLLOW_UP may be received in any order (thread319 and thread320)
                    if (obs->sync_steps == 2 && obs->sync_sequenceId == obs->flup_sequenceId) {
                        observerUpdate(ptp, obs, obs->flup_master_time, obs->sync_correction, obs->sync_local_time); // 2 step
                    }
                }

                return true; // Processed by this observer
            } // match

        }

        // Not yet locked onto a grandmaster
        else {
            // Check if Announce from any master match this observers master filter
            if (ptp_msg->type == PTP_ANNOUNCE) {

                // Check if domain, uuid (if specified) and addr (if specified) match
                if ((obs->domain == ptp_msg->domain) && (memcmp(obs->uuid, ptp_msg->clockId, 8) == 0 || memcmp(obs->uuid, "\0\0\0\0\0\0\0\0", 8) != 0) &&
                    (memcmp(obs->addr, addr, 4) == 0 || memcmp(obs->addr, "\0\0\0", 4) == 0)) {
                    if (ptp->log_level >= 1) {
                        printf("PTP Announce received from a master matching observer '%s' filter\n", obs->name);
                    }
                    obs->gmValid = true;
                    obs->gm.a = ptp_msg->u.a;
                    memcpy(obs->gm.addr, addr, 4);
                    memcpy(obs->gm.uuid, ptp_msg->clockId, 8);
                    observerReset(obs); // Reset observer state
                    observerPrintMaster(obs);
                    obs->gm_last_update_time = clockGet(); // Timeout from now
                    return true;                           // Locked onto a grandmaster
                }
            }
        }
    }

    // Check announce messages from unknown masters
    if (ptp_msg->type == PTP_ANNOUNCE) {
        if (ptp->auto_observer_enabled) {
            // Create new observer for this master
            // Generate a name
            char name[32];
            snprintf(name, sizeof(name), "obs_%u.%u_%u", addr[2], addr[3], ptp_msg->domain);
            tPtpObserverHandle ptpObs = ptpCreateObserver(name, ptp, ptp_msg->domain, ptp_msg->clockId, addr);
        }
    }

    return true;
}

bool observerTask(tPtp *ptp, tPtpObserver *observer) {
    // Check for grandmaster timeout
    if (observer->gmValid) {
        uint64_t now = clockGet();
        uint64_t elapsed = now - observer->gm_last_update_time;
        if (elapsed / (double)CLOCK_TICKS_PER_S > PTP_OBSERVER_GM_TIMEOUT_S) {
            if (observer->log_level >= 1) {
                printf("PTP Observer %s: Grandmaster lost! timeout after %us. Last seen %gs ago\n", observer->name, PTP_OBSERVER_GM_TIMEOUT_S, elapsed / (double)CLOCK_TICKS_PER_S);
            }
            observer->gmValid = false;
        }
    }
    return true;
}

tPtpObserverHandle ptpCreateObserver(const char *name, tPtpInterfaceHandle ptp_handle, uint8_t domain, const uint8_t *uuid, const uint8_t *addr) {

    tPtp *ptp = (tPtp *)ptp_handle;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    if (ptp->observer_count >= PTP_MAX_OBSERVERS) {
        DBG_PRINT_ERROR("PTP observer limit reached\n");
        return NULL;
    }

    // Create observer instance
    tPtpObserver *obs = (tPtpObserver *)malloc(sizeof(tPtpObserver));
    memset(obs, 0, sizeof(tPtpObserver));
    strncpy(obs->name, name, sizeof(obs->name) - 1);
    obs->log_level = ptp->log_level;
    observerInit(obs, domain, uuid, addr); // Initialize observer state
    obs->gm_last_update_time = clockGet(); // Timeout from now

    // Register the observer instance
    ptp->observer_list[ptp->observer_count++] = obs;

    if (obs->log_level >= 1) {
        printf("Created PTP observer instance %s, listening on domain %u, addr=%u.%u.%u.%u, uuid=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", obs->name, obs->domain, obs->addr[0],
               obs->addr[1], obs->addr[2], obs->addr[3], obs->uuid[0], obs->uuid[1], obs->uuid[2], obs->uuid[3], obs->uuid[4], obs->uuid[5], obs->uuid[6], obs->uuid[7]);
    }

    return (tPtpObserverHandle)obs;
}

// Write the observer list to a simple ASCII file, where each line contains one observer index, name, domain, uuid and addr
bool ptpSaveObserverList(tPtpInterfaceHandle ptp_handle, const char *filename) {
    tPtp *ptp = (tPtp *)ptp_handle;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    FILE *f = fopen(filename, "w");
    if (f == NULL) {
        DBG_PRINT_ERROR("Failed to open observer list file for writing\n");
        return false;
    }

    for (int i = 0; i < ptp->observer_count; i++) {
        tPtpObserver *obs = ptp->observer_list[i];
        fprintf(f, "%u: %s %u %02X%02X%02X%02X%02X%02X%02X%02X %u.%u.%u.%u\n", i, obs->name, obs->domain, obs->uuid[0], obs->uuid[1], obs->uuid[2], obs->uuid[3], obs->uuid[4],
                obs->uuid[5], obs->uuid[6], obs->uuid[7], obs->addr[0], obs->addr[1], obs->addr[2], obs->addr[3]);
    }

    fclose(f);
    return true;
}

// Read the observer list from a simple ASCII file, where each line contains one observer index, name, domain, uuid and addr
bool ptpLoadObserverList(tPtpInterfaceHandle ptp_handle, const char *filename) {
    tPtp *ptp = (tPtp *)ptp_handle;
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
            tPtpObserverHandle obs_handle = ptpCreateObserver(name, ptp_handle, domain, uuid_bytes, addr_bytes);
            if (obs_handle == NULL) {
                DBG_PRINT_ERROR("Failed to create observer from list\n");
            }
        } else {
            DBG_PRINTF_ERROR("Invalid line in observer list file (%u): %s\n", ret, line);
        }
    }
    fclose(f);
    return true;
}
