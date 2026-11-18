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
|   The context list is part of the XCP state (tXcpData), next to the event list.
|   It is a lock-free and wait-free name list (namelist.h): contexts are created without a mutex, by any thread and any process.
|
|   Span names are registered in one of two ways:
|     - OPTION_DAQ_SPAN_SECTION_REGISTRATION: at link time, the span names are constant descriptors in the linker section xcp_spans,
|       the span id is the position in the section + 1. Deterministic ids, no registration at runtime, a span name which is used
|       at several code locations may have several ids. ELF and Mach-O only, not in SHM mode.
|     - Otherwise at runtime in a span name list in the XCP state, a name list like the context list. The ids are assigned in the
|       order of the first execution, a span name has exactly one id.
|   See docs/NAMELIST.md.
|
|   The application programming interface is C++ only, see xcplib_context.hpp.
|   Enabled with OPTION_DAQ_CONTEXT_LIST, default and shm configuration only.
|   In SHM mode, the lists are shared by all application processes: each application has its own namespace for context names,
|   span names and span ids are common to all applications.
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
#include <string.h>  // for strnlen

#if defined(OPTION_DAQ_SPAN_SECTION_REGISTRATION) && defined(__APPLE__)
#include <mach-o/getsect.h> // for getsectiondata
#include <mach-o/ldsyms.h>  // for _mh_execute_header
#endif

#include "ctx.h" // for the context list interface

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "namelist.h"  // for NameListRegister, NameListGetName, ...
#include "platform.h"  // for mutex
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

// The context list is a lock-free and wait-free name list (namelist.h), the context id is the id in the name list + 1
#define contextList() (&shared.context_list.list)
#define contextListMut() (&shared_mut.context_list.list)

#ifdef OPTION_DAQ_SPAN_SECTION_REGISTRATION

// Link time registration: the span names are the descriptors in the linker section xcp_spans, set once by XcpInitContextList
static const tXcpSpanDescriptor *gXcpSpanSectionBegin = NULL;
static uint16_t gXcpSpanSectionCount = 0;

static void initSpanSection(void) {

    const tXcpSpanDescriptor *begin = NULL;
    size_t size = 0;

#if defined(__ELF__)
    // The boundary symbols are declared weak: without any span in the program, the section does not exist and the symbols are NULL
    const tXcpSpanDescriptor *start = __start_xcp_spans;
    const tXcpSpanDescriptor *stop = __stop_xcp_spans;
    if (start != NULL && stop != NULL && start < stop) {
        begin = start;
        size = (size_t)((const uint8_t *)stop - (const uint8_t *)start);
    }
#elif defined(__APPLE__)
    // Looked up at runtime: without any span in the program, the section does not exist and a boundary symbol may not link
    unsigned long section_size = 0;
    begin = (const tXcpSpanDescriptor *)(void *)getsectiondata(&_mh_execute_header, "__DATA", "xcp_spans", &section_size);
    size = (begin != NULL) ? (size_t)section_size : 0;
#endif

    // The section must be an array of descriptors without gaps
    assert(size % sizeof(tXcpSpanDescriptor) == 0);
    size_t count = size / sizeof(tXcpSpanDescriptor);
    if (count > 0xFFFE) {
        DBG_PRINTF_ERROR("too many span names in section xcp_spans (%u)\n", (unsigned int)count);
        count = 0xFFFE;
    }
    gXcpSpanSectionBegin = begin;
    gXcpSpanSectionCount = (uint16_t)count;

    DBG_PRINTF3("Found %u span names in section xcp_spans\n", (unsigned int)count);
    for (uint16_t i = 0; i < gXcpSpanSectionCount; i++) {
        DBG_PRINTF5("  Span %u: %s\n", i + 1, begin[i].name);
    }
}

#else

// The span name list is a lock-free and wait-free name list (namelist.h), the span id is the id in the name list
#define spanList() (&shared.span_list.list)
#define spanListMut() (&shared_mut.span_list.list)

#endif

/****************************************************************************/
/* Context list                                                             */
/****************************************************************************/

// Initialize the context list and the span name list
// Called once by XcpInit, in SHM mode by the leader only, before the lists are used by other threads or processes
void XcpInitContextList(void) {

    bool ok = NameListInit(contextListMut(), sizeof(shared_mut.context_list), XCP_MAX_CONTEXT_COUNT, XCP_NAMELIST_SPARE, XCP_MAX_CONTEXT_NAME + 1, 0);
    assert(ok);
    (void)ok;

#ifdef OPTION_DAQ_SPAN_SECTION_REGISTRATION
    initSpanSection();
#else
    ok = NameListInit(spanListMut(), sizeof(shared_mut.span_list), XCP_MAX_SPAN_COUNT, XCP_NAMELIST_SPARE, XCP_MAX_SPAN_NAME + 1, 0);
    assert(ok);
    (void)ok;

    // Span id 0 is reserved for "no span"
    uint32_t none = NameListRegister(spanListMut(), 0, "none", NULL, NULL);
    assert(none == 0);
    (void)none;
#endif
}

// Get the number of contexts
uint16_t XcpGetContextCount(void) {
    if (!XcpIsActivated())
        return 0;
    return (uint16_t)NameListGetCount(contextList());
}

// Get the name of a context, returns NULL if not found
const char *XcpGetContextName(uint16_t context) {
    if (!XcpIsActivated() || context == XCP_UNDEFINED_CONTEXT)
        return NULL;
    return NameListGetName(contextList(), (uint32_t)context - 1);
}

#ifdef OPTION_SHM_MODE // get context application id
// Get the application id of a context
uint8_t XcpGetContextAppId(uint16_t context) {
    if (!XcpIsActivated() || context == XCP_UNDEFINED_CONTEXT)
        return 0;
    return (uint8_t)NameListGetScope(contextList(), (uint32_t)context - 1);
}
#endif

// Create a context or return the id of the existing context with this name
// An existing context may have been preloaded from the binary persistence file
// In SHM mode, the context list is shared by all application processes, each application has its own namespace for context names (the scope of the name)
// Thread safe and process safe, lock-free and wait-free: all threads and processes get the same context id for the same name
#ifdef OPTION_SHM_MODE // context of this application process
static uint16_t createContext(const char *name, uint8_t app_id);
uint16_t XcpCreateContext(const char *name) { return createContext(name, XcpShmGetAppId()); }
uint16_t XcpCreateAppContext(const char *name, uint8_t app_id) { return createContext(name, app_id); }
static uint16_t createContext(const char *name, uint8_t app_id) {
    const uint32_t scope = app_id;
#else
uint16_t XcpCreateContext(const char *name) {
    const uint32_t scope = 0;
#endif

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

    // Find the existing context with this name or create a new one
    bool created = false;
    uint32_t id = NameListRegister(contextListMut(), scope, name, NULL, &created);
    if (id == NAMELIST_UNDEFINED) {
        DBG_PRINTF_ERROR("too many contexts, context '%s' not created (OPTION_DAQ_CONTEXT_COUNT=%u)\n", name, XCP_MAX_CONTEXT_COUNT);
        return XCP_UNDEFINED_CONTEXT;
    }
    if (created) {
        DBG_PRINTF3("Create Context %u: %s\n", (unsigned int)id + 1, name);
    }
    return (uint16_t)(id + 1);
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

    if (context > NameListGetCount(contextList())) {
        DBG_PRINTF_ERROR("context %u of event '%s' does not exist\n", context, name);
        return XCP_UNDEFINED_EVENT_ID;
    }
#ifdef OPTION_SHM_MODE // the context must be owned by this application process
    if (context != XCP_UNDEFINED_CONTEXT && NameListGetScope(contextList(), (uint32_t)context - 1) != XcpShmGetAppId()) {
        DBG_PRINTF_ERROR("context %u of event '%s' belongs to another application\n", context, name);
        return XCP_UNDEFINED_EVENT_ID;
    }
#endif

    mutexLock(&local_mut.event_list_mutex);
    tXcpEventId id = XcpFindContextEvent(context, name);
    if (id == XCP_UNDEFINED_EVENT_ID) {
        id = XcpCreateIndexedEvent(name, context, cycle_time_ns, priority);
    }
    mutexUnlock(&local_mut.event_list_mutex);
    return id;
}

/****************************************************************************/
/* Span names                                                               */
/****************************************************************************/

#ifdef OPTION_DAQ_SPAN_SECTION_REGISTRATION

// Link time registration
// The span names are constant data in the linker section xcp_spans, the span id is the position of the descriptor in the section + 1
// There is no registration at runtime, see XCP_SPAN_SECTION_DECL and XCP_SPAN_SECTION_ID in xcplib.h

// Get the number of span ids, including the reserved span id 0
uint16_t XcpGetSpanCount(void) {
    if (!XcpIsActivated())
        return 0;
    return (uint16_t)(gXcpSpanSectionCount + 1);
}

// Get the name of a span id, returns NULL if not found
// A span name which is used at several code locations may have several span ids
const char *XcpGetSpanName(uint16_t span) {
    if (!XcpIsActivated() || span > gXcpSpanSectionCount)
        return NULL;
    if (span == 0)
        return "none";
    return gXcpSpanSectionBegin[span - 1].name;
}

#else // OPTION_DAQ_SPAN_SECTION_REGISTRATION

// Runtime registration in the span name list

// Get the number of span ids, including the reserved span id 0
uint16_t XcpGetSpanCount(void) {
    if (!XcpIsActivated())
        return 0;
    return (uint16_t)NameListGetCount(spanList());
}

// Get the name of a span id, returns NULL if not found
const char *XcpGetSpanName(uint16_t span) {
    if (!XcpIsActivated())
        return NULL;
    return NameListGetName(spanList(), span);
}

// Register a span name or return the id of the existing span name
// Names longer than XCP_MAX_SPAN_NAME are truncated
// Returns the span id (1..XCP_MAX_SPAN_COUNT-1) or 0 (no span) when out of memory
// Thread safe and process safe, lock-free and wait-free: all threads and processes get the same span id for the same name
uint16_t XcpRegisterSpanName(const char *name) {

    if (!XcpIsActivated()) {
        return 0; // Uninitialized
    }

    assert(name != NULL);
    if (name[0] == 0) {
        DBG_PRINT_ERROR("span name is empty\n");
        return 0;
    }

    // Find the existing span name or register a new one
    bool created = false;
    uint32_t id = NameListRegister(spanListMut(), 0, name, NULL, &created);
    if (id == NAMELIST_UNDEFINED) {
        DBG_PRINTF_ERROR("too many span names, span '%.*s' not registered (OPTION_DAQ_SPAN_COUNT=%u)\n", XCP_MAX_SPAN_NAME, name, XCP_MAX_SPAN_COUNT);
        return 0;
    }
    if (created) {
        DBG_PRINTF3("Register span name %u: %.*s\n", (unsigned int)id, XCP_MAX_SPAN_NAME, name);
    }
    return (uint16_t)id;
}

#endif // !OPTION_DAQ_SPAN_SECTION_REGISTRATION

#else

// Avoid an empty translation unit
typedef int ctx_c_empty_translation_unit_t;

#endif // XCP_ENABLE_DAQ_CONTEXT_LIST
