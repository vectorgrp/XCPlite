/* udp.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __UDP_H__
#define __UDP_H__

#ifdef APP_ENABLE_XLAPI_V3

#ifdef __cplusplus
extern "C" {
#endif


#define HTONS(x) ((((x)&0x00ff) << 8) | (((x)&0xff00) >> 8))

#ifdef _WIN
#include <pshpack1.h>
#else
#pragma pack(1) 
#endif


// Compatibel to WinSock SOCKADDR_IN, holds MAC for XL-API UDP stack
typedef struct {
    uint16_t sin_family; // AF_INET = 2
    uint16_t sin_port; // Port is in Network byteorder
    unsigned char sin_addr[4];
    unsigned char sin_zero[2];
    unsigned char sin_mac[6];
} tUdpSockAddrXl;

#ifdef _WIN
#include <poppack.h>
#else
#pragma pack() 
#endif


typedef struct {
   XLnetworkHandle networkHandle; // Network handle
   XLethPortHandle portHandle; // VP handle
   tUdpSockAddrXl localAddr; // Local socket address 
   tUdpSockAddrXl multicastAddr; // Local socket address for Multicast
   XLhandle event; // Notofication event
} tUdpSockXl;


#define RECV_FLAGS_UNICAST   0x01
#define RECV_FLAGS_MULTICAST 0x02

int udpRecvFrom(tUdpSockXl*socket, unsigned char *data, unsigned int size, tUdpSockAddrXl*socket_addr, unsigned int *flags);
int udpSendTo(tUdpSockXl*socket, const unsigned char* data, unsigned int size, int mode, tUdpSockAddrXl*socket_addr, int socket_addr_size);
int udpInit(tUdpSockXl**socket, tUdpSockAddrXl*socket_addr, tUdpSockAddrXl* multicast_addr );
void udpShutdown(tUdpSockXl*socket);


#ifdef __cplusplus
}
#endif

#endif
#endif
