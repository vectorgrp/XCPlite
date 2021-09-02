/* xcpTl.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __UDPSERVER_H__
#define __UDPSERVER_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _LINUX // Linux sockets

    #define RECV_FLAGS 0 // Blocking receive (no MSG_DONTWAIT)
    #define SENDTO_FLAGS 0 // Blocking transmit (no MSG_DONTWAIT)
    #define SEND_RETRIES 10 // Retry when send CRM would block


#endif 

#ifdef _WIN // Windows sockets or XL-API

    #ifdef APP_ENABLE_XLAPI_V3
      #undef udpSendtoWouldBlock
      #define udpSendtoWouldBlock(r) (gOptionUseXLAPI ? (r==0) : (WSAGetLastError()==WSAEWOULDBLOCK))
    #endif

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
#ifdef APP_ENABLE_XLAPI_V3
    tUdpSockAddrXl addrXl;
#endif
} tUdpSockAddr;

typedef union {
    SOCKET sock; // Winsock or Linux
#ifdef APP_ENABLE_XLAPI_V3
    tUdpSockXl *sockXl; // XL-API
#endif
} tUdpSock;

typedef struct {
    
    tUdpSock Sock;
    unsigned int SlaveMTU;
    tUdpSockAddr SlaveAddr;
    uint8_t SlaveUUID[8];
    tUdpSockAddr MasterAddr;
    int MasterAddrValid;

    // Transmit queue 
    tXcpDtoBuffer dto_queue[XCPTL_DTO_QUEUE_SIZE];
    unsigned int dto_queue_rp; // rp = read index
    unsigned int dto_queue_len; // rp+len = write index (the next free entry), len=0 ist empty, len=XCPTL_DTO_QUEUE_SIZE is full
    tXcpDtoBuffer* dto_buffer_ptr; // current incomplete or not fully commited entry

    // CTO command transfer object counters (CRM,CRO)
    uint16_t LastCroCtr; // Last CRO command receive object message packet counter received
    uint16_t CrmCtr; // next CRM command response message packet counter

    // DTO data transfer object counter (DAQ,STIM)
    uint16_t DtoCtr; // next DAQ DTO data transmit message packet counter 

    // Multicast
#ifdef APP_ENABLE_MULTICAST
    tXcpThread MulticastThreadHandle;
    SOCKET MulticastSock;
    // XL-API
    #ifdef APP_ENABLE_XLAPI_V3
        tUdpSockAddr MulticastAddrXl;
    #endif
#endif 

    MUTEX Mutex_Queue;
    MUTEX Mutex_Send;
       
} tXcpTlData;

extern tXcpTlData gXcpTl;

extern int networkInit();
extern void networkShutdown();

extern int udpTlInit(uint8_t*slaveAddr, uint16_t slavePort, uint16_t slaveMTU);
extern void udpTlShutdown();

extern int udpTlHandleCommands();
extern int udpTlSendCrmPacket(const uint8_t* data, unsigned int n);

extern uint8_t* udpTlGetPacketBuffer(void** par, unsigned int size);
extern void udpTlCommitPacketBuffer(void* par);
extern void udpTlFlushTransmitQueue();
extern int udpTlHandleTransmitQueue();
extern void udpTlInitTransmitQueue();
extern void udpTlWaitForTransmitData(unsigned int timeout_us);

#ifdef __cplusplus
}
#endif

#endif
