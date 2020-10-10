#ifndef __UDPSERVER_H__
#define __UDPSERVER_H__

#include "xcpLite.h"

extern struct sockaddr_in gServerAddr;
extern struct sockaddr_in gClientAddr;
extern int gClientAddrValid;

extern int gSock;

extern unsigned short gLastCmdCtr;
extern unsigned short gLastResCtr;

extern pthread_mutex_t gMutex;


typedef struct {
    unsigned short dlc;               /* BYTE 1,2 lenght */
    unsigned short ctr;               /* BYTE 3,4 packet counter */
    unsigned char  data[kXcpMaxCTO];  /* BYTE[] data */
} XCP_CTO_MESSAGE;


typedef struct {
    unsigned short dlc;               /* BYTE 1,2 lenght */
    unsigned short ctr;               /* BYTE 3,4 packet counter */
    unsigned char  data[kXcpMaxDTO];  /* BYTE[] data */
} XCP_DTO_MESSAGE;

#define XCP_PACKET_HEADER_SIZE (2*sizeof(unsigned short))
#define DTO_BUFFER_LEN XCP_UDP_MTU


#ifdef DTO_SEND_QUEUE

typedef struct dto_buffer {

    unsigned int xcp_size;             // Number of overall bytes in XCP DTO messages
    unsigned int xcp_uncommited;       // Number of uncommited XCP DTO messages
#ifdef DTO_SEND_RAW
    struct iphdr ip;                   // IP header
    struct udphdr udp;                 // UDP header
#endif
    unsigned char xcp[DTO_BUFFER_LEN]; // Contains concatenated XCP_DTO_MESSAGES

} DTO_BUFFER;

#endif


extern int udpServerSendCrmPacket(const unsigned char* data, unsigned int n);
extern int udpServerInit(unsigned short serverPort, unsigned int socketTimeout);
extern int udpServerHandleXCPCommands(void);
extern int udpServerShutdown(void);

extern unsigned char* udpServerGetPacketBuffer(void** par, unsigned int size);
extern void udpServerCommitPacketBuffer(void* par);

#ifdef DTO_SEND_QUEUE
extern void udpServerFlushTransmitQueue(void); 
extern void udpServerHandleTransmitQueue(void);
#else
extern void udpServerFlushPacketBuffer(void);
#endif




#endif
