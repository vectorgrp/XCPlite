#pragma once
/* xcpTl.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#define XCPTL_TIMEOUT_INFINITE 0xFFFFFFFF // Infinite timeout (blocking mode) for XcpTlHandleCommands, XcpTlWaitForTransmitData

// Transport Layer functions called by protocol layer in XCPlite.c 
extern void XcpTlSendCrm(const uint8_t* data, uint16_t n); // Send or queue (depending on XCPTL_QUEUED_CRM) a command response
extern uint8_t* XcpTlGetTransmitBuffer(void** handle, uint16_t size); // Get a buffer for a message with size
extern void XcpTlCommitTransmitBuffer(void* handle, BOOL flush); // Commit a buffer (by handle returned from XcpTlGetTransmitBuffer)
extern void XcpTlFlushTransmitBuffer(); // Finalize the current transmit packet (ETH only)
extern void XcpTlWaitForTransmitQueueEmpty(); // Wait (sleep) until transmit queue is empty 

// Generic transport layer functions for XCP server
extern BOOL XcpTlInit(); // Start generic transport layer
extern void XcpTlShutdown(); // Stop generic transport layer
extern uint8_t XcpTlCommand(uint16_t msgLen, const uint8_t* msgBuf); // Handle XCP message
extern const uint8_t * XcpTlTransmitQueuePeek( uint16_t* msg_len);  // Check if there is a fully commited message segment buffer in the transmit queue
extern void XcpTlTransmitQueueNext(); // Remove the next transmit queue entry

// Get transmit queue level
extern int32_t XcpTlGetTransmitQueueLevel(); 
