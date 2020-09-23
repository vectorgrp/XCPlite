// udpserver.h
// V1.0 23.9.2020

#ifndef __UDPSERVER_H
#define __UDPSERVER_H

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

// udpserver.h

#define XCP_PACKET_DATA_MAX 250

typedef struct xcp_packet {

    unsigned short dlc;                        /* BYTE 1,2 lenght */
    unsigned short ctr;                        /* BYTE 3,4 packet counter */
    unsigned char  data[XCP_PACKET_DATA_MAX];  /* BYTE[] data */

} XCP_MESSAGE;

#define XCP_PACKET_HEADER_SIZE (2*sizeof(unsigned short))


extern int udpServerSendPacket(unsigned int n, const unsigned char* data);
extern int udpServerFlush(void);
extern int udpServerHandleXCPCommands(void);
extern int udpServerInit(unsigned short serverPort);
extern int udpServerShutdown(void);



#endif
