/*----------------------------------------------------------------------------
| File:
|   persistence.c
|
| Description:
|   Read and write binary file for calibration segment persistence
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "persistence.h"

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <stdarg.h>   // for va_
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t
#include <stdio.h>    // for fclose, fopen, fread, fseek, ftell
#include <string.h>   // for strlen, strncpy

#include "dbg_print.h"  // for DBG_PRINTF3, DBG_PRINT4, DBG_PRINTF4, DBG...
#include "platform.h"   // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex
#include "shm.h"        // for shared memory management
#include "xcp.h"        // for CRC_XXX
#include "xcp_cfg.h"    // for XCP_xxx
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcplite.h"    // for tXcpDaqLists, XcpXxx, ApplXcpXxx, ...
#include "xcptl_cfg.h"  // for XCPTL_xxx

#ifdef OPTION_ENABLE_PERSISTENCE

#if !defined(XCP_ENABLE_DAQ_EVENT_LIST) || !defined(XCP_ENABLE_CALSEG_LIST)
#error "XCP_ENABLE_DAQ_EVENT_LIST and XCP_ENABLE_CALSEG_LIST must be enabled for calibration segment persistence"
#endif

#define BIN_SIGNATURE "XCPLITE__BINARY"
#define BIN_VERSION 0x0205

#pragma pack(push, 1)

typedef struct {
    char signature[16];                              // File signature "XCPLITE__BINARY"
    uint16_t version;                                // File version
    uint16_t event_count;                            // Number of events, tEventDescriptor
    uint16_t calseg_count;                           // Number of calibration segments, tCalSegDescriptor
    uint16_t app_count;                              // Number of applications (processes) in SHM mode, 0 in local mode
    uint8_t reserved[128 - 16 - 2 - 2 - 2 - 2];      // Reserved for future use
    char Epk[XCP_EPK_MAX_LENGTH + 1];                // EPK string, 0 terminated
    uint8_t padding[128 - (XCP_EPK_MAX_LENGTH + 1)]; // Reserved for longer EPK strings up to 128 bytes
} tHeader;

static_assert(sizeof(tHeader) == 256, "Size of tHeader must be 256 bytes");

typedef struct {
    uint16_t id;                                     // Event ID
    uint16_t index;                                  // Event index
    uint32_t cycle_time_ns;                          // Cycle time in ns
    uint8_t priority;                                // Priority 0 = queued, 1 = pushing, 2 = realtime
    uint8_t app_id;                                  // App ID of the event owner in SHM mode, 0 in local mode
    uint8_t reserved[128 - 2 - 2 - 4 - 1 - 1];       // Reserved for future use
    char name[XCP_MAX_EVENT_NAME + 1];               // Event name, 0 terminated
    uint8_t padding[128 - (XCP_MAX_EVENT_NAME + 1)]; // Reserved for longer event names up to 128 bytes
} tEventDescriptor;

static_assert(sizeof(tEventDescriptor) == 256, "Size of tEventDescriptor must be 256 bytes");

typedef struct {
    uint16_t index;                                   // Index of the calibration segment in the list, 0..<XCP_MAX_CALSEG_COUNT
    uint16_t size;                                    // Size of the calibration segment in bytes, multiple of 4
    uint32_t addr;                                    // Address of the calibration segment
    uint8_t app_id;                                   // App ID of the calibration segment owner in SHM mode, 0 in local mode
    uint8_t number;                                   // Memory segment number
    uint8_t reserved[128 - 2 - 2 - 4 - 1 - 1];        // Reserved for future use
    char name[XCP_MAX_CALSEG_NAME + 1];               // Calibration segment name, 0 terminated
    uint8_t padding[128 - (XCP_MAX_CALSEG_NAME + 1)]; // Reserved for longer calibration segment names up to 128 bytes
} tCalSegDescriptor;

static_assert(sizeof(tCalSegDescriptor) == 256, "Size of tCalSegDescriptor must be 256 bytes");

typedef struct {
    uint8_t app_id;                                                                      // App ID of the application owner in SHM mode, 0 in local mode
    uint8_t xcp_init_mode;                                                               // Bitfield of XCP_MODE_XXX (for XcpInit())
    uint8_t reserved[128 - 2];                                                           // Reserved for future use
    char project_name[XCP_PROJECT_NAME_MAX_LENGTH + 1];                                  // Application name, 0 terminated
    char epk[XCP_EPK_MAX_LENGTH + 1];                                                    // build version  (null-terminated)
    uint8_t padding[128 - (XCP_PROJECT_NAME_MAX_LENGTH + 1) - (XCP_EPK_MAX_LENGTH + 1)]; // Reserved for longer application names up to 128 bytes
} tAppDescriptor;

static_assert(sizeof(tAppDescriptor) == 256, "Size of tAppDescriptor must be 256 bytes");

#pragma pack(pop)

static tHeader gBinHeader;

//--------------------------------------------------------------------------------------------------------------------------------

#define XCP_BIN_FILENAME_MAX_LENGTH 255 // Maximum length of BIN filename with extension
static char gXcpBinFilename[XCP_BIN_FILENAME_MAX_LENGTH + 1] = "";

// Build BIN filename from project name and epk, e.g. "app_name_V100.bin" (or "ecu_name.bin" in SHM mode, written by the server)
static const char *XcpBinGetFilename(void) {
#ifdef OPTION_SHM_MODE // generate BIN filename without EPK postfix
    // Only server creates the persistence file with unique name
    SNPRINTF(gXcpBinFilename, XCP_BIN_FILENAME_MAX_LENGTH, "%s.bin", XcpShmGetEcuProjectName());
    return gXcpBinFilename;
#else
    SNPRINTF(gXcpBinFilename, XCP_BIN_FILENAME_MAX_LENGTH, "%s_%s.bin", XcpGetProjectName(), XcpGetEpk());
    return gXcpBinFilename;
#endif
}

// Print the content of a calibration segment page for debugging
#ifdef OPTION_ENABLE_DBG_PRINTS
static void printCalsegPage(const uint8_t *page, uint16_t size) {
    printf(ANSI_COLOR_GREY);
    for (uint16_t i = 0; i < size; i++) {
        printf("%02X ", page[i]);
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    if (size % 16 != 0) {
        printf("\n");
    }
    printf(ANSI_COLOR_RESET);
}
#endif

// Write the BIN file header
static bool writeHeader(FILE *file, const char *epk, uint16_t event_count, uint16_t calseg_count, uint8_t app_count) {

    memset(&gBinHeader, 0, sizeof(tHeader));
    strncpy(gBinHeader.signature, BIN_SIGNATURE, sizeof(gBinHeader.signature) - 1);
    gBinHeader.signature[sizeof(gBinHeader.signature) - 1] = '\0'; // Ensure null termination
    gBinHeader.version = BIN_VERSION;
    strncpy(gBinHeader.Epk, epk, XCP_EPK_MAX_LENGTH);
    gBinHeader.Epk[XCP_EPK_MAX_LENGTH] = '\0';
    gBinHeader.event_count = event_count;
    gBinHeader.calseg_count = calseg_count;
    gBinHeader.app_count = app_count;
    size_t written = fwrite(&gBinHeader, sizeof(tHeader), 1, file);
    if (written != 1) {
        DBG_PRINT_ERROR("Failed to write header to BIN file\n");
        return false;
    }
    return true;
}

// Write an event descriptor to the BIN file
static bool writeEvent(FILE *file, tXcpEventId event_id, const tXcpEvent *event) {
    tEventDescriptor desc;
    memset(&desc, 0, sizeof(tEventDescriptor));
    memcpy(desc.name, event->name, sizeof(desc.name));
    // In SHM mode, save the app_id of the event owner
#ifdef OPTION_SHM_MODE // initialized app-id in event descriptor
    desc.app_id = event->app_id;
#endif // SHM_MODE
    desc.cycle_time_ns = event->cycle_time_ns;
    desc.priority = event->flags & XCP_DAQ_EVENT_FLAG_PRIORITY ? 0xFF : 0x00;
    desc.id = event_id;
    desc.index = XcpGetEventIndex(event_id);
    size_t written = fwrite(&desc, sizeof(tEventDescriptor), 1, file);
    if (written != 1) {
        DBG_PRINT_ERROR("Failed to write event descriptor to BIN file\n");
        return false;
    }
    DBG_PRINTF4("Writing event %u:'%s' cycle_time_ns=%u %s\n", event_id, event->name, event->cycle_time_ns, desc.priority == 0xFF ? "high priority" : "normal priority");
    return true;
}

// Write a calibration segment descriptor and page data to the BIN file
static bool writeCalseg(FILE *file, tXcpCalSegIndex calseg, const tXcpCalSeg *seg, uint8_t page) {
    tCalSegDescriptor desc;
    memset(&desc, 0, sizeof(tCalSegDescriptor));
    memcpy(desc.name, seg->h.name, sizeof(desc.name)); // src and dst are same size, null-terminated
    desc.size = seg->h.size;
    desc.addr = XcpGetCalSegBaseAddress(calseg);
    desc.index = calseg;
    desc.number = seg->h.calseg_number;
    // In SHM mode, save the app_id of the calibration segment owner
#ifdef OPTION_SHM_MODE // initialize app-id in calibration segment descriptor
    desc.app_id = seg->h.app_id;
#endif // SHM_MODE
    size_t written = fwrite(&desc, sizeof(tCalSegDescriptor), 1, file);
    if (written != 1) {
        DBG_PRINT_ERROR("Failed to write calibration segment descriptor to BIN file\n");
        return false;
    }
    // @@@@ TODO: Cast away const, improve design to avoid this
    ((tXcpCalSeg *)seg)->h.file_pos = (uint32_t)ftell(file); // Save the position of the segment page data in the file
    DBG_PRINTF4("Writing calibration segment %u:'%s' size=%u %s page data:\n", calseg, seg->h.name, seg->h.size, page == XCP_CALPAGE_DEFAULT_PAGE ? "default" : "working");
#ifdef OPTION_ENABLE_DBG_PRINTS
    if (DBG_LEVEL >= 5)
        printCalsegPage(page == XCP_CALPAGE_DEFAULT_PAGE ? CalSegDefaultPage(seg) : CalSegEcuPage(seg), seg->h.size);
#endif

    // Write the calibration segment page data to the file, either default page or working page, depending on the specified page parameter
    // This is safe, because XCP is not connected
    const uint8_t *page_ptr = (page == XCP_CALPAGE_DEFAULT_PAGE) ? CalSegDefaultPage(seg) : CalSegEcuPage(seg);
    written = fwrite(page_ptr, seg->h.size, 1, file);
    if (written != 1) {
        DBG_PRINT_ERROR("Failed to write calibration segment data to BIN file\n");
        return false;
    }
    return true;
}

// In SHM mode, the .BIN file contains an application list
#ifdef OPTION_SHM_MODE // write an application descriptor to BIN file

// Write a application descriptor
static bool writeApp(FILE *file, uint8_t app_id, const char *project_name, const char *epk, uint8_t xcp_init_mode) {
    assert(project_name != NULL);
    assert(epk != NULL);
    tAppDescriptor desc;
    memset(&desc, 0, sizeof(tAppDescriptor));
    strncpy(desc.project_name, project_name, XCP_PROJECT_NAME_MAX_LENGTH);
    desc.project_name[XCP_PROJECT_NAME_MAX_LENGTH] = '\0'; // Ensure null termination
    strncpy(desc.epk, epk, XCP_EPK_MAX_LENGTH);
    desc.epk[XCP_EPK_MAX_LENGTH] = '\0'; // Ensure null termination
    desc.xcp_init_mode = xcp_init_mode;
    desc.app_id = app_id;
    size_t written = fwrite(&desc, sizeof(tAppDescriptor), 1, file);
    if (written != 1) {
        DBG_PRINT_ERROR("Failed to write application descriptor to BIN file\n");
        return false;
    }
    DBG_PRINTF4(ANSI_COLOR_BLUE "Writing application %u:'%s', epk='%s', xcp_init_mode=0x%02X\n" ANSI_COLOR_RESET, app_id, project_name, epk, desc.xcp_init_mode);
    return true;
}

#endif // SHM_MODE

//--------------------------------------------------------------------------------------------------------------------------------

/// Create the binary persistence file.
/// This function writes the current state of the XCP events and calibration segments to a binary file.
/// It creates a file with the specified filename and writes the header, events, and calibration segments.
/// It is called from the A2L generator when finalizing the A2L file, so it belongs to and exactly matches the state of the A2L file
/// @param filename The name of the file to write.
/// @param page The page of the calibration segments to write, either default or working page, see XCP_CALPAGE_XXX
/// @return
/// Returns true if the file was successfully written, false otherwise.
bool XcpBinWrite(const char *epk) {

    if (!XcpIsActivated()) {
        return false;
    }
    const char *filename = XcpBinGetFilename();
    DBG_PRINTF3("Writing persistence data to file '%s' with EPK '%s'\n", filename, epk);

    // Open file for writing
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        DBG_PRINTF_ERROR("Failed to open file %s for writing\n", filename);
        return false;
    }

#ifdef OPTION_SHM_MODE // write application count to BIN file header
    uint8_t app_count = XcpShmGetAppCount();
#else
    uint8_t app_count = 0;
#endif
    uint16_t event_count = XcpGetEventCount();
    uint16_t calseg_count = XcpGetCalSegCount();

    if (!writeHeader(file, epk, event_count, calseg_count, app_count)) {
        fclose(file);
        return false;
    }

    // Write events
    for (tXcpEventId i = 0; i < event_count; i++) {
        const tXcpEvent *event = XcpGetEvent(i);
        if (!writeEvent(file, i, event)) {
            fclose(file);
            return false;
        }
    }

    // Write calibration segments descriptors and data
    // Iterate cal_seg_list cal_seg_list
    for (tXcpCalSegIndex i = 0; i < calseg_count; i++) {
        const tXcpCalSeg *seg = XcpGetCalSeg(i);
        assert(seg != NULL);
        if (!writeCalseg(file, i, seg, XCP_CALPAGE_DEFAULT_PAGE)) {
            fclose(file);
            return false;
        }
    }

    // In SHM mode, write application list
#ifdef OPTION_SHM_MODE // write application descriptors to BIN file

    // Write application descriptors
    for (uint8_t i = 0; i < app_count; i++) {
        if (!writeApp(file, i, XcpShmGetAppProjectName(i), XcpShmGetAppEpk(i), XcpShmGetInitMode(i))) {
            fclose(file);
            return false;
        }
    }
#endif // SHM_MODE

    fclose(file);

    DBG_PRINTF3(ANSI_COLOR_GREEN "Persistence data written to file '%s'\n" ANSI_COLOR_RESET, gXcpBinFilename);
#ifdef OPTION_SHM_MODE // debug print application list
    if (DBG_LEVEL >= 4) {
        XcpShmDebugPrint();
    }
#endif

    return true;
}

//--------------------------------------------------------------------------------------------------------------------------------

/// Freeze the working page of a calibration segment.
/// This function writes the working page of the specified calibration segment to the binary persistence file.
/// @param calseg Calibration segment index
/// @return
/// Returns true if the operation was successful.
bool XcpBinFreezeCalSeg(tXcpCalSegIndex calseg) {
    if (calseg >= XcpGetCalSegCount()) {
        DBG_PRINTF_ERROR("Invalid calibration segment index %u\n", calseg);
        return false;
    }
    const tXcpCalSeg *seg = XcpGetCalSeg(calseg);
    if (seg == NULL) {
        DBG_PRINTF_ERROR("Calibration segment '%u' not found!\n", calseg);
        return false;
    }

    const char *filename = XcpBinGetFilename();
    FILE *file = fopen(filename, "r+b");
    if (file == NULL) {
        DBG_PRINTF_ERROR("Failed to open file '%s'\n", filename);
        return false;
    }

    // Set position to start of calseg data and write the active page data
    assert(seg->h.file_pos > 0); // Ensure the file position is set
    size_t n = 0;
    if (0 == fseek(file, seg->h.file_pos, SEEK_SET)) {
        const uint8_t *ecu_page = XcpLockCalSeg(calseg);
#ifdef OPTION_ENABLE_DBG_PRINTS
        DBG_PRINTF4("Freezing calibration segment %u, size=%u active page data to file '%s'+%u\n", calseg, seg->h.size, filename, seg->h.file_pos);
        if (DBG_LEVEL >= 4)
            printCalsegPage(ecu_page, seg->h.size);
#endif
        n = fwrite(ecu_page, seg->h.size, 1, file);
        XcpUnlockCalSeg(calseg);
    }
    fclose(file);
    if (n != 1) {
        DBG_PRINTF_ERROR("Failed to write calibration segment %u, size=%u active page data to file '%s'+%u\n", calseg, seg->h.size, gXcpBinFilename, seg->h.file_pos);
        return false;
    } else {
        return true;
    }
}

//--------------------------------------------------------------------------------------------------------------------------------

// Load the binary persistence file.
// @param filename The pathname of the file (with extension) to read
// @param epk The expected EPK string for verification or NULL to skip EPK verification (e.g. in case the EPK is not yet known)
// @return
// If the file is successfully loaded, it returns true.
// Returns false, if the file does not exist, has an invalid format, the EPK does not match or any other reason
static bool load(const char *filename, const char *epk) {

    uint32_t error_count = 0;

    assert(filename != NULL);
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        DBG_PRINTF3(ANSI_COLOR_YELLOW "Binary file '%s' not found, starting with default pages\n" ANSI_COLOR_RESET, filename);
        return false;
    }

    // Read and verify header
    size_t read = fread(&gBinHeader, sizeof(tHeader), 1, file);
    if (read != 1 || strncmp(gBinHeader.signature, BIN_SIGNATURE, sizeof(gBinHeader.signature)) != 0) {
        DBG_PRINTF_ERROR("Invalid file format or signature in '%s'\n", filename);
        fclose(file);
        return false;
    }
    if (gBinHeader.version != BIN_VERSION) {
        DBG_PRINTF_ERROR("Unsupported BIN file version 0x%04X in '%s'\n", gBinHeader.version, filename);
        fclose(file);
        return false;
    }

    // Check EPK match
    if (epk != NULL && strncmp(gBinHeader.Epk, epk, XCP_EPK_MAX_LENGTH) != 0) {
        DBG_PRINTF_WARNING("Persistence file '%s' not loaded, EPK mismatch: file EPK '%s', current EPK '%s'\n", filename, gBinHeader.Epk, epk);
        fclose(file);
        return false; // EPK mismatch
    }

    DBG_PRINTF3("Loading '%s'\n", filename);

    // Load events
    // Event list must be empty at this point
    if (XcpGetEventCount() != 0) {
        DBG_PRINT_ERROR("Event list not empty prior to loading persistence file\n");
        fclose(file);
        return false;
    }
    for (uint16_t i = 0; i < gBinHeader.event_count; i++) {
        tEventDescriptor desc;
        tXcpEventId event_id;

        // Read event descriptor
        read = fread(&desc, sizeof(tEventDescriptor), 1, file);
        if (read != 1) {
            DBG_PRINT_ERROR("Failed to read event descriptor from BIN file\n");
            fclose(file);
            assert(0 && "Corrupt file");
            return false;
        }

        // Create the event
        // As it is created in the original order, the event ID must match
        // XcpCreateIndexedEvent does not check for duplicate event names, the application id will be set below
        // Not thread safe, but called in XcpInit() before XCP is activated, so no concurrent access to event list here, and no need to lock the event list mutex
        // @@@@ TODO: Find a process safe solution for SHM mode
        event_id = XcpCreateIndexedEvent(desc.name, desc.index, desc.cycle_time_ns, desc.priority);
        if (event_id == XCP_UNDEFINED_EVENT_ID || event_id != desc.id) { // Should not happen
            DBG_PRINTF_ERROR("Failed to create event '%s' from persistence file\n", desc.name);
            error_count++;
        }

        // In SHM mode, set the app_id of the event owner
#ifdef OPTION_SHM_MODE // load app-id in event descriptor
        // @@@@ TODO: Cast away const, improve design to avoid this
        tXcpEvent *event = (tXcpEvent *)XcpGetEvent(event_id);
        event->app_id = desc.app_id;
#endif // SHM_MODE
    }

    // Load calibration segments
    // Calibration segment list must be empty at this point
    if (XcpGetCalSegCount() != 0) {
        DBG_PRINT_ERROR("Calibration segment list not empty prior to loading persistence file\n");
        fclose(file);
        assert(0 && "Sequence problem");
        return false;
    }
    for (uint16_t i = 0; i < gBinHeader.calseg_count; i++) {

        tCalSegDescriptor desc;

        uint32_t file_pos = (uint32_t)ftell(file);
        read = fread(&desc, sizeof(tCalSegDescriptor), 1, file);
        if (read != 1) {
            DBG_PRINT_ERROR("Failed to read calibration segment descriptor from BIN file\n");
            fclose(file);
            assert(0 && "Corrupt file");
            return false;
        }

        tXcpCalSegIndex calseg_index = XcpCreateCalSegPreloaded(desc.name, desc.app_id, desc.size, desc.index, desc.number, file, file_pos);
        if (calseg_index == XCP_UNDEFINED_CALSEG) {
            DBG_PRINTF_ERROR("Failed to create calibration segment %u:'%s'\n", i, desc.name);
            error_count++;
        }
#ifdef OPTION_ENABLE_DBG_PRINTS
        else {
            if (DBG_LEVEL >= 5) {
                const uint8_t *page = (uint8_t *)XcpLockCalSeg(calseg_index);
                printCalsegPage(page, desc.size);
                XcpUnlockCalSeg(calseg_index);
            }
        }
#endif
    }

// In SHM mode, load application list and pre register applications (assuming the application list is empty at this point)
#ifdef OPTION_SHM_MODE // load all application descriptors and pre register these applications
    for (uint8_t i = 0; i < gBinHeader.app_count; i++) {
        tAppDescriptor desc;
        read = fread(&desc, sizeof(tAppDescriptor), 1, file);
        if (read != 1) {
            DBG_PRINT_ERROR("Failed to read application descriptor from BIN file\n");
            fclose(file);
            assert(0 && "Corrupt file");
            return false;
        }

        // Pre register application by name and epk
        // Don't know who is leader or server yet
        DBG_PRINTF4(ANSI_COLOR_BLUE "Pre registered application %u:'%s', epk='%s'\n" ANSI_COLOR_RESET, desc.app_id, desc.project_name, desc.epk);
        int16_t app_id = XcpShmRegisterApp(desc.project_name, desc.epk, 0, desc.xcp_init_mode, false, false);
        if (app_id < 0 || (uint8_t)app_id != desc.app_id) { // Just created in order, assuming before empty application list, the app_id in the file must match the allocated app_id
            DBG_PRINTF_ERROR("Could not register application %u:'%s'\n", desc.app_id, desc.project_name);
            assert(0 && "Failed to register application"); // Should never happen
            error_count++;
        }

        // If the A2L file already exists, set the A2L finalized flag, so the main file will be generated for it, even if the application was not started before tool connect
        char a2l_filename[XCP_A2L_FILENAME_MAX_LENGTH + 1];
        SNPRINTF(a2l_filename, sizeof(a2l_filename), "%s_%s.a2l", desc.project_name, desc.epk);
        if (fexists(a2l_filename)) {
            XcpShmSetA2lFinalized(desc.app_id, a2l_filename);
            DBG_PRINTF4(ANSI_COLOR_BLUE "Application %u:'%s' A2L file'%s' exists, set A2L finalized flag\n" ANSI_COLOR_RESET, desc.app_id, desc.project_name, a2l_filename);
        }
    }
#endif // SHM_MODE

    fclose(file);
    if (error_count > 0) {
        return false;
    }
    return true;
}

// Load the binary persistence file.
// This function reads the binary file containing calibration segment descriptors with data and event descriptors
// It pre-registers the events and calibration segments
// In SHM mode, it also loads the applications and pre-registers them
// This must be done early, before any event, segment or application was registered
// @return
// If the file is successfully loaded, it returns true.
// Returns false, if the file does not exist, has an invalid format
bool XcpBinLoad(void) {
    if (!XcpIsActivated()) {
        return false;
    }
    const char *filename = XcpBinGetFilename();
    if (load(filename, NULL /* no epk check */)) {
        DBG_PRINTF3(ANSI_COLOR_GREEN "Loaded binary persistence file %s\n" ANSI_COLOR_RESET, filename);
        return true;
    }
    return false;
}

void XcpBinDelete(void) {
    const char *filename = XcpBinGetFilename();
    if (remove(filename) == 0) {
        DBG_PRINTF3("Deleted persistence file '%s'\n", filename);
    }
}

#endif // OPTION_ENABLE_PERSISTENCE
