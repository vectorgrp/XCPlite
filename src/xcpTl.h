#pragma once
/* xcpTl.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#define XCPTL_TIMEOUT_INFINITE 0xFFFFFFFF // Infinite timeout (blocking mode) for XcpTlHandleCommands, XcpTlWaitForTransmitData

/* Transport Layer functions called by XCPlite.c */
extern void XcpTlSendCrm(const uint8_t* data, uint16_t n); // Send or queue (depending on XCPTL_QUEUED_CRM) a command response
extern uint8_t* XcpTlGetTransmitBuffer(void** handle, uint16_t size); // Get a buffer for a message with size
extern void XcpTlCommitTransmitBuffer(void* handle, BOOL flush); // Commit a buffer (by handle returned from XcpTlGetTransmitBuffer)
extern void XcpTlFlushTransmitBuffer(); // Finalize the current transmit packet (ETH only)
extern void XcpTlWaitForTransmitQueueEmpty(); // Wait (sleep) until transmit queue is empty 

/* Transport Layer functions called by application */
/* All other network specific application functions functions declared in xcpCanTl.h or xcpEthTl.h */
extern int32_t XcpTlHandleTransmitQueue(); // Send all outgoing packets in the transmit queue
extern BOOL XcpTlWaitForTransmitData(uint32_t timeout_ms); // Wait for at least timeout_ms, until packets are pending in the transmit queue

// Generic transport layer functions
// For testing 
extern BOOL XcpTlInit(uint16_t segmentSize, int (*cb)(uint16_t msgLen, const uint8_t* msgBuf)); // Start generic transport layer, tx message callback
extern void XcpTlShutdown(); // Stop transport layer
extern uint8_t XcpTlCommand(uint16_t msgLen, const uint8_t* msgBuf); // rx message

// Get last error code
#define XCPTL_OK                   0
#define XCPTL_ERROR_WOULD_BLOCK    1
#define XCPTL_ERROR_SEND_FAILED    2
#define XCPTL_ERROR_INVALID_MASTER 3
extern int32_t XcpTlGetLastError();

// Get transmit queue level
extern int32_t XcpTlGetTransmitQueueLevel(); 
