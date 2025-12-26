/*----------------------------------------------------------------------------
| File:
|   ptp.c
|
| Description:
|   PTP observer and master with XCP instrumentation
|   For analyzing PTP masters and testing PTP client stability
|
 ----------------------------------------------------------------------------*/

#include <assert.h> // for assert
#include <math.h>
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "dbg_print.h" // for DBG_PRINT_ERROR, DBG_PRINTF_WARNING, ...
#include "filter.h"    // for average filter
#include "platform.h"  // from xcplib for SOCKET, socketSendTo, socketGetSendTime, ...

#include "ptp.h"

//-------------------------------------------------------------------------------------------------------
// XCP

#ifdef OPTION_ENABLE_XCP

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

// XCP test instrumentation events
static uint16_t gPtp_syncEvent = XCP_UNDEFINED_EVENT_ID; // on SYNC

#endif

//-------------------------------------------------------------------------------------------------------
// PTP master descriptor

#include "ptpHdr.h" // for struct announce from the PTP protocol

typedef struct {
    uint8_t domain;
    uint8_t uuid[8];
    uint8_t addr[4];
    struct announce a; // Announce header from the announce protocol message of this master
} tPtpMaster;

//-------------------------------------------------------------------------------------------------------
// PTP client descriptor

typedef struct {
    uint8_t addr[4];      // IP addr
    uint8_t id[8];        // clock UUID
    uint64_t time;        // DELAY_REQ time stmap
    int64_t diff;         // DELAY_REQ current timestamp - delay req timestamp
    int64_t lastSeenTime; // Last rx timestamp
    int64_t cycle;        // Last cycle time
    int64_t counter;      // Cycle counter
    uint32_t corr;        // PTP correction
    uint8_t domain;       // PTP domain
} tPtpClient;

//-------------------------------------------------------------------------------------------------------
// PTP state

typedef struct {

    // Sockets and communication
    uint8_t addr[4];    // local addr
    uint8_t maddr[4];   // multicast addr
    char interface[32]; // network interface name
    THREAD threadHandle;
    THREAD threadHandle320;
    THREAD threadHandle319;
    SOCKET sock320;
    SOCKET sock319;
    MUTEX mutex;

} tPtp;

static uint8_t gPtpDebugLevel = 3;
static uint8_t gPtpMode = PTP_MODE_OBSERVER;
static tPtp gPtp;

//-------------------------------------------------------------------------------------------------------

// Print information on a grandmaster
static void printMaster(const tPtpMaster *m) {

    printf("PTP Master:\n");
    const char *timesource = (m->a.timeSource == PTP_TIME_SOURCE_INTERNAL) ? "internal oscilator" : (m->a.timeSource == PTP_TIME_SOURCE_GPS) ? "GPS" : "Unknown";
    printf("    domain=%u, addr=%u.%u.%u.%u, id=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n"
           "    timesource=%s (%02X), utcOffset=%u, prio1=%u, class=%u, acc=%u, var=%u, prio2=%u, steps=%u\n",
           m->domain, m->addr[0], m->addr[1], m->addr[2], m->addr[3], m->uuid[0], m->uuid[1], m->uuid[2], m->uuid[3], m->uuid[4], m->uuid[5], m->uuid[6], m->uuid[7], timesource,
           m->a.timeSource, htons(m->a.utcOffset), m->a.priority1, m->a.clockClass, m->a.clockAccuraccy, htons(m->a.clockVariance), m->a.priority2, htons(m->a.stepsRemoved));
}

// Print a PTP frame
static void printFrame(char *prefix, struct ptphdr *ptp, uint8_t *addr, uint64_t rx_timestamp) {

    const char *s = NULL;
    switch (ptp->type) {
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
        printf("%s: %s (seqId=%u, timestamp=%" PRIu64 " from %u.%u.%u.%u - %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", prefix, s, htons(ptp->sequenceId), rx_timestamp, addr[0],
               addr[1], addr[2], addr[3], ptp->clockId[0], ptp->clockId[1], ptp->clockId[2], ptp->clockId[3], ptp->clockId[4], ptp->clockId[5], ptp->clockId[6], ptp->clockId[7]);
        if (ptp->type == PTP_DELAY_RESP)
            printf("  to %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", ptp->u.r.clockId[0], ptp->u.r.clockId[1], ptp->u.r.clockId[2], ptp->u.r.clockId[3], ptp->u.r.clockId[4],
                   ptp->u.r.clockId[5], ptp->u.r.clockId[6], ptp->u.r.clockId[7]);
        printf("\n");
    }
}

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
// PTP observer for master timing analysis

// (XCP tunable) parameters
typedef struct {
    uint8_t reset;                  // Reset PTP observer state
    int32_t t1_correction;          // Correction to apply to t1 timestamps
    uint8_t drift_filter_size;      // Size of the drift average filter
    uint8_t jitter_rms_filter_size; // Size of the jitter RMS average filter
    uint8_t jitter_avg_filter_size; // Size of the jitter average filter
    double max_correction;          // Maximum allowed servo correction per SYNC interval
    double servo_p_gain;            // Proportional gain (typically 0.1 - 0.5)
} observer_parameters_t;

// Default parameter values
static observer_parameters_t observer_params = {.reset = 0,
                                                .t1_correction = 3, // Apply 4ns correction to t1 to compensate for master timestamp rounding
                                                .drift_filter_size = 30,
                                                .jitter_rms_filter_size = 30,
                                                .jitter_avg_filter_size = 30,
                                                .max_correction = 1000.0, // 1000ns maximum correction per SYNC interval
                                                .servo_p_gain = 1.0};

#ifdef OPTION_ENABLE_XCP
// Create a tunable calibration segment handle for the observer parameters
tXcpCalSegIndex gParamsHandle = XCP_UNDEFINED_CALSEG;
#define lockObserverParams() (observer_parameters_t *)XcpLockCalSeg(gParamsHandle);
#define unlockObserverParams() XcpUnlockCalSeg(gParamsHandle);
#else
#define lockObserverParams() (&observer_params)
#define unlockObserverParams()
#endif

// PTP observer state
typedef struct {

    uint8_t domain;

    // Grandmaster info
    bool gmValid;
    tPtpMaster gm;

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

} tPtpC;

static tPtpC gPtpC;

// Initialize the PTP observer state
static void observerInit(uint8_t domain) {

    // Grandmaster info
    gPtpC.gmValid = false;
    gPtpC.domain = domain;

    // Init protocol state
    gPtpC.sync_local_time = 0;
    gPtpC.sync_master_time = 0;
    gPtpC.sync_correction = 0;
    gPtpC.sync_sequenceId = 0;
    gPtpC.sync_steps = 0;
    gPtpC.flup_master_time = 0;
    gPtpC.flup_correction = 0;
    gPtpC.flup_sequenceId = 0;

    observer_parameters_t *p = lockObserverParams();
    gPtpC.cycle_count = 0;
    gPtpC.t1_norm = 0;
    gPtpC.t2_norm = 0;
    gPtpC.sync_cycle_time = 1000000000;
    gPtpC.master_offset_raw = 0;
    gPtpC.master_drift_raw = 0;
    average_filter_init(&gPtpC.master_drift_filter, p->drift_filter_size);
    gPtpC.master_drift_raw = 0;
    gPtpC.master_drift = 0;
    gPtpC.master_drift_drift = 0;
    gPtpC.master_offset_compensation = 0;
    gPtpC.servo_integral = 0.0;
    gPtpC.master_jitter = 0;
    average_filter_init(&gPtpC.master_jitter_rms_filter, p->jitter_rms_filter_size);
    gPtpC.master_jitter_rms = 0;
    average_filter_init(&gPtpC.master_jitter_avg_filter, p->jitter_avg_filter_size);
    gPtpC.master_jitter_avg = 0;
    unlockObserverParams();
}

// Update the PTP observer state with each new SYNC (t1,t2) timestamps
static void observerUpdate(uint64_t t1_in, uint64_t correction, uint64_t t2_in) {

    char ts1[64], ts2[64];

    // t1 - master, t2 - local clock
    gPtpC.cycle_count++;

    if (gPtpDebugLevel >= 3)
        printf("PTP SYNC cycle %u:\n", gPtpC.cycle_count);
    if (gPtpDebugLevel >= 4) {
        printf("  t1 (SYNC tx on master (via PTP))  = %s (%" PRIu64 ") (%08X)\n", clockGetString(ts1, sizeof(ts1), t1_in), t1_in, (uint32_t)t1_in);
        printf("  t2 (SYNC rx)  = %s (%" PRIu64 ") (%08X)\n", clockGetString(ts2, sizeof(ts2), t2_in), t2_in, (uint32_t)t2_in);
        printf("  correction    = %" PRIu64 "ns\n", correction);
    }

    // Apply rounding correction to t1 ( Vector VN/VX PTP master has 8ns resolution, which leads to a systematic error )
    observer_parameters_t *p = lockObserverParams();
    t1_in = t1_in + p->t1_correction;
    unlockObserverParams();

    // Apply correction to t1
    t1_in += correction;

    // Master offset raw value
    gPtpC.master_offset_raw = (int64_t)(t1_in - t2_in); // Positive master_offset means master is ahead
    if (gPtpDebugLevel >= 4) {
        printf("    master_offset_raw   = %" PRIi64 " ns\n", gPtpC.master_offset_raw);
    }

    // First round, init
    if (gPtpC.t1_offset == 0 || gPtpC.t2_offset == 0) {

        gPtpC.t1_norm = 0; // corrected sync tx time on master clock
        gPtpC.t2_norm = 0; // sync rx time on slave clock

        // Normalization time offsets for t1,t2
        gPtpC.t1_offset = t1_in;
        gPtpC.t2_offset = t2_in;

        gPtpC.master_offset_compensation = 0;

        if (gPtpDebugLevel >= 3)
            printf("  Initial offsets: t1_offset=%" PRIu64 ", t2_offset=%" PRIu64 "\n", gPtpC.t1_offset, gPtpC.t2_offset);
    }

    // Analysis
    else {

        // Normalize t1,t2 to first round start time (may be negative in the beginning)
        int64_t t1_norm = (int64_t)(t1_in - gPtpC.t1_offset);
        int64_t t2_norm = (int64_t)(t2_in - gPtpC.t2_offset);

        if (gPtpDebugLevel >= 4)
            printf("  Normalized time: t1_norm=%" PRIi64 ", t2_norm=%" PRIi64 "\n", t1_norm, t2_norm);

        // Time differences since last SYNC, with correction applied to master time
        int64_t c1, c2;
        c1 = t1_norm - gPtpC.t1_norm; // time since last sync on master clock
        c2 = t2_norm - gPtpC.t2_norm; // time since last sync on local clock
        gPtpC.sync_cycle_time = c2;   // Update last cycle time

        if (gPtpDebugLevel >= 4)
            printf("  Cycle times: c1=%" PRIi64 ", c2=%" PRIi64 "\n", c1, c2);

        // Drift calculation
        int64_t diff = c2 - c1; // Positive diff = master clock faster than local clock
        if (gPtpDebugLevel >= 4)
            printf("  Cycle time diff: diff=%" PRIi64 "\n", diff);
        if (diff < -200000 || diff > +200000) { // Plausibility checking of cycle drift (max 200us per cycle)
            printf("WARNING: Master drift too high! dt=%lld ns \n", diff);
        } else {
            gPtpC.master_drift_raw = (double)diff * 1000000000 / c2; // Calculate drift in ppm instead of drift per cycle (drift is in ns/s (1/1000 ppm)
            double drift = average_filter_calc(&gPtpC.master_drift_filter, gPtpC.master_drift_raw); // Filter drift
            gPtpC.master_drift_drift = ((drift - gPtpC.master_drift) * 1000000000) / c2; // Calculate drift of drift in ns/s2 (should be close to zero when temperature is stable )
            gPtpC.master_drift = drift;
        }

        // Check if drift filter is warmed up
        if (average_filter_count(&gPtpC.master_drift_filter) < average_filter_size(&gPtpC.master_drift_filter)) {
            if (gPtpDebugLevel >= 3) {
                printf("  Master drift filter warming up (%zu)\n", average_filter_count(&gPtpC.master_drift_filter));
                printf("    master_drift_raw    = %g ns/s\n", gPtpC.master_drift_raw);
            }
        } else {
            if (gPtpDebugLevel >= 3) {
                printf("  Drift calculation:\n");
                printf("    master_drift_raw    = %g ns/s\n", gPtpC.master_drift_raw);
                printf("    master_drift        = %g ns/s\n", gPtpC.master_drift);
                printf("    master_drift_drift  = %g ns/s2\n", gPtpC.master_drift_drift);
            }

            // Calculate momentary master offset by detrending with current average drift
            gPtpC.master_offset_norm = t1_norm - t2_norm; // Positive master_offset means master is ahead
            if (gPtpDebugLevel >= 4) {
                printf("    master_offset_norm  = %" PRIi64 " ns\n", gPtpC.master_offset_norm);
            }

            if (gPtpC.master_offset_compensation == 0) {
                // Initialize compensation
                gPtpC.master_offset_compensation = gPtpC.master_offset_norm;
            } else {
                // Compensate drift
                // gPtpC.master_offset_compensation -= ((gPtpC.master_drift) * (double)gPtpC.sync_cycle_time) / 1000000000;
                // Compensate drift and drift of drift
                double n = average_filter_count(&gPtpC.master_drift_filter) / 2;
                gPtpC.master_offset_compensation -= ((gPtpC.master_drift + gPtpC.master_drift_drift * n) * (double)gPtpC.sync_cycle_time) / 1000000000;
            }
            gPtpC.master_offset_detrended = (double)gPtpC.master_offset_norm - gPtpC.master_offset_compensation;
            gPtpC.master_offset_detrended_filtered = average_filter_calc(&gPtpC.master_jitter_avg_filter, gPtpC.master_offset_detrended);
            if (gPtpDebugLevel >= 4) {
                printf("    master_offset_comp  = %g ns\n", gPtpC.master_offset_compensation);
            }
            if (gPtpDebugLevel >= 3) {
                printf("    master_offset = %g ns (detrended)\n", gPtpC.master_offset_detrended);
                printf("    master_offset = %g ns (filtered detrended)\n", gPtpC.master_offset_detrended_filtered);
            }

            // PI Servo Controller to prevent offset runaway
            // The offset error should ideally be zero-mean jitter.
            // Any persistent non-zero mean indicates drift estimation error that needs correction.
            double correction = gPtpC.master_offset_detrended_filtered;

            // P-term
            observer_parameters_t *p = lockObserverParams();
            correction *= p->servo_p_gain;

            // Correction rate limiting
            if (correction > p->max_correction)
                correction = p->max_correction;
            if (correction < -p->max_correction)
                correction = -p->max_correction;

            unlockObserverParams();

            // Apply correction
            gPtpC.master_offset_compensation += correction;
            average_filter_add(&gPtpC.master_jitter_avg_filter, -correction);
            printf("Applied compensation correction: %g ns\n", correction);

            // Jitter
            gPtpC.master_jitter = gPtpC.master_offset_detrended; // Jitter is the unfiltered detrended master offset
            gPtpC.master_jitter_rms =
                sqrt((double)average_filter_calc(&gPtpC.master_jitter_rms_filter, gPtpC.master_jitter * gPtpC.master_jitter)); // Filter jitter and calculate RMS
            if (gPtpDebugLevel >= 3) {
                printf("  Jitter analysis:\n");
                printf("    master_jitter       = %g ns\n", gPtpC.master_jitter);
                printf("    master_jitter_avg   = %g ns\n", gPtpC.master_jitter_avg);
                printf("    master_jitter_rms   = %g ns\n\n", gPtpC.master_jitter_rms);
            }
        }

        // Remember last normalized input values
        gPtpC.t1_norm = t1_norm; // sync tx time on master clock
        gPtpC.t2_norm = t2_norm; // sync rx time on slave clock
    }

    // XCP measurement event
#ifdef OPTION_ENABLE_XCP
    XcpEvent(gPtp_syncEvent);
#endif
}

static bool observerHandleFrame(SOCKET sock, int n, struct ptphdr *ptp, uint8_t *addr, uint64_t timestamp) {

    if (n >= 44 && n <= 64) {

        if (gPtpC.domain == ptp->domain) {

            if (gPtpC.gmValid && (ptp->type == PTP_SYNC || ptp->type == PTP_FOLLOW_UP)) {
                if (ptp->type == PTP_SYNC) {
                    if (timestamp == 0) {
                        DBG_PRINT_WARNING("PTP SYNC received without timestamp!\n");
                        return false;
                    }
                    gPtpC.sync_local_time = timestamp;
                    gPtpC.sync_master_time = htonl(ptp->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp->timestamp.timestamp_ns);
                    gPtpC.sync_sequenceId = htons(ptp->sequenceId);
                    gPtpC.sync_correction = (uint32_t)(htonll(ptp->correction) >> 16);
                    gPtpC.sync_steps = (htons(ptp->flags) & PTP_FLAG_TWO_STEP) ? 2 : 1;

                    // 1 step sync update
                    if (gPtpC.sync_steps == 1) {
                        observerUpdate(gPtpC.sync_master_time, gPtpC.sync_correction, gPtpC.sync_local_time);
                    }
                } else {

                    gPtpC.flup_master_time = htonl(ptp->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp->timestamp.timestamp_ns);
                    gPtpC.flup_sequenceId = htons(ptp->sequenceId);
                    gPtpC.flup_correction = (uint32_t)(htonll(ptp->correction) >> 16);
                }

                // 2 step sync update, SYNC and FOLLOW_UP may be received in any order (thread319 and thread320)
                if (gPtpC.sync_steps == 2 && gPtpC.sync_sequenceId == gPtpC.flup_sequenceId) {
                    observerUpdate(gPtpC.flup_master_time, gPtpC.sync_correction, gPtpC.sync_local_time); // 2 step
                }

            } else if (ptp->type == PTP_ANNOUNCE) {
                if (!gPtpC.gmValid) {
                    printf("Found active PTP master:\n");
                    gPtpC.gmValid = true;
                    gPtpC.gm.a = ptp->u.a;
                    gPtpC.gm.domain = ptp->domain;
                    memcpy(gPtpC.gm.uuid, ptp->clockId, 8);
                    memcpy(gPtpC.gm.addr, addr, 4);
                    printMaster(&gPtpC.gm);
                }
            }

        } // from active master
    } else {
        if (gPtpDebugLevel >= 3) {
            printf("Received PTP frame of type %d with length %d, not handled\n", ptp->type, n);
            printFrame("RX", ptp, addr, timestamp);
        }
    }

    return true;
}

//-------------------------------------------------------------------------------------------------------
// A2L and XCP for the PTP observer

#ifdef OPTION_ENABLE_XCP

// Create XCP events
void observerCreateXcpEvents() { gPtp_syncEvent = XcpCreateEvent("SYNC", 0, 0); }

// Create XCP calibration parameters
void observerCreateXcpParameters() {

    gParamsHandle = XcpCreateCalSeg("observer_params", &observer_params, sizeof(observer_params));

    A2lSetSegmentAddrMode(gParamsHandle, observer_params);

    A2lCreateParameter(observer_params.reset, "Reset PTP observer state", "", 0, 1);
    A2lCreateParameter(observer_params.t1_correction, "Correction for t1", "", -100, 100);

    A2lCreateParameter(observer_params.drift_filter_size, "Drift filter size", "", 1, 300);
    A2lCreateParameter(observer_params.jitter_rms_filter_size, "Jitter RMS filter size", "", 1.0, 300.0);
    A2lCreateParameter(observer_params.jitter_avg_filter_size, "Jitter average filter size", "", 1.0, 300.0);

    A2lCreateParameter(observer_params.max_correction, "Maximum correction per cycle", "ns", 0.0, 1000.0);
    A2lCreateParameter(observer_params.servo_p_gain, "Proportional gain for servo", "", 0.0, 1.0);
}

// Create A2L description for measurements
void observerCreateA2lDescription() {

    // Event measurements
    A2lSetAbsoluteAddrMode__i(gPtp_syncEvent);

    A2lCreateMeasurement(gPtpC.gm.domain, "domain");
    A2lCreateMeasurementArray(gPtpC.gm.uuid, "Grandmaster UUID");
    A2lCreateMeasurementArray(gPtpC.gm.addr, "Grandmaster IP address");

    A2lCreateMeasurement(gPtpC.sync_local_time, "SYNC RX timestamp");
    A2lCreateMeasurement(gPtpC.sync_master_time, "SYNC timestamp");
    A2lCreatePhysMeasurement(gPtpC.sync_correction, "SYNC correction", "ns", 0, 1000000);
    A2lCreateMeasurement(gPtpC.sync_sequenceId, "SYNC sequence counter");
    A2lCreateMeasurement(gPtpC.sync_steps, "SYNC mode");
    A2lCreatePhysMeasurement(gPtpC.sync_cycle_time, "SYNC cycle time", "ns", 999999900, 1000000100);

    A2lCreateMeasurement(gPtpC.flup_master_time, "FOLLOW_UP timestamp");
    A2lCreateMeasurement(gPtpC.flup_sequenceId, "FOLLOW_UP sequence counter");
    A2lCreatePhysMeasurement(gPtpC.flup_correction, "FOLLOW_UP correction", "ns", 0, 1000000);

    A2lCreatePhysMeasurement(gPtpC.t1_norm, "t1 normalized to startup reference time t1_offset", "ns", 0, +1000000);
    A2lCreatePhysMeasurement(gPtpC.t2_norm, "t2 normalized to startup reference time t2_offset", "ns", 0, +1000000);

    A2lCreatePhysMeasurement(gPtpC.master_drift_raw, "", "ppm*1000", -100, +100);
    A2lCreatePhysMeasurement(gPtpC.master_drift, "", "ppm*1000", -100, +100);
    A2lCreatePhysMeasurement(gPtpC.master_drift_drift, "", "ppm*1000", -10, +10);

    A2lCreatePhysMeasurement(gPtpC.master_offset_raw, "t1-t2 raw value (not used)", "ns", -1000000, +1000000);
    A2lCreatePhysMeasurement(gPtpC.master_offset_compensation, "offset for detrending", "ns", -1000, +1000);
    A2lCreatePhysMeasurement(gPtpC.master_offset_detrended, "detrended master offset", "ns", -1000, +1000);
    A2lCreatePhysMeasurement(gPtpC.master_offset_detrended_filtered, "filtered detrended master offset", "ns", -1000, +1000);

    A2lCreatePhysMeasurement(gPtpC.master_jitter, "offset jitter raw value", "ns", -1000, +1000);
    A2lCreatePhysMeasurement(gPtpC.master_jitter_rms, "Jitter root mean square", "ns", -1000, +1000);
    A2lCreatePhysMeasurement(gPtpC.master_jitter_avg, "Jitter average", "ns", -1000, +1000);
}
#endif

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
static announce_parameters_t announce_params = {
    .utcOffset = 37,
    .clockClass = PTP_CLOCK_CLASS_PTP_PRIMARY,
    .clockAccuraccy = PTP_CLOCK_ACC_GPS,
    .clockVariance = 0,
    .stepsRemoved = 0,
    .timeSource = PTP_TIME_SOURCE_GPS,
    .priority1 = 0,
    .priority2 = 0,
};

// (XCP tunable) master parameters
typedef struct master_params {
    uint32_t announceCycleTimeMs; // Announce message cycle time in ms
    uint32_t syncCycleTimeMs;     // SYNC message cycle time in ms
} master_parameters_t;

// Default master parameter values
static master_parameters_t master_params = {
    .announceCycleTimeMs = 2000, // Announce every 2s
    .syncCycleTimeMs = 1000      // SYNC every 1s
};

#ifdef OPTION_ENABLE_XCP
// Create a tunable calibration segment handle for the master parameters
tXcpCalSegIndex gMasterParamsHandle = XCP_UNDEFINED_CALSEG;
#define LockMasterParams() ((void *)XcpLockCalSeg(gMasterParamsHandle))
#define UnlockMasterParams(x) XcpUnlockCalSeg(gMasterParamsHandle)
#else
#define LockMasterParams() (&master_params)
#define UnlockMasterParams(x)
#endif

typedef struct master_state {
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
} master_state_t;

static master_state_t gPtpM;

//---------------------------------------------------------------------------------------
// PTP master message sending

// Init constant values in PTP header
static void initHeader(struct ptphdr *h, uint8_t type, uint16_t len, uint16_t flags, uint16_t sequenceId) {

    master_parameters_t *p = (master_parameters_t *)LockMasterParams();

    memset(h, 0, sizeof(struct ptphdr));
    h->version = 2;
    h->domain = gPtpM.domain;
    memcpy(h->clockId, gPtpM.uuid, 8);
    h->sourcePortId = htons(1);
    h->logMessageInterval = 0;
    h->type = type;
    h->len = htons(len);
    h->flags = htons(flags);
    h->sequenceId = htons(sequenceId);

    UnlockMasterParams(p);

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

static bool ptpSendAnnounce() {

    struct ptphdr h;
    int16_t l;

    initHeader(&h, PTP_ANNOUNCE, 64, 0, ++gPtpM.sequenceIdAnnounce);
    h.u.a.utcOffset = htons(announce_params.utcOffset);
    h.u.a.stepsRemoved = htons(announce_params.stepsRemoved);
    memcpy(h.u.a.grandmasterId, gPtpM.uuid, 8);
    h.u.a.clockVariance = htons(announce_params.clockVariance); // Allan deviation
    h.u.a.clockAccuraccy = announce_params.clockAccuraccy;
    h.u.a.clockClass = announce_params.clockClass;
    h.u.a.priority1 = announce_params.priority1;
    h.u.a.priority2 = announce_params.priority2;
    h.u.a.timeSource = announce_params.timeSource;
    l = socketSendTo(gPtp.sock320, (uint8_t *)&h, 64, gPtp.maddr, 320, NULL);

    if (gPtpDebugLevel >= 2) {
        printf("TX ANNOUNCE %u %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", gPtpM.sequenceIdAnnounce, h.clockId[0], h.clockId[1], h.clockId[2], h.clockId[3], h.clockId[4],
               h.clockId[5], h.clockId[6], h.clockId[7]);
    }

    return (l == 64);
}

static bool ptpSendSync(uint64_t *sync_txTimestamp) {

    struct ptphdr h;
    int16_t l;

    assert(sync_txTimestamp != NULL);

    initHeader(&h, PTP_SYNC, 44, PTP_FLAG_TWO_STEP, ++gPtpM.sequenceIdSync);
    l = socketSendTo(gPtp.sock319, (uint8_t *)&h, 44, gPtp.maddr, 319, sync_txTimestamp /* request tx time stamp */);
    if (l != 44) {
        printf("ERROR: ptpSendSync: socketSendTo failed, returned l = %d\n", l);
        return false;
    }
    if (*sync_txTimestamp == 0) { // If timestamp not obtained during send, get it now
        *sync_txTimestamp = socketGetSendTime(gPtp.sock319);
        if (*sync_txTimestamp == 0) {
            printf("ERROR: ptpSendSync: socketGetSendTime failed, no tx timestamp available\n");
            return false;
        }
    }
    if (gPtpDebugLevel >= 2) {
        printf("TX SYNC %u, tx time = %" PRIu64 "\n", gPtpM.sequenceIdSync, *sync_txTimestamp);
    }
    return true;
}

static bool ptpSendSyncFollowUp(uint64_t sync_txTimestamp) {

    struct ptphdr h;
    int16_t l;

    initHeader(&h, PTP_FOLLOW_UP, 44, 0, gPtpM.sequenceIdSync);
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

    l = socketSendTo(gPtp.sock320, (uint8_t *)&h, 44, gPtp.maddr, 320, NULL);

    if (gPtpDebugLevel >= 2) {
        char ts[64];
        printf("TX FLUP %u t1 = %s (%" PRIu64 ")\n", gPtpM.sequenceIdSync, clockGetString(ts, sizeof(ts), t1), t1);
    }
    return (l == 44);
}

static bool ptpSendDelayResponse(struct ptphdr *req, uint64_t delayreg_rxTimestamp) {

    struct ptphdr h;
    int16_t l;

    initHeader(&h, PTP_DELAY_RESP, 54, 0, htons(req->sequenceId)); // copy sequence id
    h.correction = req->correction;                                // copy correction
    h.u.r.sourcePortId = req->sourcePortId;                        // copy request egress port id
    memcpy(h.u.r.clockId, req->clockId, 8);                        // copy request clock id

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

    l = socketSendTo(gPtp.sock320, (uint8_t *)&h, 54, gPtp.maddr, 320, NULL);

    if (gPtpDebugLevel >= 2) {
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

static void initClientList() {
    gPtpM.clientCount = 0;
    for (uint16_t i = 0; i < gPtpM.clientCount; i++)
        memset(&gPtpM.client[i], 0, sizeof(gPtpM.client[i]));
}

void printClient(uint16_t i) {

    char ts[64];
    printf("%u: addr=x.x.x.%u: domain=%u uuid=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X time=%s corr=%u diff=%" PRId64 " cycle=%" PRId64 " \n", i, gPtpM.client[i].addr[3],
           gPtpM.client[i].domain, gPtpM.client[i].id[0], gPtpM.client[i].id[1], gPtpM.client[i].id[2], gPtpM.client[i].id[3], gPtpM.client[i].id[4], gPtpM.client[i].id[5],
           gPtpM.client[i].id[6], gPtpM.client[i].id[7], clockGetString(ts, sizeof(ts), gPtpM.client[i].time), gPtpM.client[i].corr, gPtpM.client[i].diff, gPtpM.client[i].cycle);
}

static uint16_t lookupClient(uint8_t *addr, uint8_t *uuid) {
    uint16_t i;
    for (i = 0; i < gPtpM.clientCount; i++) {
        if (memcmp(addr, gPtpM.client[i].addr, 4) == 0)
            return i;
    }
    return 0xFFFF;
}

static uint16_t addClient(uint8_t *addr, uint8_t *uuid, uint8_t domain) {

    uint16_t i = lookupClient(addr, uuid);
    if (i < MAX_CLIENTS)
        return i;
    i = gPtpM.clientCount;
    gPtpM.client[i].domain = domain;
    memcpy(gPtpM.client[i].addr, addr, 4);
    memcpy(gPtpM.client[i].id, uuid, 8);
    gPtpM.clientCount++;
    return i;
}

//-------------------------------------------------------------------------------------------------------

// Print info
void masterPrintInfo() {

    char ts[64];
    uint64_t t;

    printf("\nMaster Info:\n");
    printf(" UUID:           %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", gPtpM.uuid[0], gPtpM.uuid[1], gPtpM.uuid[2], gPtpM.uuid[3], gPtpM.uuid[4], gPtpM.uuid[5], gPtpM.uuid[6],
           gPtpM.uuid[7]);
    printf(" IP:             %u.%u.%u.%u\n", gPtp.addr[0], gPtp.addr[1], gPtp.addr[2], gPtp.addr[3]);
    printf(" Interface:      %s\n", gPtp.interface);
    printf(" Domain:         %u\n", gPtpM.domain);
    master_parameters_t *p = (master_parameters_t *)LockMasterParams();
    printf(" Announce cycle: %ums\n", p->announceCycleTimeMs);
    printf(" Sync cycle:     %ums\n", p->syncCycleTimeMs);
    printf("Client list:\n");
    for (uint16_t i = 0; i < gPtpM.clientCount; i++) {
        printClient(i);
    }
    printf("\n");
}

// Initialize the PTP master state
static void masterInit(uint8_t domain, uint8_t *uuid) {

    gPtpM.domain = domain;
    memcpy(gPtpM.uuid, uuid, sizeof(gPtpM.uuid));
    initClientList();

    master_parameters_t *p = (master_parameters_t *)LockMasterParams();

    uint64_t t = clockGet();
    gPtpM.announceCycleTimer = 0;                                                                  // Send announce immediately
    gPtpM.syncCycleTimer = t + 100 * CLOCK_TICKS_PER_MS - p->syncCycleTimeMs * CLOCK_TICKS_PER_MS; // First SYNC after 100ms
    gPtpM.syncTxTimestamp = 0;
    gPtpM.sequenceIdAnnounce = 0;
    gPtpM.sequenceIdSync = 0;

    UnlockMasterParams();
}

// Master main cycle
static void masterCycle() {

    printf("c");

    for (;;) {

        uint64_t t = clockGet();

        master_parameters_t *p = (master_parameters_t *)LockMasterParams();
        uint32_t announceCycleTimeMs = p->announceCycleTimeMs; // Announce message cycle time in ms
        uint32_t syncCycleTimeMs = p->syncCycleTimeMs;         // SYNC message cycle time in ms
        UnlockMasterParams();

        // Announce cycle
        if (announceCycleTimeMs > 0 && t - gPtpM.announceCycleTimer > announceCycleTimeMs * CLOCK_TICKS_PER_MS) {
            gPtpM.announceCycleTimer = t;
            if (!ptpSendAnnounce())
                break;
        }

        // Sync cycle
        if (syncCycleTimeMs > 0 && t - gPtpM.syncCycleTimer > syncCycleTimeMs * CLOCK_TICKS_PER_MS) {
            gPtpM.syncCycleTimer = t;

            mutexLock(&gPtp.mutex);
            if (!ptpSendSync(&gPtpM.syncTxTimestamp)) {
                printf("ERROR: Failed to send SYNC\n");
                mutexUnlock(&gPtp.mutex);
                break;
            }

            if (gPtpM.syncTxTimestamp == 0) {
                printf("ERROR: SYNC tx timestamp not available !\n");
            } else {
                if (!ptpSendSyncFollowUp(gPtpM.syncTxTimestamp)) {
                    printf("ERROR:Failed to send SYNC FOLLOW UP\n");
                    mutexUnlock(&gPtp.mutex);
                    break;
                }
            }
            mutexUnlock(&gPtp.mutex);
        }
    }
}

// Handle a received Delay Request message
static bool masterHandleFrame(SOCKET sock, int n, struct ptphdr *ptp, uint8_t *addr, uint64_t rxTimestamp) {

    if (n >= 44 && n <= 64) {

        if (ptp->type == PTP_DELAY_REQ) {

            if (ptp->domain == gPtpM.domain) {
                bool ok;
                mutexLock(&gPtp.mutex);
                ok = ptpSendDelayResponse(ptp, rxTimestamp);
                mutexUnlock(&gPtp.mutex);
                if (!ok)
                    return false;
            }

            // Maintain PTP client list
            uint16_t i = lookupClient(addr, ptp->clockId);
            bool newClient = (i >= MAX_CLIENTS);
            if (newClient) {
                i = addClient(addr, ptp->clockId, ptp->domain);
            }

            // Some clients send non zero timestamp values in their DELAY_REQ which allows to visualize information on time synchronisation quality
            gPtpM.client[i].time = htonl(ptp->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp->timestamp.timestamp_ns);
            gPtpM.client[i].diff = rxTimestamp - gPtpM.client[i].time;
            gPtpM.client[i].corr = (uint32_t)(htonll(ptp->correction) >> 16);
            gPtpM.client[i].cycle = rxTimestamp - gPtpM.client[i].lastSeenTime;
            gPtpM.client[i].counter++;
            gPtpM.client[i].lastSeenTime = rxTimestamp;

            // Log
            if (gPtpDebugLevel >= 1 && newClient) {
                printClient(i);
            }
        }
    }
    return true;
}

//-------------------------------------------------------------------------------------------------------
// A2L and XCP for the PTP observer

#ifdef OPTION_ENABLE_XCP

// Create XCP events
void masterCreateXcpEvents() { gPtp_syncEvent = XcpCreateEvent("SYNC", 0, 0); }

// Create XCP calibration parameters
void masterCreateXcpParameters() {

    gMasterParamsHandle = XcpCreateCalSeg("master_params", &master_params, sizeof(master_params));
    A2lSetSegmentAddrMode(gMasterParamsHandle, master_params);
    A2lCreateParameter(master_params.announceCycleTimeMs, "Announce cycle time (ms)", "", 0, 10000);
    A2lCreateParameter(master_params.syncCycleTimeMs, "Sync cycle time (ms)", "", 0, 10000);
}

// Create A2L description for measurements
void masterCreateA2lDescription() {

    A2lTypedefBegin(tPtpClient, NULL, "PTP client structure");
    A2lTypedefMeasurementComponent(counter, "cycle counter");
    A2lTypedefPhysMeasurementComponent(cycle, "Cycle time", "ns", 0, 1000000000);
    A2lTypedefMeasurementArrayComponent(addr, "IP address");
    A2lTypedefMeasurementArrayComponent(id, "Clock UUID");
    A2lTypedefMeasurementComponent(time, "DELAY_REQ timestamp (t3)");
    A2lTypedefMeasurementComponent(corr, "DELAY_REQ correction");
    A2lTypedefPhysMeasurementComponent(diff, "Timestamp difference (t4 - t3)", "ns", -1000000000, +1000000000);
    A2lTypedefEnd();

    A2lSetAbsoluteAddrMode__i(gPtp_syncEvent);
    A2lCreateMeasurement(gPtpM.clientCount, "Number of PTP clients");
    A2lCreateTypedefInstanceArray(gPtpM.client, tPtpClient, MAX_CLIENTS, "PTP client list");
    A2lCreateMeasurement(gPtpM.syncTxTimestamp, "SYNC tx timestamp");
    A2lCreateMeasurement(gPtpM.sequenceIdAnnounce, "Announce sequence id");
    A2lCreateMeasurement(gPtpM.sequenceIdSync, "SYNC sequence id");
}
#endif

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
// Threads

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

    (void)par;
    for (;;) {
        n = socketRecvFrom(gPtp.sock319, buffer, (uint16_t)sizeof(buffer), addr, NULL, &rxTime);
        if (n <= 0)
            break; // Terminate on error or socket close
        if (gPtpDebugLevel >= 4)
            printFrame("RX", (struct ptphdr *)buffer, addr, rxTime); // Print incoming PTP traffic
        if (gPtpMode == PTP_MODE_MASTER)
            masterHandleFrame(gPtp.sock319, n, (struct ptphdr *)buffer, addr, rxTime);
        if (gPtpMode == PTP_MODE_OBSERVER)
            observerHandleFrame(gPtp.sock319, n, (struct ptphdr *)buffer, addr, rxTime);
    }
    if (gPtpDebugLevel >= 3)
        printf("Terminate PTP multicast 319 thread\n");
    socketClose(&gPtp.sock319);
    return 0;
}

// General messages (Announce, Follow_Up, Delay_Resp) on port 320
// To keep track of other master activities
// Does not need rx timestamping
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

    (void)par;
    for (;;) {
        n = socketRecvFrom(gPtp.sock320, buffer, (uint16_t)sizeof(buffer), addr, NULL, NULL);
        if (n <= 0)
            break; // Terminate on error or socket close
        if (gPtpDebugLevel >= 4)
            printFrame("RX", (struct ptphdr *)buffer, addr, 0); // Print incoming PTP traffic
        if (gPtpMode == PTP_MODE_OBSERVER)
            observerHandleFrame(gPtp.sock320, n, (struct ptphdr *)buffer, addr, 0);
        if (gPtpMode == PTP_MODE_MASTER) {
            masterHandleFrame(gPtp.sock320, n, (struct ptphdr *)buffer, addr, 0);
        }
    }
    if (gPtpDebugLevel >= 3)
        printf("Terminate PTP multicast 320 thread\n");
    socketClose(&gPtp.sock320);
    return 0;
}

#ifdef _WIN
static DWORD WINAPI ptpThread(LPVOID par)
#else
static void *ptpThread(void *par)
#endif
{
    (void)par;

    for (;;) {
        sleepMs(10);
        if (gPtpMode == PTP_MODE_MASTER) {
            masterCycle();
        }
    }

    return 0;
}

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
// Public functions

// Start PTP
// If bindAddr = INADDR_ANY, bind to given interface
// Enable hardware timestamps on interface (requires root privileges)
bool ptpInit(uint8_t mode, uint8_t domain, uint8_t *uuid, uint8_t *bindAddr, char *interface, uint8_t debugLevel) {

    gPtpDebugLevel = debugLevel;
    gPtpMode = mode;
    memset(&gPtpC, 0, sizeof(gPtpC));

    // PTP parameters
    memcpy(gPtp.addr, bindAddr, 4);
    strncpy(gPtp.interface, interface ? interface : "", sizeof(gPtp.interface) - 1);

    if (mode == PTP_MODE_OBSERVER) {
#ifdef OPTION_ENABLE_XCP
        observerCreateXcpEvents();
        observerCreateXcpParameters();
        observerCreateA2lDescription();
#endif
        observerInit(domain);
    }
    if (mode == PTP_MODE_MASTER) {
#ifdef OPTION_ENABLE_XCP
        masterCreateXcpEvents();
        masterCreateXcpParameters();
        masterCreateA2lDescription();
#endif
        masterInit(domain, uuid);
    }

    // Create sockets for event (319) and general messages (320)
    gPtp.sock319 = gPtp.sock320 = INVALID_SOCKET;

    // For multicast reception on a specific interface:
    // - When bindAddr is INADDR_ANY and interface is specified: bind to ANY and use socketBindToDevice (SO_BINDTODEVICE)
    // - When bindAddr is specific: bind to that address (works only if multicast source is on same subnet)
    bool useBindToDevice = (interface != NULL && bindAddr[0] == 0 && bindAddr[1] == 0 && bindAddr[2] == 0 && bindAddr[3] == 0);

    // SYNC with tx (master) or rx (observer) timestamp, DELAY_REQ - with rx timestamps
    if (!socketOpen(&gPtp.sock319, SOCKET_MODE_BLOCKING | SOCKET_MODE_TIMESTAMPING))
        return false;
    if (!socketBind(gPtp.sock319, bindAddr, 319))
        return false;
    if (useBindToDevice && !socketBindToDevice(gPtp.sock319, interface))
        return false;

    // General messages ANNOUNCE, FOLLOW_UP, DELAY_RESP - without rx timestamps
    if (!socketOpen(&gPtp.sock320, SOCKET_MODE_BLOCKING))
        return false;
    if (!socketBind(gPtp.sock320, bindAddr, 320))
        return false;
    if (useBindToDevice && !socketBindToDevice(gPtp.sock320, interface))
        return false;

    // Enable hardware timestamps for SYNC tx and DELAY_REQ messages (requires root privileges)
    if (!socketEnableHwTimestamps(gPtp.sock319, interface, true /* tx + rx PTP only*/)) {
        DBG_PRINT_ERROR("Hardware timestamping not enabled (may need root), using software timestamps\n");
        // return false;
    }

    if (gPtpDebugLevel >= 2) {
        if (useBindToDevice)
            printf("  Bound PTP sockets to interface %s\n", interface);
        else
            printf("  Bound PTP sockets to %u.%u.%u.%u:320/319\n", bindAddr[0], bindAddr[1], bindAddr[2], bindAddr[3]);
    }

    // Join PTP multicast group
    if (gPtpDebugLevel >= 2)
        printf("  Listening for PTP multicast on 224.0.1.129 %s\n", interface ? interface : "");
    uint8_t maddr[4] = {224, 0, 1, 129};
    memcpy(gPtp.maddr, maddr, 4);
    if (!socketJoin(gPtp.sock319, gPtp.maddr, bindAddr, interface))
        return false;
    if (!socketJoin(gPtp.sock320, gPtp.maddr, bindAddr, interface))
        return false;

    // Start all PTP threads
    mutexInit(&gPtp.mutex, true, 1000);
    create_thread(&gPtp.threadHandle320, ptpThread320);
    create_thread(&gPtp.threadHandle319, ptpThread319);
    create_thread(&gPtp.threadHandle, ptpThread);

    return true;
}

// Check health status and perform some background tasks
// This is called from the application on a regular basis
// It monitors the status and checks for reset requests via calibration parameters
bool ptpCheckStatus(void) {

    if (gPtpMode == PTP_MODE_OBSERVER) {

        // Reset request via calibration parameter
        observer_parameters_t *p = lockObserverParams();
        uint8_t reset = p->reset; // Get the reset calibration parameter
        p->reset = 0;             // Clear the reset request
        unlockObserverParams();
        if (reset != 0) {
            printf("PTP observer reset requested via calibration parameter\n");
            // Reset grandmaster info
            gPtpC.gmValid = false;
            sleepMs(100);
            // Reset PTP observer analysis state
            observerInit(gPtpC.domain);
        }
    }

    if (gPtpMode == PTP_MODE_MASTER) {
        masterPrintInfo();
    }

    return true;
}

// Stop PTP
void ptpShutdown() {

    cancel_thread(gPtp.threadHandle);
    cancel_thread(gPtp.threadHandle320);
    cancel_thread(gPtp.threadHandle319);
    sleepMs(200);
    socketClose(&gPtp.sock319);
    socketClose(&gPtp.sock320);
}
