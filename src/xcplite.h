#pragma once
#define __XCPLITE_H__

/*----------------------------------------------------------------------------
| File:
|   xcplite.h
|
| Description:
|   XCPlite internal header file for XCP protocol layer xcplite.c
|
| All functions, types and constants intended to be public API are declared in xcplib.h
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
 ----------------------------------------------------------------------------*/

#include <stdbool.h> // for bool
#include <stdint.h>  // for uint16_t, uint32_t, uint8_t

#include "cal.h"        // for calibration segment management if enabled
#include "dbg_print.h"  // for DBG_LEVEL, DBG_PRINTF, DBG_PRINT, ...
#include "platform.h"   // for atomics
#include "queue.h"      // for tQueueHandle
#include "shm.h"        // for shared memory management if enabled
#include "xcp.h"        // for XCP protocol definitions
#include "xcp_cfg.h"    // for XCP_PROTOCOL_LAYER_VERSION, XCP_ENABLE_...
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcptl_cfg.h"  // for XCPTL_MAX_CTO_SIZE

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************/
/* Protocol layer interface                                                 */
/****************************************************************************/

/// XcpInit mode flags
#define XCP_MODE_DEACTIVATE 0     ///< Initialize XCP singleton without activating the protocol layer (passive/off)
#define XCP_MODE_LOCAL 0x01       ///< Initialize and activate XCP, allocate state in static memory if libxcplite not compiled in SHM mode, otherwise allocate state in heap memory
#define XCP_MODE_PERSISTENCE 0x02 ///< Load the binary persistence file, to keep deterministic order of events and calibration segments, and load persisted calibration data
#define XCP_MODE_SHM 0x80         ///< Initialize and activate XCP, allocate state in POSIX shared memory
#define XCP_MODE_SHM_AUTO 0x04    ///< Set this flag to automatically choose leader as XCP server
#define XCP_MODE_SHM_SERVER 0x08  ///< Set this flag, to make this application the XCP server, regardless which application is started first

// Manage the XCP driver singleton
bool XcpInit(const char *name, const char *epk, uint8_t mode);
bool XcpIsActivated(void);
uint8_t XcpGetInitMode(void); // Returns the mode passed to XcpInit() — XCP_MODE_/DEACTIVATE/LOCAL/SHM/SHM_AUTO/SHM_SERVER
void XcpStart(tQueueHandle queue_handle, bool resumeMode);
void XcpDeinit(void);

// Project name
const char *XcpGetProjectName(void);

// EPK software version identifier
const char *XcpGetEpk(void);
const char *XcpGetEcuEpk(void); // Only in SHM mode different to XcpGetEpk(), which is for the application, while XcpGetEcuEpk() is for the overall ECU

// XCP command processor
// Execute an XCP command
uint8_t XcpCommand(const uint32_t *pCommand, uint8_t len);

// Let XCP handle non realtime critical background tasks
// Must be called in regular intervals from the same thread that calls XcpCommand() !!
void XcpBackgroundTasks(void);

// Disconnect, stop DAQ, flush queue, flush pending calibrations
void XcpDisconnect(void);

// XCP event identifier type
typedef uint16_t tXcpEventId;

// Trigger a XCP data acquisition event
// Absolute base address only
void XcpEvent(tXcpEventId event);
void XcpEventAt(tXcpEventId event, uint64_t clock);
// Single dyn base address
void XcpEventExt(tXcpEventId event, const uint8_t *base2);
void XcpEventExtAt(tXcpEventId event, const uint8_t *base2, uint64_t clock);
// Explicit dyn base address list
void XcpEventExt_(tXcpEventId event, int count, const uint8_t **bases);
void XcpEventExtAt_(tXcpEventId event, int count, const uint8_t **bases, uint64_t clock);
// Variadic dyn base address list
void XcpEventExt_Var(tXcpEventId event, int count, ...);
void XcpEventExtAt_Var(tXcpEventId event, uint64_t clock, int count, ...);

// Enable or disable a XCP DAQ event
void XcpEventEnable(tXcpEventId event, bool enable);

// Send a XCP event message
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

#define XCP_DAQ_EVENT_FLAG_DISABLED 0x01 // Event is disabled
#define XCP_DAQ_EVENT_FLAG_PRIORITY 0x02 // Event priority flag

typedef struct {
    uint32_t cycle_time_ns; // Cycle time in nanoseconds, 0 means sporadic event
    uint16_t index;         // Event instance index, 0 = single instance, 1.. = multiple instances
    uint16_t daq_first;     // First associated DAQ list, linked list
    uint8_t flags;          // Control flags for the event
#ifdef OPTION_SHM_MODE      // app_id in tXcpEvent
    uint8_t app_id;         // In SHM mode, the event has an application id
#endif
#ifdef XCP_ENABLE_DAQ_PRESCALER
    uint8_t daq_prescaler;     // Current prescaler set with SET_DAQ_LIST_MODE
    uint8_t daq_prescaler_cnt; // Current prescaler counter
#endif
    char name[XCP_MAX_EVENT_NAME + 1]; // Event name
} tXcpEvent;

typedef struct {
    atomic_uint_fast16_t count;           // number of events
    tXcpEvent event[XCP_MAX_EVENT_COUNT]; // event list
} tXcpEventList;

// Create an XCP event (internal use only, not thread safe)
tXcpEventId XcpCreateIndexedEvent(const char *name, uint16_t index, uint32_t cycle_time_ns, uint8_t priority);

// Add a measurement event to event list, return event number (0..MAX_EVENT-1)
tXcpEventId XcpCreateEvent(const char *name, uint32_t cycle_time_ns /* ns */, uint8_t priority /* 0 = queued, >=1 flushing*/);
// Add a measurement event to event list, return event number (0..MAX_EVENT-1), thread safe, if name exists, an instance id is appended to the name
tXcpEventId XcpCreateEventInstance(const char *name, uint32_t cycle_time_ns /* ns */, uint8_t priority /* 0 = queued, >=1 flushing */);

// Get event list
const tXcpEventList *XcpGetEventList(void);

// Get the number of events in the XCP event list
uint16_t XcpGetEventCount(void);

// Get event id by name, returns XCP_UNDEFINED_EVENT_ID if not found
// In SHM mode, only searches within the calling process's own events (scoped by app_id)
tXcpEventId XcpFindEvent(const char *name);

// Get event name by id, returns NULL if not found
const char *XcpGetEventName(tXcpEventId event);
// Get the event index (1..), return 0 if not found
uint16_t XcpGetEventIndex(tXcpEventId event);
// Get the event descriptor struct by id, returns NULL if not found
const tXcpEvent *XcpGetEvent(tXcpEventId event);

#ifdef OPTION_SHM_MODE // get event application id
// In SHM mode, get event application id
uint8_t XcpGetEventAppId(tXcpEventId event);
#endif // SHM_MODE

#endif // XCP_ENABLE_DAQ_EVENT_LIST

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
    uint16_t event_id;  /* Associated event */
#ifdef XCP_MAX_EVENT_COUNT
    uint16_t next; /* Next DAQ list associated to event_id */
#else
    uint16_t res1;
#endif
    uint8_t mode;
    uint8_t state;
    uint8_t priority;
    uint8_t addr_ext;
} tXcpDaqList;
#pragma pack(pop)
static_assert(sizeof(tXcpDaqList) == 12, "Error: size of tXcpDaqList is not equal to 12");

/* Dynamic DAQ list structure in a linear memory block with size XCP_DAQ_MEM_SIZE + 8  */
#pragma pack(push, 1)
typedef struct {
    uint16_t odt_entry_count; // Total number of ODT entries in ODT entry addr and size arrays
    uint16_t odt_count;       // Total number of ODTs in ODT array
    uint16_t daq_count;       // Number of DAQ lists in DAQ list array
    uint16_t res;
#ifdef XCP_ENABLE_DAQ_RESUME
    uint16_t config_id;
#endif
#if !defined(XCP_ENABLE_DAQ_EVENT_LIST) && defined(XCP_MAX_EVENT_COUNT)
    uint16_t daq_first[XCP_MAX_EVENT_COUNT]; // Event channel to DAQ list mapping when there is no event management
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
        tXcpDaqList daq_list[(size_t)XCP_DAQ_MEM_SIZE / sizeof(tXcpDaqList)];
        // ODT array
        tXcpOdt odt[(size_t)XCP_DAQ_MEM_SIZE / sizeof(tXcpOdt)];
        // ODT entry addr array
        uint32_t odt_entry_addr[(size_t)XCP_DAQ_MEM_SIZE / 4];
        // ODT entry size array
        uint8_t odt_entry_size[(size_t)XCP_DAQ_MEM_SIZE / 1];
        // ODT entry addr extension array
#ifdef XCP_ENABLE_DAQ_ADDREXT
        uint8_t odt_entry_addr_ext[(size_t)XCP_DAQ_MEM_SIZE / 1];
#endif
        uint64_t b[XCP_DAQ_MEM_SIZE / 8 + 1];
    } u;

} tXcpDaqLists;
#pragma pack(pop)

/****************************************************************************/
/* Protocol layer state                                                     */
/****************************************************************************/

// XCP protocol layer global or shared state
// Can be stored in shared memory, accessed atomically where needed, all fields must be safe for concurrent access across processes
// No pointers allowed that require fixup across processes, using offsets instead
typedef struct XcpData {

    uint16_t session_status; // must be the first field of the struct

#ifdef OPTION_SHM_MODE // SHM header in XCP data
    tShmHeader shm_header;
#endif

    tXcpCto crm;     /* response message buffer */
    uint8_t crm_len; /* RES,ERR message length */

#ifdef XCP_ENABLE_DYN_ADDRESSING
    ATOMIC_BOOL cmd_pending;
    tXcpCto cmd_pending_crm;     /* pending command message buffer */
    uint8_t cmd_pending_crm_len; /* pending command message length */
#endif

#ifdef DBG_LEVEL
    uint8_t cmd_last;
    uint8_t cmd_last1;
#endif

    /* DAQ */
    ATOMIC_BOOL daq_running;     // DAQ is running
    tXcpDaqLists daq_lists;      // DAQ list
    uint32_t daq_overflow_count; // DAQ queue overflow

    /* Optional event list */
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    tXcpEventList event_list;
#endif

    /* Optional calibration segment list */
#ifdef XCP_ENABLE_CALSEG_LIST
    tXcpCalSegList cal_seg_list;
#endif

} tXcpData;

/****************************************************************************/

// Process-local state (not shared, one instance per process)
// Contains fields that are process-local or different per process
// Can not live in shared memory
typedef struct XcpLocalData {
    // Initialisation mode (XCP_MODE_DEACTIVATE / XCP_MODE_LOCAL / XCP_MODE_SHM / XCP_MODE_SHM_AUTO / XCP_MODE_SHM_SERVER)
    uint8_t init_mode;

#ifdef OPTION_SHM_MODE  // SHM header in tXcpLocalData
    uint8_t shm_app_id; // Index in shm_header.app_list,  SHM_MAX_APP_COUNT = no slot assigned yet
    bool shm_server;    // This process is the XCP server
    bool shm_leader;    // This process created the shared memory segment, responsible for initializing
#endif

    // Memory transfer address (virtual pointer, OS handle)
    uint8_t *mta_ptr;   // Memory Transfer Address as pointer (process virtual address)
    uint32_t mta_addr;  // MTA as encoded XCP address (also kept here for reference)
    uint8_t mta_ext;    // MTA address extension
    tQueueHandle queue; // DAQ queue handle (process-local OS handle)

    // SET_DAQ_PTR cursor (XCP command thread only)
    uint16_t write_daq_odt_entry; // Absolute odt entry index
    uint16_t write_daq_odt;       // Absolute odt index
    uint16_t write_daq_daq;       // DAQ list index

    // DAQ timing (XCP command thread only)
    uint64_t daq_start_clock; // DAQ start timestamp

    // Local mutexes for event and calseg creation
    // Between processes, namespaces are separated, memory and index allocation itself is thread-safe and lockless
    MUTEX cal_seg_list_mutex;
    MUTEX event_list_mutex;

    // Per-process identity
    char project_name[XCP_PROJECT_NAME_MAX_LENGTH + 1]; // Project name string, null terminated
    char epk[XCP_EPK_MAX_LENGTH + 1];                   // EPK string, null terminated

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
#ifdef XCP_ENABLE_PROTOCOL_LAYER_ETH
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
    uint16_t cluster_id;
#endif

#pragma pack(push, 1)
    struct {
        T_CLOCK_INFO server;
#ifdef XCP_ENABLE_PTP
        T_CLOCK_INFO_GRANDMASTER grandmaster;
        T_CLOCK_INFO_RELATION relation;
#endif
    } clock_info;
#pragma pack(pop)
#endif
#endif // XCP_ENABLE_PROTOCOL_LAYER_ETH

} tXcpLocalData;

/****************************************************************************/
/* Protocol layer external dependencies                                     */
/****************************************************************************/

/* Callbacks on connect, disconnect, measurement prepare, start and stop */
bool ApplXcpConnect(uint8_t mode);
void ApplXcpDisconnect(void);
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
bool ApplXcpPrepareDaq(void);
#endif
void ApplXcpStartDaq(void);
void ApplXcpStopDaq(void);

/* Address conversions from A2L address to pointer and vice versa in absolute addressing mode */
const uint8_t *ApplXcpGetBaseAddr(void);      // Get the base address for absolute addressing mode */
void ApplXcpSetBaseAddr(const uint8_t *addr); // Set base address for absolute addressing mode, only needed for special cases where the default base addr is not suitable
extern const uint8_t *gXcpBaseAddr;
#define xcp_get_base_addr() gXcpBaseAddr     // For runtime optimization, some macros use xcp_get_base_addr() instead of ApplXcpGetBaseAddr()
uint32_t ApplXcpGetAddr(const uint8_t *p);   // Calculate the absolute XCP/A2L 32 bit address from a pointer
uint8_t ApplXcpGetAddrExt(const uint8_t *p); // Get the absolute XCP/A2L 8 bit address extension from a pointer
const uint8_t *ApplXcpGetModuleAddr(void);   // Get the module base address, used as default base address for absolute addressing mode

/* Check memory access permissions, called in prepare DAQ */
uint8_t ApplXcpCheckMemory(uint8_t ext, uint32_t addr, uint8_t size);

/* Read and write memory for application addressing mode XCP_ADDR_EXT_APP */
#ifdef XCP_ENABLE_APP_ADDRESSING
uint8_t ApplXcpReadMemory(uint32_t src, uint8_t size, uint8_t *dst);
uint8_t ApplXcpWriteMemory(uint32_t dst, uint8_t size, const uint8_t *src);
#endif

/* User command */
#ifdef XCP_ENABLE_USER_COMMAND
uint8_t ApplXcpUserCommand(uint8_t cmd);
#endif

// Calibration segment number
// Is the type (uint8_t) used by XCP commands like GET_SEGMENT_INFO, SET_CAL_PAGE, ...
typedef uint8_t tXcpCalSegNumber;

// Calibration page number
#define XCP_CALPAGE_DEFAULT_PAGE 1 // FLASH page
#define XCP_CALPAGE_WORKING_PAGE 0 // RAM page
#define XCP_CALPAGE_INVALID_PAGE 0xFF
typedef uint8_t tXcpCalPageNumber;

/* Calibration page handling by application */
#if defined(XCP_ENABLE_CAL_PAGE) && !defined(XCP_ENABLE_CALSEG_LIST)
uint8_t ApplXcpSetCalPage(tXcpCalSegNumber segment, tXcpCalPageNumber page, uint8_t mode);
uint8_t ApplXcpGetCalPage(tXcpCalSegNumber segment, uint8_t mode);
#ifdef XCP_ENABLE_COPY_CAL_PAGE
uint8_t ApplXcpCopyCalPage(tXcpCalSegNumber srcSeg, tXcpCalPageNumber srcPage, tXcpCalSegNumber destSeg, tXcpCalPageNumber destPage);
#endif
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE
uint8_t ApplXcpCalFreeze(void);
#endif
#endif

/* DAQ clock provided by application*/
uint64_t ApplXcpGetClock64(void);
#define CLOCK_STATE_SYNCH_IN_PROGRESS (0)
#define CLOCK_STATE_SYNCH (1)
#define CLOCK_STATE_FREE_RUNNING (7)
#define CLOCK_STATE_GRANDMASTER_STATE_SYNCH (1 << 3) // not used yet
uint8_t ApplXcpGetClockState(void);

#ifdef XCP_ENABLE_PTP
#define CLOCK_STRATUM_LEVEL_UNKNOWN 255
#define CLOCK_STRATUM_LEVEL_ARB 16
#define CLOCK_STRATUM_LEVEL_UTC 0 // Atomic reference clock
#define CLOCK_EPOCH_TAI 0         // Atomic monotonic time since 1.1.1970 (TAI)
#define CLOCK_EPOCH_UTC 1         // Universal Coordinated Time (with leap seconds) since 1.1.1970 (UTC)
#define CLOCK_EPOCH_ARB 2         // Arbitrary (epoch unknown)
bool ApplXcpGetClockInfoGrandmaster(uint8_t *client_uuid, uint8_t *grandmaster_uuid, uint8_t *epoch, uint8_t *stratum);
#endif

// Register clock callbacks
void ApplXcpRegisterGetClockCallback(uint64_t (*cb_get_clock)(void));
void ApplXcpRegisterGetClockStateCallback(uint8_t (*cb_get_clock_state)(void));
void ApplXcpRegisterGetClockInfoGrandmasterCallback(bool (*cb_get_clock_info_grandmaster)(uint8_t *client_uuid, uint8_t *grandmaster_uuid, uint8_t *epoch, uint8_t *stratum));

/* DAQ resume */
#ifdef XCP_ENABLE_DAQ_RESUME
uint8_t ApplXcpDaqResumeStore(uint16_t config_id);
uint8_t ApplXcpDaqResumeClear(void);
#endif

/* Get info for GET_ID command (pointer to and length of data) */
/* Supports IDT_ASCII, IDT_ASAM_NAME, IDT_ASAM_PATH, IDT_ASAM_URL, IDT_ASAM_EPK and IDT_ASAM_UPLOAD */
/* Returns 0 if not available or buffer size exceeded */
uint32_t ApplXcpGetId(uint8_t id, uint8_t *buf, uint32_t bufLen);

/* Read a chunk (offset,size) of a file for upload */
/* Return false if out of bounds */
#if defined(XCP_ENABLE_IDT_A2L_UPLOAD) || defined(XCP_ENABLE_IDT_ELF_UPLOAD) // Enable A2L or ELF content upload to host
bool ApplXcpReadFile(uint8_t size, uint32_t offset, uint8_t *data);
#endif

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

// Set/get the ELF and A2L file name (for GET_ID)
void XcpSetA2lName(const char *name);
const char *XcpGetA2lName(void);
void XcpSetElfName(const char *name);
const char *XcpGetElfName(void);

#ifdef __cplusplus
} // extern "C"
#endif

// Some metrics collected by the XCP protocol layer for debugging and performance analysis
#ifdef TEST_ENABLE_DBG_METRICS
#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t gXcpWritePendingCount;
extern uint32_t gXcpCalSegPublishAllCount;
extern uint32_t gXcpDaqEventCount;
extern uint32_t gXcpTxPacketCount;
extern uint32_t gXcpTxMessageCount;
extern uint32_t gXcpTxIoVectorCount;
extern uint32_t gXcpRxPacketCount;
#ifdef __cplusplus
}
#endif
#endif // TEST_ENABLE_DBG_METRICS
