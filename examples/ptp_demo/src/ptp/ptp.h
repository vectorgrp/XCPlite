#pragma once

/* ptp.h */

#define OPTION_ENABLE_XCP

#define PTP_MODE_OBSERVER 0x01
#define PTP_MODE_MASTER 0x02

#ifdef __cplusplus
extern "C" {
#endif

typedef void *tPtpHandle;

extern tPtpHandle ptpInit(const char *instance_name, uint8_t mode, uint8_t domain, uint8_t *uuid, uint8_t *bindAddr, char *interface, uint8_t debugLevel);
extern bool ptpTask(tPtpHandle ptp);
extern void ptpShutdown(tPtpHandle ptp);
extern void ptpReset(tPtpHandle ptp);
extern void ptpPrintStatusInfo(tPtpHandle ptp);

#ifdef __cplusplus
} // extern "C"
#endif
