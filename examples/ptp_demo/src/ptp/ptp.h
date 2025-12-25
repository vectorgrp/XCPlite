#pragma once

/* ptp.h */

#define PTP_MODE_OBSERVER 0

extern bool ptpInit(uint8_t mode, uint8_t domain, uint8_t *bindAddr, char *interface, uint8_t debugLevel);
extern void ptpShutdown();
extern void ptpBackgroundTask(void);
extern void ptpReset();
