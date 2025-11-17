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

// Get the XCP/A2L address of a calibration segment
// Internal function used for A2L generation
uint32_t XcpGetCalSegBaseAddress(tXcpCalSegIndex calseg);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Calibration segment convenience macros

// Convienienc macros to create and access calibration segments by identifier without providing explicit handles and the need to pass them around

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
/// If name exists, a new event instance index is generated (will be the postfix of the event name in the A2L file)
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
// Dynamic DAQ event creation convenience macros

// Create XCP events by 'name' as identifier or string
// Cycle time is set to sporadic and priority to normal
// Setting the cycle time would only have the benefit for the XCP client tool to estimate the expected data rate of a DAQ setup
// To create an XCP event with increased priority, use XcpCreateEvent

/// Global event
/// Macro may be used anywhere in the code, even in loops
/// TLS once pattern (the first call creates the event, the first call per thread does the event lookup)
/// @param name Name given as identifier
#define DaqCreateEvent(name)                                                                                                                                                       \
    static THREAD_LOCAL tXcpEventId evt__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                          \
    if (XcpIsActivated()) {                                                                                                                                                        \
        if (evt__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                               \
            evt__##name = XcpCreateEvent(#name, 0, 0);                                                                                                                             \
        }                                                                                                                                                                          \
    }

/// Global event
/// Macro may be used anywhere in the code, even in loops
/// TLS once pattern (the first call creates the event, the first call per thread does the event lookup)
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
/// If the name exists, an incrementing instance id is appended to the name <name>_xxx
/// @param name Name given as identifier
#define DaqCreateEventInstance(name)                                                                                                                                               \
    static THREAD_LOCAL tXcpEventId evt__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                          \
    if (XcpIsActivated()) {                                                                                                                                                        \
        evt__##name = XcpCreateEventInstance(#name, 0, 0);                                                                                                                         \
    }

/// Thread instance event
/// TLS once pattern (the first call per thread creates the event, an incrementing instance id is appended to the name <name>_xxx
/// @param name Name given as identifier
#define DaqCreateEventThreadInstance(name)                                                                                                                                         \
    static THREAD_LOCAL tXcpEventId evt__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                          \
    if (XcpIsActivated()) {                                                                                                                                                        \
        if (evt__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                               \
            evt__##name = XcpCreateEventInstance(#name, 0, 0);                                                                                                                     \
        }                                                                                                                                                                          \
    }

/// Get event instance id
/// @param name Name given as identifier
#define DaqGetEventInstanceId(name) evt__##name

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// DAQ event trigger measurement instrumentation point

/// Trigger timestamped measurement events and transfer XCP tool configured associated data
/// Function are thread safe and look-free, depending on the transmit queue configuration and platform. See technical reference for details.

/// Trigger the XCP event 'event' for stack absolute addressing mode (XCP_ADDR_EXT_ABS)
/// @param event Event id.
void XcpEvent(tXcpEventId event);

/// Trigger the XCP event 'event' for absolute or relative mode with explicitly given base address (for XCP_ADDR_EXT_DYN)
/// @param event
/// @param base address pointer for the relative (XCP_ADDR_EXT_DYN) addressing mode
void XcpEventExt(tXcpEventId event, const uint8_t *base2);

// Variadic versions for more call convenience

/// Trigger the XCP event 'event' with explicitly given base addresses for all addressing modes (address extensions XCP_ADDR_EXT_DYN, ...)
/// @param event
/// @param count Number of base address pointers passed
void XcpEventExt_Var(tXcpEventId event, uint8_t count, ...);

/// Internal use by some macros and the Rust API
void XcpEventExtAt(tXcpEventId event, const uint8_t *base2, uint64_t clock);
void XcpEventExt2(tXcpEventId event, const uint8_t *base2, const uint8_t *base3);
void XcpEventExt2At(tXcpEventId event, const uint8_t *base2, const uint8_t *base3, uint64_t clock);
void XcpEventExtAt_Var(tXcpEventId event, uint64_t clock, uint8_t count, ...);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Get stack frame pointer
// Used by the Daq and A2l macros to get the stack frame pointer for stack relative addressing mode

// Linux, MACOS gnu and clang compiler
#if defined(__GNUC__) || defined(__clang__)

#define xcp_get_frame_addr() (const uint8_t *)__builtin_frame_address(0)

// MSVC compiler
#elif defined(_MSC_VER)

// Workaround to get the stackframe address with the MS compiler
// The __forceinline ensures this gets inlined, so effectively zero function call overhead
// The local variable approach compiles to a single LEA instruction
// Suppress the warning since this is intentional behavior for stack frame detection
#pragma warning(push)
#pragma warning(disable : 4172) // returning address of local variable - intentional
static __forceinline const uint8_t *xcp_get_frame_addr_msvc(void) {
    volatile char stack_marker = 0;
    return (const uint8_t *)&stack_marker;
}
#pragma warning(pop)
#define xcp_get_frame_addr() xcp_get_frame_addr_msvc()

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
// Uses thread local storage to create a thread safe once pattern for the event lookup
// All macros can be used to measure variables registered in absolute addressing mode as well

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

/// Trigger the XCP event 'name' for stack relative or absolute addressing
/// Cache the event name lookup
/// @param name Name given as identifier
#define DaqTriggerEvent(name)                                                                                                                                                      \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static THREAD_LOCAL tXcpEventId trg__AAS__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                 \
        if (trg__AAS__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                          \
            trg__AAS__##name = XcpFindEvent(#name, NULL);                                                                                                                          \
            assert(trg__AAS__##name != XCP_UNDEFINED_EVENT_ID);                                                                                                                    \
        }                                                                                                                                                                          \
        XcpEventExt_Var(trg__AAS__##name, 1, xcp_get_frame_addr());                                                                                                                \
    }
#define DaqTriggerEventAt(name, clock)                                                                                                                                             \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static THREAD_LOCAL tXcpEventId trg__AAS__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                 \
        if (trg__AAS__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                          \
            trg__AAS__##name = XcpFindEvent(#name, NULL);                                                                                                                          \
            assert(trg__AAS__##name != XCP_UNDEFINED_EVENT_ID);                                                                                                                    \
        }                                                                                                                                                                          \
        XcpEventExtAt_Var(trg__AAS__##name, clock, 1, xcp_get_frame_addr());                                                                                                       \
    }

/// Trigger the XCP event by handle 'event_id' for stack relative or absolute addressing
/// No lookup
/// @param name Event given as id
#define DaqTriggerEvent_i(event_id)                                                                                                                                                \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static THREAD_LOCAL tXcpEventId trg__AAS__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                 \
        trg__AAS__##name = event_id;                                                                                                                                               \
        XcpEventExt(event_id, xcp_get_frame_addr());                                                                                                                               \
    }
#define DaqTriggerEventAt_i(event_id, clock)                                                                                                                                       \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static THREAD_LOCAL tXcpEventId trg__AAS__##name = event_id;                                                                                                               \
        trg__AAS__##name = event_id;                                                                                                                                               \
        XcpEventExtAt(event_id, xcp_get_frame_addr(), clock);                                                                                                                      \
    }

/// Trigger the XCP event 'name' for absolute, stack and relative addressing mode with given individual base address (from A2lSetRelativeAddrMode(base_addr))
/// Cache the event name lookup
#define DaqTriggerEventExt(name, base_addr)                                                                                                                                        \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static THREAD_LOCAL tXcpEventId trg__AASD__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                \
        if (trg__AASD__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                         \
            trg__AASD__##name = XcpFindEvent(#name, NULL);                                                                                                                         \
            assert(trg__AASD__##name != XCP_UNDEFINED_EVENT_ID);                                                                                                                   \
        }                                                                                                                                                                          \
        XcpEventExt_Var(trg__AASD__##name, 2, xcp_get_frame_addr(), (const uint8_t *)base_addr);                                                                                   \
    }

/// Trigger the XCP event 'name' for absolute, stack and relative addressing mode with given individual base address (from A2lSetRelativeAddrMode(base_addr))
/// Name is a character string, but must not be a string literal
/// Cache the event lookup
/// @param name Name given as identifier
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
/// No lookup
/// @param event_id Event given as id
/// @param base_addr Base address pointer for relative addressing mode
#define DaqTriggerEventExt_i(event_id, base_addr)                                                                                                                                  \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static THREAD_LOCAL tXcpEventId trg__AASD__##name = event_id;                                                                                                              \
        XcpEventExt_Var(trg__AASD__##name, 2, xcp_get_frame_addr(), (const uint8_t *)base_addr);                                                                                   \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Combined create and trigger DAQ event macros

#define DaqCreateAndTriggerEvent(name)                                                                                                                                             \
    if (XcpIsActivated()) {                                                                                                                                                        \
        static THREAD_LOCAL tXcpEventId evt__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                      \
        static THREAD_LOCAL tXcpEventId trg__AAS__##name = XCP_UNDEFINED_EVENT_ID;                                                                                                 \
        if (trg__AAS__##name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                          \
            evt__##name = trg__AAS__##name;                                                                                                                                        \
            trg__AAS__##name = XcpCreateEvent(#name, 0, 0);                                                                                                                        \
        }                                                                                                                                                                          \
        XcpEventExt_Var(trg__AAS__##name, 1, xcp_get_frame_addr());                                                                                                                \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Build time A2L file generation helpers
// @@@@ NOTE: Work in progress

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

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Measurement of stack variables

// Mark a (local) variable as volatile and used to prevent compiler optimizations and force spilling it to a memory location
// XCPlite can measure stack variables but not registers
// The A2L updater or creator can handle only simple location expressions such as absolute addresses, stack relative addresses (CFA) and calibration segment relative addresses
// For complex cases, use the DaqCapture macro to capture the variable in a hidden static variable
/// Attribute to mark a variable as measurable by XCP/A2L
/// Example usage: XCP_MEA int32_t my_var = 0;
#define XCP_MEA volatile
// #define XCP_MEA volatile __attribute__((used))

/// Capture a local variable for measurement with a specific event
/// The variable must be in scope when the event is triggered with DaqTriggerEvent
/// The build time A2L file generator will find the hidden static variable 'daq__##event##__##var' and create the measurement with approriate addressing mode and event association
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

/// Check if XCP has been activated
bool XcpIsActivated(void);

/// Check if XCP is connected
bool XcpIsConnected(void);

// Project name
void XcpSetProjectName(const char *name);
const char *XcpGetProjectName(void);

// A2L file name
// Notify xcplib there is a valid A2L with this name to be provided for upload via XCP command GET_ID
void XcpSetA2lName(const char *name);
const char *XcpGetA2lName(void);

// EPK software version identifier
void XcpSetEpk(const char *epk);
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
void ApplXcpRegisterFlushCallback(uint8_t (*cb_flush)(void)); // Internal function used by the Rust API
void ApplXcpRegisterCallbacks(bool (*cb_connect)(uint8_t mode), uint8_t (*cb_prepare_daq)(void), uint8_t (*cb_start_daq)(void), void (*cb_stop_daq)(void),
                              uint8_t (*cb_freeze_daq)(uint8_t clear, uint16_t config_id), uint8_t (*cb_get_cal_page)(uint8_t segment, uint8_t mode),
                              uint8_t (*cb_set_cal_page)(uint8_t segment, uint8_t page, uint8_t mode), uint8_t (*cb_freeze_cal)(void),
                              uint8_t (*cb_init_cal)(uint8_t src_page, uint8_t dst_page), uint8_t (*cb_read)(uint32_t src, uint8_t size, uint8_t *dst),
                              uint8_t (*cb_write)(uint32_t dst, uint8_t size, const uint8_t *src, uint8_t delay), uint8_t (*cb_flush)(void));

// xcplib utility functions used for the demos to keep them clean and platform-independent
uint64_t clockGetNs(void);
uint64_t clockGetUs(void);
void sleepUs(uint32_t us);

#ifdef __cplusplus
} // extern "C"
#endif
