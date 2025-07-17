#pragma once
#define __XCP_LITE_H__

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#include <stdbool.h> // for bool
#include <stdint.h>  // for uint16_t, uint32_t, uint8_t

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINTF, DBG_PRINT, ...
#include "xcp.h"       // for XCP protocol definitions
#include "xcpQueue.h"  // for tQueueHandle
#include "xcp_cfg.h"   // for XCP_PROTOCOL_LAYER_VERSION, XCP_ENABLE_...
#include "xcptl_cfg.h" // for XCPTL_MAX_CTO_SIZE

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************/
/* Protocol layer interface                                                 */
/****************************************************************************/

// Initialization for the XCP Protocol Layer
void XcpInit(bool activate);
bool XcpIsInitialized(void);
bool XcpIsActivated(void);
void XcpStart(tQueueHandle queueHandle, bool resumeMode);
void XcpReset(void);

// EPK software version identifier
#ifndef XCP_EPK_MAX_LENGTH
#define XCP_EPK_MAX_LENGTH 31 // Maximum length of EPK string
#endif
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
uint8_t XcpEventDynRelAt(tXcpEventId event, const uint8_t *dyn_base, const uint8_t *rel_base, uint64_t clock);
void XcpEventExt(tXcpEventId event, const uint8_t *base);
void XcpEvent(tXcpEventId event);

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
    uint32_t cycleTimeNs;              // cycle time in nanoseconds, 0 means sporadic event
    uint8_t priority;                  // priority 0 = queued, 1 = pushing, 2 = realtime
    uint8_t res;                       // reserved
    char name[XCP_MAX_EVENT_NAME + 1]; // event name
} tXcpEvent;

typedef struct {
    MUTEX mutex;
    uint16_t count;                       // number of events
    tXcpEvent event[XCP_MAX_EVENT_COUNT]; // event list
} tXcpEventList;

// Create an XCP event (internal use)
tXcpEventId XcpCreateIndexedEvent(const char *name, uint16_t index, uint32_t cycleTimeNs, uint8_t priority);
// Add a measurement event to event list, return event number (0..MAX_EVENT-1)
tXcpEventId XcpCreateEvent(const char *name, uint32_t cycleTimeNs /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/);
// Add a measurement event to event list, return event number (0..MAX_EVENT-1), thread safe, if name exists, an instance id is appended to the name
tXcpEventId XcpCreateEventInstance(const char *name, uint32_t cycleTimeNs /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/);

// Get event list
tXcpEventList *XcpGetEventList(void);

// Get event id by name, returns XCP_UNDEFINED_EVENT_ID if not found
tXcpEventId XcpFindEvent(const char *name, uint16_t *count);

// Get event name by id, returns NULL if not found
const char *XcpGetEventName(tXcpEventId event);
// Get the event index (1..), return 0 if not found
uint16_t XcpGetEventIndex(tXcpEventId event);
// Get the event descriptor struct by id, returns NULL if not found
tXcpEvent *XcpGetEvent(tXcpEventId event);

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
lock-free, wait-free CalSeg RCU:
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
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE
    uint8_t mode;      // reuested for freeze and preload
    uint32_t file_pos; // position of the calibration segment in the persistency file
#endif
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
tXcpCalSeg *XcpGetCalSeg(tXcpCalSegIndex calseg);

// Get the name of the calibration segment
const char *XcpGetCalSegName(tXcpCalSegIndex calseg);

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
/* XCP packet                                                               */
/****************************************************************************/

typedef union {
    uint8_t b[((XCPTL_MAX_CTO_SIZE + 3) & 0xFFC)];
    uint16_t w[((XCPTL_MAX_CTO_SIZE + 3) & 0xFFC) / 2];
    uint32_t dw[((XCPTL_MAX_CTO_SIZE + 3) & 0xFFC) / 4];
} tXcpCto;

/****************************************************************************/
/* DAQ tables                                                               */
/****************************************************************************/

#define XCP_UNDEFINED_DAQ_LIST 0xFFFF

// ODT
// size = 8 byte
#pragma pack(push, 1)
typedef struct {
    uint16_t first_odt_entry; /* Absolute odt entry number */
    uint16_t last_odt_entry;  /* Absolute odt entry number */
    uint16_t size;            /* Number of bytes */
    uint16_t res;
} tXcpOdt;
#pragma pack(pop)
// static_assert(sizeof(tXcpOdt) == 8, "Error: size of tXcpOdt is not equal to 8");

/* DAQ list */
// size = 12 byte
#pragma pack(push, 1)
typedef struct {
    uint16_t last_odt;  /* Absolute odt number */
    uint16_t first_odt; /* Absolute odt number */
    uint16_t EVENT_ID;  /* Associated event */
#ifdef XCP_MAX_EVENT_COUNT
    uint16_t next; /* Next DAQ list associated to EVENT_ID */
#else
    uint16_t res1;
#endif
    uint8_t mode;
    uint8_t state;
    uint8_t priority;
    uint8_t addr_ext;
} tXcpDaqList;
#pragma pack(pop)
// static_assert(sizeof(tXcpDaqList) == 12, "Error: size of tXcpDaqList is not equal to 12");

/* Dynamic DAQ list structure in a linear memory block with size XCP_DAQ_MEM_SIZE + 8  */
#pragma pack(push, 1)
typedef struct {
    uint16_t odt_entry_count; // Total number of ODT entries in ODT entry addr and size arrays
    uint16_t odt_count;       // Total number of ODTs in ODT array
    uint16_t daq_count;       // Number of DAQ lists in DAQ list array
    uint16_t res;
#ifdef XCP_ENABLE_DAQ_RESUME
    uint16_t config_id;
    uint16_t res1;
#endif
#ifdef XCP_MAX_EVENT_COUNT
    uint16_t daq_first[XCP_MAX_EVENT_COUNT]; // Event channel to DAQ list mapping
#endif

    // DAQ array
    // size and alignment % 8
    // memory layout:
    //  tXcpDaqList[] - DAQ list array
    //  tXcpOdt[]     - ODT array
    //  uint32_t[]    - ODT entry addr array
    //  uint8_t[]     - ODT entry size array
    //  uint8_t[]     - ODT entry addr extension array (optional)
    union {
        // DAQ array
        tXcpDaqList daq_list[XCP_DAQ_MEM_SIZE / sizeof(tXcpDaqList)];
        // ODT array
        tXcpOdt odt[XCP_DAQ_MEM_SIZE / sizeof(tXcpOdt)];
        // ODT entry addr array
        uint32_t odt_entry_addr[XCP_DAQ_MEM_SIZE / 4];
        // ODT entry size array
        uint8_t odt_entry_size[XCP_DAQ_MEM_SIZE / 1];
        // ODT entry addr extension array
#ifdef XCP_ENABLE_DAQ_ADDREXT
        uint8_t odt_entry_addr_ext[XCP_DAQ_MEM_SIZE / 1];
#endif
        uint64_t b[XCP_DAQ_MEM_SIZE / 8 + 1];
    } u;

} tXcpDaqLists;
#pragma pack(pop)

/****************************************************************************/
/* Protocol layer state data                                                */
/****************************************************************************/

typedef struct {

    uint16_t SessionStatus;

    tXcpCto Crm;    /* response message buffer */
    uint8_t CrmLen; /* RES,ERR message length */

#ifdef XCP_ENABLE_DYN_ADDRESSING
    atomic_bool CmdPending;
    tXcpCto CmdPendingCrm;    /* pending command message buffer */
    uint8_t CmdPendingCrmLen; /* pending command message length */
#endif

#ifdef DBG_LEVEL
    uint8_t CmdLast;
    uint8_t CmdLast1;
#endif

    /* Memory Transfer Address as pointer (ApplXcpGetPointer) */
    uint8_t *MtaPtr;
    uint32_t MtaAddr;
    uint8_t MtaExt;

    /* State info from SET_DAQ_PTR for WRITE_DAQ and WRITE_DAQ_MULTIPLE */
    uint16_t WriteDaqOdtEntry; // Absolute odt index
    uint16_t WriteDaqOdt;      // Absolute odt index
    uint16_t WriteDaqDaq;

    /* DAQ */
    tQueueHandle Queue;        // Daq queue handle
    tXcpDaqLists *DaqLists;    // DAQ lists
    atomic_bool DaqRunning;    // DAQ is running
    uint64_t DaqStartClock64;  // DAQ start time
    uint32_t DaqOverflowCount; // DAQ queue overflow

    /* EPK */
    char Epk[XCP_EPK_MAX_LENGTH + 1]; // EPK string, null terminated

    /* Optional event list */
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    tXcpEventList EventList;
#endif

    /* Optional calibration segment list */
#ifdef XCP_ENABLE_CALSEG_LIST
    tXcpCalSegList CalSegList;
#endif

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103

#ifdef XCP_ENABLE_PROTOCOL_LAYER_ETH

#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
    uint16_t ClusterId;
#endif

#pragma pack(push, 1)
    struct {
        T_CLOCK_INFO server;
#ifdef XCP_ENABLE_PTP
        T_CLOCK_INFO_GRANDMASTER grandmaster;
        T_CLOCK_INFO_RELATION relation;
#endif
    } ClockInfo;
#pragma pack(pop)
#endif
#endif // XCP_ENABLE_PROTOCOL_LAYER_ETH

} tXcpData;

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

#ifdef __cplusplus
} // extern "C"
#endif
