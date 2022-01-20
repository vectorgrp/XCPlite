#pragma once

/* xcpTl.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifdef __cplusplus
extern "C" {
#endif

extern int XcpTlInit(const uint8_t* addr, uint16_t port, BOOL useTCP); // Start transport layer
extern void XcpTlShutdown(); // Stop transport layer
extern int XcpTlGetLastError(); // Get last error code

extern const char* XcpTlGetServerAddrString(); // Get slave addr as string
extern uint16_t XcpTlGetServerPort(); // Get slave port
extern int XcpTlGetServerMAC(uint8_t* mac); // Get slave MAC
extern uint64_t XcpTlGetBytesWritten(); // Get the number of bytes send

extern int XcpTlHandleCommands(); // Handle incoming XCP commands
extern void XcpTlSendCrm(const uint8_t* data, uint16_t n); // Send or queue (depending on XCPTL_QUEUED_CRM) a command response

extern uint8_t* XcpTlGetTransmitBuffer(void** par, uint16_t size); // Get a buffer for a message with size
extern void XcpTlCommitTransmitBuffer(void* par); // Commit a buffer from XcpTlGetTransmitBuffer
extern void XcpTlFlushTransmitBuffer(); // Finalize the current transmit packet
extern void XcpTlFlushTransmitQueue(); // Empty the transmit queue
extern void XcpTlWaitForTransmitQueue(); // Wait (sleep) until transmit queue is ready for immediate response
extern int XcpTlHandleTransmitQueue(); // Send all full packets in the transmit queue
extern void XcpTlInitTransmitQueue(); // Initialize the transmit queue
extern void XcpTlWaitForTransmitData(uint32_t timeout_ms); // Wait until packets are ready to send

extern void XcpTlSetClusterId(uint16_t clusterId); // Set cluster id for GET_DAQ_CLOCK_MULTICAST reception

#ifdef __cplusplus
}
#endif
