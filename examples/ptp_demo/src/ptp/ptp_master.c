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
#include "ptp_master.h"

//-------------------------------------------------------------------------------------------------------
// XCP

#ifdef OPTION_ENABLE_XCP

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

#endif

//---------------------------------------------------------------------------------------

// Default master parameter values
static const master_parameters_t master_params = {
    .announceCycleTimeMs = ANNOUNCE_CYCLE_TIME_MS_DEFAULT, // ANNOUNCE rate
    .syncCycleTimeMs = SYNC_CYCLE_TIME_MS_DEFAULT          // SYNC rate
};

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

void masterPrintState(tPtp *ptp, tPtpMaster *master) {
    char ts[64];
    uint64_t t;

    printf("\nMaster Info:\n");

    printf("  UUID:           %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", master->uuid[0], master->uuid[1], master->uuid[2], master->uuid[3], master->uuid[4], master->uuid[5],
           master->uuid[6], master->uuid[7]);
    printf("  IP:             %u.%u.%u.%u\n", ptp->if_addr[0], ptp->if_addr[1], ptp->if_addr[2], ptp->if_addr[3]);
    printf("  Interface:      %s\n", ptp->if_name);
    printf("  Domain:         %u\n", master->domain);
    if (!master->active) {
        printf(" Status:         INACTIVE\n");
    } else {
        printf("  ANNOUNCE cycle: %ums\n", master->params->announceCycleTimeMs);
        printf("  SYNC cycle:     %ums\n", master->params->syncCycleTimeMs);
        printf("  Client list:\n");
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
void masterInit(tPtp *ptp, tPtpMaster *master, uint8_t domain, const uint8_t *uuid) {

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
bool masterTask(tPtp *ptp) {

    for (int i = 0; i < ptp->master_count; i++) {
        tPtpMaster *master = ptp->master_list[i];
        assert(master != NULL);

        // Update master parameters (update XCP calibrations)
#ifdef OPTION_ENABLE_XCP
        // Each master instance holds its parameter lock continuously, so it may take about a second to make calibration changes effective (until all updates are done)
        XcpUpdateCalSeg((void **)&master->params);
#endif

        if (!master->active)
            continue;
        ;

        uint64_t t = clockGet();
        uint32_t announceCycleTimeMs = master->params->announceCycleTimeMs; // Announce message cycle time in ms
        uint32_t syncCycleTimeMs = master->params->syncCycleTimeMs;         // SYNC message cycle time in ms

        // Announce cycle
        if (announceCycleTimeMs > 0 && t - master->announceCycleTimer > announceCycleTimeMs * CLOCK_TICKS_PER_MS) {
            master->announceCycleTimer = t;
            if (!ptpSendAnnounce(ptp, master->domain, master->uuid, ++master->sequenceIdAnnounce)) {
                printf("ERROR: Failed to send ANNOUNCE\n");
            }
        }

        // Sync cycle
        if (syncCycleTimeMs > 0 && t - master->syncCycleTimer > syncCycleTimeMs * CLOCK_TICKS_PER_MS) {
            master->syncCycleTimer = t;

            mutexLock(&ptp->mutex);

            if (!ptpSendSync(ptp, master->domain, master->uuid, &master->syncTxTimestamp, ++master->sequenceIdSync)) {
                printf("ERROR: Failed to send SYNC\n");
            } else {
                if (master->syncTxTimestamp == 0) {
                    printf("ERROR: SYNC tx timestamp not available !\n");
                } else {
                    if (!ptpSendSyncFollowUp(ptp, master->domain, master->uuid, master->syncTxTimestamp, master->sequenceIdSync)) {
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

    return true;
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
                mutexLock(&ptp->mutex);
                ok = ptpSendDelayResponse(ptp, master->domain, master->uuid, ptp_msg, rxTimestamp);
                mutexUnlock(&ptp->mutex);
                if (!ok)
                    return false;

                // Maintain PTP client list
                uint16_t i = lookupClient(master, addr, ptp_msg->clockId);
                bool newClient = (i >= MAX_CLIENTS);
                if (newClient) {
                    i = addClient(master, addr, ptp_msg->clockId, ptp_msg->domain);
                    if (ptp->log_level >= 2) {
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

tPtpMasterHandle ptpCreateMaster(tPtp *ptp, const char *name, uint8_t domain, const uint8_t *uuid) {

    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    if (ptp->master_count >= PTP_MAX_MASTERS) {
        DBG_PRINT_ERROR("Maximum number of PTP masters reached\n");
        return NULL;
    }

    // Create and initialize master instance
    tPtpMaster *master = (tPtpMaster *)malloc(sizeof(tPtpMaster));
    memset(master, 0, sizeof(tPtpMaster));
    strncpy(master->name, name, sizeof(master->name) - 1);
    masterInit(ptp, master, domain, uuid);
    master->log_level = ptp->log_level;

    // Register the master instance
    ptp->master_list[ptp->master_count++] = master;

    return (tPtpMasterHandle)master;
}
