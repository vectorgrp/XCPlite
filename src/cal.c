/*****************************************************************************
| File:
|   cal.c
|
| Description:
|   XCPlite calibration segment RCU
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
|***************************************************************************/

#include "xcp_cfg.h"    // XCP protocol layer configuration parameters (XCP_xxx)
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcptl_cfg.h"  // XCP transport layer configuration parameters (XCPTL_xxx)

#include "cal.h"

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIx32, PRIu64
#include <stdarg.h>   // for va_list, va_start, va_arg, va_end
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uint8_t, uint16_t, ...
#include <stdlib.h>   // for size_t, NULL, abort
#include <string.h>   // for memcpy, memset

#ifdef __APPLE__
#include <mach-o/getsect.h> // for getsectiondata(), used by XcpRegisterSectionEvents()
#include <mach-o/ldsyms.h>  // for _mh_execute_header
#endif

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#ifdef OPTION_ENABLE_PERSISTENCE
#include "persistence.h" // for XcpBinFreezeCalSeg
#endif
#include "platform.h" // for atomics OS abstraction
#include "xcp.h"      // XCP protocol definitions
#include "xcplite.h"  // XCP protocol layer interface functions

/****************************************************************************/
// Metrics

#ifdef TEST_ENABLE_CAL_METRICS
uint32_t gXcpWritePendingCount = 0;
uint32_t gXcpCalSegPublishAllCount = 0;
#endif

/**************************************************************************/
// State

#ifdef OPTION_SHM_MODE // extern declarations for gXcpData

extern tXcpData *gXcpData;                   // Shared state in SHM mode
#define shared (*(const tXcpData *)gXcpData) // Shortcut for read only access to the XCP singleton data
#define shared_mut (*gXcpData)               // Shortcut for mutable access to the XCP singleton data
#define shared_mut_safe (*gXcpData)          // Shortcut for mutable access to the XCP singleton data

#else

extern tXcpData gXcpData;
#define shared (*(const tXcpData *)&gXcpData) // Shortcut for read only access to the XCP singleton data
#define shared_mut gXcpData                   // Shortcut for mutable access to the XCP singleton data
#define shared_mut_safe gXcpData              // Shortcut for mutable access to the XCP singleton data

#endif

// Local state, not shared between processes in SHM mode
// The only local state used is the mutex to protect the calibration segment list and the MTA pointer in XcpGetSegInfo
// @@@@ TODO: Move the XCP protocol specific parts back to xcplite.c
extern tXcpLocalData gXcpLocalData;
// #define local (*(const tXcpLocalData *)&gXcpLocalData) // Read-only access to process-local state
#define local_mut gXcpLocalData // Mutable access to process-local state

// @@@@ TODO: Deactivate mode in C API
// Calibration instrumentation can currently not be deactivate at runtime, it has to be done at compile time
// An attempt to create a calibration segment or block with XCP not initialized (XcpInit not called) will fail and assert

// Global state checks
#ifdef OPTION_SHM_MODE // different isActivated check
#define isActivated() (gXcpData != NULL && 0 != (gXcpData->session_status & SS_ACTIVATED))
#else
#define isActivated() (0 != (gXcpData.session_status & SS_ACTIVATED))
#endif

/**************************************************************************/
// Command response buffer access shortcuts

// Command response buffer access shortcuts
#define CRM_LEN shared_mut.crm_len
#define CRM shared_mut.crm
#define CRM_BYTE(x) (shared_mut.crm.b[x])
#define CRM_WORD(x) (shared_mut.crm.w[x])
#define CRM_DWORD(x) (shared_mut.crm.dw[x])

/**************************************************************************/
/* Calibration segments                                                   */
/**************************************************************************/

#ifdef XCP_ENABLE_CALSEG_LIST

/**************************************************************************/
// Forward declarations

static bool XcpInitCalSeg_(tXcpCalSeg *calseg, const char *name, const void *default_page, FILE *default_page_file, uint16_t page_size, bool memory_segment);
static tXcpCalSegIndex XcpCreateCalSeg_(const char *name, bool lookup, const void *default_page, FILE *default_page_file, uint16_t page_size, bool memory_segment);

// RCU primitives, two implementations selected with OPTION_CAL_RCU_REFCOUNT, see below
static void CalSegRcuReset(tXcpCalSeg *c);
static void CalSegRcuInit(tXcpCalSeg *c, uint32_t aligned_page_size);
static const uint8_t *CalSegRcuLock(tXcpCalSeg *c);
#ifdef OPTION_CAL_RCU_REFCOUNT
static void CalSegRcuUnlock(tXcpCalSeg *c, const uint8_t *page);
#else
static void CalSegRcuUnlock(tXcpCalSeg *c);
#endif
static bool CalSegRcuTryPublish(tXcpCalSeg *c);
#if !defined(NDEBUG) && !defined(OPTION_SHM_MODE)
#define XCP_CALSEG_CHECK_UNLOCKED // Contract check in XcpDeinitCalSegList, not in SHM mode, other processes may hold locks
static bool CalSegRcuIsUnlocked(const tXcpCalSeg *c);
#endif

// Thread-safe bump allocator for calibration segment memory
// Memory is only freed as a whole when the calibration segment list is destroyed
static void *XcpCalMemAlloc_(size_t size) {
    DBG_PRINTF6("Allocating %zu bytes from calibration memory pool\n", size);
    assert(size > 0);
    assert((size % XCP_CALPAGE_ALIGNMENT) == 0);
    assert(size <= (size_t)XCP_CAL_MEM_SIZE);
    assert((uintptr_t)shared.cal_seg_list.cal_mem.pool % XCP_CALPAGE_ALIGNMENT == 0);
    uint_least32_t old_used, new_used;
    do {
        old_used = atomic_load_explicit(&shared.cal_seg_list.cal_mem_used, memory_order_relaxed);
        new_used = old_used + (uint_least32_t)size;
        if (new_used > XCP_CAL_MEM_SIZE) {
            DBG_PRINT_ERROR("XCP calibration memory pool exhausted\n");
            return NULL;
        }
    } while (!atomic_compare_exchange_weak_explicit(&shared_mut_safe.cal_seg_list.cal_mem_used, &old_used, new_used, memory_order_relaxed, memory_order_relaxed));
    return &shared_mut_safe.cal_seg_list.cal_mem.pool[old_used];
}

// Pre-register all tXcpCalSegDescriptor variables placed in the xcp_cals section
// Must be called after SS_ACTIVATED is set
// If a persistence file was loaded before this call, segments are matched by name and keep their saved id
#ifdef OPTION_SECTION_REGISTRATION
uint16_t XcpRegisterSectionCalSegs(void) {

    uint16_t count = 0;

#if defined(__ELF__)
    // Declared weak: if no object file contributes to the xcp_cals section the symbols
    // resolve to NULL rather than causing an undefined-reference linker error.
    extern const tXcpCalSegDescriptor __start_xcp_cals[] __attribute__((weak));
    extern const tXcpCalSegDescriptor __stop_xcp_cals[] __attribute__((weak));
    const tXcpCalSegDescriptor *begin = __start_xcp_cals;
    const tXcpCalSegDescriptor *end = __stop_xcp_cals;
    if (begin != NULL && end != NULL && begin < end) {
        for (const tXcpCalSegDescriptor *e = begin; e < end; e++) {
            DBG_PRINTF6("Found calibration segment descriptor in section: name=%s, addr=%p, size=%u, type=%x, indexp=%p\n", e->name, e->addr, e->size, e->type, e->indexp);
            tXcpCalSegIndex index = XcpFindCalSeg(e->name);
            if (index == XCP_UNDEFINED_CALSEG) {
                assert(e->type == XCP_CALSEG_TYPE_SEGMENT || e->type == XCP_CALSEG_TYPE_BLOCK);
                index = XcpCreateCalSeg_(e->name, false, e->addr, NULL, e->size, e->type == XCP_CALSEG_TYPE_SEGMENT);
                assert(index != XCP_UNDEFINED_CALSEG);
                count++;
            }
#if defined(XCP_ENABLE_TEST_CHECKS) && !defined(NDEBUG)
            else {
                const tXcpCalSeg *c = CalSegPtr(index);
#ifdef XCP_ENABLE_CAL_PERSISTENCE
                assert((c->h.mode & PAG_PROPERTY_PRELOAD) != 0 || strcmp(c->h.name, XCP_EPK_CALSEG_NAME) == 0); // Unexpected duplicate
#else
                assert(strcmp(c->h.name, XCP_EPK_CALSEG_NAME) == 0); // Unexpected duplicate
#endif
            }
#endif
            *(e->indexp) = index; // initialize the segment index pointer
        }
    } else {
        DBG_PRINT3("(ELF): No xcp_cals section found for pre-registration\n");
    }
#elif defined(__APPLE__)
    unsigned long sz = 0;
    const tXcpCalSegDescriptor *begin = (tXcpCalSegDescriptor *)getsectiondata(&_mh_execute_header, "__DATA", "xcp_cals", &sz);
    if (begin != NULL) {
        const tXcpCalSegDescriptor *end = begin + (sz / sizeof(tXcpCalSegDescriptor));
        for (const tXcpCalSegDescriptor *e = begin; e < end; e++) {
            DBG_PRINTF6("Found calibration segment descriptor in section: name=%s, addr=%p, size=%u, type=%x, indexp=%p\n", e->name, e->addr, e->size, e->type, (void *)e->indexp);
            tXcpCalSegIndex index = XcpFindCalSeg(e->name);
            if (index == XCP_UNDEFINED_CALSEG) {
                assert(e->type == XCP_CALSEG_TYPE_SEGMENT || e->type == XCP_CALSEG_TYPE_BLOCK);
                index = XcpCreateCalSeg_(e->name, false, e->addr, NULL, e->size, e->type == XCP_CALSEG_TYPE_SEGMENT);
                assert(index != XCP_UNDEFINED_CALSEG);
                count++;
            }
#if defined(XCP_ENABLE_TEST_CHECKS) && !defined(NDEBUG)
            else {
                const tXcpCalSeg *c = CalSegPtr(index);
#ifdef XCP_ENABLE_CAL_PERSISTENCE
                assert((c->h.mode & PAG_PROPERTY_PRELOAD) != 0 || strcmp(c->h.name, XCP_EPK_CALSEG_NAME) == 0); // Unexpected duplicate
#else
                assert(strcmp(c->h.name, XCP_EPK_CALSEG_NAME) == 0); // Unexpected duplicate
#endif
            }
#endif
            *(e->indexp) = index;
        }
    } else {
        DBG_PRINT3("(MacOS): No xcp_cals section found for pre-registration\n");
    }
#else
#ifndef _WIN
#error "Unsupported platform for section calibration segment registration"
#endif
#endif

    if (count > 0)
        DBG_PRINTF3(ANSI_COLOR_GREEN "Preregistered %u new calibration segments or blocks from descriptor section\n" ANSI_COLOR_RESET, count);
#ifndef _WIN
    else
        DBG_PRINT3("No new calibration segment descriptors found in section xcp_cals\n");
#endif
    return count;
}
#endif

/**************************************************************************/

// Initialize the calibration segment list
void XcpInitCalSegList(void) {
    atomic_store_explicit(&shared_mut_safe.cal_seg_list.count, 0, memory_order_relaxed);
    atomic_store_explicit(&shared_mut_safe.cal_seg_list.cal_mem_used, 0, memory_order_relaxed);
    shared_mut.cal_seg_list.memory_segment_count = 0;
    shared_mut.cal_seg_list.write_delayed = false;
    mutexInit(&local_mut.cal_seg_list_mutex, false, 0); // Non-recursive mutex, no spin count

    DBG_PRINTF6("Calibration segment list initialized, sizeof(tXcpCalSegHeader) = %zu, sizeof(tXcpCalSegList) = %zu\n", sizeof(tXcpCalSegHeader), sizeof(tXcpCalSegList));
}

// Free the calibration segment list
void XcpDeinitCalSegList(void) {

#ifdef OPTION_SHM_MODE // not implemented clear pending state
    // @@@@ TODO: Deinit calibration segment list in SHM mode, clear pending states
#endif

#ifdef XCP_CALSEG_CHECK_UNLOCKED
    // User contract (see docs/CAL_RCU.md): no thread may be inside a calibration segment lock when XCP is deinitialized
    // A lock which is still held at this point would be leaked, the check is compiled out in release builds
    // Not checked in SHM mode, the lock count is shared with other processes, which may legitimately hold locks
    uint16_t n = (uint16_t)atomic_load_explicit(&shared.cal_seg_list.count, memory_order_acquire);
    for (uint16_t i = 0; i < n; i++) {
        const tXcpCalSeg *c = CalSegPtr(i);
        if (!CalSegRcuIsUnlocked(c)) {
            DBG_PRINTF_ERROR("XcpDeinitCalSegList: calseg %s is still locked\n", c->h.name);
            assert(0);
        }
    }
#endif

    // Just destroy the local mutex
    mutexDestroy(&local_mut.cal_seg_list_mutex);
}

// Get the number of calibration segments
uint16_t XcpGetCalSegCount(void) {
    if (!isActivated()) {
        assert(0);
        return 0;
    }
    return (uint16_t)atomic_load_explicit(&shared.cal_seg_list.count, memory_order_acquire);
}

// Get the number of memory segments
uint8_t XcpGetMemSegCount(void) {
    if (!isActivated()) {
        assert(0);
        return XCP_UNDEFINED_CALSEG_NUM;
    }
    return (uint8_t)(shared.cal_seg_list.memory_segment_count & 0xFF);
}

// Get a pointer to the calibration segment struct of calseg index
const tXcpCalSeg *XcpGetCalSeg(tXcpCalSegIndex calseg_index) {
    if (calseg_index >= XcpGetCalSegCount()) {
        DBG_PRINTF5("Invalid calibration segment index: %u\n", calseg_index);
        assert(0);
        return NULL;
    }
    return CalSegPtr(calseg_index);
}

// Get the index of a calibration segment by name
// Lock-free, thread-safe
tXcpCalSegIndex XcpFindCalSeg(const char *name) {

    uint16_t n = XcpGetCalSegCount();

    // Iterate cal_seg_list cal_seg_list
    for (tXcpCalSegIndex i = 0; i < n; i++) {
        const tXcpCalSeg *calseg = CalSegPtr(i);
        assert(calseg != NULL);
#ifdef OPTION_SHM_MODE // find calibration segments only among entries owned by this application process
// In SHM mode, match only entries owned by this application process — different apps may use the same name
#ifdef XCP_ENABLE_EPK_CALSEG
        // Not for index 0, which is reserved for the EPK segment
        if (i > 0 && calseg->h.app_id != XcpShmGetAppId())
            continue; // Skip entries owned by other processes
#else
        if (calseg->h.app_id != XcpShmGetAppId())
            continue; // Skip entries owned by other processes
#endif
        if (strcmp(calseg->h.name, name) == 0)
            return i;
#else
        if (strcmp(calseg->h.name, name) == 0)
            return i;
#endif
    }
    return XCP_UNDEFINED_CALSEG; // Not found
}

// Get the index of a calibration segment by address (inside of the default page)
// Lock-free, thread-safe
#if defined(XCP_ENABLE_ABS_ADDRESSING) && XCP_ADDR_EXT_ABS == 0x00
tXcpCalSegIndex XcpFindCalSegByAddr(uint8_t *addr) {
    // Iterate cal_seg_list cal_seg_list
    uint16_t n = XcpGetCalSegCount();
    for (tXcpCalSegIndex i = 0; i < n; i++) {
        const tXcpCalSeg *calseg = CalSegPtr(i);
        assert(calseg != NULL);
        if (addr >= calseg->h.default_page_ptr && addr < calseg->h.default_page_ptr + calseg->h.size) {
            return i;
        }
    }
    return XCP_UNDEFINED_CALSEG; // Not found
}
#endif

// Get the calibration segment index by memory segment number, returns XCP_UNDEFINED_CALSEG if not found
// Not all calibrations segments can be controlled by XCP
// XCP uses a uin8_t number to identify memory segments (tXcpCalSegNumber), while the calibration segment index (tXcpCalSegIndex) is uint16_t
// Lock-free, thread-safe
tXcpCalSegIndex XcpGetCalSegIndex(tXcpCalSegNumber segment_number) {
    // Iterate cal_seg_list cal_seg_list
    uint16_t n = XcpGetCalSegCount();
    for (uint16_t i = 0; i < n; i++) {
        const tXcpCalSeg *calseg = CalSegPtr(i);
        assert(calseg != NULL);
        if (calseg->h.calseg_number == segment_number) {
            return i;
        }
    }
    return XCP_UNDEFINED_CALSEG; // Not found
}

// Get the memory segment number of a calibration segment, returns 0xFF if not found
// Lock-free, thread-safe
tXcpCalSegNumber XcpGetCalSegNumber(tXcpCalSegIndex calseg_index) {
    if (calseg_index >= XcpGetCalSegCount())
        return XCP_UNDEFINED_CALSEG_NUM;
    return CalSegPtr(calseg_index)->h.calseg_number;
}

// Get the name of the calibration segment
const char *XcpGetCalSegName(tXcpCalSegIndex calseg_index) {
    if (calseg_index >= XcpGetCalSegCount()) {
        assert(0);
        return NULL;
    }
    return CalSegPtr(calseg_index)->h.name;
}

// Get the size of a calibration segment
uint16_t XcpGetCalSegSize(tXcpCalSegIndex calseg_index) {
    if (calseg_index >= XcpGetCalSegCount()) {
        assert(0);
        return 0;
    }
    return CalSegPtr(calseg_index)->h.size;
}

// Get the XCP/A2L address of a calibration segment
uint32_t XcpGetCalSegBaseAddress(tXcpCalSegIndex calseg_index) {
    if (calseg_index >= XcpGetCalSegCount()) {
        assert(0);
        return 0;
    }
#if XCP_ADDR_EXT_SEG == 0x00
    // Memory segments are addressed in relative mode
    return XcpAddrEncodeSegIndex(calseg_index, 0);
#else
#if defined(XCP_ENABLE_ABS_ADDRESSING) && XCP_ADDR_EXT_ABS != 0x00
#error "XCP_ADDR_EXT_ABS must be 0x00"
#endif
    return XcpAddrEncodeAbs(CalSegPtr(calseg_index)->h.default_page_ptr);
#endif
}

// Create a preloaded calibration segment, which is initialized with data from the binary persistence file at startup
// Return the default page to the caller for initialization with the preloaded data, or NULL on error (e.g. wrong index, wrong memory segment number, out of memory, etc.)
tXcpCalSegIndex XcpCreateCalSegPreloaded(const char *name, uint8_t app_id, uint16_t page_size, tXcpCalSegIndex index, tXcpCalSegNumber number, FILE *file, uint32_t file_pos) {

    // Create a calibration segment with given name, index and number without initial value to be loaded from file
    tXcpCalSegIndex seg_index = XcpCreateCalSeg_(name, false /* lookup */, NULL, file, page_size, number != XCP_UNDEFINED_CALSEG_NUM);
    if (seg_index != index) {
        assert(0);
        return XCP_UNDEFINED_CALSEG;
    }
    tXcpCalSeg *seg = CalSegPtrMut(seg_index);
    if (seg->h.calseg_number != number) {
        assert(0);
        return XCP_UNDEFINED_CALSEG;
    }

#ifdef XCP_ENABLE_CAL_PERSISTENCE
    // Mark the segment as preloaded
    seg->h.mode = PAG_PROPERTY_PRELOAD;
    seg->h.file_pos = file_pos; // Save the position of the segment page data in the file
    seg->h.app_id = app_id;     // Save the application ID
#else
    (void)app_id;   // Unused parameter
    (void)file_pos; // Unused parameter
#endif

    return seg_index;
}

// Create a calibration segment if not already exists
// Thread safe
// Returns the handle or XCP_UNDEFINED_CALSEG when out of memory
// Calibration segments have 2 pages and can be controlled via XCP through their memory segment number (XcpGetCalSegNumber)
// The number of memory segments is limited to 255
tXcpCalSegIndex XcpCreateCalSeg(const char *name, const void *default_page, uint16_t page_size) {
    // @@@@ TODO: Create a way to let the user call functions CreateCalSeg/Lock/Unlock without initializing the calibration segment list
    // tXcpCalSegIndex could be pointer size or introduce a simple array of default page pointers ??
    if (!isActivated()) {
        return XCP_UNDEFINED_CALSEG;
    }
    return XcpCreateCalSeg_(name, true /* lookup */, default_page, NULL, page_size, true);
}

// Create a calibration block if not already exists
// Thread safe
// Returns the handle or XCP_UNDEFINED_CALSEG when out of memory
// Calibration blocks don't have a memory segment and the related XCP features
tXcpCalSegIndex XcpCreateCalBlk(const char *name, const void *default_page, uint16_t page_size) {
    if (!isActivated()) {
        return XCP_UNDEFINED_CALSEG;
    }
    return XcpCreateCalSeg_(name, true /* lookup */, default_page, NULL, page_size, false);
}

// Helper for XcpCreateCalSeg_ to register a calibration segment in the calibration segment list
// Note: This is one of the compromises we make for this simple RCU: Uses a mutex for thread-safe creation
// Returns the list index
static tXcpCalSegIndex XcpRegisterCalSeg_(tXcpCalSeg *c) {

    // Mutex to make the operation list[count++]=offset atomic
    mutexLock(&local_mut.cal_seg_list_mutex);

    // Get the index for a new segment
    uint16_t calseg_index = XcpGetCalSegCount();

    // Check if out of list space
    if (calseg_index >= XCP_MAX_CALSEG_COUNT - 1) {
        mutexUnlock(&local_mut.cal_seg_list_mutex);
        DBG_PRINT_ERROR("too many calibration segments, increase XCP_MAX_CALSEG_COUNT\n");
        return XCP_UNDEFINED_CALSEG;
    }

    // Store the new segments memory pool offset in the list
    shared_mut_safe.cal_seg_list.offset[calseg_index] = (uint32_t)((uint8_t *)c - shared_mut_safe.cal_seg_list.cal_mem.pool);

    // Publish the new entry
    // Release store ensures all preceding writes (name, size, etc.) are visible to any thread that iterates on the calseg list
    atomic_store_explicit(&shared_mut_safe.cal_seg_list.count, calseg_index + 1, memory_order_release);
    mutexUnlock(&local_mut.cal_seg_list_mutex);
    return (tXcpCalSegIndex)calseg_index;
}

// Helper for XcpCreateCalSeg, XcpCreateCalBlk and XcpCreateCalSegPreloaded to create a calibration block with given page count (0 for blk, 2 for seg)
// A segment with this name may already exist, when preloaded - then it is reinitialized
// Lookup for existence can be skipped if lookup is false, which is the case for preloaded segments, because they have a predefined index and are loaded in order
// If default_page is NULL, it is a preloaded segment, the caller will initialize the default page
static tXcpCalSegIndex XcpCreateCalSeg_(const char *name, bool lookup, const void *default_page, FILE *default_page_file, uint16_t page_size, bool memory_segment) {

    DBG_PRINTF6("XcpCreateCalSeg_ name='%s', lookup=%d, page_size=%u, memory_segment=%d\n", name, lookup, page_size, memory_segment);

    tXcpCalSeg *calseg = NULL;
    tXcpCalSegIndex calseg_index = XCP_UNDEFINED_CALSEG;

    // If lookup is enabled, check for existence (in the calibration segment list for the current application)
    if (lookup) {
        // Check if the segment does not already exist, only if not create a new one
        // @@@@ TODO: Optimize XcpFindCalSeg, because this is the once execution check
        calseg_index = XcpFindCalSeg(name);
        assert(calseg_index == XCP_UNDEFINED_CALSEG || calseg_index < XcpGetCalSegCount());
    }

    // Create, if not exists
    if (calseg_index == XCP_UNDEFINED_CALSEG) {

        // Align page size to XCP_CALPAGE_ALIGNMENT bytes for better performance
        uint16_t aligned_page_size = (page_size + XCP_CALPAGE_ALIGNMENT - 1) & ~(XCP_CALPAGE_ALIGNMENT - 1);

        // Allocate memory for the new segment from the embedded pool using the thread-safe bump allocator
        // Header + DEFAULT page + ECU page + XCP page + RCU swap page
        calseg = (tXcpCalSeg *)XcpCalMemAlloc_(sizeof(tXcpCalSegHeader) + CALSEG_PAGE_COUNT * (size_t)aligned_page_size);
        DBG_PRINTF3("Create CalSeg '%s' size=%u, type=%s\n", name, page_size, memory_segment ? "seg" : "blk");
        if (!XcpInitCalSeg_(calseg, name, default_page, default_page_file, page_size, memory_segment)) {
            return XCP_UNDEFINED_CALSEG;
        }
        if ((calseg_index = XcpRegisterCalSeg_(calseg)) == XCP_UNDEFINED_CALSEG) {
            return XCP_UNDEFINED_CALSEG;
        }

    } else {
        // Segment already exists
        calseg = CalSegPtrMut(calseg_index);
        DBG_PRINTF6("Calibration segment '%s' already exists\n", calseg->h.name);
    }

    // Must have the correct size
    if (page_size != calseg->h.size) {
        DBG_PRINTF_ERROR("Calibration segment '%s' already exists with wrong size %u, expected %u\n", calseg->h.name, calseg->h.size, page_size);
        return XCP_UNDEFINED_CALSEG;
    }

    // Special case are preloaded segments, which are loaded from the binary calibration segment image file on startup, and have the PAG_PROPERTY_PRELOAD bit set
#ifdef XCP_ENABLE_CAL_PERSISTENCE
    // Check if this is a preloaded segment
    // Preloaded segments have an initialized default page with data from the binary calibration segment image file on startup
    // The PAG_PROPERTY_PRELOAD bit is set to indicate this
    if ((calseg->h.mode & PAG_PROPERTY_PRELOAD) != 0) {

        assert(default_page_file == NULL); // For preloaded segments, the default page is initialized
        assert(strcmp(calseg->h.name, name) == 0);
        assert(page_size == calseg->h.size);

        // Nothing to do
        // Complete the initialization of the preloaded segment
        DBG_PRINTF5("CalSeg '%s' finalized from preloaded, index=%u, size=%u\n", calseg->h.name, calseg_index, calseg->h.size);
        if (default_page != NULL && memcmp(CalSegDefaultPage(calseg), default_page, page_size) != 0) {
            DBG_PRINTF3(ANSI_COLOR_YELLOW "Persisted (different) default page for '%s' loaded from binary persistence file!\n" ANSI_COLOR_RESET, calseg->h.name);
        }

        calseg->h.mode = 0; // Clear PAG_PROPERTY_PRELOAD, reset mode flags (freeze not enabled, set by XCP command SET_SEGMENT_MODE)
        return calseg_index;
    }
#endif //   XCP_ENABLE_CAL_PERSISTENCE

#if defined(XCP_ENABLE_TEST_CHECKS) && defined(OPTION_ENABLE_DBG_PRINTS)
    // For an already existing segment, the default page should be the same as the existing one, because we assume it has static lifetime
    if (default_page != NULL && memcmp(default_page, CalSegDefaultPage(calseg), page_size) != 0) {
        DBG_PRINTF_WARNING("Calibration segment '%s' already exists with a different default page\n", calseg->h.name);
        // return XCP_UNDEFINED_CALSEG;
    }
#endif

    return calseg_index;
}

// Helper function to initialize a new calibration segment
// Thread-safe
// Note that preloaded calibration segments have an already existing initialized default page content from loading the persistence file
// This is indicated by default_page = NULL
static bool XcpInitCalSeg_(tXcpCalSeg *calseg, const char *name, const void *default_page, FILE *default_page_file, uint16_t page_size, bool memory_segment) {

    tXcpCalSeg *c = calseg; // Alias
    assert(c != NULL);

    // Align page size to 8 bytes for better performance
    uint16_t aligned_page_size = (page_size + XCP_CALPAGE_ALIGNMENT - 1) & ~(XCP_CALPAGE_ALIGNMENT - 1);

    size_t name_len = STRNLEN(name, XCP_MAX_CALSEG_NAME);
    memcpy(c->h.name, name, name_len);
    c->h.name[name_len] = '\0';
    c->h.size = page_size;
    // In SHM mode, assign the app_id to the segment
#ifdef OPTION_SHM_MODE // store application id in tXcpCalSeg
    c->h.app_id = XcpShmGetAppId();
#endif

    // Create a memory segment with a memory segment number, which can be used for XCP access and has the related XCP features
    if (memory_segment) {
        if (shared.cal_seg_list.memory_segment_count >= 0xFF) {
            DBG_PRINT_ERROR("Too many memory segments\n");
            assert(false);
            return false;
        }
        // @@@@ TODO: Memory segment number allocation not protected by mutex
        c->h.calseg_number = (tXcpCalSegNumber)shared_mut_safe.cal_seg_list.memory_segment_count++;
    } else {
        c->h.calseg_number = XCP_UNDEFINED_CALSEG_NUM; // Not a memory segment
    }

    // Initialize the default page
    // Standard: default page pointer provided by the caller
    if (default_page != NULL) {
        assert(default_page_file == NULL);
#if defined(XCP_ENABLE_ABS_ADDRESSING) && XCP_ADDR_EXT_ABS == 0x00
        c->h.default_page_ptr = (uint8_t *)default_page; // Store the static lifetime default page pointer
#else
        memcpy(CalSegDefaultPage(c), default_page, page_size); // Copy default page to the allocated memory buffer
#endif
    }

    // Preload: Caller wants to create a preinitialized, preloaded segment from file
#ifdef XCP_ENABLE_CAL_PERSISTENCE
    else if (default_page_file != NULL) {
#if defined(XCP_ENABLE_ABS_ADDRESSING) && XCP_ADDR_EXT_ABS == 0x00
        // @@@@ TODO: Implement loading default working page from file in absolute addressing mode (working page persistence)
        DBG_PRINT_ERROR("Not implemented: Loading default page from file in absolute addressing mode\n");
        assert(false);
        return false; // Not supported to load the default page from file in absolute addressing mode
#else
        // Load the default page content from the binary persistence file (reference page persistence)
        size_t read = fread(CalSegDefaultPage(c), 1, page_size, default_page_file);
        if (read != page_size) {
            DBG_PRINTF_ERROR("Failed to read the default page content from file, expected %u bytes, got %zu bytes\n", page_size, read);
            assert(false);
            return false;
        }
#endif
    }
#endif //   XCP_ENABLE_CAL_PERSISTENCE
    else {
        DBG_PRINT_ERROR("No default page provided for calibration segment\n");
        assert(false);
        return false;
    }

    // Reset RCU, XCP passive mode
    CalSegRcuReset(c);
    c->h.write_pending = false;
    c->h.xcp_access = XCP_CALPAGE_DEFAULT_PAGE;                                              // Default page for XCP access if XCP is not activated
    atomic_store_explicit(&c->h.ecu_access, XCP_CALPAGE_DEFAULT_PAGE, memory_order_relaxed); // Default page for ECU access if XCP is not activated

    // Init RCU if XCP is activated
    if (isActivated()) {

        // Initialize the RCU pages with the default page content
        CalSegRcuInit(c, aligned_page_size);

#ifdef XCP_START_ON_REFERENCE_PAGE
        // Enable access to the reference page
        c->h.xcp_access = XCP_CALPAGE_DEFAULT_PAGE;                                              // Default page for XCP access is the reference page
        atomic_store_explicit(&c->h.ecu_access, XCP_CALPAGE_DEFAULT_PAGE, memory_order_relaxed); // Default page for ECU access is the reference page
#else
        // Enable access to the working page
        c->h.xcp_access = XCP_CALPAGE_WORKING_PAGE;                                              // Default page for XCP access is the working page
        atomic_store_explicit(&c->h.ecu_access, XCP_CALPAGE_WORKING_PAGE, memory_order_relaxed); // Default page for ECU access is the working page
#endif
    } // isActivated()

    return true;
}

//----------------------------------------------------------------------------------------------------------

/**************************************************************************/
// RCU primitives
//
// Two implementations, selected with OPTION_CAL_RCU_REFCOUNT (see docs/CAL_RCU.md, chapters 4 and 5)
// Everything else in this file is independent of the RCU algorithm and uses only these functions:
//   CalSegRcuReset      - reset the RCU state, passive mode, no pages
//   CalSegRcuInit       - initialize the RCU pages from the default page, once, before the segment is visible to other threads
//   CalSegRcuLock       - reader: get the active page (working or reference page)
//   CalSegRcuUnlock     - reader: release the page returned by CalSegRcuLock
//   CalSegRcuTryPublish - writer: publish the writer page, false when no page is available yet, the changes stay in the writer page
//   CalSegRcuIsUnlocked - no reader holds a lock, contract check in debug builds

#ifdef OPTION_CAL_RCU_REFCOUNT

//----------------------------------------------------------------------------------------------------------
// Per page reference counting
//
// RCU page 0 is the writer page, it is never published and never pinned by a reader
// The writer publishes by copying the writer page into a free candidate page and announcing it in published_page
// A reader pins the published page with its reference count and re-validates published_page afterwards
// The four operations which couple readers and writer (pin, validation load, reference count load, announcement store)
// are sequentially consistent, the correctness argument needs a single total order over them (see CAL_RCU.md 5.6)
// Publishing is independent of reader progress, the lock is lock-free (retries when a publish lands between its two loads), the unlock is wait-free

static void CalSegRcuReset(tXcpCalSeg *c) {
    c->h.xcp_page = XCP_CALSEG_NO_PAGE;
    atomic_store_explicit(&c->h.published_page, XCP_CALSEG_NO_PAGE, memory_order_relaxed);
    for (uint32_t i = 0; i < XCP_CALSEG_RCU_PAGES; i++) {
        atomic_store_explicit(&c->h.page_refs[i], 0, memory_order_relaxed);
    }
}

static void CalSegRcuInit(tXcpCalSeg *c, uint32_t aligned_page_size) {
    // Writer page (RCU page 0) and published page (RCU page 1) start with the default page content, the candidates are written before they are published
    c->h.xcp_page = CALSEG_RCU_PAGE_OFFSET(aligned_page_size, 0);
    memcpy(CalSegXcpPage(c), CalSegDefaultPage(c), c->h.size);
    memcpy(CalSegRcuPage(c, 1), CalSegDefaultPage(c), c->h.size);
    for (uint32_t i = 0; i < XCP_CALSEG_RCU_PAGES; i++) {
        atomic_store_explicit(&c->h.page_refs[i], 0, memory_order_relaxed);
    }
    atomic_store_explicit(&c->h.published_page, 1, memory_order_release);
}

// Reader: lock, lock-free
static const uint8_t *CalSegRcuLock(tXcpCalSeg *c) {
    // Reference page active: it is immutable, no pin needed
    if (atomic_load_explicit(&c->h.ecu_access, memory_order_relaxed) != XCP_CALPAGE_WORKING_PAGE) {
        return CalSegDefaultPage(c);
    }
    for (;;) {
        uint32_t page = (uint32_t)atomic_load_explicit(&c->h.published_page, memory_order_seq_cst);
        assert(page > 0 && page < XCP_CALSEG_RCU_PAGES);
        atomic_fetch_add_explicit(&c->h.page_refs[page], 1, memory_order_seq_cst); // Pin
        // Still the published page ? Then it holds complete data and the writer will not overwrite it while it is pinned
        if ((uint32_t)atomic_load_explicit(&c->h.published_page, memory_order_seq_cst) == page) {
            return CalSegRcuPage(c, page);
        }
        atomic_fetch_sub_explicit(&c->h.page_refs[page], 1, memory_order_seq_cst); // A publish landed in between, unpin and retry
    }
}

// Reader: unlock, wait-free
static void CalSegRcuUnlock(tXcpCalSeg *c, const uint8_t *page) {
    if (page == CalSegDefaultPage(c)) {
        return; // Reference page was active, nothing pinned
    }
    // Find the RCU page index from the page pointer, page 0 (writer page) is never pinned
    uint32_t aligned_page_size = CalSegAlignedPageSize(c);
    const uint8_t *base = &c->b[CALSEG_RCU_PAGE_BASE(aligned_page_size)];
    uint32_t idx = 1;
    while (idx < XCP_CALSEG_RCU_PAGES && page != base + idx * aligned_page_size) {
        idx++;
    }
    if (idx >= XCP_CALSEG_RCU_PAGES) {
        DBG_PRINTF_ERROR("XcpUnlockCalSeg: calseg %s, the page pointer is not a locked page\n", c->h.name);
        assert(0);
        return;
    }
    uint16_t old_refs = (uint16_t)atomic_fetch_sub_explicit(&c->h.page_refs[idx], 1, memory_order_seq_cst); // Unpin
    (void)old_refs;
    assert(old_refs > 0); // Calling XcpUnlockCalSeg without a matching lock
}

// Writer: publish
static bool CalSegRcuTryPublish(tXcpCalSeg *c) {
    uint32_t published = (uint32_t)atomic_load_explicit(&c->h.published_page, memory_order_seq_cst);
    // Find a target page, neither the writer page (0), nor the published page, nor pinned by a reader
    // Round robin starting after the published page, so that a retired page is not reused before the other candidates
    uint32_t target = XCP_CALSEG_RCU_PAGES; // none
    uint32_t idx = published;
    for (uint32_t i = 1; i < XCP_CALSEG_RCU_PAGES; i++) {
        idx = (idx + 1 < XCP_CALSEG_RCU_PAGES) ? idx + 1 : 1;
        if (idx == published) {
            continue;
        }
        if (atomic_load_explicit(&c->h.page_refs[idx], memory_order_seq_cst) == 0) {
            target = idx;
            break;
        }
    }
    if (target >= XCP_CALSEG_RCU_PAGES) {
        return false; // Every candidate is pinned by a reader
    }
    memcpy(CalSegRcuPage(c, target), CalSegXcpPage(c), c->h.size);             // Copy first ...
    atomic_store_explicit(&c->h.published_page, target, memory_order_seq_cst); // ... then announce
    DBG_PRINTF6("XcpCalSegPublish: %s published page %u\n", c->h.name, target);
    return true;
}

#ifdef XCP_CALSEG_CHECK_UNLOCKED
static bool CalSegRcuIsUnlocked(const tXcpCalSeg *c) {
    for (uint32_t i = 0; i < XCP_CALSEG_RCU_PAGES; i++) {
        if (atomic_load_explicit(&c->h.page_refs[i], memory_order_relaxed) != 0) {
            return false;
        }
    }
    return true;
}
#endif

#else

//----------------------------------------------------------------------------------------------------------
// Lock count and page hand-over
//
// Three pages: ecu_page (readers), xcp_page (writer) and free_page (reclamation list with one element)
// The reader which takes the first lock (lock count 0->1) takes over a published page and hands the old ECU page back as free page
// The free page is confirmed as unused by the next first lock without a pending page (free_page_hazard)
// Publishing depends on the readers, the lock and the unlock are wait-free

static void CalSegRcuReset(tXcpCalSeg *c) {
    c->h.xcp_page = XCP_CALSEG_NO_PAGE;
    atomic_store_explicit(&c->h.ecu_page, XCP_CALSEG_NO_PAGE, memory_order_relaxed);
    atomic_store_explicit(&c->h.free_page, XCP_CALSEG_NO_PAGE, memory_order_relaxed);
    c->h.free_page_hazard = false;
    atomic_store_explicit(&c->h.ecu_page_next, XCP_CALSEG_NO_PAGE, memory_order_relaxed);
    atomic_store_explicit(&c->h.lock_count, 0, memory_order_relaxed);
}

static void CalSegRcuInit(tXcpCalSeg *c, uint32_t aligned_page_size) {
    // Initialize the ECU working page (RAM page)
    atomic_store_explicit(&c->h.ecu_page, (uint_least32_t)ECU_PAGE_OFFSET(aligned_page_size), memory_order_relaxed);
    memcpy(CalSegEcuPage(c), CalSegDefaultPage(c), c->h.size); // Copy default page to ECU page
    // Initialize the XCP working page (RAM page)
    c->h.xcp_page = XCP_PAGE_OFFSET(aligned_page_size);
    memcpy(CalSegXcpPage(c), CalSegDefaultPage(c), c->h.size); // Copy default page to working page
    // Allocate a free uninitialized page
    atomic_store_explicit(&c->h.free_page, (uint_least32_t)FREE_PAGE_OFFSET(aligned_page_size), memory_order_relaxed);
    c->h.free_page_hazard = false;
    // New ECU page version not updated
    atomic_store_explicit(&c->h.ecu_page_next, atomic_load_explicit(&c->h.ecu_page, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&c->h.lock_count, 0, memory_order_relaxed);
}

// Reader: lock, wait-free
// Shared atomic state is lock_count, ecu_page_next, free_page, ecu_page, ecu_access
// Shared non atomic is free_page_hazard, release on free_page
static const uint8_t *CalSegRcuLock(tXcpCalSeg *c) {
    // Increment the lock count
    // Acquire, to make sure the lock is announced before the ECU page offset and the page content are read
    // Pairs with the release in CalSegRcuUnlock, a thread observing old_lock_count==0 then also observes the completed reads of all previous lock holders
    uint16_t old_lock_count = (uint16_t)atomic_fetch_add_explicit(&c->h.lock_count, 1, memory_order_acquire);
    assert(old_lock_count != UINT16_MAX); // Lock count overflow, too many concurrent or nested locks, or missing XcpUnlockCalSeg
    if (old_lock_count == 0) {

        // Update if there is a new page version, free the old page
        // No other thread can modify ecu_page while we hold the first lock, a relaxed load is sufficient here
        uint32_t ecu_page_next = (uint32_t)atomic_load_explicit(&c->h.ecu_page_next, memory_order_acquire);
        uint32_t ecu_page = (uint32_t)atomic_load_explicit(&c->h.ecu_page, memory_order_relaxed);
        if (ecu_page != ecu_page_next) {
            DBG_PRINTF6("XcpLockCalSeg: %s ecu_page updated\n", c->h.name);
            c->h.free_page_hazard = true; // Free page might be acquired by some other thread, since we got the first lock on this segment
            // Release, to make the page content visible to the reader threads which load ecu_page without taking the first lock
            atomic_store_explicit(&c->h.ecu_page, (uint_least32_t)ecu_page_next, memory_order_release);
            assert(ecu_page != XCP_CALSEG_NO_PAGE);
            atomic_store_explicit(&c->h.free_page, (uint_least32_t)ecu_page, memory_order_release);
        } else {
            c->h.free_page_hazard = false; // There was no lock and no need for update, free page must be safe now, if there is one
        }
    }

    // Return the active ECU page (RAM or FLASH)
    if (atomic_load_explicit(&c->h.ecu_access, memory_order_relaxed) != XCP_CALPAGE_WORKING_PAGE) {
        return CalSegDefaultPage(c);
    } else {
        return CalSegEcuPage(c);
    }
}

// Reader: unlock, wait-free
static void CalSegRcuUnlock(tXcpCalSeg *c) {
    // Decrement the lock count
    // Release, to make sure all reads from the ECU page are completed before the lock is released
    // Pairs with the acquire in CalSegRcuLock, which resets free_page_hazard when it observes lock_count==0
    uint16_t old_lock_count = (uint16_t)atomic_fetch_sub_explicit(&c->h.lock_count, 1, memory_order_release);
    (void)old_lock_count;
    assert(old_lock_count > 0); // Calling XcpUnlockCalSeg without a prior lock
}

// Writer: publish
static bool CalSegRcuTryPublish(tXcpCalSeg *c) {
    // Try to allocate a new xcp page
    // In a multithreaded consumer use case, we must be sure the free page is really not in use anymore (free_page_hazard)
    // Acquire/release semantics with CalSegRcuLock on the free page offset
    uint32_t free_page = (uint32_t)atomic_load_explicit(&c->h.free_page, memory_order_acquire);
    if (free_page == XCP_CALSEG_NO_PAGE || c->h.free_page_hazard) {
        return false; // No free page available, or it is not confirmed as unused yet
    }

    // Acquire the free page
    uint32_t xcp_page_new = free_page;
    atomic_store_explicit(&c->h.free_page, (uint_least32_t)XCP_CALSEG_NO_PAGE, memory_order_release);

    // Copy old xcp page to the new xcp page
    uint32_t xcp_page_old = c->h.xcp_page;
    memcpy(&c->b[xcp_page_new], &c->b[xcp_page_old], c->h.size); // Copy the xcp page
    c->h.xcp_page = xcp_page_new;

    // Publish the old xcp page
    // Acquire/release semantics with CalSegRcuLock on the ecu_page_next offset
    atomic_store_explicit(&c->h.ecu_page_next, (uint_least32_t)xcp_page_old, memory_order_release);
    DBG_PRINTF6("XcpCalSegPublish: %s xcp_page published\n", c->h.name);
    return true;
}

#ifdef XCP_CALSEG_CHECK_UNLOCKED
static bool CalSegRcuIsUnlocked(const tXcpCalSeg *c) { return atomic_load_explicit(&c->h.lock_count, memory_order_relaxed) == 0; }
#endif

#endif // OPTION_CAL_RCU_REFCOUNT

//----------------------------------------------------------------------------------------------------------

// Lock a calibration segment and return a pointer to the active page (working or reference page)
// Thread safe
const uint8_t *XcpLockCalSeg(tXcpCalSegIndex calseg_index) {

    // User contract (see docs/CAL_RCU.md): XcpInit() has completed, XCP is activated and calseg_index is a valid handle
    // Passive mode (XCP_MODE_DEACTIVATE) is resolved by the CalSegLock() macros and the C++ wrappers, they return the default page without calling this function
    // The checks below are contract assertions only, they are compiled out in release builds
#ifndef NDEBUG
    if (!isActivated()) {
        DBG_PRINT_ERROR("XcpLockCalSeg: XCP not activated\n");
        assert(0);
        return NULL;
    }
    if (calseg_index >= atomic_load_explicit(&shared.cal_seg_list.count, memory_order_relaxed)) {
        DBG_PRINTF_ERROR("XcpLockCalSeg: Invalid calseg index %u\n", calseg_index);
        assert(0);
        return NULL; // Uninitialized or invalid calseg_index
    }
#endif

    tXcpCalSeg *c = CalSegPtrMut(calseg_index);
    return CalSegRcuLock(c);
}

// Unlock a calibration segment
// page is the pointer returned by the matching XcpLockCalSeg
// Thread safe
#ifdef OPTION_CAL_RCU_REFCOUNT
void XcpUnlockCalSeg(tXcpCalSegIndex calseg_index, const void *page) {
#else
void XcpUnlockCalSeg(tXcpCalSegIndex calseg_index) {
#endif

    // User contract (see docs/CAL_RCU.md): XCP is still activated (XcpDeinit() may not be called while a lock is held) and calseg_index is a valid handle
    // The checks below are contract assertions only, they are compiled out in release builds
#ifndef NDEBUG
    if (!isActivated()) {
        DBG_PRINT_ERROR("XcpUnlockCalSeg: XCP not activated\n");
        assert(0);
        return;
    }
    if (calseg_index >= atomic_load_explicit(&shared.cal_seg_list.count, memory_order_relaxed)) {
        DBG_PRINTF_ERROR("XcpUnlockCalSeg: Invalid calseg index %u\n", calseg_index);
        assert(0);
        return; // Uninitialized or invalid calseg_index
    }
#endif

    tXcpCalSeg *c = CalSegPtrMut(calseg_index);
#ifdef OPTION_CAL_RCU_REFCOUNT
    CalSegRcuUnlock(c, (const uint8_t *)page);
#else
    CalSegRcuUnlock(c);
#endif
}

//----------------------------------------------------------------------------------------------------------

// XCP client memory read
// Read xcp or default page
// Read ecu page is not supported, calibration changes might be stale
// Note: This is one of the compromises we make for this simple RCU: XCP read operations may not reflect the current state of the ECU, actual calibration changes may happen later
// Single threaded function, called from XCP server thread !!!
uint8_t XcpCalSegReadMemory(uint32_t src, uint16_t size, uint8_t *dst) {

    // Decode the source address into calibration segment and offset
    uint16_t calseg_index = XcpAddrDecodeSegNumber(src); // Get the calibration segment number from the address
    uint16_t offset = XcpAddrDecodeSegOffset(src);       // Get the offset within the calibration segment

    if (calseg_index >= XcpGetCalSegCount()) {
        DBG_PRINTF_ERROR("invalid calseg index %u\n", calseg_index);
        return CRC_ACCESS_DENIED;
    }
    const tXcpCalSeg *c = CalSegPtr(calseg_index);
    assert(c != NULL);
    if (offset + size > c->h.size) {
        DBG_PRINTF_ERROR("out of bound calseg read access (addr=%08X, size=%u)\n", src, size);
        return CRC_ACCESS_DENIED;
    }

    memcpy(dst, c->h.xcp_access != XCP_CALPAGE_WORKING_PAGE ? CalSegDefaultPage(c) + offset : CalSegXcpPage(c) + offset, size);
    return CRC_CMD_OK;
}

// Publish a modified calibration segment
// Option to wait for this, or return unsuccessful with CRC_CMD_PENDING
// Wait timeout is XCP_CALSEG_AQUIRE_FREE_PAGE_TIMEOUT in ms
// Single threaded function, called from XcpCalSegPublishAll or XcpCalSegWriteMemory in the XCP server thread
static uint8_t XcpCalSegPublish(tXcpCalSeg *c, bool wait) {
    // Note: This is one of the compromises we make for this RCU: Calibration changes are delayed while no page is available, this depends on the progress of the readers
    bool published = CalSegRcuTryPublish(c);
    if (!published && wait) {
        // Wait and delay the XCP server receive thread, until a page becomes available
        for (int timeout = 0; timeout < XCP_CALSEG_AQUIRE_FREE_PAGE_TIMEOUT && !published; timeout++) {
            sleepUs(1000);
            published = CalSegRcuTryPublish(c);
        }
    }
    if (!published) {
        c->h.write_pending = true; // Keep the changes pending in the xcp page, to retry publishing them later
#ifdef TEST_ENABLE_CAL_METRICS
        gXcpWritePendingCount++;
#endif
        if (wait) {
            DBG_PRINTF_ERROR("Can not update calibration changes, timeout - calseg %s locked\n", c->h.name);
            return CRC_ACCESS_DENIED;
        }
        DBG_PRINTF6("Can not update calibration changes of %s yet\n", c->h.name);
        return CRC_CMD_PENDING;
    }
    c->h.write_pending = false; // No longer pending
    return CRC_CMD_OK;
}

// Publish all modified calibration segments
// Single threaded function, called from XCP server thread !!!
// Returns CRC_CMD_OK if all segments have been published
uint8_t XcpCalSegPublishAll(bool wait) {
    uint8_t res = CRC_CMD_OK;
    // If no atomic calibration operation is in progress
    if (!shared.cal_seg_list.write_delayed) {
        // Iterate cal_seg_list cal_seg_list
        uint16_t n = XcpGetCalSegCount();
        uint16_t i = 0;
#ifdef XCP_ENABLE_EPK_CALSEG
        i = 1;
#endif
        for (; i < n; i++) {
            // @@@@ TODO: Could be called from foreign thread through XcpDisconnect, find a solution
            tXcpCalSeg *c = CalSegPtrMut(i);
            if (c->h.write_pending) {
                uint8_t res1 = XcpCalSegPublish(c, wait);
                if (res1 == CRC_CMD_OK) {
#ifdef TEST_ENABLE_CAL_METRICS
                    gXcpCalSegPublishAllCount++;
#endif
                } else {
                    res = res1;
                }
            }
        }
    }
    return res; // Return the last error code
}

// Memory write
// Write xcp page, error on write to default page or EPK segment
// Single threaded function, called from XCP server thread !!!
uint8_t XcpCalSegWriteMemory(uint32_t dst, uint16_t size, const uint8_t *src) {
    // Decode the destination address into calibration segment index and offset
    uint16_t calseg_index = XcpAddrDecodeSegNumber(dst);
    uint16_t offset = XcpAddrDecodeSegOffset(dst);

    if (calseg_index >= XcpGetCalSegCount()) {
        DBG_PRINTF_ERROR("invalid calseg number %u\n", calseg_index);
        return CRC_ACCESS_DENIED;
    }
    tXcpCalSeg *c = CalSegPtrMut(calseg_index);
    if (offset + size > c->h.size) {
        DBG_PRINTF_ERROR("out of bound calseg write access (number=%u, offset=%u, size=%u)\n", calseg_index, offset, size);
        return CRC_ACCESS_DENIED;
    }
    if (c->h.xcp_access != XCP_CALPAGE_WORKING_PAGE) {
        DBG_PRINTF_ERROR("attempt to write default page addr=%08X\n", dst);
        return CRC_ACCESS_DENIED;
    }

    DBG_PRINTF6("XcpCalSegWriteMemory: %s xcp_page writen at %04X\n", c->h.name, dst);

    // Update data in the current xcp page
    memcpy(CalSegXcpPage(c) + offset, src, size);

    // Calibration page RCU
    // If write delayed, we do not update the ECU page yet
    if (shared.cal_seg_list.write_delayed) {
        c->h.write_pending = true; // Modified
        return CRC_CMD_OK;
    } else {
#ifdef XCP_ENABLE_CALSEG_LAZY_WRITE
        // If lazy mode is enabled, try update, but we do not require to update the ECU page yet
        XcpCalSegPublish(c, false);
        return CRC_CMD_OK;
#else
        // If not lazy mode, we wait until a free page is available
        // This should succeed
        return XcpCalSegPublish(c, true);

#endif
    }
}

// Update the EKP segment with the current EPK value
// Silently ignored, if the EPK segment is not present
// Parameter epk is an array[XCP_EPK_MAX_LENGTH+1] which has to contain a null-terminated string
#ifdef OPTION_SHM_MODE // Update the EPK
#ifdef XCP_ENABLE_EPK_CALSEG
void XcpCalUpdateEpkSeg(const char epk[XCP_EPK_MAX_LENGTH + 1]) {
    if (XcpGetCalSegCount() > 0) {
        tXcpCalSeg *c = CalSegPtrMut(0); // EPK segment is always at index 0
        assert(c != NULL);
        assert(c->h.size >= XCP_EPK_MAX_LENGTH + 1);
        memcpy(CalSegDefaultPage(c), epk, XCP_EPK_MAX_LENGTH + 1); // Update the default page with the current EPK value
        // @@@@ TODO: Direct write into the published ECU page from the writer thread, a reader may hold this page, check if this is acceptable
        // The RCU conform way is to write the xcp page and publish it
        memcpy(CalSegEcuPage(c), epk, XCP_EPK_MAX_LENGTH + 1); // Update the ECU page with the current EPK value
        memcpy(CalSegXcpPage(c), epk, XCP_EPK_MAX_LENGTH + 1); // Update the XCP page with the current EPK value
        DBG_PRINTF4(ANSI_COLOR_BLUE "EPK calibration segment 'epk' updated to  '%s'\n" ANSI_COLOR_RESET, epk);
    } else {
        DBG_PRINTF5(ANSI_COLOR_BLUE "EPK calibration segment 'epk' not present, EPK '%s' not updated\n" ANSI_COLOR_RESET, epk);
    }
}
#endif
#endif

//----------------------------------------------------------------------------------------------------------

// Table 97 GET SEGMENT INFO command structure
// Returns information on a specific SEGMENT.
// If the specified SEGMENT is not available, ERR_OUT_OF_RANGE will be returned.
uint8_t XcpGetSegInfo(tXcpCalSegNumber segment_number, uint8_t mode, uint8_t seg_info, uint8_t map_index) {
    (void)map_index; // Mapping not supported

    tXcpCalSegIndex calseg_index = XcpGetCalSegIndex(segment_number);
    if (calseg_index == XCP_UNDEFINED_CALSEG) {
        DBG_PRINTF_ERROR("invalid segment number: %u\n", segment_number);
        return CRC_OUT_OF_RANGE;
    }
    const tXcpCalSeg *c = CalSegPtr(calseg_index);
    assert(c != NULL);
    // 0 - basic address info, 1 - standard info, 2 - mapping info
    switch (mode) {
    case 0: // Mode 0 - get address or length depending on seg_info
        CRM_LEN = CRM_GET_SEGMENT_INFO_LEN_MODE0;
        if (seg_info == 0) { // Get address
            CRM_GET_SEGMENT_INFO_BASIC_INFO = XcpGetCalSegBaseAddress(calseg_index);
            return CRC_CMD_OK;
        } else if (seg_info == 1) { // Get length
            CRM_GET_SEGMENT_INFO_BASIC_INFO = (uint32_t)c->h.size;
            return CRC_CMD_OK;
        } else if (seg_info == 2) { // Get segment name (Vector extension, name via MTA and upload)
            CRM_GET_SEGMENT_INFO_BASIC_INFO = (uint32_t)STRNLEN(c->h.name, XCP_MAX_CALSEG_NAME);
            // Segment name provided via upload
            local_mut.mta_ptr = (uint8_t *)c->h.name;
            local_mut.mta_ext = XCP_ADDR_EXT_PTR;
            return CRC_CMD_OK;
        } else {
            return CRC_OUT_OF_RANGE;
        }
        break;
    case 1: // Get standard info for this SEGMENT
        CRM_LEN = CRM_GET_SEGMENT_INFO_LEN_MODE1;
        CRM_GET_SEGMENT_INFO_MAX_PAGES = 2;
        CRM_GET_SEGMENT_INFO_ADDRESS_EXTENSION = 0; // Address extension for segments is always 0 (SEG or ABS address mode)
        CRM_GET_SEGMENT_INFO_MAX_MAPPING = 0;
        CRM_GET_SEGMENT_INFO_COMPRESSION = 0;
        CRM_GET_SEGMENT_INFO_ENCRYPTION = 0;
        return CRC_CMD_OK;
    case 2: // Mode 2 - get mapping info not supported
        return CRC_OUT_OF_RANGE;
    default: // Illegal mode
        return CRC_CMD_SYNTAX;
    }
}

uint8_t XcpGetSegPageInfo(tXcpCalSegNumber segment_number, uint8_t page) {

    // Check if this segment number is valid
    tXcpCalSegIndex calseg_index = XcpGetCalSegIndex(segment_number);
    if (calseg_index == XCP_UNDEFINED_CALSEG) {
        DBG_PRINTF_ERROR("invalid segment number: %u\n", segment_number);
        return CRC_OUT_OF_RANGE;
    }

    CRM_LEN = CRM_GET_PAGE_INFO_LEN;

#ifdef XCP_ENABLE_CAL_PAGE // If GET/SET_CAL_PAGE enabled, support 2 pages
    if (page > 1)
        return CRC_PAGE_NOT_VALID;
#else
    if (page != 0)
        return CRC_PAGE_NOT_VALID;
#endif

    // All segments have the same page properties
    // PAGE 0: ECU_ACCESS_DONT_CARE, XCP_READ_ACCESS_DONT_CARE, XCP_WRITE_ACCESS_DONT_CARE
    // PAGE 1: ECU_ACCESS_DONT_CARE, XCP_READ_ACCESS_DONT_CARE, XCP_WRITE_ACCESS_NOT_ALLOWED
    if (page == XCP_CALPAGE_WORKING_PAGE) {
        CRM_GET_PAGE_INFO_PROPERTIES = 0x3F; // All bits 0..5 can be set for "don't care"
    } else if (page == XCP_CALPAGE_DEFAULT_PAGE) {
        CRM_GET_PAGE_INFO_PROPERTIES = 0x0F; // All bits 0..3 can be set for "don't care", but XCP write access not allowed (bits 4,5 not set)
    }
    CRM_GET_PAGE_INFO_INIT_SEGMENT = (uint8_t)(XCP_CALPAGE_DEFAULT_PAGE);
    return CRC_CMD_OK;
}

#ifdef XCP_ENABLE_CAL_PAGE // Calibration page support with GET/SET_CAL_PAGE and COPY_CAL_PAGE enabled

// Get active ecu or xcp calibration page
// Note: XCP/A2L segment numbers are bytes, 0 is reserved for the EPK segment, tXcpCalSegIndex is the XCP/A2L segment number - 1
uint8_t XcpCalSegGetCalPage(tXcpCalSegNumber segment_number, uint8_t mode) {
    tXcpCalSegIndex calseg_index = XcpGetCalSegIndex(segment_number);
    if (calseg_index == XCP_UNDEFINED_CALSEG) {
        DBG_PRINTF_ERROR("invalid segment number: %u\n", segment_number);
        return XCP_CALPAGE_INVALID_PAGE;
    }
    if (mode == CAL_PAGE_MODE_ECU) {
        return (uint8_t)atomic_load_explicit(&CalSegPtr(calseg_index)->h.ecu_access, memory_order_relaxed);
    }
    if (mode == CAL_PAGE_MODE_XCP) {
        return CalSegPtr(calseg_index)->h.xcp_access;
    }
    DBG_PRINT_ERROR("invalid get cal page mode\n");
    return XCP_CALPAGE_INVALID_PAGE; // Invalid mode
}

// Set active ecu and/or xcp calibration page
// Note: XCP/A2L segment numbers are bytes, 0 is reserved for the EPK segment, tXcpCalSegIndex is the XCP/A2L segment number - 1
// Single threaded function, called from XCP command handler
uint8_t XcpCalSegSetCalPage(tXcpCalSegNumber segment_number, uint8_t page, uint8_t mode) {
    tXcpCalSegIndex calseg_index = XcpGetCalSegIndex(segment_number);
    if (calseg_index == XCP_UNDEFINED_CALSEG) {
        DBG_PRINTF_ERROR("invalid segment number: %u\n", segment_number);
        return CRC_ACCESS_DENIED; // Invalid calseg
    }
    if (page > 1) {
        DBG_PRINTF_ERROR("invalid cal page number %u\n", page);
        return CRC_ACCESS_DENIED; // Invalid calseg
    }
    if (mode & CAL_PAGE_MODE_ALL) { // Set all calibration segments to the same page
        // Iterate cal_seg_list cal_seg_list
        uint16_t n = XcpGetCalSegCount();
        for (tXcpCalSegIndex i = 0; i < n; i++) {
            if (mode & CAL_PAGE_MODE_ECU) {
                atomic_store_explicit(&CalSegPtrMut(i)->h.ecu_access, page, memory_order_relaxed);
            }
            if (mode & CAL_PAGE_MODE_XCP) {
                CalSegPtrMut(i)->h.xcp_access = page;
            }
        }
    } else {
        if (mode & CAL_PAGE_MODE_ECU) {
            atomic_store_explicit(&CalSegPtrMut(calseg_index)->h.ecu_access, page, memory_order_relaxed);
        }
        if (mode & CAL_PAGE_MODE_XCP) {
            CalSegPtrMut(calseg_index)->h.xcp_access = page;
        }
    }
    return CRC_CMD_OK;
}

// Copy calibration page
// Note: XCP/A2L segment numbers are bytes, 0 is reserved for the EPK segment, tXcpCalSegIndex is the XCP/A2L segment number - 1
// Single threaded function, called from XCP command handler
#ifdef XCP_ENABLE_COPY_CAL_PAGE
uint8_t XcpCalSegCopyCalPage(tXcpCalSegNumber src_seg_num, uint8_t src_page, tXcpCalSegNumber dst_seg_num, uint8_t dst_page) {

    // Only copy from default page to working page supported
    tXcpCalSegIndex dst_seg_index = XcpGetCalSegIndex(dst_seg_num);
    if (src_seg_num != dst_seg_num || dst_seg_index == XCP_UNDEFINED_CALSEG || dst_page != XCP_CALPAGE_WORKING_PAGE || src_page != XCP_CALPAGE_DEFAULT_PAGE) {
        DBG_PRINT_ERROR("unsupported or invalid calseg copy operation\n");
        return CRC_WRITE_PROTECTED;
    }

#ifdef XCP_ENABLE_COPY_CAL_PAGE_WORKAROUND

    // Older CANapes < 24SP1  do not send individual segment copy operations for each segment
    // Copy all existing segments from default page to working page, ignoring srcSeg/dstSeg
    // Iterate cal_seg_list cal_seg_list
    uint16_t n = XcpGetCalSegCount();
    for (tXcpCalSegIndex i = 0; i < n; i++) {
        const tXcpCalSeg *c = CalSegPtr(i);
        assert(c != NULL);
        uint16_t size = c->h.size;
        const uint8_t *srcPtr = CalSegDefaultPage(c);
        uint8_t res = XcpCalSegWriteMemory(XcpAddrEncodeSegIndex(i, 0), size, srcPtr);
        if (res != CRC_CMD_OK) {
            return res;
        }
    }
    return CRC_CMD_OK;

#else

    const tXcpCalSeg *c = CalSegPtr(dst_seg_index);
    assert(c != NULL);
    uint16_t size = c->h.size;
    const uint8_t *srcPtr = CalSegDefaultPage(c);
    return XcpCalSegWriteMemory(XcpAddrEncodeSegNumber(dst_seg_index, 0), size, srcPtr);

#endif
}
#endif // XCP_ENABLE_COPY_CAL_PAGE
#endif // XCP_ENABLE_CAL_PAGE

// Handle atomic calibration segment updates
// Single threaded function, called from XCP command handler
void XcpCalSegBeginAtomicTransaction(void) {
    // Note: The write_pending flags are not reset here
    // Changes from previous writes which could not be published yet are still collected in the xcp pages
    // They stay pending and are published together with this transaction, resetting the flags here would silently drop them
    shared_mut.cal_seg_list.write_delayed = true; // Set a flag to delay ECU page updates
    DBG_PRINT4("Begin atomic calibration operation\n");
}
uint8_t XcpCalSegEndAtomicTransaction(void) {
    shared_mut.cal_seg_list.write_delayed = false; // Reset the write delay flag
    DBG_PRINT4("End atomic calibration operation\n");
    return XcpCalSegPublishAll(true); // Flush all pending writes, return true if successful
}

// Freeze calibration segment working pages
// Note: XCP/A2L segment numbers (tXcpCalSegNumber) are bytes, 0 is reserved for the EPK segment, tXcpCalSegIndex is the XCP/A2L segment number - 1
// Single threaded function, called from XCP command handler
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE

uint8_t XcpGetCalSegMode(tXcpCalSegNumber segment_number) {
    tXcpCalSegIndex calseg_index = XcpGetCalSegIndex(segment_number);
    if (calseg_index == XCP_UNDEFINED_CALSEG)
        return 0;                           // Segment number out of range, ignore
    return CalSegPtr(calseg_index)->h.mode; // Return the segment mode
}

uint8_t XcpSetCalSegMode(tXcpCalSegNumber segment_number, uint8_t mode) {
    tXcpCalSegIndex calseg_index = XcpGetCalSegIndex(segment_number);
    if (calseg_index == XCP_UNDEFINED_CALSEG)
        return CRC_OUT_OF_RANGE;             // Segment number out of range
    if ((mode & PAG_PROPERTY_FREEZE) != 0) { // Set freeze enabled bit
        CalSegPtrMut(calseg_index)->h.mode |= PAG_PROPERTY_FREEZE;
    } else {
        CalSegPtrMut(calseg_index)->h.mode &= ~PAG_PROPERTY_FREEZE;
    }
    return CRC_CMD_OK;
}

// Freeze all segments or segments with freeze mode enabled
uint8_t XcpFreezeSelectedCalSegs(bool all) {

    // Iterate cal_seg_list cal_seg_list
    uint16_t n = XcpGetCalSegCount();
    uint16_t i = 0;
#ifdef XCP_ENABLE_EPK_CALSEG
    i = 1;
#endif
    for (; i < n; i++) {
        const tXcpCalSeg *c = CalSegPtr(i);
        assert(c != NULL);
        if ((c->h.mode & PAG_PROPERTY_FREEZE) != 0 || all) {
            DBG_PRINTF3("Freeze cal seg '%s' (size=%u, pos=%u)\n", c->h.name, c->h.size, c->h.file_pos);
            if (!XcpBinFreezeCalSeg(i)) {
                return CRC_ACCESS_DENIED; // Access denied, freeze failed
            }
        }
    }
    return CRC_CMD_OK;
}

// Freeze all
bool XcpFreeze(void) {

#ifdef OPTION_SHM_MODE // only the server is allowed to freeze all segments
    if (!XcpShmIsXcpServer())
        return false;
#endif

    return CRC_CMD_OK == XcpFreezeSelectedCalSegs(true);
}

// Set all segments to the default page
void XcpResetAllCalSegs(void) {

    // Iterate cal_seg_list cal_seg_list
    uint16_t n = XcpGetCalSegCount();
    uint16_t i = 0;
#ifdef XCP_ENABLE_EPK_CALSEG
    i = 1;
#endif
    for (; i < n; i++) {
        tXcpCalSeg *c = CalSegPtrMut(i);
        atomic_store_explicit(&c->h.ecu_access, XCP_CALPAGE_DEFAULT_PAGE, memory_order_relaxed); // Default page for ECU access is the working page
    }
    XcpDisconnect(); // Reset the session status
}

#endif // XCP_ENABLE_FREEZE_CAL_PAGE

#endif // XCP_ENABLE_CALSEG_LIST
