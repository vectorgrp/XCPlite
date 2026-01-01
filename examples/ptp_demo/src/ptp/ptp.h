#pragma once

/* ptp.h */

#define OPTION_ENABLE_XCP

//-------------------------------------------------------------------------------------------------------
// PTP state

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "filter.h"   // for average filter
#include "platform.h" // from xcplib for SOCKET, socketSendTo, socketGetSendTime, ...

#include "ptp.h"
#include "ptpHdr.h" // PTP protocol message structures

// Forward declarations
struct ptp_observer;
struct ptp_master;

#define PTP_MAGIC 0x50545021 // "PTP!"

#define PTP_MAX_MASTERS 16
#define PTP_MAX_OBSERVERS 16

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

    uint8_t log_level;
    bool auto_observer_enabled;

    int master_count;
    struct ptp_master *master_list[PTP_MAX_MASTERS];

    int observer_count;
    struct ptp_observer *observer_list[PTP_MAX_OBSERVERS];
};
typedef struct ptp tPtp;

#ifdef __cplusplus
extern "C" {
#endif

typedef void *tPtpInterfaceHandle;
typedef void *tPtpMasterHandle;
typedef void *tPtpObserverHandle;

extern tPtpInterfaceHandle ptpCreateInterface(const uint8_t *ifname, const char *if_name, uint8_t debugLevel);
extern tPtpObserverHandle ptpCreateObserver(const char *instance_name, tPtpInterfaceHandle interface, uint8_t domain, const uint8_t *uuid, const uint8_t *addr);
extern tPtpMasterHandle ptpCreateMaster(const char *instance_name, tPtpInterfaceHandle interface, uint8_t domain, const uint8_t *uuid);

extern bool ptpTask(tPtpInterfaceHandle ptp);
extern void ptpShutdown(tPtpInterfaceHandle ptp);

extern bool ptpEnableAutoObserver(tPtpInterfaceHandle interface);
extern void ptpPrintState(tPtpInterfaceHandle ptp_handle);

#ifdef __cplusplus
} // extern "C"
#endif
