/*****************************************************************************
| File:
|   ctx.c
|
| Description:
|   Context list and span name list
|
|   A context is a named identity ("who is executing"), for example a class instance or a worker thread.
|   An event may belong to a context, it is then named <context name>.<event name> in the A2L file and in the tool.
|   The context id stored in the event (tXcpEvent.context) is the position of the context in the context list + 1.
|
|   A span is a named section of code. The span name gets a small id, which is measured on the "spans" event of a context
|   and displayed with the enum conversion conv.spans, created by the A2L writer.
|
|   The lists are part of the XCP state (tXcpData), next to the event list.
|   The application programming interface is C++ only, see xcplib_context.hpp.
|   Enabled with OPTION_DAQ_CONTEXT_LIST, default configuration only.
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "xcplib_cfg.h" // for OPTION_xxx

#include "xcp_cfg.h" // XCP protocol layer configuration parameters (XCP_xxx)

#ifdef XCP_ENABLE_DAQ_CONTEXT_LIST

#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uint8_t, uint16_t, ...
#include <string.h>  // for strcmp, strncpy

#include "ctx.h" // for the context list interface

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "platform.h"  // for atomics, mutex
#include "shm.h"       // for XcpShmGetAppId, declares nothing outside SHM mode
#include "xcplite.h"   // for tXcpData, tXcpLocalData, XcpCreateIndexedEvent, XcpFindContextEvent

// XCP singleton state, defined in xcplite.c
#ifdef OPTION_SHM_MODE // gXcpData is a pointer to the shared state in SHM mode
extern tXcpData *gXcpData;
#define shared (*(const tXcpData *)gXcpData) // Read only access to the XCP singleton data
#define shared_mut (*gXcpData)               // Mutable access to the XCP singleton data
#else
extern tXcpData gXcpData;
#define shared (*(const tXcpData *)&gXcpData) // Read only access to the XCP singleton data
#define shared_mut gXcpData                   // Mutable access to the XCP singleton data
#endif
extern tXcpLocalData gXcpLocalData;
#define local_mut gXcpLocalData // Mutable access to process-local state

// The lists are modified under the event list mutex, entries are published by the count (acquire/release), like the event list
#define acquireContextCount() (uint16_t)atomic_load_explicit(&shared.context_list.count, memory_order_acquire)
#define releaseContextCount(n) atomic_store_explicit(&shared_mut.context_list.count, n, memory_order_release)
#define acquireSpanCount() (uint16_t)atomic_load_explicit(&shared.span_list.count, memory_order_acquire)
#define releaseSpanCount(n) atomic_store_explicit(&shared_mut.span_list.count, n, memory_order_release)

/****************************************************************************/
/* Context list                                                             */
/****************************************************************************/

// Initialize the context list and the span name list
void XcpInitContextList(void) {

    releaseContextCount(0);

    // Span id 0 is reserved for "no span"
    strncpy(shared_mut.span_list.name[0], "none", XCP_MAX_SPAN_NAME);
    shared_mut.span_list.name[0][XCP_MAX_SPAN_NAME] = 0;
    releaseSpanCount(1);
}

// Get the number of contexts
uint16_t XcpGetContextCount(void) {
    if (!XcpIsActivated())
        return 0;
    return acquireContextCount();
}

// Get the name of a context, returns NULL if not found
const char *XcpGetContextName(uint16_t context) {
    if (!XcpIsActivated() || context == XCP_UNDEFINED_CONTEXT || context > acquireContextCount())
        return NULL;
    return shared.context_list.context[context - 1].name;
}

// Create a context or return the id of the existing context with this name
// An existing context may have been preloaded from the binary persistence file
// Thread safe by mutex
// @@@@ TODO: Find a process safe solution for SHM mode
uint16_t XcpCreateContext(const char *name) {

    if (!XcpIsActivated()) {
        return XCP_UNDEFINED_CONTEXT; // Uninitialized
    }

    assert(name != NULL);

    // Check name length
    size_t name_len = STRNLEN(name, XCP_MAX_CONTEXT_NAME + 1);
    if (name_len == 0 || name_len > XCP_MAX_CONTEXT_NAME) {
        DBG_PRINTF_ERROR("context name '%.*s' empty or too long (> %d chars)\n", XCP_MAX_CONTEXT_NAME, name, XCP_MAX_CONTEXT_NAME);
        return XCP_UNDEFINED_CONTEXT;
    }

    mutexLock(&local_mut.event_list_mutex);

    // Find an existing context with this name
    uint16_t count = acquireContextCount();
    for (uint16_t i = 0; i < count; i++) {
#ifdef OPTION_SHM_MODE // find only contexts owned by this application process
        if (shared.context_list.context[i].app_id != XcpShmGetAppId())
            continue;
#endif
        if (strcmp(shared.context_list.context[i].name, name) == 0) {
            mutexUnlock(&local_mut.event_list_mutex);
            return (uint16_t)(i + 1);
        }
    }

    // Create a new context
    if (count >= XCP_MAX_CONTEXT_COUNT) {
        mutexUnlock(&local_mut.event_list_mutex);
        DBG_PRINTF_ERROR("too many contexts, context '%s' not created (OPTION_DAQ_CONTEXT_COUNT=%u)\n", name, XCP_MAX_CONTEXT_COUNT);
        return XCP_UNDEFINED_CONTEXT;
    }
    strncpy(shared_mut.context_list.context[count].name, name, XCP_MAX_CONTEXT_NAME);
    shared_mut.context_list.context[count].name[XCP_MAX_CONTEXT_NAME] = 0;
#ifdef OPTION_SHM_MODE // store the application id in the context
    shared_mut.context_list.context[count].app_id = XcpShmGetAppId();
#endif
    releaseContextCount(count + 1); // Publish the new context

    mutexUnlock(&local_mut.event_list_mutex);

    DBG_PRINTF3("Create Context %u: %s\n", count + 1, name);
    return (uint16_t)(count + 1);
}

// Create an event which belongs to a context or return the existing event with this name in this context
// An existing event may have been preloaded from the binary persistence file
// The event is named <context name>.<name>, see XcpGetEventName
// With context XCP_UNDEFINED_CONTEXT, this is the same as XcpCreateEvent
// Thread safe by mutex
tXcpEventId XcpCreateContextEvent(uint16_t context, const char *name, uint32_t cycle_time_ns, uint8_t priority) {

    if (!XcpIsActivated()) {
        return XCP_UNDEFINED_EVENT_ID; // Uninitialized
    }

    assert(name != NULL);

    if (context > acquireContextCount()) {
        DBG_PRINTF_ERROR("context %u of event '%s' does not exist\n", context, name);
        return XCP_UNDEFINED_EVENT_ID;
    }

    mutexLock(&local_mut.event_list_mutex);
    tXcpEventId id = XcpFindContextEvent(context, name);
    if (id == XCP_UNDEFINED_EVENT_ID) {
        id = XcpCreateIndexedEvent(name, context, cycle_time_ns, priority);
    }
    mutexUnlock(&local_mut.event_list_mutex);
    return id;
}

/****************************************************************************/
/* Span name list                                                           */
/****************************************************************************/

// Get the number of span ids, including the reserved span id 0
uint16_t XcpGetSpanCount(void) {
    if (!XcpIsActivated())
        return 0;
    return acquireSpanCount();
}

// Get the name of a span id, returns NULL if not found
const char *XcpGetSpanName(uint16_t span) {
    if (!XcpIsActivated() || span >= acquireSpanCount())
        return NULL;
    return shared.span_list.name[span];
}

// Register a span name or return the id of the existing span name
// Names longer than XCP_MAX_SPAN_NAME are truncated
// Returns the span id (1..XCP_MAX_SPAN_COUNT-1) or 0 (no span) when out of memory
// Thread safe by mutex
uint16_t XcpRegisterSpanName(const char *name) {

    if (!XcpIsActivated()) {
        return 0; // Uninitialized
    }

    assert(name != NULL);

    char span_name[XCP_MAX_SPAN_NAME + 1];
    strncpy(span_name, name, XCP_MAX_SPAN_NAME);
    span_name[XCP_MAX_SPAN_NAME] = 0;

    mutexLock(&local_mut.event_list_mutex);

    // Find an existing span name
    uint16_t count = acquireSpanCount();
    for (uint16_t i = 1; i < count; i++) {
        if (strcmp(shared.span_list.name[i], span_name) == 0) {
            mutexUnlock(&local_mut.event_list_mutex);
            return i;
        }
    }

    // Register a new span name
    if (count >= XCP_MAX_SPAN_COUNT) {
        mutexUnlock(&local_mut.event_list_mutex);
        DBG_PRINTF_ERROR("too many span names, span '%s' not registered (OPTION_DAQ_SPAN_COUNT=%u)\n", span_name, XCP_MAX_SPAN_COUNT);
        return 0;
    }
    memcpy(shared_mut.span_list.name[count], span_name, sizeof(span_name));
    releaseSpanCount(count + 1); // Publish the new span name

    mutexUnlock(&local_mut.event_list_mutex);

    DBG_PRINTF3("Register span name %u: %s\n", count, span_name);
    return count;
}

#else

// Avoid an empty translation unit
typedef int ctx_c_empty_translation_unit_t;

#endif // XCP_ENABLE_DAQ_CONTEXT_LIST
