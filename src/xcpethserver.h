#pragma once
#define __XCP_ETH_SERVER_H__

/*----------------------------------------------------------------------------
| File:
|   xcpethserver.h
|
| Description:
|   XCPlite internal header file for xcpethserver.c
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdint.h>

/// Initialize the server singleton.
/// @pre User has called XcpInit.
/// @param address Address to bind to.
/// @param port Port to bind to.
/// @param use_tcp Use TCP if true, otherwise UDP.
/// @param measurement_queue_size Measurement queue size in bytes. Includes the bytes occupied by the queue header and some space needed for alignment.
/// @return true on success, otherwise false.
bool XcpEthServerInit(const uint8_t *address, uint16_t port, bool use_tcp, uint32_t measurement_queue_size);

/// Shutdown the server.
bool XcpEthServerShutdown(void);

/// Get the server status.
/// @return true if the server is running, otherwise false.
bool XcpEthServerStatus(void);
