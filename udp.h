/* udp.h */

#ifndef __UDP_H__
#define __UDP_H__

#ifdef __cplusplus
extern "C" {
#endif


#define HTONS(x) ((((x)&0x00ff) << 8) | (((x)&0xff00) >> 8))

#ifdef _WIN
#include <pshpack1.h>
#endif

// Compatibel to WinSock SOCKADDR_IN, holds MAC for XL-API UDP stack
typedef struct {
    uint16_t sin_family; // AF_INET = 2
    uint16_t sin_port; // Port is in Network byteorder
    unsigned char sin_addr[4];
    unsigned char sin_zero[2];
    unsigned char sin_mac[6];
} tUdpSockAddrXl;


typedef struct {
   XLnetworkHandle networkHandle; // Network handle
   XLethPortHandle portHandle; // VP handle
   tUdpSockAddrXl localAddr; // Local socket address 
   tUdpSockAddrXl multicastAddr; // Local socket address for Multicast
} tUdpSockXl;


int udpRecvFrom(tUdpSockXl*socket, unsigned char *data, unsigned int size, tUdpSockAddrXl*socket_addr);
int udpSendTo(tUdpSockXl*socket, const unsigned char* data, unsigned int size, int mode, tUdpSockAddrXl*socket_addr, int socket_addr_size);
int udpInit(tUdpSockXl**socket, XLhandle *pEvent, tUdpSockAddrXl*socket_addr, tUdpSockAddrXl* multicast_addr );
void udpShutdown(tUdpSockXl*socket);
int udpGetLastError(tUdpSockXl*socket);


#ifdef __cplusplus
}
#endif

#endif
