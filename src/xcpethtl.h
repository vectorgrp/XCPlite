#pragma once
#define __XCPETHTL_H__

/*----------------------------------------------------------------------------
| File:
|   xcpethtl.h
|
| Description:
|   XCPlite internal header file for xcpEthTl.c
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
 ----------------------------------------------------------------------------*/

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#include <stdbool.h>
#include <stdint.h>

#include "queue.h"     // for QueueXxxx, tQueueHandle
#include "xcptl_cfg.h" // for XCPTL_xxx

bool XcpEthTlInit(const uint8_t *addr, uint16_t port, bool useTCP, tQueueHandle queue_handle); // Start transport layer
void XcpEthTlShutdown(void);
void XcpEthTlGetInfo(bool *isTCP, uint8_t *mac, uint8_t *addr, uint16_t *port);
bool XcpEthTlHandleCommands(void); // Handle incoming XCP commands
#ifdef XCPTL_ENABLE_MULTICAST
void XcpEthTlSendMulticastCrm(const uint8_t *data, uint16_t n, const uint8_t *addr, uint16_t port); // Send multicast command response
void XcpEthTlSetClusterId(uint16_t clusterId);                                                      // Set cluster id for GET_DAQ_CLOCK_MULTICAST reception
#endif
