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

// Observer parameter structure
typedef struct {
    uint8_t reset;             // Reset PTP observer state
    int32_t t1_correction;     // Correction to apply to t1 timestamps
    uint8_t drift_filter_size; // Size of the drift average filter
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

// Observer state
struct ptp_observer {

    char name[32];

    // Filter master identification
    uint8_t domain;
    uint8_t uuid[8];
    uint8_t addr[4];

    uint8_t log_level;

    // Grandmaster info
    bool gmValid; // locked onto a valid grandmaster
    tPtpObserverMaster gm;

    // Protocol SYNC and FOLLOW_UP state
    uint64_t sync_local_time;
    uint64_t sync_master_time;
    uint32_t sync_correction;
    uint16_t sync_sequenceId;
    uint64_t sync_cycle_time;
    uint8_t sync_steps;
    uint64_t flup_master_time;
    uint32_t flup_correction;
    uint16_t flup_sequenceId;

    // PTP observer timing analysis state, all values in nanoseconds and per second units
    uint32_t cycle_count;
    bool master_sync;              // true if observer has synchronized to master
    uint64_t t1, t2;               // Current corrected timestamp pair t1 - master, t2 - local clock
    int64_t master_offset_raw;     // Current master offset t1-t2
    uint64_t t1_offset, t2_offset; // Normalization offsets
    int64_t t1_norm, t2_norm;      // Normalized timestamp pair t1_norm - master, t2_norm - local clock
    int64_t master_offset_norm;    // Normalized master offset t1_norm-t2_norm

    double master_drift_raw; // Current cycle drift in 1000*ppm
    tAverageFilter master_drift_filter;
    tAverageFilter master_drift_drift_filter;

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
    double master_offset_detrended;          // normalized master_offset error (detrended master_offset_norm)
    double master_offset_detrended_filtered; // filtered normalized master_offset error (detrended master_offset_norm)
#endif

    double master_drift;       // Filtered cycle drift over last n cycles
    double master_drift_drift; // Drift of the drift in 1000*ppm/s*s

    double master_jitter;     // jitter
    double master_jitter_rms; // jitter root mean square
    double master_jitter_avg; // jitter average
    tAverageFilter master_jitter_rms_filter;
    tAverageFilter master_jitter_avg_filter;

    // Offset and drift of this observers master clock compared to any other observer
    int64_t offset_to[PTP_MAX_OBSERVERS];
    double drift_to[PTP_MAX_OBSERVERS];

    // Observer parameters
    const observer_parameters_t *params;

    // XCP event id
#ifdef OPTION_ENABLE_XCP
    tXcpEventId xcp_event; // on observer SYNC/FOLLOW_UP update
#endif
};

typedef struct ptp_observer tPtpObserver;

void observerPrintState(tPtp *ptp, tPtpObserver *obs);
bool observerTask(tPtp *ptp, tPtpObserver *observer);
bool observerHandleFrame(tPtp *ptp, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t timestamp);
tPtpObserverHandle ptpCreateObserver(const char *name, tPtpInterfaceHandle ptp_handle, uint8_t domain, const uint8_t *uuid, const uint8_t *addr);