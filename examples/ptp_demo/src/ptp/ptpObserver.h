#pragma once

/* ptpObserver.h */

extern bool ptpObserverInit(uint8_t domain, uint8_t *bind_addr);
void ptpObserverLoop(void);
extern void ptpObserverShutdown();
extern void ptpObserverPrintInfo();
extern void ptpObserverReset();

#ifdef OPTION_ENABLE_PTP_XCP
extern void ptpObserverCreateXcpEvents();
extern void ptpObserverCreateXcpParametes();
extern void ptpObserverCreateA2lDescription();
#endif
