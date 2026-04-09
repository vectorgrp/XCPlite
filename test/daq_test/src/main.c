// daq_test

#include <assert.h>  // for assert
#include <math.h>    // for M_PI, sin
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "xcplib_cfg.h"
#ifndef OPTION_ATOMIC_EMULATION
#include <stdatomic.h> // for atomic_
#endif

// Public XCPlite API
#include "a2l.h"    // for A2l generation application programming interface
#include "xcplib.h" // for application programming interface

// Internal libxcplite includes
// Note: Take care for include order, when using internal libxcplite headers !!
// xcp_cfg.h would includes xcplib_cfg.h and platform.h
#include "dbg_print.h"
#include "platform.h" // for clockGetMonotonicNs, sleepUs, mutexLock, mutexUnlock, THREAD, create_thread, join_thread, cancel_thread

//-----------------------------------------------------------------------------------------------------
// Test configuration

#define THREAD_COUNT 8             // Number of threads to create
#define THREAD_DELAY_US 1000       // Default delay in microseconds for the thread loops, calibration parameter
#define THREAD_DELAY_OFFSET_US 100 // Default offset  added to the delay (* task index) for each thread instance, to create different sampling rates
#define THREAD_TIME_SHIFT_US                                                                                                                                                       \
    (500000 / THREAD_COUNT) // Default time shift in microseconds (* task index) for each thread instance, to disturb the sequential time ordering of events

#define TEST_DAQ_EVENT_TIMING

//-----------------------------------------------------------------------------------------------------
// XCP parameters

#define OPTION_PROJECT_NAME "daq_test"      // Project name, used to build the A2L and BIN file name
#define OPTION_PROJECT_VERSION "V2.1.5"     // EPK version string
#define OPTION_USE_TCP false                // TCP or UDP
#define OPTION_SERVER_PORT 5555             // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}     // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 1024 * 8) // Size of the measurement queue in bytes, should be large enough to cover at least 10ms of expected traffic
#define OPTION_LOG_LEVEL 4                  // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------

#ifdef TEST_ENABLE_DBG_METRICS

extern uint32_t gXcpWritePendingCount;
extern uint32_t gXcpCalSegPublishAllCount;
extern uint32_t gXcpDaqEventCount;
extern uint32_t gXcpTxPacketCount;
extern uint32_t gXcpTxMessageCount;
extern uint32_t gXcpTxIoVectorCount;
extern uint32_t gXcpRxPacketCount;

#endif

//-----------------------------------------------------------------------------------------------------

#ifdef TEST_DAQ_EVENT_TIMING

#define TIMING_HISTOGRAM_SIZE 24

static MUTEX timing_mutex;
static uint64_t timing_max = 0;
static uint64_t timing_sum = 0;
static uint64_t timing_sample_count = 0;
static uint64_t timing_sample_calibration = 0; // Calibration value for the overhead of the timing measurement itself, to get more accurate results for short lock times

// Variable-width timing histogram
// Fine granularity for short latencies, coarser for long-tail latencies
// Bin[i] counts samples where [i-1] <= t < [i]; bin[SIZE-1] is the overflow (>EDGES[SIZE-2])
static const uint64_t timing_histogram_axis[TIMING_HISTOGRAM_SIZE] = { //
    10,   20,   40,   80,   120,  160,  200,  300,                     //
    400,  500,  600,  700,  800,  900,  1000, 1250,                    //
    1500, 1750, 2000, 3000, 4000, 5000, 7500, 10000};
static uint64_t timing_histogram[TIMING_HISTOGRAM_SIZE] = {0};

static void timing_sample_test_init(void) {
    memset(timing_histogram, 0, sizeof(timing_histogram));
    timing_max = 0;
    timing_sum = 0;
    timing_sample_count = 0;

    mutexInit(&timing_mutex, false, 0);

    // Calibrate
    uint64_t sum = 0;
    for (int i = 0; i < 1000; i++) {
        uint64_t time = clockGetMonotonicNs();
        sum += clockGetMonotonicNs() - time;
    }
    timing_sample_calibration = sum / 1000;
}

static void timing_sample_test_add_sample(uint64_t d) {
    if (d >= timing_sample_calibration) // Subtract calibration value to get more accurate results for short lock times
        d -= timing_sample_calibration;
    else
        d = 0;
    mutexLock(&timing_mutex);
    ; // Subtract calibration value to get more accurate results for short lock times
    if (d > timing_max)
        timing_max = d;
    int i = 0;
    while (i < TIMING_HISTOGRAM_SIZE - 1 && d >= timing_histogram_axis[i])
        i++;
    timing_histogram[i]++;
    timing_sum += d;
    timing_sample_count++;
    mutexUnlock(&timing_mutex);
}

static void timing_sample_test_print_results(void) {
    printf("\nProducer acquire lock time statistics:\n");
    printf("  count=%" PRIu64 "  max=%" PRIu64 "ns  avg=%" PRIu64 "ns (cal=%" PRIu64 "ns)\n", timing_sample_count, timing_max, timing_sum / timing_sample_count,
           timing_sample_calibration);

    uint64_t histogram_sum = 0;
    for (int i = 0; i < TIMING_HISTOGRAM_SIZE; i++)
        histogram_sum += timing_histogram[i];
    uint64_t histogram_max = 0;
    for (int i = 0; i < TIMING_HISTOGRAM_SIZE; i++)
        if (timing_histogram[i] > histogram_max)
            histogram_max = timing_histogram[i];

    printf("\nLock time histogram (%" PRIu64 " events):\n", histogram_sum);
    printf("  %-20s  %10s  %7s  %s\n", "Range", "Count", "%", "Bar");
    printf("  %-20s  %10s  %7s  %s\n", "--------------------", "----------", "-------", "------------------------------");

    for (int i = 0; i < TIMING_HISTOGRAM_SIZE; i++) {

        double pct = (double)timing_histogram[i] * 100.0 / (double)histogram_sum;

        char range_str[32];
        uint64_t lo = (i == 0) ? 0 : timing_histogram_axis[i - 1];
        if (i == TIMING_HISTOGRAM_SIZE - 1) {
            snprintf(range_str, sizeof(range_str), ">%" PRIu64 "ns", lo);
        } else {
            snprintf(range_str, sizeof(range_str), "%" PRIu64 "-%" PRIu64 "ns", lo, timing_histogram_axis[i]);
        }

        char bar[31];
        int bar_len = (histogram_max > 0) ? (int)((double)timing_histogram[i] * 30.0 / (double)histogram_max) : 0;
        if (bar_len > 30)
            bar_len = 30;
        for (int j = 0; j < bar_len; j++)
            bar[j] = '#';
        bar[bar_len] = '\0';

        printf("  %-20s  %10" PRIu64 "  %6.2f%%  %s\n", range_str, timing_histogram[i], pct, bar);
    }
    printf("\n");
}

#endif

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

typedef struct params {
    uint16_t counter_max;    // Maximum value of the counter
    uint32_t delay_us;       // Delay in microseconds for the thread loops
    int32_t delay_offset_us; // Offset in microseconds added to the delay (* task index) for each thread instance, to create different sampling rates
    int32_t time_shift_us;   // Time shift in microseconds (* task index) for each thread instance, to disturb the sequential time ordering of events
    bool run;                // Stop flag for the task
    int8_t test_byte1;
    int8_t test_byte2;
} params_t;

// Default parameters
static const params_t params = {.counter_max = 2048,
                                .delay_us = THREAD_DELAY_US,
                                .delay_offset_us = THREAD_DELAY_OFFSET_US,
                                .time_shift_us = THREAD_TIME_SHIFT_US,
                                .run = true,
                                .test_byte1 = -1,
                                .test_byte2 = 1};

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
    int32_t time_shift_us = 0;

    // Task local measurement variables on stack
    uint16_t counter = 0;
    uint8_t counter8 = 0;
    uint16_t counter16 = 0;
    uint32_t counter32 = 0;
    uint64_t counter64 = 0;
    uint32_t array[64] = {0}; // 64*4=256 byte array

    // Instrumentation: Events and measurement variables
    // Register task local variables counter and channelx with stack addressing mode
    DaqCreateEventInstance(task);
    tXcpEventId task_event_id = DaqGetEventInstanceId(task);

    // Build the task name from the event index
    uint16_t task_index = XcpGetEventIndex(task_event_id); // Get the event index of this event instance
    char task_name[16 + 1];
    snprintf(task_name, sizeof(task_name), "task_%u", task_index);

    // Create measurement variables for this task instance
    A2lLock();
    A2lSetStackAddrMode_i(task_event_id);
    A2lCreateMeasurementInstance(task_name, counter, "Taskloop counter uint16_t, max value from calibration counter_max");
    A2lCreateMeasurementInstance(task_name, counter8, "Taskloop counter uint8_t");
    A2lCreateMeasurementInstance(task_name, counter16, "Taskloop counter uint16_t");
    A2lCreateMeasurementInstance(task_name, counter32, "Taskloop counter uint32_t");
    A2lCreateMeasurementInstance(task_name, counter64, "Taskloop counter uint64_t");
    A2lCreateMeasurementArrayInstance(task_name, array, "Taskloop array[64] uint32_t");
    A2lUnlock();

    printf("thread %s running...\n", task_name);

    while (run && gRun) {

        {
            const params_t *params = (params_t *)XcpLockCalSeg(calseg);

            counter++;
            if (counter > params->counter_max) {
                counter = 0;
            }
            counter8 = (uint8_t)counter;
            counter16 = (uint16_t)counter;
            counter32 = (uint32_t)counter;
            counter64 = (uint64_t)counter;
            array[0] = counter;

            (void)counter;
            (void)counter8;
            (void)counter16;
            (void)counter32;
            (void)counter64;
            (void)array;

            // Sleep time for this task
            // Each task has different sleep time, to simulate more or less workload and to create a less deterministic interleaving in the measurements
            // Add an offset to the delay for each task instance, to create different sampling rates
            int32_t offset = ((int32_t)task_index - THREAD_COUNT / 2) * params->delay_offset_us;
            if (offset < -(int32_t)params->delay_us)
                offset = -(int32_t)params->delay_us;
            delay_us = (uint32_t)((int32_t)params->delay_us + offset);
            // printf("thread %u:%s offset: %d, delay: %u us\n", task_index, task_name, offset, delay_us);

            // Add an offset to the time shift for each task instance, to create different time shifts in the event timestamps
            time_shift_us = ((int32_t)task_index - THREAD_COUNT / 2) * params->time_shift_us;
            // printf("thread %u:%s time shift: %d us\n", task_index, task_name, time_shift_us);

            // Stop
            run = params->run;

            XcpUnlockCalSeg(calseg);
        }

        uint64_t clock = ApplXcpGetClock64();

        // Add a time shift to the event clock for each task instance, to disturb the sequential time ordering of events and
        // to test the correct handling of event timestamps in the client
        DaqTriggerEventAt_i(task_event_id, clock + (int64_t)(time_shift_us * 1000));

#ifdef TEST_DAQ_EVENT_TIMING
        timing_sample_test_add_sample(ApplXcpGetClock64() - clock);
#endif

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
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_LOCAL);

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
    A2lCreateParameter(params.delay_offset_us, "task delay offset in us added to the delay for each task instance", "us", 0, 1000000);
    A2lCreateParameter(params.time_shift_us, "task time shift in us added to the event timestamp for each task instance", "us", 0, 1000000);
    A2lCreateParameter(params.run, "stop task", "", 0, 1);
    A2lCreateParameter(params.test_byte1, "Test byte for calibration consistency test", "", -128, 127);
    A2lCreateParameter(params.test_byte2, "Test byte for calibration consistency test", "", -128, 127);

    // Create the mainloop event and global measurement variables for statistics
    static uint16_t counter = 0;
    DaqCreateEvent(mainloop);
    A2lSetAbsoluteAddrMode(mainloop);
    A2lCreateMeasurement(counter, "Main loop counter");

#ifdef TEST_ENABLE_DBG_METRICS
    A2lCreateMeasurement(gXcpWritePendingCount, "XCP write pending count");
    A2lCreateMeasurement(gXcpCalSegPublishAllCount, "XCP calibration segment publish all count");
    A2lCreateMeasurement(gXcpDaqEventCount, "XCP DAQ event count");
    A2lCreateMeasurement(gXcpTxPacketCount, "XCP TX packet count");
    A2lCreateMeasurement(gXcpTxMessageCount, "XCP TX message count");
    A2lCreateMeasurement(gXcpTxIoVectorCount, "XCP TX IO vector count");
    A2lCreateMeasurement(gXcpRxPacketCount, "XCP RX packet count");
#endif

    // Init event timing test
#ifdef TEST_DAQ_EVENT_TIMING
    timing_sample_test_init();
    A2lCreateCurveWithSharedAxis(timing_histogram, TIMING_HISTOGRAM_SIZE, "XcpEvent duration histogram", "", 0, 100000.0, "timing_histogram_axis");
    A2lCreateAxis(timing_histogram_axis, TIMING_HISTOGRAM_SIZE, "XcpEvent duration histogram", "ns", 0, 10000.0);
#endif

    // Create multiple instances of task
    THREAD t[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        t[i] = 0;
        create_thread(&t[i], task);
    }

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
        sleepUs(1000); // 1000us
    }

    // Wait for all threads to finish
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (t[i])
            join_thread(t[i]);
    }

#ifdef TEST_ENABLE_DBG_METRICS
    printf("  Total DAQ events:  %u\n", gXcpDaqEventCount);
    printf("  Total TX packets:  %u\n", gXcpTxPacketCount);
    printf("  Total TX messages: %u\n", gXcpTxMessageCount);
    printf("  Total TX iovecs:   %u\n", gXcpTxIoVectorCount);
    printf("  Total RX packets:  %u\n", gXcpRxPacketCount);
#endif
#ifdef TEST_DAQ_EVENT_TIMING
    timing_sample_test_print_results();
#endif
    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server

    return 0;
}
