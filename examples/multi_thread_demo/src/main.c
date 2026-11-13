// multi_thread_demo XCPlite example
//
// A fixed set of worker threads processes batches of work items:
//   - The main loop starts a batch of work items on an external request (calibration parameter start_request),
//     or asks the work cyclically for a new batch while no batch is running (calibration parameter auto_restart)
//   - Each worker thread fetches the next work item index from a shared atomic counter and calls work_execute(index)
//   - When there is nothing to do, the worker sleeps for the poll interval and checks again
//   - The main loop detects when all work items of a batch are completed
//
// The work itself is defined in work.c, main.c is generic, see the interface contract in work.h
//
// Instrumentation:
//   - A single global event 'work_done' is triggered by any worker thread when it completed a work item,
//     it measures the worker stack variables work_index, worker_id, work_time and work_latency
//   - A cyclic event 'mainloop' measures the progress of the current batch
//   - An event 'batch_done' is triggered when a batch is completed, it measures the result, runtime and speedup of the batch

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf

#include "a2l.h"    // for A2l generation
#include "xcplib.h" // for application programming interface

// Internal libxcplite includes to simplify multi platform threading and atomics
#include "platform.h" // for THREAD_HANDLE, create_thread, sleepUs, clockGetMonotonicNs, atomic_xxx

#include "work.h" // for work_init, work_begin_batch, work_execute, work_complete, work_shutdown

//-----------------------------------------------------------------------------------------------------

#define THREAD_COUNT 8 // Number of worker threads

//-----------------------------------------------------------------------------------------------------
// XCP parameters

#define OPTION_PROJECT_NAME "multi_thread_demo" // A2L project name
#define OPTION_PROJECT_VERSION "V5_" __TIME__   // EPK version string
#define OPTION_USE_TCP false                    // TCP or UDP
#define OPTION_SERVER_PORT 5555                 // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}         // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 1024)         // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3                      // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

typedef struct params {
    uint32_t start_request; // Increment to request a new batch, the main loop starts a batch when the value changes
    bool auto_restart;      // Ask the work for a new batch cyclically while no batch is running, the work decides if there is something to do
    uint32_t poll_us;       // Worker poll interval in us, when there is nothing to do
    uint32_t cycle_us;      // Main loop cycle time in us
} params_t;

// Default parameters
static const params_t params = {.start_request = 0, .auto_restart = true, .poll_us = 1000, .cycle_us = 10000};

// Global calibration segment handle
static tXcpCalSegIndex calseg = XCP_UNDEFINED_CALSEG;

//-----------------------------------------------------------------------------------------------------
// Work distribution

// Number of work items not fetched yet, decremented by the workers until zero
// Published with release semantics by the main loop when a new batch is started
static atomic_uint_fast32_t gWorkRemaining = 0;

// Number of work items completed in the current batch, incremented with release semantics by the workers
// The batch is completed when gWorkDone == gBatchSize
static atomic_uint_fast32_t gWorkDone = 0;

// Sum of the work item durations of the current batch in ns, to calculate the speedup of parallel execution
static atomic_uint_fast64_t gWorkTimeSum = 0;

// Batch size and start time of the current batch
// Written by the main loop only while no batch is running, before gWorkRemaining is published,
// read by the workers only after they successfully fetched a work item (acquire), so no atomics are needed
static uint32_t gBatchSize = 0;
static uint64_t gBatchStartTime = 0;

// Fetch the next work item index
// Lock-free: decrement gWorkRemaining only if it is not zero yet, a plain atomic_fetch_sub would underflow when polled while idle
// Work item indices are handed out in ascending order 0..gBatchSize-1
static bool fetch_work(uint32_t *index) {
    uint_fast32_t remaining = atomic_load_explicit(&gWorkRemaining, memory_order_relaxed);
    while (remaining > 0) {
        // On failure, remaining is updated with the current value and the loop retries
        if (atomic_compare_exchange_strong_explicit(&gWorkRemaining, &remaining, remaining - 1, memory_order_acquire, memory_order_relaxed)) {
            *index = gBatchSize - (uint32_t)remaining;
            return true;
        }
    }
    return false;
}

// Start a new batch of work items
// Precondition: no batch is running (gWorkRemaining == 0 and gWorkDone == gBatchSize)
static void start_batch(uint32_t batch_size) {
    gBatchSize = batch_size;
    gBatchStartTime = clockGetMonotonicNs();
    atomic_store_explicit(&gWorkTimeSum, 0, memory_order_relaxed);
    atomic_store_explicit(&gWorkDone, 0, memory_order_relaxed);
    atomic_store_explicit(&gWorkRemaining, batch_size, memory_order_release); // Publish the batch to the workers
}

//-----------------------------------------------------------------------------------------------------

// Signal handler for clean shutdown
static volatile bool gRun = true;
static void sig_handler(int sig) {
    (void)sig;
    gRun = false;
}

//-----------------------------------------------------------------------------------------------------

// Worker thread
// Fetches and executes work items until there is nothing to do, then sleeps for the poll interval
THREAD_FUNC_RETURN worker(void *p) {

    // Worker local measurement variables on stack
    uint16_t worker_id = (uint16_t)(uintptr_t)p; // Index of this worker thread
    uint32_t work_index = 0;                     // Index of the last completed work item
    uint64_t work_time = 0;                      // Duration of the last work item in ns
    uint64_t work_latency = 0;                   // Time from batch start to the start of the last work item in ns

    // Instrumentation: Register the worker local variables with stack addressing mode for the global event 'work_done'
    // All workers execute this function, so the variables have the same stack frame relative address in each thread,
    // the event trigger captures the stack frame of the triggering thread
    A2lOnce() {
        A2lLock();
        A2lSetStackAddrMode(work_done);
        A2lCreateMeasurement(worker_id, "Index of the worker thread which completed the work item");
        A2lCreateMeasurement(work_index, "Index of the completed work item");
        A2lCreatePhysMeasurement(work_time, "Duration of the work item", "conv.ns_to_us", 0.0, 1000000.0);
        A2lCreatePhysMeasurement(work_latency, "Time from batch start to start of the work item", "conv.ns_to_us", 0.0, 10000000.0);
        A2lUnlock();
    }

    while (gRun) {

        // Fetch and execute work
        if (fetch_work(&work_index)) {
            uint64_t t1 = clockGetMonotonicNs();
            work_latency = t1 - gBatchStartTime;
            work_execute(work_index);
            work_time = clockGetMonotonicNs() - t1;

            // Instrumentation: Measurement event, triggered by any worker thread
            DaqTriggerEvent(work_done);

            // Report completion to the main loop
            atomic_fetch_add_explicit(&gWorkTimeSum, work_time, memory_order_relaxed);
            atomic_fetch_add_explicit(&gWorkDone, 1, memory_order_release);
            continue; // Check for more work immediately
        }

        // Nothing to do, sleep for the poll interval
        const params_t *params = (params_t *)XcpLockCalSeg(calseg);
        uint32_t poll_us = params->poll_us;
        XcpUnlockCalSeg(calseg);
        sleepUs(poll_us);
    }

    THREAD_FUNC_END; // Exit the thread
}

//-----------------------------------------------------------------------------------------------------

// Demo main
int main(void) {

    printf("\nXCP on Ethernet multi thread demo\n");
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, activate XCP, must be called before starting the server
    // If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_LOCAL);

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Enable A2L generation and prepare the A2L file, finalize the A2L file on XCP connect, auto grouping
    // All events are created before the worker threads are started, so the A2L file content does not depend on thread scheduling
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    // Create a calibration segment for the calibration parameter struct
    // This segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independant page switching, checksum calculation and reinitialization (copy reference page to working page)
    calseg = XcpCreateCalSeg("params", &params, sizeof(params));
    assert(calseg != XCP_UNDEFINED_CALSEG); // Ensure the calibration segment was created successfully

    // Register calibration parameters in the calibration segment
    A2lSetSegmentAddrMode(calseg, params);
    A2lCreateParameter(params.start_request, "Increment to start a new batch", "", 0, 4294967295.0);
    A2lCreateParameter(params.auto_restart, "Ask the work for a new batch cyclically while no batch is running", "", 0, 1);
    A2lCreateParameter(params.poll_us, "Worker poll interval when there is nothing to do", "us", 10, 1000000);
    A2lCreateParameter(params.cycle_us, "Main loop cycle time", "us", 100, 1000000);

    // Initialize the work, before the worker threads are started
    if (!work_init()) {
        return 1;
    }

    // Conversion for the durations measured in ns
    A2lCreateLinearConversion(ns_to_us, "Duration in ns to us", "us", 0.001, 0.0);

    // Instrumentation: Create the global event triggered by the workers, before the workers are started
    DaqCreateEvent(work_done);

    // Main loop local measurement variables on stack
    uint32_t batch_count = 0;    // Number of completed batches
    uint32_t batch_errors = 0;   // Number of completed batches with an invalid result
    uint8_t batch_running = 0;   // 1 while a batch is running
    uint32_t batch_size = 0;     // Number of work items in the current batch
    uint32_t work_remaining = 0; // Number of work items not fetched yet
    uint32_t work_done = 0;      // Number of work items completed
    uint64_t batch_time = 0;     // Runtime of the current or last batch in ns
    double batch_speedup = 0.0;  // Sum of work item durations / batch runtime of the last batch
    uint64_t batch_duration = 0; // Overall duration of the last batch in ns, from batch start until work_complete returned
    uint32_t last_start_request = params.start_request;

    // Instrumentation: Main loop event and measurement variables for the progress of the current batch
    DaqCreateEvent(mainloop);
    A2lSetStackAddrMode(mainloop);
    A2lCreateMeasurement(batch_running, "Batch is running");
    A2lCreateMeasurement(work_remaining, "Number of work items not fetched yet");
    A2lCreateMeasurement(work_done, "Number of work items completed");

    // Instrumentation: Batch completion event and measurement variables for the result of the completed batch
    DaqCreateEvent(batch_done);
    A2lSetStackAddrMode(batch_done);
    A2lCreateMeasurement(batch_count, "Number of completed batches");
    A2lCreateMeasurement(batch_errors, "Number of completed batches with an invalid result");
    A2lCreateMeasurement(batch_size, "Number of work items of the batch");
    A2lCreatePhysMeasurement(batch_time, "Runtime of the batch until all work items were completed", "conv.ns_to_us", 0.0, 100000000.0);
    A2lCreateMeasurement(batch_speedup, "Sum of work item durations / runtime of the batch");
    A2lCreatePhysMeasurement(batch_duration, "Overall duration of the batch, from batch start until work_complete returned", "conv.ns_to_us", 0.0, 100000000.0);

    // Create the worker threads
    THREAD_HANDLE t[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        t[i] = 0;
        create_thread(&t[i], NULL, worker, (void *)(uintptr_t)i);
    }

    printf("Started %d worker threads, increment params.start_request to start a batch of work\n", THREAD_COUNT);

    // Main loop
    while (gRun) {

        const params_t *params = (params_t *)XcpLockCalSeg(calseg);
        uint32_t start_request = params->start_request;
        bool auto_restart = params->auto_restart;
        uint32_t cycle_us = params->cycle_us;
        XcpUnlockCalSeg(calseg);

        if (batch_running) {
            // Check if the batch is completed
            work_remaining = (uint32_t)atomic_load_explicit(&gWorkRemaining, memory_order_relaxed);
            work_done = (uint32_t)atomic_load_explicit(&gWorkDone, memory_order_acquire);
            batch_time = clockGetMonotonicNs() - gBatchStartTime;
            if (work_done == batch_size) {
                uint64_t work_time_sum = (uint64_t)atomic_load_explicit(&gWorkTimeSum, memory_order_relaxed);
                batch_speedup = batch_time > 0 ? (double)work_time_sum / (double)batch_time : 0.0;
                batch_running = 0;
                batch_count++;
                bool ok = work_complete(batch_size);
                if (!ok) {
                    batch_errors++;
                }
                batch_duration = clockGetMonotonicNs() - gBatchStartTime;

                // Instrumentation: Batch completion event
                DaqTriggerEvent(batch_done);

                printf("Batch %u completed%s: %u work items in %.3f ms, speedup %.2f\n", batch_count, ok ? "" : " with errors", batch_size, (double)batch_time / 1000000.0,
                       batch_speedup);
            }
        } else {
            // Start a new batch on request, or ask the work for a new batch with auto restart
            bool requested = (start_request != last_start_request);
            if (requested || auto_restart) {
                last_start_request = start_request;
                uint32_t items = work_begin_batch(requested);
                if (items > 0) {
                    batch_size = items;
                    work_remaining = batch_size;
                    work_done = 0;
                    batch_time = 0;
                    batch_running = 1;
                    start_batch(batch_size);
                    printf("Batch %u started: %u work items\n", batch_count + 1, batch_size);
                }
            }
        }

        // Instrumentation: Main loop measurement event
        DaqTriggerEvent(mainloop);

        sleepUs(cycle_us);
    }

    // Wait for all threads to finish
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (t[i])
            join_thread(t[i]);
    }

    work_shutdown(); // Release the resources of the work

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server

    return 0;
}
