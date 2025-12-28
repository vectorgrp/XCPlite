#pragma once

/* ptp.h */

#define OPTION_ENABLE_XCP

#define PTP_MODE_OBSERVER 0x01
#define PTP_MODE_MASTER 0x02

#ifdef __cplusplus
extern "C" {
#endif

extern bool ptpInit(uint8_t mode, uint8_t domain, uint8_t *uuid, uint8_t *bindAddr, char *interface, uint8_t debugLevel);
extern bool ptpTask(void);
extern void ptpShutdown();
extern void ptpReset();
extern void ptpPrintStatusInfo();

#ifdef __cplusplus
} // extern "C"
#endif
