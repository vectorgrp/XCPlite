/*----------------------------------------------------------------------------
| File:
|   ptpObserver.c
|
| Description:
|   PTP client for XL-API64
|
 ----------------------------------------------------------------------------*/

#include <assert.h>  // for assert
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

#define MASTER_DRIFT_FILTER_SIZE 16

// PTP client parameters structure
struct parameters {
    uint32_t dummy; //
} parameters;

// Default PTP client parameters
static const struct parameters kParameters = {.dummy = 0};

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

    // PTP algorithm parameters
#ifdef OPTION_ENABLE_PTP_XCP
    tXcpCalSegIndex params_calseg;
#endif
    struct parameters *params; // Pointer to current parameters structure

    // PTP timing values
    uint64_t path_delay;
    int64_t master_offset;
    int64_t master_drift_raw;
    int64_t master_drift;
    filter_average_t master_drift_filter;

    uint64_t master_time; // In client clock domain
    uint64_t client_time; // Client time domain

    uint64_t t1, t2, t3, t4;
    uint64_t t1_t2_correction, t3_t4_correction;
    int64_t t1_t2_diff, t3_t4_diff;
    uint32_t syncUpdate;
    uint32_t delayUpdate;
    uint64_t sync_cycle_time;
    uint64_t flup_duration;
    uint64_t delay_resp_duration;

    // Master SYNC and FOLLOW_UP values
    uint64_t sync_local_time;
    uint64_t sync_master_time;
    uint32_t sync_correction;
    uint16_t sync_sequenceId;
    uint8_t sync_steps;
    uint64_t flup_master_time;
    uint32_t flup_correction;
    uint16_t flup_sequenceId;

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

//-------------------------------------------------------------------------------------------------------
// PTP protocol

static void syncUpdate(uint64_t t1, uint64_t correction, uint64_t t2) {

    char ts1[64], ts2[64];

    // Master drift estimation

    if (!(t1 > gPtpC.t1 && t2 > gPtpC.t2)) { // Plausibility checking
        assert(0);
    } else if (gPtpC.t1 == 0 || gPtpC.t2 == 0) { // First round, init
        gPtpC.t1 = t1;                           // sync tx time on master clock
        gPtpC.t2 = t2;                           // sync rx time on slave clock
    } else {

        uint64_t c1, c2;
        c1 = t1 - gPtpC.t1; // time since last sync on master clock
        c2 = t2 - gPtpC.t2; // time since last sync on slave clock
        assert(c1 < 0x8000000000000000);
        assert(c2 < 0x8000000000000000);

        int64_t diff = c2 - c1;
        if (diff < -200000 || diff > +200000) { // Plausibility checking of absolute drift (max 200us per cycle)
            printf("WARNING: Master drift too high! dt=%lldns \n", diff);
        } else {
            gPtpC.sync_cycle_time = c2;
            gPtpC.master_drift_raw = diff * 1000000000 / (int64_t)c2; // Drift in ns/s (1/1000 ppm)
            gPtpC.master_drift = average_calc(&gPtpC.master_drift_filter, gPtpC.master_drift_raw);
        }
    }

    gPtpC.t1 = t1; // sync tx time on master clock
    gPtpC.t2 = t2; // sync rx time on slave clock
    gPtpC.t1_t2_correction = correction;
    gPtpC.t1_t2_diff = t2 - t1;
    gPtpC.syncUpdate++;
#ifdef OPTION_ENABLE_PTP_XCP
    XcpEvent(gPtpC_syncEvent);
#endif
    if (gPtpDebugLevel >= 5) {
        printf("  t1 = %s (%" PRIu64 ")\n  t2 = %s (%" PRIu64 ")\n", clockGetString(ts1, sizeof(ts1), gPtpC.t1), gPtpC.t1, clockGetString(ts2, sizeof(ts2), gPtpC.t2), gPtpC.t2);
    }
}

//-------------------------------------------------------------------------------------------------------
// PTP protocol message handler

static void ptpPrintFrame(struct ptphdr *ptp, uint8_t *addr) {

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
        printf("%s from %u.%u.%u.%u - %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", s, addr[0], addr[1], addr[2], addr[3], ptp->clockId[0], ptp->clockId[1], ptp->clockId[2],
               ptp->clockId[3], ptp->clockId[4], ptp->clockId[5], ptp->clockId[6], ptp->clockId[7]);
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
                    gPtpC.flup_duration = timestamp - gPtpC.sync_local_time;
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
        if (rxTime == 0)
            break; // Invalid time
        mutexLock(&gPtpC.mutex);
        ptpHandleFrame(n, (struct ptphdr *)buffer, addr, rxTime);
        mutexUnlock(&gPtpC.mutex);
        if (gPtpDebugLevel >= 3)
            ptpPrintFrame((struct ptphdr *)buffer, addr); // Print incoming PTP traffic
    }
    if (gPtpDebugLevel >= 4)
        printf("Terminate PTP multicast 319 thread\n");
    socketClose(&gPtpC.sock319);
    return 0;
}

// General messages (Announce, Follow_Up, Delay_Resp) on port 320
// To keep track of other master activities
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
        n = socketRecvFrom(gPtpC.sock320, buffer, (uint16_t)sizeof(buffer), addr, NULL, &rxTime);
        if (n <= 0)
            break; // Terminate on error or socket close
        if (rxTime == 0)
            break; // Invalid time
        mutexLock(&gPtpC.mutex);
        ptpHandleFrame(n, (struct ptphdr *)buffer, addr, rxTime);
        mutexUnlock(&gPtpC.mutex);
        if (gPtpDebugLevel >= 3)
            ptpPrintFrame((struct ptphdr *)buffer, addr); // Print incoming PTP traffic
    }
    if (gPtpDebugLevel >= 4)
        printf("Terminate PTP multicast 320 thread\n");
    socketClose(&gPtpC.sock320);
    return 0;
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
    A2lCreatePhysMeasurement(gPtpC.flup_duration, "FOLLOW_UP duration time after SYNC", "ms", 0.000001, 0.0);

    A2lCreatePhysMeasurement(gPtpC.t1_t2_diff, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.t1_t2_correction, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.t1, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.t2, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.sync_cycle_time, "SYNC cycle time", "ms", 0.000001, 0.0);

    A2lCreatePhysMeasurement(gPtpC.master_drift_raw, "", "ppm", 0.001, 0.0);
    A2lCreatePhysMeasurement(gPtpC.master_drift, "", "ppm", 0.001, 0.0);

    // Parameters
    gPtpC.params_calseg = XcpCreateCalSeg("params", &kParameters, sizeof(kParameters));
    A2lSetSegmentAddrMode(gPtpC.params_calseg, kParameters);
    // A2lCreateParameter(kParameters.dummy, "n", "ns", 0, 10000);
}
#endif

//-------------------------------------------------------------------------------------------------------
// Public functions

// Start PTP Client
bool ptpObserverInit(uint8_t domain, uint8_t *bindAddr) {

    memset(&gPtpC, 0, sizeof(gPtpC));

    // PTP client communication parameters
    memcpy(gPtpC.addr, bindAddr, 4);
    gPtpC.domain = domain;

    // Parameters
    gPtpC.params = &kParameters;

    // Grandmaster info
    gPtpC.gmValid = false;

    gPtpC.t1 = 0;
    gPtpC.t2 = 0;
    gPtpC.t1_t2_correction = 0;
    gPtpC.syncUpdate = 0;

    gPtpC.master_drift_raw = 0;
    gPtpC.master_drift = 0;
    average_init(&gPtpC.master_drift_filter, MASTER_DRIFT_FILTER_SIZE);

    mutexInit(&gPtpC.mutex, 0, 1000);

    // Expected sync cycle time until not known
    gPtpC.sync_cycle_time = 1000000000;

    // Create XL-API sockets for event (319) and general messages (320)
    gPtpC.sock319 = gPtpC.sock320 = INVALID_SOCKET;
    if (!socketOpen(&gPtpC.sock319, false /* useTCP */, false /*nonblocking*/, true /*reusable*/, true /* timestamps*/))
        return false; // SYNC tx, DELAY_REQ rx timestamps
    if (!socketOpen(&gPtpC.sock320, false /* useTCP */, false /*nonblocking*/, true /*reusable*/, false /* timestamps*/))
        return false;
    if (!socketBind(gPtpC.sock320, bindAddr, 320))
        return false;
    if (!socketBind(gPtpC.sock319, bindAddr, 319))
        return false;
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

    // Start all PTP threads
    create_thread(&gPtpC.threadHandle320, ptpThread320);
    create_thread(&gPtpC.threadHandle319, ptpThread319);

    return true;
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

//-------------------------------------------------------------------------------------------------------
// Print Infos

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

#endif
