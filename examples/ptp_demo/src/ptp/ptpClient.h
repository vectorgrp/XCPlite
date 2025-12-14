#pragma once

/* ptpClient.h */

#include "ptp_cfg.h"

#ifdef OPTION_ENABLE_PTP_CLIENT

extern bool ptpClientInit(const uint8_t *uuid, uint8_t domain, uint8_t *addr, void (*ptpClientCallback)(uint64_t grandmaster_time, uint64_t local_time, int32_t drift));
extern void ptpClientShutdown();
extern tPtpMaster *ptpClientGetGrandmaster();

extern void ptpClientPrintInfo();
extern void ptpClientPrintMasterList();

#ifdef OPTION_ENABLE_PTP_XCP
extern void ptpClientCreateXcpEvents();
extern void ptpClientCreateA2lDescription();
#endif

#endif // OPTION_ENABLE_PTP_CLIENT