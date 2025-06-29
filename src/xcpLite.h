#pragma once
#define __XCP_LITE_H__

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#include <stdbool.h> // for bool
#include <stdint.h>  // for uint16_t, uint32_t, uint8_t

#include "xcpQueue.h" // for tQueueHandle
#include "xcp_cfg.h"  // for XCP_PROTOCOL_LAYER_VERSION, XCP_ENABLE_...

/****************************************************************************/
/* Protocol layer interface                                                 */
/****************************************************************************/

// Initialization for the XCP Protocol Layer
void XcpInit(void);
bool XcpIsInitialized(void);
void XcpStart(tQueueHandle queueHandle, bool resumeMode);
void XcpReset(void);

// EPK software version identifier
#define XCP_EPK_MAX_LENGTH 32 // Maximum length of EPK string
void XcpSetEpk(const char *epk);
const char *XcpGetEpk(void);

// XCP command processor
// Execute an XCP command
uint8_t XcpCommand(const uint32_t *pCommand, uint8_t len);

// Let XCP handle non realtime critical background tasks
// Mus be called from the same thread that calls XcpCommand()
void XcpBackgroundTasks(void);

// Disconnect, stop DAQ, flush queue, flush pending calibrations
void XcpDisconnect(void);

// Trigger a XCP data acquisition event
typedef uint16_t tXcpEventId;
uint8_t XcpEventExtAt(tXcpEventId event, const uint8_t *base, uint64_t clock);
uint8_t XcpEventExt(tXcpEventId event, const uint8_t *base);
void XcpEventAt(tXcpEventId event, uint64_t clock);
void XcpEvent(tXcpEventId event);
uint8_t XcpEventDyn(tXcpEventId *event);

// Send an XCP event message
void XcpSendEvent(uint8_t evc, const uint8_t *d, uint8_t l);

// Send terminate session signal event
void XcpSendTerminateSessionEvent(void);

// Send a message to the XCP client
#ifdef XCP_ENABLE_SERV_TEXT
void XcpPrint(const char *str);
#endif

// Check state
bool XcpIsStarted(void);
bool XcpIsConnected(void);
uint16_t XcpGetSessionStatus(void);
bool XcpIsDaqRunning(void);
bool XcpIsDaqEventRunning(uint16_t event);
uint64_t XcpGetDaqStartTime(void);
uint32_t XcpGetDaqOverflowCount(void);

// Time synchronisation
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
#if XCP_PROTOCOL_LAYER_VERSION < 0x0103
#error "Protocol layer version must be >=0x0103"
#endif
uint16_t XcpGetClusterId(void);
#endif

// Logging
void XcpSetLogLevel(uint8_t level);

/****************************************************************************/
/* DAQ events                                                               */
/****************************************************************************/

#define XCP_UNDEFINED_EVENT_ID 0xFFFF

#ifdef XCP_ENABLE_DAQ_EVENT_LIST

#ifndef XCP_MAX_EVENT_COUNT
#error "Please define XCP_MAX_EVENT_COUNT!"
#endif
#if XCP_MAX_EVENT_COUNT & 1 != 0
#error "XCP_MAX_EVENT_COUNT must be even!"
#endif

#ifndef XCP_MAX_EVENT_NAME
#define XCP_MAX_EVENT_NAME 15
#endif

typedef struct {
    uint16_t daqList;                  // associated DAQ list
    uint16_t index;                    // Event instance index, 0 = single instance, 1.. = multiple instances
    uint8_t timeUnit;                  // timeCycle unit, 1ns=0, 10ns=1, 100ns=2, 1us=3, ..., 1ms=6, ...
    uint8_t timeCycle;                 // cycle time in units, 0 = sporadic or unknown
    uint8_t priority;                  // priority 0 = queued, 1 = pushing, 2 = realtime
    uint8_t res;                       // reserved
    char name[XCP_MAX_EVENT_NAME + 1]; // event name
} tXcpEvent;

typedef struct {
    MUTEX mutex;
    uint16_t count;                       // number of events
    tXcpEvent event[XCP_MAX_EVENT_COUNT]; // event list
} tXcpEventList;

// Add a measurement event to event list, return event number (0..MAX_EVENT-1)
tXcpEventId XcpCreateEvent(const char *name, uint32_t cycleTimeNs /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/);
// Add a measurement event to event list, return event number (0..MAX_EVENT-1), thread safe, if name exists, an instance id is appended to the name
tXcpEventId XcpCreateEventInstance(const char *name, uint32_t cycleTimeNs /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/);

// Get event list
tXcpEventList *XcpGetEventList(void);

// Get event id by name, returns XCP_UNDEFINED_EVENT_ID if not found
tXcpEventId XcpFindEvent(const char *name, uint16_t *count);

#endif // XCP_ENABLE_DAQ_EVENT_LIST

/****************************************************************************/
/* Calibration segments                                                     */
/****************************************************************************/

#ifdef XCP_ENABLE_CALSEG_LIST

#ifndef XCP_MAX_CALSEG_COUNT
#error "Please define XCP_MAX_CALSEG_COUNT!"
#endif
#if XCP_MAX_CALSEG_COUNT < 1 || XCP_MAX_CALSEG_COUNT > 255
#error "XCP_MAX_CALSEG_COUNT must be between 1 and 255!"
#endif

#ifndef XCP_MAX_CALSEG_NAME
#define XCP_MAX_CALSEG_NAME 15
#endif

/*
Single thread lock-free, wait-free CalSeg RCU:
    XCP receive thread command handler:
        On XCP write access
        if free_page != NULL
            Copy xcp_page to free_page
            NULL -> free_page --> xcp_page --> ecu_page_next
    ECU thread XcpCalSegLock:
        if ecu_page_next != ecu_page
            ecu_page_next --> ecu_page --> free_page

    Shared state between the XCP thread and the ECU thread is:
        - ecu_page_next: the next page to be accessed by the ECU thread
        - free_page: the page freed by the ECU thread
        - ecu_access
*/

// Calibration segment index
// The index of the calibration segment in the calibration segment list or XCP_UNDEFINED_CALSEG
#define XCP_UNDEFINED_CALSEG 0xFFFF
typedef uint16_t tXcpCalSegIndex;

// Calibration segment
typedef struct {
    atomic_uintptr_t ecu_page_next;
    atomic_uintptr_t free_page;
    atomic_uint_fast8_t ecu_access; // page number for ECU access
    atomic_uint_fast8_t lock_count; // lock count for the segment, 0 = unlocked
    const uint8_t *default_page;
    uint8_t *ecu_page;
    uint8_t *xcp_page;
    uint16_t size;
    uint8_t xcp_access;    // page number for XCP access
    bool write_pending;    // write pending because write delay
    bool free_page_hazard; // safe free page use is not guaranteed yet, it may be in use
    char name[XCP_MAX_CALSEG_NAME + 1];
} tXcpCalSeg;

// Calibration segment list
typedef struct {
    tXcpCalSeg calseg[XCP_MAX_CALSEG_COUNT];
    MUTEX mutex;
    uint16_t count;
    bool write_delayed; // atomic calibration (begin/end user command) in progress
} tXcpCalSegList;

// Get calibration segment  list
tXcpCalSegList const *XcpGetCalSegList(void);

// Get a pointer to the calibration segment struct
tXcpCalSeg const *XcpGetCalSeg(tXcpCalSegIndex calseg);

// Get the XCP/A2L address of a calibration segment
uint32_t XcpGetCalSegBaseAddress(tXcpCalSegIndex calseg);

// Create a calibration segment
// Thread safe
// Returns the handle or XCP_UNDEFINED_CALSEG when out of memory
tXcpCalSegIndex XcpCreateCalSeg(const char *name, const void *default_page, uint16_t size);

// Lock a calibration segment and return a pointer to the ECU page
uint8_t const *XcpLockCalSeg(tXcpCalSegIndex calseg);

// Unlock a calibration segment
// Single threaded, must be used in the thread it was created
void XcpUnlockCalSeg(tXcpCalSegIndex calseg);

#endif // XCP_ENABLE_CALSEG_LIST

/****************************************************************************/
/* Protocol layer external dependencies                                     */
/****************************************************************************/

// All callback functions supplied by the application ust be thread save

/* Callbacks on connect, disconnect, measurement prepare, start and stop */
bool ApplXcpConnect(void);
void ApplXcpDisconnect(void);
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
bool ApplXcpPrepareDaq(void);
#endif
void ApplXcpStartDaq(void);
void ApplXcpStopDaq(void);

/* Address conversions from A2L address to pointer and vice versa in absolute addressing mode */
#ifdef XCP_ENABLE_ABS_ADDRESSING
uint8_t *ApplXcpGetPointer(uint8_t xcpAddrExt, uint32_t xcpAddr); /* Create a pointer (uint8_t*) from xcpAddrExt and xcpAddr, returns NULL if no access */
uint32_t ApplXcpGetAddr(const uint8_t *p);                        // Calculate the xcpAddr address from a pointer
uint8_t *ApplXcpGetBaseAddr(void);                                // Get the base address for DAQ data access */
#endif

/* Read and write memory */
#ifdef XCP_ENABLE_APP_ADDRESSING
uint8_t ApplXcpReadMemory(uint32_t src, uint8_t size, uint8_t *dst);
uint8_t ApplXcpWriteMemory(uint32_t dst, uint8_t size, const uint8_t *src);
#endif

/* User command */
#ifdef XCP_ENABLE_USER_COMMAND
uint8_t ApplXcpUserCommand(uint8_t cmd);
#endif

/*
 Note 1:
   For DAQ performance and memory optimization:
   XCPlite DAQ tables do not store address extensions and do not use ApplXcpGetPointer(void), addr is stored as 32 Bit value and access is hardcoded by *(baseAddr+xcpAddr)
   All accessible DAQ data is within a 4GByte range starting at ApplXcpGetBaseAddr(void)
   Attempting to setup an ODT entry with address extension != XCP_ADDR_EXT_ABS, XCP_ADDR_EXT_DYN or XCP_ADDR_EXT_REL gives a CRC_ACCESS_DENIED error message

 Note 2:
   ApplXcpGetPointer may do address transformations according to active calibration page
*/

/* Switch calibration pages */

// Calibration segment number
// Is the type (uint8_t) used by XCP commands like GET_SEGMENT_INFO, SET_CAL_PAGE, ...
typedef uint8_t tXcpCalSegNumber;

// Calibration page number type
#define XCP_CALPAGE_DEFAULT_PAGE 1 // FLASH page
#define XCP_CALPAGE_WORKING_PAGE 0 // RAM page
#define XCP_CALPAGE_INVALID_PAGE 0xFF
typedef uint8_t tXcpCalPageNumber;

#ifdef XCP_ENABLE_CAL_PAGE
uint8_t ApplXcpSetCalPage(tXcpCalSegNumber segment, tXcpCalPageNumber page, uint8_t mode);
uint8_t ApplXcpGetCalPage(tXcpCalSegNumber segment, uint8_t mode);
uint8_t ApplXcpSetCalPage(tXcpCalSegNumber segment, tXcpCalPageNumber page, uint8_t mode);
#ifdef XCP_ENABLE_COPY_CAL_PAGE
uint8_t ApplXcpCopyCalPage(tXcpCalSegNumber srcSeg, tXcpCalPageNumber srcPage, tXcpCalSegNumber destSeg, tXcpCalPageNumber destPage);
#endif
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE
uint8_t ApplXcpCalFreeze(void);
#endif
#endif

/* DAQ clock */
uint64_t ApplXcpGetClock64(void);
#define CLOCK_STATE_SYNCH_IN_PROGRESS (0)
#define CLOCK_STATE_SYNCH (1)
#define CLOCK_STATE_FREE_RUNNING (7)
#define CLOCK_STATE_GRANDMASTER_STATE_SYNCH (1 << 3)
uint8_t ApplXcpGetClockState(void);

#ifdef XCP_ENABLE_PTP
#define CLOCK_STRATUM_LEVEL_UNKNOWN 255
#define CLOCK_STRATUM_LEVEL_ARB 16 // unsychronized
#define CLOCK_STRATUM_LEVEL_UTC 0  // Atomic reference clock
#define CLOCK_EPOCH_TAI 0          // Atomic monotonic time since 1.1.1970 (TAI)
#define CLOCK_EPOCH_UTC 1          // Universal Coordinated Time (with leap seconds) since 1.1.1970 (UTC)
#define CLOCK_EPOCH_ARB 2          // Arbitrary (epoch unknown)
bool ApplXcpGetClockInfoGrandmaster(uint8_t *uuid, uint8_t *epoch, uint8_t *stratum);
#endif

/* DAQ resume */
#ifdef XCP_ENABLE_DAQ_RESUME

uint8_t ApplXcpDaqResumeStore(uint16_t config_id);
uint8_t ApplXcpDaqResumeClear(void);

#endif

/* Get info for GET_ID command (pointer to and length of data) */
/* Supports IDT_ASCII, IDT_ASAM_NAME, IDT_ASAM_PATH, IDT_ASAM_URL, IDT_ASAM_EPK and IDT_ASAM_UPLOAD */
/* Returns 0 if not available */
uint32_t ApplXcpGetId(uint8_t id, uint8_t *buf, uint32_t bufLen);

/* Read a chunk (offset,size) of the A2L file for upload */
/* Return false if out of bounds */
#ifdef XCP_ENABLE_IDT_A2L_UPLOAD // Enable A2L content upload to host (IDT_ASAM_UPLOAD)
bool ApplXcpReadA2L(uint8_t size, uint32_t offset, uint8_t *data);
#endif

// Logging
void ApplXcpSetLogLevel(uint8_t level);

// Register callbacks
#ifdef XCP_ENABLE_APP_ADDRESSING
void ApplXcpRegisterCallbacks(bool (*cb_connect)(void), uint8_t (*cb_prepare_daq)(void), uint8_t (*cb_start_daq)(void), void (*cb_stop_daq)(void),
                              uint8_t (*cb_freeze_daq)(uint8_t clear, uint16_t config_id), uint8_t (*cb_get_cal_page)(uint8_t segment, uint8_t mode),
                              uint8_t (*cb_set_cal_page)(uint8_t segment, uint8_t page, uint8_t mode), uint8_t (*cb_freeze_cal)(void),
                              uint8_t (*cb_init_cal)(uint8_t src_page, uint8_t dst_page), uint8_t (*cb_read)(uint32_t src, uint8_t size, uint8_t *dst),
                              uint8_t (*cb_write)(uint32_t dst, uint8_t size, const uint8_t *src, uint8_t delay), uint8_t (*cb_flush)(void));

#else
void ApplXcpRegisterCallbacks(bool (*cb_connect)(void), uint8_t (*cb_prepare_daq)(void), uint8_t (*cb_start_daq)(void), void (*cb_stop_daq)(void),
                              uint8_t (*cb_freeze_daq)(uint8_t clear, uint16_t config_id), uint8_t (*cb_get_cal_page)(uint8_t segment, uint8_t mode),
                              uint8_t (*cb_set_cal_page)(uint8_t segment, uint8_t page, uint8_t mode), uint8_t (*cb_freeze_cal)(void),
                              uint8_t (*cb_init_cal)(uint8_t src_page, uint8_t dst_page));

#endif

void ApplXcpRegisterConnectCallback(bool (*cb_connect)(void));

// Set/get the A2L file name (for GET_ID IDT_ASAM_NAME, IDT_ASAM_NAME and for IDT_ASAM_UPLOAD)
#define XCP_A2L_FILENAME_MAX_LENGTH 255 // Maximum length of A2L filename with extension
void ApplXcpSetA2lName(const char *name);
const char *ApplXcpGetA2lName(void);
