#pragma once

/* ptp.h */

#define gPtpDebugLevel 3
#define OPTION_ENABLE_PTP_TEST

extern int ptpInit(uint8_t *uuid, uint8_t ptpDomain, uint8_t *addr);
extern int ptpShutdown();
extern uint64_t ptpClockGet64();

#ifdef OPTION_ENABLE_PTP_TEST
extern void ptpCreateXcpEvents();
#if OPTION_ENABLE_A2L_GEN
extern void ptpCreateA2lDescription();
#endif
#endif

extern uint8_t ptpClockGetState();
extern bool ptpClockGetXcpGrandmasterInfo(uint8_t *uuid, uint8_t *epoch, uint8_t *stratumLevel);

extern bool ptpClockPrepareDaq();
