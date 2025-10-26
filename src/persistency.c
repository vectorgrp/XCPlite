/*----------------------------------------------------------------------------
| File:
|   persistency.c
|
| Description:
|   Read and write binary file for calibration segment persistency
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "persistency.h"

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <stdarg.h>   // for va_
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t
#include <stdio.h>    // for fclose, fopen, fread, fseek, ftell
#include <stdlib.h>   // for free, malloc
#include <string.h>   // for strlen, strncpy

#include "dbg_print.h" // for DBG_PRINTF3, DBG_PRINT4, DBG_PRINTF4, DBG...
#include "main_cfg.h"  // for OPTION_xxx
#include "platform.h"  // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex
#include "xcp.h"       // for CRC_XXX
#include "xcpLite.h"   // for tXcpDaqLists, XcpXxx, ApplXcpXxx, ...
#include "xcp_cfg.h"   // for XCP_xxx
#include "xcptl_cfg.h" // for XCPTL_xxx

#ifdef OPTION_CAL_PERSISTENCE

#if !defined(XCP_ENABLE_DAQ_EVENT_LIST) || !defined(XCP_ENABLE_CALSEG_LIST)
#error "XCP_ENABLE_DAQ_EVENT_LIST and XCP_ENABLE_CALSEG_LIST must be enabled for calibration segment persistency"
#endif

#define BIN_SIGNATURE "XCPLITE__BINARY"
#define BIN_VERSION 0x0100
#pragma pack(push, 1)

typedef struct {
    char signature[16];               // File signature "XCPLITE__BINARY"
    uint16_t version;                 // File version, currently 0x0100
    char Epk[XCP_EPK_MAX_LENGTH + 1]; // EPK string, 0 terminated, 32 bytes
    uint16_t event_count;             // Number of events tEventDescriptor
    uint16_t calseg_count;
    uint32_t res;
} tHeader;

typedef struct {
    char name[XCP_MAX_EVENT_NAME + 1]; // event name, 0 terminated, 16 bytes
    uint16_t id;
    uint16_t index;
    uint32_t cycleTimeNs; // cycle time in ns
    uint8_t priority;     // priority 0 = queued, 1 = pushing, 2 = realtime
    uint8_t res[3];       // reserved, 3 bytes
} tEventDescriptor;

typedef struct {
    char name[XCP_MAX_CALSEG_NAME + 1]; // calibration segment name, 0 terminated, 16 bytes
    uint16_t size;                      // size of the calibration segment in bytes, multiple of 4
    uint16_t index;                     // index of the calibration segment in the list, 0..<XCP_MAX_CALSEG_COUNT
    uint8_t res[4];                     // reserved, 4 bytes
} tCalSegDescriptor;

#pragma pack(pop)

extern tXcpData gXcp;

static tHeader gA2lHeader;

//--------------------------------------------------------------------------------------------------------------------------------

#define XCP_BIN_FILENAME_MAX_LENGTH 255 // Maximum length of BIN filename with extension
static char gXcpBinFilename[XCP_BIN_FILENAME_MAX_LENGTH + 1] = "";

// Build BIN filename from project name and EPK
static void buildBinFilename(void) {
    if (strlen(gXcpBinFilename) > 0)
        return; // Already built
    const char *project_name = XcpGetProjectName();
    assert(project_name != NULL);
    const char *epk = XcpGetEpk();
    assert(epk != NULL);
    SNPRINTF(gXcpBinFilename, XCP_BIN_FILENAME_MAX_LENGTH, "%s_%s.bin", project_name, epk);
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
static bool writeHeader(FILE *file, const char *epk, uint16_t event_count, uint16_t calseg_count) {

    strncpy(gA2lHeader.signature, BIN_SIGNATURE, sizeof(gA2lHeader.signature) - 1);
    gA2lHeader.signature[sizeof(gA2lHeader.signature) - 1] = '\0'; // Ensure null termination
    gA2lHeader.version = BIN_VERSION;
    strncpy(gA2lHeader.Epk, epk, XCP_EPK_MAX_LENGTH);
    gA2lHeader.Epk[XCP_EPK_MAX_LENGTH] = '\0'; // Ensure null termination
    gA2lHeader.event_count = event_count;
    gA2lHeader.calseg_count = calseg_count;
    gA2lHeader.res = 0;
    size_t written = fwrite(&gA2lHeader, sizeof(tHeader), 1, file);
    if (written != 1) {
        DBG_PRINTF3("Failed to write header to file: %s\n", strerror(errno));
        return false;
    }
    return true;
}

// Write an event descriptor to the BIN file
static bool writeEvent(FILE *file, tXcpEventId event_id, const tXcpEvent *event) {
    tEventDescriptor desc;
    strncpy(desc.name, event->name, XCP_MAX_EVENT_NAME);
    desc.name[XCP_MAX_EVENT_NAME] = '\0'; // Ensure null termination
    desc.cycleTimeNs = event->cycleTimeNs;
    desc.priority = event->priority;
    desc.id = event_id;
    desc.index = XcpGetEventIndex(event_id);
    desc.res[0] = 0xEE;
    desc.res[1] = 0xEE;
    desc.res[2] = 0xEE;
    size_t written = fwrite(&desc, sizeof(tEventDescriptor), 1, file);
    if (written != 1) {
        DBG_PRINTF3("Failed to write event descriptor to file: %s\n", strerror(errno));
        return false;
    }

    return true;
}

// Write a calibration segment descriptor and page data to the BIN file
static bool writeCalseg(FILE *file, tXcpCalSegIndex calseg, tXcpCalSeg *seg, uint8_t page) {
    tCalSegDescriptor desc;
    strncpy(desc.name, seg->name, XCP_MAX_CALSEG_NAME);
    desc.name[XCP_MAX_CALSEG_NAME] = '\0'; // Ensure null termination
    desc.size = seg->size;
    desc.index = calseg;
    *(uint32_t *)&desc.res[0] = 0xDDDDDDDD;
    size_t written = fwrite(&desc, sizeof(tCalSegDescriptor), 1, file);
    if (written != 1) {
        DBG_PRINTF3("Failed to write calibration segment descriptor to file: %s\n", strerror(errno));
        return false;
    }
    seg->file_pos = (uint32_t)ftell(file); // Save the position of the segment page data in the file
#ifdef OPTION_ENABLE_DBG_PRINTS
    DBG_PRINTF4("Writing calibration segment %u, size=%u %s page data:\n", calseg, seg->size, page == XCP_CALPAGE_DEFAULT_PAGE ? "default" : "working");
    if (DBG_LEVEL >= 4)
        printCalsegPage(page == XCP_CALPAGE_DEFAULT_PAGE ? seg->default_page : seg->ecu_page, seg->size);
#endif
    // This is safe, because XCP is not connected
    written = fwrite(page == XCP_CALPAGE_DEFAULT_PAGE ? seg->default_page : seg->ecu_page, seg->size, 1, file);
    if (written != 1) {
        DBG_PRINTF3("Failed to write calibration segment data to file: %s\n", strerror(errno));
        return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------------------------------------

/// Write the binary persistency file.
/// This function writes the current state of the XCP events and calibration segments to a binary file.
/// It creates a file with the specified filename and writes the header, events, and calibration segments.
/// The tool must not be connected at that time
/// @param filename The name of the file to write.
/// @return
/// Returns true if the file was successfully written, false otherwise.
bool XcpBinWrite(uint8_t page) {

    buildBinFilename();

    if (XcpIsConnected() && page == XCP_CALPAGE_WORKING_PAGE) {
        DBG_PRINT_ERROR("Cannot write persistency file while XCP is connected\n");
        return false;
    }

    // Open file for writing
    FILE *file = fopen(gXcpBinFilename, "wb");
    if (file == NULL) {
        DBG_PRINTF3("Failed to open file %s for writing: %s\n", gXcpBinFilename, strerror(errno));
        return false;
    }
    if (!writeHeader(file, XcpGetEpk(), gXcp.EventList.count, gXcp.CalSegList.count)) {
        fclose(file);
        return false;
    }

    // Write events
    for (tXcpEventId i = 0; i < gXcp.EventList.count; i++) {
        const tXcpEvent *event = XcpGetEvent(i);
        if (!writeEvent(file, i, event)) {
            fclose(file);
            return false;
        }
    }

    // Write calibration segments descriptors and data
    for (tXcpCalSegIndex i = 0; i < gXcp.CalSegList.count; i++) {
        tXcpCalSeg *seg = XcpGetCalSeg(i);
        assert(seg != NULL);
        if (!writeCalseg(file, i, seg, page)) {
            fclose(file);
            return false;
        }
    }

    fclose(file);

    DBG_PRINTF3("Persistency data written to file '%s'\n", gXcpBinFilename);
    return true;
}

//--------------------------------------------------------------------------------------------------------------------------------

/// Freeze the active page of a calibration segment to the binary persistency file.
/// This function writes the active page of the specified calibration segment to the binary persistency file.
/// @param calseg Calibration segment index
/// @return
/// Returns true if the operation was successful.
bool XcpBinFreezeCalSeg(tXcpCalSegIndex calseg) {
    assert(calseg < gXcp.CalSegList.count);
    tXcpCalSeg *seg = XcpGetCalSeg(calseg);
    if (seg == NULL) {
        DBG_PRINTF_ERROR("Calibration segment '%u' not found!\n", calseg);
        return false;
    }

    buildBinFilename();
    FILE *file = fopen(gXcpBinFilename, "r+b");
    if (file == NULL) {
        // If the file does not exist yet, create a new initial one with default page data
        XcpBinWrite(XCP_CALPAGE_DEFAULT_PAGE);
        file = fopen(gXcpBinFilename, "r+b");
    }
    if (file == NULL) {
        DBG_PRINTF_ERROR("Failed to open file '%s' for read/write: %s\n", gXcpBinFilename, strerror(errno));
        return false;
    }

    // Set position to start of calseg data and write the active page data
    assert(seg->file_pos > 0); // Ensure the file position is set
    size_t n = 0;
    if (0 == fseek(file, seg->file_pos, SEEK_SET)) {
        const uint8_t *ecu_page = XcpLockCalSeg(calseg);
#ifdef OPTION_ENABLE_DBG_PRINTS
        DBG_PRINTF4("Freezing calibration segment %u, size=%u active page data to file '%s'+%u\n", calseg, seg->size, gXcpBinFilename, seg->file_pos);
        if (DBG_LEVEL >= 4)
            printCalsegPage(ecu_page, seg->size);
#endif
        n = fwrite(ecu_page, seg->size, 1, file);
        XcpUnlockCalSeg(calseg);
    }
    fclose(file);
    if (n != 1) {
        DBG_PRINTF_ERROR("Failed to write calibration segment %u, size=%u active page data to file '%s'+%u\n", calseg, seg->size, gXcpBinFilename, seg->file_pos);
        return false;
    } else {
        return true;
    }
}

//--------------------------------------------------------------------------------------------------------------------------------

// Load the binary persistency file.
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

    size_t read = fread(&gA2lHeader, sizeof(tHeader), 1, file);
    if (read != 1 || strncmp(gA2lHeader.signature, BIN_SIGNATURE, sizeof(gA2lHeader.signature)) != 0) {
        DBG_PRINTF_ERROR("Invalid file format or signature in '%s'\n", filename);
        fclose(file);
        return false;
    }

    // Check EPK match
    if (strncmp(gA2lHeader.Epk, epk, XCP_EPK_MAX_LENGTH) != 0) {
        DBG_PRINTF_WARNING("Persistence file '%s' not loaded, EPK mismatch: file EPK '%s', current EPK '%s'\n", filename, gA2lHeader.Epk, epk);
        fclose(file);
        return false; // EPK mismatch
    }

    DBG_PRINTF3("Loading '%s', EPK '%s'\n", filename, epk);

    // Load events
    // Event list must be empty at this point
    if (gXcp.EventList.count != 0) {
        DBG_PRINT_ERROR("Event list not empty prior to loading persistency file\n");
        fclose(file);
        return false;
    }
    for (uint16_t i = 0; i < gA2lHeader.event_count; i++) {
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
        event_id = XcpCreateIndexedEvent(desc.name, desc.index, desc.cycleTimeNs, desc.priority);
        if (event_id == XCP_UNDEFINED_EVENT_ID || event_id != desc.id) { // Should not happen
            DBG_PRINTF_ERROR("Failed to create event '%s' from persistency file\n", desc.name);
            fclose(file);
            return false;
        }
    }

    // Load calibration segments
    // Calibration segment list must be empty at this point
    if (gXcp.CalSegList.count != 0) {
        DBG_PRINT_ERROR("Calibration segment list not empty prior to loading persistency file\n");
        fclose(file);
        return false;
    }
    for (uint16_t i = 0; i < gA2lHeader.calseg_count; i++) {
        tXcpCalSegIndex calseg;

        tCalSegDescriptor desc;
        read = fread(&desc, sizeof(tCalSegDescriptor), 1, file);
        if (read != 1) {
            DBG_PRINTF_ERROR("Failed to read calibration segment descriptor from file: %s\n", strerror(errno));
            fclose(file);
            return false;
        }

        // Read calibration segment page data
        // Allocate memory for persisted page from heap
        void *page = malloc(desc.size);
        read = fread(page, desc.size, 1, file);
        if (read != 1) {
            DBG_PRINTF_ERROR("Failed to read calibration segment data from file: %s\n", strerror(errno));
            free(page);
            fclose(file);
            return false;
        }
#ifdef OPTION_ENABLE_DBG_PRINTS
        DBG_PRINTF4("Reading calibration segment %u, size=%u:\n", i, desc.size);
        if (DBG_LEVEL >= 4)
            printCalsegPage(page, desc.size);
#endif

        // The persisted data will become the preliminary reference page
        // Providing a heap allocated default page may not work for absolute segment addressing mode in reference page persistency mode
        // In working page persistency mode, the default page will be moved to working page in the later XcpCreateCalSeg called by the user, otherwise fail
        calseg = XcpCreateCalSeg(desc.name, page, desc.size);
        if (calseg != desc.index) {
            DBG_PRINT_ERROR("Failed to create calibration segment\n");
            free(page);
            fclose(file);
            return false;
        }

        // Mark the segment as pre initialized
        tXcpCalSeg *seg = XcpGetCalSeg(calseg);
        seg->mode = PAG_PROPERTY_PRELOAD;
        seg->file_pos = (uint32_t)ftell(file) - desc.size; // Save the position of the segment page data in the file
    }

    return true;
}

// Load the binary persistency file.
// This function reads the binary file containing calibration segment descriptors and data and event descriptors
// It verifies the file signature and EPK, and creates the events and calibration segments
// This must be done early, before any event or segments are created
// @return
// If the file is successfully loaded, it returns true.
// Returns false, if the file does not exist, has an invalid format, the EPK does not match or any other reason

bool XcpBinLoad(void) {

    buildBinFilename();
    const char *epk = XcpGetEpk();
    assert(epk != NULL);
    if (load(gXcpBinFilename, epk)) {
        DBG_PRINTF3("Loaded binary file %s\n", gXcpBinFilename);
        return true;
    }
    return false;
}

#endif // OPTION_CAL_PERSISTENCE
