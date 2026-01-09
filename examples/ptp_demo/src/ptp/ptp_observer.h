#pragma once

//-------------------------------------------------------------------------------------------------------
// PTP observer for master timing analysis

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "filter.h"   // for average filter
#include "platform.h" // from xcplib for SOCKET, socketSendTo, socketGetSendTime, ...

#include "ptp.h"    //
#include "ptpHdr.h" // PTP protocol message structures

#ifdef OPTION_ENABLE_XCP
#include <xcplib.h> // for tXcpEventId
#endif

// Timeout for grandmaster validity in nanoseconds (e.g. 5 seconds)
#define PTP_OBSERVER_GM_TIMEOUT_S 5

// Enable persistent observer allocation
// #define PTP_OBSERVER_LIST

// Offset and drift analysis options
#define OBSERVER_LINREG
// #define OBSERVER_SERVO

// Time analyser parameter structure
typedef struct {

    int32_t t1_correction;       // Correction to apply to t1 timestamps
    uint8_t delay_req_burst_len; // Number of DELAY_REQ messages to send after each SYNC in active mode
    uint8_t drift_filter_size;   // Size of the drift average filter
#ifdef OBSERVER_LINREG
    uint8_t linreg_filter_size;        // Size of the linear regression filter
    uint8_t linreg_offset_filter_size; // Size of the linear regression offset filter
    uint8_t linreg_jitter_filter_size; // Size of the linear regression jitter filter
#endif
#ifdef OBSERVER_SERVO
    double max_correction; // Maximum allowed servo correction per SYNC interval
    double servo_p_gain;   // Proportional gain (typically 0.1 - 0.5)
#endif
    uint8_t jitter_rms_filter_size; // Size of the jitter RMS average filter
    uint8_t jitter_avg_filter_size; // Size of the jitter average filter
} observer_parameters_t;

// Master descriptor for observers
typedef struct {
    uint8_t domain;
    uint8_t uuid[8];
    uint8_t addr[4];
    struct announce a; // Announce header from the announce protocol message of this master
} tPtpObserverMaster;

// Clock analyzer state
struct ptp_clock_analyzer {

    // PTP observer timing analysis state, all values in nanoseconds and per second units
    uint32_t cycle_count;
    bool is_sync;                  // true if observer has synchronized
    uint64_t t1, t2;               // Current corrected timestamp pair t1 - master clock, t2 - local clock
    int64_t offset_raw;            // Current offset t1-t2
    uint64_t t1_offset, t2_offset; // Normalization offsets
    int64_t t1_norm, t2_norm;      // Normalized timestamp pair t1_norm - master, t2_norm - local clock
    int64_t offset_norm;           // Normalized master offset t1_norm-t2_norm

    double drift_raw; // Current cycle drift in 1000*ppm
    tAverageFilter drift_filter;
    tAverageFilter drift_drift_filter;
#ifdef OBSERVER_LINREG
    tLinregFilter linreg_filter; // Linear regression filter for drift and offset calculation
    double linreg_drift;         // Drift by linear regression in 1000*ppm
    double linreg_drift_drift;   // Drift of the drift by linear regression in 1000*ppm/s*s
    double linreg_offset;        // Offset by linear regression in ns
    tAverageFilter linreg_offset_filter;
    double linreg_offset_avg; // Average of linreg_offset in ns
    double linreg_jitter;     // Jitter by linear regression in ns
    tAverageFilter linreg_jitter_filter;
    double linreg_jitter_avg; // Average jitter by linear regression in ns
#endif
#ifdef OBSERVER_SERVO
    double master_offset_compensation;       // normalized master_offset compensation servo offset
    double master_offset_detrended;          // normalized master_offset error (detrended offset_norm)
    double master_offset_detrended_filtered; // filtered normalized master_offset error (detrended offset_norm)
#endif

    // Results
    double drift;       // Drift  in 1000*ppm
    double drift_drift; // Drift of the drift in 1000*ppm/s*s
    double jitter;      // Jitter in ns

    double jitter_rms; // jitter root mean square
    double jitter_avg; // jitter average
    tAverageFilter jitter_rms_filter;
    tAverageFilter jitter_avg_filter;
};

typedef struct ptp_clock_analyzer tPtpClockAnalyzer;

// Observer states
struct ptp_observer {

    char name[32]; // Observer name

    // Filter master identification
    uint8_t target_domain;
    uint8_t target_uuid[8];
    uint8_t target_addr[4];

    bool activeMode;                // observer active mode (sends DELAY_REQ)
    uint32_t delay_req_burst_count; // Number of DELAY_REQ messages to send after each SYNC in active mode

    uint8_t log_level; // observer log level 0 .. 5

    MUTEX mutex;

    // Grandmaster info
    bool gmValid;                 // Grandmaster found and valid
    uint64_t gm_last_update_time; // Grandmasterlast update time in local clock time
    tPtpObserverMaster gm;        // Grandmaster info

    // Observer parameters
    observer_parameters_t *params; // Pointer to observer parameter structure, adjustable by XCP, shared for all observers

    // XCP event id
#ifdef OPTION_ENABLE_XCP
    tXcpEventId xcp_event; // XCP event triggered on observer SYNC/FOLLOW_UP update
#endif

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

    // Active mode: Protocol DELAY_REQ and DELAY_RESP state
    uint8_t client_uuid[8];
    uint16_t delay_req_sequenceId;  // Sequence id for last DELAY_REQ
    uint64_t delay_req_local_time;  // Local send timestamp of last DELAY_REQ
    uint64_t delay_req_system_time; // System time when last DELAY_REQ was sent
    uint64_t delay_req_master_time;
    uint32_t delay_resp_correction;
    uint16_t delay_resp_sequenceId;
    uint16_t delay_resp_logMessageInterval;

    tPtpClockAnalyzer a12; // Clock analyzer state for t1,t2 timestamp pairs
    tPtpClockAnalyzer a34; // Clock analyzer state for t3,t4 timestamp pairs

    // Calculated master drift and jitter from SYNC messages (passive mode)
    double master_drift;       // Filtered cycle drift over last n cycles
    double master_drift_drift; // Drift of the drift in 1000*ppm/s*s
    double master_jitter;      // jitter
    double master_jitter_rms;  // jitter root mean square
    double master_jitter_avg;  // jitter average

    int64_t offset_to[PTP_MAX_OBSERVERS]; // Offset and drift of this observers master clock compared to any other observer
    double drift_to[PTP_MAX_OBSERVERS];

    // Active mode: calculated path delay and master offset
    uint64_t path_delay;   // Current path delay
    int64_t master_offset; // Current master offset
};

typedef struct ptp_observer tPtpObserver;

#ifdef __cplusplus
extern "C" {
#endif

void observerPrintState(tPtp *ptp, tPtpObserver *obs);
bool observerTask(tPtp *ptp);
bool observerHandleFrame(tPtp *ptp, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t timestamp);
tPtpObserverHandle ptpCreateObserver(tPtp *ptp, const char *name, bool active_mode, uint8_t domain, const uint8_t *uuid, const uint8_t *addr);

bool ptpLoadObserverList(tPtp *ptp_handle, const char *filename, bool active_mode);
bool ptpSaveObserverList(tPtp *ptp_handle, const char *filename);

#ifdef __cplusplus
}
#endif
