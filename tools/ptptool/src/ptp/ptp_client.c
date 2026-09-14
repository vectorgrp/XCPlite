/*----------------------------------------------------------------------------
| File:
|   ptp_client.c
|
| Description:
|   PTP client clock
|   For demonstrating PTP support in XCP
|   Supports IEEE 1588-2008 PTPv2 over UDP/IPv4 in E2E mode
|   Implementation for demonstration purposes without discipline of local clocks
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

#include "platform.h" // from libxcplite for MUTEX, mutexInit, mutexLock, mutexUnlock, clockGetMonotonicNs, sleepUs
#include "sockets.h"  // from libxcplite for SOCKET_HANDLE, htonll

#include "ptp.h"

#ifdef OPTION_ENABLE_PTP_CLIENT

#include "util.h" // from libxcplite for average filter

#include <xcplib.h> // for application programming interface

#include "ptpHdr.h" // PTP protocol message structures
#include "ptp_client.h"

extern uint8_t ptp_log_level;
#define DBG_LEVEL ptp_log_level
#include "dbg_print.h" // for DBG_PRINT_ERROR, DBG_PRINTF_WARNING, ...

tPtpClient *gPtpClient = NULL;

//---------------------------------------------------------------------------------------

// Parameters
typedef struct ptp_client_parameters {
    uint8_t gm_timeout_s;            // Grandmaster timeout in seconds
    uint8_t delay_request_burst;     // Delay request burst after lock onto grandmaster
    uint32_t delay_request_delay_us; // Delay between delay requests in burst mode in microseconds
    uint8_t min_sync_cycles;         // Number of cycles for sync
    uint8_t path_delay_filter_size;  // Size of the path delay average filter
    uint8_t median_filter_size;      // Size of the median filter for master offset and drift calculation
    uint8_t kp_shift;                // P gain exponent for SYNC_MODE_PI
    uint8_t ki_shift;                // I gain exponent for SYNC_MODE_PI
} tPtpClientParameters;

// Default parameter values
static tPtpClientParameters params = {
    .gm_timeout_s = 4,                    // Grandmaster timeout in seconds
    .delay_request_burst = 16,            // Delay request burt after lock onto grandmaster
    .delay_request_delay_us = 10000,      // Delay between delay requests in burst mode in microseconds
    .min_sync_cycles = 5,                 // Number of cycles for sync
    .path_delay_filter_size = 10,         // Size of the path delay average filter
    .median_filter_size = 7,              // Size of the median filter for drift calculations
    .kp_shift = SYNC_PI_KP_SHIFT_DEFAULT, // P gain exponent for SYNC_MODE_PI
    .ki_shift = SYNC_PI_KI_SHIFT_DEFAULT, // I gain exponent for SYNC_MODE_PI
};

//---------------------------------------------------------------------------------------

// Reset the PTP client state
static void clientReset(tPtpClient *client) {
    assert(client != NULL);

    // Grandmaster info
    gPtpClient->gmValid = false;
    gPtpClient->gm_last_update_time = 0;

    // Init protocol state
    client->sync_client_time = 0;      // Local receive timestamp of SYNC
    client->sync_client_time_last = 0; // Local receive timestamp of previous SYNC
    client->sync_cycle_time = 0;       // Master SYNC cycle time
    client->sync_master_time = 0;      // SYNC timestamp
    client->sync_correction = 0;       // SYNC correction
    client->sync_sequenceId = 0;       // SYNC sequence Id
    client->sync_steps = 0;            // SYNC steps removed
    client->flup_master_time = 0;      // FOLLOW_UP timestamp
    client->flup_correction = 0;       // FOLLOW_UP correction
    client->flup_sequenceId = 0;       // FOLLOW_UP sequence Id
    client->delay_req_system_time = 0; // System time when last DELAY_REQ was sent
    client->delay_req_client_time = 0; // Local send timestamp of DELAY_REQ
    client->delay_req_sequenceId = 0;  // Sequence Id of last DELAY_REQ message sent
    client->delay_req_master_time = 0; // DELAY_RESP timestamp
    client->delay_resp_correction = 0; // DELAY_RESP correction
    client->delay_resp_sequenceId = 0; // Sequence Id of last DELAY_RESP message received
    client->delay_resp_logMessageInterval = 0;

    client->sync_sequenceId_last = 0;
    client->delay_resp_sequenceId_last = 0;
    client->delay_request_burst = 0;

    client->is_sync = false;    // true if synchronized to grandmaster
    client->raw_path_delay = 0; // Current raw path delay
    client->path_delay = 0;     // Current path delay
    client->master_offset = 0;  // Current master offset

    syncInit(&client->s12, SYNC_MODE_PI, params.median_filter_size);
    client->s12.kp_shift = params.kp_shift;
    client->s12.ki_shift = params.ki_shift;
    syncInit(&client->s34, SYNC_MODE_PI, params.median_filter_size);
    client->s34.kp_shift = params.kp_shift;
    client->s34.ki_shift = params.ki_shift;
    syncInit(&client->ssw, SYNC_MODE_PI, params.median_filter_size);
    client->ssw.kp_shift = params.kp_shift;
    client->ssw.ki_shift = params.ki_shift;

    median_filter_init(&client->path_delay_filter, params.path_delay_filter_size);
    client->path_delay = 0; // Filtered path delay

    client->gm_last_update_time = clockGetMonotonicNs(); // Timeout from now
}

// Send delay requests
static bool clientSendDelayRequest(tPtp *ptp, tPtpClient *client) {
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);
    assert(client != NULL);
    assert(client->gmValid);

    if (client->delay_req_sequenceId != client->delay_resp_sequenceId) {
        DBG_PRINTF_WARNING("PTP client: Skipping DELAY_REQ, previous request (seqID=%u) has not been answered yet\n", client->delay_req_sequenceId);
        return false;
    } else {
        // Send a delay request to our grandmaster
        if (!ptpSendDelayRequest(ptp, client->gm.domain, client->client_uuid, ++client->delay_req_sequenceId, &client->delay_req_client_time, &client->delay_req_system_time)) {
            DBG_PRINT_ERROR("PTP client: Failed to send DELAY_REQ\n");
            client->delay_req_sequenceId--;
            return false;
        }

        syncUpdate(&client->ssw, client->delay_req_client_time, client->delay_req_system_time);
    }

    return false;
}

//---------------------------------------------------------------------------------------

// New t1,t2 pair available from SYNC message
static void clientSyncUpdate(tPtp *ptp, tPtpClient *client) {

    uint64_t t1 = (client->sync_steps == 1) ? client->sync_master_time : client->flup_master_time; // master clock
    uint64_t t2 = client->sync_client_time;                                                        // local clock
    uint64_t correction = client->sync_correction;                                                 // for master timestamp t1

    syncUpdate(&client->s12, t1 + correction, t2);
}

//---------------------------------------------------------------------------------------

// New t3,t4 pair available from DELAY_REQ/RESP messages
static void clientDelayUpdate(tPtpClient *client) {
    assert(client != NULL);

    uint64_t t1 = (client->sync_steps == 1) ? client->sync_master_time : client->flup_master_time; // master clock
    uint64_t t2 = client->sync_client_time;                                                        // local clock
    uint64_t t3 = client->delay_req_client_time;                                                   // local clock
    uint64_t t4 = client->delay_req_master_time;                                                   // master clock
    uint64_t correction = client->delay_resp_correction;                                           // for master timestamp t4

    syncUpdate(&client->s34, t4 - correction, t3);

    // Update path_delay and master_offset only, when new data from SYNC and DELAY_REQ is available
    if (client->delay_resp_sequenceId != client->delay_resp_sequenceId_last && client->sync_sequenceId != client->sync_sequenceId_last) {

        client->delay_resp_sequenceId_last = client->delay_resp_sequenceId;
        client->sync_sequenceId_last = client->sync_sequenceId;

        /* PTP protocol
           Master   Client
               t1             t1 = SYNC master tx timestamp
                  \
                    t2        t2 = SYNC client rx timestamp
                    t3        t3 = DELAY_REQUEST client tx timestamp
                  /
               t4             t4 = DELAY_RESPONSE master rx timestamp
        */

        if (client->s34.is_sync) { // Drift available from DELAY timestamps

            // Drift correction for t4
            int64_t t4_drift_correction = (int64_t)(t4 - t1) * client->s34.drift_ppb / 1000000000;

            // Calculate mean path delay
            int64_t t21 = ((int64_t)(t2 - t1) - client->sync_correction);
            int64_t t43 = ((int64_t)(t4 - t3) - client->delay_resp_correction - t4_drift_correction);
            client->raw_path_delay = (t21 + t43) / 2;
            assert(client->raw_path_delay >= 0);
            client->path_delay = median_filter_calc(&client->path_delay_filter, client->raw_path_delay);

            // Calculate current master offset
            client->master_offset = t21 - client->path_delay;

            DBG_PRINTF4("t1=%" PRIu64 " ns, t2=%" PRIu64 " ns, t3=%" PRIu64 " ns, t4=%" PRIu64 " ns, t4_drift_correction=%" PRIi64 " ns\n", t1, t2, t3, t4, t4_drift_correction);
            DBG_PRINTF3("path delay = %" PRIi64 " ns, master offset = %" PRIi64 " ns, master_drift = (%llu,%llu) ppm\n", client->path_delay, client->master_offset,
                        client->s34.drift_ppb / 1000, client->s12.drift_ppb / 1000);

            // Set synchronized flag if drift and sw->hw sync is available
            if (!client->is_sync && client->ssw.is_sync && client->s34.is_sync) {
                client->is_sync = true;
                DBG_PRINT3("PTP clock synchronized ! \n");
            }
        }
    }
}

//---------------------------------------------------------------------------------------

// Handle PTP messages (SYNC, FOLLOW_UP, DELAY_RESP) for the PTP client
bool ptpClientHandleFrame(tPtp *ptp, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t rx_timestamp) {

    tPtpClient *client = gPtpClient;
    if (client == NULL)
        return false; // No client instance

    if (!(n >= 44 && n <= 64)) {
        DBG_PRINT_ERROR("Invalid PTP message size\n");
        return false; // PTP message too small or too large
    }

    // Check if this client is locked onto a grandmaster
    if (client->gmValid) {

        // Check if message is from this clients master
        if (client->gm.domain == ptp_msg->domain && (memcmp(client->gm.uuid, ptp_msg->clockId, 8) == 0) && (memcmp(client->gm.addr, addr, 4) == 0)) {

            mutexLock(&client->mutex);

            // SYNC or FOLLOW_UP message from our master
            if (client->gmValid && (ptp_msg->type == PTP_SYNC || ptp_msg->type == PTP_FOLLOW_UP)) {

                client->gm_last_update_time = clockGetMonotonicNs(); // Last master activity in system time

                if (ptp_msg->type == PTP_SYNC) {
                    assert(rx_timestamp != 0);
                    client->sync_client_time_last = client->sync_client_time;
                    client->sync_client_time = rx_timestamp;
                    client->sync_cycle_time = (client->sync_client_time_last == 0) ? 0 : (client->sync_client_time - client->sync_client_time_last);
                    client->sync_master_time = htonl(ptp_msg->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp_msg->timestamp.timestamp_ns);
                    client->sync_sequenceId = htons(ptp_msg->sequenceId);
                    client->sync_correction = (uint32_t)(htonll(ptp_msg->correction) >> 16);
                    client->sync_steps = (htons(ptp_msg->flags) & PTP_FLAG_TWO_STEP) ? 2 : 1;

                    // 1 step sync update
                    if (client->sync_steps == 1) {
                        clientSyncUpdate(ptp, client);
                        // Delay request immediately after each SYNC
                        clientSendDelayRequest(ptp, client);
                    }
                }

                else { // FOLLOW_UP

                    client->flup_master_time = htonl(ptp_msg->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp_msg->timestamp.timestamp_ns);
                    client->flup_sequenceId = htons(ptp_msg->sequenceId);
                    client->flup_correction = (uint32_t)(htonll(ptp_msg->correction) >> 16);
                }

                // 2 step sync update, SYNC and FOLLOW_UP may be received in any order (thread319 and thread320)
                if (client->sync_steps == 2 && client->sync_sequenceId == client->flup_sequenceId) {
                    clientSyncUpdate(ptp, client); // 2 step
                    // Delay request immediately after each FOLLOW_UP
                    clientSendDelayRequest(ptp, client);
                }
            }

            // DELAY_RESP message from our master
            else if (ptp_msg->type == PTP_DELAY_RESP) {

                if (memcmp(client->client_uuid, ptp_msg->u.r.clockId, 8) == 0) { // Check delay request response is for us
                    client->delay_req_master_time = htonl(ptp_msg->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp_msg->timestamp.timestamp_ns);
                    client->delay_resp_sequenceId = htons(ptp_msg->sequenceId);
                    if (client->delay_resp_sequenceId != client->delay_req_sequenceId) {
                        DBG_PRINTF_WARNING("PTP client: DELAY_RESP sequenceId %u does not match last DELAY_REQ sequenceId %u\n", client->delay_resp_sequenceId,
                                           client->delay_req_sequenceId);
                    }
                    client->delay_resp_correction = (uint32_t)(htonll(ptp_msg->correction) >> 16);
                    client->delay_resp_logMessageInterval = ptp_msg->logMessageInterval;

                    // update DELAY_REQ cycletime (DELAY_REQ has constant delay to SYNC (parameter delayReqDelayMs), logMessageInterval is realized by skipping SYNCs
                    // @@@@ Not implemented yet
                    // if (delayReqCycle == 0)
                    //     delayReqCycle = 1 << gPtpC.delay_resp_logMessageInterval;

                    DBG_PRINTF5("PTP client: DELAY_RESP received from %u.%u.%u.%u: seqID=%u, t3=%llu, t4=%llu, corr=%u\n", addr[0], addr[1], addr[2], addr[3],
                                client->delay_resp_sequenceId, (unsigned long long)client->delay_req_client_time, (unsigned long long)client->delay_req_master_time,
                                client->delay_resp_correction);
                    clientDelayUpdate(client);

                    if (client->delay_request_burst > 0) {
                        // In burst mode, send next delay request immediately
                        sleepUs(params.delay_request_delay_us);
                        clientSendDelayRequest(ptp, client);
                        client->delay_request_burst--;
                        DBG_PRINTF4("PTP client: DELAY_REQ burst, remaining %u\n", client->delay_request_burst);
                    }

                } else {
                    DBG_PRINTF5("PTP client: DELAY_RESP received for another slave clock %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", ptp_msg->u.r.clockId[0],
                                ptp_msg->u.r.clockId[1], ptp_msg->u.r.clockId[2], ptp_msg->u.r.clockId[3], ptp_msg->u.r.clockId[4], ptp_msg->u.r.clockId[5],
                                ptp_msg->u.r.clockId[6], ptp_msg->u.r.clockId[7]

                    );
                }
            }

            // ANNOUNCE message from our master
            else if (ptp_msg->type == PTP_ANNOUNCE) {
                // Update grandmaster info
                client->gm_last_update_time = clockGetMonotonicNs(); // Last master activity in system time
                client->gm.a = ptp_msg->u.a;

            }

            // Unknown message type
            else {
                DBG_PRINTF_WARNING("PTP client: Unexpected PTP message type %u from grandmaster\n", ptp_msg->type);
            }

            mutexUnlock(&client->mutex);

            return true; // Message handled

        } // message for our master

    } // master locked

    // Client has not yet seen his grandmaster
    else {
        // Check if announce messages from any master if match this clients master filter
        if (ptp_msg->type == PTP_ANNOUNCE) {

            // Check if domain, uuid (if specified) and addr (if specified) match
            if (true // Match all masters
            ) {

                mutexLock(&client->mutex);

                clientReset(client); // Reset client state
                memcpy(client->gm.addr, addr, 4);
                memcpy(client->gm.uuid, ptp_msg->clockId, 8);
                client->gm.domain = ptp_msg->domain;
                client->gm.a = ptp_msg->u.a;
                client->gm_last_update_time = clockGetMonotonicNs(); // Timeout from now
                client->gmValid = true;
                client->delay_request_burst = params.delay_request_burst; // Send delay request burst after lock

                mutexUnlock(&client->mutex);

                DBG_PRINTF4("PTP client: Locked !!! Grandmaster clockId = %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", ptp_msg->clockId[0], ptp_msg->clockId[1], ptp_msg->clockId[2],
                            ptp_msg->clockId[3], ptp_msg->clockId[4], ptp_msg->clockId[5], ptp_msg->clockId[6], ptp_msg->clockId[7]);

                return true; // Message handled
            }
        }
    }

    return true;
}

//---------------------------------------------------------------------------------------

// PTP client task, called periodically
// Returns clock state
uint8_t ptpClientTask(tPtp *ptp) {

    tPtpClient *client = gPtpClient;
    if (client == NULL)
        return CLOCK_STATE_FREE_RUNNING; // No client instance

    mutexLock(&client->mutex);

    // Check for grandmaster timeout
    if (client->gmValid) {
        uint64_t now = clockGetMonotonicNs();
        uint64_t elapsed = now - client->gm_last_update_time;
        if (elapsed / (double)CLOCK_TICKS_PER_S > params.gm_timeout_s) {
            DBG_PRINTF3("PTP grandmaster lost ! timeout after %us. Last seen %gs ago\n", params.gm_timeout_s, elapsed / (double)CLOCK_TICKS_PER_S);
            client->gmValid = false;
            client->is_sync = false;
        }
    }

    mutexUnlock(&client->mutex);

    // Return clock state
    if (gPtpClient->gmValid) {
        // Check if master is sufficiently synchronized
        if (gPtpClient->is_sync) {
            return CLOCK_STATE_SYNCH; // Clock is synchronized to grandmaster
        } else {
            return CLOCK_STATE_SYNCH_IN_PROGRESS; // Clock is synchronizing to grandmaster
        }
    } else {
        return CLOCK_STATE_FREE_RUNNING; // No master locked
    }
}

// Create a PTP client singleton instance
tPtpClient *ptpCreateClient(tPtp *ptp) {

    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    // Create singleton instance
    if (gPtpClient != NULL) {
        DBG_PRINT_ERROR("PTP client instance already exists\n");
        return gPtpClient;
    }
    gPtpClient = (tPtpClient *)malloc(sizeof(tPtpClient));
    memset(gPtpClient, 0, sizeof(tPtpClient));

    // Generate a random client UUID
    // TODO: use MAC address or other unique identifier
    // TODO: Use the same as XCP slave UUID
    for (int i = 0; i < 8; i++) {
        gPtpClient->client_uuid[i] = (uint8_t)(rand() & 0xFF);
    }

    mutexInit(&gPtpClient->mutex, true, 1000); // Protect this client instance state

    clientReset(gPtpClient);

    DBG_PRINTF4("Created PTP client, UUID: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", gPtpClient->client_uuid[0], gPtpClient->client_uuid[1], gPtpClient->client_uuid[2],
                gPtpClient->client_uuid[3], gPtpClient->client_uuid[4], gPtpClient->client_uuid[5], gPtpClient->client_uuid[6], gPtpClient->client_uuid[7]);

    return gPtpClient;
}

//---------------------------------------------------------------------------------------

// Shutdown PTP client
void ptpClientShutdown(tPtp *ptp) {

    if (gPtpClient == NULL)
        return; // No client instance

    mutexDestroy(&gPtpClient->mutex);
    free(gPtpClient);
    gPtpClient = NULL;
}

//---------------------------------------------------------------------------------------

// Get grandmaster clock time in nanoseconds
// If not synchronized, return system clock time
// @@@@ TODO: Must be thread safe and lock free
uint64_t ptpClientGetGrandmasterClock() {

    uint64_t sys_clock = clockGetMonotonicNs();
    uint64_t client_clock = syncInterpolateT1(&gPtpClient->ssw, sys_clock);
    uint64_t master_clock = syncInterpolateT1(&gPtpClient->s34, client_clock) + gPtpClient->path_delay;
    return master_clock;
}

void ptpClientPrintState(tPtp *ptp) {
    tPtpClient *client = gPtpClient;
    if (client == NULL) {
        return;
    }

    mutexLock(&client->mutex);

    if (client->gmValid) {
        printf("PTP Client State:\n");
        printf("  Grandmaster clockId: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", client->gm.uuid[0], client->gm.uuid[1], client->gm.uuid[2], client->gm.uuid[3],
               client->gm.uuid[4], client->gm.uuid[5], client->gm.uuid[6], client->gm.uuid[7]);
        printf("  Synchronized: %s\n", client->is_sync ? "Yes" : "No");
        printf("  Path delay: %" PRIi64 " ns\n", client->raw_path_delay);
        printf("  Master offset: %" PRIi64 " ns\n", client->master_offset);
        printf("  Master drift: %g ppm\n", client->s34.drift_ppb / 1000.0);
    } else {
        printf("PTP Client State: No grandmaster locked\n");
    }

    mutexUnlock(&client->mutex);
}

#endif
