// Header file for the XCPlite xcplib application interface
// A2L generation functions and macros are in src/a2l.h
// Used for Rust bindgen to generate FFI bindings for xcplib

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "platform.h" // for THREAD_LOCAL

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// XCP on Ethernet server interface

/// Initialize the XCP on Ethernet server singleton.
/// @pre User has called XcpInit.
/// @param address Address to bind to.
/// @param port Port to bind to.
/// @param use_tcp Use TCP if true, otherwise UDP.
/// @param measurement_queue_size Measurement queue size in bytes. Includes the bytes occupied by the queue header and some space needed for alignment.
/// @return true on success, otherwise false.
bool XcpEthServerInit(uint8_t const *address, uint16_t port, bool use_tcp, uint32_t measurement_queue_size);

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

#define XCP_UNDEFINED_CALSEG 0xFFFF
typedef uint16_t tXcpCalSegIndex;
#define XCP_MAX_CALSEG_NAME 15 // defined in xcp_cfg.h

// Create a calibration segment
// Thread safe
// Returns the handle or XCP_UNDEFINED_CALSEG when out of memory
tXcpCalSegIndex XcpCreateCalSeg(const char *name, const void *default_page, uint16_t size);

// Get the name of the calibration segment
const char *XcpGetCalSegName(tXcpCalSegIndex calseg);

// Get the XCP/A2L address of a calibration segment
uint32_t XcpGetCalSegBaseAddress(tXcpCalSegIndex calseg);

// Lock a calibration segment and return a pointer to the ECU page
uint8_t const *XcpLockCalSeg(tXcpCalSegIndex calseg);

// Unlock a calibration segment
// Single threaded, must be used in the thread it was created
void XcpUnlockCalSeg(tXcpCalSegIndex calseg);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Events

#define XCP_UNDEFINED_EVENT_ID 0xFFFF
typedef uint16_t tXcpEventId;
#define XCP_MAX_EVENT_NAME 15 // defined in xcp_cfg.h

// Add a measurement event to event list, return event number (0..XCP_MAX_EVENT_COUNT-1)
tXcpEventId XcpCreateEvent(const char *name, uint32_t cycleTimeNs /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/);
// Add a measurement event to event list, return event number (0..XCP_MAX_EVENT_COUNT-1), thread safe, if name exists, an instance id is appended to the name
tXcpEventId XcpCreateEventInstance(const char *name, uint32_t cycleTimeNs /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/);
// Get event id by name, returns XCP_UNDEFINED_EVENT_ID if not found
tXcpEventId XcpFindEvent(const char *name, uint16_t *count);

// Create the XCP event 'name'
// Cycle time is set to sporadic and priority to normal
// Setting the cycle time would only have the benefit for the XCP client tool to estimate the expected data rate of a DAQ setup
#define DaqCreateEvent(name) XcpCreateEvent(#name, 0, 0)
#define DaqCreateEvent_s(name) XcpCreateEvent(name, 0, 0)

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// DAQ event trigger convenience macros

// Event name parameter is a symbol, a string (_s) or an event index (_i)
// Creates linker map file markers (static variables: daq_event_stackframe_'eventname'_ or daq_event_relative_'eventname'_) for the XCP event id used
// No need to take care to store the event id
// Uses thread local storage to create a thread safe once pattern for the event lookup
// Required option is XCP_ENABLE_DAQ_EVENT_LIST (must be set in xcp_cfg.h)

// All macros can be used to measure variables registered in absolute addressing mode as well

#ifndef get_stack_frame_pointer
#define get_stack_frame_pointer() (const uint8_t *)__builtin_frame_address(0)
#endif

// Not needed
// __builtin_frame_address should be available on all desired platforms
// static inline const uint8_t *get_stack_frame_pointer_(void) {
//  #if defined(__x86_64__) || defined(_M_X64)
//      void *fp;
//      __asm__ volatile("movq %%rbp, %0" : "=r"(fp));
//      return (uint8_t *)fp;
//  #elif defined(__i386__) || defined(_M_IX86)
//      void *fp;
//      __asm__ volatile("movl %%ebp, %0" : "=r"(fp));
//      return (uint8_t *)fp;
//  #elif defined(__aarch64__)
//      void *fp;
//      __asm__ volatile("mov %0, x29" : "=r"(fp));
//      return (uint8_t *)fp;
//  #elif defined(__arm__)
//      void *fp;
//      __asm__ volatile("mov %0, fp" : "=r"(fp));
//      return (uint8_t *)fp;
//  #else
//      return (uint8_t *)__builtin_frame_address(0);
//  #endif
//}

// Used by the DAQ macros
uint8_t XcpEventDynRelAt(tXcpEventId event, const uint8_t *dyn_base, const uint8_t *rel_base, uint64_t clock);
void XcpEventExt(tXcpEventId event, const uint8_t *base);
void XcpEvent(tXcpEventId event);

// Trigger the XCP event 'name' for stack only (DaqEvent) or relative and stack addressing (DaqEventDyn) mode
// Cache the event lookup
// Error if the event does not exist
#define DaqEvent(name)                                                                                                                                                             \
    {                                                                                                                                                                              \
        static THREAD_LOCAL tXcpEventId daq_event_stackframe_##name##_ = XCP_UNDEFINED_EVENT_ID;                                                                                   \
        if (daq_event_stackframe_##name##_ == XCP_UNDEFINED_EVENT_ID) {                                                                                                            \
            daq_event_stackframe_##name##_ = XcpFindEvent(#name, NULL);                                                                                                            \
            if (daq_event_stackframe_##name##_ == XCP_UNDEFINED_EVENT_ID) {                                                                                                        \
                assert(false);                                                                                                                                                     \
            }                                                                                                                                                                      \
        } else {                                                                                                                                                                   \
            XcpEventDynRelAt(daq_event_stackframe_##name##_, get_stack_frame_pointer(), get_stack_frame_pointer(), 0);                                                             \
        }                                                                                                                                                                          \
    }

// Trigger a XCP event with explicit 'event_id'
#define DaqEvent_i(event_id) XcpEventDynRelAt(event_id, get_stack_frame_pointer(), get_stack_frame_pointer(), 0);

// Trigger the XCP event 'name' for relative mode with individual base address
// Cache the event lookup
// Error if the event does not exist
#define DaqEventRelative(name, base_addr)                                                                                                                                          \
    {                                                                                                                                                                              \
        static THREAD_LOCAL tXcpEventId daq_event_relative_##name##_ = XCP_UNDEFINED_EVENT_ID;                                                                                     \
        if (daq_event_relative_##name##_ == XCP_UNDEFINED_EVENT_ID) {                                                                                                              \
            daq_event_relative_##name##_ = XcpFindEvent(#name, NULL);                                                                                                              \
            if (daq_event_relative_##name##_ == XCP_UNDEFINED_EVENT_ID) {                                                                                                          \
                assert(false);                                                                                                                                                     \
            }                                                                                                                                                                      \
        } else {                                                                                                                                                                   \
            XcpEventDynRelAt(daq_event_relative_##name##_, (const uint8_t *)base_addr, get_stack_frame_pointer(), 0);                                                              \
        }                                                                                                                                                                          \
    }

// Trigger the XCP event "name" for relative mode with individual base address
// Cache the event lookup
// Error if the event does not exist
#define DaqEventRelative_s(name, base_addr)                                                                                                                                        \
    {                                                                                                                                                                              \
        static THREAD_LOCAL tXcpEventId daq_event_relative_##name##_ = XCP_UNDEFINED_EVENT_ID;                                                                                     \
        if (daq_event_relative_##name##_ == XCP_UNDEFINED_EVENT_ID) {                                                                                                              \
            daq_event_relative_##name##_ = XcpFindEvent(name, NULL);                                                                                                               \
            if (daq_event_relative_##name##_ == XCP_UNDEFINED_EVENT_ID) {                                                                                                          \
                assert(false);                                                                                                                                                     \
            }                                                                                                                                                                      \
        } else {                                                                                                                                                                   \
            XcpEventDynRelAt(daq_event_relative_##name##_, (const uint8_t *)base_addr, get_stack_frame_pointer(), 0);                                                              \
        }                                                                                                                                                                          \
    }

// Trigger a XCP event with explicit 'event_id' for relative mode with individual base address
#define DaqEventRelative_i(event_id, base_addr) XcpEventDynRelAt(event_id, (const uint8_t *)base_addr, get_stack_frame_pointer(), 0);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Misc

// Logging
void XcpSetLogLevel(uint8_t level);

// Initialize the XCP singleton, must be called befor starting the server
void XcpInit(void);

// Set the A2L file name (for GET_ID IDT_ASAM_NAME, IDT_ASAM_NAME and for IDT_ASAM_UPLOAD)
// Used by the A2L generator
#define XCP_A2L_FILENAME_MAX_LENGTH 255 // Maximum length of A2L filename with extension
void ApplXcpSetA2lName(const char *name);

// EPK software version identifier
// Used by the A2L generator
#define XCP_EPK_MAX_LENGTH 32 // Maximum length of EPK string
void XcpSetEpk(const char *epk);

// Force Disconnect, stop DAQ, flush queue, flush pending calibrations
void XcpDisconnect(void);

// Send terminate session event to the XCP client
void XcpSendTerminateSessionEvent(void);

// Send a message to the XCP client
void XcpPrint(const char *str);

// Get the DAQ clock
uint64_t ApplXcpGetClock64(void);

// Register XCP callbacks
// Used by the Rust API
void ApplXcpRegisterCallbacks(bool (*cb_connect)(void), uint8_t (*cb_prepare_daq)(void), uint8_t (*cb_start_daq)(void), void (*cb_stop_daq)(void),
                              uint8_t (*cb_freeze_daq)(uint8_t clear, uint16_t config_id), uint8_t (*cb_get_cal_page)(uint8_t segment, uint8_t mode),
                              uint8_t (*cb_set_cal_page)(uint8_t segment, uint8_t page, uint8_t mode), uint8_t (*cb_freeze_cal)(void),
                              uint8_t (*cb_init_cal)(uint8_t src_page, uint8_t dst_page), uint8_t (*cb_read)(uint32_t src, uint8_t size, uint8_t *dst),
                              uint8_t (*cb_write)(uint32_t dst, uint8_t size, const uint8_t *src, uint8_t delay), uint8_t (*cb_flush)(void));

// Register a connect callback
// Used by the A2L generator
void ApplXcpRegisterConnectCallback(bool (*cb_connect)(void));

#ifdef __cplusplus
} // extern "C"
#endif
