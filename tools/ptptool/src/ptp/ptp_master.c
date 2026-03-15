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

#include <arpa/inet.h> // for htons, htonl
#include <assert.h>    // for assert
#include <inttypes.h>  // for PRIu64
#include <math.h>      // for fabs
#include <signal.h>    // for signal handling
#include <stdbool.h>   // for bool
#include <stdint.h>    // for uintxx_t
#include <stdio.h>     // for printf
#include <stdlib.h>    // for malloc, free
#include <string.h>    // for sprintf

#include "ptp.h"

#ifdef OPTION_ENABLE_PTP_MASTER

extern uint8_t ptp_log_level;
#define DBG_LEVEL ptp_log_level
#include "dbg_print.h" // for DBG_PRINT_ERROR, DBG_PRINTF_WARNING, ...

#include "filter.h" // for average filter

#include "ptpHdr.h" // PTP protocol message structures
#include "ptp_master.h"

// #ifdef _LINUX
// #include "phc.h"
// #endif

//-------------------------------------------------------------------------------------------------------
// XCP

#ifdef OPTION_ENABLE_XCP

#include <a2l.h>    // for A2l generation
#include <xcplib.h> // for application programming interface

#endif

//---------------------------------------------------------------------------------------

// Default master parameter values
static tMasterParams master_params = {
    .announce_interval_ms = ANNOUNCE_CYCLE_TIME_MS_DEFAULT, // ANNOUNCE rate
    .sync_interval_ms = SYNC_CYCLE_TIME_MS_DEFAULT,         // SYNC rate
#ifdef MASTER_TIME_ADJUST
    .enable_test_time_adjustment = false,
    .drift = 0,       // PTP master time drift in ns/s
    .drift_drift = 0, // PTP master time drift drift in ns/s2
    .offset = 0,      // PTP master time offset in ns
    .jitter = 0,      // PTP master time jitter in ns
#endif
};

//-------------------------------------------------------------------------------------------------------
// Master time drift, drift_drift, jitter and offset calculation

#ifdef MASTER_TIME_ADJUST

// Initialize test time parameters
static void testTimeInit(tPtpMaster *master) {
    assert(master != NULL);
    master->testTimeDrift = 0;           // Current drift in ns/s
    master->testTimeCurrentDrift = 0;    // Current drift including drift_drift
    master->testTimeSyncDriftOffset = 0; // Current offset: testTime = originTime+testTimeSyncDriftOffset
    master->testTimeLast = 0;            // Current test time
    master->testTimeLastSync = 0;        // Original time of last sync
    mutexInit(&master->testTimeMutex, 0, 1000);
}

// Calculate simulated test time from origin time applying drift, drift_drift, offset and jitter
static uint64_t testTimeAdjust(tPtpMaster *master, uint64_t originTime) {

    uint64_t t = originTime;

    if (master->params->enable_test_time_adjustment) {

        assert(t >= master->testTimeLastSync);

        mutexLock(&master->testTimeMutex);

        // time since last sync
        uint64_t dt = t - master->testTimeLastSync;

        //  Apply drift offset
        int64_t drift_offset = (int64_t)((master->testTimeCurrentDrift * (int64_t)dt) / 1000000000) + master->testTimeSyncDriftOffset;
        t += drift_offset;

        // Apply jitter
        int64_t jitter_offset = 0;
        if (master->params->jitter > 0) {
            jitter_offset = (int64_t)(((double)rand() / (double)RAND_MAX) * 2.0 * (double)(master->params->jitter + 1) - (double)(master->params->jitter + 1));
            t += jitter_offset;
        }

        // Apply offset
        t += master->params->offset;

        mutexUnlock(&master->testTimeMutex);

        // warn if time is non monotonic
        if (t < master->testTimeLast) {
            DBG_PRINTF_ERROR("Non monotonic time ! (dt=-%" PRIu64 ")\n", master->testTimeLast - t);
        }

        if (ptp_log_level >= 5) {
            if (originTime != t) {
                printf("TEST: time adjust: originTime=%" PRIu64 " ns, drift_offset=%" PRIi64 " ns, jitter=%" PRIi64 " ns, offset=%d ns => testTime=%" PRIu64 " ns\n", originTime,
                       drift_offset, jitter_offset, master->params->offset, t);
            }
        }
    }

    master->testTimeLast = t;
    return t;
}

// Recalculate test time sync offset and zero test time drift offset
// At drift 100ppm, calculation would overflow after 2,8s
static void testTimeSync(tPtpMaster *master, uint64_t originTime) {

    if (originTime < master->testTimeLastSync)
        return; // Ignore non monotonic time

    // Check if drift parameter has changed since last sync
    assert(master->params->drift >= -1000000 && master->params->drift <= +1000000);
    if (master->params->drift != master->testTimeDrift) {
        master->testTimeDrift = master->testTimeCurrentDrift = master->params->drift;
        if (ptp_log_level >= 3) {
            printf("PTP Master %s: new drift=%d ns/s\n", master->name, master->testTimeDrift);
        }
    }

    mutexLock(&master->testTimeMutex);

    if (master->testTimeLastSync > 0) {

        // time since last sync
        uint64_t dt = originTime - master->testTimeLastSync;
        assert(dt < 2000000000); // Be sure integer calculation does not overflow

        int64_t o = (int64_t)((master->testTimeCurrentDrift * (int64_t)dt) / 1000000000);
        master->testTimeSyncDriftOffset += o;
        // printf("sync dt=%" PRIu64 ", driftOffset=%d, timeOffset=%d\n", dt, master->testTimeDriftOffset, master->testTimeSyncDriftOffset);

        // Apply drift drift
        master->testTimeCurrentDrift += (int32_t)((master->params->drift_drift * (int64_t)dt) / 1000000000);
    }

    master->testTimeLastSync = originTime;

    if (ptp_log_level >= 5 && (master->testTimeSyncDriftOffset) != 0 || master->testTimeCurrentDrift != 0) {
        printf("    testTimeSync: originTime=%" PRIu64 " ns, testTimeSyncDriftOffset=%" PRIi64 " ns, testTimeCurrentDrift=%d ns/s\n", originTime, master->testTimeSyncDriftOffset,
               master->testTimeCurrentDrift);
    }

    mutexUnlock(&master->testTimeMutex);
}

#endif

//-------------------------------------------------------------------------------------------------------
// Client list

static void initClientList(tPtpMaster *master) {
    master->clientCount = 0;
    for (uint16_t i = 0; i < master->clientCount; i++)
        memset(&master->client[i], 0, sizeof(master->client[i]));
}

void printClient(tPtpMaster *master, uint16_t i) {

    char ts[64];
    printf("    %u: addr=%u.%u.%u.%u: domain=%u uuid=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X time=%s corr=%uns diff=%" PRIi64 " cycle=%u cycle_time=%gs\n", i,
           master->client[i].addr[0], master->client[i].addr[1], master->client[i].addr[2], master->client[i].addr[3],                   //
           master->client[i].domain, master->client[i].id[0], master->client[i].id[1], master->client[i].id[2], master->client[i].id[3], //
           master->client[i].id[4], master->client[i].id[5], master->client[i].id[6], master->client[i].id[7],                           //
           clockGetString(ts, sizeof(ts), master->client[i].time), master->client[i].corr, master->client[i].diff, master->client[i].cycle_counter,
           (double)master->client[i].cycle_time / 1e9);
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

void masterPrintState(tPtp *ptp, int index) {

    assert(index >= 0 && index < ptp->master_count);
    tPtpMaster *master = ptp->master_list[index];
    assert(master != NULL);

    char ts[64];
    uint64_t t;

    printf("\nMaster %u:\n", index + 1);
    printf("  UUID:           %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", master->uuid[0], master->uuid[1], master->uuid[2], master->uuid[3], master->uuid[4], master->uuid[5],
           master->uuid[6], master->uuid[7]);
    printf("  IP:             %u.%u.%u.%u\n", ptp->ifaddr[0], ptp->ifaddr[1], ptp->ifaddr[2], ptp->ifaddr[3]);
    printf("  Interface:      %s\n", ptp->ifname);
    printf("  Domain:         %u\n", master->domain);
    if (!master->active) {
        printf(" Status:         INACTIVE\n");
    } else {
        printf("  ANNOUNCE cycle: %ums\n", master->params->announce_interval_ms);
        printf("  SYNC cycle:     %ums\n", master->params->sync_interval_ms);
        if (master->clientCount > 0) {
            printf("  Client list:\n");
            for (uint16_t i = 0; i < master->clientCount; i++) {
                printClient(master, i);
            }
        }
    }

    printf("\n");
}

// Initialize the PTP master state
void masterInit(tPtp *ptp, tPtpMaster *master, uint8_t domain, const uint8_t *uuid) {

    master->domain = domain;

    // Generate UUID from MAC address if not provided
    if (uuid == NULL || memcmp(uuid, "\0\0\0\0\0\0\0\0", 8) == 0) {
        if (!ptpGenerateLocalClockUUID(ptp->ifname, master->uuid)) {
            printf("ERROR: Failed to get MAC based for interface %s, using zero UUID\n", ptp->ifname);
            memset(master->uuid, 0, 8);
        }
    }

    initClientList(master);
    master->params = &master_params;

    // XCP instrumentation
#ifdef OPTION_ENABLE_XCP

    // Create XCP event for master SYNC messages
    master->xcp_event = XcpCreateEvent(master->name, 0, 0);
    assert(master->xcp_event != XCP_UNDEFINED_EVENT_ID);

    // Create XCP calibration parameter segment, if not already existing
    master->xcp_calseg = XcpCreateCalSeg("master_params", &master_params, sizeof(master_params));
    assert(master->xcp_calseg != XCP_UNDEFINED_CALSEG);
    master->params = (tMasterParams *)XcpLockCalSeg(master->xcp_calseg); // Initial lock of the calibration segment (for persistence)

    A2lOnce() {

        // Create A2L parameter definitions for master parameters
        A2lSetSegmentAddrMode(master->xcp_calseg, master_params);
        A2lCreateParameter(master_params.announce_interval_ms, "Announce cycle time (ms)", "", 0, 10000);
        A2lCreateParameter(master_params.sync_interval_ms, "Sync cycle time (ms)", "", 0, 10000);
        A2lCreateParameter(master_params.enable_test_time_adjustment, "Enable test time adjustment", "", 0, 1);
        A2lCreateParameter(master_params.drift, "Master time drift (ns/s)", "", -100000, +100000);
        A2lCreateParameter(master_params.drift_drift, "Master time drift drift (ns/s2)", "", -1000, +1000);
        A2lCreateParameter(master_params.jitter, "Master time jitter (ns)", "", 0, 1000000);
        A2lCreateParameter(master_params.offset, "Master time offset (ns)", "", -1000000000, +1000000000);

        // Create a A2L typedef for the PTP client structure
        A2lTypedefBegin(tPtpMasterClient, NULL, "PTP client structure");
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
    A2lCreateInstance(name, tPtpMasterClient, MAX_CLIENTS, m.client, "PTP client list"); // Array of clients
    A2lCreateMeasurementInstance(master->name, m.syncTxTimestamp, "SYNC tx timestamp");
    A2lCreateMeasurementInstance(master->name, m.sequenceIdAnnounce, "Announce sequence id");
    A2lCreateMeasurementInstance(master->name, m.sequenceIdSync, "SYNC sequence id");

#endif

    uint64_t t = clockGet();
    master->announceCycleTimer = 0;                                                                                // Send announce immediately
    master->syncCycleTimer = t + 100 * CLOCK_TICKS_PER_MS - master->params->sync_interval_ms * CLOCK_TICKS_PER_MS; // First SYNC after 100ms
    master->syncTxTimestamp = 0;
    master->sequenceIdAnnounce = 0;
    master->sequenceIdSync = 0;

    master->active = true;

    if (ptp_log_level >= 3) {
        printf("PTP Master '%s' initialized: domain=%u, UUID=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", master->name, master->domain, master->uuid[0], master->uuid[1],
               master->uuid[2], master->uuid[3], master->uuid[4], master->uuid[5], master->uuid[6], master->uuid[7]);
        printf(" PTP Master '%s': Announce interval=%ums, SYNC interval=%ums\n", master->name, master->params->announce_interval_ms, master->params->sync_interval_ms);
    }
}

// Master main cycle
void masterTask(tPtp *ptp) {

    for (int i = 0; i < ptp->master_count; i++) {
        tPtpMaster *master = ptp->master_list[i];
        assert(master != NULL);

        // Update master parameters (update XCP calibrations)
#ifdef OPTION_ENABLE_XCP
        tXcpCalSegIndex c = master->xcp_calseg;
        tMasterParams *p = (tMasterParams *)XcpLockCalSeg(c);
        memcpy(master->params, p, sizeof(*master->params));
        XcpUnlockCalSeg(c);
#endif

        if (!master->active)
            continue;
        ;

        uint64_t t = clockGet();
        uint32_t announce_interval_ms = master->params->announce_interval_ms; // Announce message cycle time in ms
        uint32_t sync_interval_ms = master->params->sync_interval_ms;         // SYNC message cycle time in ms

        // Announce cycle
        if (announce_interval_ms > 0 && t - master->announceCycleTimer > announce_interval_ms * CLOCK_TICKS_PER_MS) {
            master->announceCycleTimer = t;
            if (!ptpSendAnnounce(ptp, master->domain, master->uuid, ++master->sequenceIdAnnounce)) {
                printf("ERROR: Failed to send ANNOUNCE\n");
            }
        }

        // Sync cycle
        if (sync_interval_ms > 0 && t - master->syncCycleTimer > sync_interval_ms * CLOCK_TICKS_PER_MS) {
            master->syncCycleTimer = t;

            mutexLock(&ptp->mutex);

            if (!ptpSendSync(ptp, master->domain, master->uuid, &master->syncTxTimestamp, ++master->sequenceIdSync)) {
                printf("ERROR: Failed to send SYNC\n");
            } else {
                if (master->syncTxTimestamp == 0) {
                    printf("ERROR: SYNC tx timestamp not available !\n");
                } else {

#ifdef MASTER_TIME_ADJUST
                    testTimeSync(master, master->syncTxTimestamp); // Adjust test time parameters at SYNC, not at DELAY_REQ
                    uint64_t t = testTimeAdjust(master, master->syncTxTimestamp);
#else
                    uint64_t t = master->syncTxTimestamp;
#endif

                    if (!ptpSendSyncFollowUp(ptp, master->domain, master->uuid, t, master->sequenceIdSync)) {
                        printf("ERROR: Failed to send SYNC FOLLOW UP\n");
                    }
                }
            }

            mutexUnlock(&ptp->mutex);

            // XCP measurement event (relative addressing mode for this master instance)
#ifdef OPTION_ENABLE_XCP
            XcpEventExt_Var(master->xcp_event, 1, (const uint8_t *)master); // Base address 0 (addr ext = 2) is master instance

#endif
        }
    }
}

//-------------------------------------------------------------------------------------------------------
// PTP master frame handling

// Handle a received Delay Request message
bool masterHandleFrame(tPtp *ptp, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t rxTimestamp) {

    if (!(n >= 44 && n <= 64)) {
        DBG_PRINT_ERROR("Invalid PTP message size\n");
        return false; // PTP message too small or too large
    }

    for (int i = 0; i < ptp->master_count; i++) {
        tPtpMaster *master = ptp->master_list[i];

        if (!master->active)
            continue;
        ;

        if (ptp_msg->type == PTP_ANNOUNCE) {
            if (ptp_msg->domain == master->domain && memcmp(ptp_msg->clockId, master->uuid, 8) != 0) {
                // There is another master on the network with the same domain and a different UUID
                printf("PTP Master '%s': Received ANNOUNCE from another master with same domain %u (UUID %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X)\n", master->name, ptp_msg->domain,
                       ptp_msg->clockId[0], ptp_msg->clockId[1], ptp_msg->clockId[2], ptp_msg->clockId[3], ptp_msg->clockId[4], ptp_msg->clockId[5], ptp_msg->clockId[6],
                       ptp_msg->clockId[7]);
                printf("PTP Master '%s': Best master algorithm is not supported!\n", master->name);
                master->active = false;
            }
        }

        if (ptp_msg->type == PTP_DELAY_REQ) {

            if (ptp_msg->domain == master->domain) {
                bool ok;
#ifdef MASTER_TIME_ADJUST
                uint64_t t4 = testTimeAdjust(master, rxTimestamp);
#else
                uint64_t t4 = rxTimestamp;
#endif
                mutexLock(&ptp->mutex);
                ok = ptpSendDelayResponse(ptp, master->domain, master->uuid, ptp_msg, t4);
                mutexUnlock(&ptp->mutex);
                if (!ok)
                    return false;

                // Maintain PTP client list
                uint16_t i = lookupClient(master, addr, ptp_msg->clockId);
                bool newClient = (i >= MAX_CLIENTS);
                if (newClient) {
                    i = addClient(master, addr, ptp_msg->clockId, ptp_msg->domain);
                    if (ptp_log_level >= 3) {
                        printf("\nPTP Master '%s': New client %u.%u.%u.%u domain %u UUID %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n\n", master->name, addr[0], addr[1], addr[2],
                               addr[3], ptp_msg->domain, ptp_msg->clockId[0], ptp_msg->clockId[1], ptp_msg->clockId[2], ptp_msg->clockId[3], ptp_msg->clockId[4],
                               ptp_msg->clockId[5], ptp_msg->clockId[6], ptp_msg->clockId[7]);
                    }
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
    }
    return true;
}

tPtpMaster *ptpCreateMaster(tPtp *ptp, const char *name, uint8_t domain, const uint8_t *uuid, uint32_t announce_interval_ms, uint32_t sync_interval_ms, int32_t offset,
                            int32_t drift, int32_t drift_drift, int32_t jitter) {

    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    if (ptp->master_count >= PTP_MAX_MASTERS) {
        DBG_PRINT_ERROR("Maximum number of PTP masters reached\n");
        return NULL;
    }

    // Set master parameters
    if (announce_interval_ms > 0)
        master_params.announce_interval_ms = announce_interval_ms;
    if (sync_interval_ms > 0)
        master_params.sync_interval_ms = sync_interval_ms;
#ifdef MASTER_TIME_ADJUST
    master_params.enable_test_time_adjustment = offset != 0 || drift != 0 || drift_drift != 0 || jitter != 0;
    master_params.offset = offset;
    master_params.drift = drift;
    master_params.drift_drift = drift_drift;
    master_params.jitter = jitter;
#endif

    // Create and initialize master instance
    tPtpMaster *master = (tPtpMaster *)malloc(sizeof(tPtpMaster));
    memset(master, 0, sizeof(tPtpMaster));
    strncpy(master->name, name, sizeof(master->name) - 1);
    masterInit(ptp, master, domain, uuid);

    // Init offset and drift calculation
#ifdef MASTER_TIME_ADJUST
    testTimeInit(master);
#endif

    // Register the master instance
    ptp->master_list[ptp->master_count++] = master;

    return master;
}

void ptpMasterShutdown(tPtpMaster *master) {

    assert(master != NULL);

    // Terminate master activity
    master->active = false;

// XCP cleanup
#ifdef OPTION_ENABLE_XCP
    if (master->params != NULL) {
        XcpUnlockCalSeg(master->xcp_calseg);
        master->params = NULL;
    }
#endif
    free(master);
}

#endif // OPTION_ENABLE_PTP_MASTER