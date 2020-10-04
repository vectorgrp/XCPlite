// udpserver.h
// V1.0 23.9.2020

#ifndef __UDPSERVER_H
#define __UDPSERVER_H



#include "xcpLite.h"



extern unsigned short gLastCmdCtr;
extern unsigned short gLastResCtr;
extern int gSock;
extern struct sockaddr_in gServerAddr;
extern struct sockaddr_in gClientAddr;
extern socklen_t gClientAddrLen;
extern pthread_mutex_t gMutex;


#define USAGE_ERR			-1
#define BAD_PORT_NUM_ERR 	-2
#define SOCK_OPEN_ERR		-3
#define SOCK_BIND_ERR		-4
#define ACC_CONN_ERR		-5
#define SOCK_READ_ERR		-6
#define SOCK_WRITE_ERR		-7
#define	FILE_WRITE_ERR		-8
#define FILE_APP_ERR		-9
#define FILE_RCV_ERR		-10
#define UNKNOWN_HOST_ERR	-13


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

typedef struct dto_buffer {

    unsigned int xcp_size;             // Number of overall bytes in XCP DTO messages
    unsigned int xcp_uncommited;       // Number of uncommited XCP DTO messages
#ifdef DTO_SEND_RAW
    struct iphdr ip;                   // IP header
    struct udphdr udp;                 // UDP header
#endif
    unsigned char xcp[DTO_BUFFER_LEN]; // Contains concatenated XCP_DTO_MESSAGES

} DTO_BUFFER;



extern int udpServerSendCrmPacket(unsigned int n, const unsigned char* data);
extern unsigned char* udpServerGetPacketBuffer(unsigned int size, void **par);
extern void udpServerCommitPacketBuffer(void* par);

extern void udpServerFlushTransmitQueue(void); 
extern void udpServerHandleTransmitQueue(void);
extern int udpServerHandleXCPCommands(void);

extern int udpServerInit(unsigned short serverPort, unsigned int socketTimeout);
extern int udpServerShutdown(void);



#endif
