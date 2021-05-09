/* udp.h */


#ifndef __UDP_H__
#define __UDP_H__

#ifdef XCP_ENABLE_XLAPI

#ifdef __cplusplus
extern "C" {
#endif

#include "xcp_cfg.h"


typedef struct {
    unsigned char sin_mac[6];
    unsigned char sin_addr[4];
    unsigned short sin_port;
} tUdpSockAddr;

typedef struct {
   XLnetworkHandle networkHandle; // Network handle
   XLethPortHandle portHandle; // VP handle
   tUdpSockAddr localAddr; // Local socket address 
} tUdpSock;





int udpRecvFrom(tUdpSock *socket, unsigned char *data, unsigned int size, int mode, tUdpSockAddr *socket_addr, int *socket_addr_size);
int udpSendTo(tUdpSock *socket, const unsigned char* data, unsigned int size, int mode, tUdpSockAddr *socket_addr, int socket_addr_size);
int udpInit(tUdpSock **socket, XLhandle *pEvent, tUdpSockAddr *socket_addr); 
void udpShutdown(tUdpSock *socket);
int udpGetLastError();


#ifdef __cplusplus
}
#endif

#endif

#endif
