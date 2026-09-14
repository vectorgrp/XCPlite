#pragma once
#define __XCPLIB_H__

/*----------------------------------------------------------------------------
| File:
|   xcplib.h - Public libxcplite C API
|
| Description:
|   C header file for the XCPlite library libxcplite application programming interface
|   Used for Rust bindgen to generate FFI bindings for libxcplite
|   Supporting functions and macros for A2L generation are in a2l.h
|     C_API functions XcpXxxx
|     C_API types tXcpCalSegIndex, tXcpEventId
|     Macros CalSegXxxx, DaqXxxx, A2lXxxx, A2L_XXX
|     Constants XCP_XXX, CRC_XXX
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <assert.h>  // for static_assert
#include <stdbool.h> // for bool
#include <stddef.h>  // for size_t
#include <stdint.h>  // for uintxx_t

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XCPLITE_CONFIGURATION
#error "XCPLITE_CONFIGURATION must be defined to make sure the correct configuration override file is set"
#endif                  // XCPLITE_CONFIGURATION
#include "xcplib_cfg.h" // for OPTION_xxx, must include the correct configuration override file XCPLIB_CFG_OVERRIDE

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// XCP on Ethernet server

/// Initialize the XCP on Ethernet server singleton.
/// @pre User has called XcpInit.
/// @param address Address to bind to.
/// @param port Port to bind to.
/// @param use_tcp Use TCP if true, otherwise UDP.
/// @param measurement_queue_size Measurement queue size in bytes. Includes the bytes occupied by the queue header and some space needed for alignment.
/// @return true on success, otherwise false.
bool XcpEthServerInit(const uint8_t *address, uint16_t port, bool use_tcp, uint32_t measurement_queue_size);

/// Shutdown the XCP on Ethernet server.
bool XcpEthServerShutdown(void);

/// Get the XCP on Ethernet server instance status.
/// @return true if the server is running, otherwise false.
bool XcpEthServerStatus(void);

/// Get the XCP protocol layer status
bool XcpIsStarted(void);
bool XcpIsConnected(void);
bool XcpIsDaqRunning(void);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Calibration segments

/// Calibration segment handle
typedef uint16_t tXcpCalSegIndex;
#define XCP_UNDEFINED_CALSEG ((tXcpCalSegIndex)0xFFFF)
/// Calibration segment number (for XCP and A2L)
typedef uint8_t tXcpCalSegNumber;
#define XCP_UNDEFINED_CALSEG_NUM 0xFF

/// Create a calibration segment and add it to the list of calibration segments.
/// This calibration segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
/// It provides safe (thread safe against XCP modifications), lock-free and consistent atomic access to calibration parameters
/// It supports XCP/ECU independent page switching, checksum calculation, copy and reinitialization (copy reference page to working page)
/// @param name Name of the calibration segment.
/// @param default_page Pointer to the default page.
/// @param size Size of the calibration page in bytes.
/// @return a handle or XCP_UNDEFINED_CALSEG when out of memory or the name already exists.
tXcpCalSegIndex XcpCreateCalSeg(const char *name, const void *default_page, uint16_t size);

/// Create a calibration value and add it to the list of calibration segments.
/// This calibration segment has a working page (RAM) and a reference page (FLASH) (controlled with XcpSetCalPage CAL_PAGE_MODE_ALL)
/// This calibration segment has no MEMORY_SEGMENT in the A2L file
/// It provides safe (thread safe against XCP modifications), lock-free and consistent atomic access to calibration parameters
/// @param name Name of the calibration segment.
/// @param default_page Pointer to the default page.
/// @param size Size of the calibration page in bytes.
/// @return a handle or XCP_UNDEFINED_CALSEG when out of memory or the name already exists.
tXcpCalSegIndex XcpCreateCalBlk(const char *name, const void *default_page, uint16_t size);

/// Get the number of calibration segments
/// @return the number of calibration segments and blocks
uint16_t XcpGetCalSegCount(void);

/// Find a calibration segment by name
/// @param name Name of the calibration segment or block
/// @return the Handle of the calibration segment or XCP_UNDEFINED_CALSEG if not found
tXcpCalSegIndex XcpFindCalSeg(const char *name);

/// Get the name of the calibration segment
/// @param index Handle of the calibration segment or block
/// @return the name of the calibration segment or NULL if the index is invalid.
const char *XcpGetCalSegName(tXcpCalSegIndex index);

/// Get the size of the calibration segment
/// @param calseg Handle of the calibration segment or block
/// @return the size of the calibration segment in bytes
uint16_t XcpGetCalSegSize(tXcpCalSegIndex calseg);

/// Get the number of the calibration segment
/// @param calseg Handle of the calibration segment
/// @return the number of the calibration segment, calibration blocks don't have a number and return XCP_UNDEFINED_CALSEG_NUM
tXcpCalSegNumber XcpGetCalSegNumber(tXcpCalSegIndex calseg);

/// Lock a calibration segment.
/// @param index Calibration segment index.
/// @return Pointer to the active page of the calibration segment (working page or reference page, controlled by the XCP client tool).
/// The pointer is valid until the calibration segment is unlocked.
/// The data can be safely accessed while the lock is held.
/// There is no contention with the XCP client tool and with other threads acquiring the lock.
/// Acquiring the lock is wait-free, locks may be recursive
const uint8_t *XcpLockCalSeg(tXcpCalSegIndex index);

/// Unlock a calibration segment
uint8_t XcpUnlockCalSeg(tXcpCalSegIndex index);

/// Set all calibration segments to their default page
/// Maybe used in emergency situation
/// @return true on success, otherwise false
bool XcpResetAllCalSegs(void);

/// Writes current working page data to an existing persistence file
/// The working page calibration data, will become the default page content of the next session
/// @return true on success
bool XcpFreeze(void);

/// Create the binary persistence file with the current working pages as default pages
/// @param epk The EPK string for verification
/// @return true on success
bool XcpBinWrite(const char *epk);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Section-based calibration segment registration

#ifndef __CAL_H__

#define XCP_CALSEG_TYPE_SEGMENT 0x8001
#define XCP_CALSEG_TYPE_BLOCK 0x8002

// Calibration segment or block descriptor used for section-based pre-registration.
typedef struct {
    const char *name;
    const void *addr;        // pointer to static lifetime default page
    tXcpCalSegIndex *indexp; // pointer to the index variable initialized at runtime
    uint16_t size;
    uint16_t type; // XCP_CALSEG_TYPE_SEGMENT or XCP_CALSEG_TYPE_BLOCK
    uint8_t res[32 - sizeof(char *) - sizeof(void *) - sizeof(tXcpCalSegIndex *) - 2 - 2];
} tXcpCalSegDescriptor;

static_assert(sizeof(tXcpCalSegDescriptor) == 32, "sizeof(XcpCalDescriptor) must be 32");
static_assert(sizeof(((tXcpCalSegDescriptor *)0)->res) > 0, "tXcpCalSegDescriptor res padding must not be zero; check pointer sizes vs struct layout");

// Platform section attribute for tXcpCalSegDescriptor static variables created by CalSegCreate() and CalBlkCreate().
// Placing all descriptors in a named ELF/Mach-O section lets XcpInit() iterate them and
// pre-register every calibration segment or block before the first use, without requiring the call site of the creation to execute first.
#if defined(__ELF__)
#define XCP_CAL_SECTION_ATTR __attribute__((section("xcp_cals"), used))
#elif defined(__APPLE__)
#define XCP_CAL_SECTION_ATTR __attribute__((section("__DATA,xcp_cals"), used))
#else
#define XCP_CAL_SECTION_ATTR /* section-based registration not supported on this platform */
#endif

#endif // __CAL_H__

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Macros to create and access calibration segments or blocks
#ifndef __cplusplus

/// Global, lazy definition of a calibration segment or block: only registers a static tXcpCalSegDescriptor in the
/// xcp_cals linker section. It does NOT call XcpCreateCalSeg/XcpCreateCalBlk itself - calseg_id_##name stays
/// XCP_UNDEFINED_CALSEG until XcpInit() pre-registers all xcp_cals descriptors via XcpRegisterSectionCalSegs()
/// (see src/xcplite.c). Because of this, the segment is only usable after XcpInit() has run, and the macro itself
/// must be used at file/global scope, so the static descriptor exists at link time regardless of whether its call
/// site ever executes. For the equivalent macro that also creates the segment immediately (usable inside a function
/// or loop, without depending on XcpInit()'s section scan), see CalSegCreate below.
/// Name given as identifier, type name and segment name must be identical
/// @param name given as identifier, &name is expected to be the const static lifetime pointer to the default page, sizeof(name) is used as size of the calibration segment
// calseg__##name and calblk__##name are the linker map file markers for calibration segments and blocks
#define CalSegDecl(calseg_name)                                                                                                                                                    \
    static tXcpCalSegIndex calseg_id_##calseg_name = XCP_UNDEFINED_CALSEG;                                                                                                         \
    static const tXcpCalSegDescriptor calseg__##calseg_name XCP_CAL_SECTION_ATTR = {                                                                                               \
        .name = #calseg_name, .addr = (const void *)&(calseg_name), .indexp = &calseg_id_##calseg_name, .size = sizeof(calseg_name), .type = XCP_CALSEG_TYPE_SEGMENT};
#define CalBlkDecl(calblk_name)                                                                                                                                                    \
    static tXcpCalSegIndex calblk_id_##calblk_name = XCP_UNDEFINED_CALSEG;                                                                                                         \
    static const tXcpCalSegDescriptor calblk__##calblk_name XCP_CAL_SECTION_ATTR = {                                                                                               \
        .name = #calblk_name, .addr = (const void *)&(calblk_name), .indexp = &calblk_id_##calblk_name, .size = sizeof(calblk_name), .type = XCP_CALSEG_TYPE_BLOCK};

/// Dynamic, self-sufficient creation of a calibration segment or block: registers the same xcp_cals section
/// descriptor as CalSegDecl/CalBlkDecl above, but additionally calls XcpCreateCalSeg/XcpCreateCalBlk immediately if
/// not already created. Whichever runs first - this call site or XcpInit()'s section scan - creates the segment;
/// the other one then finds it already registered by name and just reuses its index (see XcpRegisterSectionCalSegs()
/// in src/cal.c). This makes the macro usable anywhere, including inside a function body or loop, without relying
/// on XcpInit() having run yet.
/// Note for readers of xcplib.hpp: the C++ macro of the same name, CalSegCreate(value), is unrelated - it is an
/// expression (not a statement) that always creates the segment immediately via xcp::CalSeg<T>'s constructor and
/// does not register a section descriptor at all.
/// Name given as identifier, type name and segment name must be identical
/// @param name given as identifier, &name is expected to be the const static lifetime pointer to the default page, sizeof(name) is used as size of the calibration segment
// calseg__##name and calblk__##name are the linker map file markers for calibration segments and blocks
#define CalSegCreate(calseg_name)                                                                                                                                                  \
    static tXcpCalSegIndex calseg_id_##calseg_name = XCP_UNDEFINED_CALSEG;                                                                                                         \
    static const tXcpCalSegDescriptor calseg__##calseg_name XCP_CAL_SECTION_ATTR = {                                                                                               \
        .name = #calseg_name, .addr = (void *)&(calseg_name), .indexp = &calseg_id_##calseg_name, .size = sizeof(calseg_name), .type = XCP_CALSEG_TYPE_SEGMENT};                   \
    if (calseg_id_##calseg_name == XCP_UNDEFINED_CALSEG) {                                                                                                                         \
        calseg_id_##calseg_name = XcpCreateCalSeg(#calseg_name, (uint8_t *)&(calseg_name), sizeof(calseg_name));                                                                   \
    }
#define CalBlkCreate(calblk_name)                                                                                                                                                  \
    static tXcpCalSegIndex calblk_id_##calblk_name = XCP_UNDEFINED_CALSEG;                                                                                                         \
    static const tXcpCalSegDescriptor calblk__##calblk_name XCP_CAL_SECTION_ATTR = {                                                                                               \
        .name = #calblk_name, .addr = (void *)&(calblk_name), .indexp = &calblk_id_##calblk_name, .size = sizeof(calblk_name), .type = XCP_CALSEG_TYPE_BLOCK};                     \
    if (calblk_id_##calblk_name == XCP_UNDEFINED_CALSEG) {                                                                                                                         \
        calblk_id_##calblk_name = XcpCreateCalBlk(#calblk_name, (uint8_t *)&(calblk_name), sizeof(calblk_name));                                                                   \
    }

/// Lock calibration segment macro
/// Calibration segment descriptor must be visible in scope
/// @param name given as identifier
#define CalSegLock(name) ((const __typeof__(name) *)XcpLockCalSeg(calseg_id_##name))
#define CalBlkLock(name) ((const __typeof__(name) *)XcpLockCalSeg(calblk_id_##name))

/// Unlock calibration segment macro
/// @param name given as identifier
#define CalSegUnlock(name) XcpUnlockCalSeg(calseg_id_##name)
#define CalBlkUnlock(name) XcpUnlockCalSeg(calblk_id_##name)

#endif // __cplusplus

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Dynamic DAQ event creation

/// Undefined event id
#define XCP_UNDEFINED_EVENT_ID 0xFFFF
/// DAQ event id as handle
typedef uint16_t tXcpEventId;

#ifdef OPTION_DAQ_EVENT_LIST

/// Add a measurement event to the event list, returns the event id  (0..XCP_MAX_EVENT_COUNT-1)
/// If the event name already exists, returns the existing event event number
/// Function is thread safe by using a mutex for event list access.
/// @param name Name of the event.
/// @param cycle_time_ns Cycle time in nanoseconds. 0 means sporadic event.
/// @param priority Priority of the event. 0 means normal, >=1 means realtime.
/// @return The event id or XCP_UNDEFINED_EVENT_ID if out of event list memory.
tXcpEventId XcpCreateEvent(const char *name, uint32_t cycle_time_ns /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/);

/// Add a measurement event to event list, return event id (0..XCP_MAX_EVENT_COUNT-1)
/// If the event name exists, a new event instance index is generated (will be the postfix of the event name in the A2L file)
/// Function is thread safe by using a mutex for event list access.
/// @param name Name of the event.
/// @param cycle_time_ns Cycle time in nanoseconds. 0 means sporadic event.
/// @param priority Priority of the event. 0 means normal, >=1 means realtime.
/// @return The event id or XCP_UNDEFINED_EVENT_ID if out of memory.
tXcpEventId XcpCreateEventInstance(const char *name, uint32_t cycle_time_ns /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/);

#endif // OPTION_DAQ_EVENT_LIST

/// Get event id by name, returns XCP_UNDEFINED_EVENT_ID if not found
/// @param name Name of the event.
/// @return The event id or XCP_UNDEFINED_EVENT_ID if not found.
/// If multiple events instances with the same name exist, the first one is returned.
tXcpEventId XcpFindEvent(const char *name);

/// Get the event instance index (1..)
/// @param event Event id.
/// @return The event index (1..), or 0 if no indexed event instance found.
uint16_t XcpGetEventIndex(tXcpEventId event);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Dynamic or linktime named DAQ event creation

// Note on thread safety of the once patterns for dynamic event creation using static state instead of thread local state:
// The XcpCreateEventXxx functions are thread safe by using a mutex for event list access and there are atomic aquire/release operations on event count to handle event visibility
// Using a static non atomic variable to check the once state, has no considerable risk of reading torn values on a >=32 microprocessor architecture
// The existing race condition is irrelevant

// Needs thread local storage
#ifndef THREAD_LOCAL
#ifdef __cplusplus
#define THREAD_LOCAL thread_local
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define THREAD_LOCAL _Thread_local
#elif defined(__GNUC__)
#define THREAD_LOCAL __thread
#elif defined(_MSC_VER)
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL static // Fallback to static (not thread-safe)
#error "Thread-local storage not supported"
#endif
#endif // THREAD_LOCAL

// Attribute for variables which are used only in some configurations or only by some of the instrumentation macros.
#ifndef XCP_MAYBE_UNUSED
#if defined(__cplusplus) && (__cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L))
#define XCP_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
#define XCP_MAYBE_UNUSED __attribute__((unused))
#else
#define XCP_MAYBE_UNUSED
#endif
#endif // XCP_MAYBE_UNUSED

// Event descriptor used by DaqCreateEvent() for section-based pre-registration
#ifndef __XCPLITE_H__ // Public API header guard

typedef struct {
    const char *name;
    uint32_t cycle_time_ns;
    uint8_t priority;
    uint8_t res[16 - sizeof(char *) - 4 - 1];
} tXcpEventDescriptor;

// Positional initializer (field order as above). Designated initializers would be a C++20 extension
// and are reported by -pedantic when this header is compiled as C++17 (e.g. consumers via FetchContent).
#define XCP_EVENT_DESCRIPTOR_INIT(name_str, cycle_ns, prio) {(name_str), (cycle_ns), (prio), {0}}
static_assert(sizeof(tXcpEventDescriptor) == 16, "Size of tXcpEventDescriptor must be 16 bytes for correct section parsing in xcpclient tool");
static_assert(sizeof(((tXcpEventDescriptor *)0)->res) > 0, "tXcpEventDescriptor res padding must not be zero; check pointer size vs struct layout");

// Linker-synthesized section boundary symbols, resolved at link time
#if defined(__ELF__)
extern const tXcpEventDescriptor __start_xcp_evts[] __attribute__((weak));
extern const tXcpEventDescriptor __stop_xcp_evts[] __attribute__((weak));
#elif defined(__APPLE__)
extern const tXcpEventDescriptor __start_xcp_evts[] __asm("section$start$__DATA$xcp_evts");
extern const tXcpEventDescriptor __stop_xcp_evts[] __asm("section$end$__DATA$xcp_evts");
#elif defined(_MSC_VER)
#define __start_xcp_evts ((const tXcpEventDescriptor *)NULL)
#define __stop_xcp_evts ((const tXcpEventDescriptor *)NULL)
#else
#error "Unsupported platform for section based event pre-registration"
#endif

#endif // __XCPLITE_H__

// Platform section attribute for tXcpEventDescriptor const static variables created by DaqCreateEvent().
// Placing all descriptors in a named ELF/Mach-O section lets XcpInit() iterate them and
// pre-register every event before the first trigger, without requiring the call site of the event creation to execute first.
#if defined(__ELF__)
#define XCP_EVENT_SECTION_ATTR __attribute__((section("xcp_evts"), used))
#elif defined(__APPLE__)
#define XCP_EVENT_SECTION_ATTR __attribute__((section("__DATA,xcp_evts"), used))
#else
#define XCP_EVENT_SECTION_ATTR /* section-based registration not supported on this platform */
#endif

// Attribute for functions which trigger an event and measure their local variables: such a function must not be inlined.
// An inlined function has a copy with its own stack frame at each call site, there is no stack frame relative address which is
// valid for all copies. The offline A2L generator (xcpclient) does not register the local variables of an inlined function, see docs/OFFLINE_A2L.md
#if defined(__GNUC__) || defined(__clang__)
#define XCP_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define XCP_NOINLINE __declspec(noinline)
#else
#define XCP_NOINLINE
#endif

// Link-time event id derived from the descriptor's position in the xcp_evts section
// Only with clang on Linux, this is a link-time constant, usable as a static initializer
#if defined(__ELF__) || defined(__APPLE__)
#if defined(__clang__) && defined(_LINUX)
// Get the event id as compile-time constant for an event descriptor name (evt__<event_name>)
#define XCP_EVENT_SECTION_GET_LINKTIME_ID(evt) ((tXcpEventId)(&(evt) - __start_xcp_evts))
// Set the event id for an event descriptor at runtime not needed, the link-time id is already set
#define XCP_EVENT_SECTION_SET_ID(evt_descr, evt_id) (void)(evt_id)
#else
// With other compilers, the event id is not a compile-time constant, but a link-time constant, so it can be used as static initializer
// Get the event id as compile-time constant for an event descriptor not possible
#define XCP_EVENT_SECTION_GET_LINKTIME_ID(evt) XCP_UNDEFINED_EVENT_ID
// Set the event id for an event descriptor at runtime
#define XCP_EVENT_SECTION_SET_ID(evt_descr, evt_id) ((evt_id) = ((tXcpEventId)(&(evt_descr) - __start_xcp_evts)))
#endif
#else
#ifdef OPTION_DAQ_EVENT_LIST
// Use dynamic event creation, no compile-time or link-time event id available
#define XCP_EVENT_SECTION_GET_LINKTIME_ID(evt) XCP_UNDEFINED_EVENT_ID
#define XCP_EVENT_SECTION_SET_ID(evt_descr, evt_id)                                                                                                                                \
    if ((evt_id) == XCP_UNDEFINED_EVENT_ID) {                                                                                                                                      \
        (evt_id) = XcpCreateEvent((evt_descr).name, 0, 0);                                                                                                                         \
    }
#else
#error "This platform does not support link-time event id generation, please enable OPTION_DAQ_EVENT_LIST"
#endif
#endif

/// Create an event
/// Depending on option OPTION_DAQ_EVENT_LIST defined, events are created at runtime or otherwise at link time
/// Dynamic event management (defined(OPTION_DAQ_EVENT_LIST)):
///    On platforms with ELF linker, the macro emits a static const tXcpEventDescriptor in the xcp_evts section, which is scanned at runtime by XcpInit() to create the event and
///    set the event id Otherwise the event is created at the call site with a once execution pattern
/// Static event management (!defined(OPTION_DAQ_EVENT_LIST)):
///    Requires a platform with ELF linker
///    Event descriptor and event id is created at link time
/// Event cycle time is set to sporadic and priority to normal
/// Setting the cycle time would only have the benefit for the XCP client tool to estimate the expected data rate of a DAQ setup
/// To create an XCP event with increased priority or specified expected cycle time, use DaqCreateEventExt
/// @param name Name given as identifier
#define DaqCreateEvent(event_name)                                                                                                                                                 \
    static const tXcpEventDescriptor evt__##event_name XCP_EVENT_SECTION_ATTR = XCP_EVENT_DESCRIPTOR_INIT(#event_name, 0, 0);                                                      \
    XCP_MAYBE_UNUSED static tXcpEventId evt_id_##event_name = XCP_EVENT_SECTION_GET_LINKTIME_ID(evt__##event_name);                                                                \
    XCP_EVENT_SECTION_SET_ID(evt__##event_name, evt_id_##event_name);

/// Create an event with given expected cycle time and priority
/// @param name Name given as identifier
/// @param cycle_time Cycle time in microseconds (0 = sporadic)
/// @param priority Priority of the event (0 = normal, >=1 = realtime)
#define DaqCreateEventExt(event_name, cycle, prio)                                                                                                                                 \
    static const tXcpEventDescriptor evt__##event_name XCP_EVENT_SECTION_ATTR = XCP_EVENT_DESCRIPTOR_INIT(#event_name, (cycle) * 1000U, (prio));                                   \
    XCP_MAYBE_UNUSED static tXcpEventId evt_id_##event_name = XCP_EVENT_SECTION_GET_LINKTIME_ID(evt__##event_name);                                                                \
    XCP_EVENT_SECTION_SET_ID(evt__##event_name, evt_id_##event_name);

#ifdef OPTION_DAQ_EVENT_LIST

/// Create an event
/// Thread local once execution pattern, a new event is created only once per thread, subsequent calls are ignored
/// The first call in a thread creates the event, event must be unique per thread and per code location
/// Calling again in the same thread is ignored, even if the the event name is different
/// @param name Name given as string
#define DaqCreateEvent_s(event_name)                                                                                                                                               \
    {                                                                                                                                                                              \
        static THREAD_LOCAL tXcpEventId evt__dynname = XCP_UNDEFINED_EVENT_ID;                                                                                                     \
        if (XcpIsActivated()) {                                                                                                                                                    \
            if (evt__dynname == XCP_UNDEFINED_EVENT_ID) {                                                                                                                          \
                evt__dynname = XcpCreateEvent(event_name, 0, 0);                                                                                                                   \
            }                                                                                                                                                                      \
        }                                                                                                                                                                          \
    }

/// Create a multi instance event
/// No once pattern, a new event instance is created for each call
/// If the name exists, an incrementing instance index is generated and appended to the event name (<name>_<instance_index>)
/// @param name Name given as identifier
#define DaqCreateEventInstance(event_name)                                                                                                                                         \
    static tXcpEventId evt__##event_name = XCP_UNDEFINED_EVENT_ID;                                                                                                                 \
    if (XcpIsActivated()) {                                                                                                                                                        \
        evt__##event_name = XcpCreateEventInstance(#event_name, 0, 0);                                                                                                             \
    }

/// Get event instance id
/// @param name Name given as identifier
#define DaqGetEventInstanceId(event_name) evt__##event_name

#endif // OPTION_DAQ_EVENT_LIST

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// DAQ event trigger measurement instrumentation point

/// Trigger timestamped measurement events and transfer XCP tool configured associated data
/// Function are thread safe and look-free, depending on the transmit queue configuration and platform. See technical reference for details.

/// Trigger the XCP event 'event' for absolute addressing mode (XCP_ADDR_EXT_ABS)
/// @param event Event id.
void XcpEvent(tXcpEventId event);

/// Trigger the XCP event 'event' for absolute or dyn addressing mode with explicitly given base address (address extension = 2)
/// @param event
/// @param base address pointer
void XcpEventExt(tXcpEventId event, const uint8_t *base2);

/// Trigger the XCP event 'event' for absolute or relative addressing mode with explicitly given base addresses for multiple relative addressing modes (address extensions = [2..])
/// @param event
/// @param count Number of base address pointers passed
void XcpEventExt_Var(tXcpEventId event, int count, ...);

// At timestamp variants (clock==0 -> same as non timestamped versions)
void XcpEventAt(tXcpEventId event, uint64_t clock);
void XcpEventExtAt(tXcpEventId event, const uint8_t *base2, uint64_t clock);
void XcpEventExtAt_(tXcpEventId event, int count, const uint8_t **bases, uint64_t clock); // Used by variadic C++ macro/template
void XcpEventExtAt_Var(tXcpEventId event, uint64_t clock, int count, ...);

// Enable or disable a XCP DAQ event
void XcpEventEnable(tXcpEventId event, bool enable);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Get stack frame pointer
// Used by the Daq and A2l macros to get the stack frame pointer for stack relative addressing mode

// Offset added to the frame pointer to get the base address for stack relative addressing mode
// This defines the maximum stack frame size which can be accessed
#define XCP_FRAME_ADDR_OFFSET 0x10000

// The frame address must be the frame base which the compiler uses in the DWARF locations of the local variables (DW_AT_frame_base),
// the offline A2L generator (xcpclient) takes the variable offsets from there without any further correction:
// - clang describes the local variables relative to the frame pointer register, __builtin_frame_address(0) is the frame pointer and
//   forces the function to keep one
// - GCC describes the local variables relative to the canonical frame address (CFA, DW_OP_call_frame_cfa), __builtin_dwarf_cfa() is
//   the CFA on every architecture and does not force a frame pointer
// The on-target A2L generation uses the same macro for the registration and for the trigger, any consistent value works there
#if defined(__clang__)

#define xcp_get_frame_addr() (const uint8_t *)((uint8_t *)__builtin_frame_address(0) - XCP_FRAME_ADDR_OFFSET)

#elif defined(__GNUC__)

#define xcp_get_frame_addr() (const uint8_t *)((uint8_t *)__builtin_dwarf_cfa() - XCP_FRAME_ADDR_OFFSET)

// MSVC compiler
#elif defined(_MSC_VER)

#if defined(_M_X64)

// x64 architecture - inline assembly not supported by MSVC x64, use _AddressOfReturnAddress intrinsic
#include <intrin.h>
#pragma intrinsic(_AddressOfReturnAddress)
static __forceinline const uint8_t *xcp_get_frame_addr(void) {
    // _AddressOfReturnAddress() returns the address where the return address is stored (RSP + offset to return address)
    // The saved RBP is stored at [RSP + offset to saved RBP] in the function prologue
    // With /Oy- (frame pointer not omitted), this provides a consistent reference point
    void **return_addr_ptr = (void **)_AddressOfReturnAddress();
    // The saved frame pointer is typically at return_addr_ptr - 1
    // This gives us a consistent base address for stack-relative addressing
    return (const uint8_t *)(return_addr_ptr - 1 - (XCP_FRAME_ADDR_OFFSET / sizeof(void *)));
}
#else
#error "Unsupported MSVC architecture for frame pointer detection"
#endif

// Other compilers
#else
#error "xcp_get_frame_addr is not defined for this compiler. Please implement it."
#endif

// Prevent the compiler from turning the preceding call into a tail call (sibling call optimization)
// Used after every call which passes xcp_get_frame_addr(): if this is the last statement of a function, the compiler may
// replace it with a jump and release the stack frame of the function first. The library would then copy the local variables
// from a released frame, which the called function already reuses. Applications which call XcpEventExt_Var(), XcpEventExt() or
// directly with xcp_get_frame_addr() must add XCP_NO_TAIL_CALL() after the call themselves, or make sure the
// call is not the last statement of the function!!
#if defined(__GNUC__) || defined(__clang__)
#define XCP_NO_TAIL_CALL() __asm__ __volatile__("" ::: "memory")
#elif defined(_MSC_VER)
#define XCP_NO_TAIL_CALL() __nop() // intrin.h, an instruction after the call keeps it from becoming a tail call
#else
#define XCP_NO_TAIL_CALL() ((void)0)
#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Absolute addressing mode

const uint8_t *ApplXcpGetBaseAddr(void);      // Get base for the XCP address range in absolute addressing mode
void ApplXcpSetBaseAddr(const uint8_t *addr); // Set base for absolute addressing mode, only needed for special cases where the default base addr is not suitable
const uint8_t *ApplXcpGetModuleAddr(void);    // Get the default base address, used as default base address for absolute addressing mode
uint32_t ApplXcpGetAddr(const uint8_t *p);    // Get the absolute XCP/A2L 32 bit address from a pointer
uint8_t ApplXcpGetAddrExt(const uint8_t *p);  // Get the absolute XCP/A2L 8 bit address extension from a pointer
extern const uint8_t *gXcpBaseAddr;
#define xcp_get_base_addr() gXcpBaseAddr // For runtime optimization, use xcp_get_base_addr() instead of ApplXcpGetBaseAddr()

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// DAQ event trigger convenience macros

// Event name parameter is a symbol, a string (_s) or an event index tXcpEventIndex (_i)
// Creates linker map file markers (static variables: trg__xxxx_'eventname' )
// If needed, uses local scope static or thread local storage to create a once pattern for the event lookup to save runtime overhead
// All macros can be used to measure variables registered in absolute addressing mode as well
// Note that XCP_EVENT_SECTION_SET_ID expands to nothing on platforms where the event id is a link-time constant
// A function which triggers an event and measures its local variables (stack relative addressing) must not be inlined, mark it XCP_NOINLINE

// @@@@ TODO: Not all permutations of name, string, index with At implemented

/// Trigger the global XCP event 'name' for stack relative or absolute addressing AAS
/// @param name Name given as identifier
#define DaqTriggerEvent(event_name)                                                                                                                                                \
    {                                                                                                                                                                              \
        static tXcpEventId trg__AAS__##event_name = XCP_EVENT_SECTION_GET_LINKTIME_ID(evt__##event_name);                                                                          \
        XCP_EVENT_SECTION_SET_ID(evt__##event_name, trg__AAS__##event_name);                                                                                                       \
        XcpEventExt_Var(trg__AAS__##event_name, 1, xcp_get_frame_addr());                                                                                                          \
        XCP_NO_TAIL_CALL();                                                                                                                                                        \
    }
#define DaqTriggerEventAt(event_name, clock)                                                                                                                                       \
    {                                                                                                                                                                              \
        static tXcpEventId trg__AAS__##event_name = XCP_EVENT_SECTION_GET_LINKTIME_ID(evt__##event_name);                                                                          \
        XCP_EVENT_SECTION_SET_ID(evt__##event_name, trg__AAS__##event_name);                                                                                                       \
        XcpEventExtAt_Var(trg__AAS__##event_name, clock, 1, xcp_get_frame_addr());                                                                                                 \
        XCP_NO_TAIL_CALL();                                                                                                                                                        \
    }

/// Trigger the XCP event by handle 'tXcpEventId event_id' for stack relative or absolute addressing AAS
/// No lookup overhead, event id must be valid
/// @param name Event given as id
#define DaqTriggerEvent_i(event_id)                                                                                                                                                \
    {                                                                                                                                                                              \
        static tXcpEventId trg__AAS = XCP_UNDEFINED_EVENT_ID;                                                                                                                      \
        (void)trg__AAS;                                                                                                                                                            \
        XcpEventExt(event_id, xcp_get_frame_addr());                                                                                                                               \
        XCP_NO_TAIL_CALL();                                                                                                                                                        \
    }
#define DaqTriggerEventAt_i(event_id, clock)                                                                                                                                       \
    {                                                                                                                                                                              \
        static tXcpEventId trg__AAS = XCP_UNDEFINED_EVENT_ID;                                                                                                                      \
        (void)trg__AAS;                                                                                                                                                            \
        XcpEventExtAt(event_id, xcp_get_frame_addr(), clock);                                                                                                                      \
        XCP_NO_TAIL_CALL();                                                                                                                                                        \
    }

/// Trigger the XCP event 'name' for absolute, stack and relative addressing mode AASD with a single given individual base address (from A2lSetRelativeAddrMode(base_addr))
/// @param name Name given as identifier
/// @param base_addr Base address pointer for relative addressing mode
#define DaqTriggerEventExt(event_name, base_addr)                                                                                                                                  \
    {                                                                                                                                                                              \
        static tXcpEventId trg__AASD__##event_name = XCP_UNDEFINED_EVENT_ID;                                                                                                       \
        XCP_EVENT_SECTION_SET_ID(evt__##event_name, trg__AASD__##event_name);                                                                                                      \
        XcpEventExt_Var(trg__AASD__##event_name, 2, xcp_get_frame_addr(), (const uint8_t *)(base_addr));                                                                           \
        XCP_NO_TAIL_CALL();                                                                                                                                                        \
    }

/// Trigger the XCP event 'name' for absolute, stack and relative addressing mode with given individual base address (from A2lSetRelativeAddrMode(base_addr))
/// Cache the one time event lookup in thread local storage, can be called with different names in the same code location in different threads
/// @param name Name given as string, must be unique per thread and code location
/// @param base_addr Base address pointer for relative addressing mode
#define DaqTriggerEventExt_s(event_name, base_addr)                                                                                                                                \
    {                                                                                                                                                                              \
        static THREAD_LOCAL tXcpEventId trg__AASD__##event_name = XCP_UNDEFINED_EVENT_ID;                                                                                          \
        if (XcpIsActivated()) {                                                                                                                                                    \
            if (trg__AASD__##event_name == XCP_UNDEFINED_EVENT_ID) {                                                                                                               \
                trg__AASD__##event_name = XcpFindEvent(event_name);                                                                                                                \
            }                                                                                                                                                                      \
            XcpEventExt_Var(trg__AASD__##event_name, 2, xcp_get_frame_addr(), (const uint8_t *)(base_addr));                                                                       \
            XCP_NO_TAIL_CALL();                                                                                                                                                    \
        }                                                                                                                                                                          \
    }

/// Trigger the XCP event by handle 'event_id' for absolute, stack and relative addressing mode with given individual base address (from A2lSetRelativeAddrMode(base_addr))
/// No lookup overhead, event id must be valid
/// @param event_id Event given as id
/// @param base_addr Base address pointer for relative addressing mode
#define DaqTriggerEventExt_i(event_id, base_addr)                                                                                                                                  \
    {                                                                                                                                                                              \
        static tXcpEventId trg__AASD = XCP_UNDEFINED_EVENT_ID;                                                                                                                     \
        (void)trg__AASD;                                                                                                                                                           \
        XcpEventExt_Var(event_id, 2, xcp_get_frame_addr(), (const uint8_t *)(base_addr));                                                                                          \
        XCP_NO_TAIL_CALL();                                                                                                                                                        \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Combined create and trigger DAQ event macros

/// Create and trigger the global XCP event 'name' for stack relative or absolute addressing
/// The descriptor is placed in the xcp_evts section so XcpInit() pre-registers the event.
/// trg__AAS__##event_name is kept as a linker map marker for the trigger location (same role as in DaqTriggerEvent).
/// @param event_name Name given as identifier
#define DaqCreateAndTriggerEvent(event_name)                                                                                                                                       \
    {                                                                                                                                                                              \
        static const tXcpEventDescriptor evt__##event_name XCP_EVENT_SECTION_ATTR = XCP_EVENT_DESCRIPTOR_INIT(#event_name, 0, 0);                                                  \
        static tXcpEventId trg__AAS__##event_name = XCP_EVENT_SECTION_GET_LINKTIME_ID(evt__##event_name);                                                                          \
        XCP_EVENT_SECTION_SET_ID(evt__##event_name, trg__AAS__##event_name);                                                                                                       \
        XcpEventExt_Var(trg__AAS__##event_name, 1, xcp_get_frame_addr());                                                                                                          \
        XCP_NO_TAIL_CALL();                                                                                                                                                        \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Capture of local variables

// A local variable which the compiler keeps in a register has no memory location and can not be measured. Instead of forcing it
// into memory for its whole lifetime with 'volatile', the capture macros copy the given variables into a struct on the stack when
// the event is triggered, and pass the address of that struct as the base address of address extension 3.
// The offline A2L generator (xcpclient) unfolds the struct and registers its members with the names of the original variables,
// see docs/OFFLINE_A2L.md. The originals stay in registers, the copy of a scalar is a single store instruction.
// The capture struct is alive while the event is triggered, which is when the XCP server reads it, synchronously for DAQ and
// asynchronously for polling (the pending command is executed in the trigger).
// Restrictions: up to XCP_CAPTURE_MAX_COUNT variables, each given as a plain identifier of a local variable, a parameter or a
// global variable. Bitfield members can not be captured, and const qualified variables only in C, in C++ a const member would
// leave the capture struct without a default constructor. Not available with MSVC.
// In C++ the captured objects must be trivially copyable, they are copied byte wise.
// A function with a capture may be inlined, unlike a function which measures its local variables on the stack.

#define XCP_CAPTURE_MAX_COUNT 16

#define XCP_CAP_CAT_(a, b) a##b
#define XCP_CAP_CAT(a, b) XCP_CAP_CAT_(a, b)

// Number of arguments (1 to XCP_CAPTURE_MAX_COUNT)
#define XCP_CAP_NARG(...) XCP_CAP_NARG_(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define XCP_CAP_NARG_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N

// Apply m(c, x) to each argument x, with the context c
#define XCP_CAP_FE_1(m, c, x) m(c, x)
#define XCP_CAP_FE_2(m, c, x, ...) m(c, x) XCP_CAP_FE_1(m, c, __VA_ARGS__)
#define XCP_CAP_FE_3(m, c, x, ...) m(c, x) XCP_CAP_FE_2(m, c, __VA_ARGS__)
#define XCP_CAP_FE_4(m, c, x, ...) m(c, x) XCP_CAP_FE_3(m, c, __VA_ARGS__)
#define XCP_CAP_FE_5(m, c, x, ...) m(c, x) XCP_CAP_FE_4(m, c, __VA_ARGS__)
#define XCP_CAP_FE_6(m, c, x, ...) m(c, x) XCP_CAP_FE_5(m, c, __VA_ARGS__)
#define XCP_CAP_FE_7(m, c, x, ...) m(c, x) XCP_CAP_FE_6(m, c, __VA_ARGS__)
#define XCP_CAP_FE_8(m, c, x, ...) m(c, x) XCP_CAP_FE_7(m, c, __VA_ARGS__)
#define XCP_CAP_FE_9(m, c, x, ...) m(c, x) XCP_CAP_FE_8(m, c, __VA_ARGS__)
#define XCP_CAP_FE_10(m, c, x, ...) m(c, x) XCP_CAP_FE_9(m, c, __VA_ARGS__)
#define XCP_CAP_FE_11(m, c, x, ...) m(c, x) XCP_CAP_FE_10(m, c, __VA_ARGS__)
#define XCP_CAP_FE_12(m, c, x, ...) m(c, x) XCP_CAP_FE_11(m, c, __VA_ARGS__)
#define XCP_CAP_FE_13(m, c, x, ...) m(c, x) XCP_CAP_FE_12(m, c, __VA_ARGS__)
#define XCP_CAP_FE_14(m, c, x, ...) m(c, x) XCP_CAP_FE_13(m, c, __VA_ARGS__)
#define XCP_CAP_FE_15(m, c, x, ...) m(c, x) XCP_CAP_FE_14(m, c, __VA_ARGS__)
#define XCP_CAP_FE_16(m, c, x, ...) m(c, x) XCP_CAP_FE_15(m, c, __VA_ARGS__)
#define XCP_CAP_FOR_EACH(m, c, ...) XCP_CAP_CAT(XCP_CAP_FE_, XCP_CAP_NARG(__VA_ARGS__))(m, c, __VA_ARGS__)

// One struct member per captured variable, with the type of the variable and its name with a trailing underscore. The trailing
// underscore is needed in C++: a name must not change its meaning within a class scope, and a member named like the variable in
// its own type expression does exactly that, GCC rejects it. The offline A2L generator removes the trailing underscore again and
// registers the member with the name of the variable, see docs/OFFLINE_A2L.md
#define XCP_CAP_MEMBER(c, x) __typeof__(x) x##_;

// Copy one variable into the capture struct. The casts avoid a discarded qualifier warning for a volatile variable, the builtin
// is expanded inline for the constant size, so the address of the variable does not force it into memory
#define XCP_CAP_COPY(c, x) __builtin_memcpy((void *)&(c).x##_, (const void *)&(x), sizeof(x));

// Declare the capture struct cap__<event_name> and fill it
#define XCP_CAPTURE(event_name, ...)                                                                                                                                               \
    struct {                                                                                                                                                                       \
        XCP_CAP_FOR_EACH(XCP_CAP_MEMBER, 0, __VA_ARGS__)                                                                                                                           \
    } cap__##event_name;                                                                                                                                                           \
    XCP_CAP_FOR_EACH(XCP_CAP_COPY, cap__##event_name, __VA_ARGS__)

/// Trigger the XCP event 'event_name' and capture the given local variables for measurement, AASR
/// @param event_name Name given as identifier, the event must exist (DaqCreateEvent)
/// @param ... The local variables to capture, plain identifiers, up to XCP_CAPTURE_MAX_COUNT
#define DaqTriggerEventCapture(event_name, ...)                                                                                                                                    \
    {                                                                                                                                                                              \
        XCP_CAPTURE(event_name, __VA_ARGS__)                                                                                                                                       \
        static tXcpEventId trg__AASR__##event_name = XCP_EVENT_SECTION_GET_LINKTIME_ID(evt__##event_name);                                                                         \
        XCP_EVENT_SECTION_SET_ID(evt__##event_name, trg__AASR__##event_name);                                                                                                      \
        XcpEventExt_Var(trg__AASR__##event_name, 2, xcp_get_frame_addr(), (const uint8_t *)&cap__##event_name);                                                                    \
        XCP_NO_TAIL_CALL();                                                                                                                                                        \
    }

/// Trigger the XCP event 'event_name' with a given timestamp and capture the given local variables for measurement, AASR
/// @param event_name Name given as identifier, the event must exist (DaqCreateEvent)
/// @param clock Timestamp of the event
/// @param ... The local variables to capture, plain identifiers, up to XCP_CAPTURE_MAX_COUNT
#define DaqTriggerEventCaptureAt(event_name, clock, ...)                                                                                                                           \
    {                                                                                                                                                                              \
        XCP_CAPTURE(event_name, __VA_ARGS__)                                                                                                                                       \
        static tXcpEventId trg__AASR__##event_name = XCP_EVENT_SECTION_GET_LINKTIME_ID(evt__##event_name);                                                                         \
        XCP_EVENT_SECTION_SET_ID(evt__##event_name, trg__AASR__##event_name);                                                                                                      \
        XcpEventExtAt_Var(trg__AASR__##event_name, clock, 2, xcp_get_frame_addr(), (const uint8_t *)&cap__##event_name);                                                           \
        XCP_NO_TAIL_CALL();                                                                                                                                                        \
    }

/// Create and trigger the XCP event 'event_name' and capture the given local variables for measurement, AASR
/// @param event_name Name given as identifier
/// @param ... The local variables to capture, plain identifiers, up to XCP_CAPTURE_MAX_COUNT
#define DaqCreateAndTriggerEventCapture(event_name, ...)                                                                                                                           \
    {                                                                                                                                                                              \
        XCP_CAPTURE(event_name, __VA_ARGS__)                                                                                                                                       \
        static const tXcpEventDescriptor evt__##event_name XCP_EVENT_SECTION_ATTR = XCP_EVENT_DESCRIPTOR_INIT(#event_name, 0, 0);                                                  \
        static tXcpEventId trg__AASR__##event_name = XCP_EVENT_SECTION_GET_LINKTIME_ID(evt__##event_name);                                                                         \
        XCP_EVENT_SECTION_SET_ID(evt__##event_name, trg__AASR__##event_name);                                                                                                      \
        XcpEventExt_Var(trg__AASR__##event_name, 2, xcp_get_frame_addr(), (const uint8_t *)&cap__##event_name);                                                                    \
        XCP_NO_TAIL_CALL();                                                                                                                                                        \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Enable/disable events

/// Enable the XCP event 'name'
#define DaqEventEnable(name)                                                                                                                                                       \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static THREAD_LOCAL tXcpEventId ena__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                      \
        if (ena__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                               \
            ena__##name = XcpFindEvent(#name);                                                                                                                                     \
        }                                                                                                                                                                          \
        XcpEventEnable(ena__##name, true);                                                                                                                                         \
    }

/// Disable the XCP event 'name'
#define DaqEventDisable(name)                                                                                                                                                      \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static THREAD_LOCAL tXcpEventId ena__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                      \
        if (ena__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                               \
            ena__##name = XcpFindEvent(#name);                                                                                                                                     \
        }                                                                                                                                                                          \
        XcpEventEnable(ena__##name, false);                                                                                                                                        \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Build time A2L file generation macros for metadata annotations

#if defined(__ELF__)
#define XCP_METADATA_SECTION_ATTR __attribute__((section("xcp_meta"), used))
#elif defined(__APPLE__)
#define XCP_METADATA_SECTION_ATTR __attribute__((section("__DATA,xcp_meta"), used))
#else
#define XCP_METADATA_SECTION_ATTR /* section-based registration not supported on this platform */
#ifndef _WIN32
#error "Unsupported platform for XCP metadata section"
#endif
#endif

// Avoid mangled names for C++ meta data symbols, use __asm__ to set the symbol name in the object file
// #define XCP_STRINGIFY_INNER(x) #x
// #define XCP_STRINGIFY(x) XCP_STRINGIFY_INNER(x)
// #if defined(__cplusplus) && (defined(__clang__) || defined(__GNUC__))
// #define XCP_COMMENT(name, comment) static const char XCP_METADATA_SECTION_ATTR xcp_meta__comment__##name[] __asm__("xcp_meta__comment__" XCP_STRINGIFY(name)) = comment;
// #define XCP_READ_WRITE(name) static const bool XCP_METADATA_SECTION_ATTR xcp_meta__read_write__##name[] __asm__("xcp_meta__read_write__" XCP_STRINGIFY(name)) = true;
// #define XCP_UNIT(name, unit) static const char XCP_METADATA_SECTION_ATTR xcp_meta__unit__##name[] __asm__("xcp_meta__unit__" XCP_STRINGIFY(name)) = unit;
// #define XCP_LIMITS(name, min, max)
//     static const double XCP_METADATA_SECTION_ATTR xcp_meta__min__##name __asm__("xcp_meta__min__" XCP_STRINGIFY(name)) = min;
//     static const double XCP_METADATA_SECTION_ATTR xcp_meta__max__##name __asm__("xcp_meta__max__" XCP_STRINGIFY(name)) = max
// #else
#define XCP_COMMENT(name, comment) static const char XCP_METADATA_SECTION_ATTR xcp_meta__comment__##name[] = comment;
#define XCP_READ_WRITE(name) static const bool XCP_METADATA_SECTION_ATTR xcp_meta__read_write__##name = true;
#define XCP_UNIT(name, unit) static const char XCP_METADATA_SECTION_ATTR xcp_meta__unit__##name[] = unit;
#define XCP_LIMITS(name, min, max)                                                                                                                                                 \
    static const double XCP_METADATA_SECTION_ATTR xcp_meta__min__##name = min;                                                                                                     \
    static const double XCP_METADATA_SECTION_ATTR xcp_meta__max__##name = max;
// #endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Measurement of local variables and function parameters without A2L runtime generation enabled

// Note on local variable and function parameter visibility:
// When runtime A2L generation is not used, the compiler may optimize local variables and function parameters to be stored in CPU registers only, without a memory location on the
// stack In this case, XCPlite can not measure these variables since there is no memory location to read from, reading the register value is not supported yet To prevent this
// optimization, the variable must be marked as 'volatile' to force the compiler to always read and write it from/to memory The XCP_MEAS macros mark a (local) variable
// as volatile for this purpose An alternative is to trigger the event with DaqTriggerEventCapture, which copies the variables into a capture struct on
// the stack and leaves the originals in their registers

// The A2L updater/creator in xcpclient can handle only simple location expressions such as absolute addresses, stack relative addresses (CFA) and calibration segment relative
// addresses For complex cases, use DaqTriggerEventCapture to capture the variables in a capture struct

/// Attribute to mark a local variable as measurable
/// Example usage: XCP_MEAS int32_t my_var = 0;
#define XCP_MEAS volatile

// Macro to force a function parameter to be stored on the stack
#define XCP_FORCE_TO_STACK(var) asm volatile("" ::"m"(var) : "memory")

// Compiler memory barrier to prevent reordering of memory accesses across this point
#define XCP_MEMORY_BARRIER() asm volatile("" ::: "memory")

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Misc

/* XCP command Return Codes */
#define CRC_CMD_OK 0x00
#define CRC_CMD_SYNCH 0x00
#define CRC_CMD_PENDING 0x01
#define CRC_CMD_IGNORED 0x02
#define CRC_CMD_BUSY 0x10
#define CRC_DAQ_ACTIVE 0x11
#define CRC_PGM_ACTIVE 0x12
#define CRC_CMD_UNKNOWN 0x20
#define CRC_CMD_SYNTAX 0x21
#define CRC_OUT_OF_RANGE 0x22
#define CRC_WRITE_PROTECTED 0x23
#define CRC_ACCESS_DENIED 0x24
#define CRC_ACCESS_LOCKED 0x25
#define CRC_PAGE_NOT_VALID 0x26
#define CRC_MODE_NOT_VALID 0x27
#define CRC_SEGMENT_NOT_VALID 0x28
#define CRC_SEQUENCE 0x29
#define CRC_DAQ_CONFIG 0x2A
#define CRC_MEMORY_OVERFLOW 0x30
#define CRC_GENERIC 0x31
#define CRC_VERIFY 0x32
#define CRC_RESOURCE_TEMPORARY_NOT_ACCESSIBLE 0x33
#define CRC_SUBCMD_UNKNOWN 0x34
#define CRC_TIMECORR_STATE_CHANGE 0x35

/// Set log level
/// Does not require XCP to be initialized yet
/// Log level 4 provides a trace of all XCP commands and responses.
/// @param level (0 = no logging, 1 = error, 2 = warning, 3 = info, 4 = debug, 5 = trace)
void XcpSetLogLevel(uint8_t level);

// Create the memory section for epk software version string, used for compatibility check of A2L and BIN file
#if defined(__ELF__)
#define XCP_EPK_SECTION_ATTR __attribute__((section("xcp_epk"), used))
#elif defined(__APPLE__)
#define XCP_EPK_SECTION_ATTR __attribute__((section("__DATA,xcp_epk"), used))
#else
#define XCP_EPK_SECTION_ATTR /* section-based registration not supported on this platform */
#endif
#define XcpCreateEpk(epk)                                                                                                                                                          \
    do {                                                                                                                                                                           \
        static char gXcpEpkString[] XCP_EPK_SECTION_ATTR = epk;                                                                                                                    \
        volatile char xcp_epk_keep = gXcpEpkString[0];                                                                                                                             \
        (void)xcp_epk_keep;                                                                                                                                                        \
    } while (0)

/// XcpInit mode flags
#define XCP_MODE_DEACTIVATE 0     ///< Initialize XCP singleton without activating the protocol layer (passive/off)
#define XCP_MODE_LOCAL 0x01       ///< Initialize and activate XCP, allocate state in static memory if libxcplite not compiled in SHM mode, otherwise allocate state in heap memory
#define XCP_MODE_PERSISTENCE 0x02 ///< Load the binary persistence file, to keep deterministic order of events and calibration segments, and load persisted calibration data
#define XCP_MODE_SHM 0x80         ///< Initialize and activate XCP, allocate state in POSIX shared memory
#define XCP_MODE_SHM_AUTO 0x04    ///< Set this flag to automatically choose leader as XCP server
#define XCP_MODE_SHM_SERVER 0x08  ///< Set this flag, to make this application the XCP server, regardless which application is started first

/// Initialize the XCP driver singleton, must be called before starting the server
/// @param name Project name, used as A2L file name and to identify the XCP server
/// @param epk EPK version string, used for compatibility check of A2L and BIN file
/// @param mode XCP_MODE_DEACTIVATE, XCP_MODE_LOCAL, XCP_MODE_SHM, XCP_MODE_SHM_AUTO or XCP_MODE_SHM_SERVER (libxcplite build with in SHM mode)
bool XcpInit(const char *name, const char *epk, uint8_t mode);
void XcpDeinit(void); // Internal function for Rust build.rs

/// Check if XCP has been activated
bool XcpIsActivated(void);

/// Get the mode passed to XcpInit()
/// @return XCP_MODE_DEACTIVATE, XCP_MODE_LOCAL, XCP_MODE_SHM, XCP_MODE_SHM_AUTO, or XCP_MODE_SHM_SERVER
uint8_t XcpGetInitMode(void);

/// Check if XCP is connected
bool XcpIsConnected(void);

/// Check if XCP library is compiled in shared memory mode
bool XcpIsInShmMode(void);

// Project name
const char *XcpGetProjectName(void);

// EPK software version identifier
const char *XcpGetEcuEpk(void); // Only in SHM mode different to XcpGetLocalEpk(), which is for the application, while XcpGetEcuEpk() is for the overall ECU
const char *XcpGetLocalEpk(void);

// A2L file name
// Notify XCPlite there is a valid A2L with this name to be provided for upload via XCP command GET_ID
void XcpSetA2lName(const char *name);
const char *XcpGetA2lName(void);

// ELF file name
// Notify XCPlite there is a valid ELF with this pathname to be provided for upload via XCP command GET_ID
void XcpSetElfName(const char *name);
const char *XcpGetElfName(void);

/// Force Disconnect
/// Stop DAQ, flush queue, flush pending calibrations
void XcpDisconnect(void);

/// Send terminate session event to the XCP client
/// Force the XCP client to terminate the session
void XcpSendTerminateSessionEvent(void);

/// Send a message to the XCP client
/// @param str Message to send, appears in the XCP client write log window
void XcpPrint(const char *str);

/// Get the current DAQ clock value
/// @return time in 1/CLOCK_TICKS_PER_S ticks
/// Resolution and epoch is defined in xcplib_cfg.h
/// Epoch may be PTP or arbitrary
/// Resolution is 1ns or 1us
uint64_t ApplXcpGetClock64(void);

// Callback
// Get clock synchronization state and grandmaster UUID
/// @return clock state
#define CLOCK_STATE_SYNCH_IN_PROGRESS (0)
#define CLOCK_STATE_SYNCH (1)
#define CLOCK_STATE_FREE_RUNNING (7)
#define CLOCK_STATE_GRANDMASTER_STATE_SYNCH (1 << 3) // not used yet
uint8_t ApplXcpGetClockState(void);

// Callback
// Return client and grandmaster clock uuid, stratum level and epoch
// @param uuid Pointer to 16 byte array to store the grandmaster UUID
// @param epoch Pointer to store the epoch
// @param stratum Pointer to store the stratum level
// @return true if PTP is available, otherwise XCP will assume an unsynchronized clock
#define CLOCK_STRATUM_LEVEL_UNKNOWN 255
#define CLOCK_STRATUM_LEVEL_ARB 16 // unsychronized
#define CLOCK_STRATUM_LEVEL_UTC 0  // Atomic reference clock
#define CLOCK_EPOCH_TAI 0          // Atomic monotonic time since 1.1.1970 (TAI)
#define CLOCK_EPOCH_UTC 1          // Universal Coordinated Time (with leap seconds) since 1.1.1970 (UTC)
#define CLOCK_EPOCH_ARB 2          // Arbitrary (epoch unknown)
bool ApplXcpGetClockInfoGrandmaster(uint8_t *client_uuid, uint8_t *grandmaster_uuid, uint8_t *epoch, uint8_t *stratum);

// Register clock callbacks
// If no callback is registered for a given hook, XCPlite falls back to its own default implementation (see src/xcpappl.c).

/// Override the source of ApplXcpGetClock64()'s value.
/// @param cb_get_clock returns the current clock in 1/CLOCK_TICKS_PER_S ticks; see ApplXcpGetClock64 above for resolution/epoch
void ApplXcpRegisterGetClockCallback(uint64_t (*cb_get_clock)(void));
/// Override the source of ApplXcpGetClockState()'s value.
/// @param cb_get_clock_state returns one of the CLOCK_STATE_* values above
void ApplXcpRegisterGetClockStateCallback(uint8_t (*cb_get_clock_state)(void));
/// Override the source of ApplXcpGetClockInfoGrandmaster()'s value.
/// @param cb_get_clock_info_grandmaster fills client_uuid/grandmaster_uuid/epoch/stratum as documented on
/// ApplXcpGetClockInfoGrandmaster above; returns true if PTP is available, otherwise XCP assumes an unsynchronized clock
void ApplXcpRegisterGetClockInfoGrandmasterCallback(bool (*cb_get_clock_info_grandmaster)(uint8_t *client_uuid, uint8_t *grandmaster_uuid, uint8_t *epoch, uint8_t *stratum));

// Register XCP callbacks
// If no callback is registered for a given hook, XCPlite falls back to its own default implementation (see src/xcpappl.c);
// most default implementations are permissive (e.g. connect always succeeds, memory checks always pass).

/// Called when an XCP client sends CONNECT.
/// @param cb_connect mode is the CONNECT command's mode byte (0 = normal); return true to accept the connection, false to reject it
void ApplXcpRegisterConnectCallback(bool (*cb_connect)(uint8_t mode));
/// Called before a requested DAQ start is applied, giving the application a chance to veto it (XCP protocol layer >= 1.4 only).
/// @param cb_prepare_daq return false to cancel the DAQ start, true to allow it
void ApplXcpRegisterPrepareDaqCallback(uint8_t (*cb_prepare_daq)(void));
/// Called once DAQ has actually started running (after PrepareDaq, if applicable).
/// @param cb_start_daq no parameters, no meaningful return value
void ApplXcpRegisterStartDaqCallback(uint8_t (*cb_start_daq)(void));
/// Called once DAQ has stopped.
/// @param cb_stop_daq no parameters
void ApplXcpRegisterStopDaqCallback(void (*cb_stop_daq)(void));
/// Reserved for persisting/clearing the DAQ list configuration (XCP SET_REQUEST command's STORE_DAQ/CLEAR_DAQ semantics,
/// 'clear' = clear rather than store, 'config_id' = the requested configuration id); not currently invoked by the
/// protocol layer.
/// @param cb_freeze_daq return a CRC_CMD_xxx status code (see "XCP command Return Codes" above)
void ApplXcpRegisterFreezeDaqCallback(uint8_t (*cb_freeze_daq)(uint8_t clear, uint16_t config_id));
/// Get the currently active calibration page. Only invoked when XCP_ENABLE_CAL_PAGE is used WITHOUT the built-in
/// calibration segment management (i.e. custom page handling bypassing XcpCreateCalSeg); calibration segments created
/// via XcpCreateCalSeg manage their own page switching and never reach this callback.
/// @param cb_get_cal_page segment is the calibration segment number (0 if only one segment is supported); mode is
/// CAL_PAGE_MODE_ECU (0x01, page as seen by the application) or CAL_PAGE_MODE_XCP (0x02, page as seen by the XCP tool),
/// optionally combined with CAL_PAGE_MODE_ALL (0x80, query applies to all segments); these CAL_PAGE_MODE_* values are
/// internal (src/xcp.h) and not re-exported here. Return the active page number: 0 = working/RAM page, 1 = default/FLASH page
void ApplXcpRegisterGetCalPageCallback(uint8_t (*cb_get_cal_page)(uint8_t segment, uint8_t mode));
/// Set the active calibration page. Same "bypasses built-in calibration segment management" scope as
/// ApplXcpRegisterGetCalPageCallback above.
/// @param cb_set_cal_page segment, mode as in ApplXcpRegisterGetCalPageCallback; page is the page number to activate
/// (0 = working/RAM page, 1 = default/FLASH page); return a CRC_CMD_xxx status code
void ApplXcpRegisterSetCalPageCallback(uint8_t (*cb_set_cal_page)(uint8_t segment, uint8_t page, uint8_t mode));
/// Freeze the current working page as the new default/reference page. Only invoked when XCP_ENABLE_FREEZE_CAL_PAGE is
/// used without the built-in calibration segment management (which has its own equivalent, XcpFreeze()).
/// @param cb_freeze_cal no parameters; return a CRC_CMD_xxx status code
void ApplXcpRegisterFreezeCalCallback(uint8_t (*cb_freeze_cal)(void));
/// Copy one calibration page to another (XCP COPY_CAL_PAGE). Only invoked when XCP_ENABLE_COPY_CAL_PAGE is used without
/// the built-in calibration segment management; currently only a single calibration segment is supported for this operation.
/// @param cb_init_cal src_page, dst_page are page numbers (0 = working/RAM page, 1 = default/FLASH page); return a CRC_CMD_xxx status code
void ApplXcpRegisterInitCalCallback(uint8_t (*cb_init_cal)(uint8_t src_page, uint8_t dst_page));
/// Verify memory access permissions before a Memory Transfer Address (MTA) based command (e.g. SHORT_UPLOAD/DOWNLOAD)
/// touches a given address range - called generically for any address extension, not just XCP_ENABLE_APP_ADDRESSING below.
/// If no callback is registered, all accesses are allowed.
/// @param cb_check ext is the XCP address extension, addr/size describe the byte range being accessed; return
/// CRC_CMD_OK to allow the access, or a CRC_CMD_xxx error code (e.g. CRC_ACCESS_DENIED) to deny it
void ApplXcpRegisterCheckCallback(uint8_t (*cb_check)(uint8_t ext, uint32_t addr, uint8_t size));
/// Read memory for XCP_ENABLE_APP_ADDRESSING: redirects asynchronous memory access to the application instead of
/// XCPlite's own address space, for the address extension XcpAddrIsApp() matches in xcp_cfg.h (XCP_ADDR_EXT_ABS by
/// default; see docs/TECHNICAL.md "User specific addressing mode"). If no callback is registered, access is denied.
/// @param cb_read src/size describe the source range, dst is the destination buffer to fill; return a CRC_CMD_xxx status code
void ApplXcpRegisterReadCallback(uint8_t (*cb_read)(uint32_t src, uint8_t size, uint8_t *dst));
/// Write memory for XCP_ENABLE_APP_ADDRESSING (the write-side counterpart of ApplXcpRegisterReadCallback above; same
/// address-extension scope). If no callback is registered, access is denied.
/// @param cb_write dst/size/src describe the destination range and source bytes; delay is true while CANape is in
/// indirect calibration mode with an atomic calibration operation in progress (CC_USER_CMD 0xF1, subcmd 0x01=begin,
/// 0x02=end - see docs/TECHNICAL.md), signaling the write may be held back and applied consistently with the others
/// once ApplXcpRegisterFlushCallback's callback is invoked at the end of the operation; return a CRC_CMD_xxx status code
void ApplXcpRegisterWriteCallback(uint8_t (*cb_write)(uint32_t dst, uint8_t size, const uint8_t *src, uint8_t delay));
/// Apply any writes held back via ApplXcpRegisterWriteCallback's delay parameter, ending an atomic calibration operation.
/// @param cb_flush no parameters; return a CRC_CMD_xxx status code
void ApplXcpRegisterFlushCallback(uint8_t (*cb_flush)(void));
/// Called periodically from the XCP server's background/idle processing; use for lightweight polling work that must
/// run on that thread (e.g. lazy calibration writes needing non-blocking socket polling, see docs/CAL_RCU.md item 8).
/// @param cb_idle no parameters
void ApplXcpRegisterIdleCallback(void (*cb_idle)(void));

// Utility functions (from platform.c) used for the demos to keep them clean and platform-independent
uint64_t clockGetMonotonicNs(void);
uint64_t clockGetMonotonicUs(void);
void sleepUs(uint32_t us);
void sleepMs(uint32_t ms);

// Debug metrics
void XcpEthTlPrintStatistics(void);
void XcpEthServerDebugInfo(size_t *rxStackSize, size_t *txStackSize);
void clockGetPrintStatistic(void);

#ifdef __cplusplus
} // extern "C"
#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Variadic C event trigger convinience macros DaqEventVar and DaqEventExtVar
// Option to create event, register measurements and trigger event in one call

#ifdef OPTION_ENABLE_A2L_GENERATOR
#ifndef __cplusplus

#define A2L_MEAS_PHYS
#define A2L_MEAS

// Macro to count arguments in a tuple
#define A2L_TUPLE_SIZE_(...) A2L_TUPLE_SIZE_IMPL_(__VA_ARGS__, 5, 4, 3, 2, 1, 0)
#define A2L_TUPLE_SIZE_IMPL_(_1, _2, _3, _4, _5, N, ...) N

// Dispatch macro based on tuple size
#define A2L_UNPACK_AND_REG_DISPATCH_(tuple) A2L_UNPACK_AND_REG_DISPATCH_IMPL_ tuple
#define A2L_UNPACK_AND_REG_DISPATCH_IMPL_(...) A2L_UNPACK_AND_REG_SELECT_(A2L_TUPLE_SIZE_(__VA_ARGS__))(__VA_ARGS__)

// Select the appropriate registration macro based on argument count
#define A2L_UNPACK_AND_REG_SELECT_(N) A2L_UNPACK_AND_REG_SELECT_IMPL_(N)
#define A2L_UNPACK_AND_REG_SELECT_IMPL_(N) A2L_UNPACK_AND_REG_##N##_

// Measurement: (var, comment)
#define A2L_UNPACK_AND_REG_2_(var, comment) A2lCreateMeasurement_(NULL, #var, A2lGetTypeId(var), 1, &(var), NULL, 0.0, 0.0, comment);

// Physical measurement: (var, comment, unit_or_conversion, min, max)
#define A2L_UNPACK_AND_REG_5_(var, comment, unit_or_conversion, min, max) A2lCreateMeasurement_(NULL, #var, A2lGetTypeId(var), 1, &(var), unit_or_conversion, min, max, comment);

// Main unpacking macro - dispatches to the right version
#define A2L_UNPACK_AND_REG_(...) A2L_UNPACK_AND_REG_DISPATCH_((__VA_ARGS__))

// Macro helpers for FOR_EACH pattern
// These expand the variadic arguments and apply a macro to each one
#define XCPLIB_FOR_EACH_MEAS_(macro, ...) XCPLIB_FOR_EACH_MEAS_IMPL_(macro, __VA_ARGS__)

// Implementation helper - handles up to 16 measurements
// Each XCPLIB_APPLY_ expands to macro(args) where args is (var, comment)
#define XCPLIB_FOR_EACH_MEAS_IMPL_(m, ...)                                                                                                                                         \
    XCPLIB_GET_MACRO_(__VA_ARGS__, XCPLIB_FE_16_, XCPLIB_FE_15_, XCPLIB_FE_14_, XCPLIB_FE_13_, XCPLIB_FE_12_, XCPLIB_FE_11_, XCPLIB_FE_10_, XCPLIB_FE_9_, XCPLIB_FE_8_,            \
                      XCPLIB_FE_7_, XCPLIB_FE_6_, XCPLIB_FE_5_, XCPLIB_FE_4_, XCPLIB_FE_3_, XCPLIB_FE_2_, XCPLIB_FE_1_, XCPLIB_FE_0_)(m, __VA_ARGS__)

// Selector macro - picks the right expander based on argument count
#define XCPLIB_GET_MACRO_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, NAME, ...) NAME

// Expander macros for different argument counts
#define XCPLIB_FE_0_(m)
#define XCPLIB_FE_1_(m, x1) XCPLIB_APPLY_(m, x1)
#define XCPLIB_FE_2_(m, x1, x2) XCPLIB_APPLY_(m, x1) XCPLIB_APPLY_(m, x2)
#define XCPLIB_FE_3_(m, x1, x2, x3) XCPLIB_APPLY_(m, x1) XCPLIB_APPLY_(m, x2) XCPLIB_APPLY_(m, x3)
#define XCPLIB_FE_4_(m, x1, x2, x3, x4) XCPLIB_APPLY_(m, x1) XCPLIB_APPLY_(m, x2) XCPLIB_APPLY_(m, x3) XCPLIB_APPLY_(m, x4)
#define XCPLIB_FE_5_(m, x1, x2, x3, x4, x5) XCPLIB_APPLY_(m, x1) XCPLIB_APPLY_(m, x2) XCPLIB_APPLY_(m, x3) XCPLIB_APPLY_(m, x4) XCPLIB_APPLY_(m, x5)
#define XCPLIB_FE_6_(m, x1, x2, x3, x4, x5, x6) XCPLIB_APPLY_(m, x1) XCPLIB_APPLY_(m, x2) XCPLIB_APPLY_(m, x3) XCPLIB_APPLY_(m, x4) XCPLIB_APPLY_(m, x5) XCPLIB_APPLY_(m, x6)
#define XCPLIB_FE_7_(m, x1, x2, x3, x4, x5, x6, x7)                                                                                                                                \
    XCPLIB_APPLY_(m, x1) XCPLIB_APPLY_(m, x2) XCPLIB_APPLY_(m, x3) XCPLIB_APPLY_(m, x4) XCPLIB_APPLY_(m, x5) XCPLIB_APPLY_(m, x6) XCPLIB_APPLY_(m, x7)
#define XCPLIB_FE_8_(m, x1, x2, x3, x4, x5, x6, x7, x8)                                                                                                                            \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2) XCPLIB_APPLY_(m, x3) XCPLIB_APPLY_(m, x4) XCPLIB_APPLY_(m, x5) XCPLIB_APPLY_(m, x6) XCPLIB_APPLY_(m, x7) XCPLIB_APPLY_(m, x8)
#define XCPLIB_FE_9_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9)                                                                                                                        \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3) XCPLIB_APPLY_(m, x4) XCPLIB_APPLY_(m, x5) XCPLIB_APPLY_(m, x6) XCPLIB_APPLY_(m, x7) XCPLIB_APPLY_(m, x8) XCPLIB_APPLY_(m, x9)
#define XCPLIB_FE_10_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10)                                                                                                                  \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x4) XCPLIB_APPLY_(m, x5) XCPLIB_APPLY_(m, x6) XCPLIB_APPLY_(m, x7) XCPLIB_APPLY_(m, x8) XCPLIB_APPLY_(m, x9) XCPLIB_APPLY_(m, x10)
#define XCPLIB_FE_11_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11)                                                                                                             \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x4)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x5) XCPLIB_APPLY_(m, x6) XCPLIB_APPLY_(m, x7) XCPLIB_APPLY_(m, x8) XCPLIB_APPLY_(m, x9) XCPLIB_APPLY_(m, x10) XCPLIB_APPLY_(m, x11)
#define XCPLIB_FE_12_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12)                                                                                                        \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x4)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x5)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x6) XCPLIB_APPLY_(m, x7) XCPLIB_APPLY_(m, x8) XCPLIB_APPLY_(m, x9) XCPLIB_APPLY_(m, x10) XCPLIB_APPLY_(m, x11) XCPLIB_APPLY_(m, x12)
#define XCPLIB_FE_13_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13)                                                                                                   \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x4)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x5)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x6)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x7) XCPLIB_APPLY_(m, x8) XCPLIB_APPLY_(m, x9) XCPLIB_APPLY_(m, x10) XCPLIB_APPLY_(m, x11) XCPLIB_APPLY_(m, x12) XCPLIB_APPLY_(m, x13)
#define XCPLIB_FE_14_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14)                                                                                              \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x4)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x5)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x6)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x7)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x8) XCPLIB_APPLY_(m, x9) XCPLIB_APPLY_(m, x10) XCPLIB_APPLY_(m, x11) XCPLIB_APPLY_(m, x12) XCPLIB_APPLY_(m, x13) XCPLIB_APPLY_(m, x14)
#define XCPLIB_FE_15_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15)                                                                                         \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x4)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x5)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x6)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x7)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x8)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x9) XCPLIB_APPLY_(m, x10) XCPLIB_APPLY_(m, x11) XCPLIB_APPLY_(m, x12) XCPLIB_APPLY_(m, x13) XCPLIB_APPLY_(m, x14) XCPLIB_APPLY_(m, x15)
#define XCPLIB_FE_16_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16)                                                                                    \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x4)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x5)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x6)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x7)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x8)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x9)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x10) XCPLIB_APPLY_(m, x11) XCPLIB_APPLY_(m, x12) XCPLIB_APPLY_(m, x13) XCPLIB_APPLY_(m, x14) XCPLIB_APPLY_(m, x15) XCPLIB_APPLY_(m, x16)

// Apply macro to unpacked tuple arguments
// Strips the outer parentheses from (var, comment) and passes to macro as two separate arguments
#define XCPLIB_APPLY_(m, args) m args

// =============================================================================
// Variadic DAQ macros which create, register variables and trigger events in one call

/// Trigger an event, create the event once and register global and local measurement variables once
/// Supports absolute, stack and relative addressing mode measurements
/// Don't use same event name in multiple code locations
/// @param event_name Name of the event to trigger
/// @param ... List of measurement variables to register, each as a tuple (var, comment) or (var, comment, unit_or_conversion, min, max)
/// Needs #define OPTION_DAQ_EVENT_LIST for on target A2L generation
#define DaqEventVar(event_name, ...)                                                                                                                                               \
    do {                                                                                                                                                                           \
        static const tXcpEventDescriptor evt__##event_name XCP_EVENT_SECTION_ATTR = XCP_EVENT_DESCRIPTOR_INIT(#event_name, 0, 0);                                                  \
        static tXcpEventId trg__AAS__##event_name = XCP_UNDEFINED_EVENT_ID;                                                                                                        \
        XCP_EVENT_SECTION_SET_ID(evt__##event_name, trg__AAS__##event_name);                                                                                                       \
        if (XcpIsActivated()) {                                                                                                                                                    \
            A2lOnce() {                                                                                                                                                            \
                A2lLock();                                                                                                                                                         \
                A2lSetAutoAddrMode__s(#event_name, xcp_get_frame_addr(), NULL);                                                                                                    \
                XCPLIB_FOR_EACH_MEAS_(A2L_UNPACK_AND_REG_, __VA_ARGS__);                                                                                                           \
                A2lUnlock();                                                                                                                                                       \
            }                                                                                                                                                                      \
        }                                                                                                                                                                          \
        XcpEventExt_Var(trg__AAS__##event_name, 1, xcp_get_frame_addr());                                                                                                          \
        XCP_NO_TAIL_CALL();                                                                                                                                                        \
    } while (0)

#endif // !__cplusplus
#endif // OPTION_ENABLE_A2L_GENERATOR
