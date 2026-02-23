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

// Option to use XCP for online performance monitoring and logging of the queue test
// #define USE_XCP

// Option to enable timing statistics
#define TEST_ACQUIRE_LOCK_TIMING

// Note: Take care for include order

// Public XCPlite API
#include "xcplib_cfg.h" // for OPTION_xxx
// Disable socket support with vectored IO to avoid platform.h includes queue.h
#undef OPTION_ENABLE_TCP
#undef OPTION_ENABLE_UDP
#include "platform.h"

// Option XCP server for online performance monitoring and logging of the queue test
#ifdef USE_XCP
#include "a2l.h"    // for A2l generation application programming interface
#include "xcplib.h" // for application programming interface
#endif

// Use the logger from XCPlite but don't include the rest of the API
#include "dbg_print.h"
void XcpSetLogLevel(uint8_t level);

//-------------------------------------------------------------------------------------------------------
// Test the mc_queue reference implementation
#ifdef TEST_MC_QUEUE

#ifdef __XCP_QUEUE_h__
#error "queue.h included, please check your include order"
#endif
#include "mc/reference.h"
// Undef
#undef XCP_DAQ_MEM_SIZE

// MC queue has no transport layer header space - define before queue.h gets pulled in via xcp_cfg.h
#undef QUEUE_ENTRY_USER_HEADER_SIZE
#define QUEUE_ENTRY_USER_HEADER_SIZE 0

//-------------------------------------------------------------------------------------------------------
// Test the queue implementation from XCPlite
#else

// Internal libxcplite includes
#include "../src/queue.h"

#endif

//-----------------------------------------------------------------------------------------------------

// Use the logger from XCPlite
// Note: If logging enabled with log level 6 OPTION_MAX_DBG_LEVEL must be set to 6
#define OPTION_LOG_LEVEL 5 // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug, 5 = trace, 6 = verbose

#define QUEUE_SIZE (1024 * 256) // Size of the test queue in bytes

// Test parameters
// 64 byte payload  * THREAD_COUNT * 1000000/THREAD_DELAY_US = Throughput in byte/s

// Parameters for 1000000 msg/s with 10 threads, 64 byte payload, 10us delay
#define THREAD_COUNT 10                            // Number of threads to create
#define THREAD_DELAY_US 10                         // Delay in microseconds for the thread loops
#define THREAD_BURST_SIZE 4                        // Acquire and push this many entries in a burst before sleeping
#define THREAD_PAYLOAD_SIZE (4 * sizeof(uint64_t)) // Size of the test payload produced by the threads

//

// queue62v.c and queue64f.c support peeking ahead
#if defined(OPTION_QUEUE_64_VAR_SIZE) || defined(OPTION_QUEUE_64_FIX_SIZE)
#define TEST_QUEUE_PEEK          // Use queuePeek(random(QUEUE_PEEK_MAX_INDEX)) instead of queuePop
#define QUEUE_PEEK_MAX_INDEX (8) // Max offset for peeking ahead

// reference.c also supports peeking ahead
#elif defined(TEST_MC_QUEUE)
#define TEST_QUEUE_PEEK          // MC queue also supports peeking ahead with mc_queue_peak
#define QUEUE_PEEK_MAX_INDEX (8) // Max offset for peeking ahead
#endif

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Acquire timing test

// Queue acquire lock timing and spin
// For high contention use test queue_test or example daq_test with xcp_client --upload-a2l --udp --mea .  --dest-addr 192.168.0.206
// Note that this tests have significant performance impact, do not turn on for production use !!!!!!!!!!!

#ifdef TEST_ACQUIRE_LOCK_TIMING

static MUTEX lock_mutex = MUTEX_INTIALIZER;
static uint64_t lock_time_max = 0;
static uint64_t lock_time_sum = 0;
static uint64_t lock_count = 0;
static uint64_t lock_spin_count_max = 0;
// Variable-width lock timing histogram
// Fine granularity for short latencies, coarser for long-tail latencies
// Bin[i] counts events where EDGES[i-1] <= t < EDGES[i]; bin[SIZE-1] is the overflow (>EDGES[SIZE-2])
#define LOCK_TIME_HISTOGRAM_SIZE 26
static const uint64_t LOCK_TIME_HISTOGRAM_EDGES[LOCK_TIME_HISTOGRAM_SIZE - 1] = {
    40,    80,    120,   160,    200,    240, 280, 320, 360, 400, // 10 bins: 40ns steps
    600,   800,   1000,  1500,   2000,                            //  5 bins: 200-500ns steps (up to 2us)
    3000,  4000,  6000,  8000,   10000,                           //  5 bins: 1-2us steps (up to 10us)
    20000, 40000, 80000, 160000, 320000,                          //  5 bins: 10-160us steps (up to 320us, preemption range)
};
static uint64_t lock_time_histogram[LOCK_TIME_HISTOGRAM_SIZE] = {0};

// There should be better alternatives in your target specific environment than this portable reference
// Select a clock mode appropriate for your platform, CLOCK_MONOTONIC_RAW is a good choice for high resolution and monotonicity
#ifndef TEST_MC_QUEUE
static uint64_t get_timestamp_ns(void) {
    static const uint64_t kNanosecondsPerSecond = 1000000000ULL;
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts); // NOLINT(missing-includes) // do **not** include internal "bits" headers directly.
    return ((uint64_t)ts.tv_sec) * kNanosecondsPerSecond + ((uint64_t)ts.tv_nsec);
}
#endif

void lock_test_add_sample(uint64_t d, uint32_t spin_count) {
    mutexLock(&lock_mutex);
    if (spin_count > lock_spin_count_max)
        lock_spin_count_max = spin_count;
    if (d > lock_time_max)
        lock_time_max = d;
    int i = 0;
    while (i < LOCK_TIME_HISTOGRAM_SIZE - 1 && d >= LOCK_TIME_HISTOGRAM_EDGES[i])
        i++;
    lock_time_histogram[i]++;
    lock_time_sum += d;
    lock_count++;
    mutexUnlock(&lock_mutex);
}

static void lock_test_print_results(void) {
    printf("\nProducer acquire lock time statistics:\n");
    printf("  count=%" PRIu64 "  max_spins=%" PRIu64 "  max=%" PRIu64 "ns  avg=%" PRIu64 "ns\n", lock_count, lock_spin_count_max, lock_time_max, lock_time_sum / lock_count);

    uint64_t histogram_sum = 0;
    for (int i = 0; i < LOCK_TIME_HISTOGRAM_SIZE; i++)
        histogram_sum += lock_time_histogram[i];
    uint64_t histogram_max = 0;
    for (int i = 0; i < LOCK_TIME_HISTOGRAM_SIZE; i++)
        if (lock_time_histogram[i] > histogram_max)
            histogram_max = lock_time_histogram[i];

    printf("\nLock time histogram (%" PRIu64 " events):\n", histogram_sum);
    printf("  %-20s  %10s  %7s  %s\n", "Range", "Count", "%", "Bar");
    printf("  %-20s  %10s  %7s  %s\n", "--------------------", "----------", "-------", "------------------------------");

    for (int i = 0; i < LOCK_TIME_HISTOGRAM_SIZE; i++) {
        if (!lock_time_histogram[i])
            continue;
        double pct = (double)lock_time_histogram[i] * 100.0 / (double)histogram_sum;

        char range_str[32];
        uint64_t lo = (i == 0) ? 0 : LOCK_TIME_HISTOGRAM_EDGES[i - 1];
        if (i == LOCK_TIME_HISTOGRAM_SIZE - 1) {
            snprintf(range_str, sizeof(range_str), ">%" PRIu64 "ns", lo);
        } else {
            snprintf(range_str, sizeof(range_str), "%" PRIu64 "-%" PRIu64 "ns", lo, LOCK_TIME_HISTOGRAM_EDGES[i]);
        }

        char bar[31];
        int bar_len = (histogram_max > 0) ? (int)((double)lock_time_histogram[i] * 30.0 / (double)histogram_max) : 0;
        if (bar_len > 30)
            bar_len = 30;
        for (int j = 0; j < bar_len; j++)
            bar[j] = '#';
        bar[bar_len] = '\0';

        printf("  %-20s  %10" PRIu64 "  %6.2f%%  %s\n", range_str, lock_time_histogram[i], pct, bar);
    }
    printf("\n");
}

#endif

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

#ifdef USE_XCP
#define OPTION_PROJECT_NAME "queue_test" // Project name, used to build the A2L and BIN file name
#define OPTION_PROJECT_EPK "V1.0"        // EPK version string
#define OPTION_USE_TCP false             // TCP or UDP
#define OPTION_SERVER_PORT 5555          // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}  // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 32)    // Size of the measurement queue in bytes, should be large enough to cover at least 10ms of expected traffic
#endif

//-----------------------------------------------------------------------------------------------------

// Signal handler for clean shutdown
static volatile bool gRun = true;
static void sig_handler(int sig) { gRun = false; }

//-----------------------------------------------------------------------------------------------------

#ifdef TEST_MC_QUEUE
static McQueueHandle queue_handle = NULL;
#else
static tQueueHandle queue_handle = NULL;
#endif

static atomic_uint_least16_t task_index_ctr = 0;

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

        for (int n = 0; n < THREAD_BURST_SIZE; n++) {

            counter++;

            uint16_t size = THREAD_PAYLOAD_SIZE + rand() % 32; // Add some random size to the payload to increase the variability of the test
#ifdef TEST_ACQUIRE_LOCK_TIMING
            uint64_t start_time = get_timestamp_ns();
#endif
#ifdef TEST_MC_QUEUE
            McQueueBuffer queue_buffer = mc_queue_acquire(queue_handle, (size_t)size);
            if (queue_buffer.size >= (int64_t)size) {
                assert(queue_buffer.buffer != NULL);

                // Test data (MC queue has no XCP transport layer or DAQ header prefix)
                uint64_t *b = (uint64_t *)queue_buffer.buffer;
                b[0] = task_index;
                b[1] = size;
                b[2] = counter;

                mc_queue_push(queue_handle, &queue_buffer);
            }
#else
            tQueueBuffer queue_buffer = queueAcquire(queue_handle, size);
            if (queue_buffer.size >= size) {
                assert(queue_buffer.buffer != NULL);

                // Simulate XCP DAQ header, because some queue implementations is not generic, it has some XCP specific asserts
                // assert(entry->data[4 + 1] == 0xAA || entry->data[4 + 0] >=0xFC))) {
                *(uint32_t *)queue_buffer.buffer = 0x0000AAFC;

                // Test data
                uint64_t *b = (uint64_t *)(queue_buffer.buffer + sizeof(uint32_t));
                b[0] = task_index;
                b[1] = size;
                b[2] = counter;

                queuePush(queue_handle, &queue_buffer, false);
            }
#endif
#ifdef TEST_ACQUIRE_LOCK_TIMING
            lock_test_add_sample(get_timestamp_ns() - start_time, 0);
#endif
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

    // Set log level
    XcpSetLogLevel(OPTION_LOG_LEVEL);
#ifdef TEST_MC_QUEUE
#ifdef MC_USE_XCPLITE_QUEUE
    DBG_PRINT3("queue_test for mc_queue API XCPlite wrapper\n");
#else
    DBG_PRINT3("queue_test for mc_queue API reference implementation\n");
#endif
#else
    DBG_PRINT3("queue_test for XCPlite queue\n");
#ifdef QUEUE_64_VAR_SIZE
    DBG_PRINT3("Using queue (queue64v.c) with 64 bit variable size entries\n");
#elif defined(QUEUE_64_FIX_SIZE)
    DBG_PRINT3("Using queue (queue64f.c) with 64 bit fixed size entries\n");
#elif defined(QUEUE_32)
    DBG_PRINT3("Using queue (queue32.c) with 32 bit variable size entries\n");
#else
    DBG_PRINT3("Using old deprecated queue (queue64.c) with unknown configuration\n");
#endif
#endif

#ifdef TEST_QUEUE_PEEK
    DBG_PRINT3("Testing peek support\n");
#else
    DBG_PRINT3("Testing without peek support\n");
#endif
    DBG_PRINT3("\n");

    DBG_PRINT3("Test parameters:\n");
    DBG_PRINTF3("THREAD_COUNT=%d\n", THREAD_COUNT);
    DBG_PRINTF3("THREAD_BURST_SIZE=%d\n", THREAD_BURST_SIZE);
    DBG_PRINTF3("THREAD_DELAY_US=%d\n", THREAD_DELAY_US);
    DBG_PRINTF3("THREAD_PAYLOAD_SIZE=%zu\n", THREAD_PAYLOAD_SIZE);
    DBG_PRINT3("\n");

    DBG_PRINT3("Queue parameters:\n");
    DBG_PRINTF3("QUEUE_ENTRY_USER_HEADER_SIZE=%d\n", QUEUE_ENTRY_USER_HEADER_SIZE);
#ifndef TEST_MC_QUEUE
    DBG_PRINTF3("QUEUE_ENTRY_USER_PAYLOAD_SIZE=%u\n", QUEUE_ENTRY_USER_PAYLOAD_SIZE);
    DBG_PRINTF3("QUEUE_ENTRY_USER_SIZE=%u\n", QUEUE_ENTRY_USER_SIZE);
    DBG_PRINTF3("QUEUE_SEGMENT_SIZE=%u\n", QUEUE_SEGMENT_SIZE);
    DBG_PRINTF3("QUEUE_MAX_ENTRY_SIZE=%u\n", QUEUE_MAX_ENTRY_SIZE);
    DBG_PRINTF3("QUEUE_PAYLOAD_SIZE_ALIGNMENT=%u\n", QUEUE_PAYLOAD_SIZE_ALIGNMENT);
#endif
    DBG_PRINT3("\n");

#ifdef USE_XCP

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
#endif

#ifdef TEST_MC_QUEUE
    queue_handle = mc_queue_init(QUEUE_SIZE);
#else
    queue_handle = queueInit(QUEUE_SIZE); // Initialize the queue, the queue memory is allocated by the library, the queue buffer size is specified by OPTION_QUEUE_SIZE
#endif
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

#ifdef USE_XCP
    A2lLock();
    DaqCreateEvent(mainloop);
    A2lSetStackAddrMode(mainloop);
    A2lCreateMeasurement(msg_count, "Message count");
    A2lCreateMeasurement(msg_lost, "Messages lost");
    A2lCreateMeasurement(msg_bytes, "Message bytes");
    A2lUnlock();
    sleepUs(200000); // Wait 200ms for the threads to start
    A2lFinalize();   // Manually finalize the A2L file to make it visible without XCP tool connect
#endif

    // Wait for signal to stop
    printf("main loop running - press Ctrl+C to stop...\n");
    while (gRun) {

        // Poll the queue, break if empty

#ifdef TEST_QUEUE_PEEK

        while (gRun) {

#ifdef TEST_MC_QUEUE
            McQueueBuffer buffer[QUEUE_PEEK_MAX_INDEX + 1];
#else
            tQueueBuffer buffer[QUEUE_PEEK_MAX_INDEX + 1];
#endif
            uint32_t buffer_count = 0;

            // Set max max_peek_index to a random number between 0 and QUEUE_PEEK_MAX_INDEX
            uint32_t max_peek_index = rand() % (QUEUE_PEEK_MAX_INDEX + 1);
            for (uint32_t index = 0; index <= max_peek_index; index++) {
#ifdef TEST_MC_QUEUE
                buffer[index] = mc_queue_peak(queue_handle, (int64_t)index);
#else
                uint32_t lost = 0;
                buffer[index] = queuePeek(queue_handle, index, &lost);
                msg_lost += lost;
#endif
                if (buffer[index].size == 0) { // Empty buffer, no more messages in the queue
                    break;
                }
                buffer_count++;
                assert(buffer[index].buffer != NULL);
                assert(buffer[index].size >= THREAD_PAYLOAD_SIZE);
                assert((uint64_t)buffer[index].buffer % 2 == 0);

                // Check test data
#ifdef TEST_MC_QUEUE
                // MC queue: no transport layer or XCP DAQ header prefix – test data starts at offset 0
                uint64_t *b = (uint64_t *)buffer[index].buffer;
#else
                // Test payload starts + (User header (Transport layer header) + faked XCP DAQ header)
                uint64_t *b = (uint64_t *)(buffer[index].buffer + QUEUE_ENTRY_USER_HEADER_SIZE + 4);
#endif
                uint64_t task_index = b[0];
                uint64_t size = b[1];
                uint64_t counter = b[2];

                // printf("Peeked index %u: task_index=%llu, size=%llu, counter=%llu\n", index, task_index, size, counter);

                // Check counter incrementing
                assert(size >= THREAD_PAYLOAD_SIZE);
                assert(task_index < THREAD_COUNT);
                if (msg_count > 0) {
                    if (counter != last_counter[task_index] + 1) {
                        printf("Messages lost in task %u, expected counter %llu, got %llu\n", (uint32_t)task_index, last_counter[task_index] + 1, counter);
                    }
                }
                last_counter[task_index] = counter;

                // Write to the user header
#if QUEUE_ENTRY_USER_HEADER_SIZE >= 4
                uint32_t *e = (uint32_t *)(buffer[index].buffer);
                *e = 0xFFFFFFFF;
#endif

                msg_count++;
                msg_bytes += buffer[index].size;
            }

            if (buffer_count == 0) {
                break; // No more messages in the queue
            }

            // Release the buffers obtained by queuePeek / mc_queue_peak so far
            for (uint32_t i = 0; i < buffer_count; i++) {
                assert(buffer[i].size > 0);
#ifdef TEST_MC_QUEUE
                mc_queue_release(queue_handle, &buffer[i]);
#else
                queueRelease(queue_handle, &buffer[i]);
#endif
            }

        } // for (;;)

#else

#ifdef TEST_MC_QUEUE
        for (;;) {
            McQueueBuffer buffer = mc_queue_pop(queue_handle);
            if (buffer.size == 0)
                break;

            assert(buffer.buffer != NULL);
            assert(buffer.size >= (int64_t)THREAD_PAYLOAD_SIZE);
            assert((uint64_t)buffer.buffer % 2 == 0);

            // Test data (MC queue: no header prefix, data starts at offset 0)
            uint64_t *b = (uint64_t *)buffer.buffer;
            uint64_t task_index = b[0];
            uint64_t size = b[1];
            uint64_t counter = b[2];

            assert(size >= THREAD_PAYLOAD_SIZE);
            assert(task_index < THREAD_COUNT);
            if (msg_count > 0) {
                if (counter != last_counter[task_index] + 1) {
                    printf("Messages lost in task %u, expected counter %llu, got %llu\n", (uint32_t)task_index, last_counter[task_index] + 1, counter);
                }
            }
            last_counter[task_index] = counter;

            msg_count++;
            msg_bytes += (uint32_t)buffer.size;

            mc_queue_release(queue_handle, &buffer);
        } // for (;;)
#else
        for (;;) {

            uint32_t lost = 0;
            tQueueBuffer segment_buffer = queuePop(queue_handle, true, false, &lost); // May accumulate multiple messages in one segment (message has a transport layer header)
            msg_lost += lost;
            if (segment_buffer.size == 0)
                break;

            uint32_t segment_size = segment_buffer.size;
            tQueueBuffer buffer;
            buffer.size = *(uint16_t *)segment_buffer.buffer + sizeof(uint32_t); // Get the buffer size from transportlayer header dlc
            buffer.buffer = segment_buffer.buffer;                               // Move the buffer pointer to the start of the message payload (to the transport layer header)
            assert(buffer.size > 0);

            // Iterate over all messages in the segment
            for (;;) {

                assert(buffer.buffer != NULL);
                assert(buffer.size >= THREAD_PAYLOAD_SIZE);
                assert((uint64_t)buffer.buffer % 2 == 0);

                uint64_t *b = (uint64_t *)(buffer.buffer + 8); // Test payload starts + 8 (Transport layer header + XCP DAQ header)
                uint64_t task_index = b[0];
                uint64_t size = b[1];
                uint64_t counter = b[2];

                assert(size >= THREAD_PAYLOAD_SIZE);
                assert(task_index < THREAD_COUNT);
                if (msg_count > 0) {
                    if (counter != last_counter[task_index] + 1) {
                        printf("Messages lost in task %u, expected counter %llu, got %llu\n", (uint32_t)task_index, last_counter[task_index] + 1, counter);
                    }
                }

                last_counter[task_index] = counter;

                msg_count++;
                msg_bytes += buffer.size;

                assert(segment_size >= buffer.size);
                segment_size -= buffer.size;
                if (segment_size == 0) {
                    queueRelease(queue_handle, &segment_buffer);
                    break; // No more messages in the segment
                }

                buffer.buffer += buffer.size;                                // Move to the next message in the segment (include the transport layer header size)
                buffer.size = *(uint16_t *)buffer.buffer + sizeof(uint32_t); // Get the buffer size from transportlayer header dlc

            } // for (;;)
        } // for (;;)
#endif // TEST_MC_QUEUE

#endif

#ifdef USE_XCP
        DaqTriggerEvent(mainloop);
#endif

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

    // Deinitialize the queue
#ifdef TEST_MC_QUEUE
    mc_queue_deinit(queue_handle);
#else
    queueDeinit(queue_handle); // Deinitialize the queue
#endif

// Print queue statistics
#ifdef TEST_ACQUIRE_LOCK_TIMING
    lock_test_print_results();
#endif

#ifdef USE_XCP
    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
#endif
    return 0;
}
