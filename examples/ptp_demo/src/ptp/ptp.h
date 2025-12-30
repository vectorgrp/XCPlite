#pragma once

/* ptp.h */

// #define OPTION_ENABLE_XCP

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

#ifdef __cplusplus
} // extern "C"
#endif
