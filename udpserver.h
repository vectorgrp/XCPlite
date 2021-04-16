/* udpserver.h */

#ifndef __UDPSERVER_H__
#define __UDPSERVER_H__

#include "xcpLite.h"

#ifdef __cplusplus
extern "C" {
#endif

// Small differences windows and linux
#ifndef _WIN // Linux
#define SOCKET int
#define RECV_FLAGS MSG_DONTWAIT
#else // Windows
#define socklen_t int
#define RECV_FLAGS 0
#endif


typedef struct {
    SOCKET Sock;
    unsigned short LastCmdCtr;
    unsigned short LastResCtr;
    struct sockaddr_in ServerAddr;
    struct sockaddr_in ClientAddr;
    int ClientAddrValid;
} tXcpTlData;

extern tXcpTlData gXcpTl;


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
#ifdef DTO_SEND_RAW
    struct iphdr ip;                   // IP header
    struct udphdr udp;                 // UDP header
#endif
    unsigned char xcp[DTO_BUFFER_LEN]; // Contains concatenated XCP_DTO_MESSAGES

} tXcpDtoBuffer;

#endif


extern int udpServerSendCrmPacket(const unsigned char* data, unsigned int n);
extern int udpServerInit(unsigned short serverPort);
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
