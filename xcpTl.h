/* xcpTl.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __XCPTL_H__
#define __XCPTL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xcptl_cfg.h"  // Transport layer configuration

#ifdef _LINUX // Linux sockets

    #define RECV_FLAGS 0 // Blocking receive (no MSG_DONTWAIT)
    #define SENDTO_FLAGS 0 // Blocking transmit (no MSG_DONTWAIT)
    #define SEND_RETRIES 10 // Retry when send CRM would block


#endif 

#ifdef _WIN // Windows sockets or XL-API

    #define RECV_FLAGS 0
    #define SENDTO_FLAGS 0
    #define SEND_RETRIES 10 // Retry when send CRM would block

#endif

typedef struct {
    uint16_t dlc;               /* BYTE 1,2 lenght */
    uint16_t ctr;               /* BYTE 3,4 packet counter */
    unsigned char  data[XCPTL_CTO_SIZE];  /* BYTE[] data */
} tXcpCtoMessage;

typedef struct {
    uint16_t dlc;               /* BYTE 1,2 lenght */
    uint16_t ctr;               /* BYTE 3,4 packet counter */
    unsigned char  data[XCPTL_DTO_SIZE];  /* BYTE[] data */
} tXcpDtoMessage;


typedef struct {
    unsigned int xcp_size;             // Number of overall bytes in XCP DTO messages
    unsigned int xcp_uncommited;       // Number of uncommited XCP DTO messages
    unsigned char xcp[XCPTL_SOCKET_JUMBO_MTU_SIZE]; // Contains concatenated messages
} tXcpDtoBuffer;


typedef union {
    SOCKADDR_IN addr;
} tUdpSockAddr;

typedef union {
    SOCKET sock; // Winsock or Linux
} tUdpSock;


extern int XcpTlInit(uint16_t slavePort, uint16_t slaveMTU);
extern void XcpTlShutdown();

extern int XcpTlGetSlaveMAC(uint8_t* mac);
extern const char* XcpTlGetSlaveIP();
extern uint16_t XcpTlGetSlavePort();

extern int XcpTlHandleCommands();
extern int XcpTlSendCrm(const uint8_t* data, unsigned int n);

extern uint8_t* XcpTlGetDtoBuffer(void** par, unsigned int size);
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
