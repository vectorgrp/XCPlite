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

#include "platform.h"
#include "ptpMaster.h" // for tPtpMaster
#include "ptpObserver.h"
#include "util.h"

// XCP
#ifdef OPTION_ENABLE_PTP_XCP
#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface
#endif

//-------------------------------------------------------------------------------------------------------

// PTP client status structure
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

    // Master SYNC and FOLLOW_UP measurements
    uint64_t sync_local_time;
    uint64_t sync_master_time;
    uint32_t sync_correction;
    uint16_t sync_sequenceId;
    uint8_t sync_steps;
    uint64_t flup_master_time;
    uint32_t flup_correction;
    uint16_t flup_sequenceId;

    // PTP timing analysis state
    uint64_t t1, t2; // Input
    uint64_t sync_cycle_time;
    uint64_t t1_t2_correction;
    uint64_t t1_offset, t2_offset; // Normalization offsets
    int64_t master_drift_raw;      // Calculated drift
    int64_t master_drift;
    int64_t master_offset_raw;          // raw offset t1-t2
    int64_t t1_norm;                    // normalized
    int64_t t2_norm;                    // normalized
    int64_t master_offset_norm;         // normalized
    int64_t master_offset_compensation; // normalized master_offset servo compensation
    int64_t master_offset;              // normalized master_offset
    int64_t master_jitter;              // jitter
    double master_jitter_rms;           // jitter root mean square
    double master_jitter_avg;           // jitter average
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

#ifdef OPTION_ENABLE_PTP_TEST
    if (m->path_delay > 0) {
        char ts1[64];
        printf("    mean_path_delay=%" PRIu64 "ns, offset=%s, drift=%gppm\n", m->path_delay, clockGetTimeString(ts1, sizeof(ts1), m->offset), (double)m->drift / 1000.0);
    }
#endif
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

#define MASTER_DRIFT_FILTER_SIZE 30
#define MASTER_JITTER_RMS_FILTER_SIZE 20
#define MASTER_JITTER_AVG_FILTER_SIZE 60

static void syncInit() {

    gPtpC.t1 = 0;
    gPtpC.t2 = 0;
    gPtpC.t1_t2_correction = 0;

    gPtpC.sync_cycle_time = 1000000000;

    gPtpC.master_offset_raw = 0;
    gPtpC.master_offset_norm = 0;

    gPtpC.master_drift_raw = 0;
    average_init(&gPtpC.master_drift_filter, MASTER_DRIFT_FILTER_SIZE);
    gPtpC.master_drift = 0;

    gPtpC.master_offset_compensation = 0;

    gPtpC.master_jitter = 0;
    average_init(&gPtpC.master_jitter_rms_filter, MASTER_JITTER_RMS_FILTER_SIZE);
    gPtpC.master_jitter_rms = 0;
    average_init(&gPtpC.master_jitter_avg_filter, MASTER_JITTER_AVG_FILTER_SIZE);
    gPtpC.master_jitter_avg = 0;
}

static void syncUpdate(uint64_t t1, uint64_t correction, uint64_t t2) {

    char ts1[64], ts2[64];

    // t1 - master, t2 - local clock

    if (gPtpDebugLevel >= 4) {
        printf("  t1 (SYNC tx)  = %s (%" PRIu64 ")\n", clockGetString(ts1, sizeof(ts1), t1), t1);
        printf("  t2 (SYNC rx)  = %s (%" PRIu64 ")\n", clockGetString(ts2, sizeof(ts2), t2), t2);
        printf("  correction    = %" PRIu32 "ns\n", correction);
    }

    // Master drift estimation from SYNC messages t1/t2
    // Plausibility checking
    if (!(t1 > gPtpC.t1 && t2 > gPtpC.t2)) {
        assert(0);
    }

    // First round, init
    if (gPtpC.t1 == 0 || gPtpC.t2 == 0) {
        gPtpC.t1 = t1; // sync tx time on master clock
        gPtpC.t2 = t2; // sync rx time on slave clock

        // Normalize time offsets for t1,t2
        gPtpC.t1_offset = t1;
        gPtpC.t2_offset = t2;
    }

    // Analysis
    else {

        // Time differences since last SYNC, with correction applied to master time
        uint64_t c1, c2;
        c1 = (int64_t)(t1 + correction) - (int64_t)(gPtpC.t1 + gPtpC.t1_t2_correction); // time since last sync on master clock
        c2 = t2 - gPtpC.t2;                                                             // time since last sync on local clock
        assert(c1 < 0x8000000000000000);
        assert(c2 < 0x8000000000000000);

        // Drift calculation
        int64_t diff = c2 - c1;                 // Positive diff = master clock faster than local clock
        if (diff < -200000 || diff > +200000) { // Plausibility checking of absolute drift (max 200us per cycle)
            printf("WARNING: Master drift too high! dt=%lldns \n", diff);
        } else {
            gPtpC.sync_cycle_time = c2;
            gPtpC.master_drift_raw = diff * 1000000000 / (int64_t)c2; // Drift in ns/s (1/1000 ppm)
            gPtpC.master_drift = average_calc(&gPtpC.master_drift_filter, gPtpC.master_drift_raw);
        }
        if (gPtpDebugLevel >= 3) {
            printf("  master_drift        = %" PRIi64 "ns/s\n", gPtpC.master_drift);
        }

        // Master offset
        // @@@@ TODO apply correction
        gPtpC.master_offset_raw = (int64_t)t1 - (int64_t)t2; // Positive master_offset means master is ahead
        gPtpC.t1_norm = t1 - gPtpC.t1_offset;
        gPtpC.t2_norm = t2 - gPtpC.t2_offset;
        gPtpC.master_offset_norm = gPtpC.t1_norm - gPtpC.t2_norm;
        if (gPtpC.master_offset_compensation == 0) {
            // Initialize compensation
            gPtpC.master_offset_compensation = gPtpC.master_offset_norm;
        } else {
            // Compensate drift
            gPtpC.master_offset_compensation += (gPtpC.master_drift * (int64_t)gPtpC.sync_cycle_time) / 1000000000;
        }

        gPtpC.master_offset = gPtpC.master_offset_norm + gPtpC.master_offset_compensation;
        {
            // Simple offset servo
            int64_t offset_error = gPtpC.master_offset;
            const int64_t kp = 3; // Integral gain (1/10)
            gPtpC.master_offset_compensation -= (kp * offset_error) / 10;
        }

        if (gPtpDebugLevel >= 3) {
            printf("  cycle_time          = %" PRIi64 "ns\n", gPtpC.sync_cycle_time);
            if (gPtpDebugLevel >= 5) {
                printf("  master_offset_raw   = %" PRIi64 " ns\n", gPtpC.master_offset_raw);
                printf("  master_offset_norm  = %" PRIi64 " ns\n", gPtpC.master_offset_norm);
                printf("  master_offset_comp  = %" PRIi64 " ns\n", gPtpC.master_offset_compensation);
                printf("  master_offset       = %" PRIi64 " ns\n", gPtpC.master_offset);
            }
        }

        // Jitter
        gPtpC.master_jitter = gPtpC.master_offset_norm + gPtpC.master_offset_compensation;
        gPtpC.master_jitter_rms = sqrt((double)average_calc(&gPtpC.master_jitter_rms_filter, gPtpC.master_jitter * gPtpC.master_jitter));
        gPtpC.master_jitter_avg = average_calc(&gPtpC.master_jitter_avg_filter, gPtpC.master_jitter);
        if (gPtpDebugLevel >= 3) {
            printf("  master_jitter       = %" PRIi64 " ns\n", gPtpC.master_jitter);
            printf("  master_jitter_avg   = %g ns\n", gPtpC.master_jitter_avg);
            printf("  master_jitter_rms   = %g ns\n", gPtpC.master_jitter_rms);
        }
    }

    // Remember last input values
    gPtpC.t1 = t1; // sync tx time on master clock
    gPtpC.t2 = t2; // sync rx time on slave clock
    gPtpC.t1_t2_correction = correction;

#ifdef OPTION_ENABLE_PTP_XCP
    XcpEvent(gPtpC_syncEvent);
#endif
    if (gPtpDebugLevel >= 5) {
        printf("  t1 = %s (%" PRIu64 ")\n  t2 = %s (%" PRIu64 ")\n", clockGetString(ts1, sizeof(ts1), gPtpC.t1), gPtpC.t1, clockGetString(ts2, sizeof(ts2), gPtpC.t2), gPtpC.t2);
    }
}

//-------------------------------------------------------------------------------------------------------
// A2L and XCP
// Create A2L description for measurements and parameters
// Create XCP events

#ifdef OPTION_ENABLE_PTP_XCP

void ptpObserverCreateXcpEvents() { gPtpC_syncEvent = XcpCreateEvent("PTP_SYNC", 0, 0); }

void ptpObserverCreateA2lDescription() {

    // Event measurements
    A2lSetAbsoluteAddrMode__i(gPtpC_syncEvent);

    A2lCreateMeasurement(gPtpC.sync_local_time, "SYNC RX timestamp");
    A2lCreateMeasurement(gPtpC.sync_master_time, "SYNC timestamp");
    A2lCreateMeasurement(gPtpC.sync_correction, "SYNC correction");
    A2lCreateMeasurement(gPtpC.sync_sequenceId, "SYNC sequence counter");
    A2lCreateMeasurement(gPtpC.sync_steps, "SYNC mode");

    A2lCreateMeasurement(gPtpC.flup_master_time, "FOLLOW_UP timestamp");
    A2lCreateMeasurement(gPtpC.flup_sequenceId, "FOLLOW_UP sequence counter");
    A2lCreateMeasurement(gPtpC.flup_correction, "FOLLOW_UP correction");

    A2lCreatePhysMeasurement(gPtpC.t1_t2_correction, "PTP correction from SYNC message", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.t1, "Master timestamp", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.t2, "Reference timestamp", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.sync_cycle_time, "SYNC cycle time", "ms", 0.000001, 0.0);

    A2lCreatePhysMeasurement(gPtpC.master_drift_raw, "", "ppm*1000", 0.001, 0.0);
    A2lCreatePhysMeasurement(gPtpC.master_drift, "", "ppm*1000", 0.001, 0.0);

    A2lCreatePhysMeasurement(gPtpC.master_offset_raw, "t1-t2 raw value (not used)", "ns", -1000000, +1000000);
    A2lCreatePhysMeasurement(gPtpC.t1_norm, "t1 normalized to startup reference time t1_offset", "ns", 0, +1000000);
    A2lCreatePhysMeasurement(gPtpC.t2_norm, "t2 normalized to startup reference time t2_offset", "ns", 0, +1000000);
    A2lCreatePhysMeasurement(gPtpC.master_offset_norm, "normalized master offset (t1-t2)", "ns", -1000000, +1000000);

    A2lCreatePhysMeasurement(gPtpC.master_offset_compensation, "offset for detrending", "ns", -1000, +1000);
    A2lCreatePhysMeasurement(gPtpC.master_offset, "filtered detrended master offset", "ns", -1000, +1000);

    A2lCreatePhysMeasurement(gPtpC.master_jitter, "offset jitter raw value", "ns", -1000, +1000);
    A2lCreatePhysMeasurement(gPtpC.master_jitter_rms, "Jitter root mean square", "ns", -1000, +1000);
    A2lCreatePhysMeasurement(gPtpC.master_jitter_avg, "Jitter average", "ns", -1000, +1000);
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
        printf("%s (seqId=%u, timestamp= %" PRIu64 " from %u.%u.%u.%u - %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", s, htons(ptp->sequenceId), rx_timestamp, addr[0], addr[1],
               addr[2], addr[3], ptp->clockId[0], ptp->clockId[1], ptp->clockId[2], ptp->clockId[3], ptp->clockId[4], ptp->clockId[5], ptp->clockId[6], ptp->clockId[7]);
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
        if (gPtpDebugLevel >= 3)
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
        if (gPtpDebugLevel >= 3)
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

// Start PTP Client
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
    if (!socketEnableHwTimestamps(gPtpC.sock319, "eth0")) {
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
    ptpObserverCreateA2lDescription();
#endif

    // Start all PTP threads
    mutexInit(&gPtpC.mutex, 0, 1000);
    create_thread(&gPtpC.threadHandle320, ptpThread320);
    create_thread(&gPtpC.threadHandle319, ptpThread319);

    return true;
}

// Reset PTP analysis state
void ptpObserverReset() {
    mutexLock(&gPtpC.mutex);
    syncInit();
    mutexUnlock(&gPtpC.mutex);
}

// Stop PTP client
void ptpObserverShutdown() {

    cancel_thread(gPtpC.threadHandle);
    cancel_thread(gPtpC.threadHandle320);
    cancel_thread(gPtpC.threadHandle319);

    sleepMs(200);
    socketClose(&gPtpC.sock319);
    socketClose(&gPtpC.sock320);
}

tPtpMaster *ptpObserverGetGrandmaster() { return gPtpC.gmValid ? &gPtpC.gm : NULL; }

#endif
