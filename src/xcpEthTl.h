#pragma once
#define __XCP_ETHTL_H__

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"  // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex
#include "xcpQueue.h"  // for QueueXxxx, tQueueHandle
#include "xcptl_cfg.h" // for XCPTL_xxx

#define XCPTL_TIMEOUT_INFINITE 0xFFFFFFFF // Infinite timeout (blocking mode)

bool XcpTlWaitForTransmitQueueEmpty(uint16_t timeout_ms); // Wait (sleep) until transmit queue is empty, timeout after 1s return false
int32_t XcpTlHandleTransmitQueue(void);

int XcpTlSend(const uint8_t *data, uint16_t size, const uint8_t *addr, uint16_t port); // Transmit a segment (contains multiple DTO or EVENT transport layer packets)
void XcpTlSendCrm(const uint8_t *data, uint8_t size);                                  // Transmit a packet (the packet contains a single XCP CRM command response message)
uint16_t XcpTlGetCtr(void);                                                            // Get the next transmit message counter

bool XcpEthTlInit(const uint8_t *addr, uint16_t port, bool useTCP, bool blockingRx, tQueueHandle queue_handle); // Start transport layer
void XcpEthTlShutdown(void);
void XcpEthTlGetInfo(bool *isTCP, uint8_t *mac, uint8_t *addr, uint16_t *port);
bool XcpEthTlHandleCommands(uint32_t timeout_ms); // Handle all incoming XCP commands, (wait for at least timeout_ms)
#ifdef XCPTL_ENABLE_MULTICAST
void XcpEthTlSendMulticastCrm(const uint8_t *data, uint16_t n, const uint8_t *addr, uint16_t port); // Send multicast command response
void XcpEthTlSetClusterId(uint16_t clusterId);                                                      // Set cluster id for GET_DAQ_CLOCK_MULTICAST reception
#endif
