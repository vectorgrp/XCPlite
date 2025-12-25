#pragma once

/* ptpMaster.h */

#include <stdint.h>

#include "ptp_cfg.h"

#include "ptpHdr.h" // for struct announce from the PTP protocol

// --------------------------------------------------------------------------------------------------
// PTP master descriptor

typedef struct {

    uint16_t index;
    uint8_t domain;
    uint8_t uuid[8];
    uint8_t addr[4];

    struct announce a; // Announce header from the announce protocol message of this master

    // Measurements
#ifdef OPTION_ENABLE_PTP_TEST
    uint64_t path_delay;
    int64_t path_asymmetry;
    int64_t offset;
    int64_t drift;
#endif

} tPtpMaster;

// -------------------------------------------------------------------------------------------------

// PTP master implementation
#ifdef OPTION_ENABLE_PTP_MASTER

extern bool ptpMasterInit(const uint8_t *uuid, uint8_t ptpDomain, const uint8_t *bindAddr);
extern void ptpMasterShutdown();

extern void ptpMasterPrintInfo();
extern void ptpMasterPrintClientList();
extern void ptpMasterPrintMasterList();

#ifdef OPTION_ENABLE_PTP_XCP
extern void ptpMasterCreateA2lDescription();
#endif

#endif // OPTION_ENABLE_PTP_MASTER
