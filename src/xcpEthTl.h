#pragma once
/* xcpEthTl.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */


/* ETH transport Layer functions called by server */
#if defined(XCPTL_ENABLE_UDP) || defined(XCPTL_ENABLE_TCP)

extern BOOL XcpEthTlInit(const uint8_t* addr, uint16_t port, BOOL useTCP, BOOL blockingRx); // Start transport layer
extern void XcpEthTlShutdown();
#ifdef PLATFORM_ENABLE_GET_LOCAL_ADDR
extern void XcpEthTlGetInfo(BOOL* isTCP, uint8_t* mac, uint8_t* addr, uint16_t* port);
#endif

/* ETH transport Layer functions called by server */
extern BOOL XcpEthTlHandleCommands(uint32_t timeout_ms); // Handle all incoming XCP commands, (wait for at least timeout_ms)

// All other network specific application functions functions are declared in xcpCanTl.h or xcpEthTl.h
extern int32_t XcpTlHandleTransmitQueue(); // Send all outgoing packets in the transmit queue
extern BOOL XcpTlWaitForTransmitData(uint32_t timeout_ms); // Wait for at least timeout_ms, until packets are pending in the transmit queue

/* ETH transport Layer functions called by protocol layer */
#ifdef PLATFORM_ENABLE_GET_LOCAL_ADDR
extern void XcpEthTlSendMulticastCrm(const uint8_t* data, uint16_t n, const uint8_t* addr, uint16_t port); // Send multicast command response
#endif
#ifdef XCPTL_ENABLE_MULTICAST
extern void XcpEthTlSetClusterId(uint16_t clusterId); // Set cluster id for GET_DAQ_CLOCK_MULTICAST reception
#endif

// Get last error code
#define XCPTL_OK                   0
#define XCPTL_ERROR_WOULD_BLOCK    1
#define XCPTL_ERROR_SEND_FAILED    2
#define XCPTL_ERROR_INVALID_MASTER 3
extern int32_t XcpTlGetLastError();

#endif
