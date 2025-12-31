/*----------------------------------------------------------------------------
| File:
|   ptp.c
|
| Description:
|   PTP observer and master with XCP instrumentation
|   For analyzing PTP masters and testing PTP client stability
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

//-------------------------------------------------------------------------------------------------------
// XCP

#ifdef OPTION_ENABLE_XCP

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

#endif

//-------------------------------------------------------------------------------------------------------
// PTP state

// Forward declarations
struct ptp_observer;
struct ptp_master;

#define PTP_MAGIC 0x50545021 // "PTP!"

struct ptp {

    uint32_t magic; // Magic number for validation

    // Sockets and communication
    uint8_t if_addr[4]; // local addr
    char if_name[32];   // network interface name
    uint8_t maddr[4];   // multicast addr
    THREAD threadHandle320;
    THREAD threadHandle319;
    SOCKET sock320;
    SOCKET sock319;
    MUTEX mutex;

    uint8_t log_level;
    bool auto_observer_enabled;

    struct ptp_master *master_list;
    struct ptp_observer *observer_list;
};
typedef struct ptp tPtp;

//-------------------------------------------------------------------------------------------------------

// Print a PTP frame
static void printFrame(char *prefix, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t rx_timestamp) {

    const char *s = NULL;
    switch (ptp_msg->type) {
    case PTP_ANNOUNCE:
        s = "ANNOUNCE";
        break;
    case PTP_SYNC:
        s = "SYNC";
        break;
    case PTP_FOLLOW_UP:
        s = "FOLLOW_UP";
        break;
    case PTP_DELAY_REQ:
        s = "DELAY_REQ";
        break;
    case PTP_DELAY_RESP:
        s = "DELAY_RESP";
        break;
    case PTP_PDELAY_REQ:
        s = "PDELAY_REQ";
        break;
    case PTP_PDELAY_RESP:
        s = "PDELAY_RESP";
        break;
    case PTP_PDELAY_RESP_FOLLOW_UP:
        s = "PDELAY_RESP_FOLLOW_UP";
        break;
    case PTP_SIGNALING:
        s = "SIGNALING";
        break;
    case PTP_MANAGEMENT:
        s = "MANAGEMENT";
        break;
    default:
        s = "UNKNOWN";
        break;
    }
    if (s != NULL) {
        printf("%s: %s (seqId=%u, timestamp=%" PRIu64 " from %u.%u.%u.%u - %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", prefix, s, htons(ptp_msg->sequenceId), rx_timestamp, addr[0],
               addr[1], addr[2], addr[3], ptp_msg->clockId[0], ptp_msg->clockId[1], ptp_msg->clockId[2], ptp_msg->clockId[3], ptp_msg->clockId[4], ptp_msg->clockId[5],
               ptp_msg->clockId[6], ptp_msg->clockId[7]);
        if (ptp_msg->type == PTP_DELAY_RESP)
            printf("  to %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", ptp_msg->u.r.clockId[0], ptp_msg->u.r.clockId[1], ptp_msg->u.r.clockId[2], ptp_msg->u.r.clockId[3],
                   ptp_msg->u.r.clockId[4], ptp_msg->u.r.clockId[5], ptp_msg->u.r.clockId[6], ptp_msg->u.r.clockId[7]);
        printf("\n");
    }
}

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
// PTP observer for master timing analysis

// Observer parameter structure
typedef struct {
    uint8_t reset;                  // Reset PTP observer state
    int32_t t1_correction;          // Correction to apply to t1 timestamps
    uint8_t drift_filter_size;      // Size of the drift average filter
    uint8_t jitter_rms_filter_size; // Size of the jitter RMS average filter
    uint8_t jitter_avg_filter_size; // Size of the jitter average filter
    double max_correction;          // Maximum allowed servo correction per SYNC interval
    double servo_p_gain;            // Proportional gain (typically 0.1 - 0.5)
} observer_parameters_t;

// Default observer parameter values
static const observer_parameters_t observer_params = {.reset = 0,
                                                      .t1_correction = 3, // Apply 4ns correction to t1 to compensate for master timestamp rounding
                                                      .drift_filter_size = 30,
                                                      .jitter_rms_filter_size = 30,
                                                      .jitter_avg_filter_size = 30,
                                                      .max_correction = 1000.0, // 1000ns maximum correction per SYNC interval
                                                      .servo_p_gain = 1.0};

// PTP observer master descriptor
typedef struct {
    uint8_t domain;
    uint8_t uuid[8];
    uint8_t addr[4];
    struct announce a; // Announce header from the announce protocol message of this master
} tPtpObserverMaster;

// PTP observer state
struct ptp_observer {

    char name[32];

    // Filter master identification
    uint8_t domain;
    uint8_t uuid[8];
    uint8_t addr[4];

    struct ptp_observer *next; // next observer in list

    uint8_t log_level;

    // Grandmaster info
    bool gmValid; // locked onto a valid grandmaster
    tPtpObserverMaster gm;

    // Protocol SYNC and FOLLOW_UP state
    uint64_t sync_local_time;
    uint64_t sync_master_time;
    uint32_t sync_correction;
    uint16_t sync_sequenceId;
    uint64_t sync_cycle_time;
    uint8_t sync_steps;
    uint64_t flup_master_time;
    uint32_t flup_correction;
    uint16_t flup_sequenceId;

    // PTP observer timing analysis state, all values in nanoseconds and per second units
    uint32_t cycle_count;
    int64_t master_offset_raw;               // momentary raw master offset t1-t2
    uint64_t t1_offset, t2_offset;           // Normalization offsets
    int64_t t1_norm, t2_norm;                // Input normalized timestamps
    int64_t master_offset_norm;              // normalized master offset t1_norm-t2_norm
    double master_drift_raw;                 // Raw momentary drift
    double master_drift;                     // Filtered drift over n cycles
    double master_drift_drift;               // Drift of the drift
    double master_offset_compensation;       // normalized master_offset compensation servo offset
    double master_offset_detrended;          // normalized master_offset error (detrended master_offset_norm)
    double master_offset_detrended_filtered; // filtered normalized master_offset error (detrended master_offset_norm)
    double master_jitter;                    // jitter
    double master_jitter_rms;                // jitter root mean square
    double master_jitter_avg;                // jitter average
    double servo_integral;                   // PI servo controller state: Integral accumulator for I-term
    tAverageFilter master_drift_filter;
    tAverageFilter master_jitter_rms_filter;
    tAverageFilter master_jitter_avg_filter;

    // Observer parameters
    const observer_parameters_t *params;

    // XCP event id
#ifdef OPTION_ENABLE_XCP
    tXcpEventId xcp_event; // on observer SYNC/FOLLOW_UP update
#endif
};

typedef struct ptp_observer tPtpObserver;

// Initialize the PTP observer state
static void observerInit(tPtpObserver *obs, uint8_t domain, const uint8_t *uuid, const uint8_t *addr) {

    assert(obs != NULL);

    obs->params = &observer_params;
    obs->next = NULL;

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
        A2lCreateParameter(observer_params.jitter_rms_filter_size, "Jitter RMS filter size", "", 1.0, 300.0);
        A2lCreateParameter(observer_params.jitter_avg_filter_size, "Jitter average filter size", "", 1.0, 300.0);
        A2lCreateParameter(observer_params.max_correction, "Maximum correction per cycle", "ns", 0.0, 1000.0);
        A2lCreateParameter(observer_params.servo_p_gain, "Proportional gain for servo", "", 0.0, 1.0);
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
    A2lCreatePhysMeasurementInstance(obs->name, o.master_drift_raw, "", "ppm*1000", -100, +100);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_drift, "", "ppm*1000", -100, +100);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_drift_drift, "", "ppm*1000", -10, +10);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_offset_raw, "t1-t2 raw value (not used)", "ns", -1000000, +1000000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_offset_compensation, "offset for detrending", "ns", -1000, +1000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_offset_detrended, "detrended master offset", "ns", -1000, +1000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_offset_detrended_filtered, "filtered detrended master offset", "ns", -1000, +1000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_jitter, "offset jitter raw value", "ns", -1000, +1000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_jitter_rms, "Jitter root mean square", "ns", -1000, +1000);
    A2lCreatePhysMeasurementInstance(obs->name, o.master_jitter_avg, "Jitter average", "ns", -1000, +1000);

#endif

    // Grandmaster info
    obs->gmValid = false;
    obs->domain = domain;
    if (uuid != NULL)
        memcpy(obs->uuid, uuid, 8);
    if (addr != NULL)
        memcpy(obs->addr, addr, 4);

    // Init protocol state
    obs->sync_local_time = 0;
    obs->sync_master_time = 0;
    obs->sync_correction = 0;
    obs->sync_sequenceId = 0;
    obs->sync_steps = 0;
    obs->flup_master_time = 0;
    obs->flup_correction = 0;
    obs->flup_sequenceId = 0;

    // Init timing analysis state
    obs->cycle_count = 0;
    obs->t1_norm = 0;
    obs->t2_norm = 0;
    obs->sync_cycle_time = 1000000000;
    obs->master_offset_raw = 0;
    obs->master_drift_raw = 0;
    average_filter_init(&obs->master_drift_filter, obs->params->drift_filter_size);
    obs->master_drift_raw = 0;
    obs->master_drift = 0;
    obs->master_drift_drift = 0;
    obs->master_offset_compensation = 0;
    obs->servo_integral = 0.0;
    obs->master_jitter = 0;
    average_filter_init(&obs->master_jitter_rms_filter, obs->params->jitter_rms_filter_size);
    obs->master_jitter_rms = 0;
    average_filter_init(&obs->master_jitter_avg_filter, obs->params->jitter_avg_filter_size);
    obs->master_jitter_avg = 0;
}

// Print information on the grandmaster
static void observerPrintMaster(const tPtpObserverMaster *m) {

    printf("    PTP Master:\n");
    const char *timesource = (m->a.timeSource == PTP_TIME_SOURCE_INTERNAL) ? "internal oscilator" : (m->a.timeSource == PTP_TIME_SOURCE_GPS) ? "GPS" : "Unknown";
    printf("      domain=%u, addr=%u.%u.%u.%u, id=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n"
           "      timesource=%s (%02X), utcOffset=%u, prio1=%u, class=%u, acc=%u, var=%u, prio2=%u, steps=%u\n",
           m->domain, m->addr[0], m->addr[1], m->addr[2], m->addr[3], m->uuid[0], m->uuid[1], m->uuid[2], m->uuid[3], m->uuid[4], m->uuid[5], m->uuid[6], m->uuid[7], timesource,
           m->a.timeSource, htons(m->a.utcOffset), m->a.priority1, m->a.clockClass, m->a.clockAccuraccy, htons(m->a.clockVariance), m->a.priority2, htons(m->a.stepsRemoved));
}

// Print the current PTP observer state
static void observerPrintState(tPtpObserver *obs) {

    printf("  Observer '%s':\n", obs->name);
    if (obs->gmValid) {
        observerPrintMaster(&obs->gm);
        printf("    master_drift        = %g ns/s\n", obs->master_drift);
        printf("    master_drift_drift  = %g ns/s2\n", obs->master_drift_drift);
        printf("    master_jitter       = %g ns\n", obs->master_jitter);
        printf("    master_jitter_avg   = %g ns\n", obs->master_jitter_avg);
        printf("    master_jitter_rms   = %g ns\n\n", obs->master_jitter_rms);
    } else {
        printf("    No active PTP master detected\n");
    }
}

// Update the PTP observer state with each new SYNC (t1,t2) timestamps
static void observerUpdate(tPtpObserver *obs, uint64_t t1_in, uint64_t correction, uint64_t t2_in) {

    assert(obs != NULL);

    char ts1[64], ts2[64];

    // Update observer parameters (update XCP calibrations)
    // Single threaded access assumed, called from ptpThread319 (1 step mode) or ptpThread320 (2 step mode) only
#ifdef OPTION_ENABLE_XCP
    // Each instance holds its lock continuously, so it may take about a second to make calibration changes effective
    XcpUpdateCalSeg((void **)&obs->params);
#endif

    // t1 - master, t2 - local clock
    obs->cycle_count++;

    if (obs->log_level >= 3)
        printf("Observer %s: PTP SYNC cycle %u:\n", obs->name, obs->cycle_count);
    if (obs->log_level >= 4) {
        printf("  t1 (SYNC tx on master (via PTP))  = %s (%" PRIu64 ") (%08X)\n", clockGetString(ts1, sizeof(ts1), t1_in), t1_in, (uint32_t)t1_in);
        printf("  t2 (SYNC rx)  = %s (%" PRIu64 ") (%08X)\n", clockGetString(ts2, sizeof(ts2), t2_in), t2_in, (uint32_t)t2_in);
        printf("  correction    = %" PRIu64 "ns\n", correction);
    }

    // Apply rounding correction to t1 ( Vector VN/VX PTP master has 8ns resolution, which leads to a systematic error )
    t1_in = t1_in + obs->params->t1_correction;

    // Apply correction to t1
    t1_in += correction;

    // Master offset raw value
    obs->master_offset_raw = (int64_t)(t1_in - t2_in); // Positive master_offset means master is ahead
    if (obs->log_level >= 4) {
        printf("    master_offset_raw   = %" PRIi64 " ns\n", obs->master_offset_raw);
    }

    // First round, init
    if (obs->t1_offset == 0 || obs->t2_offset == 0) {

        obs->t1_norm = 0; // corrected sync tx time on master clock
        obs->t2_norm = 0; // sync rx time on slave clock

        // Normalization time offsets for t1,t2
        obs->t1_offset = t1_in;
        obs->t2_offset = t2_in;

        obs->master_offset_compensation = 0;

        if (obs->log_level >= 3)
            printf("  Initial offsets: t1_offset=%" PRIu64 ", t2_offset=%" PRIu64 "\n", obs->t1_offset, obs->t2_offset);
    }

    // Analysis
    else {

        // Normalize t1,t2 to first round start time (may be negative in the beginning)
        int64_t t1_norm = (int64_t)(t1_in - obs->t1_offset);
        int64_t t2_norm = (int64_t)(t2_in - obs->t2_offset);

        if (obs->log_level >= 4)
            printf("  Normalized time: t1_norm=%" PRIi64 ", t2_norm=%" PRIi64 "\n", t1_norm, t2_norm);

        // Time differences since last SYNC, with correction applied to master time
        int64_t c1, c2;
        c1 = t1_norm - obs->t1_norm; // time since last sync on master clock
        c2 = t2_norm - obs->t2_norm; // time since last sync on local clock
        obs->sync_cycle_time = c2;   // Update last cycle time

        if (obs->log_level >= 4)
            printf("  Cycle times: c1=%" PRIi64 ", c2=%" PRIi64 "\n", c1, c2);

        // Drift calculation
        int64_t diff = c2 - c1; // Positive diff = master clock faster than local clock
        if (obs->log_level >= 4)
            printf("  Cycle time diff: diff=%" PRIi64 "\n", diff);
        if (diff < -200000 || diff > +200000) { // Plausibility checking of cycle drift (max 200us per cycle)
            printf("WARNING: Master drift too high! dt=%lld ns \n", diff);
        } else {
            obs->master_drift_raw = (double)diff * 1000000000 / c2; // Calculate drift in ppm instead of drift per cycle (drift is in ns/s (1/1000 ppm)
            double drift = average_filter_calc(&obs->master_drift_filter, obs->master_drift_raw); // Filter drift
            obs->master_drift_drift = drift - obs->master_drift; // Calculate drift of drift in ns/(s*s) (should be close to zero when temperature is stable )
            obs->master_drift = drift;
        }

        // Check if drift filter is warmed up
        if (average_filter_count(&obs->master_drift_filter) < average_filter_size(&obs->master_drift_filter)) {
            if (obs->log_level >= 3) {
                printf("  Master drift filter warming up (%zu/%zu)\n", average_filter_count(&obs->master_drift_filter), average_filter_size(&obs->master_drift_filter));
                printf("    master_drift_raw    = %g ns/s\n", obs->master_drift_raw);
            }
        } else {
            if (obs->log_level >= 3) {
                printf("  Drift calculation:\n");
                printf("    master_drift_raw    = %g ns/s\n", obs->master_drift_raw);
                printf("    master_drift        = %g ns/s\n", obs->master_drift);
                printf("    master_drift_drift  = %g ns/s2\n", obs->master_drift_drift);
            }

            // Calculate momentary master offset by detrending with current average drift
            obs->master_offset_norm = t1_norm - t2_norm; // Positive master_offset means master is ahead
            if (obs->log_level >= 4) {
                printf("    master_offset_norm  = %" PRIi64 " ns\n", obs->master_offset_norm);
            }

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

            // Jitter
            obs->master_jitter = obs->master_offset_detrended; // Jitter is the unfiltered detrended master offset
            obs->master_jitter_rms = sqrt((double)average_filter_calc(&obs->master_jitter_rms_filter, obs->master_jitter * obs->master_jitter)); // Filter jitter and calculate RMS
            if (obs->log_level >= 2) {
                printf("  Jitter analysis:\n");
                printf("    master_jitter       = %g ns\n", obs->master_jitter);
                printf("    master_jitter_avg   = %g ns\n", obs->master_jitter_avg);
                printf("    master_jitter_rms   = %g ns\n\n", obs->master_jitter_rms);
            }
        }

        // Remember last normalized input values
        obs->t1_norm = t1_norm; // sync tx time on master clock
        obs->t2_norm = t2_norm; // sync rx time on slave clock
    }

    // XCP measurement event (relative addressing mode to observer instance)
#ifdef OPTION_ENABLE_XCP
    XcpEventExt_Var(obs->xcp_event, 1, (const uint8_t *)obs); // Base address 0 (addr ext = 2) is observer instance
#endif
}

static bool observerHandleFrame(tPtp *ptp, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t timestamp) {

    if (!(n >= 44 && n <= 64)) {
        DBG_PRINT_ERROR("Invalid PTP message size\n");
        return false; // PTP message too small or too large
    }

    // For all observers
    for (struct ptp_observer *obs = ptp->observer_list; obs != NULL; obs = obs->next) {

        // Check if this observer is locked onto a grandmaster
        if (obs->gmValid) {

            // Check if SYNC or FOLLOW_UP match this observers master
            if (obs->domain == ptp_msg->domain && (memcmp(obs->gm.uuid, ptp_msg->clockId, 8) == 0) && (memcmp(obs->gm.addr, addr, 4) == 0)) {

                if (obs->gmValid && (ptp_msg->type == PTP_SYNC || ptp_msg->type == PTP_FOLLOW_UP)) {
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
                            observerUpdate(obs, obs->sync_master_time, obs->sync_correction, obs->sync_local_time);
                        }
                    } else {

                        obs->flup_master_time = htonl(ptp_msg->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp_msg->timestamp.timestamp_ns);
                        obs->flup_sequenceId = htons(ptp_msg->sequenceId);
                        obs->flup_correction = (uint32_t)(htonll(ptp_msg->correction) >> 16);
                    }

                    // 2 step sync update, SYNC and FOLLOW_UP may be received in any order (thread319 and thread320)
                    if (obs->sync_steps == 2 && obs->sync_sequenceId == obs->flup_sequenceId) {
                        observerUpdate(obs, obs->flup_master_time, obs->sync_correction, obs->sync_local_time); // 2 step
                    }
                }

                return true; // Processed by this observer
            } // match

        }

        // Not yet locked onto a grandmaster
        else {
            // Check if Announce from any master match this observers master filter
            if (ptp_msg->type == PTP_ANNOUNCE) {

                // Check if domain, uuid and addr match (if specified)
                if ((obs->domain == ptp_msg->domain) && (memcmp(obs->uuid, ptp_msg->clockId, 8) == 0 || memcmp(obs->uuid, "\0\0\0\0\0\0\0\0", 8) != 0) &&
                    (memcmp(obs->addr, addr, 4) == 0 || memcmp(obs->addr, "\0\0\0", 4) == 0)) {
                    if (ptp->log_level >= 1) {
                        printf("PTP Announce received from a master matching observer '%s' filter\n", obs->name);
                    }
                    obs->gmValid = true;
                    obs->gm.a = ptp_msg->u.a;
                    obs->gm.domain = ptp_msg->domain;
                    memcpy(obs->gm.uuid, ptp_msg->clockId, 8);
                    memcpy(obs->gm.addr, addr, 4);
                    observerPrintState(obs);
                    return true; // Locked onto a grandmaster
                }
            }
        }

    } // Observer list

    // Check announce messages from unknown masters
    if (ptp_msg->type == PTP_ANNOUNCE) {
        if (ptp->auto_observer_enabled) {
            // Create new observer for this master
            // Generate a name
            char name[32];
            snprintf(name, sizeof(name), "obs_%u.%u_%u", addr[2], addr[3], ptp_msg->domain);
            tPtpObserverHandle ptpObs = ptpCreateObserver(name, ptp, ptp_msg->domain, ptp_msg->clockId, addr);
        } else {
            if (ptp->log_level >= 4) {
                printf("PTP ignored announce from unknown master %u.%u.%u.%u (domain=%u)\n", addr[0], addr[1], addr[2], addr[3], ptp_msg->domain);
            }
        }
    }

    return true;
}

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
// PTP master

#define MAX_CLIENTS 16
#define SYNC_CYCLE_TIME_MS_DEFAULT 1000
#define ANNOUNCE_CYCLE_TIME_MS_DEFAULT 2000

typedef struct {
    uint16_t utcOffset;     // PTP ANNOUNCE parameter
    uint8_t clockClass;     // PTP ANNOUNCE parameter
    uint8_t clockAccuraccy; // PTP ANNOUNCE parameter
    uint16_t clockVariance; // PTP ANNOUNCE parameter
    uint16_t stepsRemoved;  // PTP ANNOUNCE parameter
    uint8_t timeSource;     // PTP ANNOUNCE parameter
    uint8_t priority1;      // PTP ANNOUNCE parameter
    uint8_t priority2;      // PTP ANNOUNCE parameter
} announce_parameters_t;

// Default master clock quality parameters for ANNOUNCE message
// lower value takes precedence in BMCA
static const announce_parameters_t announce_params = {
    .utcOffset = 37,
    .clockClass = PTP_CLOCK_CLASS_PTP_PRIMARY,
    .clockAccuraccy = PTP_CLOCK_ACC_GPS,
    .clockVariance = 0,
    .stepsRemoved = 0,
    .timeSource = PTP_TIME_SOURCE_GPS,
    .priority1 = 0,
    .priority2 = 0,
};

// Master parameter structure
typedef struct master_params {
    uint32_t announceCycleTimeMs; // Announce message cycle time in ms
    uint32_t syncCycleTimeMs;     // SYNC message cycle time in ms
} master_parameters_t;

// Default master parameter values
static const master_parameters_t master_params = {
    .announceCycleTimeMs = ANNOUNCE_CYCLE_TIME_MS_DEFAULT, // ANNOUNCE rate
    .syncCycleTimeMs = SYNC_CYCLE_TIME_MS_DEFAULT          // SYNC rate
};

// PTP client descriptor
struct ptp_client {
    uint8_t addr[4];       // IP addr
    uint8_t id[8];         // clock UUID
    uint64_t time;         // DELAY_REQ timestamp
    int64_t diff;          // DELAY_REQ current timestamp - delay req timestamp
    int64_t lastSeenTime;  // Last rx timestamp
    uint64_t cycle_time;   // Last cycle time in ns
    int32_t cycle_counter; // Cycle counter
    uint32_t corr;         // PTP correction
    uint8_t domain;        // PTP domain
};
typedef struct ptp_client tPtpClient;

// PTP master state
struct ptp_master {

    bool active;

    // Master identification
    u_int8_t domain;
    uint8_t uuid[8];

    struct ptp_master *next; // next master in list
    uint8_t log_level;
    char name[32];

    uint64_t announceCycleTimer;
    uint64_t syncCycleTimer;
    uint64_t syncTxTimestamp;
    uint16_t sequenceIdAnnounce;
    uint16_t sequenceIdSync;

    // PTP client list
    uint16_t clientCount;
    tPtpClient client[MAX_CLIENTS];

    // PTP master parameters
    const master_parameters_t *params;

    // XCP event id
#ifdef OPTION_ENABLE_XCP
    tXcpEventId xcp_event; // on master SYNC
#endif
};

typedef struct ptp_master tPtpMaster;

//---------------------------------------------------------------------------------------
// PTP master message sending

// Init constant values in PTP header
static void initHeader(tPtp *ptp, tPtpMaster *master, struct ptphdr *h, uint8_t type, uint16_t len, uint16_t flags, uint16_t sequenceId) {

    memset(h, 0, sizeof(struct ptphdr));
    h->version = 2;
    h->domain = master->domain;
    memcpy(h->clockId, master->uuid, 8);
    h->sourcePortId = htons(1);
    h->logMessageInterval = 0;
    h->type = type;
    h->len = htons(len);
    h->flags = htons(flags);
    h->sequenceId = htons(sequenceId);

    // Deprecated
    switch (type) {
    case PTP_ANNOUNCE:
        h->controlField = 0x05;
        break;
    case PTP_SYNC:
        h->controlField = 0x00;
        break;
    case PTP_FOLLOW_UP:
        h->controlField = 0x02;
        break;
    case PTP_DELAY_RESP:
        h->controlField = 0x03;
        break;
    default:
        assert(0);
    }
}

static bool ptpSendAnnounce(tPtp *ptp, tPtpMaster *master) {

    struct ptphdr h;
    int16_t l;

    initHeader(ptp, master, &h, PTP_ANNOUNCE, 64, 0, ++master->sequenceIdAnnounce);
    h.u.a.utcOffset = htons(announce_params.utcOffset);
    h.u.a.stepsRemoved = htons(announce_params.stepsRemoved);
    memcpy(h.u.a.grandmasterId, master->uuid, 8);
    h.u.a.clockVariance = htons(announce_params.clockVariance); // Allan deviation
    h.u.a.clockAccuraccy = announce_params.clockAccuraccy;
    h.u.a.clockClass = announce_params.clockClass;
    h.u.a.priority1 = announce_params.priority1;
    h.u.a.priority2 = announce_params.priority2;
    h.u.a.timeSource = announce_params.timeSource;
    l = socketSendTo(ptp->sock320, (uint8_t *)&h, 64, ptp->maddr, 320, NULL);

    if (ptp->log_level >= 3) {
        printf("TX ANNOUNCE %u %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", master->sequenceIdAnnounce, h.clockId[0], h.clockId[1], h.clockId[2], h.clockId[3], h.clockId[4],
               h.clockId[5], h.clockId[6], h.clockId[7]);
    }

    return (l == 64);
}

static bool ptpSendSync(tPtp *ptp, tPtpMaster *master, uint64_t *sync_txTimestamp) {

    struct ptphdr h;
    int16_t l;

    assert(sync_txTimestamp != NULL);

    initHeader(ptp, master, &h, PTP_SYNC, 44, PTP_FLAG_TWO_STEP, ++master->sequenceIdSync);
    l = socketSendTo(ptp->sock319, (uint8_t *)&h, 44, ptp->maddr, 319, sync_txTimestamp /* request tx time stamp */);
    if (l != 44) {
        printf("ERROR: ptpSendSync: socketSendTo failed, returned l = %d\n", l);
        return false;
    }
    if (*sync_txTimestamp == 0) { // If timestamp not obtained during send, get it now
        *sync_txTimestamp = socketGetSendTime(ptp->sock319);
        if (*sync_txTimestamp == 0) {
            printf("ERROR: ptpSendSync: socketGetSendTime failed, no tx timestamp available\n");
            return false;
        }
    }
    if (ptp->log_level >= 3) {
        printf("TX SYNC %u, tx time = %" PRIu64 "\n", master->sequenceIdSync, *sync_txTimestamp);
    }
    return true;
}

static bool ptpSendSyncFollowUp(tPtp *ptp, tPtpMaster *master, uint64_t sync_txTimestamp) {

    struct ptphdr h;
    int16_t l;

    initHeader(ptp, master, &h, PTP_FOLLOW_UP, 44, 0, master->sequenceIdSync);
    uint64_t t1 = sync_txTimestamp;
#if OPTION_TEST_TIME && !OPTION_TEST_TEST_TIME // Enable test time modifications in drift and offset
    t1 = testTimeCalc(t1);
#endif
    uint32_t ti;
    h.timestamp.timestamp_s_hi = 0;
    ti = (uint32_t)(t1 / CLOCK_TICKS_PER_S);
    h.timestamp.timestamp_s = htonl(ti);
    ti = (uint32_t)(t1 % CLOCK_TICKS_PER_S);
    h.timestamp.timestamp_ns = htonl(ti);

    l = socketSendTo(ptp->sock320, (uint8_t *)&h, 44, ptp->maddr, 320, NULL);

    if (ptp->log_level >= 3) {
        char ts[64];
        printf("TX FLUP %u t1 = %s (%" PRIu64 ")\n", master->sequenceIdSync, clockGetString(ts, sizeof(ts), t1), t1);
    }
    return (l == 44);
}

static bool ptpSendDelayResponse(tPtp *ptp, tPtpMaster *master, struct ptphdr *req, uint64_t delayreg_rxTimestamp) {

    struct ptphdr h;
    int16_t l;

    initHeader(ptp, master, &h, PTP_DELAY_RESP, 54, 0, htons(req->sequenceId)); // copy sequence id
    h.correction = req->correction;                                             // copy correction
    h.u.r.sourcePortId = req->sourcePortId;                                     // copy request egress port id
    memcpy(h.u.r.clockId, req->clockId, 8);                                     // copy request clock id

    // Set t4
    uint64_t t4 = delayreg_rxTimestamp;
#if OPTION_TEST_TIME && !OPTION_TEST_TEST_TIME // Enable test time modifications in drift and offset
    t4 = testTimeCalc(t4);
#endif
    uint32_t ti;
    h.timestamp.timestamp_s_hi = 0;
    ti = (uint32_t)(t4 / CLOCK_TICKS_PER_S);
    h.timestamp.timestamp_s = htonl(ti);
    ti = (uint32_t)(t4 % CLOCK_TICKS_PER_S);
    h.timestamp.timestamp_ns = htonl(ti);

    l = socketSendTo(ptp->sock320, (uint8_t *)&h, 54, ptp->maddr, 320, NULL);

    if (ptp->log_level >= 4) {
        char ts[64];
        struct ptphdr *ptp = &h;
        printf("TX DELAY_RESP %u to %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X  t4 = %s (%" PRIu64 ")\n", htons(h.sequenceId), ptp->u.r.clockId[0], ptp->u.r.clockId[1],
               ptp->u.r.clockId[2], ptp->u.r.clockId[3], ptp->u.r.clockId[4], ptp->u.r.clockId[5], ptp->u.r.clockId[6], ptp->u.r.clockId[7], clockGetString(ts, sizeof(ts), t4),
               t4);
    }

    return (l == 54);
}

//-------------------------------------------------------------------------------------------------------
// Client list

static void initClientList(tPtpMaster *master) {
    master->clientCount = 0;
    for (uint16_t i = 0; i < master->clientCount; i++)
        memset(&master->client[i], 0, sizeof(master->client[i]));
}

void printClient(tPtpMaster *master, uint16_t i) {

    char ts[64];
    printf("%u: addr=x.x.x.%u: domain=%u uuid=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X time=%s corr=%uns diff=%" PRIi64 " cycle=%u cycle_time=%gs\n", i, master->client[i].addr[3],
           master->client[i].domain, master->client[i].id[0], master->client[i].id[1], master->client[i].id[2], master->client[i].id[3], master->client[i].id[4],
           master->client[i].id[5], master->client[i].id[6], master->client[i].id[7], clockGetString(ts, sizeof(ts), master->client[i].time), master->client[i].corr,
           master->client[i].diff, master->client[i].cycle_counter, (double)master->client[i].cycle_time / 1e9);
}

static uint16_t lookupClient(tPtpMaster *master, uint8_t *addr, uint8_t *uuid) {
    uint16_t i;
    for (i = 0; i < master->clientCount; i++) {
        if (memcmp(addr, master->client[i].addr, 4) == 0)
            return i;
    }
    return 0xFFFF;
}

static uint16_t addClient(tPtpMaster *master, uint8_t *addr, uint8_t *uuid, uint8_t domain) {

    uint16_t i = lookupClient(master, addr, uuid);
    if (i < MAX_CLIENTS)
        return i;
    i = master->clientCount;
    master->client[i].domain = domain;
    memcpy(master->client[i].addr, addr, 4);
    memcpy(master->client[i].id, uuid, 8);
    master->clientCount++;
    return i;
}

//-------------------------------------------------------------------------------------------------------
// PTP master state machine

static void masterPrintState(tPtp *ptp, tPtpMaster *master) {
    char ts[64];
    uint64_t t;

    printf("\nMaster Info:\n");

    printf(" UUID:           %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", master->uuid[0], master->uuid[1], master->uuid[2], master->uuid[3], master->uuid[4], master->uuid[5],
           master->uuid[6], master->uuid[7]);
    printf(" IP:             %u.%u.%u.%u\n", ptp->if_addr[0], ptp->if_addr[1], ptp->if_addr[2], ptp->if_addr[3]);
    printf(" Interface:      %s\n", ptp->if_name);
    printf(" Domain:         %u\n", master->domain);
    if (!master->active) {
        printf(" Status:         INACTIVE\n");
    } else {
        printf(" ANNOUNCE cycle: %ums\n", master->params->announceCycleTimeMs);
        printf(" SYNC cycle:     %ums\n", master->params->syncCycleTimeMs);
        printf("Client list:\n");
        for (uint16_t i = 0; i < master->clientCount; i++) {
            printClient(master, i);
        }
    }
    printf("\n");
}

#if !defined(_MACOS) && !defined(_QNX)
#include <linux/if_packet.h>
#else
#include <ifaddrs.h>
#include <net/if_dl.h>
#endif
#if !defined(_WIN) // Non-Windows platforms
#include <ifaddrs.h>
#endif

static bool GetMAC(char *ifname, uint8_t *mac) {
    struct ifaddrs *ifaddrs, *ifa;
    if (getifaddrs(&ifaddrs) == 0) {
        for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
            if (!strcmp(ifa->ifa_name, ifname)) {
#if defined(_MACOS) || defined(_QNX)
                if (ifa->ifa_addr->sa_family == AF_LINK) {
                    memcpy(mac, (uint8_t *)LLADDR((struct sockaddr_dl *)ifa->ifa_addr), 6);
                }
#else
                if (ifa->ifa_addr->sa_family == AF_PACKET) {
                    struct sockaddr_ll *s = (struct sockaddr_ll *)ifa->ifa_addr;
                    memcpy(mac, s->sll_addr, 6);
                    break;
                }
#endif
            }
        }
        freeifaddrs(ifaddrs);
        return (ifa != NULL);
    }
    return false;
}

// Initialize the PTP master state
static void masterInit(tPtp *ptp, tPtpMaster *master, uint8_t domain, const uint8_t *uuid) {

    master->domain = domain;

    // Generate UUID from MAC address if not provided
    if (uuid == NULL || memcmp(uuid, "\0\0\0\0\0\0\0\0", 8) == 0) {
        uint8_t mac[6];
        if (GetMAC(ptp->if_name, mac)) {
            // Use MAC address to generate UUID (EUI-64 format)
            master->uuid[0] = mac[0] ^ 0x02; // locally administered
            master->uuid[1] = mac[1];
            master->uuid[2] = mac[2];
            master->uuid[3] = 0xFF;
            master->uuid[4] = 0xFE;
            master->uuid[5] = mac[3];
            master->uuid[6] = mac[4];
            master->uuid[7] = mac[5];
        } else {
            printf("ERROR: Failed to get MAC address for interface %s, using zero UUID\n", ptp->if_name);
            memset(master->uuid, 0, 8);
        }
    } else {
        memcpy(master->uuid, uuid, 8);
    }

    master->next = NULL;
    initClientList(master);
    master->params = &master_params;

    // XCP instrumentation
#ifdef OPTION_ENABLE_XCP

    // Create XCP event for master SYNC messages
    master->xcp_event = XcpCreateEvent(master->name, 0, 0);
    assert(master->xcp_event != XCP_UNDEFINED_EVENT_ID);

    // Create XCP calibration parameter segment, if not already existing
    tXcpCalSegIndex h = XcpCreateCalSeg("master_params", &master_params, sizeof(master_params));
    assert(h != XCP_UNDEFINED_CALSEG);
    master->params = (master_parameters_t *)XcpLockCalSeg(h); // Initial lock of the calibration segment (for persistence)

    A2lOnce() {

        // Create A2L parameter definitions for master parameters
        A2lSetSegmentAddrMode(h, master_params);
        A2lCreateParameter(master_params.announceCycleTimeMs, "Announce cycle time (ms)", "", 0, 10000);
        A2lCreateParameter(master_params.syncCycleTimeMs, "Sync cycle time (ms)", "", 0, 10000);

        // Create a A2L typedef for the PTP client structure
        A2lTypedefBegin(tPtpClient, NULL, "PTP client structure");
        A2lTypedefMeasurementComponent(cycle_counter, "Cycle counter");
        A2lTypedefPhysMeasurementComponent(cycle_time, "Cycle time", "ns", 0, 1E10);
        A2lTypedefMeasurementArrayComponent(addr, "IP address");
        A2lTypedefMeasurementArrayComponent(id, "Clock UUID");
        A2lTypedefMeasurementComponent(time, "DELAY_REQ timestamp (t3)");
        A2lTypedefMeasurementComponent(corr, "DELAY_REQ correction");
        A2lTypedefPhysMeasurementComponent(diff, "Timestamp difference (t4 - t3)", "ns", -1000000000, +1000000000);
        A2lTypedefEnd();
    }

    // Create A2L measurements for master state
    tPtpMaster m;                                                         // Dummy structure for A2L address calculations
    A2lSetRelativeAddrMode__i(master->xcp_event, 0, (const uint8_t *)&m); // Use base address index 0
    A2lCreateMeasurementInstance(master->name, m.clientCount, "Number of PTP clients");
    char name[32];
    snprintf(name, sizeof(name), "%s.master.client", master->name);
    A2lCreateInstance(name, tPtpClient, MAX_CLIENTS, m.client, "PTP client list"); // Array of clients
    A2lCreateMeasurementInstance(master->name, m.syncTxTimestamp, "SYNC tx timestamp");
    A2lCreateMeasurementInstance(master->name, m.sequenceIdAnnounce, "Announce sequence id");
    A2lCreateMeasurementInstance(master->name, m.sequenceIdSync, "SYNC sequence id");

#endif

    uint64_t t = clockGet();
    master->announceCycleTimer = 0;                                                                               // Send announce immediately
    master->syncCycleTimer = t + 100 * CLOCK_TICKS_PER_MS - master->params->syncCycleTimeMs * CLOCK_TICKS_PER_MS; // First SYNC after 100ms
    master->syncTxTimestamp = 0;
    master->sequenceIdAnnounce = 0;
    master->sequenceIdSync = 0;

    master->active = true;
}

// Master main cycle
static bool masterTask(tPtp *ptp, tPtpMaster *master) {

    // Update master parameters (update XCP calibrations)
#ifdef OPTION_ENABLE_XCP
    // Each master instance holds its parameter lock continuously, so it may take about a second to make calibration changes effective (until all updates are done)
    XcpUpdateCalSeg((void **)&master->params);
#endif

    if (!master->active)
        return true;

    uint64_t t = clockGet();
    uint32_t announceCycleTimeMs = master->params->announceCycleTimeMs; // Announce message cycle time in ms
    uint32_t syncCycleTimeMs = master->params->syncCycleTimeMs;         // SYNC message cycle time in ms

    // Announce cycle
    if (announceCycleTimeMs > 0 && t - master->announceCycleTimer > announceCycleTimeMs * CLOCK_TICKS_PER_MS) {
        master->announceCycleTimer = t;
        if (!ptpSendAnnounce(ptp, master)) {
            printf("ERROR: Failed to send ANNOUNCE\n");
        }
    }

    // Sync cycle
    if (syncCycleTimeMs > 0 && t - master->syncCycleTimer > syncCycleTimeMs * CLOCK_TICKS_PER_MS) {
        master->syncCycleTimer = t;

        mutexLock(&ptp->mutex);
        if (!ptpSendSync(ptp, master, &master->syncTxTimestamp)) {
            printf("ERROR: Failed to send SYNC\n");
            mutexUnlock(&ptp->mutex);
            return false;
        }

        if (master->syncTxTimestamp == 0) {
            printf("ERROR: SYNC tx timestamp not available !\n");
        } else {
            if (!ptpSendSyncFollowUp(ptp, master, master->syncTxTimestamp)) {
                printf("ERROR:Failed to send SYNC FOLLOW UP\n");
                mutexUnlock(&ptp->mutex);
                return false;
            }
        }
        mutexUnlock(&ptp->mutex);

        // XCP measurement event (relative addressing mode for this master instance)
#ifdef OPTION_ENABLE_XCP
        XcpEventExt_Var(master->xcp_event, 1, (const uint8_t *)master); // Base address 0 (addr ext = 2) is master instance

#endif
    }

    return true;
}

//-------------------------------------------------------------------------------------------------------
// PTP master frame handling

// Handle a received Delay Request message
static bool masterHandleFrame(tPtp *ptp, tPtpMaster *master, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t rxTimestamp) {

    if (!(n >= 44 && n <= 64)) {
        DBG_PRINT_ERROR("Invalid PTP message size\n");
        return false; // PTP message too small or too large
    }

    if (!master->active)
        return true;

    if (ptp_msg->type == PTP_ANNOUNCE) {
        if (ptp_msg->domain == master->domain && memcmp(ptp_msg->clockId, master->uuid, 8) != 0) {
            // There is another master on the network with the same domain and a different UUID
            printf("PTP Master '%s': Received ANNOUNCE from another master with same domain %u (UUID %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X)\n", master->name, ptp_msg->domain,
                   ptp_msg->clockId[0], ptp_msg->clockId[1], ptp_msg->clockId[2], ptp_msg->clockId[3], ptp_msg->clockId[4], ptp_msg->clockId[5], ptp_msg->clockId[6],
                   ptp_msg->clockId[7]);
            printf("PTP Master '%s': Best master algorithm is not supported!\n");
            master->active = false;
        }
    }

    if (ptp_msg->type == PTP_DELAY_REQ) {

        if (ptp_msg->domain == master->domain) {
            bool ok;
            mutexLock(&ptp->mutex);
            ok = ptpSendDelayResponse(ptp, master, ptp_msg, rxTimestamp);
            mutexUnlock(&ptp->mutex);
            if (!ok)
                return false;

            // Maintain PTP client list
            uint16_t i = lookupClient(master, addr, ptp_msg->clockId);
            bool newClient = (i >= MAX_CLIENTS);
            if (newClient) {
                i = addClient(master, addr, ptp_msg->clockId, ptp_msg->domain);
                masterPrintState(ptp, master); // Print state when a new client is added
            }

            // Some clients send non zero timestamp values in their DELAY_REQ which allows to visualize information on time synchronisation quality
            master->client[i].time = htonl(ptp_msg->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp_msg->timestamp.timestamp_ns);
            master->client[i].diff = rxTimestamp - master->client[i].time;
            master->client[i].corr = (uint32_t)(htonll(ptp_msg->correction) >> 16);
            master->client[i].cycle_time = rxTimestamp - master->client[i].lastSeenTime;
            master->client[i].lastSeenTime = rxTimestamp;
            master->client[i].cycle_counter++;
        }
    }

    return true;
}

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
// PTP threads for socket handling (319, 320) for both master and observer mode

// Time critical event messages (Sync, Delay_Req) on port 319
#if defined(_WIN) // Windows
static DWORD WINAPI ptpThread319(LPVOID par)
#else
static void *ptpThread319(void *par)
#endif
{
    uint8_t buffer[256];
    uint8_t addr[4];
    uint64_t rxTime;
    int n;

    tPtp *ptp = (tPtp *)par;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);
    for (;;) {
        n = socketRecvFrom(ptp->sock319, buffer, (uint16_t)sizeof(buffer), addr, NULL, &rxTime);
        if (n <= 0)
            break; // Terminate on error or socket close
        if (ptp->log_level >= 4)
            printFrame("RX", (struct ptphdr *)buffer, addr, rxTime); // Print incoming PTP traffic
        for (struct ptp_master *m = ptp->master_list; m != NULL; m = m->next) {
            masterHandleFrame(ptp, m, n, (struct ptphdr *)buffer, addr, rxTime);
        }
        observerHandleFrame(ptp, n, (struct ptphdr *)buffer, addr, rxTime);
    }
    if (ptp->log_level >= 3)
        printf("Terminate PTP multicast 319 thread\n");
    socketClose(&ptp->sock319);
    return 0;
}

// General messages (Announce, Follow_Up, Delay_Resp) on port 320
#if defined(_WIN) // Windows
static DWORD WINAPI ptpThread320(LPVOID par)
#else
static void *ptpThread320(void *par)
#endif
{
    uint8_t buffer[256];
    uint8_t addr[4];
    uint64_t rxTime;
    int n;

    tPtp *ptp = (tPtp *)par;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);
    for (;;) {
        n = socketRecvFrom(ptp->sock320, buffer, (uint16_t)sizeof(buffer), addr, NULL, NULL);
        if (n <= 0)
            break; // Terminate on error or socket close
        if (ptp->log_level >= 4)
            printFrame("RX", (struct ptphdr *)buffer, addr, 0); // Print incoming PTP traffic
        for (struct ptp_master *m = ptp->master_list; m != NULL; m = m->next) {
            masterHandleFrame(ptp, m, n, (struct ptphdr *)buffer, addr, 0);
        }
        observerHandleFrame(ptp, n, (struct ptphdr *)buffer, addr, 0);
    }
    if (ptp->log_level >= 3)
        printf("Terminate PTP multicast 320 thread\n");
    socketClose(&ptp->sock320);
    return 0;
}

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
// Public functions

// Start A PTP master or observer instance
// instance_name: Name of the PTP instance for XCP event creation
// mode: PTP_MODE_OBSERVER or PTP_MODE_MASTER
// domain: PTP domain to use (0-127)
// uuid: 8 byte clock UUID
// If if_addr = INADDR_ANY, bind to given interface
// Enable hardware timestamps on interface (requires root privileges)
tPtpInterfaceHandle ptpCreateInterface(const uint8_t *if_addr, const char *if_name, uint8_t log_level) {

    tPtp *ptp = (tPtp *)malloc(sizeof(tPtp));
    memset(ptp, 0, sizeof(tPtp));
    ptp->magic = PTP_MAGIC;

    ptp->log_level = log_level;

    // PTP communication parameters
    memcpy(ptp->if_addr, if_addr, 4);
    strncpy(ptp->if_name, if_name ? if_name : "", sizeof(ptp->if_name) - 1);

    // Create sockets for event (319) and general messages (320)
    ptp->sock319 = ptp->sock320 = INVALID_SOCKET;

    // For multicast reception on a specific interface:
    // - When if_addr is INADDR_ANY and interface is specified: bind to ANY and use socketBindToDevice (SO_BINDTODEVICE)
    // - When if_addr is specific: bind to that address (works only if multicast source is on same subnet)
    bool useBindToDevice = (if_name != NULL && if_addr[0] == 0 && if_addr[1] == 0 && if_addr[2] == 0 && if_addr[3] == 0);

    // SYNC with tx (master) or rx (observer) timestamp, DELAY_REQ - with rx timestamps
    if (!socketOpen(&ptp->sock319, SOCKET_MODE_BLOCKING | SOCKET_MODE_TIMESTAMPING))
        return NULL;
    if (!socketBind(ptp->sock319, if_addr, 319))
        return NULL;
    if (useBindToDevice && !socketBindToDevice(ptp->sock319, if_name))
        return NULL;

    // General messages ANNOUNCE, FOLLOW_UP, DELAY_RESP - without rx timestamps
    if (!socketOpen(&ptp->sock320, SOCKET_MODE_BLOCKING))
        return NULL;
    if (!socketBind(ptp->sock320, if_addr, 320))
        return NULL;
    if (useBindToDevice && !socketBindToDevice(ptp->sock320, if_name))
        return NULL;

    // Enable hardware timestamps for SYNC tx and DELAY_REQ messages (requires root privileges)
    if (!socketEnableHwTimestamps(ptp->sock319, if_name, true /* tx + rx PTP only*/)) {
        DBG_PRINT_ERROR("Hardware timestamping not enabled (may need root), using software timestamps\n");
        // return false;
    }

    if (ptp->log_level >= 2) {
        if (useBindToDevice)
            printf("  Bound PTP sockets to if_name %s\n", if_name);
        else
            printf("  Bound PTP sockets to %u.%u.%u.%u:320/319\n", if_addr[0], if_addr[1], if_addr[2], if_addr[3]);
    }

    // Join PTP multicast group
    if (ptp->log_level >= 2)
        printf("  Listening for PTP multicast on 224.0.1.129 %s\n", if_name ? if_name : "");
    uint8_t maddr[4] = {224, 0, 1, 129};
    memcpy(ptp->maddr, maddr, 4);
    if (!socketJoin(ptp->sock319, ptp->maddr, if_addr, if_name))
        return NULL;
    if (!socketJoin(ptp->sock320, ptp->maddr, if_addr, if_name))
        return NULL;

    // Start all PTP threads
    mutexInit(&ptp->mutex, true, 1000);
    create_thread_arg(&ptp->threadHandle320, ptpThread320, ptp);
    create_thread_arg(&ptp->threadHandle319, ptpThread319, ptp);

    return (tPtpInterfaceHandle)ptp;
}

tPtpObserverHandle ptpCreateObserver(const char *name, tPtpInterfaceHandle ptp_handle, uint8_t domain, const uint8_t *uuid, const uint8_t *addr) {

    tPtp *ptp = (tPtp *)ptp_handle;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    // Create observer instance
    tPtpObserver *obs = (tPtpObserver *)malloc(sizeof(tPtpObserver));
    memset(obs, 0, sizeof(tPtpObserver));
    strncpy(obs->name, name, sizeof(obs->name) - 1);
    observerInit(obs, domain, uuid, addr); // Initialize observer state
    obs->log_level = ptp->log_level;

    // Register the observer instance
    obs->next = ptp->observer_list;
    ptp->observer_list = obs;

    if (obs->log_level >= 1) {
        printf("Created PTP observer instance %s, listening on domain %u, addr=%u.%u.%u.%u, uuid=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", obs->name, obs->domain, obs->addr[0],
               obs->addr[1], obs->addr[2], obs->addr[3], obs->uuid[0], obs->uuid[1], obs->uuid[2], obs->uuid[3], obs->uuid[4], obs->uuid[5], obs->uuid[6], obs->uuid[7]);
    }

    return (tPtpObserverHandle)obs;
}

tPtpMasterHandle ptpCreateMaster(const char *name, tPtpInterfaceHandle ptp_handle, uint8_t domain, const uint8_t *uuid) {

    tPtp *ptp = (tPtp *)ptp_handle;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    tPtpMaster *master = (tPtpMaster *)malloc(sizeof(tPtpMaster));
    memset(master, 0, sizeof(tPtpMaster));
    strncpy(master->name, name, sizeof(master->name) - 1);
    masterInit(ptp, master, domain, uuid);
    master->log_level = ptp->log_level;

    // Register the master instance
    master->next = ptp->master_list;
    ptp->master_list = master;

    return (tPtpMasterHandle)master;
}

// Perform background tasks
// This is called from the application on a regular basis
// Observer: It monitors the status and checks for reset requests via calibration parameter
// Master: Send SYNC and ANNOUNCE messages
bool ptpTask(tPtpInterfaceHandle ptp_handle) {

    tPtp *ptp = (tPtp *)ptp_handle;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    for (struct ptp_master *m = ptp->master_list; m != NULL; m = m->next) {
        if (!masterTask(ptp, m))
            return false;
    }
    return true;
}

// Stop PTP
void ptpShutdown(tPtpInterfaceHandle ptp_handle) {

    tPtp *ptp = (tPtp *)ptp_handle;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    cancel_thread(ptp->threadHandle320);
    cancel_thread(ptp->threadHandle319);
    sleepMs(200);
    socketClose(&ptp->sock319);
    socketClose(&ptp->sock320);

    for (struct ptp_master *m = ptp->master_list; m != NULL;) {
        struct ptp_master *next = m->next;
        free(m);
        m = next;
    }
    ptp->master_list = NULL;

    for (tPtpObserver *c = ptp->observer_list; c != NULL;) {
        tPtpObserver *next = c->next;
        free(c);
        c = next;
    }
    ptp->observer_list = NULL;

    ptp->magic = 0;
    free(ptp);
}

// Set auto observer mode (accept announce from any master and create a new observer instance)
bool ptpEnableAutoObserver(tPtpInterfaceHandle ptp_handle) {

    tPtp *ptp = (tPtp *)ptp_handle;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);
    ptp->auto_observer_enabled = true;
    return true;
}

void ptpPrintState(tPtpInterfaceHandle ptp_handle) {

    tPtp *ptp = (tPtp *)ptp_handle;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    if (ptp->master_list != NULL) {
        for (tPtpMaster *m = ptp->master_list; m != NULL; m = m->next) {
            masterPrintState(ptp, m);
        }
    }
    if (ptp->observer_list != NULL) {
        printf("\nPTP Observer States:\n");
        for (tPtpObserver *obs = ptp->observer_list; obs != NULL; obs = obs->next) {
            observerPrintState(obs);
        }
        printf("\n");
    }
}