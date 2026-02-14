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

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <math.h>     // for fabs
#include <signal.h>   // for signal handling
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t
#include <stdio.h>    // for printf
#include <stdlib.h>   // for malloc, free
#include <string.h>   // for sprintf

#include "platform.h" // from libxcplite for SOCKET_HANDLE, ...

#include "ptp.h"

#ifdef OPTION_ENABLE_PTP_CLIENT

#include "filter.h" // for average filter

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
    uint8_t gm_timeout_s;               // Grandmaster timeout in seconds
    uint8_t delay_request_burst;        // Delay request burt after lock onto grandmaster
    uint32_t delay_request_delay_us;    // Delay between delay requests in burst mode in microseconds
    uint8_t min_sync_cycles;            // Number of cycles for sync
    double min_sync_drift_drift;        // Minimum drift of drift for sync lock in ns/(s*s)
    uint8_t linreg_filter_size;         // Size of the linear regression filter
    uint8_t linreg_offset_filter_size;  // Size of the linear regression offset filter
    uint8_t linreg_jitter_filter_size;  // Size of the linear regression jitter filter
    uint8_t jitter_rms_filter_size;     // Size of the jitter RMS average filter
    uint8_t jitter_avg_filter_size;     // Size of the jitter average filter
    uint8_t path_delay_avg_filter_size; // Size of the path delay average filter
} tPtpClientParameters;

// Default parameter values
static tPtpClientParameters params = {
    .gm_timeout_s = 4,                // Grandmaster timeout in seconds
    .delay_request_burst = 16,        // Delay request burt after lock onto grandmaster
    .delay_request_delay_us = 10000,  // Delay between delay requests in burst mode in microseconds
    .min_sync_cycles = 5,             // Number of cycles for sync
    .min_sync_drift_drift = 20.0,     // Minimum drift of drift for sync lock in ns/(s*s)
    .linreg_filter_size = 30,         // Size of the linear regression filter
    .linreg_offset_filter_size = 10,  // Size of the linear regression offset filter
    .linreg_jitter_filter_size = 30,  // Size of the linear regression jitter filter
    .jitter_rms_filter_size = 30,     // Size of the jitter RMS average filter
    .jitter_avg_filter_size = 30,     // Size of the jitter average filter
    .path_delay_avg_filter_size = 10, // Size of the path delay average filter

};

//---------------------------------------------------------------------------------------
// PTP client clock synchronizer

// Initialize clock synchronizer state
static void syncInit(tPtpClockSynchronizer *a, const char *name, tPtpClientParameters *params) {

    a->name = name;

    a->cycle_count = 0; // cycle counter
    a->is_sync = false;
    a->t1 = 0;
    a->t2 = 0;
    a->offset_raw = 0;
    a->t1_offset = 0;
    a->t2_offset = 0;
    a->t1_norm = 0;
    a->t2_norm = 0;
    a->offset_norm = 0;

    // Results
    a->offset = 0;
    a->drift = 0.0;
    a->drift_drift = 0.0;
    a->jitter = 0.0;

    // Linear regression filter state
    a->linreg_drift = 0.0;
    a->linreg_offset = 0.0;
    a->linreg_offset_avg = 0.0;
    a->linreg_drift_drift = 0.0;
    a->linreg_jitter = 0.0;
    a->linreg_jitter_avg = 0.0;
    linreg_filter_init(&a->linreg_filter, params->linreg_filter_size);
    average_filter_init(&a->linreg_offset_filter, params->linreg_offset_filter_size);
    average_filter_init(&a->linreg_jitter_filter, params->linreg_jitter_filter_size);

    // Jitter anaylsis state
    a->jitter = 0.0;
    average_filter_init(&a->jitter_rms_filter, params->jitter_rms_filter_size);
    a->jitter_rms = 0;
    average_filter_init(&a->jitter_avg_filter, params->jitter_avg_filter_size);
    a->jitter_avg = 0;
}

// Print synchronizer state
void syncPrintState(tPtpClockSynchronizer *a) {
    assert(a != NULL);

    printf("Clock Synchronizer %s state:\n", a->name);
    printf("  cycle_count = %u\n", a->cycle_count);
    printf("  is_sync = %s\n", a->is_sync ? "true" : "false");
    printf("  t1 = %" PRIu64 "\n", a->t1);
    printf("  t2 = %" PRIu64 "\n", a->t2);
    printf("  offset_raw = %" PRIi64 "\n", a->offset_raw);
    printf("  t1_offset = %" PRIu64 "\n", a->t1_offset);
    printf("  t2_offset = %" PRIu64 "\n", a->t2_offset);
    printf("  t1_norm = %" PRIi64 "\n", a->t1_norm);
    printf("  t2_norm = %" PRIi64 "\n", a->t2_norm);
    printf("  offset_norm = %" PRIi64 "\n", a->offset_norm);
    printf("  offset = %" PRIi64 "\n", a->offset);
    printf("  drift = %g\n", a->drift);
    printf("  drift_drift = %g\n", a->drift_drift);
    printf("  jitter = %g\n", a->jitter);
}

// Interpolate t1 on target clock from t2 on reference clock
// Function is lock free for fast response
// @@@@ TODO: Thread safety, t2, drift and linreg_offset must be read atomically and consistently
static uint64_t syncInterpolateT1(tPtpClockSynchronizer *a, uint64_t t2) {
    assert(a != NULL);

    if (!a->is_sync) {
        DBG_PRINTF_ERROR("%s: syncInterpolateT1 called while not synchronized\n", a->name);
        return t2;
    }

    // Delta time since last sync reference clock
    int64_t dt2 = (int64_t)(t2 - a->t2);

    // Estimate corresponding delta time on target clock using last syncs drift
    double dt1 = (double)dt2 * (1.0 + a->drift / 1.0E9);

    // Calculate t1 on target clock using last syncs t1 value and its linear regression offset
    return (uint64_t)(a->t1 + a->linreg_offset + (uint64_t)dt1);
}

// Update clock synchronizer state with new (t1 - target clock, t2 - reference clock) timestamp pair
// For example from PTP SYNC messages:
// t1 - corrected master timestamp
// t2 - local clock timestamp
static void syncUpdate(tPtpClockSynchronizer *a, uint64_t t1, uint64_t t2) {

    assert(a != NULL);

    a->cycle_count++;

    // Save current original timestamp pair
    a->t1 = t1;
    a->t2 = t2;

    DBG_PRINTF5("  Sync %s: t1 = %" PRIu64 ", t2 = %" PRIu64 "\n", a->name, t1, t2);

    // Master offset raw value
    a->offset_raw = (int64_t)(t1 - t2); // Positive means master is ahead
    DBG_PRINTF5("      offset_raw = %" PRIi64 " ns\n", a->offset_raw);

    // First round, init normalization offsets
    if (a->t1_offset == 0 || a->t2_offset == 0) {

        a->t1_norm = 0; // corrected sync tx time on master clock
        a->t2_norm = 0; // sync rx time on slave clock

        // Normalization time offsets for t1,t2
        a->t1_offset = t1;
        a->t2_offset = t2;

        DBG_PRINTF5("      t1_offset = %" PRIu64 " ns, t2_offset = %" PRIu64 " ns\n", a->t1_offset, a->t2_offset);
    }

    // Analysis
    else {

        uint64_t t2_interpolated_1 = 0, t2_interpolated_2 = 0;
        if (a->is_sync)
            t2_interpolated_1 = syncInterpolateT1(a, t2);

        // Normalize t1,t2 to first round start values
        // Avoid conversion loss, when using double precision floating point calculations later
        // The largest uint64_t value that can be converted to double without loss of precision is 2^53 (9,007,199,254,740,992).
        int64_t t1_norm = (int64_t)(t1 - a->t1_offset);
        int64_t t2_norm = (int64_t)(t2 - a->t2_offset);
        assert(t1_norm >= 0 && t2_norm >= 0);                                 // Should never happen
        assert((t2_norm < 0x20000000000000) && (t1_norm < 0x20000000000000)); // Should never happen (loss of precision after ≈ 104.25 days
        DBG_PRINTF5("      t1_norm = %" PRIi64 " ns, t2_norm = %" PRIi64 " ns\n", t1_norm, t2_norm);

        // Calculate momentary master offset
        a->offset_norm = t1_norm - t2_norm; // Positive means master is ahead
        DBG_PRINTF5("      offset_norm = %" PRIi64 " ns\n", a->offset_norm);

        // Calculate cycle time on local (c2) and master clock (c1)
        int64_t c1 = t1_norm - a->t1_norm; // time since last sync on master clock
        int64_t c2 = t2_norm - a->t2_norm; // time since last sync on local clock

        // Drift, drift of drift and jitter calculation by linear regression method
        // Drift of drift in ns/(s*s) (should be close to zero when temperature is stable )
        double linreg_slope = 0.0;
        double linreg_offset = 0.0;
        if (linreg_filter_calc(&a->linreg_filter, (double)t2_norm, (double)t1_norm, &linreg_slope, &linreg_offset)) {
            double linreg_drift = (linreg_slope - 1.0) * 1E9;
            a->linreg_drift_drift = linreg_drift - a->linreg_drift;
            a->linreg_drift = linreg_drift;
            a->linreg_offset = linreg_offset;
            // Jitter is the deviation from the linear regression line at t2 !!!!
            // While drift of drift is not zero, linreg_offset is not jitter!
            // Calculate average linreg offset to compensate offset for jitter calculation, avarage of jitter should be zero
            a->linreg_offset_avg = average_filter_calc(&a->linreg_offset_filter, linreg_offset);
            a->linreg_jitter = linreg_offset - a->linreg_offset_avg;
            a->linreg_jitter_avg = average_filter_calc(&a->linreg_jitter_filter, a->linreg_jitter);

            DBG_PRINTF5("  Sync %s: Linear regression results at t2 = %lld ns: \n", a->name, t2_norm);
            DBG_PRINTF5("    linreg drift = %g ns/s\n", a->linreg_drift);
            DBG_PRINTF5("    linreg drift_drift = %g ns/s2\n", a->linreg_drift_drift);
            DBG_PRINTF5("    linreg offset = %g ns\n", a->linreg_offset);
            DBG_PRINTF5("    linreg offset average = %g ns\n", a->linreg_offset_avg);
            DBG_PRINTF5("    linreg jitter = %g ns\n", a->linreg_jitter);
            DBG_PRINTF5("    linreg jitter average = %g ns\n", a->linreg_jitter_avg);

            if (a->cycle_count > params.min_sync_cycles) {
                a->is_sync = true;
                DBG_PRINTF4("Analyzer %s: Sync\n", a->name);
            } else {
                DBG_PRINTF4("Analyzer %s: Warming up\n", a->name);
            }
        }

        // Update linear regression output values
        a->offset = (int64_t)a->linreg_offset;  // Offset to current t1
        a->drift = a->linreg_drift;             // Drift in 1000*ppm
        a->drift_drift = a->linreg_drift_drift; // Drift of the drift in 1000*ppm/s*s
        a->jitter = a->linreg_jitter;           // Jitter in ns

        // Jitter analysis
        a->jitter_avg = average_filter_calc(&a->jitter_avg_filter, a->jitter);                           // Filter jitter
        a->jitter_rms = sqrt((double)average_filter_calc(&a->jitter_rms_filter, a->jitter * a->jitter)); // Filter jitter and calculate RMS

        DBG_PRINT5("    Jitter analysis:\n");
        DBG_PRINTF5("      jitter       = %g ns\n", a->jitter);
        DBG_PRINTF5("      jitter_avg   = %g ns\n", a->jitter_avg);
        DBG_PRINTF5("      jitter_rms   = %g ns\n", a->jitter_rms);

        DBG_PRINTF3("  %s: %s (%u) drift = %g ns/s, drift_drift = %g ns/s2, offset = %" PRIi64 " ns, jitter = %g ns\n", a->name, a->is_sync ? "SYNC" : "", a->cycle_count, a->drift,
                    a->drift_drift, a->offset, a->jitter);

        // uint64_t test1 = syncInterpolateT1(a, t2);
        // printf("    Interpolated t1 = %" PRIu64 " ns (diff = %" PRIi64 "), t2 = %" PRIu64 " ns \n", test1, (int64_t)test1 - (int64_t)t1, t2);

        // Save last normalized input values
        a->t1_norm = t1_norm;
        a->t2_norm = t2_norm;

        if (a->is_sync) {
            t2_interpolated_2 = syncInterpolateT1(a, t2);
            DBG_PRINTF4("  %s: Sync gap = %" PRIi64 " ns \n", a->name, (int64_t)(t2_interpolated_1 - t2_interpolated_2));
        }
    }
}

//---------------------------------------------------------------------------------------

// Reset the PTP client state
static void clientReset(tPtpClient *client) {
    assert(client != NULL);

    // Grandmaster info
    gPtpClient->gmValid = false;
    gPtpClient->gm_last_update_time = 0;

    // Init protocol state
    client->sync_local_time = 0;       // Local receive timestamp of SYNC
    client->sync_local_time_last = 0;  // Local receive timestamp of previous SYNC
    client->sync_cycle_time = 0;       // Master SYNC cycle time
    client->sync_master_time = 0;      // SYNC timestamp
    client->sync_correction = 0;       // SYNC correction
    client->sync_sequenceId = 0;       // SYNC sequence Id
    client->sync_steps = 0;            // SYNC steps removed
    client->flup_master_time = 0;      // FOLLOW_UP timestamp
    client->flup_correction = 0;       // FOLLOW_UP correction
    client->flup_sequenceId = 0;       // FOLLOW_UP sequence Id
    client->delay_req_system_time = 0; // System time when last DELAY_REQ was sent
    client->delay_req_local_time = 0;  // Local send timestamp of DELAY_REQ
    client->delay_req_sequenceId = 0;  // Sequence Id of last DELAY_REQ message sent
    client->delay_req_master_time = 0; // DELAY_RESP timestamp
    client->delay_resp_correction = 0; // DELAY_RESP correction
    client->delay_resp_sequenceId = 0; // Sequence Id of last DELAY_RESP message received
    client->delay_resp_logMessageInterval = 0;

    // Clock analyzer state
    syncInit(&client->a12, "a12", &params); // Init sync state for t1,t2 SYNC timestamps
    syncInit(&client->a34, "a34", &params); // Init SYNC state for t3,t4 DELAY timestamps
    syncInit(&client->asw, "asw", &params); // Init SYNC state for hw,sw DELAY_REQ timestamps

    // Path delay and master offset
    client->sync_sequenceId_last = 0;
    client->delay_resp_sequenceId_last = 0;
    client->delay_request_burst = 0;
    client->is_sync = false; // true if synchronized to grandmaster

    // client->drift = 0.0;        // Current drift in 1000*ppm
    // client->drift_drift = 0.0;  // Current drift of the drift in 1000*ppm/s*s

    client->raw_path_delay = 0; // Current path delay
    client->path_delay = 0;     // Path delay
    average_filter_init(&client->path_delay_avg_filter, params.path_delay_avg_filter_size);
    client->raw_master_offset = 0; // Current master offset
    linreg_filter_init(&client->master_offset_linreg_filter, params.linreg_filter_size);
    client->master_offset = 0; // Master offset

    client->gm_last_update_time = clockGet(); // Timeout from now
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
        if (!ptpSendDelayRequest(ptp, client->gm.domain, client->client_uuid, ++client->delay_req_sequenceId, &client->delay_req_local_time, &client->delay_req_system_time)) {
            DBG_PRINT_ERROR("PTP client: Failed to send DELAY_REQ\n");
            client->delay_req_sequenceId--;
            return false;
        }

        // Sync sw to hw time
        syncUpdate(&client->asw, client->delay_req_local_time, client->delay_req_system_time);
    }

    return false;
}

//---------------------------------------------------------------------------------------

// New t1,t2 pair available from SYNC message
static void clientSyncUpdate(tPtp *ptp, tPtpClient *client) {

    uint64_t t1 = (client->sync_steps == 1) ? client->sync_master_time : client->flup_master_time; // master clock
    uint64_t t2 = client->sync_local_time;                                                         // local clock
    uint64_t correction = client->sync_correction;                                                 // for master timestamp t1

    syncUpdate(&client->a12, t1 + correction, t2);
    // if (client->a12.is_sync) {
    //     client->drift = client->a12.drift;
    //     client->drift_drift = client->a12.drift_drift;
    // }
}

//---------------------------------------------------------------------------------------

// New t3,t4 pair available from DELAY_REQ/RESP messages
static void clientDelayUpdate(tPtpClient *client) {
    assert(client != NULL);

    uint64_t t1 = (client->sync_steps == 1) ? client->sync_master_time : client->flup_master_time; // master clock
    uint64_t t2 = client->sync_local_time;                                                         // local clock
    uint64_t t3 = client->delay_req_local_time;                                                    // local clock
    uint64_t t4 = client->delay_req_master_time;
    uint64_t correction = client->delay_resp_correction; // for master timestamp t4

    syncUpdate(&client->a34, t4 - correction, t3);
    // if (client->a34.is_sync) {
    //     client->drift = client->a34.drift;
    //     client->drift_drift = client->a34.drift_drift;
    // }

    // Update path_delay and master_offset only, when new data from SYNC and DELAY_REQ is available
    assert(t4 > t1);
    char ts[64];
    DBG_PRINTF5(" t1 = %s\n", clockGetString(ts, sizeof(ts), t1));
    DBG_PRINTF5(" t2 = %s\n", clockGetString(ts, sizeof(ts), t2));
    DBG_PRINTF5(" t3 = %s\n", clockGetString(ts, sizeof(ts), t3));
    DBG_PRINTF5(" t4 = %s\n", clockGetString(ts, sizeof(ts), t4));
    assert((t1 & 0x8000000000000000) == 0); // This will happen 29.12.2259, should be ok for now :-)
    assert((t2 & 0x8000000000000000) == 0);
    assert((t3 & 0x8000000000000000) == 0);
    assert((t4 & 0x8000000000000000) == 0);

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

        if (client->a34.is_sync) { // Drift available from DELAY timestamps

            // Drift correction for t4
            int64_t t4_drift_correction = (int64_t)(t4 - t1) * client->a34.drift / 1.0E9;

            // Calculate mean path delay
            int64_t t21 = ((int64_t)(t2 - t1) - client->sync_correction);
            int64_t t43 = ((int64_t)(t4 - t3) - client->delay_resp_correction - t4_drift_correction);
            client->raw_path_delay = (t21 + t43) / 2;
            assert(client->raw_path_delay >= 0);
            client->path_delay = average_filter_calc(&client->path_delay_avg_filter, client->raw_path_delay);

            // Calculate current master offset
            client->raw_master_offset = t21 - client->path_delay;

            // Calculate master offset linear regression
            double linreg_slope = 0.0;
            double linreg_offset = 0.0;
            if (linreg_filter_calc(&client->master_offset_linreg_filter, (double)t3, (double)client->raw_master_offset, &linreg_slope, &linreg_offset)) {
                DBG_PRINTF4(" Client master offset linear regression at t3 = %llu ns: \n", (unsigned long long)t3);
                DBG_PRINTF4("    linreg slope = %g\n", linreg_slope);
                DBG_PRINTF4("    linreg offset = %g ns\n", linreg_offset);
                client->master_offset = client->raw_master_offset + (int64_t)linreg_offset;
            } else {
                client->master_offset = client->raw_master_offset;
            }

            DBG_PRINTF4("t1=%" PRIu64 " ns, t2=%" PRIu64 " ns, t3=%" PRIu64 " ns, t4=%" PRIu64 " ns, t4_drift_correction=%" PRIi64 " ns\n", t1, t2, t3, t4, t4_drift_correction);
            DBG_PRINTF3("path delay = %" PRIi64 " ns, master offset = %" PRIi64 " ns, master_drift = %g ppm\n", client->path_delay, client->master_offset,
                        client->a34.drift / 1000.0);

            // Set synchronized flag if drift and sw->hw sync is available
            if (!client->is_sync && client->asw.is_sync && client->a34.is_sync) {
                client->is_sync = true;
                DBG_PRINTF3("PTP clock synchronized ! (path delay = %" PRIi64 " ns, master offset = %" PRIi64 " ns, drift = %g ppm, drift_drift =%g ppm/s, jitter_rms = %g ns)\n",
                            client->path_delay, client->master_offset, client->a34.drift / 1000.0, client->a34.drift_drift / 1000, client->a34.jitter_rms);
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

                client->gm_last_update_time = clockGet(); // Last master activity in system time

                if (ptp_msg->type == PTP_SYNC) {
                    assert(rx_timestamp != 0);
                    client->sync_local_time_last = client->sync_local_time;
                    client->sync_local_time = rx_timestamp;
                    client->sync_cycle_time = (client->sync_local_time_last == 0) ? 0 : (client->sync_local_time - client->sync_local_time_last);
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
                                client->delay_resp_sequenceId, (unsigned long long)client->delay_req_local_time, (unsigned long long)client->delay_req_master_time,
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
                client->gm_last_update_time = clockGet(); // Last master activity in system time
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
                client->gm_last_update_time = clockGet(); // Timeout from now
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
        uint64_t now = clockGet();
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

#define ENABLE_CHECKS

// Get grandmaster clock time in nanoseconds
// If not synchronized, return system clock time
// @@@@ TODO: Must be thread safe and lock free
uint64_t ptpClientGetGrandmasterClock() {

#ifdef ENABLE_CHECKS
    static uint64_t last_master_clock = 0; // @@@@ TODO: Not thread safe checks, disable for multi threaded use
    static uint64_t last_sys_clock = 0;
    static uint64_t last_client_clock = 0;
#endif

    // Interpolate the current grandmaster clock value from the configured local system clock
    // This requires the system clock is not discontinuous (e.g. NTP adjustments)
#if !defined(OPTION_CLOCK_EPOCH_ARB) || !defined(OPTION_CLOCK_TICKS_1NS)
#error "clock configuration not supported for ptpClientGetGrandmasterClock()"
#endif
    uint64_t sys_clock = clockGet();

    if (gPtpClient == NULL || !gPtpClient->is_sync) {
        DBG_PRINT3("ptpClientGetGrandmasterClock: Not synchronized to grandmaster\n");
        return sys_clock; // Not synchronized, return system clock
    }

    assert(gPtpClient->a34.is_sync);
    assert(gPtpClient->asw.is_sync);

    // mutexLock(&gPtpClient->mutex); // @@@@ TODO Lock free version needed for fast response

    // Convert system clock to local hardware clock
    uint64_t client_clock = syncInterpolateT1(&gPtpClient->asw, sys_clock);

// Check for large drift between local hardware clock and system clock
#ifdef ENABLE_CHECKS
    if (last_client_clock != 0 && last_sys_clock != 0) {
        int64_t clock_diff_client = (int64_t)(client_clock - last_client_clock);
        int64_t clock_diff_sys = (int64_t)(sys_clock - last_sys_clock);
        if (clock_diff_client == 0) {
            DBG_PRINT_WARNING("ptpClientGetGrandmasterClock: Local hardware clock not moving!\n");
            syncPrintState(&gPtpClient->asw);
        };
        if (clock_diff_client > 1000 && clock_diff_sys > 1000) {
            static const double MAX_PPM = 500; // 100 ppm
            double ppm = (double)llabs(clock_diff_client - clock_diff_sys) / clock_diff_sys * 1e6;
            if (ppm > MAX_PPM) {
                DBG_PRINTF3("ptpClientGetGrandmasterClock: Clock difference between local hw clock and system clock is %g ppm > 100 ppm! hw diff = %" PRIi64
                            " ns, sys diff = %" PRIi64 " ns\n",
                            ppm, clock_diff_client, clock_diff_sys);
            }
        }
    }
#endif

    // Convert local hardware clock to grandmaster clock
    uint64_t master_clock = syncInterpolateT1(&gPtpClient->a34, client_clock);
    master_clock += gPtpClient->path_delay;
    //+ gPtpClient->master_offset;

// Check for large drift between grandmaster clock and system clock
#ifdef ENABLE_CHECKS
    if (last_master_clock != 0 && last_sys_clock != 0) {
        int64_t clock_diff_master = (int64_t)(master_clock - last_master_clock);
        int64_t clock_diff_sys = (int64_t)(sys_clock - last_sys_clock);
        if (clock_diff_master > 1000 && clock_diff_sys > 1000) {
            static const double MAX_PPM = 500; // ppm
            double ppm = (double)llabs(clock_diff_master - clock_diff_sys) / clock_diff_sys * 1e6;
            if (ppm > MAX_PPM) {
                DBG_PRINTF3("ptpClientGetGrandmasterClock: Clock difference between grandmaster and system clock is %g ppm > 100 ppm! master diff = %" PRIi64
                            " ns, sys diff = %" PRIi64 " ns\n",
                            ppm, clock_diff_master, clock_diff_sys);
            }
        }
    }
#endif

    // Check for clocks going backwards
    if (master_clock < last_master_clock) {
        DBG_PRINTF_WARNING("ptpClientGetGrandmasterClock: Grandmaster clock went backwards! last = %" PRIu64 ", now = %" PRIu64 "\n", last_master_clock, master_clock);
    }
    if (sys_clock < last_sys_clock) {
        DBG_PRINTF_WARNING("ptpClientGetGrandmasterClock: System clock went backwards! last = %" PRIu64 ", now = %" PRIu64 "\n", last_sys_clock, sys_clock);
    }

    // mutexUnlock(&gPtpClient->mutex); // @@@@ TODO Lock free version needed for fast response

#ifdef ENABLE_CHECKS
    last_master_clock = master_clock;
    last_sys_clock = sys_clock;
    last_client_clock = client_clock;
#endif

    DBG_PRINTF3("Grandmaster clock = %" PRIu64 ", Client clock = %" PRIu64 ", System clock = %" PRIu64 "\n", master_clock, client_clock, sys_clock);
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
        printf("  Path delay: %" PRIi64 " ns\n", client->path_delay);
        printf("  Master offset: %" PRIi64 " ns\n", client->master_offset);
        printf("  Master drift: %g ppm\n", client->a34.drift / 1000.0);
        printf("  Master drift of drift: %g ppm/s\n", client->a34.drift_drift / 1000.0);
        printf("  Master jitter RMS: %g ns\n", client->a34.jitter_rms);
    } else {
        printf("PTP Client State: No grandmaster locked\n");
    }

    mutexUnlock(&client->mutex);
}

#endif
