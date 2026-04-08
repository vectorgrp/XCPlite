#pragma once
#define __CAL_H__

/*----------------------------------------------------------------------------
| File:
|   cal.h
|
| Description:
|   XCPlite internal header file for calibration segment RCU
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <stdbool.h> // for bool
#include <stdint.h>  // for uint16_t, uint32_t, uint8_t

#include "dbg_print.h"  // for DBG_LEVEL, DBG_PRINTF, DBG_PRINT, ...
#include "platform.h"   // for atomics
#include "queue.h"      // for tQueueHandle
#include "xcp.h"        // for XCP protocol definitions
#include "xcp_cfg.h"    // for XCP_PROTOCOL_LAYER_VERSION, XCP_ENABLE_...
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcptl_cfg.h"  // for XCPTL_MAX_CTO_SIZE

#ifdef XCP_ENABLE_CALSEG_LIST

#include "shm.h" // for shared memory management if enabled

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************/
/* Calibration segments                                                     */
/****************************************************************************/

#ifdef XCP_ENABLE_CALSEG_LIST

#ifndef XCP_MAX_CALSEG_COUNT
#error "Please define XCP_MAX_CALSEG_COUNT!"
#endif
#if XCP_MAX_CALSEG_COUNT < 1
#error "XCP_MAX_CALSEG_COUNT must be at least 1!"
#endif

#ifndef XCP_MAX_CALSEG_NAME
#define XCP_MAX_CALSEG_NAME 31
#endif

// Calibration segment number
// Used in A2l and XCP commands to identify a calibration segment
// Currently only 256 segments are supported
typedef uint8_t tXcpCalSegNumber;
#define XCP_UNDEFINED_CALSEG_NUM 0xFF

// Calibration segment index
// The index of the calibration segment in the calibration segment list or XCP_UNDEFINED_CALSEG
typedef uint16_t tXcpCalSegIndex;
#define XCP_UNDEFINED_CALSEG ((tXcpCalSegIndex)0xFFFF)

#define XCP_CALPAGE_ALIGNMENT 8   // Page alignment in bytes
#define XCP_CALSEG_HEADER_SIZE 64 // Must be & XCP_CALPAGE_ALIGNMENT

// Sentinel value for "null" page offsets (replaces NULL pointer)
#define XCP_CALSEG_NO_PAGE UINT32_MAX

// Calibration segment header struct
// All page references are stored as uint32_t byte offsets from c->b[0] so that
// the entire tXcpCalSeg (header + pages) is position-independent and safe to
// place in POSIX shared memory without pointer fixup across processes.
typedef struct {

    atomic_uint_least32_t ecu_page_next; // offset into c->b[]
    atomic_uint_least32_t free_page;     // offset into c->b[]
    atomic_uint_fast8_t ecu_access;      // page number for ECU access
    atomic_uint_fast8_t lock_count;      // lock count for the segment, 0 = unlocked

#if defined(XCP_ENABLE_ABS_ADDRESSING) && XCP_ADDR_EXT_ABS == 0x00
    uint8_t *default_page_ptr; // process-local ptr to caller's static data, NOT sharable, used for
#else
    uint8_t *res1; // In SHM mode, there is no pointer to the default page
#endif
    uint32_t ecu_page; // offset into c->b[], or XCP_CALSEG_NO_PAGE
    uint32_t xcp_page; // offset into c->b[], or XCP_CALSEG_NO_PAGE
    uint16_t size;
    tXcpCalSegNumber calseg_number; // segment number, XCP_UNDEFINED_CALSEG_NUM if not a MEMORY_SEGMENT
    uint8_t xcp_access;             // page number for XCP access
    bool write_pending;             // write pending because write delay
    bool free_page_hazard;          // safe free page use is not guaranteed yet, it may be in use
#ifdef XCP_ENABLE_CAL_PERSISTENCE
    uint32_t file_pos; // position of the calibration segment in the persistence file
    uint8_t mode;      // requested for freeze and preload
#else
    uint32_t res2;
    uint8_t res3;
#endif
    uint8_t app_id; // Application id for SHM_MODE
    char name[XCP_MAX_CALSEG_NAME + 1];

#ifdef OPTION_ATOMIC_EMULATION
    uint8_t res[64 - 22];
#endif

} tXcpCalSegHeader;

// Accessor helpers: resolve a page offset to a pointer within c->b[]
// Returns NULL when offset is XCP_CALSEG_NO_PAGE
#define DEFAULT_PAGE_OFFSET (0)                                       // Constant offset of the default page in the allocated memory buffer
#define XCP_PAGE_OFFSET(aligned_page_size) (aligned_page_size)        // Initial offset of the XCP working page in the allocated memory buffer
#define ECU_PAGE_OFFSET(aligned_page_size) (2 * (aligned_page_size))  // Initial of the ECU working page in the allocated memory buffer
#define FREE_PAGE_OFFSET(aligned_page_size) (3 * (aligned_page_size)) // Initial of the free swap page in the allocated memory buffer
#define CalSegDefaultPage(c) &(c)->b[DEFAULT_PAGE_OFFSET]
#define CalSegEcuPage(c) &(c)->b[(c)->h.ecu_page]
#define CalSegXcpPage(c) &(c)->b[(c)->h.xcp_page]

static_assert(sizeof(tXcpCalSegHeader) % XCP_CALPAGE_ALIGNMENT == 0, "Error: size of tXcpCalSegHeader is not a multiple of XCP_CALPAGE_ALIGNMENT");
static_assert(sizeof(tXcpCalSegHeader) % XCP_CALSEG_HEADER_SIZE == 0, "Error: size of tXcpCalSegHeader is not a multiple of XCP_CALSEG_HEADER_SIZE");

// Calibration segment
typedef struct {
    tXcpCalSegHeader h;
    // variable size data block for the pages, actual size is page_size * 3, for working page, free page and xcp page
    uint8_t b[];
} tXcpCalSeg;

// Calibration segment list
typedef struct {
    atomic_uint_least32_t offset[XCP_MAX_CALSEG_COUNT]; // calseg_offset[i] is the byte offset of calseg i from cal_mem[0], XCP_CALSEG_NO_PAGE means slot is unused
    atomic_uint_fast16_t count;                         // Number of calibration segments, max XCP_MAX_CALSEG_COUNT
    uint16_t memory_segment_count;                      // Number of memory segments used by calibration segments, max 255
    bool write_delayed;                                 // atomic calibration (begin/end user command) in progress

    // Thread-safe bump allocator pool for calibration segment memory segments
    atomic_uint_fast32_t cal_mem_used; // Bytes consumed so far, updated with CAS

    union {
        uint64_t pool_alignment;        // Force alignment of the memory pool to 8 bytes for safe atomic access
        uint8_t pool[XCP_CAL_MEM_SIZE]; // Flat memory pool, all calseg structs allocated here
    } cal_mem;

} tXcpCalSegList;

// Resolve a calseg index to a pointer within cal_mem[]
#define CalSegPtr(idx) ((const tXcpCalSeg *)(&(shared.cal_seg_list.cal_mem.pool[shared.cal_seg_list.offset[(idx)]])))
#define CalSegPtrMut(idx) ((tXcpCalSeg *)(&(shared.cal_seg_list.cal_mem.pool[shared.cal_seg_list.offset[(idx)]])))

/**************************************************************************/
// Application side
// Thread-safe
/**************************************************************************/

// Create a preloaded calibration segment, which can be initialized with data from the binary persistence file at startup
tXcpCalSegIndex XcpCreateCalSegPreloaded(const char *name, uint8_t app_id, uint16_t page_size, tXcpCalSegIndex index, tXcpCalSegNumber number, FILE *file, uint32_t file_pos);

// Create a calibration segment
tXcpCalSegIndex XcpCreateCalSeg(const char *name, const void *default_page, uint16_t page_size);

// Create a calibration value
tXcpCalSegIndex XcpCreateCalBlk(const char *name, const void *default_page, uint16_t page_size);

// Lock a calibration segment and return a pointer to the ECU page
const uint8_t *XcpLockCalSeg(tXcpCalSegIndex calseg);

// Unlock a calibration segment
// Single threaded, must be used in the thread it was created
uint8_t XcpUnlockCalSeg(tXcpCalSegIndex calseg);

/**************************************************************************/
// Server side
// Single-threaded
/**************************************************************************/

// Initialize the calibration segment list
void XcpInitCalSegList(void);
void XcpDeinitCalSegList(void);

// Get the number of calibration segments
uint16_t XcpGetCalSegCount(void);

// Number of calibration memory segments
uint8_t XcpGetMemSegCount(void);

// Find a calibration segment by name, returns XCP_UNDEFINED_CALSEG if not found
// In SHM mode, only searches within the calling process's own segments (scoped by app_id)
tXcpCalSegIndex XcpFindCalSeg(const char *name);

// Find a calibration segment by a pointer into its static default page memory, returns XCP_UNDEFINED_CALSEG if not found
tXcpCalSegIndex XcpFindCalSegByAddr(uint8_t *addr);

// Convert between segment number and segment index, returns XCP_UNDEFINED_CALSEG or XCP_UNDEFINED_CALSEG_NUM if not found
tXcpCalSegIndex XcpGetCalSegIndex(tXcpCalSegNumber segment_number);
tXcpCalSegNumber XcpGetCalSegNumber(tXcpCalSegIndex calseg);

// Get a pointer to the calibration segment struct
const tXcpCalSeg *XcpGetCalSeg(tXcpCalSegIndex calseg);

// Get the name of the calibration segment
const char *XcpGetCalSegName(tXcpCalSegIndex calseg);

// Get the size of the calibration segment
uint16_t XcpGetCalSegSize(tXcpCalSegIndex calseg);

// Get the XCP/A2L address of a calibration segment
uint32_t XcpGetCalSegBaseAddress(tXcpCalSegIndex calseg);

// Update all pending calibration changes
uint8_t XcpCalSegPublishAll(bool wait);

/**************************************************************************/
// Server side
// Single-threaded XCP commands
/**************************************************************************/

// XCP read/write
uint8_t XcpCalSegWriteMemory(uint32_t dst, uint16_t size, const uint8_t *src);
uint8_t XcpCalSegReadMemory(uint32_t src, uint16_t size, uint8_t *dst);

// XCP atomic write
void XcpCalSegBeginAtomicTransaction(void);
uint8_t XcpCalSegEndAtomicTransaction(void);

// XCP protocol commands for calibration segment management
uint8_t XcpCalSegSetCalPage(tXcpCalSegNumber segment_number, uint8_t page, uint8_t mode);
uint8_t XcpCalSegGetCalPage(tXcpCalSegNumber segment_number, uint8_t mode);
#ifdef XCP_ENABLE_COPY_CAL_PAGE
uint8_t XcpCalSegCopyCalPage(tXcpCalSegNumber src_seg_num, uint8_t src_page, tXcpCalSegNumber dst_seg_num, uint8_t dst_page);
#endif
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE
uint8_t XcpGetCalSegMode(tXcpCalSegNumber segment_number);
uint8_t XcpSetCalSegMode(tXcpCalSegNumber segment_number, uint8_t mode);
uint8_t XcpFreezeSelectedCalSegs(bool all);
#endif
uint8_t XcpGetSegInfo(tXcpCalSegNumber segment_number, uint8_t mode, uint8_t seg_info, uint8_t map_index);
uint8_t XcpGetSegPageInfo(tXcpCalSegNumber segment_number, uint8_t page);

#endif // XCP_ENABLE_CALSEG_LIST

#ifdef __cplusplus
} // extern "C"
#endif

#endif // XCP_ENABLE_CALSEG_LIST
