#pragma once
#define __XCPTL_H__

/*----------------------------------------------------------------------------
| File:
|   xcptl.h
|
| Description:
|   XCPlite internal header file for generic transport layer functions
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

// for server
int32_t XcpTlHandleTransmitQueue(void);

// for protocol layer
bool XcpTlWaitForTransmitQueueEmpty(uint16_t timeout_ms); // Wait (sleep) until transmit queue is empty, timeout after 1s return false
void XcpTlSendCrm(const uint8_t *data, uint8_t size);     // Transmit a packet (the packet contains a single XCP CRM command response message)
uint16_t XcpTlGetCtr(void);                               // Get the next transmit message counter
