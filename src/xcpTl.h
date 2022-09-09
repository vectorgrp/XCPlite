#pragma once
/* xcpTl.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#define XCPTL_ERROR_OK             0
#define XCPTL_ERROR_WOULD_BLOCK    1
#define XCPTL_ERROR_SEND_FAILED    2
#define XCPTL_ERROR_INVALID_MASTER 3

extern BOOL XcpTlInit(const uint8_t* addr, uint16_t port, BOOL useTCP, uint16_t segmentSize); // Start transport layer
extern void XcpTlShutdown(); // Stop transport layer
extern int32_t XcpTlGetLastError(); // Get last error code
extern BOOL XcpTlHandleCommands(); // Handle incoming XCP commands
extern void XcpTlSendCrm(const uint8_t* data, uint16_t n); // Send or queue (depending on XCPTL_QUEUED_CRM) a command response
extern uint8_t* XcpTlGetTransmitBuffer(void** par, uint16_t size); // Get a buffer for a message with size
extern void XcpTlCommitTransmitBuffer(void* par, BOOL flush); // Commit a buffer from XcpTlGetTransmitBuffer
extern void XcpTlFlushTransmitBuffer(); // Finalize the current transmit packet
extern void XcpTlWaitForTransmitQueueEmpty(); // Wait (sleep) until transmit queue is ready for immediate response
extern int32_t XcpTlHandleTransmitQueue(); // Send all committed packets in the transmit queue
extern BOOL XcpTlWaitForTransmitData(uint32_t timeout_ms); // Wait until packets are ready to send
#ifdef XCPTL_ENABLE_MULTICAST
extern void XcpTlSetClusterId(uint16_t clusterId); // Set cluster id for GET_DAQ_CLOCK_MULTICAST reception
#endif

