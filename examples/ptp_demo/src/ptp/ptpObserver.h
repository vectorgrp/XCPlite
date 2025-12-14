#pragma once

/* ptpObserver.h */

#include "ptp_cfg.h"

#ifdef OPTION_ENABLE_PTP_OBSERVER

#include "ptpMaster.h" // for tPtpMaster

extern bool ptpObserverInit(uint8_t domain, uint8_t *bind_addr);
extern void ptpObserverShutdown();

extern tPtpMaster *ptpObserverGetGrandmaster();
extern void ptpObserverPrintInfo();

#ifdef OPTION_ENABLE_PTP_XCP
extern void ptpObserverCreateXcpEvents();
extern void ptpObserverCreateA2lDescription();
#endif

#endif // OPTION_ENABLE_PTP_CLIENT