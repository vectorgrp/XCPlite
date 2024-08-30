#pragma once
/* xcpEthTl.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

/* ETH transport Layer functions called by application */
extern BOOL XcpEthTlInit(const uint8_t* addr, uint16_t port, BOOL useTCP, uint16_t segmentSize, BOOL blockingRx); // Start transport layer
extern void XcpEthTlShutdown();
#ifdef PLATFORM_ENABLE_GET_LOCAL_ADDR
extern void XcpEthTlGetInfo(BOOL* isTCP, uint8_t* mac, uint8_t* addr, uint16_t* port);
#endif

/* ETH transport Layer functions called by XCPlite.c */
extern BOOL XcpEthTlHandleCommands(uint32_t timeout_ms); // Handle all incoming XCP commands, (wait for at least timeout_ms)
extern void XcpEthTlSendMulticastCrm(const uint8_t* data, uint16_t n, const uint8_t* addr, uint16_t port); // Send multicast command response
#ifdef XCPTL_ENABLE_MULTICAST
extern void XcpEthTlSetClusterId(uint16_t clusterId); // Set cluster id for GET_DAQ_CLOCK_MULTICAST reception
#endif

