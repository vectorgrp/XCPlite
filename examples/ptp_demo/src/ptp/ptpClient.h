#pragma once

/* ptpClient.h */

#include "ptpHdr.h"

// PTP master description
typedef struct {

    uint16_t index;
    uint8_t domain;
    uint8_t uuid[8];
    uint8_t addr[4];

    struct announce par;

    // Measurements
#ifdef OPTION_ENABLE_PTP_TEST
    uint64_t path_delay;
    int64_t path_asymmetry;
    int64_t offset;
    int64_t drift;
#endif

} tPtpMaster;

extern bool ptpClientInit(const uint8_t *uuid, uint8_t domain, uint8_t *addr, void (*ptpClientCallback)(uint64_t grandmaster_time, uint64_t local_time, int32_t drift));
extern void ptpClientShutdown();
extern tPtpMaster *ptpClientGetGrandmaster();

extern void ptpClientPrintInfo();
extern void ptpClientPrintMasterList();

#ifdef OPTION_ENABLE_PTP_TEST
extern void ptpClientCreateXcpEvents();
#if OPTION_ENABLE_A2L_GEN
extern void ptpClientCreateA2lDescription();
#endif
#endif
