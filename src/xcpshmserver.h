#pragma once
#define __XCP_ETH_SERVER_H__

/*----------------------------------------------------------------------------
| File:
|   xcpshmserver.h
|
| Description:
|   XCPlite internal header file for the SHM server
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdint.h>

/// Initialize the server singleton.
/// @pre User has called XcpInit.
/// @param measurement_queue_size Measurement queue size in bytes. Includes the bytes occupied by the queue header and some space needed for alignment.
/// @return true on success, otherwise false.
bool XcpShmServerInit(uint32_t measurement_queue_size);

/// Shutdown the server.
bool XcpShmServerShutdown(void);

/// Get the server status.
/// @return true if the server is running, otherwise false.
bool XcpShmServerStatus(void);
