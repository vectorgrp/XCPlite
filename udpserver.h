// udpserver.h
// V1.0 23.9.2020

#ifndef __UDPSERVER_H
#define __UDPSERVER_H

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <assert.h>

#include <sys/types.h>
#include <errno.h>

#include <sys/time.h>

#include <sys/socket.h>

#include <netinet/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <arpa/inet.h>


#define DTO_SEND_QUEUE
#define DTO_SEND_RAW

#include "xcpLite.h"
#ifdef DTO_SEND_RAW
  #include "udpraw.h"
#endif



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



extern int udpServerSendCrmPacket(unsigned int n, const unsigned char* data);
extern unsigned char* udpServerGetPacketBuffer(unsigned int size, void **par);
extern void udpServerCommitPacketBuffer(void* par);

extern void udpServerFlushTransmitQueue(void); 
extern void udpServerHandleTransmitQueue(void);
extern int udpServerHandleXCPCommands(void);

extern int udpServerInit(unsigned short serverPort, unsigned int socketTimeout);
extern int udpServerShutdown(void);



#endif
