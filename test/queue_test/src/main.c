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

// SHM two-process test - available with TEST_MC_QUEUE (both native MC and xcplite wrapper)
#if defined(TEST_MC_QUEUE)
#define TEST_QUEUE_SHM
#include <errno.h>    // for errno, strerror, ESRCH
#include <fcntl.h>    // for shm_open, O_CREAT, O_RDWR
#include <signal.h>   // for kill() - used to probe consumer liveness (signal 0 = existence check)
#include <sys/mman.h> // for mmap, munmap, MAP_SHARED, MAP_FAILED
#include <sys/stat.h> // for S_IRUSR, S_IWUSR
#include <unistd.h>   // for close, ftruncate, getpid()
#endif

//-----------------------------------------------------------------------------------------------------

// Use the logger from XCPlite
// Note: If logging enabled with log level 6 OPTION_MAX_DBG_LEVEL must be set to 6
#define OPTION_LOG_LEVEL 5 // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug, 5 = trace, 6 = verbose

#define QUEUE_SIZE (1024 * 256) // Size of the test queue in bytes

// Test parameters
// 64 byte payload  * THREAD_COUNT * 1000000/THREAD_DELAY_US = Throughput in byte/s

// Parameters for 2000000 msg/s with 10 threads, 64 byte payload, 10us delay
#define THREAD_COUNT 10                            // Number of threads to create
#define MAX_PRODUCERS 8                            // Max concurrent producer processes (SHM mode); also bounds last_counter[] in single-process mode
#define THREAD_DELAY_US 10                         // Delay in microseconds for the thread loops
#define THREAD_BURST_SIZE 2                        // Acquire and push this many entries in a burst before sleeping
#define THREAD_PAYLOAD_SIZE (4 * sizeof(uint64_t)) // Size of the test payload produced by the threads

// The queue implementations in reference.c, queue62v.c and queue64f.c support peeking ahead
#if defined(OPTION_QUEUE_64_VAR_SIZE) || defined(OPTION_QUEUE_64_FIX_SIZE)
#define TEST_QUEUE_PEEK          // Use queuePeek(random(QUEUE_PEEK_MAX_INDEX)) instead of queuePop
#define QUEUE_PEEK_MAX_INDEX (8) // Max offset for peeking ahead
#endif

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Acquire timing test

// Queue acquire + push timing
// For high contention use test queue_test or example daq_test with xcp_client --upload-a2l --udp --mea .  --dest-addr 192.168.0.206
// Note that this tests have significant performance impact, do not turn on for production use !!!!!!!!!!!

#ifdef TEST_ACQUIRE_LOCK_TIMING

static MUTEX lock_mutex = MUTEX_INTIALIZER;
static uint64_t lock_time_max = 0;
static uint64_t lock_time_sum = 0;
static uint64_t lock_count = 0;

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

static void lock_test_add_sample(uint64_t d) {
    mutexLock(&lock_mutex);

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
    printf("\nProducer acquire + push timing statistics:\n");
    printf("  count=%" PRIu64 "  max=%" PRIu64 "ns  avg=%" PRIu64 "ns\n", lock_count, lock_time_max, lock_time_sum / lock_count);

    uint64_t histogram_sum = 0;
    for (int i = 0; i < LOCK_TIME_HISTOGRAM_SIZE; i++)
        histogram_sum += lock_time_histogram[i];
    uint64_t histogram_max = 0;
    for (int i = 0; i < LOCK_TIME_HISTOGRAM_SIZE; i++)
        if (lock_time_histogram[i] > histogram_max)
            histogram_max = lock_time_histogram[i];

    printf("\nHistogram (%" PRIu64 " events):\n", histogram_sum);
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

Producer acquire+push time statistics:
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


  Producer acquire+push time statistics:
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
// Create or attach to a queue in shared memory for inter-process communication between a producer and consumer process

// Mode flags set at startup from command-line arguments
static bool g_shm_producer = false;   // This process is a producer: attach to consumer-created queue
static bool g_shm_consumer = false;   // This process is the consumer: create and own the queue
static uint16_t g_producer_index = 0; // Claimed from SHM header in --producer mode (0 = single-process or first producer)

#ifdef TEST_QUEUE_SHM

#define SHM_NAME "/queue_test_shm"      // POSIX shared memory object name
#define SHM_LOCK "/tmp/queue_test_lock" // flock-based lock file for race-free SHM creation
#define SHM_OVERHEAD (16 * 1024)        // Overhead for QueueHeader + McQueue internals (64+8208 bytes used, 16KB reserved)

// Small header prepended to the SHM region (before queue data).
// consumer_pid: set to getpid() by the consumer after queue init, cleared to 0 on graceful exit.
// Producers probe liveness with kill(consumer_pid, 0): ESRCH means the process is gone.
// This detects both graceful termination and crashes, since the OS reclaims the PID on death.
typedef struct {
    atomic_int_least32_t consumer_pid;        // PID of the owning consumer process (0 = no consumer)
    atomic_uint_least32_t producer_index_ctr; // Next producer-slot counter, claimed by each attaching producer via fetch_add
    uint32_t pad[14];                         // pad to 64 bytes (one cache line)
} tShmHeader;

// SHM layout: [tShmHeader (64 B)] [queue memory (QUEUE_SIZE + SHM_OVERHEAD)]
#define SHM_HEADER_SIZE ((size_t)sizeof(tShmHeader))
#define SHM_SIZE (SHM_HEADER_SIZE + QUEUE_SIZE + SHM_OVERHEAD)

// Pointers into the mmap'd SHM region
static void *g_shm_mem = NULL;       // start of mmap'd SHM (== tShmHeader*)
static tShmHeader *g_shm_hdr = NULL; // convenience alias for the header

// Race-free SHM open: uses acquire_lock (flock) to serialize creation.
// consumer=true  (called by --consumer): (re)creates the SHM object, inits the queue,
//                                        sets consumer_alive = 1.
// consumer=false (called by --producer): opens an existing SHM object and attaches;
//                                        returns NULL if consumer hasn't created it yet.
// Returns a queue handle on success, NULL on failure.
static McQueueHandle queue_open_shm(bool consumer) {

    int lock_fd = acquire_lock(SHM_LOCK);
    if (lock_fd < 0) {
        printf("queue_open_shm: acquire_lock failed\n");
        return NULL;
    }

    int shm_fd;

    if (consumer) {
        shm_unlink(SHM_NAME); // remove any stale object from a previous run
        shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if (shm_fd < 0) {
            printf("CONSUMER: shm_open failed: %s\n", strerror(errno));
            release_lock(lock_fd);
            return NULL;
        }
        if (ftruncate(shm_fd, (off_t)SHM_SIZE) < 0) {
            printf("CONSUMER: ftruncate failed: %s\n", strerror(errno));
            close(shm_fd);
            shm_unlink(SHM_NAME);
            release_lock(lock_fd);
            return NULL;
        }
    } else {
        // Producer: open existing – return NULL if consumer hasn't created it yet
        shm_fd = shm_open(SHM_NAME, O_RDWR, 0);
        if (shm_fd < 0) {
            release_lock(lock_fd);
            return NULL;
        }
    }

    void *mem = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    release_lock(lock_fd);

    if (mem == MAP_FAILED) {
        printf("%s: mmap failed: %s\n", consumer ? "CONSUMER" : "PRODUCER", strerror(errno));
        if (consumer)
            shm_unlink(SHM_NAME);
        return NULL;
    }

    // consumer_alive is at offset 0 in SHM (before the queue data)
    // The queue data sits after the tShmHeader
    tShmHeader *hdr = (tShmHeader *)mem;
    void *queue_mem = (uint8_t *)mem + SHM_HEADER_SIZE;
    size_t queue_size = SHM_SIZE - SHM_HEADER_SIZE;

    if (consumer) {
        // Full init: clear queue structure then signal producers
        McQueueHandle h = mc_queue_init_from_memory(queue_mem, queue_size, true, NULL);
        if (h == NULL) {
            printf("CONSUMER: mc_queue_init_from_memory failed\n");
            munmap(mem, SHM_SIZE);
            shm_unlink(SHM_NAME);
            return NULL;
        }
        g_shm_mem = mem;
        g_shm_hdr = hdr;
        // Reset producer-slot counter (fresh SHM is zero-filled, but be explicit)
        atomic_store_explicit(&g_shm_hdr->producer_index_ctr, 0, memory_order_relaxed);
        // Publish PID AFTER queue is fully initialized so producers can safely attach
        atomic_store_explicit(&g_shm_hdr->consumer_pid, (int32_t)getpid(), memory_order_release);
        printf("CONSUMER: queue in shared memory '%s' (%u KB)\n", SHM_NAME, (unsigned)(SHM_SIZE / 1024));
        return h;

    } else {
        // Producer attach: check consumer_pid FIRST, before touching queue internals.
        // Consumer publishes its PID only after full queue init, so pid!=0 means the
        // queue is ready AND the consumer is currently alive.
        // If pid==0 the queue may not be initialized yet -> retry.
        if (atomic_load_explicit(&hdr->consumer_pid, memory_order_acquire) == 0) {
            munmap(mem, SHM_SIZE); // consumer not ready yet, retry
            return NULL;
        }
        McQueueHandle h = mc_queue_init_from_memory(queue_mem, queue_size, false, NULL);
        if (h == NULL) {
            printf("PRODUCER: mc_queue_init_from_memory failed\n");
            munmap(mem, SHM_SIZE);
            return NULL;
        }
        g_shm_mem = mem;
        g_shm_hdr = hdr;
        // Claim a unique sequential producer index (0, 1, 2, ...).  Used to build a flat thread_id
        // in the task threads: thread_id = producer_index * THREAD_COUNT + task_index.
        g_producer_index = (uint16_t)atomic_fetch_add_explicit(&g_shm_hdr->producer_index_ctr, 1u, memory_order_relaxed);
        printf("PRODUCER: attached to queue in shared memory '%s' (%u KB) as producer[%u]\n", SHM_NAME, (unsigned)(SHM_SIZE / 1024), (unsigned)g_producer_index);
        return h;
    }
}

#endif // TEST_QUEUE_SHM

//-----------------------------------------------------------------------------------------------------

#ifdef TEST_MC_QUEUE
static McQueueHandle queue_handle = NULL;
#else
static tQueueHandle queue_handle = NULL;
#endif

static atomic_uint_least16_t task_index_ctr = 0;

// Task function that runs in a separate thread
// Simulates a producer that acquires buffers from the queue, fills them with test data and pushes them to the queue
#ifdef _WIN32 // Windows 32 or 64 bit
DWORD WINAPI task(LPVOID p)
#else
void *task(void *p)
#endif
{
    bool run = true;

    // Task local measurement variables on stack
    uint64_t counter = 0;

    // Build the task name from the event index
    uint16_t task_index = atomic_fetch_add(&task_index_ctr, 1);
    // Flat thread ID unique across all producer processes: each producer gets a sequential
    // producer_index, so thread IDs never collide even with multiple concurrent producers.
    uint16_t thread_id = (uint16_t)(g_producer_index * THREAD_COUNT) + task_index;
    char task_name[16 + 1];
    snprintf(task_name, sizeof(task_name), "task_%u", task_index);

    printf("thread %s running...\n", task_name);

    while (run && gRun) {
        // Consumer liveness is checked in the main loop (every 500us) which sets gRun=false.
        // No per-thread kill() check here -- that would add 1M syscalls/s with 10 threads at 10us.

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
                b[0] = thread_id;
                b[1] = size;
                b[2] = counter;

                mc_queue_push(queue_handle, &queue_buffer);
            }
#else
            tQueueBuffer queue_buffer = queueAcquire(queue_handle, size);
            if (queue_buffer.size >= size) {
                assert(queue_buffer.buffer != NULL);

                // Simulate XCP DAQ header, because some queue implementations are not generic and have XCP specific asserts
                *(uint32_t *)queue_buffer.buffer = 0x0000AAFC;

                // Test data
                uint64_t *b = (uint64_t *)(queue_buffer.buffer + sizeof(uint32_t));
                b[0] = thread_id;
                b[1] = size;
                b[2] = counter;

                queuePush(queue_handle, &queue_buffer, false);
            }
#endif

#ifdef TEST_ACQUIRE_LOCK_TIMING
            lock_test_add_sample(get_timestamp_ns() - start_time);
#endif
        }

        // Sleep for the specified delay parameter in microseconds, defines the approximate sampling rate
        sleepUs(THREAD_DELAY_US);
    }

    return 0; // Exit the thread
}

//-----------------------------------------------------------------------------------------------------
// Main function

static void print_test_info(void) {

#ifdef TEST_MC_QUEUE
#ifdef MC_USE_XCPLITE_QUEUE
    DBG_PRINT3("queue_test for mc_queue API XCPlite wrapper\n");
#else
    DBG_PRINT3("queue_test for mc_queue API reference implementation\n");
#endif
#ifdef TEST_QUEUE_SHM
    if (g_shm_consumer)
        DBG_PRINT3("MODE: consumer (creates shared memory queue, runs consumer)\n");
    else if (g_shm_producer)
        DBG_PRINT3("MODE: producer (attaches to shared memory queue, runs producers)\n");
    else
        DBG_PRINT3("MODE: single-process (in-process queue)\n");
#endif
#else
    DBG_PRINT3("queue_test for XCPlite queue\n");
#if defined(OPTION_QUEUE_64_VAR_SIZE)
    DBG_PRINT3("Using queue (queue64v.c) with 64 bit variable size entries\n");
#elif defined(OPTION_QUEUE_64_FIX_SIZE)
    DBG_PRINT3("Using queue (queue64f.c) with 64 bit fixed size entries\n");
#elif defined(OPTION_QUEUE_32)
    DBG_PRINT3("Using queue (queue32.c) with 32 bit variable size entries\n");
#else
#error "Please select a valid queue implementation\n"
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
}

static void print_help(void) {

#ifdef TEST_QUEUE_SHM

    printf("Usage: queue_test [--consumer | --producer | --help]\n\n");
    printf("  (no args)    Single-process test: producers and consumer in one process\n");
    printf("  --consumer   Two-process test: create shared memory queue and run consumer\n");
    printf("               Start this first, then start --producer(s) in separate terminals\n");
    printf("  --producer   Two-process test: attach to consumer-created queue and run producers\n");
    printf("               Multiple --producer processes can run concurrently\n");
    printf("               Exits gracefully when consumer terminates or crashes\n");
    printf("  --help, -h   Show this help\n\n");
    printf("Queue implementation:\n");
#ifdef MC_USE_XCPLITE_QUEUE
    printf("  mc_queue API -> XCPlite queue64v wrapper (MC_USE_XCPLITE_QUEUE)\n");
#else
    printf("  mc_queue API -> MC reference implementation\n");
#endif
    printf("  Shared memory: %s  (%u KB)\n", SHM_NAME, (unsigned)(SHM_SIZE / 1024));
    printf("  Queue size:    %u bytes\n", QUEUE_SIZE);
    printf("  Threads:       %d producers, payload %zu bytes, burst %d, delay %d us\n", THREAD_COUNT, (size_t)THREAD_PAYLOAD_SIZE, THREAD_BURST_SIZE, THREAD_DELAY_US);

#else

    printf("Usage: queue_test [--help]\n\n");
    printf("  Single-process queue stress test (no shared memory support in this build)\n\n");
    printf("Queue implementation:\n");
#ifdef TEST_MC_QUEUE
#ifdef MC_USE_XCPLITE_QUEUE
    printf("  mc_queue API -> XCPlite queue64v wrapper (MC_USE_XCPLITE_QUEUE)\n");
#else
    printf("  mc_queue API -> MC reference implementation\n");
#endif
#else
#if defined(OPTION_QUEUE_64_VAR_SIZE)
    printf("  XCPlite queue64v (64-bit variable-size entries)\n");
#elif defined(OPTION_QUEUE_64_FIX_SIZE)
    printf("  XCPlite queue64f (64-bit fixed-size entries)\n");
#elif defined(OPTION_QUEUE_32)
    printf("  XCPlite queue32 (32-bit variable-size entries)\n");
#else
    printf("  XCPlite queue (legacy queue64)\n");
#endif
#endif
    printf("  Queue size:    %u bytes\n", QUEUE_SIZE);
    printf("  Threads:       %d producers, payload %zu bytes, burst %d, delay %d us\n", THREAD_COUNT, (size_t)THREAD_PAYLOAD_SIZE, THREAD_BURST_SIZE, THREAD_DELAY_US);

#endif
}

int main(int argc, char *argv[]) {

    printf("\nqueue_test\n");
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Commandline argument parsing for test mode selection
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            return 0;
        }
#ifdef TEST_QUEUE_SHM
        if (strcmp(argv[i], "--producer") == 0)
            g_shm_producer = true;
        else if (strcmp(argv[i], "--consumer") == 0)
            g_shm_consumer = true;
#endif
        else {
            printf("Unknown option: %s  (use --help for usage)\n", argv[i]);
            return 1;
        }
    }
    if (g_shm_producer && g_shm_consumer) {
        printf("Error: --producer and --consumer are mutually exclusive\n");
        return 1;
    }

    // Set log level
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Print info
    print_test_info();

#ifdef USE_XCP
    if (!g_shm_producer) {
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
    }
#endif

// Create or attach to a queue, depending on the test mode
#ifdef TEST_MC_QUEUE
#ifdef TEST_QUEUE_SHM
    if (g_shm_consumer || g_shm_producer) {
        if (g_shm_consumer) {
            // Consumer creates and owns the queue
            queue_handle = queue_open_shm(true);
            if (queue_handle == NULL) {
                printf("CONSUMER: failed to create shared memory queue\n");
                return 1;
            }
        } else { // producer
            // Producers attach – wait for consumer to create the queue first
            printf("PRODUCER: waiting for consumer to create shared memory queue...\n");
            for (int retry = 0; retry < 100 && gRun; retry++) {
                queue_handle = queue_open_shm(false);
                if (queue_handle != NULL)
                    break;
                sleepUs(100000); // 100ms between retries, 10s total timeout
            }
            if (queue_handle == NULL) {
                printf("PRODUCER: timeout waiting for consumer (10s elapsed)\n");
                return 1;
            }
        }
    } else {
        queue_handle = mc_queue_init(QUEUE_SIZE);
    }
#else
    queue_handle = mc_queue_init(QUEUE_SIZE);
#endif
#else
    queue_handle = queueInit(QUEUE_SIZE); // Initialize the queue, the queue memory is allocated by the library, the queue buffer size is specified by OPTION_QUEUE_SIZE
#endif
    if (queue_handle == NULL) {
        printf("Failed to initialize the queue\n");
        return 1;
    }

    // Create multiple instances of the produces tasks (not in consumer only mode)
    THREAD t[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        t[i] = 0;
    }
    if (!g_shm_consumer) { // consumer-only process has no producer threads
        for (int i = 0; i < THREAD_COUNT; i++) {
            create_thread(&t[i], task);
        }
    }

    // Local variables for the consumer loop
    uint32_t msg_count = 0;
    uint32_t msg_lost = 0;
    uint32_t msg_bytes = 0;
    uint64_t last_msg_time = clockGetMonotonicUs();
    uint32_t last_msg_count = 0;
    uint32_t last_msg_bytes = 0;
    uint64_t last_counter[MAX_PRODUCERS * THREAD_COUNT];
    memset(last_counter, 0, sizeof(last_counter));

// Create XCP DAQ measurements
#ifdef USE_XCP
    if (!g_shm_producer) {
        A2lLock();
        DaqCreateEvent(mainloop);
        A2lSetStackAddrMode(mainloop);
        A2lCreateMeasurement(msg_count, "Message count");
        A2lCreateMeasurement(msg_lost, "Messages lost");
        A2lCreateMeasurement(msg_bytes, "Message bytes");
        A2lUnlock();
        sleepUs(200000); // Wait 200ms for the threads to start
        A2lFinalize();   // Manually finalize the A2L file to make it visible without XCP tool connect
    }
#endif

    // Wait for signal to stop
    printf("main loop running - press Ctrl+C to stop...\n");
    while (gRun) {

        // Poll the queue, break if empty
        // In SHM producer mode the consumer runs in a separate process: skip polling here
#ifdef TEST_QUEUE_SHM
        if (!g_shm_producer) {
#endif

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
                    buffer[index] = queuePeek(queue_handle, index, &lost, NULL);
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
                    uint64_t thread_id = b[0];
                    uint64_t size = b[1];
                    uint64_t counter = b[2];

                    // printf("Peeked index %u: thread_id=%llu, size=%llu, counter=%llu\n", index, thread_id, size, counter);

                    // Check counter incrementing
                    assert(size >= THREAD_PAYLOAD_SIZE);
                    assert(thread_id < MAX_PRODUCERS * THREAD_COUNT);
                    if (msg_count > 0) {
                        if (counter != last_counter[thread_id] + 1) {
                            printf("Messages lost in thread %u, expected counter %llu, got %llu\n", (uint32_t)thread_id, last_counter[thread_id] + 1, counter);
                        }
                    }
                    last_counter[thread_id] = counter;

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
        // mc_queue consumer loop
        for (;;) {
            McQueueBuffer buffer = mc_queue_pop(queue_handle);
            if (buffer.size == 0)
                break;

            assert(buffer.buffer != NULL);
            assert(buffer.size >= (int64_t)THREAD_PAYLOAD_SIZE);
            assert((uint64_t)buffer.buffer % 2 == 0);

            // Test data (MC queue: no header prefix, data starts at offset 0)
            uint64_t *b = (uint64_t *)buffer.buffer;
            uint64_t thread_id = b[0];
            uint64_t size = b[1];
            uint64_t counter = b[2];

            assert(size >= THREAD_PAYLOAD_SIZE);
            assert(thread_id < MAX_PRODUCERS * THREAD_COUNT);
            if (msg_count > 0) {
                if (counter != last_counter[thread_id] + 1) {
                    printf("Messages lost in thread %u, expected counter %llu, got %llu\n", (uint32_t)thread_id, last_counter[thread_id] + 1, counter);
                }
            }
            last_counter[thread_id] = counter;

            msg_count++;
            msg_bytes += (uint32_t)buffer.size;

            mc_queue_release(queue_handle, &buffer);
        } // for (;;)
#else  // TEST_MC_QUEUE

        // XCPlite queue consumer loop
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
                uint64_t thread_id = b[0];
                uint64_t size = b[1];
                uint64_t counter = b[2];

                assert(size >= THREAD_PAYLOAD_SIZE);
                assert(thread_id < MAX_PRODUCERS * THREAD_COUNT);
                if (msg_count > 0) {
                    if (counter != last_counter[thread_id] + 1) {
                        printf("Messages lost in thread %u, expected counter %llu, got %llu\n", (uint32_t)thread_id, last_counter[thread_id] + 1, counter);
                    }
                }

                last_counter[thread_id] = counter;

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
#endif // !TEST_MC_QUEUE

#endif

#ifdef TEST_QUEUE_SHM
        } // if (!g_shm_producer)
#endif

#ifdef USE_XCP
        if (!g_shm_producer)
            DaqTriggerEvent(mainloop);
#endif

        sleepUs(500); // 500us

        // Producer mode: check consumer liveness once per main loop iteration.
        // kill(pid, 0) with ESRCH means the consumer process is gone (graceful or crash).
        // Set gRun=false to exit the main loop and join all producer threads.
#ifdef TEST_QUEUE_SHM
        if (g_shm_producer && g_shm_hdr != NULL) {
            int32_t cpid = atomic_load_explicit(&g_shm_hdr->consumer_pid, memory_order_relaxed);
            if (cpid == 0 || (kill((pid_t)cpid, 0) == -1 && errno == ESRCH)) {
                printf("PRODUCER: consumer gone (pid=%d), shutting down\n", (int)cpid);
                gRun = false;
            }
        }
#endif

        // Print statistics every second
        if (clockGetMonotonicUs() - last_msg_time >= 1000000) {
            if (!g_shm_producer) {
                printf("Messages received: %u, bytes received: %u, messages lost: %u, data rate: %u msg/s, %u kbytes/s\n", msg_count, msg_bytes, msg_lost,
                       (msg_count - last_msg_count), (msg_bytes - last_msg_bytes) / 1024);
                last_msg_bytes = msg_bytes;
                last_msg_count = msg_count;
            }
            last_msg_time = clockGetMonotonicUs();
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

// Unmap shared memory; consumer signals producers to stop, then removes the SHM object
#ifdef TEST_QUEUE_SHM
    if (g_shm_consumer && g_shm_hdr != NULL) {
        // Clear PID so producers detect the graceful exit immediately via the pid==0 fast path.
        // (They would also detect it via kill()/ESRCH once this process exits, but clearing
        // first lets them stop before the 500ms drain window expires.)
        atomic_store_explicit(&g_shm_hdr->consumer_pid, 0, memory_order_release);
        printf("CONSUMER: signaled producers to stop, waiting 500ms...\n");
        sleepUs(500000);
    }
    if (g_shm_mem != NULL) {
        munmap(g_shm_mem, SHM_SIZE);
        g_shm_mem = NULL;
        g_shm_hdr = NULL;
    }
    if (g_shm_consumer) {
        shm_unlink(SHM_NAME);
        printf("CONSUMER: shared memory '%s' removed\n", SHM_NAME);
    }
#endif

// Print queue statistics
#ifdef TEST_ACQUIRE_LOCK_TIMING
    if (!g_shm_consumer) {
        lock_test_print_results();
    }
#endif

#ifdef USE_XCP
    if (!g_shm_producer) {
        XcpDisconnect();        // Force disconnect the XCP client
        A2lFinalize();          // Finalize A2L generation, if not done yet
        XcpEthServerShutdown(); // Stop the XCP server
    }
#endif
    return 0;
}
