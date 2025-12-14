/*----------------------------------------------------------------------------
| File:
|   ptpMaster.c
|
| Description:
|   PTP master
|
 ----------------------------------------------------------------------------*/

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <stdlib.h>  // for malloc, free
#include <string.h>  // for sprintf

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "platform.h"

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

#include "util.h"

#include "ptpHdr.h"
#include "ptpMaster.h"

uint32_t gPtpDebugLevel = 1; // OPTION_PTP_DEBUG_LEVEL;

#define OPTION_TEST_TIME 0
#define OPTION_TEST_TEST_TIME 0

#define MAX_MASTERS 16
#define MAX_CLIENTS 16

//-------------------------------------------------------------------------------------------------------
// PTP client (test instrumentation)

// PTP client description
typedef struct {
    uint8_t addr[4];      // IP addr
    uint8_t id[8];        // clock UUID
    uint16_t event;       // XCP event number
    char *eventName;      // XCP event name
    uint64_t time;        // DELAY_REQ time stmap
    int64_t diff;         // DELAY_REQ current timestamp - delay req timestamp
    int64_t lastSeenTime; // Last rx timestamp
    int64_t cycle;        // Last cycle time
    int64_t counter;      // Cycle counter
    uint32_t corr;        // PTP correction
    uint8_t domain;       // PTP domain
    // uint64_t path_delay;
} tPtpClient;

// PTP master description
typedef struct {
    unsigned int index;
    uint8_t domain;
    uint8_t id[8];
    uint8_t addr[4];

    struct announce a;

} tPtpMaster;

// PTP master status
typedef struct {

    bool enabled; // PTP enabled

    uint8_t domain;
    uint8_t uuid[8];
    uint8_t addr[4];  // local addr
    uint8_t maddr[4]; // multicast addr

    // Timers
    uint32_t announceCycleTimeMs;
    uint32_t syncCycleTimeMs;
    uint64_t syncTxTimestamp;

    // Sockets
    THREAD threadHandle;
    THREAD threadHandle320;
    THREAD threadHandle319;
    SOCKET sock320;
    SOCKET sock319;

    // PTP master
    uint16_t sequenceIdAnnounce;
    uint16_t sequenceIdSync;
    MUTEX syncMutex;

    // Parameters for announce (in intel byte order)
    uint16_t utcOffset;
    uint8_t clockClass;
    uint8_t clockAccuraccy;
    uint16_t clockVariance;
    uint16_t stepsRemoved;
    uint8_t timeSource;
    uint8_t priority1;
    uint8_t priority2;

    // List of all announced foreign masters
    uint16_t masterCount;
    tPtpMaster masterList[MAX_MASTERS];

    // PTP client list
    uint16_t clientCount;
    tPtpClient client[MAX_CLIENTS];

    // XCP test instrumentation events
    uint16_t announceEvent;
    uint16_t syncEvent;
    uint16_t ptpEvent;

#if OPTION_TEST_TIME
    // Test time parameters
    int32_t drift;
    int32_t offset;

    // Test time state
    int32_t testTimeOffset;          // Current user offset
    int32_t testTimeDrift;           // Current user drift
    int32_t testTimeSyncDriftOffset; // Current offset from drift accumulated on sync: testTime = originTime+testTimeSyncDriftOffset
    int32_t testTimeDriftOffset;     // Current offset from drift since last sync: testTime = originTime+testTimeDriftOffset
    int64_t testTimeDiff;            // Current test time diff to original time
    uint64_t testTime;               // Current test time
    uint64_t originTimeSync;         // Original time of last sync
    MUTEX testTimeMutex;
#endif

} tPtpM;

static tPtpM gPtpM;

//-------------------------------------------------------------------------------------------------------
// Master time drift and offset calculation

#if OPTION_TEST_TIME

static void testTimeSync(uint64_t originTime);

void testTimeInit() {

    gPtpM.testTimeDrift = gPtpM.drift = 0;
    gPtpM.testTimeOffset = gPtpM.offset = 0;
    gPtpM.testTimeSyncDriftOffset = 0; // Current offset: testTime = originTime+testTimeSyncDriftOffset
    gPtpM.testTimeDriftOffset = 0;     // Current offset from drift since last sync: testTime = originTime+testTimeDriftOffset
    gPtpM.testTimeDiff = 0;            // Current test time diff to original time
    gPtpM.testTime = 0;                // Current test time
    gPtpM.originTimeSync = 0;          // Original time of last sync
    mutexInit(&gPtpM.testTimeMutex, 0, 1000);
}

static uint64_t testTimeCalc(uint64_t originTime) {

    // If offset has been changed (by XCP or GUI), take over
    if (gPtpM.testTimeOffset != gPtpM.offset) {
        gPtpM.testTimeOffset = gPtpM.offset;
        printf("offset = %d ns\n", gPtpM.offset);
    }
    // If drift has been changed (by XCP or GUI), resync and then take over to avoid time jumps
    if (gPtpM.testTimeDrift != gPtpM.drift) {
        testTimeSync(originTime);
        gPtpM.testTimeDrift = gPtpM.drift;
        printf("drift = %d ns/s\n", gPtpM.drift);
    }

    if (originTime >= gPtpM.originTimeSync) {

        mutexLock(&gPtpM.testTimeMutex);

        // time since last sync
        uint64_t dt = originTime - gPtpM.originTimeSync;
        // drift offset since last sync
        gPtpM.testTimeDriftOffset = (int32_t)((gPtpM.testTimeDrift * (int64_t)dt) / 1000000000);
        // printf("driftOffset=%d\n", gPtpM.testTimeDriftOffset);
        //  Calculate test time and test time diff (for display only)
        uint64_t t = originTime + gPtpM.testTimeOffset + gPtpM.testTimeSyncDriftOffset + gPtpM.testTimeDriftOffset;
        if (t < gPtpM.testTime) { // warn if time is non monotonic
            printf("ERROR: non monotonic time ! (dt=-%" PRIu64 ")\n", gPtpM.testTime - t);
        }
        gPtpM.testTime = t;
        gPtpM.testTimeDiff = gPtpM.testTime - originTime;

        mutexUnlock(&gPtpM.testTimeMutex);
    } else {
        printf("ERROR: old delay request ! (dt=-%" PRIu64 ")\n", gPtpM.originTimeSync - originTime);
    }

    // XCP event
    XcpEvent(gPtpM.ptpEvent);

    return gPtpM.testTime;
}

// Recalculate test time offset and zero test time drift offset
// At drift 100ppm, calculation would overflow after 2,8s
static void testTimeSync(uint64_t originTime) {

    mutexLock(&gPtpM.testTimeMutex);

    // time since last sync
    if (gPtpM.originTimeSync > 0 && originTime > gPtpM.originTimeSync) {
        uint64_t dt = originTime - gPtpM.originTimeSync;
        assert(dt < 2000000000); // Be sure integer calculation does not overflow
        assert(gPtpM.testTimeDrift >= -100000 && gPtpM.testTimeDrift <= +100000);
        gPtpM.testTimeDriftOffset = (int32_t)((gPtpM.testTimeDrift * (int64_t)dt) / 1000000000);
        gPtpM.testTimeSyncDriftOffset += gPtpM.testTimeDriftOffset;
        gPtpM.testTimeDriftOffset = 0;
        // printf("sync dt=%" PRIu64 ", driftOffset=%d, timeOffset=%d\n", dt, gPtpM.testTimeDriftOffset, gPtpM.testTimeSyncDriftOffset);
    }
    gPtpM.originTimeSync = originTime;

    mutexUnlock(&gPtpM.testTimeMutex);
}

#else

static uint64_t testTimeCalc(uint64_t t) { return t; }

#endif

//-------------------------------------------------------------------------------------------------------
// Test instrumentation

static void initClientList() {
    gPtpM.clientCount = 0;
    for (uint16_t i = 0; i < gPtpM.clientCount; i++)
        memset(&gPtpM.client[i], 0, sizeof(gPtpM.client[i]));
}

void printClient(uint16_t i) {

    char ts[64];
    printf("%s: addr=x.x.x.%u: domain=%u uuid=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X time=%s corr=%u diff=%" PRId64 " cycle=%" PRId64 " \n", gPtpM.client[i].eventName,
           gPtpM.client[i].addr[3], gPtpM.client[i].domain, gPtpM.client[i].id[0], gPtpM.client[i].id[1], gPtpM.client[i].id[2], gPtpM.client[i].id[3], gPtpM.client[i].id[4],
           gPtpM.client[i].id[5], gPtpM.client[i].id[6], gPtpM.client[i].id[7], clockGetString(ts, sizeof(ts), gPtpM.client[i].time), gPtpM.client[i].corr, gPtpM.client[i].diff,
           gPtpM.client[i].cycle);
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
// Master list
// Keeps track of foreign announced masters

static void initMasterList() { gPtpM.masterCount = 0; }

// Print information on a known grandmaster
static void printMaster(const tPtpMaster *m) {

    const char *timesource = (m->a.timeSource == PTP_TIME_SOURCE_INTERNAL) ? "internal oscilator" : (m->a.timeSource == PTP_TIME_SOURCE_GPS) ? "GPS" : "Unknown";
    DBG_PRINTF_ERROR("Master %u:\n"
                     "    domain=%u, addr=%u.%u.%u.%u, id=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n"
                     "    timesource=%s (%02X), utcOffset=%u, prio1=%u, class=%u, acc=%u, var=%u, prio2=%u, steps=%u\n",
                     m->index, m->domain, m->addr[0], m->addr[1], m->addr[2], m->addr[3], m->id[0], m->id[1], m->id[2], m->id[3], m->id[4], m->id[5], m->id[6], m->id[7],
                     timesource, m->a.timeSource, htons(m->a.utcOffset), m->a.priority1, m->a.clockClass, m->a.clockAccuraccy, htons(m->a.clockVariance), m->a.priority2,
                     htons(m->a.stepsRemoved));
}

// Find a grandmaster in the grandmaster list
static tPtpMaster *lookupMaster(const uint8_t *id, const uint8_t *addr, uint8_t domain) {

    unsigned int i;
    for (i = 0; i < gPtpM.masterCount; i++) {
        if (memcmp(gPtpM.masterList[i].id, id, 8) == 0 && memcmp(gPtpM.masterList[i].addr, addr, 4) == 0 && gPtpM.masterList[i].domain == domain) {
            return &gPtpM.masterList[i];
        }
    }
    return NULL;
}

// Add a new grandmaster to grandmaster list
static tPtpMaster *addMaster(const uint8_t *id, const uint8_t *addr, uint8_t domain) {

    tPtpMaster *m = &gPtpM.masterList[gPtpM.masterCount++];
    memset(m, 0, sizeof(tPtpMaster));
    m->index = gPtpM.masterCount;
    if (id != NULL)
        memcpy(m->id, id, 8);
    if (addr != NULL)
        memcpy(m->addr, addr, 4);
    m->domain = domain;
    return m;
}

//-------------------------------------------------------------------------------------------------------
// PTP protocol

// Init constant values in PTP header
static void initHeader(struct ptphdr *h, uint8_t type, uint16_t len, uint16_t flags, uint16_t sequenceId) {

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
    h.u.a.utcOffset = htons(gPtpM.utcOffset);
    h.u.a.stepsRemoved = htons(gPtpM.stepsRemoved);
    memcpy(h.u.a.grandmasterId, gPtpM.uuid, 8);
    h.u.a.clockVariance = htons(gPtpM.clockVariance); // Allan deviation
    h.u.a.clockAccuraccy = gPtpM.clockAccuraccy;
    h.u.a.clockClass = gPtpM.clockClass;
    h.u.a.priority1 = gPtpM.priority1;
    h.u.a.priority2 = gPtpM.priority2;
    h.u.a.timeSource = gPtpM.timeSource;
    l = socketSendTo(gPtpM.sock320, (uint8_t *)&h, 64, gPtpM.maddr, 320, NULL);

    if (gPtpDebugLevel >= 2) {
        printf("TX ANNOUNCE %u %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", gPtpM.sequenceIdAnnounce, h.clockId[0], h.clockId[1], h.clockId[2], h.clockId[3], h.clockId[4],
               h.clockId[5], h.clockId[6], h.clockId[7]);
    }

    return (l == 64);
}

static bool ptpSendSync() {

    struct ptphdr h;
    int16_t l;

    initHeader(&h, PTP_SYNC, 44, PTP_FLAG_TWO_STEP, ++gPtpM.sequenceIdSync);
    l = socketSendTo(gPtpM.sock319, (uint8_t *)&h, 44, gPtpM.maddr, 319, NULL);

    if (gPtpDebugLevel >= 2) {
        printf("TX SYNC %u\n", gPtpM.sequenceIdSync);
    }

    return (l == 44);
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

    l = socketSendTo(gPtpM.sock320, (uint8_t *)&h, 44, gPtpM.maddr, 320, NULL);

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

    l = socketSendTo(gPtpM.sock320, (uint8_t *)&h, 54, gPtpM.maddr, 320, NULL);

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
// PTP protocol message handler

static bool ptpHandleFrame(SOCKET sock, int n, struct ptphdr *ptp, uint8_t *addr, uint64_t rxTimestamp) {

    if (!gPtpM.enabled)
        return false;
    if (n >= 44 && n <= 64) {

        if (gPtpDebugLevel >= 2) {
            const char *s;
            switch (ptp->type) {
            case PTP_SYNC:
                s = "SYNC";
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
            case PTP_FOLLOW_UP:
                s = "FOLLOW_UP";
                break;
            case PTP_PDELAY_RESP_FOLLOW_UP:
                s = "PDELAY_RESP_FOLLOW_UP";
                break;
            case PTP_SIGNALING:
                s = "SIGNALING";
                break;
            case PTP_ANNOUNCE:
                s = "ANNOUNCE";
                break;
            case PTP_MANAGEMENT:
                s = "MANAGEMENT";
                break;
            default:
                s = "UNKNOWN";
                break;
            }
            printf("RX %s from %u.%u.%u.%u - %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", s, addr[0], addr[1], addr[2], addr[3], ptp->clockId[0], ptp->clockId[1], ptp->clockId[2],
                   ptp->clockId[3], ptp->clockId[4], ptp->clockId[5], ptp->clockId[6], ptp->clockId[7]);
        }

        if (ptp->type == PTP_DELAY_REQ) {

            if (ptp->domain == gPtpM.domain) {
                bool ok;
                mutexLock(&gPtpM.syncMutex);
                ok = ptpSendDelayResponse(ptp, rxTimestamp);
                mutexUnlock(&gPtpM.syncMutex);
                if (!ok)
                    return false;
            }

            // Maintain PTP client list for test instrumentation and trigger an XCP event
            uint16_t i = lookupClient(addr, ptp->clockId);
            bool newClient = (i == 0xFFFF);
            if (newClient) {
                i = addClient(addr, ptp->clockId, ptp->domain);
            }

            // Some clients send non zero timestamp values in their DELAY_REQ which allows to visualize information on timesynchronisation quality
            gPtpM.client[i].time = htonl(ptp->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp->timestamp.timestamp_ns);
            gPtpM.client[i].diff = rxTimestamp - gPtpM.client[i].time;
            gPtpM.client[i].corr = (uint32_t)(htonll(ptp->correction) >> 16);
            gPtpM.client[i].cycle = rxTimestamp - gPtpM.client[i].lastSeenTime;
            gPtpM.client[i].counter++;
            gPtpM.client[i].lastSeenTime = rxTimestamp;

            // XCP event on delay request
            XcpEvent(gPtpM.client[i].event);

            // Log
            if (gPtpDebugLevel >= 1 && newClient) {
                printClient(i);
            }
        }

        // Announces from other masters
        else if (ptp->type == PTP_ANNOUNCE) {
            tPtpMaster *m = lookupMaster(ptp->clockId, addr, ptp->domain);
            if (m == NULL) {
                m = addMaster(ptp->clockId, addr, ptp->domain);
                m->a = ptp->u.a;
                if (gPtpDebugLevel >= 1) {
                    printMaster(m);
                }
            }
        }
    }
    return true;
}

//-------------------------------------------------------------------------------------------------------
// Threads

#ifdef _WIN
static DWORD WINAPI ptpThread(LPVOID par)
#else
static void *ptpThread(void *par)
#endif
{
    (void)par;

    sleepMs(2000);

    // Send the first ANNOUNCE immediately, first SYNC after 100ms
    uint64_t t = clockGet();
    uint64_t announceCycleTimer = 0;
    uint64_t syncCycleTimer = t + 100 * CLOCK_TICKS_PER_MS - gPtpM.syncCycleTimeMs * CLOCK_TICKS_PER_MS;

    for (;;) {

        sleepMs(10);
        t = clockGet();

        // Announce cycle
        if (gPtpM.announceCycleTimeMs > 0 && t - announceCycleTimer > gPtpM.announceCycleTimeMs * CLOCK_TICKS_PER_MS) {
            announceCycleTimer = t;
            if (!ptpSendAnnounce())
                break;

            // XCP event on announce
            XcpEvent(gPtpM.announceEvent);
        }

        // Sync cycle
        if (gPtpM.syncCycleTimeMs > 0 && t - syncCycleTimer > gPtpM.syncCycleTimeMs * CLOCK_TICKS_PER_MS) {
            syncCycleTimer = t;

            mutexLock(&gPtpM.syncMutex);
            if (!ptpSendSync())
                break;
            uint64_t tsync = clockGet();
            sleepMs(10); // Wait to be sure transmit ackknowledge for last SYNC has been received
            gPtpM.syncTxTimestamp = socketGetSendTime(gPtpM.sock319);

#if OPTION_TEST_TIME
#if !OPTION_TEST_TEST_TIME // Enable test time modifications in drift and offset
            testTimeSync(gPtpM.syncTxTimestamp);
#else
            testTimeSync(t);
#endif
#endif
            if (!ptpSendSyncFollowUp(gPtpM.syncTxTimestamp))
                break;
            mutexUnlock(&gPtpM.syncMutex);

            // XCP
            XcpEventAt(gPtpM.syncEvent, tsync);
        }

#if OPTION_TEST_TEST_TIME
        testTimeCalc(t);
#endif
    }
    return 0;
}

// Time critical messages (Sync, Delay_Req)
#ifdef _WIN
static DWORD WINAPI ptpThread319(LPVOID par)
#else
static void *ptpThread319(void *par)
#endif
{
    uint8_t buffer[256];
    uint8_t addr[4];
    uint64_t time;
    int n;

    (void)par;
    for (;;) {

        n = socketRecvFrom(gPtpM.sock319, buffer, (uint16_t)sizeof(buffer), addr, NULL, &time);

        if (n <= 0)
            break; // Terminate on error or xl_socket close
        ptpHandleFrame(gPtpM.sock319, n, (struct ptphdr *)buffer, addr, time);
    }
    if (gPtpDebugLevel >= 3)
        printf("Terminate PTP multicast 319 thread\n");

    socketClose(&gPtpM.sock319);

    return 0;
}

// General messages (Announce, Follow_Up, Delay_Resp)
// To keep track of other master activities
#ifdef _WIN
static DWORD WINAPI ptpThread320(LPVOID par)
#else
static void *ptpThread320(void *par)
#endif
{
    uint8_t buffer[256];
    uint8_t addr[4];
    uint64_t time;
    int n;
    (void)par;
    for (;;) {

        n = socketRecvFrom(gPtpM.sock320, buffer, (uint16_t)sizeof(buffer), addr, NULL, &time);

        if (n <= 0)
            break; // Terminate on error or socket close
        ptpHandleFrame(gPtpM.sock319, n, (struct ptphdr *)buffer, addr, time);
    }
    if (gPtpDebugLevel >= 3)
        printf("Terminate PTP multicast 320 thread\n");
    socketClose(&gPtpM.sock320);

    return 0;
}

//-------------------------------------------------------------------------------------------------------
// XCP

static void createXCPEvents() {

    uint16_t i;
    gPtpM.ptpEvent = XcpCreateEvent("PTP", 0, 0);
    gPtpM.announceEvent = XcpCreateEvent("ANNOUNCE", 0, 0);
    gPtpM.syncEvent = XcpCreateEvent("SYNC", 0, 0);
    for (i = 0; i < MAX_CLIENTS; i++) {
        gPtpM.client[i].eventName = (char *)malloc(16);
        sprintf(gPtpM.client[i].eventName, "CLIENT%u", i + 1);
        gPtpM.client[i].event = XcpCreateEvent(gPtpM.client[i].eventName, 0, 0);
    }
}

//-------------------------------------------------------------------------------------------------------
// Public functions

// Start PTP master
bool ptpMasterInit(const uint8_t *uuid, uint8_t domain, const uint8_t *bindAddr) {

    sleepMs(100);
    memset(&gPtpM, 0, sizeof(gPtpM));

    printf("\nStart PTP master on %u.%u.%u.%u uuid %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X domain %u\n", bindAddr[0], bindAddr[1], bindAddr[2], bindAddr[3], uuid[0], uuid[1],
           uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], domain);
    memcpy(gPtpM.addr, bindAddr, 4);

    gPtpM.domain = domain;
    memcpy(gPtpM.uuid, uuid, 8);
    gPtpM.announceCycleTimeMs = 10000;
    gPtpM.syncCycleTimeMs = 1000;
    gPtpM.syncTxTimestamp = 0;
    mutexInit(&gPtpM.syncMutex, 0, 1000);
    gPtpM.sequenceIdSync = 0;
    gPtpM.sequenceIdAnnounce = 0;
    initMasterList();
    initClientList();

    // Initial parameters for ANNOUNCE
    // Master clock quality
    // lower value takes precedence in BMCA
    gPtpM.utcOffset = 37;
    gPtpM.stepsRemoved = 0;
    gPtpM.clockVariance = 0;
    gPtpM.clockAccuraccy = PTP_CLOCK_ACC_GPS;
    gPtpM.clockClass = PTP_CLOCK_CLASS_PTP_PRIMARY;
    gPtpM.priority1 = 0;
    gPtpM.priority2 = 0;
    gPtpM.timeSource = PTP_TIME_SOURCE_GPS;

    // XCP
    // Create events for XCP test instrumentation
    createXCPEvents();

    // Init offset and drift calculation
#if OPTION_TEST_TIME
    testTimeInit();
#endif

    // Init sockets
    socketStartup();

    // Create XL-API sockets and threads for PTP
    gPtpM.sock319 = gPtpM.sock320 = INVALID_SOCKET;
    if (!socketOpen(&gPtpM.sock319, false /* useTCP */, false /*nonblocking*/, true /*reusable*/, true /* timestamps*/))
        return false; // SYNC tx, DELAY_REQ rx timestamps
    if (!socketOpen(&gPtpM.sock320, false /* useTCP */, false /*nonblocking*/, true /*reusable*/, true /* timestamps*/))
        return false;
    if (gPtpDebugLevel >= 3)
        printf("  Bind PTP sockets to port 320/319\n");
    if (!socketBind(gPtpM.sock320, NULL, 320))
        return false;
    if (!socketBind(gPtpM.sock319, NULL, 319))
        return false;
    if (gPtpDebugLevel >= 3)
        printf("  Listening for PTP multicast on 224.0.1.129\n");
    uint8_t maddr[4] = {224, 0, 1, 129};
    memcpy(gPtpM.maddr, maddr, 4);
    if (!socketJoin(gPtpM.sock319, gPtpM.maddr))
        return false;
    if (!socketJoin(gPtpM.sock320, gPtpM.maddr))
        return false;

    // Start PTP threads
    create_thread(&gPtpM.threadHandle320, ptpThread320);
    create_thread(&gPtpM.threadHandle319, ptpThread319);
    create_thread(&gPtpM.threadHandle, ptpThread);
    sleepMs(200);

    gPtpM.enabled = true;
    return true;
}

// Stop PTP client
void ptpMasterShutdown() {

    cancel_thread(gPtpM.threadHandle);
    cancel_thread(gPtpM.threadHandle320);
    cancel_thread(gPtpM.threadHandle319);
    gPtpM.enabled = false;
    sleepMs(200);
    socketClose(&gPtpM.sock319);
    socketClose(&gPtpM.sock320);
    socketCleanup();
}

// Print list of all seen masters
void ptpMasterPrintMasterList() {

    uint16_t i;
    printf("\nMaster list:\n");
    for (i = 0; i < gPtpM.masterCount; i++) {
        printMaster(&gPtpM.masterList[i]);
    }
}

//-------------------------------------------------------------------------------------------------------

// Print list of known clients
void ptpMasterPrintClientList() {

    uint16_t i;
    printf("\nClient list:\n");
    for (i = 0; i < gPtpM.clientCount; i++) {
        printClient(i);
    }
    printf("\n");
}

// Print info
void ptpMasterPrintInfo() {

    char ts[64];
    uint64_t t;
    printf("\nInfo:\n");
    printf("UUID:   %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", gPtpM.uuid[0], gPtpM.uuid[1], gPtpM.uuid[2], gPtpM.uuid[3], gPtpM.uuid[4], gPtpM.uuid[5], gPtpM.uuid[6],
           gPtpM.uuid[7]);
    printf("IP:     %u.%u.%u.%u\n", gPtpM.addr[0], gPtpM.addr[1], gPtpM.addr[2], gPtpM.addr[3]);
    printf("Domain: %u\n", gPtpM.domain);
    printf("Announce cycle: %ums\n", gPtpM.announceCycleTimeMs);
    printf("Sync cycle:     %ums\n", gPtpM.syncCycleTimeMs);
    printf("Clients seen:         %u\n", gPtpM.clientCount);
    printf("Foreign masters seen: %u\n", gPtpM.masterCount);
    // printf("Offset: %d ns\n", gPtpM.offset);
    // printf("Drift:  %d ns/s\n", gPtpM.drift);
    t = clockGet();
    printf("Local PC time:           %s\n", clockGetString(ts, sizeof(ts), t));
    t = gPtpM.syncTxTimestamp;
    printf("Last Master time (SYNC): %s\n", clockGetString(ts, sizeof(ts), t));
}

//-------------------------------------------------------------------------------------------------------
// A2L for XCP

#if OPTION_ENABLE_A2L_GEN
void ptpMasterCreateA2lDescription() {

#ifdef __cplusplus

    A2lCreateParameterWithLimits(gPtpM.domain, "PTP domain", "", 0, 255);
    A2lCreateParameterWithLimits(gPtpM.announceCycleTimeMs, "ANNOUNCE cycle time in ms", "ms", 1, 10000);
    A2lCreateParameterWithLimits(gPtpM.syncCycleTimeMs, "SYNC cycle time in ms", "ms", 10, 10000);
#if OPTION_TEST_TIME
    A2lCreateParameterWithLimits(gPtpM.offset, "PTP master time offset", "ns", -1000000000, +1000000000);
    A2lCreateParameterWithLimits(gPtpM.drift, "PTP master time drift", "ns/s", -100000, +100000);
#endif
    A2lCreateParameterWithLimits(gPtpM.utcOffset, "PTP ANNOUNCE parameter", "", 0, 0xFFFF);
    A2lCreateParameterWithLimits(gPtpM.stepsRemoved, "PTP ANNOUNCE parameter", "", 0, 0xFFFF);
    A2lCreateParameterWithLimits(gPtpM.clockVariance, "PTP ANNOUNCE parameter", "", 0, 0xFFFF);
    A2lCreateParameterWithLimits(gPtpM.clockAccuraccy, "PTP ANNOUNCE parameter", "", 0, 255);
    A2lCreateParameterWithLimits(gPtpM.clockClass, "PTP ANNOUNCE parameter", "", 0, 255);
    A2lCreateParameterWithLimits(gPtpM.priority1, "PTP ANNOUNCE parameter", "", 0, 255);
    A2lCreateParameterWithLimits(gPtpM.priority2, "PTP ANNOUNCE parameter", "", 0, 255);
    A2lCreateParameterWithLimits(gPtpM.timeSource, "PTP ANNOUNCE parameter", "", 0, 255);
    A2lParameterGroup("PTP_ANNOUNCE", 8, "gPtpM.utcOffset", "gPtpM.stepsRemoved", "gPtpM.clockVariance", "gPtpM.clockAccuraccy", "gPtpM.clockClass", "gPtpM.priority1",
                      "gPtpM.priority2", "gPtpM.timeSource");

#if OPTION_TEST_TIME
    A2lSetFixedEvent(gPtpM.ptpEvent);
    A2lCreateMeasurement(gPtpM.testTime, "testTime");
    A2lCreatePhysMeasurement(gPtpM.testTimeDiff, "testTimeDiff", 0.000001, 0, "ms");
#endif
    A2lSetFixedEvent(gPtpM.syncEvent);
    A2lCreateMeasurement(gPtpM.clientCount, "Number of clients");
    A2lCreateMeasurement(gPtpM.sequenceIdSync, "SYNC sequence number");
    A2lSetFixedEvent(gPtpM.announceEvent);
    A2lCreateMeasurement(gPtpM.sequenceIdAnnounce, "ANNOUNCE sequence number");

    uint16_t i;
    for (i = 0; i < MAX_CLIENTS; i++) {
        A2lSetFixedEvent(gPtpM.client[i].event);
        char name[32];
        sprintf(name, "Addr%u", i);
        A2lCreateMeasurement_(NULL, name, A2lGetType(gPtpM.client[i].addr[3]), 0, A2lGetAddr((uint8_t *)&(gPtpM.client[i].addr[3])), 1.0, 0.0, "", "DELAY_REQ x.x.x.Addr", NULL);
        sprintf(name, "Corr%u", i);
        A2lCreateMeasurement_(NULL, name, A2lGetType(gPtpM.client[i].corr), 0, A2lGetAddr((uint8_t *)&gPtpM.client[i].corr), 0.000001, 0.0, "ms", "DELAY_REQ correction value",
                              NULL);
        sprintf(name, "Time%u", i);
        A2lCreateMeasurement_(NULL, name, A2lGetType(gPtpM.client[i].time), 0, A2lGetAddr((uint8_t *)&gPtpM.client[i].time), 0.000001, 0.0, "ms", "DELAY_REQ timestamp", NULL);
        sprintf(name, "Diff%u", i);
        A2lCreateMeasurement_(NULL, name, A2lGetType(gPtpM.client[i].diff), 0, A2lGetAddr((uint8_t *)&gPtpM.client[i].diff), 0.000001, 0.0, "ms", "DELAY_REQ rxTime-timestamp",
                              NULL);
        sprintf(name, "Cycle%u", i);
        A2lCreateMeasurement_(NULL, name, A2lGetType(gPtpM.client[i].cycle), 0, A2lGetAddr((uint8_t *)&gPtpM.client[i].cycle), 0.000001, 0.0, "ms", "DELAY_REQ cycle time", NULL);
        sprintf(name, "Counter%u", i);
        A2lCreateMeasurement_(NULL, name, A2lGetType(gPtpM.client[i].counter), 0, A2lGetAddr((uint8_t *)&gPtpM.client[i].counter), 1.0, 0.0, "", "DELAY_REQ cycle time", NULL);
        // sprintf(name, "PathDelay%u", i);
        // A2lCreateMeasurement_(NULL, name, A2lGetType(gPtpM.client[i].path_delay), 0, A2lGetAddr((uint8_t*)&gPtpM.client[i].counter), 1.0, 0.0, "", "DELAY_REQ cycle time", NULL);
    }

#else

    /*
    #define A2L_TYPE_UINT8    1
    #define A2L_TYPE_UINT16   2
    #define A2L_TYPE_UINT32   4
    #define A2L_TYPE_UINT64   8
    #define A2L_TYPE_INT8    -1
    #define A2L_TYPE_INT16   -2
    #define A2L_TYPE_INT32   -4
    #define A2L_TYPE_INT64   -8
    #define A2L_TYPE_FLOAT   -9
    #define A2L_TYPE_DOUBLE  -10
    */

    A2lCreateParameterWithLimits(gPtpM.domain, A2L_TYPE_UINT8, "PTP domain", "", 0, 255);
    A2lCreateParameterWithLimits(gPtpM.announceCycleTimeMs, A2L_TYPE_UINT32, "ANNOUNCE cycle time in ms", "ms", 1, 10000);
    A2lCreateParameterWithLimits(gPtpM.syncCycleTimeMs, A2L_TYPE_UINT32, "SYNC cycle time in ms", "ms", 10, 10000);
#if OPTION_TEST_TIME
    A2lCreateParameterWithLimits(gPtpM.offset, A2L_TYPE_UINT32, "PTP master time offset", "ns", -1000000000, +1000000000);
    A2lCreateParameterWithLimits(gPtpM.drift, A2L_TYPE_UINT32, "PTP master time drift", "ns/s", -100000, +100000);
#endif

    A2lCreateParameterWithLimits(gPtpM.domain, A2L_TYPE_UINT8, "PTP domain", "", 0, 255);
    A2lCreateParameterWithLimits(gPtpM.announceCycleTimeMs, A2L_TYPE_UINT32, "ANNOUNCE cycle time in ms", "ms", 1, 10000);
    A2lCreateParameterWithLimits(gPtpM.syncCycleTimeMs, A2L_TYPE_UINT32, "SYNC cycle time in ms", "ms", 10, 10000);
    A2lCreateParameterWithLimits(gPtpM.utcOffset, A2L_TYPE_UINT16, "PTP ANNOUNCE parameter", "", 0, 0xFFFF);
    A2lCreateParameterWithLimits(gPtpM.stepsRemoved, A2L_TYPE_UINT16, "PTP ANNOUNCE parameter", "", 0, 0xFFFF);
    A2lCreateParameterWithLimits(gPtpM.clockVariance, A2L_TYPE_UINT16, "PTP ANNOUNCE parameter", "", 0, 0xFFFF);
    A2lCreateParameterWithLimits(gPtpM.clockAccuraccy, A2L_TYPE_UINT8, "PTP ANNOUNCE parameter", "", 0, 255);
    A2lCreateParameterWithLimits(gPtpM.clockClass, A2L_TYPE_UINT8, "PTP ANNOUNCE parameter", "", 0, 255);
    A2lCreateParameterWithLimits(gPtpM.priority1, A2L_TYPE_UINT8, "PTP ANNOUNCE parameter", "", 0, 255);
    A2lCreateParameterWithLimits(gPtpM.priority2, A2L_TYPE_UINT8, "PTP ANNOUNCE parameter", "", 0, 255);
    A2lCreateParameterWithLimits(gPtpM.timeSource, A2L_TYPE_UINT8, "PTP ANNOUNCE parameter", "", 0, 255);
    A2lParameterGroup("PTP_ANNOUNCE", 8, "gPtpM.utcOffset", "gPtpM.stepsRemoved", "gPtpM.clockVariance", "gPtpM.clockAccuraccy", "gPtpM.clockClass", "gPtpM.priority1",
                      "gPtpM.priority2", "gPtpM.timeSource");

#endif
}
#endif
