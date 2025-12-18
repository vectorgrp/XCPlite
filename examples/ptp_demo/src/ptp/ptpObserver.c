/*----------------------------------------------------------------------------
| File:
|   ptpObserver.c
|
| Description:
|   PTP client for XL-API64
|
 ----------------------------------------------------------------------------*/

#include <assert.h> // for assert
#include <math.h>
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "ptp_cfg.h"

#ifdef OPTION_ENABLE_PTP_OBSERVER

#include "platform.h" // for SOCKET, socketSendTo, socketGetSendTime, ...
#include "ptpObserver.h"
#include "util.h" // for filter_average_t, average_init, average_calc

// XCP
#ifdef OPTION_ENABLE_PTP_XCP
#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface
#endif

//-------------------------------------------------------------------------------------------------------
// PTP master descriptor

#include "ptpHdr.h" // for struct announce from the PTP protocol

typedef struct {

    uint16_t index;
    uint8_t domain;
    uint8_t uuid[8];
    uint8_t addr[4];

    struct announce a; // Announce header from the announce protocol message of this master

} tPtpMaster;

//-------------------------------------------------------------------------------------------------------

// PTP observer status
typedef struct {

    uint8_t domain;

    // Sockets and communication
    uint8_t addr[4];  // local addr
    uint8_t maddr[4]; // multicast addr
    THREAD threadHandle;
    THREAD threadHandle320;
    THREAD threadHandle319;
    SOCKET sock320;
    SOCKET sock319;
    MUTEX mutex;

    // Grandmaster info
    bool gmValid;
    tPtpMaster gm;

    // Grandmaster SYNC and FOLLOW_UP measurements
    uint64_t sync_local_time;
    uint64_t sync_master_time;
    uint32_t sync_correction;
    uint16_t sync_sequenceId;
    uint64_t sync_cycle_time;
    uint8_t sync_steps;
    uint64_t flup_master_time;
    uint32_t flup_correction;
    uint16_t flup_sequenceId;

    // PTP timing analysis state, all values in nanoseconds and per second units
    uint32_t cycle_count;
    uint64_t t1_norm, t2_norm;          // Input normalized timestamps
    uint64_t t1_offset, t2_offset;      // Normalization offsets
    int64_t master_drift_raw;           // Raw momentary drift
    int64_t master_drift;               // Filtered drift over MASTER_DRIFT_FILTER_SIZE cycles
    int64_t master_drift_drift;         // Drift of the drift
    int64_t master_offset_raw;          // momentary raw master offset t1-t2
    int64_t master_offset_norm;         // normailzed master offset t1_norm-t2_norm
    int64_t master_offset_compensation; // normalized master_offset compensation servo offset
    int64_t master_offset_detrended;    // normalized master_offset error (detrended master_offset_norm)
    int64_t master_jitter;              // jitter
    double master_jitter_rms;           // jitter root mean square
    double master_jitter_avg;           // jitter average
    double servo_integral;              // PI servo controller state: Integral accumulator for I-term
    int64_t servo_correction;           // PI servo controller state: Total servo correction applied
    filter_average_t master_drift_filter;
    filter_average_t master_jitter_rms_filter;
    filter_average_t master_jitter_avg_filter;

} tPtpC;

static tPtpC gPtpC;

#ifdef OPTION_ENABLE_PTP_XCP
// XCP test instrumentation events
static uint16_t gPtpC_syncEvent = XCP_UNDEFINED_EVENT_ID; // on SYNC
#endif

//-------------------------------------------------------------------------------------------------------

// Print information on a grandmaster
static void printMaster(const tPtpMaster *m) {

    printf("  Master %u:\n", m->index);
    const char *timesource = (m->a.timeSource == PTP_TIME_SOURCE_INTERNAL) ? "internal oscilator" : (m->a.timeSource == PTP_TIME_SOURCE_GPS) ? "GPS" : "Unknown";
    printf("    domain=%u, addr=%u.%u.%u.%u, id=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n"
           "    timesource=%s (%02X), utcOffset=%u, prio1=%u, class=%u, acc=%u, var=%u, prio2=%u, steps=%u\n",
           m->domain, m->addr[0], m->addr[1], m->addr[2], m->addr[3], m->uuid[0], m->uuid[1], m->uuid[2], m->uuid[3], m->uuid[4], m->uuid[5], m->uuid[6], m->uuid[7], timesource,
           m->a.timeSource, htons(m->a.utcOffset), m->a.priority1, m->a.clockClass, m->a.clockAccuraccy, htons(m->a.clockVariance), m->a.priority2, htons(m->a.stepsRemoved));
}

// Print info
void ptpObserverPrintInfo() {

    printf("\nClient Info:\n");
    printf("UUID:   %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", gPtpC.gm.uuid[0], gPtpC.gm.uuid[1], gPtpC.gm.uuid[2], gPtpC.gm.uuid[3], gPtpC.gm.uuid[4], gPtpC.gm.uuid[5],
           gPtpC.gm.uuid[6], gPtpC.gm.uuid[7]);
    printf("IP:     %u.%u.%u.%u\n", gPtpC.addr[0], gPtpC.addr[1], gPtpC.addr[2], gPtpC.addr[3]);
    printf("Domain: %u\n", gPtpC.domain);
    if (gPtpC.gmValid) {
        printf("Master: \n");
        printMaster(&gPtpC.gm);
    }
    printf("\n");
}

//-------------------------------------------------------------------------------------------------------
// PTP master timing analysis

#define MASTER_DRIFT_FILTER_SIZE 16
#define MASTER_JITTER_RMS_FILTER_SIZE 32
#define MASTER_JITTER_AVG_FILTER_SIZE 32

// PI servo controller initialization
// Gains tuned for typical PTP SYNC rates (1-8 Hz)
// P-gain provides fast initial convergence
// I-gain eliminates steady-state error from drift estimation errors
// servo_p_gain = 0.2;   // Proportional gain: moderate response
// servo_i_gain = 0.005; // Integral gain: slow accumulation to avoid oscillation

// Calibration parameters structure
typedef struct params {
    uint8_t reset;       // Reset PTP observer state
    int32_t correction;  // Correction to apply to t1 timestamps
    double servo_p_gain; // Proportional gain (typically 0.1 - 0.5)
    double servo_i_gain; // Integral gain (typically 0.001 - 0.01)
} parameters_t;

// Default values (reference page, "FLASH") for the calibration parameters
const parameters_t params = {.reset = false, .correction = 4, .servo_p_gain = 0.4, .servo_i_gain = 0.01};

// A global calibration segment handle for the calibration parameters
// A calibration segment has a working page ("RAM") and a reference page ("FLASH"), it is described by a MEMORY_SEGMENT in the A2L file
// Using the calibration segment to access parameters assures safe (thread safe against XCP modifications), wait-free and consistent access
// It supports RAM/FLASH page switching, reinitialization (copy FLASH to RAM page) and persistence (save RAM page to BIN file)
#ifdef OPTION_ENABLE_PTP_XCP
tXcpCalSegIndex gParams = XCP_UNDEFINED_CALSEG;
#else
#define XcpLockCalSeg(x) ((void *)&params)
#define XcpUnlockCalSeg(x)
#endif

static void syncInit() {

    gPtpC.cycle_count = 0;
    gPtpC.t1_norm = 0;
    gPtpC.t2_norm = 0;
    gPtpC.sync_cycle_time = 1000000000;
    gPtpC.master_offset_raw = 0;
    gPtpC.master_drift_raw = 0;
    average_init(&gPtpC.master_drift_filter, MASTER_DRIFT_FILTER_SIZE);
    gPtpC.master_drift = 0;
    gPtpC.master_drift_drift = 0; // Drift of the drift
    gPtpC.master_offset_compensation = 0;
    gPtpC.servo_integral = 0.0;
    gPtpC.servo_correction = 0;
    gPtpC.master_jitter = 0;
    average_init(&gPtpC.master_jitter_rms_filter, MASTER_JITTER_RMS_FILTER_SIZE);
    gPtpC.master_jitter_rms = 0;
    average_init(&gPtpC.master_jitter_avg_filter, MASTER_JITTER_AVG_FILTER_SIZE);
    gPtpC.master_jitter_avg = 0;
}

static void syncUpdate(uint64_t t1_in, uint64_t correction, uint64_t t2_in) {

    char ts1[64], ts2[64];

    // t1 - master, t2 - local clock
    gPtpC.cycle_count++;

    if (gPtpDebugLevel >= 4) {
        printf("  t1 (SYNC tx on master (via PTP))  = %s (%" PRIu64 ") (%08X)\n", clockGetString(ts1, sizeof(ts1), t1_in), t1_in, (uint32_t)t1_in);
        printf("  t2 (SYNC rx)  = %s (%" PRIu64 ") (%08X)\n", clockGetString(ts2, sizeof(ts2), t2_in), t2_in, (uint32_t)t2_in);
        printf("  correction    = %" PRIu64 "ns\n", correction);
        printf("  cycle_count   = %u\n", gPtpC.cycle_count);
    }

    // Apply rounding to t1 assuming ( Vector VN/VX PTP master has 8ns resolution)
    parameters_t *params = (parameters_t *)XcpLockCalSeg(gParams);
    t1_in = t1_in + params->correction;
    XcpUnlockCalSeg(gParams);

    // Apply correction to t1
    t1_in += correction;

    // Master offset raw value
    gPtpC.master_offset_raw = (int64_t)t1_in - (int64_t)t2_in; // Positive master_offset means master is ahead

    // Master drift estimation from SYNC messages t1/t2
    // Plausibility checking
    if (!(t1_in > gPtpC.t1_norm && t2_in > gPtpC.t2_norm)) {
        assert(0);
    }

    // First round, init
    if (gPtpC.t1_offset == 0 || gPtpC.t2_offset == 0) {

        gPtpC.t1_norm = 0; // corrected sync tx time on master clock
        gPtpC.t2_norm = 0; // sync rx time on slave clock

        // Normalization time offsets for t1,t2
        gPtpC.t1_offset = t1_in;
        gPtpC.t2_offset = t2_in;
    }

    // Analysis
    else {

        // Normaiize t1,t2 to startup time
        uint64_t t1_norm = t1_in - gPtpC.t1_offset;
        uint64_t t2_norm = t2_in - gPtpC.t2_offset;

        // Time differences since last SYNC, with correction applied to master time
        uint64_t c1, c2;
        c1 = (int64_t)t1_norm - (int64_t)gPtpC.t1_norm; // time since last sync on master clock
        c2 = t2_norm - gPtpC.t2_norm;                   // time since last sync on local clock
        assert(c1 < 0x8000000000000000);
        assert(c2 < 0x8000000000000000);

        // Drift calculation
        int64_t diff = c2 - c1; // Positive diff = master clock faster than local clock

        if (diff < -200000 || diff > +200000) { // Plausibility checking of absolute drift (max 200us per cycle)
            printf("WARNING: Master drift too high! dt=%lldns \n", diff);
        } else {
            gPtpC.sync_cycle_time = c2;
            gPtpC.master_drift_raw = diff * 1000000000 / (int64_t)c2; // Drift in ns/s (1/1000 ppm)
            int64_t drift = average_calc(&gPtpC.master_drift_filter, gPtpC.master_drift_raw);
            gPtpC.master_drift_drift = (drift - gPtpC.master_drift) * 1000000000 / (int64_t)c2; // Drift Drift in ns/s2
            gPtpC.master_drift = drift;
        }
        if (gPtpDebugLevel >= 3) {
            printf("  master_drift        = %" PRIi64 "ns/s\n", gPtpC.master_drift);
            printf("  master_drift_drift  = %" PRIi64 "ns/s2\n", gPtpC.master_drift_drift);
        }

        if (gPtpC.cycle_count >= MASTER_DRIFT_FILTER_SIZE) {

            // Master offset
            // @@@@ TODO apply correction
            gPtpC.master_offset_norm = (int64_t)t1_norm - (int64_t)t2_norm; // Positive master_offset means master is ahead
            if (gPtpC.master_offset_compensation == 0) {
                // Initialize compensation
                gPtpC.master_offset_compensation = gPtpC.master_offset_norm;
            } else {
                // Compensate drift
                gPtpC.master_offset_compensation -= ((gPtpC.master_drift) * (int64_t)gPtpC.sync_cycle_time) / 1000000000;
            }

            gPtpC.master_offset_detrended = gPtpC.master_offset_norm - gPtpC.master_offset_compensation;

            // PI Servo Controller to prevent offset runaway
            // The offset error (master_offset_detrended) should ideally be zero-mean jitter.
            // Any persistent non-zero mean indicates drift estimation error that needs correction.
            {
                double error = (double)gPtpC.master_offset_detrended;

                parameters_t *params = (parameters_t *)XcpLockCalSeg(gParams);

                // Proportional term: immediate correction proportional to error
                double p_term = params->servo_p_gain * error;

                // Integral term: accumulate error over time to eliminate steady-state offset
                // Apply anti-windup: limit integral to prevent excessive accumulation
                gPtpC.servo_integral += params->servo_i_gain * error;
                double integral_limit = 10000.0; // Limit integral to +/- 10us
                if (gPtpC.servo_integral > integral_limit)
                    gPtpC.servo_integral = integral_limit;
                if (gPtpC.servo_integral < -integral_limit)
                    gPtpC.servo_integral = -integral_limit;

                XcpUnlockCalSeg(gParams);

                // Total servo correction
                double correction = p_term + gPtpC.servo_integral;

                // Apply correction to compensation (with rate limiting to avoid large jumps)
                double max_correction_per_cycle = 100.0; // Max 100ns correction per cycle
                if (correction > max_correction_per_cycle)
                    correction = max_correction_per_cycle;
                if (correction < -max_correction_per_cycle)
                    correction = -max_correction_per_cycle;

                gPtpC.servo_correction = (int64_t)correction;
                gPtpC.master_offset_compensation += gPtpC.servo_correction;

                if (gPtpDebugLevel >= 4) {
                    printf("  servo: error=%.1f p=%.1f i=%.1f corr=%" PRIi64 "\n", error, p_term, gPtpC.servo_integral, gPtpC.servo_correction);
                }
            }

            if (gPtpDebugLevel >= 5) {
                printf("  cycle_time          = %" PRIi64 "ns\n", gPtpC.sync_cycle_time);
                printf("  master_offset = %" PRIi64 " ns (detrended)\n", gPtpC.master_offset_detrended);
                printf("  master_offset_raw   = %" PRIi64 " ns\n", gPtpC.master_offset_raw);
                printf("  master_offset_norm  = %" PRIi64 " ns\n", gPtpC.master_offset_norm);
                printf("  master_offset_comp  = %" PRIi64 " ns\n", gPtpC.master_offset_compensation);
            }

            // Jitter
            gPtpC.master_jitter = gPtpC.master_offset_detrended;
            gPtpC.master_jitter_rms = sqrt((double)average_calc(&gPtpC.master_jitter_rms_filter, gPtpC.master_jitter * gPtpC.master_jitter));
            gPtpC.master_jitter_avg = average_calc(&gPtpC.master_jitter_avg_filter, gPtpC.master_jitter);
            if (gPtpDebugLevel >= 3) {
                printf("  master_jitter       = %" PRIi64 " ns\n", gPtpC.master_jitter);
                printf("  master_jitter_avg   = %g ns\n", gPtpC.master_jitter_avg);
                printf("  master_jitter_rms   = %g ns\n\n", gPtpC.master_jitter_rms);
            }
        }

        // Remember last normalized input values
        gPtpC.t1_norm = t1_norm; // sync tx time on master clock
        gPtpC.t2_norm = t2_norm; // sync rx time on slave clock
    }

    // XCP measurement event
#ifdef OPTION_ENABLE_PTP_XCP
    XcpEvent(gPtpC_syncEvent);
#endif
}

//-------------------------------------------------------------------------------------------------------
// A2L and XCP
// Create A2L description for measurements and parameters
// Create XCP events
// Create XCP calibration segments for parameters

#ifndef OPTION_ENABLE_PTP_XCP

#else

void ptpObserverCreateXcpEvents() { gPtpC_syncEvent = XcpCreateEvent("PTP_SYNC", 0, 0); }

void ptpObserverCreateXcpParameters() {

    gParams = XcpCreateCalSeg("params", &params, sizeof(params));

    A2lSetSegmentAddrMode(gParams, params);

    A2lCreateParameter(params.reset, "Reset PTP observer state", "", 0, 1);
    A2lCreateParameter(params.correction, "Correction for t1", "", -100, 100);
    A2lCreateParameter(params.servo_i_gain, "Integral gain for servo", "", 0.0, 1.0);
    A2lCreateParameter(params.servo_p_gain, "Proportional gain for servo", "", 0.0, 1.0);
}

void ptpObserverCreateA2lDescription() {

    // Event measurements
    A2lSetAbsoluteAddrMode__i(gPtpC_syncEvent);

    A2lCreateMeasurement(gPtpC.sync_local_time, "SYNC RX timestamp");
    A2lCreateMeasurement(gPtpC.sync_master_time, "SYNC timestamp");
    A2lCreateMeasurement(gPtpC.sync_correction, "SYNC correction");
    A2lCreateMeasurement(gPtpC.sync_sequenceId, "SYNC sequence counter");
    A2lCreateMeasurement(gPtpC.sync_steps, "SYNC mode");
    A2lCreateMeasurement(gPtpC.sync_cycle_time, "SYNC cycle time");

    A2lCreateMeasurement(gPtpC.flup_master_time, "FOLLOW_UP timestamp");
    A2lCreateMeasurement(gPtpC.flup_sequenceId, "FOLLOW_UP sequence counter");
    A2lCreateMeasurement(gPtpC.flup_correction, "FOLLOW_UP correction");

    A2lCreatePhysMeasurement(gPtpC.t1_norm, "t1 normalized to startup reference time t1_offset", "ns", 0, +1000000);
    A2lCreatePhysMeasurement(gPtpC.t2_norm, "t2 normalized to startup reference time t2_offset", "ns", 0, +1000000);

    A2lCreatePhysMeasurement(gPtpC.master_drift_raw, "", "ppm*1000", -100, +100);
    A2lCreatePhysMeasurement(gPtpC.master_drift, "", "ppm*1000", -100, +100);
    A2lCreatePhysMeasurement(gPtpC.master_drift_drift, "", "ppm*1000", -10, +10);

    A2lCreatePhysMeasurement(gPtpC.master_offset_raw, "t1-t2 raw value (not used)", "ns", -1000000, +1000000);
    A2lCreatePhysMeasurement(gPtpC.master_offset_compensation, "offset for detrending", "ns", -1000, +1000);
    A2lCreatePhysMeasurement(gPtpC.master_offset_detrended, "detrended master offset", "ns", -1000, +1000);

    A2lCreatePhysMeasurement(gPtpC.master_jitter, "offset jitter raw value", "ns", -1000, +1000);
    A2lCreatePhysMeasurement(gPtpC.master_jitter_rms, "Jitter root mean square", "ns", -1000, +1000);
    A2lCreatePhysMeasurement(gPtpC.master_jitter_avg, "Jitter average", "ns", -1000, +1000);

    // PI Servo measurements
    A2lCreatePhysMeasurement(gPtpC.servo_integral, "Servo integral accumulator", "ns", -10000, +10000);
    A2lCreatePhysMeasurement(gPtpC.servo_correction, "Servo correction per cycle", "ns", -100, +100);
}
#endif

//-------------------------------------------------------------------------------------------------------
// PTP protocol message handler

static void ptpPrintFrame(struct ptphdr *ptp, uint8_t *addr, uint32_t rx_timestamp) {

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
        printf("%s (seqId=%u, timestamp=%" PRIu64 " from %u.%u.%u.%u - %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", s, htons(ptp->sequenceId), rx_timestamp, addr[0], addr[1], addr[2],
               addr[3], ptp->clockId[0], ptp->clockId[1], ptp->clockId[2], ptp->clockId[3], ptp->clockId[4], ptp->clockId[5], ptp->clockId[6], ptp->clockId[7]);
        if (ptp->type == PTP_DELAY_RESP)
            printf("  to %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", ptp->u.r.clockId[0], ptp->u.r.clockId[1], ptp->u.r.clockId[2], ptp->u.r.clockId[3], ptp->u.r.clockId[4],
                   ptp->u.r.clockId[5], ptp->u.r.clockId[6], ptp->u.r.clockId[7]);
        printf("\n");
    }
}

static bool ptpHandleFrame(int n, struct ptphdr *ptp, uint8_t *addr, uint64_t timestamp) {

    if (n >= 44 && n <= 64) {

        if (gPtpC.domain == ptp->domain) {

            if (ptp->type == PTP_SYNC || ptp->type == PTP_FOLLOW_UP) {
                if (ptp->type == PTP_SYNC) {
                    if (timestamp == 0) {
                        printf("WARNING: PTP SYNC received without timestamp!\n");
                        return false;
                    }
                    gPtpC.sync_local_time = timestamp;
                    gPtpC.sync_master_time = htonl(ptp->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp->timestamp.timestamp_ns);
                    gPtpC.sync_sequenceId = htons(ptp->sequenceId);
                    gPtpC.sync_correction = (uint32_t)(htonll(ptp->correction) >> 16);
                    gPtpC.sync_steps = (htons(ptp->flags) & PTP_FLAG_TWO_STEP) ? 2 : 1;

                    // 1 step sync update
                    if (gPtpC.sync_steps == 1) {
                        syncUpdate(gPtpC.sync_master_time, gPtpC.sync_correction, gPtpC.sync_local_time);
                    }
                } else {

                    gPtpC.flup_master_time = htonl(ptp->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp->timestamp.timestamp_ns);
                    gPtpC.flup_sequenceId = htons(ptp->sequenceId);
                    gPtpC.flup_correction = (uint32_t)(htonll(ptp->correction) >> 16);
                }

                // 2 step sync update, SYNC and FOLLOW_UP may be received in any order (thread319 and thread320)
                if (gPtpC.sync_steps == 2 && gPtpC.sync_sequenceId == gPtpC.flup_sequenceId) {
                    syncUpdate(gPtpC.flup_master_time, gPtpC.sync_correction, gPtpC.sync_local_time); // 2 step
                }

            } else if (ptp->type == PTP_ANNOUNCE) {

                if (memcmp(&gPtpC.gm.a, &ptp->u.a, sizeof(ptp->u.a)) != 0) {
                    gPtpC.gm.a = ptp->u.a;
                    printf("PTP: Master parameters updated\n");
                    printMaster(&gPtpC.gm);
                }
            }

        } // from active master
    }

    return true;
}

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
        n = socketRecvFrom(gPtpC.sock319, buffer, (uint16_t)sizeof(buffer), addr, NULL, &rxTime);
        if (n <= 0)
            break; // Terminate on error or socket close
        mutexLock(&gPtpC.mutex);
        if (gPtpDebugLevel >= 4)
            ptpPrintFrame((struct ptphdr *)buffer, addr, rxTime); // Print incoming PTP traffic
        ptpHandleFrame(n, (struct ptphdr *)buffer, addr, rxTime);
        mutexUnlock(&gPtpC.mutex);
    }
    if (gPtpDebugLevel >= 3)
        printf("Terminate PTP multicast 319 thread\n");
    socketClose(&gPtpC.sock319);
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
        n = socketRecvFrom(gPtpC.sock320, buffer, (uint16_t)sizeof(buffer), addr, NULL, NULL);
        if (n <= 0)
            break; // Terminate on error or socket close
        mutexLock(&gPtpC.mutex);
        if (gPtpDebugLevel >= 4)
            ptpPrintFrame((struct ptphdr *)buffer, addr, 0); // Print incoming PTP traffic
        ptpHandleFrame(n, (struct ptphdr *)buffer, addr, 0);
        mutexUnlock(&gPtpC.mutex);
    }
    if (gPtpDebugLevel >= 3)
        printf("Terminate PTP multicast 320 thread\n");
    socketClose(&gPtpC.sock320);
    return 0;
}

//-------------------------------------------------------------------------------------------------------
// Public functions

// Start PTP Observer
bool ptpObserverInit(uint8_t domain, uint8_t *bindAddr) {

    memset(&gPtpC, 0, sizeof(gPtpC));

    // PTP client communication parameters
    memcpy(gPtpC.addr, bindAddr, 4);
    gPtpC.domain = domain;

    // Grandmaster info
    gPtpC.gmValid = false;

    // Init protocol state
    gPtpC.sync_local_time = 0;
    gPtpC.sync_master_time = 0;
    gPtpC.sync_correction = 0;
    gPtpC.sync_sequenceId = 0;
    gPtpC.sync_steps = 0;
    gPtpC.flup_master_time = 0;
    gPtpC.flup_correction = 0;
    gPtpC.flup_sequenceId = 0;

    // Init analysis state
    syncInit();

    // Create sockets for event (319) and general messages (320)
    gPtpC.sock319 = gPtpC.sock320 = INVALID_SOCKET;
    // SYNC tx, DELAY_REQ - with rx timestamps
    if (!socketOpen(&gPtpC.sock319, SOCKET_MODE_BLOCKING | SOCKET_MODE_TIMESTAMPING))
        return false;
    if (!socketBind(gPtpC.sock319, bindAddr, 319))
        return false;
    // General messages ANNOUNCE, FOLLOW_UP, DELAY_RESP - without rx timestamps
    if (!socketOpen(&gPtpC.sock320, SOCKET_MODE_BLOCKING))
        return false;
    if (!socketBind(gPtpC.sock320, bindAddr, 320))
        return false;

    // Try to enable hardware timestamps (requires root privileges)
    // This is optional - software timestamps from SO_TIMESTAMPING will still work
    if (!socketEnableHwTimestamps(gPtpC.sock319, PTP_INTERFACE)) {
        if (gPtpDebugLevel >= 2)
            printf("  WARNING: Hardware timestamping not enabled (may need root), using software timestamps\n");
    }

    if (gPtpDebugLevel >= 2)
        printf("  Bound PTP sockets to %u.%u.%u.%u:320/319\n", bindAddr[0], bindAddr[1], bindAddr[2], bindAddr[3]);
    if (gPtpDebugLevel >= 2)
        printf("  Listening for PTP multicast on 224.0.1.129\n");
    uint8_t maddr[4] = {224, 0, 1, 129};
    memcpy(gPtpC.maddr, maddr, 4);
    if (!socketJoin(gPtpC.sock319, gPtpC.maddr))
        return false;
    if (!socketJoin(gPtpC.sock320, gPtpC.maddr))
        return false;

#ifdef OPTION_ENABLE_PTP_XCP
    ptpObserverCreateXcpEvents();
    ptpObserverCreateXcpParameters();
    ptpObserverCreateA2lDescription();
#endif

    // Start all PTP threads
    mutexInit(&gPtpC.mutex, 0, 1000);
    create_thread(&gPtpC.threadHandle320, ptpThread320);
    create_thread(&gPtpC.threadHandle319, ptpThread319);

    return true;
}

// Reset PTP observer analysis state
void ptpObserverReset() {
    mutexLock(&gPtpC.mutex);
    syncInit();
    mutexUnlock(&gPtpC.mutex);
}

// PTP observer main loop
// This is called from the main loop of the application on a regular basis
// It prints the status and checks for reset requests via calibration parameters
void ptpObserverLoop(void) {

    static bool first = true;

    // Status print
    if (gPtpDebugLevel >= 2) {
        if (gPtpC.gmValid && first) {
            printf("PTP observer status:\n");
            printf("  domain=%u, addr=%u.%u.%u.%u\n", gPtpC.domain, gPtpC.addr[0], gPtpC.addr[1], gPtpC.addr[2], gPtpC.addr[3]);
            printf("  Grandmaster:\n");
            printMaster(&gPtpC.gm);
            first = false;
        }
    }

    // Reset request via calibration parameter
    parameters_t *params = (parameters_t *)XcpLockCalSeg(gParams);
    uint8_t reset = params->reset; // Get the reset calibration parameter
    params->reset = 0;             // Clear the reset request
    XcpUnlockCalSeg(gParams);
    if (reset) {
        printf("PTP observer reset requested via calibration parameter\n");
        first = true; // Reset first flag to print status again
        // Reset grandmaster info
        gPtpC.gmValid = false;
        // Reset PTP analysis state
        ptpObserverReset();
    }
}

// Stop PTP observer
// This is called from the main application shutdown routine
void ptpObserverShutdown() {

    cancel_thread(gPtpC.threadHandle);
    cancel_thread(gPtpC.threadHandle320);
    cancel_thread(gPtpC.threadHandle319);
    sleepMs(200);
    socketClose(&gPtpC.sock319);
    socketClose(&gPtpC.sock320);
}

#endif
