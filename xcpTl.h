/* xcpTl.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __XCPTL_H__
#define __XCPTL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xcptl_cfg.h"  // Transport layer configuration

typedef struct {
    uint16_t dlc;               /* BYTE 1,2 lenght */
    uint16_t ctr;               /* BYTE 3,4 packet counter */
    uint8_t data[XCPTL_CTO_SIZE];  /* BYTE[] data */
} tXcpCtoMessage;

typedef struct {
    uint16_t dlc;               /* BYTE 1,2 lenght */
    uint16_t ctr;               /* BYTE 3,4 packet counter */
    uint8_t data[XCPTL_DTO_SIZE];  /* BYTE[] data */
} tXcpDtoMessage;


typedef struct {
    uint16_t xcp_size;             // Number of overall bytes in XCP DTO messages
    uint16_t xcp_uncommited;       // Number of uncommited XCP DTO messages
    uint8_t xcp[XCPTL_SOCKET_JUMBO_MTU_SIZE]; // Contains concatenated messages
} tXcpDtoBuffer;



extern int XcpTlInit(uint8_t* slaveAddr, uint16_t slavePort, uint16_t slaveMTU);
extern void XcpTlShutdown();

extern const char* XcpTlGetSlaveAddrString();
extern uint16_t XcpTlGetSlavePort();
extern int XcpTlGetSlaveMAC(uint8_t* mac);

extern int XcpTlHandleCommands();
extern int XcpTlSendCrm(const uint8_t* data, uint16_t n);

extern uint8_t* XcpTlGetDtoBuffer(void** par, uint16_t size);
extern void XcpTlCommitDtoBuffer(void* par);
extern void XcpTlFlushTransmitQueue();
extern int XcpTlHandleTransmitQueue();
extern void XcpTlInitTransmitQueue();
extern void XcpTlWaitForTransmitData(unsigned int timeout_us);

/* Set cluster id */
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
extern void XcpTlSetClusterId(uint16_t clusterId);
#endif

// Test instrumentation
#ifdef APP_ENABLE_A2L_GEN
void XcpTlCreateA2lDescription();
#endif
extern uint64_t XcpTlGetBytesWritten();


#ifdef __cplusplus
}
#endif

#endif
