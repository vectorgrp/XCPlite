// udpserver.h
// V1.0 23.9.2020

#ifndef __UDPSERVER_H
#define __UDPSERVER_H

#include "xcpLite.h"



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

extern void udpServerHandleTransmitQueue(void);
extern int udpServerHandleXCPCommands(void);

extern int udpServerInit(unsigned short serverPort, unsigned int socketTimeout);
extern int udpServerShutdown(void);



#endif
