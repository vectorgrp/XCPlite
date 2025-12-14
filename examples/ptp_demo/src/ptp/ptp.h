#pragma once

/* ptp.h */

extern int ptpInit(uint8_t *uuid, uint8_t ptpDomain, uint8_t *addr);
extern int ptpShutdown();
extern uint64_t ptpClockGet64();
extern uint8_t ptpClockGetState();
extern bool ptpClockGetXcpGrandmasterInfo(uint8_t *uuid, uint8_t *epoch, uint8_t *stratumLevel);
extern bool ptpClockPrepareDaq();

// Test
// Use XCP to measure PTP events and variables
#ifdef OPTION_ENABLE_PTP_XCP
extern void ptpCreateXcpEvents();
extern void ptpCreateA2lDescription();
#endif
