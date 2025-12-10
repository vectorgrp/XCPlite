#pragma once
#define __XCPLIB_H__

/*----------------------------------------------------------------------------
| File:
|   xcplib.h - Public xcplib C API
|
| Description:
|   C header file for the XCPlite library xcplib application programming interface
|   Used for Rust bindgen to generate FFI bindings for xcplib
|   Supporting functions and macros for A2L generation are in a2l.h
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/// Get information about the XCP on Ethernet server instance address.
/// @pre The server instance is running.
/// @param out_is_tcp Optional out parameter to query if TCP or UDP is used.
/// True if TCP, otherwise UDP.
/// Pass NULL if not required.
/// @param out_mac Optional out parameter to query the MAC address of the interface used in the server instance.
/// Pass NULL if not required.
/// @param out_address Optional out parameter to query the IP address used in the server instance.
/// Pass NULL if not required.
/// @param out_port Optional out parameter to query the port address used in the server instance.
/// Pass NULL if not required.
void XcpEthServerGetInfo(bool *out_is_tcp, uint8_t *out_mac, uint8_t *out_address, uint16_t *out_port);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Calibration segments

/// Calibration segment handle
typedef uint16_t tXcpCalSegIndex;
#define XCP_UNDEFINED_CALSEG 0xFFFF

/// Create a calibration segment and add it to the list of calibration segments.
/// Create a named calibration segment and add it to the list of calibration segments.
/// This calibration segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
/// It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration params
/// It supports XCP/ECU independent page switching, checksum calculation, copy and reinitialization (copy reference page to working page)
/// @param name Name of the calibration segment.
/// @param default_page Pointer to the default page.
/// @param size Size of the calibration page in bytes.
/// @return a handle or XCP_UNDEFINED_CALSEG when out of memory or the name already exists.
tXcpCalSegIndex XcpCreateCalSeg(const char *name, const void *default_page, uint16_t size);

/// Find a calibration segment by name, returns XCP_UNDEFINED_CALSEG if not found
tXcpCalSegIndex XcpFindCalSeg(const char *name);

/// Get the name of the calibration segment
/// @return the name of the calibration segment or NULL if the index is invalid.
const char *XcpGetCalSegName(tXcpCalSegIndex calseg);

/// Lock a calibration segment.
/// @param calseg Calibration segment index.
/// @return Pointer to the active page of the calibration segment (working page or reference page, controlled by the XCP client tool).
/// The pointer is valid until the calibration segment is unlocked.
/// The data can be safely accessed while the lock is held.
/// There is no contention with the XCP client tool and with other threads acquiring the lock.
/// Acquiring the lock is wait-free, locks may be recursive
const uint8_t *XcpLockCalSeg(tXcpCalSegIndex calseg);

/// Unlock a calibration segment
void XcpUnlockCalSeg(tXcpCalSegIndex calseg);

/// Freeze all calibration segments
/// The current working page is written to the persistence file
/// It will be the new default page on next application start
/// Freeze can also be required by the XCP client tool.
/// Requires option XCP_ENABLE_FREEZE_CAL_PAGE.
/// @return true on success, otherwise false.
bool XcpFreezeAllCalSeg(void);

/// Set all calibration segments to their default page.
/// Maybe used in emergency situations.
bool XcpResetAllCalSegs(void);

// Internal functions
uint32_t XcpGetCalSegBaseAddress(tXcpCalSegIndex calseg);
uint16_t XcpGetCalSegCount(void);
uint16_t XcpGetCalSegSize(tXcpCalSegIndex calseg);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Calibration segment convenience macros

// Convenience macros to create and access calibration segments by identifier without providing explicit handles and the need to pass them around

/// Create calibration segment macro
/// Name given as identifier, type name and segment name are identical
/// Macro may be used anywhere in the code, even in loops
// cal__##name is the linker map file marker for calibration segments
/// @param name given as identifier
#define CalSegCreate(name)                                                                                                                                                         \
    static tXcpCalSegIndex cal__##name = XCP_UNDEFINED_CALSEG;                                                                                                                     \
    static tXcpCalSegIndex __cal__##name = XCP_UNDEFINED_CALSEG;                                                                                                                   \
    if (cal__##name == XCP_UNDEFINED_CALSEG) {                                                                                                                                     \
        cal__##name = XcpCreateCalSeg(#name, (uint8_t *)&(name), sizeof(name));                                                                                                    \
        __cal__##name = cal__##name;                                                                                                                                               \
    }

/// Get calibration segment macro
/// @param name given as identifier
#define CalSegGet(name)                                                                                                                                                            \
    static tXcpCalSegIndex __cal__##name = XCP_UNDEFINED_CALSEG;                                                                                                                   \
    if (__cal__##name == XCP_UNDEFINED_CALSEG) {                                                                                                                                   \
        __cal__##name = XcpFindCalSeg(#name);                                                                                                                                      \
    }

/// Lock calibration segment macro
/// Macro may be used anywhere in the code, even in loops
/// @param name given as identifier
#define CalSegLock(name) ((const __typeof__(name) *)XcpLockCalSeg(__cal__##name))

/// Unlock calibration segment macro
/// @param name given as identifier
#define CalSegUnlock(name) XcpUnlockCalSeg(__cal__##name)

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Dynamic DAQ event creation

/// Undefined event id
#define XCP_UNDEFINED_EVENT_ID 0xFFFF
/// DAQ event id as handle
typedef uint16_t tXcpEventId;

/// Add a measurement event to the event list, returns the event id  (0..XCP_MAX_EVENT_COUNT-1)
/// If the event name already exists, returns the existing event event number
/// Function is thread safe by using a mutex for event list access.
/// @param name Name of the event.
/// @param cycleTimeNs Cycle time in nanoseconds. 0 means sporadic event.
/// @param priority Priority of the event. 0 means normal, >=1 means realtime.
/// @return The event id or XCP_UNDEFINED_EVENT_ID if out of event list memory.
tXcpEventId XcpCreateEvent(const char *name, uint32_t cycleTimeNs /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/);

/// Add a measurement event to event list, return event id (0..XCP_MAX_EVENT_COUNT-1)
/// If the event name exists, a new event instance index is generated (will be the postfix of the event name in the A2L file)
/// Function is thread safe by using a mutex for event list access.
/// @param name Name of the event.
/// @param cycleTimeNs Cycle time in nanoseconds. 0 means sporadic event.
/// @param priority Priority of the event. 0 means normal, >=1 means realtime.
/// @return The event id or XCP_UNDEFINED_EVENT_ID if out of memory.
tXcpEventId XcpCreateEventInstance(const char *name, uint32_t cycleTimeNs /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/);

/// Get event id by name, returns XCP_UNDEFINED_EVENT_ID if not found
/// @param name Name of the event.
/// @param count Optional (!=NULL) out parameter to return the number of event instances with the same name.
/// @return The event id or XCP_UNDEFINED_EVENT_ID if not found.
/// If multiple events instances with the same name exist, the first one is returned.
tXcpEventId XcpFindEvent(const char *name, uint16_t *count);

/// Get the event instance index (1..)
/// @param event Event id.
/// @return The event index (1..), or 0 if no indexed event instance found.
uint16_t XcpGetEventIndex(tXcpEventId event);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Dynamic DAQ event creation convenience macros with once execution patterns

// Create XCP events by 'name' given as identifier or string
// Event cycle time is set to sporadic and priority to normal
// Setting the cycle time would only have the benefit for the XCP client tool to estimate the expected data rate of a DAQ setup
// To create an XCP event with increased priority, use XcpCreateEvent

// Note on thread safety of the once patterns using static state instead of thread local state:
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

/// Create a global event
/// Macro may be used anywhere in the code, even in loops
/// Thread safe global once pattern, the first call creates the event
/// May be called multiple times in different code locations, ignored if the the event name already exists
/// @param name Name given as identifier
#define DaqCreateEvent(name)                                                                                                                                                       \
    static tXcpEventId evt__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                                       \
    if (XcpIsActivated()) {                                                                                                                                                        \
        if (evt__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                               \
            evt__##name = XcpCreateEvent(#name, 0, 0);                                                                                                                             \
        }                                                                                                                                                                          \
    }

/// Create a global event with cycle time
/// @param name Name given as identifier
/// @param cycle_time Cycle time in microseconds
#define DaqCreateCyclicEvent(name, cycle_time)                                                                                                                                     \
    static tXcpEventId evt__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                                       \
    if (XcpIsActivated()) {                                                                                                                                                        \
        if (evt__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                               \
            evt__##name = XcpCreateEvent(#name, cycle_time * 1000, 0);                                                                                                             \
        }                                                                                                                                                                          \
    }

/// Create a thread local event with dynamic name
/// Macro may be used anywhere in the code, even in loops
/// The first call in a thread creates the event, must be unique per thread and per code location
/// Name may be different per code location in different threads
/// Calling again in the same thread is ignored, ignored if the the event name is different
/// @param name Name given as string
#define DaqCreateEvent_s(name)                                                                                                                                                     \
    static THREAD_LOCAL tXcpEventId evt__dynname = XCP_UNDEFINED_EVENT_ID;                                                                                                         \
    if (XcpIsActivated()) {                                                                                                                                                        \
        if (evt__dynname == XCP_UNDEFINED_EVENT_ID) {                                                                                                                              \
            evt__dynname = XcpCreateEvent(name, 0, 0);                                                                                                                             \
        }                                                                                                                                                                          \
    }

/// Multi instance event
/// No once pattern, a new event instance is created for each call
/// If the name exists, an incrementing instance index is generated and appended to the event <name>_xxx in the A2L file
/// @param name Name given as identifier
#define DaqCreateEventInstance(name)                                                                                                                                               \
    static tXcpEventId evt__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                                       \
    if (XcpIsActivated()) {                                                                                                                                                        \
        evt__##name = XcpCreateEventInstance(#name, 0, 0);                                                                                                                         \
    }

/// Get event instance id
/// @param name Name given as identifier
#define DaqGetEventInstanceId(name) evt__##name

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// DAQ event trigger measurement instrumentation point

/// Trigger timestamped measurement events and transfer XCP tool configured associated data
/// Function are thread safe and look-free, depending on the transmit queue configuration and platform. See technical reference for details.

/// Trigger the XCP event 'event' for absolute addressing mode (XCP_ADDR_EXT_ABS)
/// @param event Event id.
void XcpEvent(tXcpEventId event);

/// Trigger the XCP event 'event' for absolute or relative addressing mode with explicitly given base address (address extension = 2)
/// @param event
/// @param base address pointer
void XcpEventExt(tXcpEventId event, const uint8_t *base2);

/// Trigger the XCP event 'event' for absolute or relative addressing mode with explicitly given base addresses for multiple relative addressing modes (address extensions = [2..4])
/// @param event
/// @param count Number of base address pointers passed (max 3 possible)
void XcpEventExt_Var(tXcpEventId event, uint8_t count, ...);

// Internal use by some macros and the Rust API
void XcpEventExt_(tXcpEventId event, uint8_t count, const uint8_t **bases);
void XcpEventExtAt_(tXcpEventId event, uint8_t count, const uint8_t **bases, uint64_t clock);
void XcpEventExt2(tXcpEventId event, const uint8_t *base2, const uint8_t *base3); // for Rust API
void XcpEventExtAt(tXcpEventId event, const uint8_t *base2, uint64_t clock);
void XcpEventExtAt_Var(tXcpEventId event, uint64_t clock, uint8_t count, ...);

// Enable or disable a XCP DAQ event
void XcpEventEnable(tXcpEventId event, bool enable);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Get stack frame pointer
// Used by the Daq and A2l macros to get the stack frame pointer for stack relative addressing mode

// Linux, MACOS gnu and clang compiler
#if defined(__GNUC__) || defined(__clang__)

#define xcp_get_frame_addr() (const uint8_t *)__builtin_frame_address(0)

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
    return (const uint8_t *)(return_addr_ptr - 1);
}
#else
#error "Unsupported MSVC architecture for frame pointer detection"
#endif

// Other compilers
#else
#error "xcp_get_frame_addr is not defined for this compiler. Please implement it."
#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Global storage

// Get the base address for absolute XCP/A2L 32 bit address
const uint8_t *ApplXcpGetBaseAddr(void);
extern const uint8_t *gXcpBaseAddr;
#define xcp_get_base_addr() gXcpBaseAddr // For runtime optimization, use xcp_get_base_addr() instead of ApplXcpGetBaseAddr()

// Calculate the absolute XCP/A2L 32 bit address from a pointer
uint32_t ApplXcpGetAddr(const uint8_t *p);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// DAQ event trigger convenience macros

// Event name parameter is a symbol, a string (_s) or an event index (_i)
// Creates linker map file markers (static variables: trg__xxxx_'eventname' ) for the XCP event id used
// No need to take care to store the event id
// Uses local scope static or thread local storage to create a once pattern for the event lookup to save runtime overhead
// All macros can be used to measure variables registered in absolute addressing mode as well

// Note:
// All macros assert that the event is found in the one time lookup, which means an event has to exist, before it is triggered the first time
// The asserts may be removed with the consequence, that the event lookup takes place on every trigger, before the event is created
// This might add unwanted runtime overhead (a linear search in the event list) on every trigger call

/// Trigger the global XCP event 'name' for stack relative or absolute addressing
/// Cache the event name lookup in global storage, can not be called with different names in its code location
/// @param name Name given as identifier
#define DaqTriggerEvent(name)                                                                                                                                                      \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static tXcpEventId trg__AAS__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                              \
        if (trg__AAS__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                          \
            trg__AAS__##name = XcpFindEvent(#name, NULL);                                                                                                                          \
            assert(trg__AAS__##name != XCP_UNDEFINED_EVENT_ID);                                                                                                                    \
        }                                                                                                                                                                          \
        XcpEventExt_Var(trg__AAS__##name, 1, xcp_get_frame_addr());                                                                                                                \
    }
#define DaqTriggerEventAt(name, clock)                                                                                                                                             \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static tXcpEventId trg__AAS__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                              \
        if (trg__AAS__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                          \
            trg__AAS__##name = XcpFindEvent(#name, NULL);                                                                                                                          \
            assert(trg__AAS__##name != XCP_UNDEFINED_EVENT_ID);                                                                                                                    \
        }                                                                                                                                                                          \
        XcpEventExtAt_Var(trg__AAS__##name, clock, 1, xcp_get_frame_addr());                                                                                                       \
    }

/// Trigger the XCP event by handle 'event_id' for stack relative or absolute addressing
/// No lookup overhead, event id must be valid
/// @param name Event given as id
#define DaqTriggerEvent_i(event_id)                                                                                                                                                \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static tXcpEventId trg__AAS__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                              \
        XcpEventExt(event_id, xcp_get_frame_addr());                                                                                                                               \
    }
#define DaqTriggerEventAt_i(event_id, clock)                                                                                                                                       \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static tXcpEventId trg__AAS__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                              \
        XcpEventExtAt(event_id, xcp_get_frame_addr(), clock);                                                                                                                      \
    }

/// Trigger the XCP event 'name' for absolute, stack and relative addressing mode with given individual base address (from A2lSetRelativeAddrMode(base_addr))
/// Cache the event name lookup in global storage, can not be called with different names in its code location
/// @param name Name given as identifier
/// @param base_addr Base address pointer for relative addressing mode
#define DaqTriggerEventExt(name, base_addr)                                                                                                                                        \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static tXcpEventId trg__AASD__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                             \
        if (trg__AASD__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                         \
            trg__AASD__##name = XcpFindEvent(#name, NULL);                                                                                                                         \
            assert(trg__AASD__##name != XCP_UNDEFINED_EVENT_ID);                                                                                                                   \
        }                                                                                                                                                                          \
        XcpEventExt_Var(trg__AASD__##name, 2, xcp_get_frame_addr(), (const uint8_t *)base_addr);                                                                                   \
    }

/// Trigger the XCP event 'name' for absolute, stack and relative addressing mode with given individual base address (from A2lSetRelativeAddrMode(base_addr))
/// Cache the event lookup in thread local storage, can be called with different names in the same code location in different threads
/// @param name Name given as string, must be unique per thread and code location
/// @param base_addr Base address pointer for relative addressing mode
#define DaqTriggerEventExt_s(name, base_addr)                                                                                                                                      \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static THREAD_LOCAL tXcpEventId trg__AASD__ = XCP_UNDEFINED_EVENT_ID;                                                                                                      \
        if (trg__AASD__ == XCP_UNDEFINED_EVENT_ID) {                                                                                                                               \
            trg__AASD__ = XcpFindEvent(name, NULL);                                                                                                                                \
            assert(trg__AASD__ != XCP_UNDEFINED_EVENT_ID);                                                                                                                         \
        }                                                                                                                                                                          \
        XcpEventExt_Var(trg__AASD__, 2, xcp_get_frame_addr(), (const uint8_t *)base_addr);                                                                                         \
    }

/// Trigger the XCP event by handle 'event_id' for absolute, stack and relative addressing mode with given individual base address (from A2lSetRelativeAddrMode(base_addr))
/// No lookup overhead, event id must be valid
/// @param event_id Event given as id
/// @param base_addr Base address pointer for relative addressing mode
#define DaqTriggerEventExt_i(event_id, base_addr)                                                                                                                                  \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static tXcpEventId trg__AASD = XCP_UNDEFINED_EVENT_ID;                                                                                                                     \
        XcpEventExt_Var(event_id, 2, xcp_get_frame_addr(), (const uint8_t *)base_addr);                                                                                            \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Combined create and trigger DAQ event macros

/// Create and trigger the global XCP event 'name' for stack relative or absolute addressing
/// Cache the event name lookup in global storage, can not be called with different names in its code location
/// The first call creates the event
/// @param name Name given as identifier
#define DaqCreateAndTriggerEvent(name)                                                                                                                                             \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static tXcpEventId evt__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                                   \
        static tXcpEventId trg__AAS__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                              \
        if (trg__AAS__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                          \
            evt__##name = trg__AAS__##name;                                                                                                                                        \
            trg__AAS__##name = XcpCreateEvent(#name, 0, 0);                                                                                                                        \
        }                                                                                                                                                                          \
        XcpEventExt_Var(trg__AAS__##name, 1, xcp_get_frame_addr());                                                                                                                \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Enable/disable events

/// Enable the XCP event 'name'
#define DaqEventEnable(name)                                                                                                                                                       \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static THREAD_LOCAL tXcpEventId ena__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                      \
        if (ena__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                               \
            ena__##name = XcpFindEvent(#name, NULL);                                                                                                                               \
        }                                                                                                                                                                          \
        XcpEventEnable(ena__##name, true);                                                                                                                                         \
    }

/// Disable the XCP event 'name'
#define DaqEventDisable(name)                                                                                                                                                      \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static THREAD_LOCAL tXcpEventId ena__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                      \
        if (ena__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                               \
            ena__##name = XcpFindEvent(#name, NULL);                                                                                                                               \
        }                                                                                                                                                                          \
        XcpEventEnable(ena__##name, false);                                                                                                                                        \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Build time A2L file generation helpers

// @@@@ NOTE: Work in progress
#if 0

#define _XCP_STRING(var_name, suffix, value) static const char __attribute__((section(XCP_STRING_SECTION), used)) __xcp_str_##var_name##suffix[] = value

#ifdef __APPLE__
// macOS Mach-O: segment,section format
#define XCP_META_SECTION "__DATA,__xcp_meta"
#define XCP_STRING_SECTION "__DATA,__xcp_str"
#else
// Linux ELF: simple section names
#define XCP_META_SECTION ".xcp.meta"
#define XCP_STRING_SECTION ".xcp.strings"
#endif

// Metadata annotations
#define XCP_COMMENT (name, comment) static const char *__attribute__((section(XCP_STRING_SECTION), used)) __a2l_comment_##name = comment;
#define XCP_UNIT(name, unit) static const char *__attribute__((section(XCP_STRING_SECTION), used)) __a2l_unit_##name = unit;
#define XCP_LIMITS(name, min, max)                                                                                                                                                 \
    static const double __attribute__((section(XCP_META_SECTION), used)) __a2l_min_##name = min;                                                                                   \
    static const double __attribute__((section(XCP_META_SECTION), used)) __a2l_max_##name = max;

#else
#define XCP_COMMENT (name, comment)
#define XCP_UNIT(name, unit)
#define XCP_LIMITS(name, min, max)
#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Measurement of local variables and function parameters without A2L runtime generation enabled

// Note on local variable and function parameter visibility:
// When runtime A2L generation is not used, the compiler may optimize local variables and function parameters to be stored in CPU registers only, without a memory location on the
// stack In this case, XCPlite can not measure these variables since there is no memory location to read from, reading the register value is not supported yet To prevent this
// optimization, the variable must be marked as 'volatile' to force the compiler to always read and write it from/to memory The XCP_MEA and XCP_MEAS macros mark a (local) variable
// as volatile for this purpose An alternative is to use the DaqCapture macro to capture the variable in a hidden static variable for measurement

// The A2L updater/creator in xcp_client can handle only simple location expressions such as absolute addresses, stack relative addresses (CFA) and calibration segment relative
// addresses For complex cases, use the DaqCapture macro to capture the variable in a hidden static variable

/// Attribute to mark a local variable as measurable
/// Example usage: XCP_MEAS int32_t my_var = 0;
#define XCP_MEA volatile
#define XCP_MEAS volatile

// Macro to force a function parameter to be stored on the stack
#define XCP_FORCE_TO_STACK(var) asm volatile("" ::"m"(var) : "memory")

// Compiler memory barrier to prevent reordering of memory accesses across this point
#define XCP_MEMORY_BARRIER() asm volatile("" ::: "memory")

/// Capture a local variable for measurement with a specific event
/// The variable must be in scope when the event is triggered with DaqTriggerEvent
/// The build time A2L file generator will find the hidden static variable 'daq__##event##__##var' and create the measurement with approriate addressing mode and
/// event association
#define DaqCapture(event, var)                                                                                                                                                     \
    do {                                                                                                                                                                           \
        static __typeof__(var) daq__##event##__##var;                                                                                                                              \
        daq__##event##__##var = var;                                                                                                                                               \
    } while (0)

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Misc

/// Set log level
/// Log level 4 provides a trace of all XCP commands and responses.
/// @param level (0 = no logging, 1 = error, 2 = warning, 3 = info, 4 = debug, 5 = trace)
void XcpSetLogLevel(uint8_t level);

/// Initialize the XCP singleton, activate XCP, must be called before starting the server
/// If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
/// @param activate If true, the XCP library is activated
void XcpInit(const char *name, const char *epk, bool activate);

/// Reset XCP library to initial state
void XcpReset(void);

/// Check if XCP has been activated
bool XcpIsActivated(void);

/// Check if XCP is connected
bool XcpIsConnected(void);

// Project name
const char *XcpGetProjectName(void);

// A2L file name
// Notify xcplib there is a valid A2L with this name to be provided for upload via XCP command GET_ID
void XcpSetA2lName(const char *name);
const char *XcpGetA2lName(void);

// EPK software version identifier
const char *XcpGetEpk(void);

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
/// @return time in CLOCK_TICKS_PER_S units
/// Resolution and epoch is defined in main_cfg.h
/// Epoch may be PTP or arbitrary
/// Resolution is 1ns or 1us
uint64_t ApplXcpGetClock64(void);

// Register XCP callbacks
void ApplXcpRegisterConnectCallback(bool (*cb_connect)(uint8_t mode));
void ApplXcpRegisterPrepareDaqCallback(uint8_t (*cb_prepare_daq)(void));
void ApplXcpRegisterStartDaqCallback(uint8_t (*cb_start_daq)(void));
void ApplXcpRegisterStopDaqCallback(void (*cb_stop_daq)(void));
void ApplXcpRegisterFreezeDaqCallback(uint8_t (*cb_freeze_daq)(uint8_t clear, uint16_t config_id));
void ApplXcpRegisterGetCalPageCallback(uint8_t (*cb_get_cal_page)(uint8_t segment, uint8_t mode));
void ApplXcpRegisterSetCalPageCallback(uint8_t (*cb_set_cal_page)(uint8_t segment, uint8_t page, uint8_t mode));
void ApplXcpRegisterFreezeCalCallback(uint8_t (*cb_freeze_cal)(void));
void ApplXcpRegisterInitCalCallback(uint8_t (*cb_init_cal)(uint8_t src_page, uint8_t dst_page));
void ApplXcpRegisterReadCallback(uint8_t (*cb_read)(uint32_t src, uint8_t size, uint8_t *dst));
void ApplXcpRegisterWriteCallback(uint8_t (*cb_write)(uint32_t dst, uint8_t size, const uint8_t *src, uint8_t delay));
void ApplXcpRegisterFlushCallback(uint8_t (*cb_flush)(void));

// xcplib utility functions used for the demos to keep them clean and platform-independent
uint64_t clockGetNs(void);
uint64_t clockGetUs(void);
void sleepUs(uint32_t us);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Compatibility

// Renamed deprecated macros
#define DaqEvent DaqTriggerEvent
#define DaqEventRelative DaqTriggerEventExt
#define DaqEventRelative_s DaqTriggerEventExt_s
#define DaqEventRelative_i DaqTriggerEventExt_i
#define DaqCreateEventInstance_s DaqCreateEventInstance
#define DaqEvent_i DaqTriggerEvent_i
#define XcpDaqEvent DaqEventVar
#define XcpDaqEventExt DaqEventExtVar

#ifdef __cplusplus
} // extern "C"
#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Variadic C event trigger convinience macros DaqEventVar and DaqEventExtVar
// Option to create event, register measurements and trigger event in one call

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

// Create the daq event (just for unique naming scheme)
#define XcpCreateDaqEvent DaqCreateEvent

// Register measurements once and trigger an already created event with stack addressing mode
#define XcpTriggerDaqEvent(event_name, ...)                                                                                                                                        \
    do {                                                                                                                                                                           \
        A2lOnce() {                                                                                                                                                                \
            A2lLock();                                                                                                                                                             \
            A2lSetStackAddrMode__s(#event_name, xcp_get_frame_addr());                                                                                                             \
            XCPLIB_FOR_EACH_MEAS_(A2L_UNPACK_AND_REG_, __VA_ARGS__)                                                                                                                \
            A2lUnlock();                                                                                                                                                           \
        }                                                                                                                                                                          \
        DaqTriggerEvent(event_name);                                                                                                                                               \
    } while (0)

// Register measurements once and trigger an already created event with stack or relative addressing mode
#define XcpTriggerDaqEventExt(event_name, base, ...)                                                                                                                               \
    do {                                                                                                                                                                           \
        A2lOnce() {                                                                                                                                                                \
            A2lLock();                                                                                                                                                             \
            A2lSetAutoAddrMode__s(#event_name, xcp_get_frame_addr(), (const uint8_t *)base);                                                                                       \
            XCPLIB_FOR_EACH_MEAS_(A2L_UNPACK_AND_REG_, __VA_ARGS__)                                                                                                                \
            A2lUnlock();                                                                                                                                                           \
        }                                                                                                                                                                          \
        DaqTriggerEventExt(event_name, base);                                                                                                                                      \
    } while (0)

// =============================================================================
// Variadic DAQ macros which create, register variables and trigger events in one call

/// Trigger an event, create the event once and register global and local measurement variables once
/// Supports absolute, stack and relative addressing mode measurements
#define DaqEventVar(event_name, ...)                                                                                                                                               \
    do {                                                                                                                                                                           \
        if (XcpIsActivated()) {                                                                                                                                                    \
            static tXcpEventId evt__##event_name = XCP_UNDEFINED_EVENT_ID;                                                                                                         \
            if (evt__##event_name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                     \
                evt__##event_name = XcpCreateEvent(#event_name, 0, 0);                                                                                                             \
                A2lOnce() {                                                                                                                                                        \
                    A2lLock();                                                                                                                                                     \
                    A2lSetAutoAddrMode__s(#event_name, xcp_get_frame_addr(), NULL);                                                                                                \
                    XCPLIB_FOR_EACH_MEAS_(A2L_UNPACK_AND_REG_, __VA_ARGS__)                                                                                                        \
                    A2lUnlock();                                                                                                                                                   \
                }                                                                                                                                                                  \
            }                                                                                                                                                                      \
            static tXcpEventId trg__AAS__##event_name = XCP_UNDEFINED_EVENT_ID;                                                                                                    \
            XcpEventExt_Var(evt__##event_name, 1, xcp_get_frame_addr());                                                                                                           \
        }                                                                                                                                                                          \
    } while (0)

/// Trigger an event, create the event once and register global, local and relative addressing mode measurement variables once
/// Supports absolute, stack and relative addressing mode measurements
#define DaqEventExtVar(event_name, base, ...)                                                                                                                                      \
    do {                                                                                                                                                                           \
        if (XcpIsActivated()) {                                                                                                                                                    \
            static tXcpEventId evt__##event_name = XCP_UNDEFINED_EVENT_ID;                                                                                                         \
            if (evt__##event_name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                     \
                evt__##event_name = XcpCreateEvent(#event_name, 0, 0);                                                                                                             \
                A2lOnce() {                                                                                                                                                        \
                    A2lLock();                                                                                                                                                     \
                    A2lSetAutoAddrMode__s(#event_name, xcp_get_frame_addr(), (const uint8_t *)base);                                                                               \
                    XCPLIB_FOR_EACH_MEAS_(A2L_UNPACK_AND_REG_, __VA_ARGS__)                                                                                                        \
                    A2lUnlock();                                                                                                                                                   \
                }                                                                                                                                                                  \
                static tXcpEventId trg__AASD__##event_name = XCP_UNDEFINED_EVENT_ID;                                                                                               \
                XcpEventExt_Var(evt__##event_name, 2, xcp_get_frame_addr(), (const uint8_t *)base);                                                                                \
            }                                                                                                                                                                      \
        }                                                                                                                                                                          \
    } while (0)

#endif // !__cplusplus
