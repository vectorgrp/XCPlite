#pragma once

/* ptpmaster.h */

extern bool ptpMasterInit(const uint8_t *uuid, uint8_t ptpDomain, const uint8_t *bindAddr);
extern void ptpMasterShutdown();

extern void ptpMasterPrintInfo();
extern void ptpMasterPrintClientList();
extern void ptpMasterPrintMasterList();

extern void ptpMasterCreateA2lDescription();
