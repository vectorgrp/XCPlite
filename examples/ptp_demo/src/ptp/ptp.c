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

struct ptp {

    // Sockets and communication
    uint8_t addr[4];    // local addr
    uint8_t maddr[4];   // multicast addr
    char interface[32]; // network interface name
    THREAD threadHandle320;
    THREAD threadHandle319;
    SOCKET sock320;
    SOCKET sock319;
    MUTEX mutex;

    uint8_t log_level;

    uint8_t mode;
    struct ptp_master *m;
    struct ptp_observer *c;

#ifdef OPTION_ENABLE_XCP

    // XCP event id
    tXcpEventId xcp_event; // on master SYNC or observer SYNC/FOLLOW_UP update

#endif
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

    uint8_t domain;

    // Grandmaster info
    bool gmValid;
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
};

typedef struct ptp_observer tPtpObserver;

// Initialize the PTP observer state
static void observerInit(tPtp *ptp, uint8_t domain) {

    ptp->c->params = &observer_params;

    // XCP instrumentation
#ifdef OPTION_ENABLE_XCP

    // Create observer parameters
    // All observers share the same calibration segment
    tXcpCalSegIndex h = XcpCreateCalSeg("observer_params", &observer_params, sizeof(observer_params));
    ptp->c->params = (const observer_parameters_t *)XcpLockCalSeg(h); // Initial lock of the calibration segment (to enable calibration persistence)

    A2lOnce() {

        A2lSetSegmentAddrMode(h, observer_params);
        A2lCreateParameter(observer_params.reset, "Reset PTP observer state", "", 0, 1);
        A2lCreateParameter(observer_params.t1_correction, "Correction for t1", "", -100, 100);
        A2lCreateParameter(observer_params.drift_filter_size, "Drift filter size", "", 1, 300);
        A2lCreateParameter(observer_params.jitter_rms_filter_size, "Jitter RMS filter size", "", 1.0, 300.0);
        A2lCreateParameter(observer_params.jitter_avg_filter_size, "Jitter average filter size", "", 1.0, 300.0);
        A2lCreateParameter(observer_params.max_correction, "Maximum correction per cycle", "ns", 0.0, 1000.0);
        A2lCreateParameter(observer_params.servo_p_gain, "Proportional gain for servo", "", 0.0, 1.0);

        // Create observer measurements
        // Each observer has its own set of measurements by relative addressing mode on observer instance address
        tPtpObserver observer;                                                    // Temporary instance for address calculations
        A2lSetRelativeAddrMode__i(ptp->xcp_event, 0, (const uint8_t *)&observer); // Set relative addressing base addr 0 as the observer instance
        A2lCreateMeasurement(observer.gm.domain, "domain");
        A2lCreateMeasurementArray(observer.gm.uuid, "Grandmaster UUID");
        A2lCreateMeasurementArray(observer.gm.addr, "Grandmaster IP address");
        A2lCreateMeasurement(observer.sync_local_time, "SYNC RX timestamp");
        A2lCreateMeasurement(observer.sync_master_time, "SYNC timestamp");
        A2lCreatePhysMeasurement(observer.sync_correction, "SYNC correction", "ns", 0, 1000000);
        A2lCreateMeasurement(observer.sync_sequenceId, "SYNC sequence counter");
        A2lCreateMeasurement(observer.sync_steps, "SYNC mode");
        A2lCreatePhysMeasurement(observer.sync_cycle_time, "SYNC cycle time", "ns", 999999900, 1000000100);
        A2lCreateMeasurement(observer.flup_master_time, "FOLLOW_UP timestamp");
        A2lCreateMeasurement(observer.flup_sequenceId, "FOLLOW_UP sequence counter");
        A2lCreatePhysMeasurement(observer.flup_correction, "FOLLOW_UP correction", "ns", 0, 1000000);
        A2lCreatePhysMeasurement(observer.t1_norm, "t1 normalized to startup reference time t1_offset", "ns", 0, +1000000);
        A2lCreatePhysMeasurement(observer.t2_norm, "t2 normalized to startup reference time t2_offset", "ns", 0, +1000000);
        A2lCreatePhysMeasurement(observer.master_drift_raw, "", "ppm*1000", -100, +100);
        A2lCreatePhysMeasurement(observer.master_drift, "", "ppm*1000", -100, +100);
        A2lCreatePhysMeasurement(observer.master_drift_drift, "", "ppm*1000", -10, +10);
        A2lCreatePhysMeasurement(observer.master_offset_raw, "t1-t2 raw value (not used)", "ns", -1000000, +1000000);
        A2lCreatePhysMeasurement(observer.master_offset_compensation, "offset for detrending", "ns", -1000, +1000);
        A2lCreatePhysMeasurement(observer.master_offset_detrended, "detrended master offset", "ns", -1000, +1000);
        A2lCreatePhysMeasurement(observer.master_offset_detrended_filtered, "filtered detrended master offset", "ns", -1000, +1000);
        A2lCreatePhysMeasurement(observer.master_jitter, "offset jitter raw value", "ns", -1000, +1000);
        A2lCreatePhysMeasurement(observer.master_jitter_rms, "Jitter root mean square", "ns", -1000, +1000);
        A2lCreatePhysMeasurement(observer.master_jitter_avg, "Jitter average", "ns", -1000, +1000);
    }

#endif

    // Grandmaster info
    ptp->c->gmValid = false;
    ptp->c->domain = domain;

    // Init protocol state
    ptp->c->sync_local_time = 0;
    ptp->c->sync_master_time = 0;
    ptp->c->sync_correction = 0;
    ptp->c->sync_sequenceId = 0;
    ptp->c->sync_steps = 0;
    ptp->c->flup_master_time = 0;
    ptp->c->flup_correction = 0;
    ptp->c->flup_sequenceId = 0;

    // Init timing analysis state
    ptp->c->cycle_count = 0;
    ptp->c->t1_norm = 0;
    ptp->c->t2_norm = 0;
    ptp->c->sync_cycle_time = 1000000000;
    ptp->c->master_offset_raw = 0;
    ptp->c->master_drift_raw = 0;
    average_filter_init(&ptp->c->master_drift_filter, ptp->c->params->drift_filter_size);
    ptp->c->master_drift_raw = 0;
    ptp->c->master_drift = 0;
    ptp->c->master_drift_drift = 0;
    ptp->c->master_offset_compensation = 0;
    ptp->c->servo_integral = 0.0;
    ptp->c->master_jitter = 0;
    average_filter_init(&ptp->c->master_jitter_rms_filter, ptp->c->params->jitter_rms_filter_size);
    ptp->c->master_jitter_rms = 0;
    average_filter_init(&ptp->c->master_jitter_avg_filter, ptp->c->params->jitter_avg_filter_size);
    ptp->c->master_jitter_avg = 0;
}

// Print information on the grandmaster
static void observerPrintMaster(const tPtpObserverMaster *m) {

    printf("PTP Master:\n");
    const char *timesource = (m->a.timeSource == PTP_TIME_SOURCE_INTERNAL) ? "internal oscilator" : (m->a.timeSource == PTP_TIME_SOURCE_GPS) ? "GPS" : "Unknown";
    printf("    domain=%u, addr=%u.%u.%u.%u, id=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n"
           "    timesource=%s (%02X), utcOffset=%u, prio1=%u, class=%u, acc=%u, var=%u, prio2=%u, steps=%u\n",
           m->domain, m->addr[0], m->addr[1], m->addr[2], m->addr[3], m->uuid[0], m->uuid[1], m->uuid[2], m->uuid[3], m->uuid[4], m->uuid[5], m->uuid[6], m->uuid[7], timesource,
           m->a.timeSource, htons(m->a.utcOffset), m->a.priority1, m->a.clockClass, m->a.clockAccuraccy, htons(m->a.clockVariance), m->a.priority2, htons(m->a.stepsRemoved));
}

// Print the current PTP observer state
static void observerPrintState(tPtp *ptp) {

    if (ptp->c->gmValid) {
        observerPrintMaster(&ptp->c->gm);
    } else {
        printf("No active PTP master detected\n");
    }
}

// Update the PTP observer state with each new SYNC (t1,t2) timestamps
static void observerUpdate(tPtp *ptp, uint64_t t1_in, uint64_t correction, uint64_t t2_in) {

    char ts1[64], ts2[64];

    // Update observer parameters (update XCP calibrations)
    // Single threaded access assumed, called from ptpThread319 (1 step mode) or ptpThread320 (2 step mode) only
#ifdef OPTION_ENABLE_XCP
    // Each instance holds its lock continiously, so it may take about a second to make calibration changes effective
    XcpUpdateCalSeg(&ptp->c->params);
#endif

    // t1 - master, t2 - local clock
    ptp->c->cycle_count++;

    if (ptp->log_level >= 3)
        printf("PTP SYNC cycle %u:\n", ptp->c->cycle_count);
    if (ptp->log_level >= 4) {
        printf("  t1 (SYNC tx on master (via PTP))  = %s (%" PRIu64 ") (%08X)\n", clockGetString(ts1, sizeof(ts1), t1_in), t1_in, (uint32_t)t1_in);
        printf("  t2 (SYNC rx)  = %s (%" PRIu64 ") (%08X)\n", clockGetString(ts2, sizeof(ts2), t2_in), t2_in, (uint32_t)t2_in);
        printf("  correction    = %" PRIu64 "ns\n", correction);
    }

    // Apply rounding correction to t1 ( Vector VN/VX PTP master has 8ns resolution, which leads to a systematic error )
    t1_in = t1_in + ptp->c->params->t1_correction;

    // Apply correction to t1
    t1_in += correction;

    // Master offset raw value
    ptp->c->master_offset_raw = (int64_t)(t1_in - t2_in); // Positive master_offset means master is ahead
    if (ptp->log_level >= 4) {
        printf("    master_offset_raw   = %" PRIi64 " ns\n", ptp->c->master_offset_raw);
    }

    // First round, init
    if (ptp->c->t1_offset == 0 || ptp->c->t2_offset == 0) {

        ptp->c->t1_norm = 0; // corrected sync tx time on master clock
        ptp->c->t2_norm = 0; // sync rx time on slave clock

        // Normalization time offsets for t1,t2
        ptp->c->t1_offset = t1_in;
        ptp->c->t2_offset = t2_in;

        ptp->c->master_offset_compensation = 0;

        if (ptp->log_level >= 3)
            printf("  Initial offsets: t1_offset=%" PRIu64 ", t2_offset=%" PRIu64 "\n", ptp->c->t1_offset, ptp->c->t2_offset);
    }

    // Analysis
    else {

        // Normalize t1,t2 to first round start time (may be negative in the beginning)
        int64_t t1_norm = (int64_t)(t1_in - ptp->c->t1_offset);
        int64_t t2_norm = (int64_t)(t2_in - ptp->c->t2_offset);

        if (ptp->log_level >= 4)
            printf("  Normalized time: t1_norm=%" PRIi64 ", t2_norm=%" PRIi64 "\n", t1_norm, t2_norm);

        // Time differences since last SYNC, with correction applied to master time
        int64_t c1, c2;
        c1 = t1_norm - ptp->c->t1_norm; // time since last sync on master clock
        c2 = t2_norm - ptp->c->t2_norm; // time since last sync on local clock
        ptp->c->sync_cycle_time = c2;   // Update last cycle time

        if (ptp->log_level >= 4)
            printf("  Cycle times: c1=%" PRIi64 ", c2=%" PRIi64 "\n", c1, c2);

        // Drift calculation
        int64_t diff = c2 - c1; // Positive diff = master clock faster than local clock
        if (ptp->log_level >= 4)
            printf("  Cycle time diff: diff=%" PRIi64 "\n", diff);
        if (diff < -200000 || diff > +200000) { // Plausibility checking of cycle drift (max 200us per cycle)
            printf("WARNING: Master drift too high! dt=%lld ns \n", diff);
        } else {
            ptp->c->master_drift_raw = (double)diff * 1000000000 / c2; // Calculate drift in ppm instead of drift per cycle (drift is in ns/s (1/1000 ppm)
            double drift = average_filter_calc(&ptp->c->master_drift_filter, ptp->c->master_drift_raw); // Filter drift
            ptp->c->master_drift_drift =
                ((drift - ptp->c->master_drift) * 1000000000) / c2; // Calculate drift of drift in ns/s2 (should be close to zero when temperature is stable )
            ptp->c->master_drift = drift;
        }

        // Check if drift filter is warmed up
        if (average_filter_count(&ptp->c->master_drift_filter) < average_filter_size(&ptp->c->master_drift_filter)) {
            if (ptp->log_level >= 3) {
                printf("  Master drift filter warming up (%zu)\n", average_filter_count(&ptp->c->master_drift_filter));
                printf("    master_drift_raw    = %g ns/s\n", ptp->c->master_drift_raw);
            }
        } else {
            if (ptp->log_level >= 3) {
                printf("  Drift calculation:\n");
                printf("    master_drift_raw    = %g ns/s\n", ptp->c->master_drift_raw);
                printf("    master_drift        = %g ns/s\n", ptp->c->master_drift);
                printf("    master_drift_drift  = %g ns/s2\n", ptp->c->master_drift_drift);
            }

            // Calculate momentary master offset by detrending with current average drift
            ptp->c->master_offset_norm = t1_norm - t2_norm; // Positive master_offset means master is ahead
            if (ptp->log_level >= 4) {
                printf("    master_offset_norm  = %" PRIi64 " ns\n", ptp->c->master_offset_norm);
            }

            if (ptp->c->master_offset_compensation == 0) {
                // Initialize compensation
                ptp->c->master_offset_compensation = ptp->c->master_offset_norm;
            } else {
                // Compensate drift
                // ptp->c->master_offset_compensation -= ((ptp->c->master_drift) * (double)ptp->c->sync_cycle_time) / 1000000000;
                // Compensate drift and drift of drift
                double n = average_filter_count(&ptp->c->master_drift_filter) / 2;
                ptp->c->master_offset_compensation -= ((ptp->c->master_drift + ptp->c->master_drift_drift * n) * (double)ptp->c->sync_cycle_time) / 1000000000;
            }
            ptp->c->master_offset_detrended = (double)ptp->c->master_offset_norm - ptp->c->master_offset_compensation;
            ptp->c->master_offset_detrended_filtered = average_filter_calc(&ptp->c->master_jitter_avg_filter, ptp->c->master_offset_detrended);
            if (ptp->log_level >= 4) {
                printf("    master_offset_comp  = %g ns\n", ptp->c->master_offset_compensation);
            }
            if (ptp->log_level >= 3) {
                printf("    master_offset = %g ns (detrended)\n", ptp->c->master_offset_detrended);
                printf("    master_offset = %g ns (filtered detrended)\n", ptp->c->master_offset_detrended_filtered);
            }

            // PI Servo Controller to prevent offset runaway
            // The offset error should ideally be zero-mean jitter.
            // Any persistent non-zero mean indicates drift estimation error that needs correction.
            double correction = ptp->c->master_offset_detrended_filtered;

            // P-term
            correction *= ptp->c->params->servo_p_gain;

            // Correction rate limiting
            if (correction > ptp->c->params->max_correction)
                correction = ptp->c->params->max_correction;
            if (correction < -ptp->c->params->max_correction)
                correction = -ptp->c->params->max_correction;

            // Apply correction
            ptp->c->master_offset_compensation += correction;
            average_filter_add(&ptp->c->master_jitter_avg_filter, -correction);
            printf("Applied compensation correction: %g ns\n", correction);

            // Jitter
            ptp->c->master_jitter = ptp->c->master_offset_detrended; // Jitter is the unfiltered detrended master offset
            ptp->c->master_jitter_rms =
                sqrt((double)average_filter_calc(&ptp->c->master_jitter_rms_filter, ptp->c->master_jitter * ptp->c->master_jitter)); // Filter jitter and calculate RMS
            if (ptp->log_level >= 3) {
                printf("  Jitter analysis:\n");
                printf("    master_jitter       = %g ns\n", ptp->c->master_jitter);
                printf("    master_jitter_avg   = %g ns\n", ptp->c->master_jitter_avg);
                printf("    master_jitter_rms   = %g ns\n\n", ptp->c->master_jitter_rms);
            }
        }

        // Remember last normalized input values
        ptp->c->t1_norm = t1_norm; // sync tx time on master clock
        ptp->c->t2_norm = t2_norm; // sync rx time on slave clock
    }

    // XCP measurement event (relative addressing mode to observer instance)
#ifdef OPTION_ENABLE_XCP
    XcpEventExt(ptp->xcp_event, (const uint8_t *)&ptp->c);
#endif
}

static bool observerHandleFrame(tPtp *ptp, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t timestamp) {

    if (n >= 44 && n <= 64) {

        tPtpObserver *obs = ptp->c;
        if (obs->domain == ptp_msg->domain) {

            if (obs->gmValid && (ptp_msg->type == PTP_SYNC || ptp_msg->type == PTP_FOLLOW_UP)) {
                if (ptp_msg->type == PTP_SYNC) {
                    if (timestamp == 0) {
                        DBG_PRINT_WARNING("PTP SYNC received without timestamp!\n");
                        return false;
                    }
                    obs->sync_local_time = timestamp;
                    obs->sync_master_time = htonl(ptp_msg->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp_msg->timestamp.timestamp_ns);
                    obs->sync_sequenceId = htons(ptp_msg->sequenceId);
                    obs->sync_correction = (uint32_t)(htonll(ptp_msg->correction) >> 16);
                    obs->sync_steps = (htons(ptp_msg->flags) & PTP_FLAG_TWO_STEP) ? 2 : 1;

                    // 1 step sync update
                    if (obs->sync_steps == 1) {
                        observerUpdate(ptp, obs->sync_master_time, obs->sync_correction, obs->sync_local_time);
                    }
                } else {

                    obs->flup_master_time = htonl(ptp_msg->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp_msg->timestamp.timestamp_ns);
                    obs->flup_sequenceId = htons(ptp_msg->sequenceId);
                    obs->flup_correction = (uint32_t)(htonll(ptp_msg->correction) >> 16);
                }

                // 2 step sync update, SYNC and FOLLOW_UP may be received in any order (thread319 and thread320)
                if (obs->sync_steps == 2 && obs->sync_sequenceId == obs->flup_sequenceId) {
                    observerUpdate(ptp, obs->flup_master_time, obs->sync_correction, obs->sync_local_time); // 2 step
                }

            } else if (ptp_msg->type == PTP_ANNOUNCE) {
                if (!obs->gmValid) {
                    printf("Found active PTP master:\n");
                    obs->gmValid = true;
                    obs->gm.a = ptp_msg->u.a;
                    obs->gm.domain = ptp_msg->domain;
                    memcpy(obs->gm.uuid, ptp_msg->clockId, 8);
                    memcpy(obs->gm.addr, addr, 4);
                    observerPrintState(ptp);
                }
            }

        } // from active master
    } else {
        if (ptp->log_level >= 3) {
            printf("Received PTP frame of type %d with length %d, not handled\n", ptp_msg->type, n);
            printFrame("RX", ptp_msg, addr, timestamp);
        }
    }

    return true;
}

static bool observerTask(tPtp *ptp) {

    // Nothing to do yet
    return true;
}

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
// PTP master

#define MAX_CLIENTS 16

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
    .announceCycleTimeMs = 2000, // Announce every 2s
    .syncCycleTimeMs = 1000      // SYNC every 1s
};

// PTP client descriptor
struct ptp_client {
    uint8_t addr[4];      // IP addr
    uint8_t id[8];        // clock UUID
    uint64_t time;        // DELAY_REQ time stmap
    int64_t diff;         // DELAY_REQ current timestamp - delay req timestamp
    int64_t lastSeenTime; // Last rx timestamp
    int64_t cycle;        // Last cycle time
    int64_t counter;      // Cycle counter
    uint32_t corr;        // PTP correction
    uint8_t domain;       // PTP domain
};
typedef struct ptp_client tPtpClient;

// PTP master state
struct ptp_master {
    u_int8_t domain;
    uint8_t uuid[8];

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
};

typedef struct ptp_master tPtpMaster;

//---------------------------------------------------------------------------------------
// PTP master message sending

// Init constant values in PTP header
static void initHeader(tPtp *ptp, struct ptphdr *h, uint8_t type, uint16_t len, uint16_t flags, uint16_t sequenceId) {

    memset(h, 0, sizeof(struct ptphdr));
    h->version = 2;
    h->domain = ptp->m->domain;
    memcpy(h->clockId, ptp->m->uuid, 8);
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

static bool ptpSendAnnounce(tPtp *ptp) {

    struct ptphdr h;
    int16_t l;

    initHeader(ptp, &h, PTP_ANNOUNCE, 64, 0, ++ptp->m->sequenceIdAnnounce);
    h.u.a.utcOffset = htons(announce_params.utcOffset);
    h.u.a.stepsRemoved = htons(announce_params.stepsRemoved);
    memcpy(h.u.a.grandmasterId, ptp->m->uuid, 8);
    h.u.a.clockVariance = htons(announce_params.clockVariance); // Allan deviation
    h.u.a.clockAccuraccy = announce_params.clockAccuraccy;
    h.u.a.clockClass = announce_params.clockClass;
    h.u.a.priority1 = announce_params.priority1;
    h.u.a.priority2 = announce_params.priority2;
    h.u.a.timeSource = announce_params.timeSource;
    l = socketSendTo(ptp->sock320, (uint8_t *)&h, 64, ptp->maddr, 320, NULL);

    if (ptp->log_level >= 2) {
        printf("TX ANNOUNCE %u %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", ptp->m->sequenceIdAnnounce, h.clockId[0], h.clockId[1], h.clockId[2], h.clockId[3], h.clockId[4],
               h.clockId[5], h.clockId[6], h.clockId[7]);
    }

    return (l == 64);
}

static bool ptpSendSync(tPtp *ptp, uint64_t *sync_txTimestamp) {

    struct ptphdr h;
    int16_t l;

    assert(sync_txTimestamp != NULL);

    initHeader(ptp, &h, PTP_SYNC, 44, PTP_FLAG_TWO_STEP, ++ptp->m->sequenceIdSync);
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
    if (ptp->log_level >= 2) {
        printf("TX SYNC %u, tx time = %" PRIu64 "\n", ptp->m->sequenceIdSync, *sync_txTimestamp);
    }
    return true;
}

static bool ptpSendSyncFollowUp(tPtp *ptp, uint64_t sync_txTimestamp) {

    struct ptphdr h;
    int16_t l;

    initHeader(ptp, &h, PTP_FOLLOW_UP, 44, 0, ptp->m->sequenceIdSync);
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

    if (ptp->log_level >= 2) {
        char ts[64];
        printf("TX FLUP %u t1 = %s (%" PRIu64 ")\n", ptp->m->sequenceIdSync, clockGetString(ts, sizeof(ts), t1), t1);
    }
    return (l == 44);
}

static bool ptpSendDelayResponse(tPtp *ptp, struct ptphdr *req, uint64_t delayreg_rxTimestamp) {

    struct ptphdr h;
    int16_t l;

    initHeader(ptp, &h, PTP_DELAY_RESP, 54, 0, htons(req->sequenceId)); // copy sequence id
    h.correction = req->correction;                                     // copy correction
    h.u.r.sourcePortId = req->sourcePortId;                             // copy request egress port id
    memcpy(h.u.r.clockId, req->clockId, 8);                             // copy request clock id

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

    if (ptp->log_level >= 2) {
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

static void initClientList(tPtp *ptp) {
    ptp->m->clientCount = 0;
    for (uint16_t i = 0; i < ptp->m->clientCount; i++)
        memset(&ptp->m->client[i], 0, sizeof(ptp->m->client[i]));
}

void printClient(tPtp *ptp, uint16_t i) {

    char ts[64];
    printf("%u: addr=x.x.x.%u: domain=%u uuid=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X time=%s corr=%u diff=%" PRId64 " cycle=%" PRId64 " \n", i, ptp->m->client[i].addr[3],
           ptp->m->client[i].domain, ptp->m->client[i].id[0], ptp->m->client[i].id[1], ptp->m->client[i].id[2], ptp->m->client[i].id[3], ptp->m->client[i].id[4],
           ptp->m->client[i].id[5], ptp->m->client[i].id[6], ptp->m->client[i].id[7], clockGetString(ts, sizeof(ts), ptp->m->client[i].time), ptp->m->client[i].corr,
           ptp->m->client[i].diff, ptp->m->client[i].cycle);
}

static uint16_t lookupClient(tPtp *ptp, uint8_t *addr, uint8_t *uuid) {
    uint16_t i;
    for (i = 0; i < ptp->m->clientCount; i++) {
        if (memcmp(addr, ptp->m->client[i].addr, 4) == 0)
            return i;
    }
    return 0xFFFF;
}

static uint16_t addClient(tPtp *ptp, uint8_t *addr, uint8_t *uuid, uint8_t domain) {

    uint16_t i = lookupClient(ptp, addr, uuid);
    if (i < MAX_CLIENTS)
        return i;
    i = ptp->m->clientCount;
    ptp->m->client[i].domain = domain;
    memcpy(ptp->m->client[i].addr, addr, 4);
    memcpy(ptp->m->client[i].id, uuid, 8);
    ptp->m->clientCount++;
    return i;
}

//-------------------------------------------------------------------------------------------------------
// PTP master state machine

static void masterPrintState(tPtp *ptp) {
    char ts[64];
    uint64_t t;

    tPtpMaster *ptpM = ptp->m;
    printf("\nMaster Info:\n");
    printf(" UUID:           %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", ptpM->uuid[0], ptpM->uuid[1], ptpM->uuid[2], ptpM->uuid[3], ptpM->uuid[4], ptpM->uuid[5], ptpM->uuid[6],
           ptpM->uuid[7]);
    printf(" IP:             %u.%u.%u.%u\n", ptp->addr[0], ptp->addr[1], ptp->addr[2], ptp->addr[3]);
    printf(" Interface:      %s\n", ptp->interface);
    printf(" Domain:         %u\n", ptpM->domain);
    printf("Client list:\n");
    for (uint16_t i = 0; i < ptpM->clientCount; i++) {
        printClient(ptp, i);
    }
    printf("\n");
}

// Initialize the PTP master state
static void masterInit(tPtp *ptp, uint8_t domain, uint8_t *uuid) {

    ptp->m->params = &master_params;

    // XCP instrumentation
#ifdef OPTION_ENABLE_XCP

    // Create XCP calibration parameter segment, if not already existing
    tXcpCalSegIndex h = XcpCreateCalSeg("master_params", &master_params, sizeof(master_params));
    ptp->m->params = XcpLockCalSeg(h); // Initial lock of the calibration segment (for persistence)

    A2lOnce() {

        // Create A2L parameter definitions for master parameters
        A2lSetSegmentAddrMode(h, master_params);
        A2lCreateParameter(master_params.announceCycleTimeMs, "Announce cycle time (ms)", "", 0, 10000);
        A2lCreateParameter(master_params.syncCycleTimeMs, "Sync cycle time (ms)", "", 0, 10000);

        // Create a A2L typedef for the PTP client structure
        A2lTypedefBegin(tPtpClient, NULL, "PTP client structure");
        A2lTypedefMeasurementComponent(counter, "cycle counter");
        A2lTypedefPhysMeasurementComponent(cycle, "Cycle time", "ns", 0, 1000000000);
        A2lTypedefMeasurementArrayComponent(addr, "IP address");
        A2lTypedefMeasurementArrayComponent(id, "Clock UUID");
        A2lTypedefMeasurementComponent(time, "DELAY_REQ timestamp (t3)");
        A2lTypedefMeasurementComponent(corr, "DELAY_REQ correction");
        A2lTypedefPhysMeasurementComponent(diff, "Timestamp difference (t4 - t3)", "ns", -1000000000, +1000000000);
        A2lTypedefEnd();

        // Create A2L measurement definitions for master state
        tPtpMaster master; // Dummy structure for A2L address calculations
        A2lSetRelativeAddrMode__i(ptp->xcp_event, 0, (const uint8_t *)&master);
        A2lCreateMeasurement(master.clientCount, "Number of PTP clients");
        A2lCreateTypedefInstanceArray(master.client, tPtpObserverlient, MAX_CLIENTS, "PTP client list");
        A2lCreateMeasurement(master.syncTxTimestamp, "SYNC tx timestamp");
        A2lCreateMeasurement(master.sequenceIdAnnounce, "Announce sequence id");
        A2lCreateMeasurement(master.sequenceIdSync, "SYNC sequence id");
    }

#endif

    ptp->m->domain = domain;
    memcpy(ptp->m->uuid, uuid, sizeof(ptp->m->uuid));
    initClientList(ptp);

    uint64_t t = clockGet();
    ptp->m->announceCycleTimer = 0;                                                                               // Send announce immediately
    ptp->m->syncCycleTimer = t + 100 * CLOCK_TICKS_PER_MS - ptp->m->params->syncCycleTimeMs * CLOCK_TICKS_PER_MS; // First SYNC after 100ms
    ptp->m->syncTxTimestamp = 0;
    ptp->m->sequenceIdAnnounce = 0;
    ptp->m->sequenceIdSync = 0;
}

// Master main cycle
static bool masterTask(tPtp *ptp) {

    // Update master parameters (update XCP calibrations)
#ifdef OPTION_ENABLE_XCP
    // Each master instance holds its parameter lock continuously, so it may take about a second to make calibration changes effective (until all updates are done)
    XcpUpdateCalSeg(&ptp->m->params);
#endif

    uint64_t t = clockGet();
    uint32_t announceCycleTimeMs = ptp->m->params->announceCycleTimeMs; // Announce message cycle time in ms
    uint32_t syncCycleTimeMs = ptp->m->params->syncCycleTimeMs;         // SYNC message cycle time in ms

    // Announce cycle
    if (announceCycleTimeMs > 0 && t - ptp->m->announceCycleTimer > announceCycleTimeMs * CLOCK_TICKS_PER_MS) {
        ptp->m->announceCycleTimer = t;
        if (!ptpSendAnnounce(ptp))
            return false;
    }

    // Sync cycle
    if (syncCycleTimeMs > 0 && t - ptp->m->syncCycleTimer > syncCycleTimeMs * CLOCK_TICKS_PER_MS) {
        ptp->m->syncCycleTimer = t;

        mutexLock(&ptp->mutex);
        if (!ptpSendSync(ptp, &ptp->m->syncTxTimestamp)) {
            printf("ERROR: Failed to send SYNC\n");
            mutexUnlock(&ptp->mutex);
            return false;
        }

        if (ptp->m->syncTxTimestamp == 0) {
            printf("ERROR: SYNC tx timestamp not available !\n");
        } else {
            if (!ptpSendSyncFollowUp(ptp, ptp->m->syncTxTimestamp)) {
                printf("ERROR:Failed to send SYNC FOLLOW UP\n");
                mutexUnlock(&ptp->mutex);
                return false;
            }
        }
        mutexUnlock(&ptp->mutex);

        // XCP measurement event (relative addressing mode for this master instance)
#ifdef OPTION_ENABLE_XCP
        XcpEventExt(ptp->xcp_event, &ptp->m);
#endif
    }

    return true;
}

//-------------------------------------------------------------------------------------------------------
// PTP master frame handling

// Handle a received Delay Request message
static bool masterHandleFrame(tPtp *ptp, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t rxTimestamp) {

    if (n >= 44 && n <= 64) {

        if (ptp_msg->type == PTP_DELAY_REQ) {

            if (ptp_msg->domain == ptp->m->domain) {
                bool ok;
                mutexLock(&ptp->mutex);
                ok = ptpSendDelayResponse(ptp, ptp_msg, rxTimestamp);
                mutexUnlock(&ptp->mutex);
                if (!ok)
                    return false;
            }

            // Maintain PTP client list
            uint16_t i = lookupClient(ptp, addr, ptp_msg->clockId);
            bool newClient = (i >= MAX_CLIENTS);
            if (newClient) {
                i = addClient(ptp, addr, ptp_msg->clockId, ptp_msg->domain);
                masterPrintState(ptp); // Print state when a new client is added
            }

            // Some clients send non zero timestamp values in their DELAY_REQ which allows to visualize information on time synchronisation quality
            ptp->m->client[i].time = htonl(ptp_msg->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp_msg->timestamp.timestamp_ns);
            ptp->m->client[i].diff = rxTimestamp - ptp->m->client[i].time;
            ptp->m->client[i].corr = (uint32_t)(htonll(ptp_msg->correction) >> 16);
            ptp->m->client[i].cycle = rxTimestamp - ptp->m->client[i].lastSeenTime;
            ptp->m->client[i].counter++;
            ptp->m->client[i].lastSeenTime = rxTimestamp;
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

    for (;;) {
        n = socketRecvFrom(ptp->sock319, buffer, (uint16_t)sizeof(buffer), addr, NULL, &rxTime);
        if (n <= 0)
            break; // Terminate on error or socket close
        if (ptp->log_level >= 4)
            printFrame("RX", (struct ptphdr *)buffer, addr, rxTime); // Print incoming PTP traffic
        if (ptp->mode == PTP_MODE_MASTER)
            masterHandleFrame(ptp, n, (struct ptphdr *)buffer, addr, rxTime);
        if (ptp->mode == PTP_MODE_OBSERVER)
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
    for (;;) {
        n = socketRecvFrom(ptp->sock320, buffer, (uint16_t)sizeof(buffer), addr, NULL, NULL);
        if (n <= 0)
            break; // Terminate on error or socket close
        if (ptp->log_level >= 4)
            printFrame("RX", (struct ptphdr *)buffer, addr, 0); // Print incoming PTP traffic
        if (ptp->mode == PTP_MODE_OBSERVER)
            observerHandleFrame(ptp, n, (struct ptphdr *)buffer, addr, 0);
        if (ptp->mode == PTP_MODE_MASTER) {
            masterHandleFrame(ptp, n, (struct ptphdr *)buffer, addr, 0);
        }
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
// If bindAddr = INADDR_ANY, bind to given interface
// Enable hardware timestamps on interface (requires root privileges)
tPtpHandle ptpInit(const char *instance_name, uint8_t mode, uint8_t domain, uint8_t *uuid, uint8_t *bindAddr, char *interface, uint8_t log_level) {

    tPtp *ptp = (tPtp *)malloc(sizeof(tPtp));
    memset(ptp, 0, sizeof(tPtp));

    ptp->log_level = log_level;
    ptp->mode = mode;

    // PTP communication parameters
    memcpy(ptp->addr, bindAddr, 4);
    strncpy(ptp->interface, interface ? interface : "", sizeof(ptp->interface) - 1);

#ifdef OPTION_ENABLE_XCP
    // Create an individual XCP event for measurement of this instance
    // @@@@ TODO: Use interface name ????
    ptp->xcp_event = XcpCreateEvent(instance_name, 0, 0);
#endif

    if (mode == PTP_MODE_OBSERVER) {
        tPtpObserver *ptpC = malloc(sizeof(tPtpObserver));
        memset(ptpC, 0, sizeof(tPtpObserver));
        ptp->c = ptpC;
        observerInit(ptp, domain);
    }
    if (mode == PTP_MODE_MASTER) {
        tPtpMaster *ptpM = malloc(sizeof(tPtpMaster));
        memset(ptpM, 0, sizeof(tPtpMaster));
        ptp->m = ptpM;
        masterInit(ptp, domain, uuid);
    }

    // Create sockets for event (319) and general messages (320)
    ptp->sock319 = ptp->sock320 = INVALID_SOCKET;

    // For multicast reception on a specific interface:
    // - When bindAddr is INADDR_ANY and interface is specified: bind to ANY and use socketBindToDevice (SO_BINDTODEVICE)
    // - When bindAddr is specific: bind to that address (works only if multicast source is on same subnet)
    bool useBindToDevice = (interface != NULL && bindAddr[0] == 0 && bindAddr[1] == 0 && bindAddr[2] == 0 && bindAddr[3] == 0);

    // SYNC with tx (master) or rx (observer) timestamp, DELAY_REQ - with rx timestamps
    if (!socketOpen(&ptp->sock319, SOCKET_MODE_BLOCKING | SOCKET_MODE_TIMESTAMPING))
        return NULL;
    if (!socketBind(ptp->sock319, bindAddr, 319))
        return NULL;
    if (useBindToDevice && !socketBindToDevice(ptp->sock319, interface))
        return NULL;

    // General messages ANNOUNCE, FOLLOW_UP, DELAY_RESP - without rx timestamps
    if (!socketOpen(&ptp->sock320, SOCKET_MODE_BLOCKING))
        return NULL;
    if (!socketBind(ptp->sock320, bindAddr, 320))
        return NULL;
    if (useBindToDevice && !socketBindToDevice(ptp->sock320, interface))
        return NULL;

    // Enable hardware timestamps for SYNC tx and DELAY_REQ messages (requires root privileges)
    if (!socketEnableHwTimestamps(ptp->sock319, interface, true /* tx + rx PTP only*/)) {
        DBG_PRINT_ERROR("Hardware timestamping not enabled (may need root), using software timestamps\n");
        // return false;
    }

    if (ptp->log_level >= 2) {
        if (useBindToDevice)
            printf("  Bound PTP sockets to interface %s\n", interface);
        else
            printf("  Bound PTP sockets to %u.%u.%u.%u:320/319\n", bindAddr[0], bindAddr[1], bindAddr[2], bindAddr[3]);
    }

    // Join PTP multicast group
    if (ptp->log_level >= 2)
        printf("  Listening for PTP multicast on 224.0.1.129 %s\n", interface ? interface : "");
    uint8_t maddr[4] = {224, 0, 1, 129};
    memcpy(ptp->maddr, maddr, 4);
    if (!socketJoin(ptp->sock319, ptp->maddr, bindAddr, interface))
        return NULL;
    if (!socketJoin(ptp->sock320, ptp->maddr, bindAddr, interface))
        return NULL;

    // Start all PTP threads
    mutexInit(&ptp->mutex, true, 1000);
    create_thread(&ptp->threadHandle320, ptpThread320);
    create_thread(&ptp->threadHandle319, ptpThread319);

    return ptp;
}

// Perform background tasks
// This is called from the application on a regular basis
// Observer: It monitors the status and checks for reset requests via calibration parameter
// Master: Send SYNC and ANNOUNCE messages
bool ptpTask(tPtpHandle tPtpHandle) {

    tPtp *ptp = (tPtp *)tPtpHandle;

    if (ptp->mode == PTP_MODE_OBSERVER) {
        return observerTask(ptp);
    }
    if (ptp->mode == PTP_MODE_MASTER) {
        return masterTask(ptp);
    }
    return true;
}

// Stop PTP
void ptpShutdown(tPtpHandle tPtpHandle) {

    tPtp *ptp = (tPtp *)tPtpHandle;
    cancel_thread(ptp->threadHandle320);
    cancel_thread(ptp->threadHandle319);
    sleepMs(200);
    socketClose(&ptp->sock319);
    socketClose(&ptp->sock320);
}

// Print PTP status information
void ptpPrintStatusInfo(tPtpHandle ptpHandle) {

    tPtp *ptp = (tPtp *)ptpHandle;
    if (ptp->mode == PTP_MODE_MASTER) {
        masterPrintState(ptp);
    }
    if (ptp->mode == PTP_MODE_OBSERVER) {
        observerPrintState(ptp);
    }
}
