// queue_test

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
#include "a2l.h" // for A2l generation application programming interface
#include "platform.h"
#include "xcplib.h" // for application programming interface
#include "xcplib_cfg.h"

// Internal libxcplite includes
// Note: Take care for include order, when using internal libxcplite headers !!
// xcp_cfg.h would includes xcplib_cfg.h and platform.h
#include "../src/queue.h"
#include "dbg_print.h"
#include "xcp_cfg.h"

//-----------------------------------------------------------------------------------------------------

// Test parameters
// 1 million msg/s
// 64 byte payload  -> 64 MByte/s

#define THREAD_COUNT 10    // Number of threads to create
#define THREAD_DELAY_US 10 // Delay in microseconds for the thread loops

#define QUEUE_SIZE (1024 * 1024 * 8)              // Size of the test queue in bytes
#define QUEUE_PAYLOAD_SIZE (4 * sizeof(uint64_t)) // Size of the test payload
#define QUEUE_PEEK                                // Use queuePeek(0) instead of queuePop

// @@@@ TODO Testing queuePeek(n) is not implemented yet
/*

Results:

OPTION_QUEUE_64_VAR_SIZE

Producer acquire lock time statistics:
  count=15843768  max_spins=10  max=83250ns  avg=56ns

Lock time histogram (15843768 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                   3230672   20.39%  #############
  40-80ns                  7069609   44.62%  ##############################
  80-120ns                 5046179   31.85%  #####################
  120-160ns                 327939    2.07%  #
  160-200ns                  59255    0.37%
  200-240ns                  29012    0.18%
  240-280ns                  14370    0.09%
  280-320ns                   6562    0.04%
  320-360ns                   4022    0.03%
  360-400ns                   3057    0.02%
  400-600ns                   9470    0.06%
  600-800ns                   9037    0.06%
  800-1000ns                  7589    0.05%
  1000-1500ns                12568    0.08%
  1500-2000ns                 4872    0.03%
  2000-3000ns                 3419    0.02%
  3000-4000ns                 1098    0.01%
  4000-6000ns                 1144    0.01%
  6000-8000ns                  507    0.00%
  8000-10000ns                 776    0.00%
  10000-20000ns               2075    0.01%
  20000-40000ns                486    0.00%
  40000-80000ns                 49    0.00%
  80000-160000ns                 1    0.00%



OPTION_QUEUE_64_VAR_SIZE


  Producer acquire lock time statistics:
  count=10700464  max_spins=8  max=60834ns  avg=129ns

Lock time histogram (10700464 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                   1151517   10.76%  ############
  40-80ns                  2843906   26.58%  ##############################
  80-120ns                 1735959   16.22%  ##################
  120-160ns                1834476   17.14%  ###################
  160-200ns                1673446   15.64%  #################
  200-240ns                1163311   10.87%  ############
  240-280ns                 141192    1.32%  #
  280-320ns                  40214    0.38%
  320-360ns                  14068    0.13%
  360-400ns                   6130    0.06%
  400-600ns                   7572    0.07%
  600-800ns                  12378    0.12%
  800-1000ns                 13293    0.12%
  1000-1500ns                21039    0.20%
  1500-2000ns                 8902    0.08%
  2000-3000ns                 8382    0.08%
  3000-4000ns                 3856    0.04%
  4000-6000ns                 3900    0.04%
  6000-8000ns                 2343    0.02%
  8000-10000ns                4072    0.04%
  10000-20000ns               9923    0.09%
  20000-40000ns                570    0.01%
  40000-80000ns                 15    0.00%


*/

//-----------------------------------------------------------------------------------------------------
// XCP parameters

#define OPTION_PROJECT_NAME "daq_test"  // Project name, used to build the A2L and BIN file name
#define OPTION_PROJECT_EPK __TIME__     // EPK version string
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 32)   // Size of the measurement queue in bytes, should be large enough to cover at least 10ms of expected traffic
#define OPTION_LOG_LEVEL 3              // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------

// Signal handler for clean shutdown
static volatile bool gRun = true;
static void sig_handler(int sig) { gRun = false; }

//-----------------------------------------------------------------------------------------------------

static atomic_uint_least16_t task_index_ctr = 0;
static tQueueHandle queue_handle = NULL;

// Task function that runs in a separate thread
// Calculates a sine wave, square wave, and sawtooth wave signal
#ifdef _WIN32 // Windows 32 or 64 bit
DWORD WINAPI task(LPVOID p)
#else
void *task(void *p)
#endif
{

    bool run = true;
    uint32_t delay_us = 1;

    // Task local measurement variables on stack
    uint64_t counter = 0;

    // Build the task name from the event index
    uint16_t task_index = atomic_fetch_add(&task_index_ctr, 1);
    char task_name[16 + 1];
    snprintf(task_name, sizeof(task_name), "task_%u", task_index);

    printf("thread %s running...\n", task_name);

    while (run && gRun) {

        for (int n = 0; n < 2; n++) {

            counter++;

            uint16_t size = QUEUE_PAYLOAD_SIZE;
            tQueueBuffer queue_buffer = queueAcquire(queue_handle, size);
            if (queue_buffer.size >= size) {
                assert(queue_buffer.buffer != NULL);

                // Simulate XCP DAQ header, because the queue implementation is not generic, it has some XCP specific asserts
                // assert(entry->data[4 + 1] == 0xAA || entry->data[4 + 0] >=0xFC))) {
                *(uint32_t *)queue_buffer.buffer = 0x0000AAFC;

                // Test data
                uint64_t *b = (uint64_t *)(queue_buffer.buffer + sizeof(uint32_t));
                b[0] = task_index;
                b[1] = size;
                b[2] = counter;

                queuePush(queue_handle, &queue_buffer, false);
            }
        }

        // Sleep for the specified delay parameter in microseconds, defines the approximate sampling rate
        sleepUs(THREAD_DELAY_US);
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

    queue_handle = queueInit(QUEUE_SIZE); // Initialize the queue, the queue memory is allocated by the library, the queue buffer size is specified by OPTION_QUEUE_SIZE
    if (queue_handle == NULL) {
        printf("Failed to initialize the queue\n");
        return 1;
    }

    // Create multiple instances of task
    THREAD t[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        t[i] = 0;
        create_thread(&t[i], task);
    }

    uint32_t msg_count = 0;
    uint32_t msg_lost = 0;
    uint32_t msg_bytes = 0;
    uint64_t last_msg_time = clockGetMonotonicUs();
    uint32_t last_msg_count = 0;
    uint32_t last_msg_bytes = 0;
    uint64_t last_counter[THREAD_COUNT];
    memset(last_counter, 0, sizeof(last_counter));

    A2lLock();
    DaqCreateEvent(mainloop);
    A2lSetStackAddrMode(mainloop);
    A2lCreateMeasurement(msg_count, "Message count");
    A2lCreateMeasurement(msg_lost, "Messages lost");
    A2lCreateMeasurement(msg_bytes, "Message bytes");
    A2lUnlock();

    sleepUs(200000); // Wait 200ms for the threads to start
    A2lFinalize();   // Manually finalize the A2L file to make it visible without XCP tool connect

    // Wait for signal to stop
    printf("main loop running - press Ctrl+C to stop...\n");
    while (gRun) {

        // Poll the queue, break if empty
        for (;;) {
            uint32_t lost = 0;
#ifdef QUEUE_PEEK
            tQueueBuffer buffer = queuePeek(queue_handle, 0, false, &lost);
            msg_lost += lost;
            if (buffer.size == 0)
                break; // No more messages in the queue
#else
            tQueueBuffer segment_buffer = queuePop(queue_handle, true, false, &lost); // May accumulate multiple messages in one segment (message has a transport layer header)
            msg_lost += lost;
            if (segment_buffer.size == 0)
                break;

            uint32_t segment_size = segment_buffer.size;
            tQueueBuffer buffer;
            buffer.size = *(uint16_t *)segment_buffer.buffer + sizeof(uint32_t); // Get the buffer size from transportlayer header dlc
            buffer.buffer = segment_buffer.buffer;                               // Move the buffer pointer to the start of the message payload (to the transport layer header)
            assert(buffer.size > 0 && buffer.size <= QUEUE_PAYLOAD_SIZE + 8);

            // Iterate over all messages in the segment
            for (;;) {

#endif

            assert(buffer.buffer != NULL);
            assert(buffer.size >= QUEUE_PAYLOAD_SIZE);
            assert((uint64_t)buffer.buffer % 2 == 0);

            uint64_t *b = (uint64_t *)(buffer.buffer + 8); // Test payload starts + 8 (Transport layer header + XCP DAQ header)
            uint64_t task_index = b[0];
            uint64_t size = b[1];
            uint64_t counter = b[2];

            assert(size >= QUEUE_PAYLOAD_SIZE);
            assert(task_index < THREAD_COUNT);
            if (msg_count > 0) {
                if (counter != last_counter[task_index] + 1) {
                    printf("Messages lost in task %u, expected counter %llu, got %llu\n", (uint32_t)task_index, last_counter[task_index] + 1, counter);
                }
            }

            last_counter[task_index] = counter;

            msg_count++;
            msg_bytes += buffer.size;

#ifdef QUEUE_PEEK
            queueRelease(queue_handle, &buffer);
#else
                assert(segment_size >= buffer.size);
                segment_size -= buffer.size;
                if (segment_size == 0) {
                    queueRelease(queue_handle, &segment_buffer);
                    break; // No more messages in the segment
                }

                buffer.buffer += buffer.size;                                // Move to the next message in the segment (include the transport layer header size)
                buffer.size = *(uint16_t *)buffer.buffer + sizeof(uint32_t); // Get the buffer size from transportlayer header dlc

            } // for (;;)
#endif
        } // for (;;)

        DaqTriggerEvent(mainloop);
        sleepUs(500); // 500us

        // Print statistics every second
        if (clockGetMonotonicUs() - last_msg_time >= 1000000) {
            printf("Messages received: %u, bytes received: %u, messages lost: %u, data rate: %u msg/s, %u kbytes/s\n", msg_count, msg_bytes, msg_lost, (msg_count - last_msg_count),
                   (msg_bytes - last_msg_bytes) / 1024);
            last_msg_time = clockGetMonotonicUs();
            last_msg_bytes = msg_bytes;
            last_msg_count = msg_count;
        }
    } // gRun

    // Wait for all threads to finish
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (t[i])
            join_thread(t[i]);
    }

    queueDeinit(queue_handle); // Deinitialize the queue

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
    return 0;
}
