/* ptp.h */
#pragma once

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "filter.h"   // for average filter
#include "platform.h" // from libxcplite for SOCKET_HANDLE, THREAD, ...
#include "ptpHdr.h"   // for struct ptphdr

//-------------------------------------------------------------------------------------------------------
// Options

// Enable buildin PTP client clock
// #define OPTION_ENABLE_PTP_CLIENT

// Enable buildin PTP master
#define OPTION_ENABLE_PTP_MASTER

// Enable buildin PTP observer mode (master analyzer)
#define OPTION_ENABLE_PTP_OBSERVER

// Enable XCP instrumentation for observer or master measurements
#define OPTION_ENABLE_XCP

//-------------------------------------------------------------------------------------------------------

#ifndef OPTION_ENABLE_XCP
#define CLOCK_STATE_SYNCH_IN_PROGRESS (0)
#define CLOCK_STATE_SYNCH (1)
#define CLOCK_STATE_FREE_RUNNING (7)
#define CLOCK_STATE_GRANDMASTER_STATE_SYNCH (1 << 3) // @@@@ not used yet
#define CLOCK_STRATUM_LEVEL_UNKNOWN 255
#define CLOCK_STRATUM_LEVEL_ARB 16 // unsychronized
#define CLOCK_STRATUM_LEVEL_UTC 0  // Atomic reference clock
#define CLOCK_EPOCH_TAI 0          // Atomic monotonic time since 1.1.1970 (TAI)
#define CLOCK_EPOCH_UTC 1          // Universal Coordinated Time (with leap seconds) since 1.1.1970 (UTC)
#define CLOCK_EPOCH_ARB 2          // Arbitrary (epoch unknown)
#endif

#ifdef OPTION_ENABLE_PTP_OBSERVER
#define PTP_MAX_OBSERVERS 16
struct ptp_observer;
#endif
#ifdef OPTION_ENABLE_PTP_MASTER
#define PTP_MAX_MASTERS 16
struct ptp_master;
#endif

#define PTP_MAGIC 0x50545021 // "PTP!"

struct ptp {

    uint32_t magic; // Magic number for validation

    // Sockets and communication
    uint8_t ifaddr[4]; // local addr
    char ifname[32];   // network interface name
    uint8_t maddr[4];  // multicast addr
    THREAD threadHandle320;
    THREAD threadHandle319;
    SOCKET_HANDLE sock320;
    SOCKET_HANDLE sock319;
    MUTEX mutex;

#ifdef OPTION_ENABLE_PTP_MASTER
    int master_count;
    struct ptp_master *master_list[PTP_MAX_MASTERS];
#endif

#ifdef OPTION_ENABLE_PTP_OBSERVER
    bool auto_observer;
    bool auto_observer_active_mode;
    uint64_t local_clock_offset;
    int observer_count;
    struct ptp_observer *observer_list[PTP_MAX_OBSERVERS];
#endif
};
typedef struct ptp tPtp;

#ifdef __cplusplus
extern "C" {
#endif

tPtp *ptpCreateInterface(const uint8_t *ifaddr, const char *ifname, bool sync_phc);
bool ptpGenerateLocalClockUUID(char *ifname, uint8_t *uuid);

uint8_t ptpTask(tPtp *ptp);
void ptpShutdown(tPtp *ptp);
void ptpPrintState(tPtp *ptp_handle);

bool ptpSendAnnounce(tPtp *ptp, uint8_t domain, const uint8_t *master_uuid, uint16_t sequenceId);
bool ptpSendSync(tPtp *ptp, uint8_t domain, const uint8_t *master_uuid, uint64_t *sync_txLocalTime, uint16_t sequenceId);
bool ptpSendSyncFollowUp(tPtp *ptp, uint8_t domain, const uint8_t *master_uuid, uint64_t sync_txLocalTime, uint16_t sequenceId);
bool ptpSendDelayResponse(tPtp *ptp, uint8_t domain, const uint8_t *master_uuid, struct ptphdr *delay_req, uint64_t delay_req_rxLocalTime);

bool ptpSendDelayRequest(tPtp *ptp, uint8_t domain, const uint8_t *client_uuid, uint16_t sequenceId, uint64_t *txLocalTime, uint64_t *txSystemTime);

#ifdef __cplusplus
} // extern "C"
#endif
