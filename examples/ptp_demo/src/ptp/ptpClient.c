/*----------------------------------------------------------------------------
| File:
|   ptpClient.c
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
#ifdef OPTION_ENABLE_PTP_CLIENT

#include "platform.h"

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

#include "ptpClient.h"
#include "ptpMaster.h"
#include "util.h"

//-------------------------------------------------------------------------------------------------------

#define MAX_MASTERS 16
#define GRANDMASTER_LOST_TIMEOUT 10 // s
#define MASTER_DRIFT_FILTER_SIZE 16

// PTP client parameters structure
struct parameters {
    uint32_t delayReqCorrectionNs; // Parameter DELAY_REQ correction in ns
    uint32_t delayReqDelayMs;      // Parameter DELAY_REQ delay to SYNC
    uint32_t delayReqJitterMs;     // Parameter DELAY_REQ time jitter in ms
    uint16_t delayReqCycle;        // Parameter DELAY_REQ every nth SYNC
} parameters;

// Default PTP client parameters
static const struct parameters kParameters = {
    .delayReqCorrectionNs = 0,
    .delayReqDelayMs = 30,
    .delayReqJitterMs = 20,
    .delayReqCycle = 1 //  Every nth SYNC with delay and jitter, with n = 2^delay_req.logMessageInterval
};

// PTP client status structure
typedef struct {

    bool enabled; // PTP enabled

    uint8_t domain;  // Time domain, only masters in this domain accepted
    uint8_t uuid[8]; // UUID of this PTP client

    // Sockets and communication
    uint8_t addr[4];  // local addr
    uint8_t maddr[4]; // multicast addr
    THREAD threadHandle;
    THREAD threadHandle320;
    THREAD threadHandle319;
    SOCKET sock320;
    SOCKET sock319;
    MUTEX mutex;
    void (*ptpClientCallback)(uint64_t grandmaster_time, uint64_t local_time, int32_t drift);

    // List of all announced masters
    uint16_t masterCount;
    tPtpMaster masterList[MAX_MASTERS];

    // Current grandmaster
    tPtpMaster *gm; // Current grandmaster maybe NULL

    uint16_t gmIndex; // Copy of the actual master values for XCP measurement
    uint8_t gmDomain;
    uint32_t gmAddr;
    uint64_t gmId;

    // Everything below is related to current grandmaster

    uint64_t gmLastSeenTime; // Last message seen from current grandmaster

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

#ifdef OPTION_ENABLE_PTP_TEST
    int64_t path_asymmetry;
    int64_t path_asymmetry_avg;
    filter_average_t path_asymmetry_filter;
#endif

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

    // Client DELAY_REQ values
    uint16_t delay_req_sequenceId;
    uint64_t delay_req_local_time;

    // Master DELAY_RESP values
    uint64_t delay_resp_local_time;
    uint64_t delay_resp_master_time;
    uint32_t delay_resp_correction;
    uint16_t delay_resp_sequenceId;
    uint16_t delay_resp_logMessageInterval;

} tPtpC;

static tPtpC gPtpC;

#ifdef OPTION_ENABLE_PTP_XCP
// XCP test instrumentation events
static uint16_t gPtpC_syncEvent = XCP_UNDEFINED_EVENT_ID;   // on SYNC
static uint16_t gPtpC_delayEvent = XCP_UNDEFINED_EVENT_ID;  // on DELAY_RESP
static uint16_t gPtpC_updateEvent = XCP_UNDEFINED_EVENT_ID; // on path_delay and master_offset calculation
#endif

//-------------------------------------------------------------------------------------------------------
// Master list
// Keeps track of foreign announced masters

static void initMasterList() { gPtpC.masterCount = 0; }

// Print information on a known grandmaster
static void printMaster(const tPtpMaster *m) {

    printf("  Master %u:\n", m->index);

    const char *timesource = (m->par.timeSource == PTP_TIME_SOURCE_INTERNAL) ? "internal oscilator" : (m->par.timeSource == PTP_TIME_SOURCE_GPS) ? "GPS" : "Unknown";
    printf("    domain=%u, addr=%u.%u.%u.%u, id=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n"
           "    timesource=%s (%02X), utcOffset=%u, prio1=%u, class=%u, acc=%u, var=%u, prio2=%u, steps=%u\n",
           m->domain, m->addr[0], m->addr[1], m->addr[2], m->addr[3], m->uuid[0], m->uuid[1], m->uuid[2], m->uuid[3], m->uuid[4], m->uuid[5], m->uuid[6], m->uuid[7], timesource,
           m->par.timeSource, htons(m->par.utcOffset), m->par.priority1, m->par.clockClass, m->par.clockAccuraccy, htons(m->par.clockVariance), m->par.priority2,
           htons(m->par.stepsRemoved));

#ifdef OPTION_ENABLE_PTP_TEST
    if (m->path_delay > 0) {
        char ts1[64];
        printf("    mean_path_delay=%" PRIu64 "ns, offset=%s, drift=%gppm\n", m->path_delay, clockGetTimeString(ts1, sizeof(ts1), m->offset), (double)m->drift / 1000.0);
    }
#endif
}

// Find a grandmaster in the grandmaster list
static tPtpMaster *lookupMaster(const uint8_t *id, const uint8_t *addr, uint8_t domain) {

    unsigned int i;
    for (i = 0; i < gPtpC.masterCount; i++) {
        if (memcmp(gPtpC.masterList[i].uuid, id, 8) == 0 && memcmp(gPtpC.masterList[i].addr, addr, 4) == 0 && gPtpC.masterList[i].domain == domain) {
            return &gPtpC.masterList[i];
        }
    }
    return NULL;
}

// Add a new grandmaster to grandmaster list
static tPtpMaster *addMaster(const uint8_t *id, const uint8_t *addr, uint8_t domain) {

    tPtpMaster *m = &gPtpC.masterList[gPtpC.masterCount++];
    memset(m, 0, sizeof(tPtpMaster));
    m->index = gPtpC.masterCount;
    if (id != NULL)
        memcpy(m->uuid, id, 8);
    if (addr != NULL)
        memcpy(m->addr, addr, 4);
    m->domain = domain;
    return m;
}

//-------------------------------------------------------------------------------------------------------
// PTP protocol

// Set the active grandmaster
static void setGrandmaster(tPtpMaster *m) {

    gPtpC.gm = m;
    if (m != NULL) {
        gPtpC.gmIndex = m->index;
        gPtpC.gmDomain = m->domain;
        gPtpC.gmAddr = *(uint32_t *)m->addr;
        gPtpC.gmId = *(uint64_t *)m->uuid;
    } else {
        gPtpC.gmIndex = 0;
        gPtpC.gmDomain = 0;
        gPtpC.gmAddr = 0;
        gPtpC.gmId = 0;
    }

    gPtpC.t4 = 0;
    gPtpC.t3 = 0;
    gPtpC.t3_t4_correction = 0;
    gPtpC.delayUpdate = 0;
    gPtpC.t1 = 0;
    gPtpC.t2 = 0;
    gPtpC.t1_t2_correction = 0;
    gPtpC.syncUpdate = 0;

    gPtpC.master_drift_raw = 0;
    gPtpC.master_drift = 0;
    average_init(&gPtpC.master_drift_filter, MASTER_DRIFT_FILTER_SIZE);

#ifdef OPTION_ENABLE_PTP_TEST
    gPtpC.path_asymmetry = 0;
    gPtpC.path_asymmetry_avg = 0;
    average_init(&gPtpC.path_asymmetry_filter, 60);
#endif

    if (m != NULL) {
        printf("\nPTP: Active grandmaster is %u: addr=%u.%u.%u.%u\n\n", m->index, m->addr[0], m->addr[1], m->addr[2], m->addr[3]);
    } else {
        printf("\nPTP: Grandmaster lost\n\n");
    }
}

// Init constant values in PTP header
static void initHeader(struct ptphdr *h, uint8_t type, uint16_t len, uint16_t flags, uint16_t sequenceId, uint32_t correction_ns) {

    memset(h, 0, sizeof(struct ptphdr));
    h->version = 2;
    h->domain = gPtpC.domain;
    memcpy(h->clockId, gPtpC.uuid, 8);
    h->sourcePortId = htons(1);
    h->logMessageInterval = 127;
    h->type = type;
    h->len = htons(len);
    h->flags = htons(flags);
    h->sequenceId = htons(sequenceId);
    h->correction = htonll(((uint64_t)correction_ns) << 16);

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
    case PTP_DELAY_REQ:
        h->controlField = 0x01;
        break;
    default:
        assert(0);
    }
}

static bool ptpSendDelayRequest() {

    struct ptphdr h;
    int16_t l;

#ifdef OPTION_ENABLE_PTP_XCP
    gPtpC.params = XcpLockCalSeg(gPtpC.params_calseg);
#endif
    initHeader(&h, PTP_DELAY_REQ, 44, 0, ++gPtpC.delay_req_sequenceId, gPtpC.params->delayReqCorrectionNs);
#ifdef OPTION_ENABLE_PTP_XCP
    XcpUnlockCalSeg(gPtpC.params_calseg);
#endif

    l = socketSendTo(gPtpC.sock319, (uint8_t *)&h, 44, gPtpC.maddr, 319, &gPtpC.delay_req_local_time);
    if (l == 44) {
        if (gPtpC.delay_req_local_time == 0) {
            gPtpC.delay_req_local_time = socketGetSendTime(gPtpC.sock319);
        }
        if (gPtpDebugLevel >= 3 && gXcpDebugLevel > 0) {
            printf("TX DELAY_REQ %u, tx time = %" PRIu64 "\n", htons(h.sequenceId), gPtpC.delay_req_local_time);
        }
        return true;
    } else {
        printf("ERROR: ptpSendDelayRequest: socketSendTo failed\n");
        return false;
    }
}

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
    if (gPtpDebugLevel >= 5 && gXcpDebugLevel > 0) {
        printf("  t1 = %s (%" PRIu64 ")\n  t2 = %s (%" PRIu64 ")\n", clockGetString(ts1, sizeof(ts1), gPtpC.t1), gPtpC.t1, clockGetString(ts2, sizeof(ts2), gPtpC.t2), gPtpC.t2);
    }
}

static void delayUpdate(uint64_t t3, uint64_t correction, uint64_t t4) {

    char ts1[64], ts2[64];

    gPtpC.t4 = t4;
    gPtpC.t3 = t3;
    gPtpC.t3_t4_correction = correction;
    gPtpC.t3_t4_diff = t4 - t3;
    gPtpC.delayUpdate++;
#ifdef OPTION_ENABLE_PTP_XCP
    XcpEvent(gPtpC_delayEvent);
#endif
    if (gPtpDebugLevel >= 5 && gXcpDebugLevel > 0) {
        printf("  t3 = %s (%" PRIu64 ")\n  t4 = %s (%" PRIu64 ")\n", clockGetString(ts1, sizeof(ts1), gPtpC.t3), gPtpC.t3, clockGetString(ts2, sizeof(ts2), gPtpC.t4), gPtpC.t4);
    }
}

//-------------------------------------------------------------------------------------------------------
// PTP protocol message handler

static void ptpPrintFrame(tPtpMaster *m, struct ptphdr *ptp, uint8_t *addr) {

    if (gXcpDebugLevel > 0 && gPtpDebugLevel >= 3) {

        if (gPtpDebugLevel == 3) { // personal PTP messages
            if (m == NULL)
                return; // no master active
            if (m != gPtpC.gm)
                return; // not my master
            if (ptp->type == PTP_DELAY_RESP && memcmp(gPtpC.uuid, ptp->u.r.clockId, 8) != 0)
                return; // not my response
        }

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
}

static tPtpMaster *ptpHandleFrame(int n, struct ptphdr *ptp, uint8_t *addr, uint64_t timestamp) {

    tPtpMaster *m = NULL;

    if (!gPtpC.enabled)
        return NULL;
    if (n >= 44 && n <= 64) {

        m = lookupMaster(ptp->clockId, addr, ptp->domain);
        if (m != NULL && gPtpC.gm == m) { // Check if message is from active master
        sync:
            gPtpC.gmLastSeenTime = clockGet();

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

            } else if (ptp->type == PTP_DELAY_RESP) {
                if (memcmp(gPtpC.uuid, ptp->u.r.clockId, 8) == 0) { // Check response is for us
                    if (gPtpC.delay_req_local_time == 0)
                        gPtpC.delay_req_local_time = socketGetSendTime(gPtpC.sock319); // get tx timestamp of last DELAY_REQ (XL_API)
                    if (gPtpC.delay_req_local_time != 0) {                             // if the tx timestamp is not available yet, do not wait, just ignore this REQ-RESP cycle
                        gPtpC.delay_resp_duration = timestamp - gPtpC.delay_req_local_time; // time since DELAY_REQ
                        gPtpC.delay_resp_local_time = timestamp;
                        gPtpC.delay_resp_master_time = htonl(ptp->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp->timestamp.timestamp_ns);
                        gPtpC.delay_resp_sequenceId = htons(ptp->sequenceId);
                        gPtpC.delay_resp_correction = (uint32_t)(htonll(ptp->correction) >> 16);
                        gPtpC.delay_resp_logMessageInterval = ptp->logMessageInterval;

                        // update DELAY_REQ cycletime (DELAY_REQ has constant delay to SYNC (parameter delayReqDelayMs), logMessageInterval is realized by skipping SYNCs
                        // @@@@ Not implemented yet
                        // if (delayReqCycle == 0)
                        //     delayReqCycle = 1 << gPtpC.delay_resp_logMessageInterval;

                        // delay update
                        delayUpdate(gPtpC.delay_req_local_time, gPtpC.delay_resp_correction, gPtpC.delay_resp_master_time);
                    } else {
                        if (gPtpDebugLevel >= 2)
                            printf("WARNING: could not process delay response, socketGetSendTime failed!\n");
                    }
                }

            } else if (ptp->type == PTP_ANNOUNCE) {

                if (memcmp(&m->par, &ptp->u.a, sizeof(ptp->u.a)) != 0) {
                    m->par = ptp->u.a; // update master parameters
                    printf("PTP: Master parameters updated\n");
                    printMaster(m);
                }
            }

        } // from active master

        else if (m != NULL) { // from other known master

            // Sync message from known master with correct domain
            if (ptp->type == PTP_SYNC && ptp->domain == gPtpC.domain) { // Sync

                if (gPtpC.gm == NULL) { // no active grandmaster yet
                    setGrandmaster(m);
                    goto sync;
                } else { // other grandmaster in the same domain
                    printf("WARNING: Conflicting PTP SYNC message from grandmaster %u received in domain %u\n", m->index, gPtpC.domain);
                }
            }
        } // from other known master

        else { // (m==NULL) message from from yet unknown master

            // Remember all announced masters in a list
            if (ptp->type == PTP_ANNOUNCE) { // ANNOUNCE
                printf("\nPTP: Master detected by ANNOUNCE\n\n");
                m = addMaster(ptp->clockId, addr, ptp->domain);
                m->par = ptp->u.a;
                printMaster(m);
            }

            // SYNC message from unknown (unannounced yet) master with correct domain
            if (ptp->type == PTP_SYNC && gPtpC.gm == NULL) { // fast SYNC
                printf("\nPTP: Master detected by SYNC\n\n");
                m = addMaster(ptp->clockId, addr, ptp->domain);
                printMaster(m);
                // master parameters not initialized yet
                setGrandmaster(m);
                goto sync;
            }
        }
    }

    return m;
}

//-------------------------------------------------------------------------------------------------------
// Threads

#if defined(_WIN) // Windows
static DWORD WINAPI ptpThread(LPVOID par)
#else
static void *ptpThread(void *par)
#endif
{
    (void)par;

    uint64_t t = clockGet();
    uint64_t delayReqTimer = 0xFFFFFFFFFFFFFFFF;
    uint32_t lastSyncUpdate = 0;
    uint32_t lastDelayUpdate = 0;
    int16_t delayReqCycle = 1;

    for (;;) {

        sleepMs(20);
        t = clockGet();

        // Check grandmaster is active
        if (gPtpC.gm != NULL) {
            if (t - gPtpC.gmLastSeenTime > (uint64_t)GRANDMASTER_LOST_TIMEOUT * CLOCK_TICKS_PER_S) { // Master timeout
                setGrandmaster(NULL);
                if (gPtpC.ptpClientCallback != NULL)
                    gPtpC.ptpClientCallback(0, 0, 0);
            }
        }

        // Send a delayed DELAY_REQ for every nth SYNC
        if (gPtpC.gm != NULL) {

            if (gPtpC.syncUpdate != lastSyncUpdate && delayReqTimer == 0xFFFFFFFFFFFFFFFF) {
                lastSyncUpdate = gPtpC.syncUpdate;
                if (--delayReqCycle <= 0) {
#ifdef OPTION_ENABLE_PTP_XCP
                    gPtpC.params = XcpLockCalSeg(gPtpC.params_calseg);
#endif
                    delayReqCycle = gPtpC.params->delayReqCycle;
                    delayReqTimer = t + ((uint64_t)gPtpC.params->delayReqDelayMs * 1000000) + ((uint64_t)gPtpC.params->delayReqJitterMs * 1000000 / 65536 * random16());
#ifdef OPTION_ENABLE_PTP_XCP
                    XcpUnlockCalSeg(gPtpC.params_calseg);
#endif
                }
            }

            if (t > delayReqTimer) {
                delayReqTimer = 0xFFFFFFFFFFFFFFFF;
                ;
                ptpSendDelayRequest();
            }

            // Update path_delay and master_offset when new data from SYNC and DELAY_REQ is available
            if (gPtpC.delayUpdate != lastDelayUpdate) {
                lastDelayUpdate = gPtpC.delayUpdate;

                /* PTP protocol
                   Master   Client
                       t1             t1 = FOLLOW_UP.sync_tx_timestamp
                          \
                            t2        t2 = SYNC client rx timestamp
                            t3        t3 = DELAY_REQUEST client tx timestamp
                          /
                       t4             t4 = DELAY_RESPONCE.req_rx_timestamp
                */

                // Drift correction for t4
                int64_t t4_drift_correction = (int64_t)(gPtpC.t4 - gPtpC.t1) * gPtpC.master_drift / 1000000000;

                // Calculate mean path delay
                uint64_t t21 = (gPtpC.t2 - gPtpC.t1 - gPtpC.t1_t2_correction) + 100;
                uint64_t t43 = (gPtpC.t4 - gPtpC.t3 - gPtpC.t3_t4_correction);
                gPtpC.path_delay = (t21 + (t43 + t4_drift_correction)) / 2;
                if (gPtpC.path_delay >= 1000000)
                    gPtpC.path_delay = 0; // @@@@ Unplausible path_delay !!!!

                // Calculate master offset
                gPtpC.master_offset = t21 - gPtpC.path_delay;

                // Calculate master time
                gPtpC.master_time = gPtpC.t3 - gPtpC.master_offset;

#ifdef OPTION_ENABLE_PTP_TEST

                // Calculate path asymmetry
                gPtpC.path_asymmetry = (int64_t)t21 - (int64_t)(t43 + t4_drift_correction);
                gPtpC.path_asymmetry_avg = average_calc(&gPtpC.path_asymmetry_filter, gPtpC.path_asymmetry);

                // Set client time
                gPtpC.client_time = gPtpC.t3;
#endif

                // Update the new master/client timestamp pair and drift
                if (gPtpC.ptpClientCallback != NULL) {
                    gPtpC.ptpClientCallback(gPtpC.master_time, gPtpC.client_time, (int32_t)gPtpC.master_drift);
                }
#ifdef OPTION_ENABLE_PTP_TEST

#ifdef OPTION_ENABLE_PTP_XCP
                XcpEvent(gPtpC_updateEvent);
#endif
                if (gPtpDebugLevel >= 1 && gXcpDebugLevel > 0) {
                    if (gPtpC.gm != NULL && gPtpC.path_delay > 0) {

                        // Store master measurement values in master list
                        gPtpC.gm->path_delay = gPtpC.path_delay;
                        gPtpC.gm->offset = gPtpC.master_offset;
                        gPtpC.gm->drift = gPtpC.master_drift;
                        gPtpC.gm->path_asymmetry = gPtpC.path_asymmetry;

                        char ts1[64];
                        if (gPtpDebugLevel >= 2) {
                            printf("PTP: mean_path_delay=%" PRIu64 "ns, path_asymmetry=%" PRIi64 "ns, master_offset=%s, drift=%gppm (%gppm), sync_corr=%" PRIu64
                                   "ns resp_corr=%" PRIu64 "ns\n",
                                   gPtpC.path_delay, gPtpC.path_asymmetry, clockGetTimeString(ts1, sizeof(ts1), gPtpC.master_offset), (double)gPtpC.master_drift / 1000.0,
                                   (double)gPtpC.master_drift_raw / 1000.0, gPtpC.t1_t2_correction, gPtpC.t3_t4_correction);
                        } else {
                            printf("PTP: mean_path_delay=%" PRIu64 "ns, master_offset=%s, drift=%gppm\n", gPtpC.path_delay,
                                   clockGetTimeString(ts1, sizeof(ts1), gPtpC.master_offset), (double)gPtpC.master_drift / 1000.0);
                        }
                    }
                }
#endif
            }
        }
    }
}

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
    tPtpMaster *m;

    (void)par;
    for (;;) {
        n = socketRecvFrom(gPtpC.sock319, buffer, (uint16_t)sizeof(buffer), addr, NULL, &rxTime);
        if (n <= 0)
            break; // Terminate on error or socket close
        if (rxTime == 0)
            break; // Invalid time
        mutexLock(&gPtpC.mutex);
        m = ptpHandleFrame(n, (struct ptphdr *)buffer, addr, rxTime);
        mutexUnlock(&gPtpC.mutex);
        ptpPrintFrame(m, (struct ptphdr *)buffer, addr); // Print incoming PTP traffic
    }
    if (gPtpDebugLevel >= 4 && gXcpDebugLevel > 0)
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
    tPtpMaster *m;

    (void)par;
    for (;;) {
        n = socketRecvFrom(gPtpC.sock320, buffer, (uint16_t)sizeof(buffer), addr, NULL, &rxTime);
        if (n <= 0)
            break; // Terminate on error or socket close
        if (rxTime == 0)
            break; // Invalid time
        mutexLock(&gPtpC.mutex);
        m = ptpHandleFrame(n, (struct ptphdr *)buffer, addr, rxTime);
        mutexUnlock(&gPtpC.mutex);
        ptpPrintFrame(m, (struct ptphdr *)buffer, addr); // Print incoming PTP traffic
    }
    if (gPtpDebugLevel >= 4 && gXcpDebugLevel > 0)
        printf("Terminate PTP multicast 320 thread\n");
    socketClose(&gPtpC.sock320);
    return 0;
}

//-------------------------------------------------------------------------------------------------------
// A2L and XCP
// Create A2L description for measurements and parameters
// Create XCP events

#ifdef OPTION_ENABLE_PTP_XCP

void ptpClientCreateXcpEvents() {

    gPtpC_syncEvent = XcpCreateEvent("PTP_SYNC", 0, 0);
    gPtpC_delayEvent = XcpCreateEvent("PTP_DELAY", 0, 0);
    gPtpC_updateEvent = XcpCreateEvent("PTP_UPDATE", 0, 0);
}

void ptpClientCreateA2lDescription() {

    // Event measurements
    A2lSetAbsoluteAddrMode__i(gPtpC_syncEvent);
    A2lCreateMeasurement(gPtpC.sync_local_time, "SYNC RX timestamp");
    A2lCreateMeasurement(gPtpC.sync_master_time, "SYNC timestamp");
    A2lCreateMeasurement(gPtpC.sync_correction, "SYNC correction");
    A2lCreateMeasurement(gPtpC.sync_sequenceId, "SYNC sequence counter");
    A2lCreateMeasurement(gPtpC.sync_steps, "SYNC mode");
    A2lCreateMeasurement(gPtpC.flup_master_time, "FOLLOW_UP timestamp");
    A2lCreateMeasurement(gPtpC.flup_sequenceId, "FOLLOW_UP sequence counter");
    A2lCreatePhysMeasurement(gPtpC.flup_duration, "FOLLOW_UP duration time after SYNC", "ms", 0.000001, 0.0);
    A2lCreatePhysMeasurement(gPtpC.t1_t2_diff, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.t1_t2_correction, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.t1, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.t2, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.sync_cycle_time, "SYNC cycle time", "ms", 0.000001, 0.0);
    A2lCreateMeasurement(gPtpC.flup_correction, "FOLLOW_UP correction");
    A2lCreatePhysMeasurement(gPtpC.master_drift_raw, "", "ppm", 0.001, 0.0);
    A2lCreatePhysMeasurement(gPtpC.master_drift, "", "ppm", 0.001, 0.0);

    A2lSetAbsoluteAddrMode__i(gPtpC_delayEvent);
    A2lCreateMeasurement(gPtpC.delay_resp_logMessageInterval, "DELAY_RESP delay req message intervall");
    A2lCreateMeasurement(gPtpC.delay_resp_correction, "DELAY_RESP correction");
    A2lCreatePhysMeasurement(gPtpC.delay_resp_duration, "DELAY_RESP response duration time", "ms", 0.000001, 0.0);
    A2lCreateMeasurement(gPtpC.delay_req_local_time, "DELAY_REQ TX timestamp");
    A2lCreateMeasurement(gPtpC.delay_req_sequenceId, "DELAY_REQ sequence counter");
    A2lCreateMeasurement(gPtpC.delay_resp_local_time, "DELAY_RESP RX timestamp");
    A2lCreateMeasurement(gPtpC.delay_resp_master_time, "DELAY_RESP timestamp");
    A2lCreateMeasurement(gPtpC.delay_resp_sequenceId, "DELAY_RESP sequence counter");
    A2lCreatePhysMeasurement(gPtpC.t3_t4_diff, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.t3_t4_correction, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.t3, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.t4, "", "ns", 1.0, 0.0);

    A2lSetAbsoluteAddrMode__i(gPtpC_updateEvent);
    A2lCreatePhysMeasurement(gPtpC.path_delay, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.path_asymmetry, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.path_asymmetry_avg, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.master_offset, "", "ms", 0.000001, 0.0);
    A2lCreatePhysMeasurement(gPtpC.master_time, "", "ns", 1.0, 0.0);
    A2lCreatePhysMeasurement(gPtpC.client_time, "", "ns", 1.0, 0.0);
    // A2lCreateMeasurementGroup("PTPtest", 5 + 1 + 4, "gPtpC.sync_correction", "gPtpC.sync_cycle_time", "gPtpC.flup_correction", "gPtpC.master_drift_raw",
    // "gPtpC.master_drift","gPtpC.delay_resp_logMessageInterval", "gPtpC.path_delay", "gPtpC.master_offset", "gPtpC.master_time", "gPtpC.client_time");

    // Status measurements
    A2lSetAbsoluteAddrMode__i(gPtpC_syncEvent);
    A2lCreateMeasurement(gPtpC.masterCount, "");
    A2lCreateMeasurement(gPtpC.gmIndex, "Master Index");
    A2lCreateMeasurement(gPtpC.gmDomain, "Master Domain");
    A2lCreateMeasurement(gPtpC.gmAddr, "Master IP ADDR as uint32_t");
    A2lCreateMeasurement(gPtpC.gmId, "Master UUID as uint64_t");
    // A2lCreateMeasurementGroup("PTPstatus", 5, "gPtpC.masterCount", "gPtpC.gmIndex", "gPtpC.gmDomain", "gPtpC.gmAddr", "gPtpC.gmId");

    // Parameters
    gPtpC.params_calseg = XcpCreateCalSeg("params", &kParameters, sizeof(kParameters));
    A2lSetSegmentAddrMode(gPtpC.params_calseg, kParameters);
    A2lCreateParameter(kParameters.delayReqCorrectionNs, "DELAY_REQ correction in ns", "ns", 0, 10000);
    A2lCreateParameter(kParameters.delayReqDelayMs, "DELAY_REQ delay to SYNC in ms", "ms", 1, 10000);
    A2lCreateParameter(kParameters.delayReqJitterMs, "DELAY_REQ jitter in ms", "ms", 1, 10000);
    A2lCreateParameter(kParameters.delayReqCycle, "DELAY_REQ cycle ", "", 0, 10);
    // A2lCreateParameterGroup("PTPparams", 3, "gPtpC.delayReqDelayMs", "gPtpC.delayReqJitterMs", "gPtpC.delayReqCycle");
}
#endif

//-------------------------------------------------------------------------------------------------------
// Public functions

// Start PTP Client
bool ptpClientInit(const uint8_t *uuid, uint8_t domain, uint8_t *bindAddr, void (*ptpClientCallback)(uint64_t grandmaster_time, uint64_t local_time, int32_t drift)) {

    printf("PTP: Client uuid %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X domain %u\n", uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], domain);

    memset(&gPtpC, 0, sizeof(gPtpC));

    // PTP client communication parameters
    memcpy(gPtpC.addr, bindAddr, 4);
    gPtpC.domain = domain;
    memcpy(gPtpC.uuid, uuid, 8);
    gPtpC.ptpClientCallback = ptpClientCallback;

    // Parameters
    gPtpC.params = &kParameters;

    // PTP client active master and master history list
    gPtpC.gm = NULL;
    gPtpC.gmLastSeenTime = 0;
    initMasterList();

    mutexInit(&gPtpC.mutex, 0, 1000);

    // Expected sync cycle time until not known
    gPtpC.sync_cycle_time = 1000000000;

    // Create XL-API sockets for event (319) and general messages (320)
    gPtpC.sock319 = gPtpC.sock320 = INVALID_SOCKET;
    if (!socketOpen(&gPtpC.sock319, SOCKET_MODE_BLOCKING | SOCKET_MODE_TIMESTAMPING ))
        return false; // SYNC tx, DELAY_REQ rx timestamps
    if (!socketOpen(&gPtpC.sock320, SOCKET_MODE_BLOCKING))
        return false;
    if (!socketBind(gPtpC.sock320, bindAddr, 320))
        return false;
    if (!socketBind(gPtpC.sock319, bindAddr, 319))
        return false;
    if (gPtpDebugLevel >= 2 && gXcpDebugLevel > 0)
        printf("  Bound PTP sockets to %u.%u.%u.%u:320/319\n", bindAddr[0], bindAddr[1], bindAddr[2], bindAddr[3]);
    if (gPtpDebugLevel >= 2 && gXcpDebugLevel > 0)
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
    create_thread(&gPtpC.threadHandle, ptpThread);

    gPtpC.enabled = true;

    return true;
}

// Stop PTP client
void ptpClientShutdown() {

    cancel_thread(gPtpC.threadHandle);
    cancel_thread(gPtpC.threadHandle320);
    cancel_thread(gPtpC.threadHandle319);
    gPtpC.enabled = false;
    sleepMs(200);
    socketClose(&gPtpC.sock319);
    socketClose(&gPtpC.sock320);
}

tPtpMaster *ptpClientGetGrandmaster() { return gPtpC.gm; }

//-------------------------------------------------------------------------------------------------------
// Print Infos

// Print list of all seen masters
void ptpClientPrintMasterList() {

    uint16_t i;
    printf("\nMaster list:\n");
    for (i = 0; i < gPtpC.masterCount; i++) {
        printMaster(&gPtpC.masterList[i]);
    }

    if (gPtpC.gm != NULL) {
        printf("\nActive grandmaster is %u\n", gPtpC.gm->index);
    }

    printf("\n");
}

// Print info
void ptpClientPrintInfo() {

    printf("\nClient Info:\n");
    printf("UUID:   %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", gPtpC.uuid[0], gPtpC.uuid[1], gPtpC.uuid[2], gPtpC.uuid[3], gPtpC.uuid[4], gPtpC.uuid[5], gPtpC.uuid[6],
           gPtpC.uuid[7]);
    printf("IP:     %u.%u.%u.%u\n", gPtpC.addr[0], gPtpC.addr[1], gPtpC.addr[2], gPtpC.addr[3]);
    printf("Domain: %u\n", gPtpC.domain);
    if (gPtpC.gm != NULL) {
        printf("Current Master: \n");
        printMaster(gPtpC.gm);
    }
    printf("\n");
}

#endif
