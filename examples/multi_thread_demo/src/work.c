// multi_thread_demo - work implementation
//
// Placeholder work: a batch has work_params.items work items, each work item sleeps for a pseudo random duration
// between 10% and 100% of work_params.time_us, deterministic for each index.
// The batch result checks that every work item was executed exactly once.
// Replace with the real work, see the interface contract in work.h

#include "work.h"

#include <assert.h> // for assert
#include <stdio.h>  // for printf
#include <stdlib.h> // for malloc, free

#include "a2l.h"    // for A2l generation
#include "xcplib.h" // for application programming interface

#include "platform.h" // for sleepUs

//-----------------------------------------------------------------------------------------------------
// Work calibration parameters

typedef struct work_params {
    uint32_t items;   // Number of work items in a batch
    uint32_t time_us; // Maximum duration of a simulated work item in us
} work_params_t;

// Default parameters
static const work_params_t work_params = {.items = 100, .time_us = 10000};

// Calibration segment handle
static tXcpCalSegIndex work_calseg = XCP_UNDEFINED_CALSEG;

//-----------------------------------------------------------------------------------------------------
// Work state

// Execution count per work item index, each index is owned by the worker which executes it, so no atomics are needed
static uint8_t *gExecCount = NULL;
static uint32_t gExecCountSize = 0;

//-----------------------------------------------------------------------------------------------------

bool work_init(void) {

    // Create a calibration segment for the work parameters
    work_calseg = XcpCreateCalSeg("work_params", &work_params, sizeof(work_params));
    if (work_calseg == XCP_UNDEFINED_CALSEG) {
        return false;
    }

    // Register the work parameters in the calibration segment
    A2lSetSegmentAddrMode(work_calseg, work_params);
    A2lCreateParameter(work_params.items, "Number of work items in a batch", "", 0, 1000000);
    A2lCreateParameter(work_params.time_us, "Maximum duration of a simulated work item", "us", 0, 1000000);

    return true;
}

uint32_t work_begin_batch(void) {

    // Number of work items in this batch
    const work_params_t *params = (const work_params_t *)XcpLockCalSeg(work_calseg);
    uint32_t items = params->items;
    XcpUnlockCalSeg(work_calseg);

    // Resize and clear the execution counters
    if (items > gExecCountSize) {
        uint8_t *p = (uint8_t *)realloc(gExecCount, items);
        if (p == NULL) {
            return 0;
        }
        gExecCount = p;
        gExecCountSize = items;
    }
    for (uint32_t i = 0; i < items; i++) {
        gExecCount[i] = 0;
    }

    return items;
}

void work_execute(uint32_t index) {

    assert(index < gExecCountSize);
    gExecCount[index]++;

    const work_params_t *params = (const work_params_t *)XcpLockCalSeg(work_calseg);
    uint32_t time_us = params->time_us;
    XcpUnlockCalSeg(work_calseg); // Don't hold the calibration segment lock while working

    // Simulate work of a pseudo random duration
    uint32_t hash = (index * 2654435761u) >> 16; // Knuth multiplicative hash, 16 bit
    uint32_t min_us = time_us / 10;
    sleepUs(min_us + (uint32_t)(((uint64_t)(time_us - min_us) * hash) / 0xFFFF));
}

bool work_complete(uint32_t items) {

    // Check that every work item was executed exactly once
    for (uint32_t i = 0; i < items; i++) {
        if (gExecCount[i] != 1) {
            printf("Work item %u executed %u times\n", i, (unsigned)gExecCount[i]);
            return false;
        }
    }
    return true;
}

void work_shutdown(void) {
    free(gExecCount);
    gExecCount = NULL;
    gExecCountSize = 0;
}
