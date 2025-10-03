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

#define BIN_SIGNATURE "XCPlite__BINARY"
#define BIN_VERSION 0x0100
#pragma pack(push, 1)

typedef struct {
    char signature[16];               // File signature "XCPlite__BINARY"
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
static const char *gA2lBinFilename = NULL;

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

static bool writeCalseg(FILE *file, tXcpCalSegIndex calseg, tXcpCalSeg *seg) {
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
    written = fwrite(seg->ecu_page, seg->size, 1, file);
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
/// @param filename The name of the file to write.
/// @return
/// Returns true if the file was successfully written, false otherwise.
bool XcpBinWrite(const char *filename) {
    assert(filename != NULL);

    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        DBG_PRINTF3("Failed to open file for writing: %s\n", strerror(errno));
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
        if (!writeCalseg(file, i, seg)) {
            fclose(file);
            return false;
        }
    }

    fclose(file);
    gA2lBinFilename = filename;

    DBG_PRINTF3("Persistency data written to file '%s'\n", filename);
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

    FILE *file = fopen(gA2lBinFilename, "r+b");
    if (file == NULL) {
        DBG_PRINTF_ERROR("Failed to open file '%s' for read/write: %s\n", gA2lBinFilename, strerror(errno));
        return false;
    }

    // Set position to start of calseg data and write the active page data
    assert(seg->file_pos > 0); // Ensure the file position is set
    size_t n = 0;
    if (0 == fseek(file, seg->file_pos, SEEK_SET)) {
        printf("Writing calibration segment %u, size=%u active page data to file '%s'+%u\n", calseg, seg->size, gA2lBinFilename, seg->file_pos);
        const uint8_t *ecu_page = XcpLockCalSeg(calseg);
        n = fwrite(ecu_page, seg->size, 1, file);
        XcpUnlockCalSeg(calseg);
    }
    fclose(file);
    if (n != 1) {
        DBG_PRINTF_ERROR("Failed to write calibration segment %u, size=%u active page data to file '%s'+%u\n", calseg, seg->size, gA2lBinFilename, seg->file_pos);
        return false;
    } else {
        return true;
    }
}

//--------------------------------------------------------------------------------------------------------------------------------

/// Load the binary persistency file.
/// This function reads the binary file containing calibration segment descriptors and data and event descriptors
/// It verifies the file signature and EPK, and creates the events and calibration segments
/// @return
/// If the file is successfully loaded, it returns true.
/// If the file does not exist, has an invalid format, or the EPK does not match, it returns false.
bool XcpBinLoad(const char *filename, const char *epk) {
    assert(filename != NULL);
    gA2lBinFilename = NULL;

    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        DBG_PRINTF3("File '%s' does not exist\n", filename);
        return false;
    }

    size_t read = fread(&gA2lHeader, sizeof(tHeader), 1, file);
    if (read != 1 || strncmp(gA2lHeader.signature, BIN_SIGNATURE, sizeof(gA2lHeader.signature)) != 0) {
        DBG_PRINTF3("Invalid file format or signature in '%s'\n", filename);
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
    gA2lBinFilename = filename;

    // Load events
    for (uint16_t i = 0; i < gA2lHeader.event_count; i++) {
        tEventDescriptor desc;
        read = fread(&desc, sizeof(tEventDescriptor), 1, file);
        if (read != 1) {
            DBG_PRINTF3("Failed to read event descriptor from file: %s\n", strerror(errno));
            fclose(file);
            return false;
        }

        tXcpEventId event_id = XcpCreateIndexedEvent(desc.name, desc.index, desc.cycleTimeNs, desc.priority);
        assert(event_id == desc.id); // Ensure the event ID matches the descriptor ID
        (void)event_id;
    }

    // Load calibration segments
    for (uint16_t i = 0; i < gA2lHeader.calseg_count; i++) {
        tCalSegDescriptor desc;
        read = fread(&desc, sizeof(tCalSegDescriptor), 1, file);
        if (read != 1) {
            DBG_PRINTF3("Failed to read calibration segment descriptor from file: %s\n", strerror(errno));
            fclose(file);
            return false;
        }

        void *default_page = malloc(desc.size);
        read = fread(default_page, desc.size, 1, file);
        if (read != 1) {
            DBG_PRINTF3("Failed to read calibration segment data from file: %s\n", strerror(errno));
            free(default_page);
            fclose(file);
            return false;
        }
        tXcpCalSegIndex calseg = XcpCreateCalSeg(desc.name, default_page, desc.size);
        assert(calseg == desc.index);

        // Mark the segment as pre initialized
        tXcpCalSeg *seg = XcpGetCalSeg(calseg);
        seg->mode = PAG_PROPERTY_PRELOAD;
        seg->file_pos = (uint32_t)ftell(file) - desc.size; // Save the position of the segment page data in the file
    }

    return true;
}

#endif // OPTION_CAL_PERSISTENCE
