// daq_test

#include <assert.h> // for assert
#include <math.h>   // for M_PI, sin
#include <signal.h> // for signal handling
#ifndef _WIN32
#include <stdatomic.h> // for atomic_
#endif
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

// Public XCPlite API
#include "a2l.h"    // for A2l generation application programming interface
#include "xcplib.h" // for application programming interface

// Internal libxcplite includes
// Note: Take care for include order, when using internal libxcplite headers !!
// xcp_cfg.h would includes xcplib_cfg.h and platform.h
#include "dbg_print.h"
#include "platform.h"
#include "xcp_cfg.h"
#include "xcplib_cfg.h"

//-----------------------------------------------------------------------------------------------------

#define XCP_MAX_EVENT_NAME 15
#define THREAD_COUNT 8      // Number of threads to create
#define THREAD_DELAY_US 100 // Delay in microseconds for the thread loops

//-----------------------------------------------------------------------------------------------------
// XCP parameters

#define OPTION_PROJECT_NAME "daq_test"      // A2L project name
#define OPTION_PROJECT_EPK __TIME__         // EPK version string
#define OPTION_USE_TCP false                // TCP or UDP
#define OPTION_SERVER_PORT 5555             // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}     // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 1024 * 8) // Size of the measurement queue in bytes, must be a multiple of 128
#define OPTION_LOG_LEVEL 3                  // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

typedef struct params {
    uint16_t counter_max; // Maximum value of the counter
    uint32_t delay_us;    // Delay in microseconds for the thread loops
    bool run;             // Stop flag for the task
    int8_t test_byte1;
    int8_t test_byte2;
} params_t;

// Default parameters
static const params_t params = {.counter_max = 1024, .delay_us = THREAD_DELAY_US, .run = true, .test_byte1 = -1, .test_byte2 = 1};

// Global calibration segment handle
static tXcpCalSegIndex calseg = XCP_UNDEFINED_CALSEG;

//-----------------------------------------------------------------------------------------------------

// Signal handler for clean shutdown
static volatile bool gRun = true;
static void sig_handler(int sig) { gRun = false; }

//-----------------------------------------------------------------------------------------------------

// Task function that runs in a separate thread
// Calculates a sine wave, square wave, and sawtooth wave signal
#ifdef _WIN32 // Windows 32 or 64 bit
DWORD WINAPI task(LPVOID p)
#else
void *task(void *p)
#endif
{
    (void)p;

    bool run = true;
    uint32_t delay_us = 1;

    // Task local measurement variables on stack
    uint16_t counter = 0;
    uint32_t array[256] = {0};

    // Instrumentation: Events and measurement variables
    // Register task local variables counter and channelx with stack addressing mode
    DaqCreateEventInstance(task);
    tXcpEventId task_event_id = DaqGetEventInstanceId(task);

    // Build the task name from the event index
    uint16_t task_index = XcpGetEventIndex(task_event_id); // Get the event index of this event instance
    char task_name[XCP_MAX_EVENT_NAME + 1];
    snprintf(task_name, sizeof(task_name), "task_%u", task_index);

    // Create measurement variables for this task instance
    A2lLock();
    A2lSetStackAddrMode_i(task_event_id);
    A2lCreateMeasurementInstance(task_name, counter, "Taskloop counter");
    A2lCreateMeasurementArrayInstance(task_name, array, "task array (to increase measurement workload)");
    A2lUnlock();

    printf("thread %s running...\n", task_name);

    while (run && gRun) {

        {
            const params_t *params = (params_t *)XcpLockCalSeg(calseg);

            counter++;
            if (counter > params->counter_max) {
                counter = 0;
            }

            // Sleep time
            delay_us = params->delay_us;

            // Stop
            run = params->run;

            XcpUnlockCalSeg(calseg);
        }

        // Instrumentation: Measurement event
        DaqTriggerEvent_i(task_event_id);

        // Sleep for the specified delay parameter in microseconds, defines the approximate sampling rate
        sleepUs(delay_us);
    }

    return 0; // Exit the thread
}

//-----------------------------------------------------------------------------------------------------

// Demo main
int main(void) {

    printf("\nXCP on Ethernet multi thread daq test\n");
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, activate XCP, must be called before starting the server
    // If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_EPK, true);

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Enable A2L generation and prepare the A2L file, finalize the A2L file on XCP connect, auto grouping
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
    A2lCreateParameter(params.counter_max, "Max counter value, wrap around", "", 0, 65535);
    A2lCreateParameter(params.delay_us, "task delay time in us", "us", 0, 1000000);
    A2lCreateParameter(params.run, "stop task", "", 0, 1);
    A2lCreateParameter(params.test_byte1, "Test byte for calibration consistency test", "", -128, 127);
    A2lCreateParameter(params.test_byte2, "Test byte for calibration consistency test", "", -128, 127);

    // Create multiple instances of task
    THREAD t[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        t[i] = 0;
        create_thread(&t[i], task);
    }

    uint16_t counter = 0;

    A2lLock();
    DaqCreateEvent(mainloop);
    A2lSetStackAddrMode(mainloop);
    A2lCreateMeasurement(counter, "Main loop counter");
    A2lUnlock();

    sleepUs(200000); // Wait 200ms for the threads to start
    A2lFinalize();   // Manually finalize the A2L file to make it visible without XCP tool connect

    // Wait for signal to stop
    printf("main loop running - press Ctrl+C to stop...\n");
    while (gRun) {

        counter++;

        // Limit the counter by the max value from the calibration parameters, to test calibration access under heavy load
        const params_t *params = (params_t *)XcpLockCalSeg(calseg);
        if (counter > params->counter_max) {
            counter = 0;
        }

        // Calibration consistency test under heavy load
        if (params->test_byte1 != -params->test_byte2 && params->test_byte2 < 0) {
            printf("Inconsistent %u:  %d -  %d\n", counter, params->test_byte1, params->test_byte2);
        }

        XcpUnlockCalSeg(calseg);

        DaqTriggerEvent(mainloop);
        sleepUs(100); // 100us
    }

    // Wait for all threads to finish
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (t[i])
            join_thread(t[i]);
    }

#ifdef OPTION_ENABLE_DBG_METRICS
    printf("  Total DAQ events: %u\n", gXcpDaqEventCount);
    printf("  Total TX packets: %u\n", gXcpTxPacketCount);
    printf("  Total RX packets: %u\n", gXcpRxPacketCount);
#endif

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
    return 0;
}
