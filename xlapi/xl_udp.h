#pragma once

/* xl_udp.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#if OPTION_ENABLE_XLAPI_V3

#define XL_SOCKET uint64_t
#define XL_INVALID_SOCKET (-1)

extern BOOL xlUdpSocketStartup(char* app_name);
extern void xlUdpSocketCleanup();

// SOCKET
extern BOOL xlUdpSocketOpen(XL_SOCKET* sockp, BOOL useTCP, BOOL nonBlocking, BOOL reuseaddr);
extern BOOL xlUdpSocketClose(XL_SOCKET* sockp);
extern BOOL xlUdpSocketBind(XL_SOCKET sock, char* netName, char* segName, uint8_t* mac, uint8_t* addr, uint16_t port);

// MULTICAST
extern BOOL xlUdpSocketJoin(XL_SOCKET sock, uint8_t* multicastAddr);

// UDP
extern int16_t xlUdpSocketRecvFrom(XL_SOCKET sock, uint8_t* buffer, uint16_t bufferSize, uint8_t* addr, uint16_t* port);
extern int16_t xlUdpSocketSendTo(XL_SOCKET sock, const uint8_t* buffer, uint16_t bufferSize, const uint8_t* addr, uint16_t port);

// TCP
extern BOOL xlUdpSocketListen(XL_SOCKET sock);
extern BOOL xlUdpSocketAccept(XL_SOCKET sock, uint8_t addr[]);
extern int16_t xlUdpSocketRecv(XL_SOCKET sock, uint8_t* buffer, uint16_t bufferSize);
extern int16_t xlUdpSocketSend(XL_SOCKET sock, const uint8_t* buffer, uint16_t bufferSize);
extern BOOL xlUdpSocketShutdown(XL_SOCKET sock);



#endif
