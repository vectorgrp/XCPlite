/* udpserver.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __UDPSERVER_H__
#define __UDPSERVER_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _LINUX // Linux sockets

#define SOCKADDR_IN struct sockaddr_in
#define SOCKADDR struct sockaddr
#define SOCKET int

#define udpSendtoWouldBlock(r) (errno == EAGAIN)
#define udpRecvWouldBlock() (errno == EAGAIN)
#define udpGetLastError() errno

#define RECV_FLAGS 0 // Blocking receive (no MSG_DONTWAIT)
#define SENDTO_FLAGS 0 // Blocking transmit (no MSG_DONTWAIT)
#define SEND_RETRIES 10 // Retry when send CRM would block

#define LOCK() pthread_mutex_lock(&gXcpTlMutex)
#define UNLOCK() pthread_mutex_unlock(&gXcpTlMutex)
#define DESTROY() pthread_mutex_destroy(&gXcpTlMutex)

#endif 
#ifdef _WIN // Windows sockets or XL-API
#ifdef XCPSIM_ENABLE_XLAPI_V3
#define udpSendtoWouldBlock(r) (gOptionUseXLAPI ? (r==0) : (WSAGetLastError()==WSAEWOULDBLOCK))
#else
#define udpSendtoWouldBlock(r) ((WSAGetLastError()==WSAEWOULDBLOCK))
#endif
#define udpRecvWouldBlock() ((WSAGetLastError()) == WSAEWOULDBLOCK)
#define udpGetLastError() (WSAGetLastError())

#define RECV_FLAGS 0
#define SENDTO_FLAGS 0
#define SEND_RETRIES 10 // Retry when send CRM would block

#define LOCK() EnterCriticalSection(&gXcpTl.cs)
#define UNLOCK() LeaveCriticalSection(&gXcpTl.cs);
#define DESTROY()
    
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
    //-----------------------------------------------------------------------------------------------------
#ifdef XCPSIM_ENABLE_XLAPI_V3
    tUdpSockAddrXl addrXl;
#endif
} tUdpSockAddr;

typedef union {
    SOCKET sock; // Winsock or Linux
#ifdef XCPSIM_ENABLE_XLAPI_V3
    tUdpSockXl *sockXl; // XL-API
#endif
} tUdpSock;

typedef struct {
    
    tUdpSock Sock;
    tUdpSockAddr SlaveAddr;
    tUdpSockAddr SlaveMulticastAddr;
    unsigned int SlaveMTU;

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

#ifdef _WIN 
    CRITICAL_SECTION cs;
#endif

} tXcpTlData;



extern tXcpTlData gXcpTl;

extern int udpTlInit(unsigned char *slaveMac, unsigned char *slaveAddr, uint16_t slavePort, unsigned int slaveMTU);
extern void udpTlShutdown(void);

extern void udpTlWaitForReceiveEvent(unsigned int timeout_us);
extern int udpTlHandleXCPCommands(void);
extern int udpTlSendCrmPacket(const unsigned char* data, unsigned int n);

extern unsigned char* udpTlGetPacketBuffer(void** par, unsigned int size);
extern void udpTlCommitPacketBuffer(void* par);
extern void udpTlFlushTransmitQueue(void);
extern int udpTlHandleTransmitQueue(void);
extern void udpTlInitTransmitQueue(void);
extern void udpTlWaitForTransmitData(unsigned int timeout_us);


#ifdef __cplusplus
}
#endif

#endif
