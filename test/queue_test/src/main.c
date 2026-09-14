// queue_test

#define _GNU_SOURCE // for CLOCK_MONOTONIC_RAW, CLOCK_THREAD_CPUTIME_ID, clock_gettime

#include <assert.h>  // for assert
#include <math.h>    // for M_PI, sin
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <stdlib.h>  // for rand()
#include <string.h>  // for sprintf
#include <time.h>    // for clock_gettime, CLOCK_MONOTONIC_RAW

#include "xcplib_cfg.h"
#ifndef OPTION_ATOMIC_EMULATION
#include <stdatomic.h> // for atomic_
#endif

// Note: Take care for include order

// Public XCPlite API
#include "xcplib_cfg.h" // for OPTION_xxx

#include "platform.h" // for THREAD_HANDLE, MUTEX, THREAD_FUNC_RETURN, create_thread, cancel_thread, sleepUs, clockGetMonotonicNs, ...

// Option XCP server for online performance monitoring and logging of the queue test
#ifdef USE_XCP
#include "a2l.h"    // for A2l generation application programming interface
#include "xcplib.h" // for application programming interface
#endif

// Use the logger from XCPlite but don't include the rest of the API
#include "dbg_print.h"
void XcpSetLogLevel(uint8_t level);

// Internal libxcplite includes
#include "../src/queue.h"

//-----------------------------------------------------------------------------------------------------

// Use the logger from XCPlite
// Note: If logging enabled with log level 6 OPTION_MAX_DBG_LEVEL must be set to 6
#define OPTION_LOG_LEVEL 3 // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug, 5 = trace, 6 = verbose

// Test parameters
// Default for 2M messages/s and 1G byte payload/s, reduce for testing on slower systems

#define QUEUE_SIZE (1024 * 64) // Size of the test queue in bytes

#define THREAD_COUNT 10     // Number of threads to create
#define THREAD_DELAY_US 10  // Delay in microseconds for the thread loops
#define THREAD_BURST_SIZE 4 // Acquire and push this many entries in a burst before sleeping (stop bursting on overrun)
// Min size of the payload produced by the threads
#define THREAD_PAYLOAD_MIN_SIZE 64
// Max size of the payload produced by the threads (random)
// #define THREAD_PAYLOAD_MAX_SIZE 64 // Small payload test
#define THREAD_PAYLOAD_MAX_SIZE QUEUE_ENTRY_USER_PAYLOAD_SIZE // Big payload test

// The queue implementations in queue62v.c and queue64f.c support peeking ahead
#if defined(OPTION_QUEUE_64_VAR_SIZE) || defined(OPTION_QUEUE_64_FIX_SIZE)
#define TEST_QUEUE_PEEK          // Use queuePeek(random(QUEUE_PEEK_MAX_INDEX)) instead of queuePop
#define QUEUE_PEEK_MAX_INDEX (8) // Max offset for peeking ahead
#else
#endif

// Overruns during the test are intentionally provoked by producing faster than consuming to test the behavior of the queue in this case
#define CONSUMER_SLEEP_ON_EMPTY_QUEUE_US 1 // Start consumer sleep time in microseconds when queue was empty (incremented over time until queue level increases to 90%)

// Option to enable timing measurement
#define TEST_ACQUIRE_LOCK_TIMING

// Define the clock type used for lock time measurement
// Use CLOCK_THREAD_CPUTIME_ID to measure the CPU time consumed by the producer thread, which is more accurate for short lock times and not affected by OS scheduling and preemption
// (which causes the long tail in the lock time histogram on POSIX systems)
// #define TEST_CLOCK_TYPE CLOCK_THREAD_CPUTIME_ID
#define TEST_CLOCK_TYPE CLOCK_MONOTONIC_RAW

// Be aware, that the 2 clocks may have significantly different runtime and jitter depending on the platform

/* AI statement to this:
CLOCK_MONOTONIC_RAW is served via the vDSO (virtual Dynamic Shared Object):
the kernel maps a small piece of code + data directly into every process's address space,
so clock_gettime reads the hardware counter (ARM cycle counter on Pi, TSC on x86) entirely in userspace — no syscall, no context switch.
Typical cost: ~20–50 ns.

CLOCK_THREAD_CPUTIME_ID cannot use the vDSO because the per-thread CPU accounting data lives in kernel data structures (task_struct).
It must do a full syscall (clock_gettime → trap → kernel → return). Typical cost: ~200–500 ns on a Raspberry Pi.

So for measuring lock contention times (which can be as short as 20–100 ns), CLOCK_THREAD_CPUTIME_ID is counterproductive —
its own overhead is larger than what you're trying to measure,
and the lock_calibration loop in your code tries to compensate for this but can only subtract the average overhead, not jitter.

CLOCK_MONOTONIC_RAW is the better choice for this use case — it's both faster and has lower jitter.
The tradeoff is that it is affected by OS preemption (long tail in the histogram), which is actually informative for a lock latency test, not a problem.
*/

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Acquire timing test

// Queue acquire + push timing
// For high contention use test queue_test or example daq_test with xcpclient --upload-a2l --udp --mea .  --dest-addr 192.168.0.206
// Note that this tests have significant performance impact, do not turn on for production use !!!!!!!!!!!

#ifdef TEST_ACQUIRE_LOCK_TIMING

#ifdef _WIN

#define get_lock_test_timestamp clockGetMonotonicNs
#else

// Get the clock used for lock time measurement
static uint64_t get_lock_test_timestamp(void) {
    static const uint64_t kNanosecondsPerSecond = 1000000000ULL;
    struct timespec ts = {0};
    clock_gettime(TEST_CLOCK_TYPE, &ts); // NOLINT(missing-includes) // do **not** include internal "bits" headers directly.
    return ((uint64_t)ts.tv_sec) * kNanosecondsPerSecond + ((uint64_t)ts.tv_nsec);
}

#endif

static MUTEX lock_mutex = MUTEX_INTIALIZER;
static uint64_t lock_time_max = 0;
static uint64_t lock_time_sum = 0;
static uint64_t lock_count = 0;
static uint64_t lock_calibration = 0; // Calibration value for the overhead of the timing measurement itself, to get more accurate results for short lock times

// Variable-width lock timing histogram
// Fine granularity for short latencies, coarser for long-tail latencies
// Bin[i] counts events where EDGES[i-1] <= t < EDGES[i]; bin[SIZE-1] is the overflow (>EDGES[SIZE-2])
#define LOCK_TIME_HISTOGRAM_SIZE 26
static const uint64_t LOCK_TIME_HISTOGRAM_EDGES[LOCK_TIME_HISTOGRAM_SIZE - 1] = {
    10, 20, 40, 80, 120, 160, 200, 300, 400, 500, 600, 800, 1000, 1500, 2000, 3000, 4000, 6000, 8000, 10000, 20000, 40000, 80000, 160000, 320000,
};
static uint64_t lock_time_histogram[LOCK_TIME_HISTOGRAM_SIZE] = {0};

static void lock_test_init(void) {
    memset(lock_time_histogram, 0, sizeof(lock_time_histogram));
    lock_time_max = 0;
    lock_time_sum = 0;
    lock_count = 0;

    // Calibrate
    uint64_t sum = 0;
    for (int i = 0; i < 10000; i++) {
        volatile uint64_t time = get_lock_test_timestamp();
        sum += get_lock_test_timestamp() - time;
    }
    lock_calibration = sum / 10000;
}

static void lock_test_add_sample(uint64_t d) {
    if (d >= lock_calibration) // Subtract calibration value to get more accurate results for short lock times
        d -= lock_calibration;
    else
        d = 0;
    mutexLock(&lock_mutex);
    ; // Subtract calibration value to get more accurate results for short lock times
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
    printf("  count=%" PRIu64 "  max=%" PRIu64 "ns  avg=%" PRIu64 "ns (cal=%" PRIu64 "ns)\n", lock_count, lock_time_max, lock_time_sum / lock_count, lock_calibration);

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
        DBG_PRINTF_ERROR("queue_open_shm: acquire_lock failed\n");
        return NULL;
    }

    int shm_fd;

    if (consumer) {
        shm_unlink(SHM_NAME); // remove any stale object from a previous run
        shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if (shm_fd < 0) {
            DBG_PRINTF_ERROR("CONSUMER: shm_open failed: %s\n", strerror(errno));
            release_lock(lock_fd);
            return NULL;
        }
        if (ftruncate(shm_fd, (off_t)SHM_SIZE) < 0) {
            DBG_PRINTF_ERROR("CONSUMER: ftruncate failed: %s\n", strerror(errno));
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
        DBG_PRINTF_ERROR("%s: mmap failed: %s\n", consumer ? "CONSUMER" : "PRODUCER", strerror(errno));
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
            DBG_PRINTF_ERROR("CONSUMER: mc_queue_init_from_memory failed\n");
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
            DBG_PRINTF_ERROR("PRODUCER: mc_queue_init_from_memory failed\n");
            munmap(mem, SHM_SIZE);
            return NULL;
        }
        g_shm_mem = mem;
        g_shm_hdr = hdr;
        // Claim a unique sequential producer index (0, 1, 2, ...).  Used to build a flat thread_id
        // in the task threads: thread_id = producer_index * THREAD_COUNT + task_index.
        g_producer_index = (uint16_t)atomic_fetch_add_explicit(&g_shm_hdr->producer_index_ctr, 1u, memory_order_relaxed);
        DBG_PRINTF3("PRODUCER: attached to queue in shared memory '%s' (%u KB) as producer[%u]\n", SHM_NAME, (unsigned)(SHM_SIZE / 1024), (unsigned)g_producer_index);
        return h;
    }
}

#endif // TEST_QUEUE_SHM

//-----------------------------------------------------------------------------------------------------

static tQueueHandle queue_handle = NULL;

static atomic_uint_least16_t task_index_ctr = 0;

// Task function that runs in a separate thread
// Simulates a producer that acquires buffers from the queue, fills them with test data and pushes them to the queue
THREAD_FUNC_RETURN task(void *p) {
    bool run = true;

    uint64_t counter = 0;
    uint32_t overruns = 0;

    // Build the task name from the event index
    uint16_t task_index = atomic_fetch_add_explicit(&task_index_ctr, 1, memory_order_relaxed);
    // Flat thread ID unique across all producer processes: each producer gets a sequential
    // producer_index, so thread IDs never collide even with multiple concurrent producers.
    uint16_t thread_id = (uint16_t)(g_producer_index * THREAD_COUNT) + task_index;
    char task_name[16 + 1];
    snprintf(task_name, sizeof(task_name), "task_%u", task_index);

    DBG_PRINTF3("thread %s running...\n", task_name);

    while (run && gRun) {
        // Consumer liveness is checked in the main loop (every 500us) which sets gRun=false.
        // No per-thread kill() check here -- that would add 1M syscalls/s with 10 threads at 10us.

        for (int n = 0; n < THREAD_BURST_SIZE; n++) {

            assert(THREAD_PAYLOAD_MIN_SIZE >= sizeof(uint32_t) + 4 * sizeof(uint64_t));
            uint16_t size = THREAD_PAYLOAD_MIN_SIZE +
                            rand() % (THREAD_PAYLOAD_MAX_SIZE - THREAD_PAYLOAD_MIN_SIZE + 1); // Add some random size to the payload to increase the variability of the test
            size = (size + sizeof(uint32_t) - 1) & ~(sizeof(uint32_t) - 1);                   // Align size to 4

#ifdef TEST_ACQUIRE_LOCK_TIMING
            uint64_t start_time = get_lock_test_timestamp();
#endif

            tQueueBuffer queue_buffer = queueAcquire(queue_handle, size);
            if (queue_buffer.size >= size) {
                assert(queue_buffer.buffer != NULL);

                uint32_t *b = (uint32_t *)(queue_buffer.buffer);

                // Simulate XCP DAQ header, because some queue implementations are not generic and have XCP specific asserts
                b[0] = 0x0000AAFC;

                // Test data
                b[1] = thread_id;
                b[2] = size;
                b[3] = (++counter) & 0xFFFFFFFF;
                b[4] = overruns;

                // Fill the rest of the payload with some data to increase the variability of the test
                for (uint32_t i = 5; i < size / sizeof(uint32_t); i++)
                    b[i] = thread_id + i;

                overruns = 0; // Reset overrun counter after reporting it in the payload
                queuePush(queue_handle, &queue_buffer, false);
            } else {
                overruns++;
                uint32_t queue_max_level = 0;
                uint32_t queue_level = queueLevel(queue_handle, &queue_max_level);
                DBG_PRINTF5(ANSI_COLOR_RED "Overruns in thread %u, count = %u, level = %u/%u)\n" ANSI_COLOR_RESET, (uint32_t)thread_id, overruns, queue_level, queue_max_level);
            }

#ifdef TEST_ACQUIRE_LOCK_TIMING
            lock_test_add_sample(get_lock_test_timestamp() - start_time);
#endif

            if (overruns > 0) {
                sleepUs(THREAD_DELAY_US * 2);
                break;
            }
        }

        // Sleep for the specified delay parameter in microseconds, defines the approximate sampling rate
        sleepUs(THREAD_DELAY_US);
    }

    THREAD_FUNC_END; // Exit the thread
}

//-----------------------------------------------------------------------------------------------------
// Main function

static void print_test_parameters(void) {

    DBG_PRINT3("\nTest parameters\n");

#ifdef TEST_QUEUE_SHM
    if (g_shm_consumer)
        DBG_PRINT3("MODE: consumer (creates shared memory queue, runs consumer)\n");
    else if (g_shm_producer)
        DBG_PRINT3("MODE: producer (attaches to shared memory queue, runs producers)\n");
    else
        DBG_PRINT3("MODE: single-process (in-process queue)\n");
#endif

#if defined(NDEBUG)
    DBG_PRINT3("Release Build\n");
#else
    DBG_PRINT3("Debug Build\n");
#endif
#if defined(OPTION_QUEUE_64_VAR_SIZE)
    DBG_PRINT3("Using queue " ANSI_COLOR_GREEN "(queue64v.c)" ANSI_COLOR_RESET " with 64 bit variable size entries\n");
#elif defined(OPTION_QUEUE_64_FIX_SIZE)
    DBG_PRINT3("Using queue " ANSI_COLOR_GREEN "(queue64f.c)" ANSI_COLOR_RESET " with 64 bit fixed size entries\n");
#ifdef OPTION_QUEUE_64_FIX_SIZE_SYNC_TAIL
    DBG_PRINT3("queue64f sync mode: " ANSI_COLOR_YELLOW "tail release/acquire" ANSI_COLOR_RESET "\n");
#else
    DBG_PRINT3("queue64f sync mode: " ANSI_COLOR_YELLOW "entry_header release, relaxed tail" ANSI_COLOR_RESET "\n");
#endif
#elif defined(OPTION_QUEUE_32)
    DBG_PRINT3("Using queue " ANSI_COLOR_GREEN "(queue32.c)" ANSI_COLOR_RESET " with 32 bit variable size entries\n");
#else
#error "Please select a valid queue implementation\n"
#endif
#ifdef TEST_QUEUE_PEEK
    DBG_PRINT3("Testing peek support\n");
#else
    DBG_PRINT3("Testing without peek support\n");
#endif
    DBG_PRINT3("\n");
    if (THREAD_PAYLOAD_MAX_SIZE > 64)
        DBG_PRINTF3("Testing with " ANSI_COLOR_YELLOW "big payload (max %u bytes)\n" ANSI_COLOR_RESET, THREAD_PAYLOAD_MAX_SIZE);
    else
        DBG_PRINTF3("Testing with " ANSI_COLOR_YELLOW "small payload (max %u bytes)\n" ANSI_COLOR_RESET, THREAD_PAYLOAD_MAX_SIZE);
#ifndef _WIN
    if (TEST_CLOCK_TYPE == CLOCK_THREAD_CPUTIME_ID)
        DBG_PRINT3("Using " ANSI_COLOR_YELLOW "CLOCK_THREAD_CPUTIME_ID" ANSI_COLOR_RESET " for lock time measurement\n");
    else
        DBG_PRINT3("Using " ANSI_COLOR_YELLOW "CLOCK_MONOTONIC_RAW" ANSI_COLOR_RESET " for lock time measurement\n");
#endif
    DBG_PRINTF3("THREAD_COUNT=%d\n", THREAD_COUNT);
    DBG_PRINTF3("THREAD_BURST_SIZE=%d\n", THREAD_BURST_SIZE);
    DBG_PRINTF3("THREAD_DELAY_US=%d\n", THREAD_DELAY_US);
    DBG_PRINTF3("THREAD_PAYLOAD_MIN_SIZE=%u\n", THREAD_PAYLOAD_MIN_SIZE);
    DBG_PRINTF3("THREAD_PAYLOAD_MAX_SIZE=%u\n", THREAD_PAYLOAD_MAX_SIZE);
    DBG_PRINT3("\n");
    DBG_PRINT3("Queue parameters:\n");
    DBG_PRINTF3("QUEUE_ENTRY_USER_HEADER_SIZE=%d\n", QUEUE_ENTRY_USER_HEADER_SIZE);
    DBG_PRINTF3("QUEUE_ENTRY_USER_PAYLOAD_SIZE=%u\n", QUEUE_ENTRY_USER_PAYLOAD_SIZE);
    DBG_PRINTF3("QUEUE_ENTRY_USER_SIZE=%u\n", QUEUE_ENTRY_USER_SIZE);
    DBG_PRINTF3("QUEUE_MAX_ENTRY_SIZE=%u\n", QUEUE_MAX_ENTRY_SIZE);
    DBG_PRINTF3("QUEUE_PAYLOAD_SIZE_ALIGNMENT=%u\n", QUEUE_PAYLOAD_SIZE_ALIGNMENT);
    DBG_PRINT3("\n");
}

static void print_help(void) {

#if defined(OPTION_QUEUE_64_VAR_SIZE)
    printf("  XCPlite queue64v (64-bit variable-size entries)\n");
#elif defined(OPTION_QUEUE_64_FIX_SIZE)
    printf("  XCPlite queue64f (64-bit fixed-size entries)\n");
#elif defined(OPTION_QUEUE_32)
    printf("  XCPlite queue32 (32-bit variable-size entries)\n");
#else
    printf("  XCPlite queue (legacy queue64)\n");
#endif

    printf("  Queue size:    %u bytes\n", QUEUE_SIZE);
    printf("  Threads:       %d producers, payload %zu bytes, burst %d, delay %d us\n", THREAD_COUNT, (size_t)THREAD_PAYLOAD_MIN_SIZE, THREAD_BURST_SIZE, THREAD_DELAY_US);
}

int main(int argc, char *argv[]) {

    // Set log level
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    DBG_PRINT3("\nqueue_test\n");

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
            DBG_PRINTF_ERROR("Unknown option: %s  (use --help for usage)\n", argv[i]);
            return 1;
        }
    }
    if (g_shm_producer && g_shm_consumer) {
        DBG_PRINT_ERROR("--producer and --consumer are mutually exclusive\n");
        return 1;
    }

#ifdef TEST_ACQUIRE_LOCK_TIMING
    lock_test_init();
#endif

    // Print info
    print_test_parameters();

    // Create or attach to a queue, depending on the test mode
    queue_handle = queueInit(QUEUE_SIZE); // Initialize the queue, the queue memory is allocated by the library, the queue buffer size is specified by OPTION_QUEUE_SIZE
    if (queue_handle == NULL) {
        DBG_PRINT_ERROR("Failed to initialize the queue\n");
        return 1;
    }

    // Create multiple instances of the producer task (not in consumer only mode)
    THREAD_HANDLE t[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        t[i] = 0;
    }
    if (!g_shm_consumer) { // consumer-only process has no producer threads
        for (int i = 0; i < THREAD_COUNT; i++) {
            create_thread(&t[i], NULL, task, NULL);
        }
    }

    // Local variables for the consumer loop
    uint32_t msg_count = 0;
    uint32_t msg_overruns = 0;
    uint32_t msg_errors = 0;
    uint64_t msg_bytes = 0;
    uint32_t max_level = 0;
    uint64_t start_time = clockGetMonotonicUs();
    uint64_t last_msg_time = clockGetMonotonicUs();
    uint32_t last_msg_count = 0;
    uint64_t last_msg_bytes = 0;
    uint32_t last_counter[THREAD_COUNT];
    memset(last_counter, 0, sizeof(last_counter));
    uint32_t sleep_time_ns = CONSUMER_SLEEP_ON_EMPTY_QUEUE_US * 1000;

    // Wait for signal to stop
    DBG_PRINT3("main loop running - press Ctrl+C to stop...\n");
    while (gRun) {

        // Poll the queue, break if empty
        // In SHM producer mode the consumer runs in a separate process: skip polling here
#ifdef TEST_QUEUE_SHM
        if (!g_shm_producer) {
#endif

#ifdef TEST_QUEUE_PEEK

            while (gRun) {

                tQueueBuffer buffer[QUEUE_PEEK_MAX_INDEX + 1];
                uint32_t buffer_count = 0;

                // Check queue level and print if it increased, to monitor how full the queue is getting
                uint32_t level_max;
                uint32_t level_cur = queueLevel(queue_handle, &level_max);
                uint32_t level_rel = ((level_cur * 100) / level_max);
                if (level_rel > max_level) {
                    max_level = level_rel;
                    DBG_PRINTF3("New max queue level: %u %% (%u Bytes)\n", max_level, level_cur);
                }

                // Set max max_peek_index to a random number between 0 and QUEUE_PEEK_MAX_INDEX
                uint32_t max_peek_index = rand() % (QUEUE_PEEK_MAX_INDEX + 1);
                for (uint32_t index = 0; index <= max_peek_index; index++) {
                    uint32_t lost = 0;
                    buffer[index] = queuePeek(queue_handle, index, &lost, NULL);
                    msg_overruns += lost;
                    if (buffer[index].size == 0) { // Empty buffer, no more messages in the queue
                        break;
                    }
                    buffer_count++;
                    assert(buffer[index].buffer != NULL);
                    assert(buffer[index].size >= THREAD_PAYLOAD_MIN_SIZE);
                    assert((uint64_t)buffer[index].buffer % 2 == 0);

                    // Check test data
                    // Test payload starts + (User header (space reserved for XCP transport layer header)
                    uint32_t *b = (uint32_t *)(buffer[index].buffer + QUEUE_ENTRY_USER_HEADER_SIZE);
                    uint32_t header = b[0];
                    uint32_t thread_id = b[1];
                    uint32_t size = b[2];
                    uint32_t counter = b[3];
                    uint32_t overruns = b[4];

                    // DBG_PRINTF("Peeked index %u: thread_id=%u, size=%u, counter=%u, overruns=%u\n", index, thread_id, size, counter, overruns);

                    // Check the faked XCP DAQ header, to detect if the message is corrupted or if we are not correctly aligned with the message boundaries in the queue
                    if (header != 0x0000AAFC) {
                        DBG_PRINTF_ERROR(ANSI_COLOR_RED "Corrupt message header: expected 0x0000AAFC, got 0x%08X\n" ANSI_COLOR_RESET, header);
                        msg_errors++;
                        continue;
                    }

                    // Check counter incrementing
                    if (size < THREAD_PAYLOAD_MIN_SIZE || thread_id >= THREAD_COUNT) {
                        DBG_PRINT_ERROR(ANSI_COLOR_RED "Corrupt message received \n" ANSI_COLOR_RESET);
                        msg_errors++;
                    } else {
                        if (msg_count > 0) {
                            if (counter != last_counter[thread_id] + 1) {
                                DBG_PRINTF_ERROR(ANSI_COLOR_RED "Counter error in thread %u, expected counter %u, got %u\n" ANSI_COLOR_RESET, (uint32_t)thread_id,
                                                 last_counter[thread_id] + 1, counter);
                                msg_errors++;
                            }
                        }
                        last_counter[thread_id] = counter;
                    }

                    // Check overruns
                    if (overruns > 0) {
                        DBG_PRINTF4(ANSI_COLOR_YELLOW "Overruns in thread %u, count = %u)\n" ANSI_COLOR_RESET, (uint32_t)thread_id, overruns);
                    }

                    // Check the rest of the payload data to detect if there is any corruption in the message or if we are not correctly aligned with the message boundaries in the
                    // queue
                    for (uint32_t i = 5; i < size / sizeof(uint32_t); i++) {
                        if (b[i] != thread_id + i) {
                            DBG_PRINTF_ERROR(ANSI_COLOR_RED "Corrupt message payload in thread %u at index %u: expected 0x%08X, got 0x%08X\n" ANSI_COLOR_RESET, (uint32_t)thread_id,
                                             i, thread_id + i, b[i]);
                            msg_errors++;
                            break;
                        }
                    }

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
                    queueRelease(queue_handle, &buffer[i]);
                }

            } // for (;;)

#else

        // XCPlite queue consumer loop
        for (;;) {

            uint32_t lost = 0;
            tQueueBuffer segment_buffer = queuePop(queue_handle, true, false, &lost); // May accumulate multiple messages in one segment (message has a transport layer header)
            msg_overruns += lost;
            if (segment_buffer.size == 0)
                break;

            uint32_t segment_size = segment_buffer.size;
            tQueueBuffer buffer;
            buffer.size = *(uint16_t *)segment_buffer.buffer + sizeof(uint32_t); // Get the buffer size from transportlayer header dlc
            buffer.buffer = segment_buffer.buffer;                               // Move the buffer pointer to the start of the message payload (to the transport layer header)
            assert(buffer.size > 0);

            // Iterate cal_seg_list over all messages in the segment
            for (;;) {

                assert(buffer.buffer != NULL);
                assert(buffer.size >= THREAD_PAYLOAD_MIN_SIZE);
                assert((uint64_t)buffer.buffer % 2 == 0);

                uint32_t *b = (uint32_t *)(buffer.buffer + 8); // Test payload starts + 8 (Transport layer header + XCP DAQ header)
                uint32_t thread_id = b[0];
                uint32_t size = b[1];
                uint32_t counter = b[2];

                assert(size >= THREAD_PAYLOAD_MIN_SIZE);
                assert(thread_id < THREAD_COUNT);
                if (msg_count > 0) {
                    if (counter != last_counter[thread_id] + 1) {
                        DBG_PRINTF3("Messages lost in thread %u, expected counter %u, got %u\n", (uint32_t)thread_id, last_counter[thread_id] + 1, counter);
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

#endif

#ifdef TEST_QUEUE_SHM
        } // if (!g_shm_producer)
#endif

        // Iterate close to the overrun limit to test the behavior with high level
        sleepUs(sleep_time_ns / 1000);
        if (max_level < 90 && msg_overruns == 0) {
            sleep_time_ns += 10;
            if (sleep_time_ns % 1000 == 0)
                DBG_PRINTF3("Increasing consumer sleep time to %u us to increase the queue level\n", sleep_time_ns / 1000);
        }

// Producer mode: check consumer liveness once per main loop iteration.
// kill(pid, 0) with ESRCH means the consumer process is gone (graceful or crash).
// Set gRun=false to exit the main loop and join all producer threads.
#ifdef TEST_QUEUE_SHM
        if (g_shm_producer && g_shm_hdr != NULL) {
            int32_t cpid = atomic_load_explicit(&g_shm_hdr->consumer_pid, memory_order_relaxed);
            if (cpid == 0 || (kill((pid_t)cpid, 0) == -1 && errno == ESRCH)) {
                DBG_PRINT3("PRODUCER: consumer gone (pid=%d), shutting down\n", (int)cpid);
                gRun = false;
            }
        }
#endif

        // Print statistics every second
        if (clockGetMonotonicUs() - last_msg_time >= 1000000) {
            if (!g_shm_producer) {
                DBG_PRINTF3("Messages received: %u, overruns: %u, errors: %u, data rate: %u msg/s, %u kbytes/s\n", msg_count, msg_overruns, msg_errors,
                            (msg_count - last_msg_count), (uint32_t)((msg_bytes - last_msg_bytes) / 1024));
                last_msg_bytes = msg_bytes;
                last_msg_count = msg_count;
            }
            last_msg_time = clockGetMonotonicUs();
        }

    } // gRun

    uint64_t end_time = clockGetMonotonicUs();

    // Wait for all threads to finish
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (t[i])
            join_thread(t[i]);
    }

// Unmap shared memory; consumer signals producers to stop, then removes the SHM object
#ifdef TEST_QUEUE_SHM
    if (g_shm_consumer && g_shm_hdr != NULL) {
        // Clear PID so producers detect the graceful exit immediately via the pid==0 fast path.
        // (They would also detect it via kill()/ESRCH once this process exits, but clearing
        // first lets them stop before the 500ms drain window expires.)
        atomic_store_explicit(&g_shm_hdr->consumer_pid, 0, memory_order_release);
        DBG_PRINT3("CONSUMER: signaled producers to stop, waiting 500ms...\n");
        sleepUs(500000);
    }
    if (g_shm_mem != NULL) {
        munmap(g_shm_mem, SHM_SIZE);
        g_shm_mem = NULL;
        g_shm_hdr = NULL;
    }
    if (g_shm_consumer) {
        shm_unlink(SHM_NAME);
        DBG_PRINT3("CONSUMER: shared memory '%s' removed\n", SHM_NAME);
    }
#endif

    printf("\n\nDone:\n");
    print_test_parameters();

    printf("\nStatistics:\n");
    uint64_t total_time = end_time - start_time;
    printf("Test duration: %.2f seconds\n", total_time / 1000000.0);
    printf("Messages received: %u, bytes received: %" PRIu64 ", messages lost: %u\n", msg_count, msg_bytes, msg_overruns);
    printf("Average rates: %u msg/s, %u kbytes/s\n", (uint32_t)((uint64_t)msg_count * 1000000 / total_time), (uint32_t)(msg_bytes * 1000000 / total_time) / 1024);
    printf("Max queue level: %u%%\n", max_level);
    printf("\n");

    // Deinitialize the queue (queue will print internal statistics if supported by the implementation)
    printf("\nDeinitialize queue, queue internal statistics:\n");
    queueDeinit(queue_handle); // Deinitialize the queue
    printf("\n");

#ifdef TEST_ACQUIRE_LOCK_TIMING
    if (!g_shm_consumer) {
        lock_test_print_results();
    }
#endif

    return 0;
}
