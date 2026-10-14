// persistence_test - verifies freeze and reload of calibration segments through the binary persistence file (.bin)
//
// Goal:
//   Calibration segments frozen by the XCP client (SET_SEGMENT_MODE freeze + SET_REQUEST STORE_CAL_REQ) must be restored on the next start.
//   This must also hold when the BIN file was written in the same run, which happens on every first start of a new build (A2L finalize calls XcpBinWrite()).
//   Freezing must neither grow the file nor touch the other segments.
//
//   Regression test for #141: writeCalseg() recorded the file position after the segment descriptor, while load() and XcpBinFreezeCalSeg() expect
//   the descriptor position. A freeze after XcpBinWrite() wrote 256 bytes too far: the frozen data was lost, the descriptor of the next segment
//   was overwritten (its index shifted on reload) and freezing the last segment grew the file.
//
// Steps:
//   1. First start without BIN file: create the segments and write the BIN file, as the A2L finalize does
//   2. Modify the working pages through the XCP write path and freeze a subset of the segments
//   3. Restart (XcpDeinit, XcpInit with XCP_MODE_PERSISTENCE): indices are stable, frozen segments have the frozen data, the others the default data
//   4. Freeze again (file positions now recorded by load()), restart and check again

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf, fopen, remove
#include <string.h>  // for memset, strcmp

#include <xcplib.h> // for application programming interface

// Internal interfaces used to modify and freeze calibration segments without an XCP client
#include "cal.h"         // for XcpCalSegWriteMemory, XcpSetCalSegMode, XcpFreezeSelectedCalSegs
#include "persistence.h" // for XcpBinWrite, XcpBinGetFilename
#include "xcp.h"         // for PAG_PROPERTY_FREEZE, CRC_CMD_OK
#include "xcplite.h"     // for XcpGetEcuEpk

#include "../../support/check.h" // for the shared CHECK macro

#if !defined(OPTION_ENABLE_PERSISTENCE) || !defined(XCP_ENABLE_FREEZE_CAL_PAGE)
#error "persistence_test requires OPTION_ENABLE_PERSISTENCE and XCP_ENABLE_FREEZE_CAL_PAGE"
#endif

//-----------------------------------------------------------------------------------------------------
// Test options

#define OPTION_PROJECT_NAME "persistence_test"
#define OPTION_PROJECT_VERSION "V1.0.0"
#define OPTION_LOG_LEVEL 2 // 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = print XCP commands
#define BIN_FILE_NAME OPTION_PROJECT_NAME "_" OPTION_PROJECT_VERSION ".bin"

#define DEFAULT_VALUE 0x11
#define FROZEN_VALUE_1 0x5A
#define FROZEN_VALUE_2 0x6B
#define WORKING_VALUE 0x7C // Modified, but not frozen

//-----------------------------------------------------------------------------------------------------
// Calibration segments
// The sizes are chosen so the misplaced freeze write of #141 overwrites the name in the descriptor of segment c (b is 200 bytes)
// and grows the file (d is the last segment)

#define SEG_COUNT 4
#define SEG_MAX_SIZE 200

typedef struct {
    const char *name;
    uint16_t size;
    bool freeze;
} seg_t;

static const seg_t segs[SEG_COUNT] = {
    {"seg_a", 16, true},
    {"seg_b", 200, true},
    {"seg_c", 8, false},
    {"seg_d", 32, true},
};

#ifdef XCP_ENABLE_EPK_CALSEG
#define EPK_SEG_COUNT 1 // The EPK segment has index 0
#else
#define EPK_SEG_COUNT 0
#endif

static uint8_t default_page[SEG_MAX_SIZE];
static tXcpCalSegIndex seg_index[SEG_COUNT];

//-----------------------------------------------------------------------------------------------------

static long fileSize(const char *filename) {
    FILE *file = fopen(filename, "rb");
    CHECK(file != NULL, "BIN file '%s' does not exist", filename);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return size;
}

// Start XCP with persistence and create the calibration segments
// If expect_preloaded, all segments must have been preloaded from the BIN file with unchanged indices
static void start(bool expect_preloaded) {
    CHECK(XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_LOCAL | XCP_MODE_PERSISTENCE));
    CHECK(XcpIsActivated());
    CHECK(strcmp(XcpBinGetFilename(), BIN_FILE_NAME) == 0, "BIN filename is '%s'", XcpBinGetFilename());
    uint16_t count = XcpGetCalSegCount();
    CHECK(count == (expect_preloaded ? EPK_SEG_COUNT + SEG_COUNT : EPK_SEG_COUNT), "calseg count is %u after XcpInit", count);
    for (int i = 0; i < SEG_COUNT; i++) {
        tXcpCalSegIndex index = XcpCreateCalSeg(segs[i].name, default_page, segs[i].size);
        CHECK(index != XCP_UNDEFINED_CALSEG, "create '%s'", segs[i].name);
        if (expect_preloaded) {
            CHECK(index == seg_index[i], "'%s' index changed from %u to %u", segs[i].name, seg_index[i], index);
        }
        seg_index[i] = index;
    }
    count = XcpGetCalSegCount();
    CHECK(count == EPK_SEG_COUNT + SEG_COUNT, "calseg count is %u", count);
}

// Lock the ECU page of segment i and check its content, the lock also takes a published page over (see docs/CAL_RCU.md)
static void checkSeg(int i, uint8_t expected) {
    const uint8_t *page = XcpLockCalSeg(seg_index[i]);
    uint16_t n = 0;
    while (n < segs[i].size && page[n] == expected) {
        n++;
    }
#ifdef OPTION_CAL_RCU_REFCOUNT
    XcpUnlockCalSeg(seg_index[i], page);
#else
    XcpUnlockCalSeg(seg_index[i]);
#endif
    CHECK(n == segs[i].size, "'%s' byte %u is not 0x%02X", segs[i].name, n, expected);
}

// Write all segment working pages through the XCP write path and wait until the change is visible in the ECU pages
// There is no application thread: its locks, which drive the RCU page hand-over, are emulated with lock/unlock cycles,
// the retries of pending publishes in XcpBackgroundTasks() with XcpCalSegPublishAll()
static void writeWorkingPages(uint8_t value) {
    uint8_t data[SEG_MAX_SIZE];
    memset(data, value, sizeof(data));
    for (int i = 0; i < SEG_COUNT; i++) {
        CHECK(XcpCalSegWriteMemory(XcpGetCalSegBaseAddress(seg_index[i]), segs[i].size, data) == CRC_CMD_OK, "write '%s'", segs[i].name);
    }
    for (int cycle = 0;; cycle++) {
        bool pending = false;
        for (int i = 0; i < SEG_COUNT; i++) {
            pending = pending || XcpGetCalSeg(seg_index[i])->h.write_pending;
        }
        if (!pending) {
            break;
        }
        CHECK(cycle < 10, "calibration changes not published after %d cycles", cycle);
        for (int i = 0; i < SEG_COUNT; i++) {
            const uint8_t *page = XcpLockCalSeg(seg_index[i]);
#ifdef OPTION_CAL_RCU_REFCOUNT
            XcpUnlockCalSeg(seg_index[i], page);
#else
            (void)page;
            XcpUnlockCalSeg(seg_index[i]);
#endif
        }
        XcpCalSegPublishAll(false);
    }
    for (int i = 0; i < SEG_COUNT; i++) {
        checkSeg(i, value);
    }
}

// Freeze the selected segments the way the XCP client does (SET_SEGMENT_MODE freeze, then SET_REQUEST STORE_CAL_REQ)
// The file size must not change
static void freezeSelected(void) {
    long size = fileSize(BIN_FILE_NAME);
    for (int i = 0; i < SEG_COUNT; i++) {
        if (segs[i].freeze) {
            CHECK(XcpSetCalSegMode(XcpGetCalSegNumber(seg_index[i]), PAG_PROPERTY_FREEZE) == CRC_CMD_OK, "set freeze mode '%s'", segs[i].name);
        }
    }
    CHECK(XcpFreezeSelectedCalSegs(false) == CRC_CMD_OK);
    CHECK(fileSize(BIN_FILE_NAME) == size, "freeze changed the BIN file size from %ld to %ld", size, fileSize(BIN_FILE_NAME));
}

// Restart and check frozen segments have frozen_value, the others the default value
static void restartAndCheck(uint8_t frozen_value) {
    XcpDeinit();
    start(true);
    for (int i = 0; i < SEG_COUNT; i++) {
        checkSeg(i, segs[i].freeze ? frozen_value : DEFAULT_VALUE);
    }
}

//-----------------------------------------------------------------------------------------------------

int main(int argc, char *argv[]) {

    (void)argc;
    printf("\nXCP calibration segment persistence test - %s\n\n", argv[0]);

    XcpSetLogLevel(OPTION_LOG_LEVEL);
    memset(default_page, DEFAULT_VALUE, sizeof(default_page));

    // 1. First start of a new build: no BIN file, the A2L finalize writes it with the default pages
    printf("First start, write BIN file\n");
    remove(BIN_FILE_NAME);
    start(false);
    CHECK(XcpBinWrite(XcpGetEcuEpk()));

    // 2. Calibrate and freeze in the same run, file positions recorded by XcpBinWrite()
    printf("Freeze after XcpBinWrite\n");
    writeWorkingPages(FROZEN_VALUE_1);
    freezeSelected();

    // 3. Restart, check the frozen data is loaded
    printf("Restart and check\n");
    restartAndCheck(FROZEN_VALUE_1);

    // 4. Calibrate and freeze again, file positions recorded by load(), restart and check
    printf("Freeze after load, restart and check\n");
    writeWorkingPages(FROZEN_VALUE_2);
    freezeSelected();
    writeWorkingPages(WORKING_VALUE); // Not frozen, must be lost on restart
    restartAndCheck(FROZEN_VALUE_2);

    XcpDeinit();
    remove(BIN_FILE_NAME);

    printf("PASSED\n");
    return 0;
}
