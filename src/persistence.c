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
#include "xcpLite.h"    // for tXcpDaqLists, XcpXxx, ApplXcpXxx, ...
#include "xcp_cfg.h"    // for XCP_xxx
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcptl_cfg.h"  // for XCPTL_xxx

#ifdef OPTION_CAL_PERSISTENCE

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
    uint8_t reserved[128 - 1];                                                           // Reserved for future use
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

// Build BIN filename from project name and EPK
static const char *XcpBinGetFilename(void) {
    const char *basename;
#ifdef OPTION_SHM_MODE
    if (XcpShmIsActive()) {
        basename = XcpShmGetEcuProjectName();
    } else
#endif
    {
        basename = XcpGetProjectName();
    }
    assert(basename != NULL);
    const char *epk = XcpGetEpk();
    assert(epk != NULL);
    SNPRINTF(gXcpBinFilename, XCP_BIN_FILENAME_MAX_LENGTH, "%s_%s.bin", basename, epk);
    return gXcpBinFilename;
}

// Print the content of a calibration segment page for debugging
#ifdef OPTION_ENABLE_DBG_PRINTS
static void printCalsegPage(const uint8_t *page, uint16_t size) {
    for (uint16_t i = 0; i < size; i++) {
        printf("%02X ", page[i]);
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    if (size % 16 != 0) {
        printf("\n");
    }
}
#endif

// Write the BIN file header
static bool writeHeader(FILE *file, const char *epk, uint16_t event_count, uint16_t calseg_count, uint8_t app_count) {

    memset(&gBinHeader, 0, sizeof(tHeader));
    strncpy(gBinHeader.signature, BIN_SIGNATURE, sizeof(gBinHeader.signature) - 1);
    gBinHeader.signature[sizeof(gBinHeader.signature) - 1] = '\0'; // Ensure null termination
    gBinHeader.version = BIN_VERSION;
    strncpy(gBinHeader.Epk, epk, XCP_EPK_MAX_LENGTH);
    gBinHeader.Epk[XCP_EPK_MAX_LENGTH] = '\0'; // Ensure null termination
    gBinHeader.event_count = event_count;
    gBinHeader.calseg_count = calseg_count;
    gBinHeader.app_count = app_count;
    size_t written = fwrite(&gBinHeader, sizeof(tHeader), 1, file);
    if (written != 1) {
        DBG_PRINTF3("Failed to write header to file: %s\n", strerror(errno));
        return false;
    }
    return true;
}

// Write an event descriptor to the BIN file
static bool writeEvent(FILE *file, tXcpEventId event_id, const tXcpEvent *event) {
    tEventDescriptor desc;
    memset(&desc, 0, sizeof(tEventDescriptor));
    strncpy(desc.name, event->name, XCP_MAX_EVENT_NAME);
    desc.name[XCP_MAX_EVENT_NAME] = '\0'; // Ensure null termination
    // In SHM mode, save the app_id of the event owner
#ifdef OPTION_SHM_MODE
    desc.app_id = event->app_id;
#endif
    desc.cycle_time_ns = event->cycle_time_ns;
    desc.priority = event->flags & XCP_DAQ_EVENT_FLAG_PRIORITY ? 0xFF : 0x00;
    desc.id = event_id;
    desc.index = XcpGetEventIndex(event_id);
    size_t written = fwrite(&desc, sizeof(tEventDescriptor), 1, file);
    if (written != 1) {
        DBG_PRINTF3("Failed to write event descriptor to file: %s\n", strerror(errno));
        return false;
    }

    return true;
}

// Write a calibration segment descriptor and page data to the BIN file
static bool writeCalseg(FILE *file, tXcpCalSegIndex calseg, const tXcpCalSeg *seg, uint8_t page) {
    tCalSegDescriptor desc;
    memset(&desc, 0, sizeof(tCalSegDescriptor));
    strncpy(desc.name, seg->h.name, XCP_MAX_CALSEG_NAME);
    desc.name[XCP_MAX_CALSEG_NAME] = '\0'; // Ensure null termination
    desc.size = seg->h.size;
    desc.addr = XcpGetCalSegBaseAddress(calseg);
    desc.index = calseg;
    desc.number = seg->h.calseg_number;
    // In SHM mode, save the app_id of the calibration segment owner
#ifdef OPTION_SHM_MODE
    desc.app_id = seg->h.app_id;
#endif
    size_t written = fwrite(&desc, sizeof(tCalSegDescriptor), 1, file);
    if (written != 1) {
        DBG_PRINTF3("Failed to write calibration segment descriptor to file: %s\n", strerror(errno));
        return false;
    }
    ((tXcpCalSeg *)seg)->h.file_pos = (uint32_t)ftell(file); // Save the position of the segment page data in the file // @@@@ TODO cast away const, improve design to avoid this
#ifdef OPTION_ENABLE_DBG_PRINTS
    DBG_PRINTF4("Writing calibration segment %u, size=%u %s page data:\n", calseg, seg->h.size, page == XCP_CALPAGE_DEFAULT_PAGE ? "default" : "working");
    if (DBG_LEVEL >= 4)
        printCalsegPage(page == XCP_CALPAGE_DEFAULT_PAGE ? CalSegDefaultPage(seg) : CalSegEcuPage(seg), seg->h.size);
#endif
    // This is safe, because XCP is not connected
    written = fwrite(page == XCP_CALPAGE_DEFAULT_PAGE ? CalSegDefaultPage(seg) : CalSegEcuPage(seg), seg->h.size, 1, file);
    if (written != 1) {
        DBG_PRINTF3("Failed to write calibration segment data to file: %s\n", strerror(errno));
        return false;
    }
    return true;
}

// In SHM mode, the .BIN file contains an application list
#ifdef OPTION_SHM_MODE

// Write a application descriptor
static bool writeApp(FILE *file, uint8_t app_id, const char *project_name, const char *epk) {
    tAppDescriptor desc;
    memset(&desc, 0, sizeof(tAppDescriptor));
    strncpy(desc.project_name, project_name, XCP_PROJECT_NAME_MAX_LENGTH);
    desc.project_name[XCP_PROJECT_NAME_MAX_LENGTH] = '\0'; // Ensure null termination
    strncpy(desc.epk, epk, XCP_EPK_MAX_LENGTH);
    desc.epk[XCP_EPK_MAX_LENGTH] = '\0'; // Ensure null termination
    size_t written = fwrite(&desc, sizeof(tAppDescriptor), 1, file);
    if (written != 1) {
        DBG_PRINTF3("Failed to write application descriptor to file: %s\n", strerror(errno));
        return false;
    }

    DBG_PRINTF4("Writing application %u, name=%s, epk=%s\n", app_id, project_name, epk);

    return true;
}

#endif

//--------------------------------------------------------------------------------------------------------------------------------

/// Write the binary persistence file.
/// This function writes the current state of the XCP events and calibration segments to a binary file.
/// It creates a file with the specified filename and writes the header, events, and calibration segments.
/// The tool must not be connected at that time
/// @param filename The name of the file to write.
/// @return
/// Returns true if the file was successfully written, false otherwise.
bool XcpBinWrite(uint8_t page) {

    if (!XcpIsActivated()) {
        return false;
    }

    const char *filename = XcpBinGetFilename();

    if (XcpIsConnected() && page == XCP_CALPAGE_WORKING_PAGE) {
        DBG_PRINT_ERROR("Cannot write persistence file while XCP is connected\n");
        return false;
    }

    // Open file for writing
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        DBG_PRINTF3("Failed to open file %s for writing: %s\n", filename, strerror(errno));
        return false;
    }

    uint8_t app_count = XcpShmGetAppCount();
    uint16_t event_count = XcpGetEventCount();
    uint16_t calseg_count = XcpGetCalSegCount();
    if (!writeHeader(file, XcpGetEpk(), event_count, calseg_count, app_count)) {
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
        if (!writeCalseg(file, i, seg, page)) {
            fclose(file);
            return false;
        }
    }

    // In SHM mode, write application list
#ifdef OPTION_SHM_MODE
    // Write application descriptors
    for (uint8_t i = 0; i < app_count; i++) {
        if (!writeApp(file, i, XcpShmGetAppProjectName(i), XcpShmGetAppEpk(i))) {
            fclose(file);
            return false;
        }
    }
#endif

    fclose(file);

    DBG_PRINTF3("Persistence data written to file '%s'\n", gXcpBinFilename);
    return true;
}

//--------------------------------------------------------------------------------------------------------------------------------

/// Freeze the active page of a calibration segment to the binary persistence file.
/// This function writes the active page of the specified calibration segment to the binary persistence file.
/// @param calseg Calibration segment index
/// @return
/// Returns true if the operation was successful.
bool XcpBinFreezeCalSeg(tXcpCalSegIndex calseg) {
    assert(calseg < XcpGetCalSegCount());
    const tXcpCalSeg *seg = XcpGetCalSeg(calseg);
    if (seg == NULL) {
        DBG_PRINTF_ERROR("Calibration segment '%u' not found!\n", calseg);
        return false;
    }

    const char *filename = XcpBinGetFilename();
    FILE *file = fopen(filename, "r+b");
    if (file == NULL) {
        // If the file does not exist yet, create a new initial one with default page data
        XcpBinWrite(XCP_CALPAGE_DEFAULT_PAGE);
        file = fopen(filename, "r+b");
    }
    if (file == NULL) {
        DBG_PRINTF_ERROR("Failed to open file '%s' for read/write: %s\n", filename, strerror(errno));
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
// @param epk The expected EPK string for verification
// @return
// If the file is successfully loaded, it returns true.
// Returns false, if the file does not exist, has an invalid format, the EPK does not match or any other reason
static bool load(const char *filename, const char *epk) {
    assert(filename != NULL);
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        DBG_PRINTF3("File '%s' does not exist, starting with default calibration parameters\n", filename);
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
    if (strncmp(gBinHeader.Epk, epk, XCP_EPK_MAX_LENGTH) != 0) {
        DBG_PRINTF_WARNING("Persistence file '%s' not loaded, EPK mismatch: file EPK '%s', current EPK '%s'\n", filename, gBinHeader.Epk, epk);
        fclose(file);
        return false; // EPK mismatch
    }

    DBG_PRINTF3("Loading '%s', EPK '%s'\n", filename, epk);

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
            DBG_PRINTF_ERROR("Failed to read event descriptor from file: %s\n", strerror(errno));
            fclose(file);
            return false;
        }

        // Create the event
        // As it is created in the original order, the event ID must match
        // @@@@ TODO: Temporary solution, improve event creation, don't rely on order
        event_id = XcpCreateEvent(desc.name, desc.cycle_time_ns, desc.priority);
        if (event_id == XCP_UNDEFINED_EVENT_ID || event_id != desc.id) { // Should not happen
            DBG_PRINTF_ERROR("Failed to create event '%s' from persistence file\n", desc.name);
            fclose(file);
            return false;
        }
        // In SHM mode, set the app_id of the event owner
#ifdef OPTION_SHM_MODE
        tXcpEvent *event = (tXcpEvent *)XcpGetEvent(event_id); // @@@@ TODO cast away const, improve design to avoid this
        event->app_id = desc.app_id;
#endif
    }

    // Load calibration segments
    // Calibration segment list must be empty at this point
    if (XcpGetCalSegCount() != 0) {
        DBG_PRINT_ERROR("Calibration segment list not empty prior to loading persistence file\n");
        fclose(file);
        return false;
    }
    for (uint16_t i = 0; i < gBinHeader.calseg_count; i++) {

        tCalSegDescriptor desc;
        read = fread(&desc, sizeof(tCalSegDescriptor), 1, file);
        if (read != 1) {
            DBG_PRINTF_ERROR("Failed to read calibration segment descriptor from file: %s\n", strerror(errno));
            fclose(file);
            return false;
        }

        uint8_t *default_page = XcpCreateCalSegPreloaded(desc.name, desc.size, desc.index, desc.number, (uint32_t)ftell(file) - desc.size);
        if (default_page == NULL) {
            DBG_PRINT_ERROR("Failed to create calibration segment\n");
            fclose(file);
            return false;
        }

        read = fread(default_page, desc.size, 1, file);
        if (read != 1) {
            DBG_PRINTF_ERROR("Failed to read calibration segment data from file: %s\n", strerror(errno));
            fclose(file);
            return false;
        }
#ifdef OPTION_ENABLE_DBG_PRINTS
        DBG_PRINTF4("Reading calibration segment %u, size=%u:\n", i, desc.size);
        if (DBG_LEVEL >= 4)
            printCalsegPage(default_page, desc.size);
#endif
    }

    // In SHM mode, load application list
#ifdef OPTION_SHM_MODE
    for (uint8_t i = 0; i < gBinHeader.app_count; i++) {
        tAppDescriptor desc;
        read = fread(&desc, sizeof(tAppDescriptor), 1, file);
        if (read != 1) {
            DBG_PRINTF_ERROR("Failed to read application descriptor from file: %s\n", strerror(errno));
            fclose(file);
            return false;
        }

        // Register this process in the SHM application list
        // Returns allocated app_id (slot index) or SHM_MAX_APP_COUNT on error
        uint8_t app_id = XcpShmRegisterApp(desc.project_name, desc.epk, false, false);
        if (app_id != desc.app_id) { // The app_id in the file must match the allocated app_id
            DBG_PRINTF_ERROR("App ID mismatch: expected %u, got %u\n", desc.app_id, app_id);
            fclose(file);
            return false;
        }
        DBG_PRINTF4("Loaded application %u, name=%s, epk=%s\n", desc.app_id, desc.project_name, desc.epk);
    }
#endif

    fclose(file);
    return true;
}

// Load the binary persistence file.
// This function reads the binary file containing calibration segment descriptors and data and event descriptors
// It verifies the file signature and EPK, and creates the events and calibration segments
// This must be done early, before any event or segments are created
// @return
// If the file is successfully loaded, it returns true.
// Returns false, if the file does not exist, has an invalid format, the EPK does not match or any other reason
bool XcpBinLoad(void) {

    if (!XcpIsActivated()) {
        return false;
    }

    const char *filename = XcpBinGetFilename();
    const char *epk = XcpGetEpk();
    assert(epk != NULL);
    if (load(filename, epk)) {
        DBG_PRINTF3("Loaded binary file %s\n", filename);
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

#endif // OPTION_CAL_PERSISTENCE
