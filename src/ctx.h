#pragma once
#define __CTX_H__

/*----------------------------------------------------------------------------
| File:
|   ctx.h
|
| Description:
|   XCPlite internal header file for the context list and the span name list
|
|   A context is a named identity ("who is executing"), for example a class instance or a worker thread.
|   An event may belong to a context, it is then named <context name>.<event name>.
|   A span is a named section of code, its name gets a small id which is measured on the "spans" event of a context.
|   The application programming interface is C++ only, see xcplib_context.hpp.
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <stdbool.h> // for bool
#include <stdint.h>  // for uint16_t, uint32_t, uint8_t

#include "xcp_cfg.h"    // for XCP_ENABLE_DAQ_CONTEXT_LIST
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcplite.h"    // for tXcpEventId

#ifdef __cplusplus
extern "C" {
#endif

#ifdef XCP_ENABLE_DAQ_CONTEXT_LIST

// Context id 0 means no context
#define XCP_UNDEFINED_CONTEXT 0

// Initialize the context list and the span name list, called by XcpInitEventList
void XcpInitContextList(void);

// Create a context or return the id of the existing context with this name, thread safe
// Returns the context id (1..XCP_MAX_CONTEXT_COUNT) or XCP_UNDEFINED_CONTEXT
uint16_t XcpCreateContext(const char *name);

#ifdef OPTION_SHM_MODE // create a context for another application
// In SHM mode, create or find a context of the application app_id, used to preload contexts from the binary persistence file
uint16_t XcpCreateAppContext(const char *name, uint8_t app_id);
// In SHM mode, get the application id of a context
uint8_t XcpGetContextAppId(uint16_t context);
#endif

// Get the name of a context, returns NULL if not found
const char *XcpGetContextName(uint16_t context);

// Get the number of contexts
uint16_t XcpGetContextCount(void);

// Create an event which belongs to a context or return the existing event with this name in this context, thread safe
// With context XCP_UNDEFINED_CONTEXT, this is the same as XcpCreateEvent
tXcpEventId XcpCreateContextEvent(uint16_t context, const char *name, uint32_t cycle_time_ns /* ns */, uint8_t priority /* 0 = queued, >=1 flushing */);

// Register a span name or return the id of the existing span name, thread safe
// Returns the span id (1..XCP_MAX_SPAN_COUNT-1) or 0 (no span) when out of memory
uint16_t XcpRegisterSpanName(const char *name);

// Get the name of a span id, returns NULL if not found
const char *XcpGetSpanName(uint16_t span);

// Get the number of span ids, including the reserved span id 0
uint16_t XcpGetSpanCount(void);

#endif // XCP_ENABLE_DAQ_CONTEXT_LIST

#ifdef __cplusplus
} // extern "C"
#endif
