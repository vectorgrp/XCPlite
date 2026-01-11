/* ptp.h */
#pragma once

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "filter.h"   // for average filter
#include "platform.h" // from xcplib for SOCKET, THREAD, ...
#include "ptpHdr.h"   // for struct ptphdr

//-------------------------------------------------------------------------------------------------------
// Options

// Enable PTP master mode
#define OPTION_ENABLE_PTP_MASTER

// Enable PTP observer mode (master analyzer)
#define OPTION_ENABLE_PTP_OBSERVER

// Enable XCP for observer or master measurements
#define OPTION_ENABLE_XCP

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
    uint8_t if_addr[4]; // local addr
    char if_name[32];   // network interface name
    uint8_t maddr[4];   // multicast addr
    THREAD threadHandle320;
    THREAD threadHandle319;
    SOCKET sock320;
    SOCKET sock319;
    MUTEX mutex;

#ifdef OPTION_ENABLE_PTP_MASTER
    int master_count;
    struct ptp_master *master_list[PTP_MAX_MASTERS];
#endif

#ifdef OPTION_ENABLE_PTP_OBSERVER
    bool auto_observer;
    bool auto_observer_active_mode;
    int observer_count;
    struct ptp_observer *observer_list[PTP_MAX_OBSERVERS];
#endif
};
typedef struct ptp tPtp;

#ifdef __cplusplus
extern "C" {
#endif

tPtp *ptpCreateInterface(const uint8_t *ifname, const char *if_name, bool sync_phc);

#ifdef OPTION_ENABLE_PTP_OBSERVER
struct ptp_observer *ptpCreateObserver(tPtp *interface, const char *name, bool active_mode, uint8_t domain, const uint8_t *uuid, const uint8_t *addr);
#endif

#ifdef OPTION_ENABLE_PTP_MASTER
struct ptp_master *ptpCreateMaster(tPtp *interface, const char *name, uint8_t domain, const uint8_t *uuid);
#endif

bool ptpTask(tPtp *ptp);
void ptpShutdown(tPtp *ptp);

bool ptpEnableAutoObserver(tPtp *interface, bool active_mode);
void ptpPrintState(tPtp *ptp_handle);

bool ptpSendAnnounce(tPtp *ptp, uint8_t domain, const uint8_t *master_uuid, uint16_t sequenceId);
bool ptpSendSync(tPtp *ptp, uint8_t domain, const uint8_t *master_uuid, uint64_t *sync_txTimestamp, uint16_t sequenceId);
bool ptpSendSyncFollowUp(tPtp *ptp, uint8_t domain, const uint8_t *master_uuid, uint64_t sync_txTimestamp, uint16_t sequenceId);
bool ptpSendDelayResponse(tPtp *ptp, uint8_t domain, const uint8_t *master_uuid, struct ptphdr *client_delay_req, uint64_t client_delay_req_rxTimestamp);

bool ptpSendDelayRequest(tPtp *ptp, uint8_t domain, const uint8_t *client_uuid, uint16_t sequenceId, uint64_t *txTimestamp);

#ifdef __cplusplus
} // extern "C"
#endif
