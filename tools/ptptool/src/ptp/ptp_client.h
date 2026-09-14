/* ptp_client.h */
#pragma once

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "platform.h" // from libxcplite for MUTEX
#include "sockets.h"  // from libxcplite for SOCKET_HANDLE
#include "util.h"     // from libxcplite for average and linear regression filters

#include "ptp.h" // for tPtp, OPTION_ENABLE_XCP

#ifdef OPTION_ENABLE_PTP_CLIENT

#include "ptpHdr.h" // PTP protocol message structures

//-------------------------------------------------------------------------------------------------------

// Master descriptor for clients
typedef struct ptp_client_master {
    uint8_t domain;
    uint8_t uuid[8];
    uint8_t addr[4];
    struct announce a; // Announce header from the announce protocol message of this master
} tPtpClientMaster;

// Client state
typedef struct ptp_client {

    MUTEX mutex;

    // Grandmaster info
    bool gmValid;                 // Grandmaster found and valid
    uint64_t gm_last_update_time; // Grandmasterlast update time in local clock time
    tPtpClientMaster gm;          // Grandmaster info

    // Protocol SYNC and FOLLOW_UP state
    uint64_t sync_client_time;      // Local receive timestamp of last SYNC
    uint64_t sync_client_time_last; // Local receive timestamp of previous SYNC
    uint64_t sync_master_time;
    uint32_t sync_correction;
    uint16_t sync_sequenceId;
    uint64_t sync_cycle_time;
    uint8_t sync_steps;
    uint64_t flup_master_time;
    uint32_t flup_correction;
    uint16_t flup_sequenceId;

    // Protocol DELAY_REQ and DELAY_RESP state
    uint8_t client_uuid[8];
    uint16_t delay_req_sequenceId;  // Sequence id for last DELAY_REQ
    uint64_t delay_req_client_time; // Local send timestamp of last DELAY_REQ
    uint64_t delay_req_system_time; // System time when last DELAY_REQ was sent
    uint64_t delay_req_master_time;
    uint32_t delay_resp_correction;
    uint16_t delay_resp_sequenceId;
    uint16_t delay_resp_logMessageInterval;
    uint16_t sync_sequenceId_last;       // Last processed SYNC sequence Id
    uint16_t delay_resp_sequenceId_last; // Last processed DELAY_RESP sequence Id
    uint16_t delay_request_burst;

    tClockSynchronizer s12;          // Synchronizer for master t1, client t1
    tClockSynchronizer s34;          // Synchronizer for master t4, client t3 (used for drift calculation and client to master interpolation)
    tClockSynchronizer ssw;          // Synchronizer for system cloc to client clock (NIC) (used for system to client interpolation)
    tMedianFilter path_delay_filter; // Median filter for path delay

    bool is_sync;           // true if synchronized to grandmaster
    int64_t raw_path_delay; // Current path delay
    int64_t path_delay;     // Filtered path delay
    int64_t master_offset;  // Current master offset

} tPtpClient;

extern tPtpClient *gPtpClient;

#ifdef __cplusplus
extern "C" {
#endif

// Get grandmaster synchronized clock
uint64_t ptpClientGetGrandmasterClock();

// PTP client background processing task
// Returns clock state
uint8_t ptpClientTask(tPtp *ptp);

// Handle incoming PTP message for client
bool ptpClientHandleFrame(tPtp *ptp, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t timestamp);

// Create and initialize PTP client instance
tPtpClient *ptpCreateClient(tPtp *ptp);

// Shutdown and free PTP client instance
void ptpClientShutdown(tPtp *ptp);

// Print PTP client state
void ptpClientPrintState(tPtp *ptp);

#ifdef __cplusplus
}
#endif

#endif
