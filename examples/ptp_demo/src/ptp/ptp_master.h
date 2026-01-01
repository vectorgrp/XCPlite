#pragma once

//-------------------------------------------------------------------------------------------------------
// PTP master

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "filter.h"   // for average filter
#include "platform.h" // from xcplib for SOCKET, socketSendTo, socketGetSendTime, ...

#include "ptp.h"
#include "ptpHdr.h" // PTP protocol message structures
#include "ptp_master.h"

#ifdef OPTION_ENABLE_XCP
#include <xcplib.h> // for tXcpEventId
#endif

#define MAX_CLIENTS 16
#define SYNC_CYCLE_TIME_MS_DEFAULT 1000
#define ANNOUNCE_CYCLE_TIME_MS_DEFAULT 2000

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

// Master parameter structure
typedef struct master_params {
    uint32_t announceCycleTimeMs; // Announce message cycle time in ms
    uint32_t syncCycleTimeMs;     // SYNC message cycle time in ms
} master_parameters_t;

// PTP client descriptor for master
struct ptp_client {
    uint8_t addr[4];       // IP addr
    uint8_t id[8];         // clock UUID
    uint64_t time;         // DELAY_REQ timestamp
    int64_t diff;          // DELAY_REQ current timestamp - delay req timestamp
    int64_t lastSeenTime;  // Last rx timestamp
    uint64_t cycle_time;   // Last cycle time in ns
    int32_t cycle_counter; // Cycle counter
    uint32_t corr;         // PTP correction
    uint8_t domain;        // PTP domain
};
typedef struct ptp_client tPtpClient;

// PTP master state
struct ptp_master {

    bool active;

    // Master identification
    u_int8_t domain;
    uint8_t uuid[8];

    uint8_t log_level;
    char name[32];

    uint64_t announceCycleTimer;
    uint64_t syncCycleTimer;
    uint64_t syncTxTimestamp;
    uint16_t sequenceIdAnnounce;
    uint16_t sequenceIdSync;

    // PTP client list
    uint16_t clientCount;
    tPtpClient client[MAX_CLIENTS];

    // PTP master parameters
    const master_parameters_t *params;

    // XCP event id
#ifdef OPTION_ENABLE_XCP
    tXcpEventId xcp_event; // on master SYNC
#endif
};

typedef struct ptp_master tPtpMaster;

void masterPrintState(tPtp *ptp, tPtpMaster *master);
bool masterTask(tPtp *ptp, tPtpMaster *master);
bool masterHandleFrame(tPtp *ptp, int n, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t rxTimestamp);
tPtpMasterHandle ptpCreateMaster(const char *name, tPtpInterfaceHandle ptp_handle, uint8_t domain, const uint8_t *uuid);