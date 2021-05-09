/* udpserver.h */

#ifndef __UDPSERVER_H__
#define __UDPSERVER_H__

#ifdef __cplusplus
extern "C" {
#endif

// Adjustments for 3 APIs: WINDOWS sockets, LINUX sockets, Vector XL-API
#ifndef _WIN // Linux sockets

#define udpRecvFrom recvfrom
#define udpSendTo sendto
#define SOCKET int
#define SOCKET_ADDR_IN struct sockaddr_in
#define SOCKET_ADDR struct sockaddr
#define RECV_FLAGS MSG_DONTWAIT
#define udpGetLastError() errno
#define SOCKET_WOULD_BLOCK EAGAIN

#else 
#ifdef XCP_ENABLE_XLAPI // Vector XL-API and buildin UDP

#include "udp.h"

#define SOCKET tUdpSock*
#define SOCKET_ADDR_IN tUdpSockAddr 
#define SOCKET_ADDR tUdpSockAddr
#define AF_INET 0
#define inet_ntop(a,b,c,d) sprintf_s(c,d,"%u.%u.%u.%u",*b[0],*b[1],*b[2],*b[3])
#define ntohs(x) (x)
#define RECV_FLAGS 0
#define SOCKET_WOULD_BLOCK 0

#else // Windows sockets

#define udpRecvFrom recvfrom
#define udpSendTo sendto
#define SOCKET SOCKET
#define SOCKET_ADDR_IN struct sockaddr_in
#define SOCKET_ADDR struct sockaddr
#define RECV_FLAGS 0
#define udpGetLastError() WSAGetLastError()
#define SOCKET_WOULD_BLOCK WSAEWOULDBLOCK

#endif
#endif


typedef struct {
    unsigned short LastCmdCtr;
    unsigned short LastResCtr;
    SOCKET Sock;
    SOCKET_ADDR_IN SlaveAddr;
    SOCKET_ADDR_IN MasterAddr;
    int MasterAddrValid;
} tXcpTlData;

typedef struct {
    unsigned short dlc;               /* BYTE 1,2 lenght */
    unsigned short ctr;               /* BYTE 3,4 packet counter */
    unsigned char  data[kXcpMaxCTO];  /* BYTE[] data */
} tXcpCtoMessage;

typedef struct {
    unsigned short dlc;               /* BYTE 1,2 lenght */
    unsigned short ctr;               /* BYTE 3,4 packet counter */
    unsigned char  data[kXcpMaxDTO];  /* BYTE[] data */
} tXcpDtoMessage;

#define XCP_MESSAGE_HEADER_SIZE (2*sizeof(unsigned short))  // Transport Layer Header
#define DTO_BUFFER_LEN kXcpMaxMTU

#ifdef DTO_SEND_QUEUE

typedef struct {

    unsigned int xcp_size;             // Number of overall bytes in XCP DTO messages
    unsigned int xcp_uncommited;       // Number of uncommited XCP DTO messages
    unsigned char xcp[DTO_BUFFER_LEN]; // Contains concatenated XCP_DTO_MESSAGES

} tXcpDtoBuffer;

#endif


extern int udpServerSendCrmPacket(const unsigned char* data, unsigned int n);
extern int udpServerInit();
extern void udpServerWaitForEvent(unsigned int timeout_us);
extern int udpServerHandleXCPCommands(void);
extern void udpServerShutdown(void);
extern unsigned char* udpServerGetPacketBuffer(void** par, unsigned int size);
extern void udpServerCommitPacketBuffer(void* par);
#ifdef DTO_SEND_QUEUE
extern void udpServerFlushTransmitQueue(void); 
extern void udpServerHandleTransmitQueue(void);
#else
extern void udpServerFlushPacketBuffer(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
