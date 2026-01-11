/* ptp_master.h */
#pragma once

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "filter.h"   // for average filter
#include "platform.h" // from xcplib for SOCKET, socketSendTo, socketGetSendTime, ...

#include "ptp.h"    // for tPtp, OPTION_ENABLE_XCP
#include "ptpHdr.h" // PTP protocol message structures

#ifdef OPTION_ENABLE_XCP
#include <xcplib.h> // for tXcpEventId, tXcpCalSegIndex
#endif

//-------------------------------------------------------------------------------------------------------
// Options

// Enable master time drift, drift_drift, offset and jitter simulation
#define MASTER_TIME_ADJUST

// Maximum number of PTP clients
#define MAX_CLIENTS 16

// Default master parameters
#define SYNC_CYCLE_TIME_MS_DEFAULT 1000
#define ANNOUNCE_CYCLE_TIME_MS_DEFAULT 2000

//-------------------------------------------------------------------------------------------------------
// Announce message clock quality parameters

typedef struct {
    uint16_t utcOffset;     // PTP ANNOUNCE parameter
    uint8_t clockClass;     // PTP ANNOUNCE parameter
    uint8_t clockAccuraccy; // PTP ANNOUNCE parameter
    uint16_t clockVariance; // PTP ANNOUNCE parameter
    uint16_t stepsRemoved;  // PTP ANNOUNCE parameter
    uint8_t timeSource;     // PTP ANNOUNCE parameter
    uint8_t priority1;      // PTP ANNOUNCE parameter
    uint8_t priority2;      // PTP ANNOUNCE parameter
} announce_parameters_t;

// Default master clock quality parameters for ANNOUNCE message
// lower value takes precedence in BMCA
static const announce_parameters_t announce_params = {
    .utcOffset = 37,
    .clockClass = PTP_CLOCK_CLASS_PTP_PRIMARY,
    .clockAccuraccy = PTP_CLOCK_ACC_GPS,
    .clockVariance = 0,
    .stepsRemoved = 0,
    .timeSource = PTP_TIME_SOURCE_GPS,
    .priority1 = 0,
    .priority2 = 0,
};

//-------------------------------------------------------------------------------------------------------

// Master parameter structure
typedef struct master_params {
    uint32_t announceCycleTimeMs; // Announce message cycle time in ms
    uint32_t syncCycleTimeMs;     // SYNC message cycle time in ms
#ifdef MASTER_TIME_ADJUST
    int32_t drift;       // PTP master time drift in ns/s
    int32_t drift_drift; // PTP master time drift drift in ns/s2
    int32_t offset;      // PTP master time offset in ns
    int32_t jitter;      // PTP master time jitter in ns

#endif
} tMasterParams;

// PTP client descriptor for master
typedef struct ptp_master_client {
    uint8_t addr[4];       // IP addr
    uint8_t id[8];         // clock UUID
    uint64_t time;         // DELAY_REQ timestamp
    int64_t diff;          // DELAY_REQ current timestamp - delay req timestamp
    int64_t lastSeenTime;  // Last rx timestamp
    uint64_t cycle_time;   // Last cycle time in ns
    int32_t cycle_counter; // Cycle counter
    uint32_t corr;         // PTP correction
    uint8_t domain;        // PTP domain
} tPtpMasterClient;

// PTP master state
typedef struct ptp_master {

    bool active;

    // Master identification
    u_int8_t domain;
    uint8_t uuid[8];

    char name[32];

    uint64_t announceCycleTimer;
    uint64_t syncCycleTimer;
    uint64_t syncTxTimestamp;
    uint16_t sequenceIdAnnounce;
    uint16_t sequenceIdSync;

    // PTP client list
    uint16_t clientCount;
    tPtpMasterClient client[MAX_CLIENTS];

    // PTP master parameters
    const tMasterParams *params;

    // XCP event id
#ifdef OPTION_ENABLE_XCP
    tXcpCalSegIndex xcp_calseg; // master parameters calibration segment
    tXcpEventId xcp_event;      // on master SYNC
#endif

// Master drift, drift_drift, offset and jitter
#ifdef MASTER_TIME_ADJUST

    // Test time state
    int32_t testTimeDrift;
    int32_t testTimeCurrentDrift;    // Current drift including drift_drift
    int64_t testTimeSyncDriftOffset; // Current offset from drift accumulated on sync: testTime = originTime+testTimeSyncDriftOffset
    uint64_t testTimeLast;           // Current test time
    uint64_t testTimeLastSync;       // Original time of last sync
    MUTEX testTimeMutex;
#endif
} tPtpMaster;

#ifdef __cplusplus
extern "C" {
#endif

void masterPrintState(tPtp *ptp, int index);
bool masterTask(tPtp *ptp);
bool masterHandleFrame(tPtp *ptp, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t rxTimestamp);
tPtpMaster *ptpCreateMaster(tPtp *ptp, const char *name, uint8_t domain, const uint8_t *uuid);
void ptpMasterShutdown(tPtpMaster *master);

#ifdef __cplusplus
}
#endif