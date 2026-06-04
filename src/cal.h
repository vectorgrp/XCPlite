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

#include "assert.h"     // for static_assert
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
// Currently only 256 segments are supported (XCP/A2L limitation), from max 64K calibration blocks (tCalSegIndex)
typedef uint8_t tXcpCalSegNumber;
#define XCP_UNDEFINED_CALSEG_NUM 0xFF

// Calibration segment index
// The index of the calibration segment in the calibration segment list or XCP_UNDEFINED_CALSEG
typedef uint16_t tXcpCalSegIndex;
#define XCP_UNDEFINED_CALSEG ((tXcpCalSegIndex)0xFFFF)

// Sentinel value for "null" page offsets
#define XCP_CALSEG_NO_PAGE UINT32_MAX

// Calibration segment header struct, 64 byte length, aligned to 8 bytes for atomic access
// All page references are stored as uint32_t byte offsets from c->b[0] so that
// the entire tXcpCalSeg (header + pages) is position-independent and safe to
// place in POSIX shared memory without pointer fixup across processes.
#define XCP_CALPAGE_ALIGNMENT 8   // Page alignment in bytes
#define XCP_CALSEG_HEADER_SIZE 64 // Must be & XCP_CALPAGE_ALIGNMENT
#if defined(PLATFORM_32BIT)
#define XCP_CALSEG_HEADER_PAD (XCP_MAX_CALSEG_NAME + 12)
#else
#define XCP_CALSEG_HEADER_PAD (XCP_MAX_CALSEG_NAME + 8)
#endif

typedef struct {
#if defined(XCP_ENABLE_ABS_ADDRESSING) && XCP_ADDR_EXT_ABS == 0x00
    uint8_t *default_page_ptr; // Pointer to static lifetime default page
#else
    uint8_t *res1; // Default, there is no pointer to the default page
#endif
    // 8
    atomic_uint_least32_t ecu_page_next; // offset into c->b[]
    atomic_uint_least32_t free_page;     // offset into c->b[]
    uint32_t ecu_page;                   // offset into c->b[], or XCP_CALSEG_NO_PAGE
    uint32_t xcp_page;                   // offset into c->b[], or XCP_CALSEG_NO_PAGE
#ifdef XCP_ENABLE_CAL_PERSISTENCE
    uint32_t file_pos; // position of the calibration segment in the persistence file
#else
    uint32_t res2;
#endif
    // 28
    uint16_t size;
    // 30
    atomic_uint_least8_t ecu_access; // page number for ECU access
    atomic_uint_least8_t lock_count; // lock count for the segment, 0 = unlocked
    uint8_t xcp_access;             // page number for XCP access
    bool write_pending;             // write pending because write delay
    bool free_page_hazard;          // safe free page use is not guaranteed yet, it may be in use
    tXcpCalSegNumber calseg_number; // segment number, XCP_UNDEFINED_CALSEG_NUM if a calibration block, but not a MEMORY_SEGMENT
    uint8_t app_id;                 // Application id for SHM_MODE
#ifdef XCP_ENABLE_CAL_PERSISTENCE
    uint8_t mode; // for freeze and preload flags
#else
    uint8_t res3;
#endif
    // 38
    char name[XCP_MAX_CALSEG_NAME + 1];
    uint8_t res[XCP_CALSEG_HEADER_PAD - XCP_MAX_CALSEG_NAME];
} tXcpCalSegHeader;

static_assert(sizeof(bool) == 1, "Error: bool is not 1 byte");
static_assert(sizeof(atomic_uint_least32_t) == 4, "Error: atomic_uint_least32_t is not 4 bytes");
static_assert(sizeof(atomic_uint_least8_t) == 1, "Error: atomic_uint_least8_t is not 1 byte");
static_assert(XCP_CALSEG_HEADER_SIZE % XCP_CALPAGE_ALIGNMENT == 0, "Error: XCP_CALSEG_HEADER_SIZE is not a multiple of XCP_CALPAGE_ALIGNMENT");
static_assert(sizeof(tXcpCalSegHeader) == XCP_CALSEG_HEADER_SIZE, "Error: size of tXcpCalSegHeader is not equal to XCP_CALSEG_HEADER_SIZE");

// Accessor helpers: resolve a page offset to a pointer within c->b[]
// Returns NULL when offset is XCP_CALSEG_NO_PAGE
#if defined(XCP_ENABLE_ABS_ADDRESSING) && XCP_ADDR_EXT_ABS == 0x00

// In absolute addressing mode
// Save the memory for the default page copy in absolute addressing mode, default page has static lifetime
#define CALSEG_PAGE_COUNT 3                                           // default page, ECU page and XCP page
#define XCP_PAGE_OFFSET(aligned_page_size) (0)        // Initial offset of the XCP working page in the allocated memory buffer
#define ECU_PAGE_OFFSET(aligned_page_size) (aligned_page_size)  // Initial of the ECU working page in the allocated memory buffer
#define FREE_PAGE_OFFSET(aligned_page_size) (2 * (aligned_page_size)) // Initial of the free swap page in the allocated memory buffer
#define CalSegDefaultPage(c) (const uint8_t *)(c)->h.default_page_ptr
#define CalSegEcuPage(c) &(c)->b[(c)->h.ecu_page]
#define CalSegXcpPage(c) &(c)->b[(c)->h.xcp_page]

#else

// In segment relative addressing mode
// Maintain a copy of the default page, mandatory for SHM mode, using shared memory
#define CALSEG_PAGE_COUNT 4                                           // default page, ECU page, XCP page and free swap page
#define DEFAULT_PAGE_OFFSET (0)                                       // Constant offset of the default page in the allocated memory buffer
#define XCP_PAGE_OFFSET(aligned_page_size) (aligned_page_size)        // Initial offset of the XCP working page in the allocated memory buffer
#define ECU_PAGE_OFFSET(aligned_page_size) (2 * (aligned_page_size))  // Initial of the ECU working page in the allocated memory buffer
#define FREE_PAGE_OFFSET(aligned_page_size) (3 * (aligned_page_size)) // Initial of the free swap page in the allocated memory buffer
#define CalSegDefaultPage(c) &(c)->b[DEFAULT_PAGE_OFFSET]
#define CalSegEcuPage(c) &(c)->b[(c)->h.ecu_page]
#define CalSegXcpPage(c) &(c)->b[(c)->h.xcp_page]

#endif

// Calibration segment
typedef struct {
    tXcpCalSegHeader h; // 64 byte header, must be first for CalSegPtr() to work
    // variable size data block for the pages, actual size is page_size * CALSEG_PAGE_COUNT, for [default_page], working page, free page and xcp page
    uint8_t b[];
} tXcpCalSeg;

// Calibration segment list
typedef struct {
    atomic_uint_least32_t offset[XCP_MAX_CALSEG_COUNT]; // offset[i] is the byte offset of calseg i from cal_mem[0], XCP_CALSEG_NO_PAGE means slot is unused
    atomic_uint_least16_t count;                         // Number of calibration segments, max XCP_MAX_CALSEG_COUNT
    uint16_t memory_segment_count;                      // Number of memory segments used by calibration segments, max 255
    bool write_delayed;                                 // atomic calibration (begin/end user command) in progress

    // Thread-safe bump allocator pool for calibration segment memory segments
    atomic_uint_least32_t cal_mem_used; // Bytes consumed so far, updated with CAS

    // Calibration segment/block memory pool
    union {
        uint64_t pool_alignment;        // Force alignment of the memory pool to 8 bytes for safe atomic access
        uint8_t pool[XCP_CAL_MEM_SIZE]; // Flat memory pool, all tXcpCalSeg structs allocated here
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

// Update the EKP segment with the current EPK value
#ifdef XCP_ENABLE_EPK_CALSEG
void XcpCalUpdateEpkSeg(const char *epk);
#endif

/**************************************************************************/
// Server side
// Single-threaded
/**************************************************************************/

#ifndef __XCPLIB_H__

#define XCP_CALSEG_TYPE_SEGMENT 0x8001
#define XCP_CALSEG_TYPE_BLOCK 0x8002

// Calibration segment or block descriptor used by CalSegCreate() and CalBlkCreate() for section-based pre-registration
typedef struct {
    const char *name;
    const void *addr;
    tXcpCalSegIndex *indexp;
    uint16_t size;
    uint16_t type;
#ifdef PLATFORM_32BIT
    uint8_t res[16];
#endif
} tXcpCalDescriptor;

static_assert(sizeof(tXcpCalDescriptor) == 32, "sizeof(XcpCalDescriptor) must be 32");

// Platform section attribute for tXcpCalDescriptor static variables created by CalSegCreate() and CalBlkCreate().
// Placing all descriptors in a named ELF/Mach-O section lets XcpInit() iterate them and
// pre-register every calibration segment or block before the first use, without requiring the call site of the creation to execute first.
#if defined(__ELF__)
#define XCP_CAL_SECTION_ATTR __attribute__((section("xcp_cals"), used))
#elif defined(__APPLE__)
#define XCP_CAL_SECTION_ATTR __attribute__((section("__DATA,xcp_cals"), used))
#else
#define XCP_CAL_SECTION_ATTR /* section-based registration not supported on this platform */
#endif

#endif // __XCPLIB_H__

// Initialize the calibration segment list
void XcpInitCalSegList(void);
void XcpDeinitCalSegList(void);

// Register all calibration segments from the xcp_cals section, returns the number of registered segments
uint16_t XcpRegisterSectionCalSegs(void);

// Get the number of calibration segments
uint16_t XcpGetCalSegCount(void);

// Number of calibration memory segments
uint8_t XcpGetMemSegCount(void);

// Find a calibration segment by name, returns XCP_UNDEFINED_CALSEG if not found
// In SHM mode, only searches within the calling process's own segments (scoped by app_id)
tXcpCalSegIndex XcpFindCalSeg(const char *name);

// Find a calibration segment by a pointer into its static default page memory, returns XCP_UNDEFINED_CALSEG if not found
#if defined(XCP_ENABLE_ABS_ADDRESSING) && XCP_ADDR_EXT_ABS == 0x00
tXcpCalSegIndex XcpFindCalSegByAddr(uint8_t *addr);
#endif

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
