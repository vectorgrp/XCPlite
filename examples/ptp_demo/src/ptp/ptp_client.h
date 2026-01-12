/* ptp_client.h */
#pragma once

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "filter.h"   // Average and linreg filter
#include "platform.h" // from xcplib for SOCKET, MUTEX, ...

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

// Clock analyzer state
typedef struct ptp_client_clock_analyzer {

    // PTP client timing analysis state, all values in nanoseconds and per second units
    uint32_t cycle_count;
    bool is_sync;                  // true if client has synchronized
    uint64_t t1, t2;               // Current corrected timestamp pair t1 - master clock, t2 - local clock
    int64_t offset_raw;            // Current offset t1-t2
    uint64_t t1_offset, t2_offset; // Normalization offsets
    int64_t t1_norm, t2_norm;      // Normalized timestamp pair t1_norm - master, t2_norm - local clock
    int64_t offset_norm;           // Normalized master offset t1_norm-t2_norm

    tLinregFilter linreg_filter; // Linear regression filter for drift and offset calculation
    double linreg_drift;         // Drift by linear regression in 1000*ppm
    double linreg_drift_drift;   // Drift of the drift by linear regression in 1000*ppm/s*s
    double linreg_offset;        // Offset by linear regression in ns
    tAverageFilter linreg_offset_filter;
    double linreg_offset_avg; // Average of linreg_offset in ns
    double linreg_jitter;     // Jitter by linear regression in ns
    tAverageFilter linreg_jitter_filter;
    double linreg_jitter_avg; // Average jitter by linear regression in ns

    // Results
    double drift;       // Drift  in 1000*ppm
    double drift_drift; // Drift of the drift in 1000*ppm/s*s
    double jitter;      // Jitter in ns

    double jitter_rms; // jitter root mean square
    double jitter_avg; // jitter average
    tAverageFilter jitter_rms_filter;
    tAverageFilter jitter_avg_filter;
} tPtpClientClockAnalyzer;

// Client state
typedef struct ptp_client {

    MUTEX mutex;

    // Grandmaster info
    bool gmValid;                 // Grandmaster found and valid
    uint64_t gm_last_update_time; // Grandmasterlast update time in local clock time
    tPtpClientMaster gm;          // Grandmaster info

    // Protocol SYNC and FOLLOW_UP state
    uint64_t sync_local_time;      // Local receive timestamp of last SYNC
    uint64_t sync_local_time_last; // Local receive timestamp of previous SYNC
    uint64_t sync_master_time;
    uint32_t sync_correction;
    uint16_t sync_sequenceId;
    uint64_t sync_cycle_time;
    uint8_t sync_steps;
    uint64_t flup_master_time;
    uint32_t flup_correction;
    uint16_t flup_sequenceId;

    tPtpClientClockAnalyzer a12; // Clock analyzer state for t1,t2 timestamp pairs
    tPtpClientClockAnalyzer a34; // Clock analyzer state for t3,t4 timestamp pairs

    // Active mode: Protocol DELAY_REQ and DELAY_RESP state
    uint8_t client_uuid[8];
    uint16_t delay_req_sequenceId;  // Sequence id for last DELAY_REQ
    uint64_t delay_req_local_time;  // Local send timestamp of last DELAY_REQ
    uint64_t delay_req_system_time; // System time when last DELAY_REQ was sent
    uint64_t delay_req_master_time;
    uint32_t delay_resp_correction;
    uint16_t delay_resp_sequenceId;
    uint16_t delay_resp_logMessageInterval;

    uint16_t sync_sequenceId_last;       // Last processed SYNC sequence Id
    uint16_t delay_resp_sequenceId_last; // Last processed DELAY_RESP sequence Id
    uint16_t delay_request_burst;

    // Current drift, path delay and master offset
    bool is_sync;          // true if synchronized to grandmaster
    double drift;          // Current drift in 1000*ppm
    double drift_drift;    // Current drift of the drift in 1000*ppm/s*s
    uint64_t path_delay;   // Current path delay
    int64_t master_offset; // Current master offset
} tPtpClient;

extern tPtpClient *gPtpClient;

#ifdef __cplusplus
extern "C" {
#endif

// PTP client background processing task
// @return clock state
uint8_t ptpClientTask(tPtp *ptp);
// Handle incoming PTP message for client
bool ptpClientHandleFrame(tPtp *ptp, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t timestamp);
// Create and initialize PTP client instance
tPtpClient *ptpCreateClient(tPtp *ptp);
// Shutdown and free PTP client instance
void ptpClientShutdown(tPtp *ptp);

#ifdef __cplusplus
}
#endif

#endif
