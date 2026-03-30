/*----------------------------------------------------------------------------
| File:
|   queue64.c
|
|  @@@@ Deprecated, this file is not maintained anymore
|  This file is only kept for reference and testing of different queue implementations.
|
| Description:
|   XCP transport layer queue
|   Multi producer single consumer queue (producer side is thread safe and lockless)
|   Queue entries include XCP transport layer message header, queue can accumulate multiple XCP packets to a segment
|   Lock free with minimal wait implementation using a seq_lock and a spin loop on the producer side
|   Optional mutex based mode for higher consumer throughput as a tradeoff for higher producer latency
|   Tested on ARM weak memory model
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "platform.h"   // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex, spinlock
#include "xcplib_cfg.h" // for OPTION_xxx

// Use queue32.c for 32 Bit platforms or on Windows
#if defined(PLATFORM_64BIT) && !defined(_WIN) && !defined(OPTION_ATOMIC_EMULATION)

#if !defined(OPTION_QUEUE_64_VAR_SIZE) && !defined(OPTION_QUEUE_64_FIX_SIZE) && !defined(OPTION_QUEUE_32)

#include "queue.h"

#include <assert.h>    // for assert
#include <inttypes.h>  // for PRIu64
#include <stdatomic.h> // for atomic_
#include <stdbool.h>   // for bool
#include <stdint.h>    // for uint32_t, uint64_t, uint8_t, int64_t
#include <stdio.h>     // for NULL, snprintf
#include <stdlib.h>    // for free, malloc
#include <string.h>    // for memcpy, strcmp

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...

#include "xcptl.h" // for XcpTlGetCtr

// Turn of misaligned atomic access warnings
// Alignment is assured by the queue header and the queue entry size alignment
#ifdef __GNUC__
#endif
#ifdef __clang__
#pragma GCC diagnostic ignored "-Watomic-alignment"
#endif
#ifdef _MSC_VER
#endif

// Hint to the CPU that we are spinning
#if defined(__x86_64__) || defined(__i386__)
#define spin_loop_hint() __asm__ volatile("pause" ::: "memory")
#elif defined(__aarch64__) || defined(__arm__)
#define spin_loop_hint() __asm__ volatile("yield" ::: "memory");
#else
#define spin_loop_hint() // Fallback: do nothing
#endif

// Assume a maximum cache line size of 64 bytes
#define CACHE_LINE_SIZE 64u // Cache line size, used to align the queue header

// Check for 64 Bit non Windows platform
#if !defined(PLATFORM_64BIT) || defined(_WIN)
#error "This implementation requires a 64 Bit Posix platform"
#endif
static_assert(sizeof(void *) == 8, "This implementation requires a 64 Bit platform");

// Check for atomic support and atomic_uint_least32_t availability
#ifdef __STDC_NO_ATOMICS__
#error "C11 atomics are not supported on this platform, but required for queue64.c"
#endif

// Test atomic_uint_least32_t availability
static_assert(sizeof(atomic_uint_least32_t) == 4, "atomic_uint_least32_t must be 4 bytes");
// static atomic_uint_least32_t _atomic_test_variable = 0;
// static_assert(sizeof(_atomic_test_variable) == 4, "atomic_uint_least32_t must be usable");

//-------------------------------------------------------------------------------------------------------------------------------------------------------

/*
Naming convention:
Transport Layer segment, message, packet:
    segment (UDP payload, MAX_SEGMENT_SIZE = UDP MTU) = message 1 + message 2 ... + message n
    message = WORD len + WORD ctr + (protocol layer packet) + fill
*/

// Different queue implementations with different tradeoffs
// The default implementation is a mutex based producer lock, no consumer lock and memory fences between producer and consumer.
// Always benchmark, to find the best tradeoff for your use case !!!!

// Use a spinlock for queue producers, this is a just a bad example
// #define QUEUE_SPINLOCK

// Use a mutex for queue producers, this is a convenient default
// Might be the best solution for high throughput, when worst case producer latency is acceptable
// #define QUEUE_MUTEX

// Use a seq_lock to protect against inconsistency during the entry acquire, the queue is lockfree with minimal CAS spin wait in contention for increasing the head
// The consumer may heavily spin, to acquire a safe consistent head
// #define QUEUE_SEQ_LOCK

// No synchronisation between producer and consumer, producer CAS loop increments the head, consumer clears memory completely for consistent reservation state
// Tradeoff between consumer spin activity and consumer cache activity, might be the optimal solution for medium throughput
#define QUEUE_NO_LOCK

// Accumulate XCP packets to multiple XCP messages in a segment obtained with queuePop
#define QUEUE_ACCUMULATE_PACKETS // Accumulate XCP packets to multiple XCP messages obtained with queuePop
#define QUEUE_ACCUMULATE_THRESHOLD (XCPTL_MAX_SEGMENT_SIZE / 2)

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Test

// Queue acquire lock timing and spin
// For high contention use test queue_test or example daq_test with xcpclient --upload-a2l --udp --mea .  --dest-addr 192.168.0.206
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
static uint64_t get_timestamp_ns(void) {
    static const uint64_t kNanosecondsPerSecond = 1000000000ULL;
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts); // NOLINT(missing-includes) // do **not** include internal "bits" headers directly.
    return ((uint64_t)ts.tv_sec) * kNanosecondsPerSecond + ((uint64_t)ts.tv_nsec);
}

static void lock_test_add_sample(uint64_t d, uint32_t spin_count) {
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

/*

Results on MacBook Pro with Apple M3 (ARM64) with 32 producers
---------------------------------------------------------------------------

QUEUE_NO_LOCK:

Producer acquire lock time statistics:
  count=7845111  max_spins=5  max=50542ns  avg=94ns

Lock time histogram (7845111 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                    252186    3.21%  #
  40-80ns                  3994443   50.92%  ##############################
  80-120ns                 2816917   35.91%  #####################
  120-160ns                 204275    2.60%  #
  160-200ns                 142489    1.82%  #
  200-240ns                 221886    2.83%  #
  240-280ns                 114354    1.46%
  280-320ns                  33043    0.42%
  320-360ns                  11807    0.15%
  360-400ns                   6870    0.09%
  400-600ns                  13727    0.17%
  600-800ns                   1716    0.02%
  800-1000ns                  1090    0.01%
  1000-1500ns                 2966    0.04%
  1500-2000ns                 2894    0.04%
  2000-3000ns                 4421    0.06%
  3000-4000ns                 3983    0.05%
  4000-6000ns                 3506    0.04%
  6000-8000ns                 1894    0.02%
  8000-10000ns                3147    0.04%
  10000-20000ns               7064    0.09%
  20000-40000ns                423    0.01%
  40000-80000ns                 10    0.00%


  QUEUE_MUTEX:

  Producer acquire lock time statistics:
  count=3336231  max_spins=0  max=113958ns  avg=335ns

Lock time histogram (3336231 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                     66998    2.01%  #
  40-80ns                  1176131   35.25%  ######################
  80-120ns                 1573225   47.16%  ##############################
  120-160ns                 150378    4.51%  ##
  160-200ns                  77050    2.31%  #
  200-240ns                  63441    1.90%  #
  240-280ns                  46252    1.39%
  280-320ns                  28185    0.84%
  320-360ns                   9663    0.29%
  360-400ns                   3048    0.09%
  400-600ns                   4978    0.15%
  600-800ns                   6859    0.21%
  800-1000ns                  8159    0.24%
  1000-1500ns                19649    0.59%
  1500-2000ns                12815    0.38%
  2000-3000ns                15783    0.47%
  3000-4000ns                 8822    0.26%
  4000-6000ns                13618    0.41%
  6000-8000ns                10567    0.32%
  8000-10000ns                8859    0.27%
  10000-20000ns              25467    0.76%
  20000-40000ns               5831    0.17%
  40000-80000ns                444    0.01%
  80000-160000ns                 9    0.00%



QUEUE_SPINLOCK:

Producer acquire lock time statistics:
  count=7553626  max_spins=0  max=62401625ns  avg=1519ns

Lock time histogram (7553626 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                    142110    1.88%  #
  40-80ns                  2968972   39.31%  #########################
  80-120ns                 3491038   46.22%  ##############################
  120-160ns                 274184    3.63%  ##
  160-200ns                 135015    1.79%  #
  200-240ns                 154529    2.05%  #
  240-280ns                 137639    1.82%  #
  280-320ns                  71583    0.95%
  320-360ns                  29692    0.39%
  360-400ns                  17383    0.23%
  400-600ns                  48639    0.64%
  600-800ns                  12771    0.17%
  800-1000ns                  4261    0.06%
  1000-1500ns                 7009    0.09%
  1500-2000ns                 5508    0.07%
  2000-3000ns                 8575    0.11%
  3000-4000ns                 7336    0.10%
  4000-6000ns                 7946    0.11%
  6000-8000ns                 6611    0.09%
  8000-10000ns                8657    0.11%
  10000-20000ns              12395    0.16%
  20000-40000ns                756    0.01%
  40000-80000ns                248    0.00%
  80000-160000ns                27    0.00%
  160000-320000ns                1    0.00%
  >320000ns                    741    0.01%


  QUEUE_SEQ_LOCK:

  Producer acquire lock time statistics:
  count=3512722  max_spins=6  max=57791ns  avg=111ns

Lock time histogram (3512722 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                     64331    1.83%  #
  40-80ns                  1275979   36.32%  #######################
  80-120ns                 1614503   45.96%  ##############################
  120-160ns                 180286    5.13%  ###
  160-200ns                  96038    2.73%  #
  200-240ns                  86318    2.46%  #
  240-280ns                  59670    1.70%  #
  280-320ns                  37039    1.05%
  320-360ns                  24750    0.70%
  360-400ns                  17585    0.50%
  400-600ns                  36853    1.05%
  600-800ns                   7004    0.20%
  800-1000ns                  1303    0.04%
  1000-1500ns                  951    0.03%
  1500-2000ns                  605    0.02%
  2000-3000ns                  906    0.03%
  3000-4000ns                  736    0.02%
  4000-6000ns                 1253    0.04%
  6000-8000ns                 1318    0.04%
  8000-10000ns                2099    0.06%
  10000-20000ns               3108    0.09%
  20000-40000ns                 80    0.00%
  40000-80000ns                  7    0.00%

*/
#endif

//-------------------------------------------------------------------------------------------------------------------------------------------------------

// Queue entry states
#define CTR_RESERVED 0x0000u  // Reserved by producer
#define CTR_COMMITTED 0xCCCCu // Committed by producer

// Transport layer message header
#pragma pack(push, 1)
typedef struct {
    atomic_uint_least32_t ctr_dlc;
    uint8_t data[];
} tXcpDtoMessage;
#pragma pack(pop)

static_assert(sizeof(tXcpDtoMessage) == XCPTL_TRANSPORT_LAYER_HEADER_SIZE, "tXcpDtoMessage size must be equal to XCPTL_TRANSPORT_LAYER_HEADER_SIZE");

// Queue header
// Aligned to cache line size
typedef struct {
    // Shared state
    atomic_uint_fast64_t head;         // Consumer reads from head
    atomic_uint_fast64_t tail;         // Producers write to tail
    atomic_uint_fast32_t packets_lost; // Packet lost counter, incremented by producers when a queue entry could not be acquired
    ATOMIC_BOOL flush;

#if defined(QUEUE_SEQ_LOCK)
    // seq_lock is used to acquire an entry safely
    // A spin loop is used to increment the head
    // It is incremented by 0x0000000100000000 on lock and 0x0000000000000001 on unlock
    atomic_uint_fast64_t seq_lock;
    uint64_t seq_head; // Last head detected as consistent by the seq lock
#elif defined(QUEUE_MUTEX)
    MUTEX mutex; // Mutex for queue producers, producers contend on each other but not on the consumer
#elif defined(QUEUE_SPINLOCK)
    atomic_int_fast64_t spinlock; // Spinlock for queue producers, producers contend on each other but not on the consumer
#endif
    // Constant
    uint32_t queue_size;  // Size of queue in bytes (for entry offset wrapping)
    uint32_t buffer_size; // Size of overall queue data buffer in bytes
    bool from_memory;     // Queue memory from queueInitFromMemory
    uint8_t reserved[7];  // Header must be 8 byte aligned
} tQueueHeader;

static_assert(((sizeof(tQueueHeader) % 8) == 0), "QueueHeader size must be %8");

// Queue
typedef struct Queue {
    tQueueHeader h;
    uint8_t buffer[];
} tQueue;

//-------------------------------------------------------------------------------------------------------------------------------------------------------

#if defined(QUEUE_SPINLOCK)

static void spinlockLock(atomic_int_fast64_t *lock) {
    int64_t expected = 0;
    int64_t const desired = 1;
    while (!atomic_compare_exchange_weak_explicit(lock, &expected, desired, memory_order_acquire, memory_order_relaxed)) {
        expected = 0;
    }
}

static void spinlockUnlock(atomic_int_fast64_t *lock) { atomic_store_explicit(lock, 0, memory_order_release); }

#endif

//-------------------------------------------------------------------------------------------------------------------------------------------------------

// Initialize a queue from given memory, a given existing queue or allocate a new queue
tQueueHandle queueInitFromMemory(void *queue_memory, size_t queue_memory_size, bool clear_queue, uint64_t *out_queue_buffer_size) {

    tQueue *queue = NULL;

    // Allocate the queue memory
    if (queue_memory == NULL) {
        size_t aligned_size = (queue_memory_size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1); // Align to cache line size
        queue = (tQueue *)aligned_alloc(CACHE_LINE_SIZE, aligned_size);
        assert(queue != NULL);
        assert(queue && ((uint64_t)queue % CACHE_LINE_SIZE) == 0); // Check alignment
        memset(queue, 0, aligned_size);                            // Clear memory
        queue->h.from_memory = false;
        queue->h.buffer_size = queue_memory_size - (uint32_t)sizeof(tQueueHeader);
        queue->h.queue_size = queue->h.buffer_size - QUEUE_MAX_ENTRY_SIZE;
        clear_queue = true;
    }
    // Queue memory is provided by the caller
    else if (clear_queue) {
        queue = (tQueue *)queue_memory;
        memset(queue, 0, queue_memory_size); // Clear memory
        queue->h.from_memory = true;
        queue->h.buffer_size = queue_memory_size - (uint32_t)sizeof(tQueueHeader);
        queue->h.queue_size = queue->h.buffer_size - QUEUE_MAX_ENTRY_SIZE;
    }

    // Queue is provided by the caller and already initialized
    else {
        queue = (tQueue *)queue_memory;
        assert(queue->h.from_memory == true);
        assert(queue->h.queue_size == queue->h.buffer_size - QUEUE_MAX_ENTRY_SIZE);
    }

    DBG_PRINT3("Init transport layer lockless queue (queue64)\n");
    DBG_PRINTF3("  XCPTL_MAX_SEGMENT_SIZE=%u, XCPTL_PACKET_ALIGNMENT=%u, queue: %u DTOs of max %u bytes, %uKiB\n", XCPTL_MAX_SEGMENT_SIZE, XCPTL_PACKET_ALIGNMENT,
                queue->h.queue_size / QUEUE_MAX_ENTRY_SIZE, QUEUE_MAX_ENTRY_SIZE - XCPTL_TRANSPORT_LAYER_HEADER_SIZE,
                (uint32_t)((queue->h.buffer_size + sizeof(tQueueHeader)) / 1024));
#if defined(QUEUE_SEQ_LOCK)
    DBG_PRINT3("  QUEUE_SEQ_LOCK\n");
#endif
#if defined(QUEUE_NO_LOCK)
    DBG_PRINT3("  QUEUE_NO_LOCK\n");
#endif
#if defined(QUEUE_MUTEX)
    DBG_PRINT3("  QUEUE_MUTEX\n");
#endif
#if defined(QUEUE_SPINLOCK)
    DBG_PRINT3("  QUEUE_SPINLOCK\n");
#endif

    if (clear_queue) {

#if defined(QUEUE_SEQ_LOCK)
        queue->h.seq_head = 0;
        atomic_store_explicit(&queue->h.seq_lock, 0, memory_order_relaxed); // Initialize the seq_lock
#elif defined(QUEUE_MUTEX)
        mutexInit(&queue->h.mutex, false, 1000);
#elif defined(QUEUE_SPINLOCK)
        queue->h.spinlock = 0;
#endif

        queueClear((tQueueHandle)queue); // Clear the queue
    }

    // Checks
    assert(atomic_is_lock_free(&((tQueue *)queue_memory)->h.head));
    assert((queue->h.queue_size & (XCPTL_PACKET_ALIGNMENT - 1)) == 0);

    if (out_queue_buffer_size) {
        *out_queue_buffer_size = 0;
    }

    DBG_PRINT4("queueInitFromMemory\n");
    return (tQueueHandle)queue;
}

// Clear the queue
void queueClear(tQueueHandle queue_handle) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    atomic_store_explicit(&queue->h.head, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.tail, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.packets_lost, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.flush, false, memory_order_relaxed);
#if defined(QUEUE_SEQ_LOCK)
    queue->h.seq_head = 0;
    atomic_store_explicit(&queue->h.seq_lock, 0, memory_order_relaxed);
#endif
    DBG_PRINT4("queueClear\n");
}

// Create and initialize a new queue with a given size
tQueueHandle queueInit(size_t queue_buffer_size) { return queueInitFromMemory(NULL, queue_buffer_size + sizeof(tQueueHeader), true, NULL); }

// Deinitialize and free the queue
void queueDeinit(tQueueHandle queue_handle) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    // Print statistics
#ifdef TEST_ACQUIRE_LOCK_TIMING
    lock_test_print_results();
#endif

    queueClear(queue_handle);
#if defined(QUEUE_MUTEX)
    mutexDestroy(&queue->h.mutex);
#endif
    if (!queue->h.from_memory) {
        free(queue);
    }

    DBG_PRINT4("QueueDeInit\n");
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Producer functions
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// For multiple producers !!

// Get a buffer for a message with packet_len bytes
tQueueBuffer queueAcquire(tQueueHandle queue_handle, uint16_t packet_len) {

    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    if (!(packet_len > 0 && packet_len <= XCPTL_MAX_DTO_SIZE)) {
        DBG_PRINTF_ERROR("Invalid packet_len %u, must be between 1 and %u\n", packet_len, XCPTL_MAX_DTO_SIZE);
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }

    tXcpDtoMessage *entry = NULL;

    // Align the message length
    uint16_t msg_len = packet_len + XCPTL_TRANSPORT_LAYER_HEADER_SIZE;
#if XCPTL_PACKET_ALIGNMENT == 2
    msg_len = (uint16_t)((msg_len + 1) & 0xFFFE); // Add fill %2
#error "XCPTL_PACKET_ALIGNMENT == 2 is not supported, use 4"
#endif
#if XCPTL_PACKET_ALIGNMENT == 4
    msg_len = (uint16_t)((msg_len + 3) & 0xFFFC); // Add fill %4
#endif
#if XCPTL_PACKET_ALIGNMENT == 8
    msg_len = (uint16_t)((msg_len + 7) & 0xFFF8); // Add fill %8
#error "XCPTL_PACKET_ALIGNMENT == 8 is not supported, use 4"
#endif

    assert(msg_len <= QUEUE_MAX_ENTRY_SIZE);

#ifdef TEST_ACQUIRE_LOCK_TIMING
    uint64_t spin_start = get_timestamp_ns();
    uint32_t spin_count = 0;
#endif

    // Prepare a new entry in reserved state
    // Reserved state has a valid dlc and ctr, ctr is unknown yet and will be marked as CTR_RESERVED for checking

    //----------------------------------------------
    // Use a seq_lock to protect the entry acquire, CAS loop to increment the head
#if defined(QUEUE_SEQ_LOCK) || defined(QUEUE_NO_LOCK)

    uint64_t tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_acquire);

#if defined(QUEUE_SEQ_LOCK) // Consumer is using the seq_lock to acquire a consistent head
    // By making sure no producer is currently in the following sequence, which might have incremented the head, but not set the entry state to not commited yet
    atomic_fetch_add_explicit(&queue->h.seq_lock, 0x0000000100000000, memory_order_acq_rel);
#endif

    for (;;) {

        // Check for overrun
        if (queue->h.queue_size - msg_len < head - tail) {
            break; // Overrun
        }

        // Try increment the head
        // Compare exchange weak, false negative ok
        if (atomic_compare_exchange_weak_explicit(&queue->h.head, &head, head + msg_len, memory_order_acq_rel, memory_order_acquire)) {
            entry = (tXcpDtoMessage *)(queue->buffer + (head % queue->h.queue_size));
            atomic_store_explicit(&entry->ctr_dlc, (CTR_RESERVED << 16) | (uint32_t)(msg_len - XCPTL_TRANSPORT_LAYER_HEADER_SIZE), memory_order_release);
            break;
        }

        // Get spin count statistics
        // spin_loop_hint(); // No hint, spin count is usually low and the locked sequence should be as fast as possible
        // assert(spin_count < 100); // No reason to be afraid about the spin count, enable spin count statistics to check
#ifdef TEST_ACQUIRE_LOCK_TIMING
        spin_count++;
#endif

    } // for (;;)
#if defined(QUEUE_SEQ_LOCK)
    atomic_fetch_add_explicit(&queue->h.seq_lock, 0x0000000000000001, memory_order_acq_rel);
#endif

    //----------------------------------------------
    // Use a mutex to protect the entry acquire
#elif defined(QUEUE_MUTEX) || defined(QUEUE_SPINLOCK)

#if defined(QUEUE_MUTEX)
    mutexLock(&queue->h.mutex);
#else
    spinlockLock(&queue->h.spinlock);
#endif

    uint64_t tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_acquire);
    assert(head >= tail);
    if (queue->h.queue_size - msg_len >= head - tail) {
        entry = (tXcpDtoMessage *)(queue->buffer + (head % queue->h.queue_size));
        atomic_store_explicit(&entry->ctr_dlc, (CTR_RESERVED << 16) | (uint32_t)(msg_len - XCPTL_TRANSPORT_LAYER_HEADER_SIZE), memory_order_release);
        atomic_store_explicit(&queue->h.head, head + msg_len, memory_order_release);
    }

#if defined(QUEUE_MUTEX)
    mutexUnlock(&queue->h.mutex);
#else
    spinlockUnlock(&queue->h.spinlock);
#endif

#endif

#ifdef TEST_ACQUIRE_LOCK_TIMING
    lock_test_add_sample(get_timestamp_ns() - spin_start, spin_count);
#endif

    if (entry == NULL) {
        uint32_t lost = (uint32_t)atomic_fetch_add_explicit(&queue->h.packets_lost, 1, memory_order_acq_rel);
        if (lost == 0)
            DBG_PRINTF_WARNING("Transmit queue overrun, msg_len=%u, head=%" PRIu64 ", tail=%" PRIu64 ", level=%u, queue_size=%u\n", msg_len, head, tail, (uint32_t)(head - tail),
                               queue->h.queue_size);
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }

    tQueueBuffer ret = {
        .buffer = entry->data,
        .size = msg_len, // Return the size of the complete entry, data buffer size can be larger than requested packet_len !
    };
    return ret;
}

// Commit a buffer (returned from queueAcquire)
void queuePush(tQueueHandle queue_handle, const tQueueBuffer *queue_buffer, bool flush) {

    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    // Set flush request
    if (flush) {
        atomic_store_explicit(&queue->h.flush, true, memory_order_relaxed); // Set flush flag, used by the consumer to prioritize packets
    }

    assert(queue_buffer != NULL);
    assert(queue_buffer->buffer != NULL);
    tXcpDtoMessage *entry = (tXcpDtoMessage *)(queue_buffer->buffer - XCPTL_TRANSPORT_LAYER_HEADER_SIZE);

    // Go to commit state
    // Complete data is then visible to the consumer
    atomic_store_explicit(&entry->ctr_dlc, (CTR_COMMITTED << 16) | (uint32_t)(queue_buffer->size - XCPTL_TRANSPORT_LAYER_HEADER_SIZE), memory_order_release);
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Consumer functions
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Single consumer thread !!!!!!!!!!
// The consumer does not contend against the providers

// Get current transmit queue level in bytes
// This function is thread safe
// Not used by the queue implementation itself
// Returns 0 when the queue is empty
uint32_t queueLevel(tQueueHandle queue_handle, uint32_t *queue_max_level) {
    tQueue *queue = (tQueue *)queue_handle;
    if (queue == NULL) {
        if (queue_max_level != NULL)
            *queue_max_level = 0;
        return 0;
    }
    if (queue_max_level != NULL)
        *queue_max_level = queue->h.queue_size;
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
    assert(head >= tail);
    assert(head - tail <= queue->h.queue_size);
    return (uint32_t)(head - tail);
}

// Check if there is a message segment (one or more accumulated packets) in the transmit queue
// Return the message length and a pointer to the message
// Returns the number of packets lost since the last call
// May not be called twice, each buffer must be released immediately with queueRelease
// Is not thread safe, must be called from one consumer thread only
tQueueBuffer queuePop(tQueueHandle queue_handle, bool accumulate, bool flush, uint32_t *packets_lost) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);
#ifndef QUEUE_ACCUMULATE_PACKETS
    assert(accumulate == false); // Accumulate not supported
#endif
    // Return the number of packets lost in the queue
    if (packets_lost != NULL) {
        uint32_t lost = (uint32_t)atomic_exchange_explicit(&queue->h.packets_lost, 0, memory_order_acq_rel);
        *packets_lost = lost;
        if (lost) {
            DBG_PRINTF_WARNING("queuePop: packets lost since last call: %u\n", lost);
        }
    }

    uint64_t head, tail;
    uint32_t level;

    uint32_t first_offset;
    tXcpDtoMessage *first_entry;

    uint16_t total_len = 0;

    tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);

    // Read a consistent head
    // Consistent means, the validity of the commit state for this entry is assured

#if defined(QUEUE_SEQ_LOCK)

    // Check if there is data in the queue already verified to be consistent
    level = (uint32_t)(queue->h.seq_head - tail);
    if (level >= XCPTL_MAX_SEGMENT_SIZE) {

        // Use the last head detected as consistent by the seq lock
        head = queue->h.seq_head;
    } else {

        // Spin until the seq_lock is consistent when reading the head
        // This spinning is the tradeoff for lockless on the producer side, it may impact the consumer performance but greatly improves the producer latency
        uint64_t seq_lock1, seq_lock2;
        uint32_t spin_count = 0;
        do {

            seq_lock1 = atomic_load_explicit(&queue->h.seq_lock, memory_order_acquire);
            head = atomic_load_explicit(&queue->h.head, memory_order_acquire);
            seq_lock2 = atomic_load_explicit(&queue->h.seq_lock, memory_order_acquire);

            spin_loop_hint(); // Hint to the CPU that this is a spin loop

            // Limit spinning
            if (spin_count++ >= 50) {
                sleepUs(100);   // Sleep for 100us to reduce CPU load
                spin_count = 0; // Reset
            }

        } while ((seq_lock1 != seq_lock2) || ((seq_lock1 >> 32) != (seq_lock2 & 0xFFFFFFFF)));

        queue->h.seq_head = head; // Set the last head detected as consistent by the seq lock
    }

#elif defined(QUEUE_MUTEX) || defined(QUEUE_SPINLOCK)

    head = atomic_load_explicit(&queue->h.head, memory_order_relaxed);

#elif defined(QUEUE_NO_LOCK)

    head = atomic_load_explicit(&queue->h.head, memory_order_relaxed);

#else

#error "No queue locking mechanism defined, use QUEUE_SEQ_LOCK, QUEUE_MUTEX or QUEUE_NO_LOCK"

#endif

    // Check if there is data in the queue
    assert(head >= tail);
    level = (uint32_t)(head - tail);
    assert(level <= queue->h.queue_size);
    if (level == 0) { // Queue is empty
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }

    // Require a minimum amount of data, to optimize segment usage (less Ethernet frames)
    // Don't when there is a flush request from producer or consumer
#ifdef QUEUE_ACCUMULATE_PACKETS
    if (accumulate) {

        // Flush request ?
        if (atomic_load_explicit(&queue->h.flush, memory_order_relaxed)) {
            flush = true; // Flush request, set by the producer
            atomic_store_explicit(&queue->h.flush, false, memory_order_relaxed);
        }

        if ((level <= QUEUE_ACCUMULATE_THRESHOLD && !flush)) { // Queue is not above the minimum segment size
            tQueueBuffer ret = {
                .buffer = NULL,
                .size = 0,
            };
            return ret;
        }
    }
#else // QUEUE_ACCUMULATE_PACKETS
    (void)flush; // Unused, suppress warning
#endif

    // Get a pointer to the entry in the queue
    first_offset = (uint32_t)(tail % queue->h.queue_size);
    first_entry = (tXcpDtoMessage *)(queue->buffer + first_offset);

    // Check the entry commit state

    uint32_t ctr_dlc = atomic_load_explicit(&first_entry->ctr_dlc, memory_order_acquire);
    uint16_t dlc = ctr_dlc & 0xFFFF;          // Transport layer packet data length
    uint16_t ctr = (uint16_t)(ctr_dlc >> 16); // Transport layer counter
    if (ctr != CTR_COMMITTED) {

        // This should never happen
        // An entry is consistent, if it is neither in reserved or committed state
        if (ctr != CTR_RESERVED) {
            DBG_PRINTF_ERROR("queuePop initial: inconsistent reserved - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (dlc=0x%04X, ctr=0x%04X)\n", head, tail, level, dlc, ctr);
            assert(false); // Fatal error, inconsistent state
        }

        // Nothing to read, the first entry is still in reserved state
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }

    // This should never fail
    // An committed entry must have a valid length and an XCP ODT in it
    if (!((ctr == CTR_COMMITTED) && (dlc > 0) && (dlc <= XCPTL_MAX_DTO_SIZE) && (first_entry->data[1] == 0xAA || first_entry->data[0] >= 0xFC))) {
        DBG_PRINTF_ERROR("queuePop initial: inconsistent commit - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (dlc=0x%04X, ctr=0x%04X, res=0x%02X)\n", head, tail, level, dlc,
                         ctr, first_entry->data[1]);
        assert(false); // Fatal error, corrupt committed state
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }

    // Set and increment the transport layer packet counter
    // The packet counter is obtained from the XCP transport layer
    ctr_dlc = ((uint32_t)XcpTlGetCtr() << 16) | dlc;
    atomic_store_explicit(&first_entry->ctr_dlc, ctr_dlc, memory_order_release);

    // First entry is ok now
    total_len = dlc + XCPTL_TRANSPORT_LAYER_HEADER_SIZE; // Include the transport layer header size

// Check for more packets to concatenate in a message segment with maximum of XCPTL_MAX_SEGMENT_SIZE, by repeating this procedure
#ifdef QUEUE_ACCUMULATE_PACKETS
    if (accumulate) {
        uint32_t offset = first_offset + total_len;
        uint32_t max_offset = first_offset + level - 1;
        if (max_offset >= queue->h.queue_size) {
            max_offset = queue->h.queue_size - 1; // Don't read over wrap around
            DBG_PRINTF6("%u-%u: queuePop: max_offset wrapped around, head=%" PRIu64 ", tail=%" PRIu64 ", level=%u, queue_size=%u\n", first_offset, max_offset, head, tail, level,
                        queue->h.queue_size);
        }

        for (;;) {
            // Check if there is another entry in the queue to accumulate
            // It is safe to read until max_offset calculated from the consistent head
            // Just stop on wrap around
            if (offset > max_offset) {
                break; // Nothing more safe to read in queue
            }

            tXcpDtoMessage *entry = (tXcpDtoMessage *)(queue->buffer + offset);

            // Check the entry commit state
            uint32_t ctr_dlc = atomic_load_explicit(&entry->ctr_dlc, memory_order_acquire);
            uint16_t dlc = ctr_dlc & 0xFFFF;          // Transport layer packet data length
            uint16_t ctr = (uint16_t)(ctr_dlc >> 16); // Transport layer counter
            if (ctr != CTR_COMMITTED) {

                if (ctr != CTR_RESERVED) {
                    DBG_PRINTF_ERROR("queuePop: inconsistent reserved - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (dlc=0x%04X, ctr=0x%04X)\n", head, tail, level, dlc, ctr);
                    assert(false);
                }

                // Nothing more to concat, the entry is still in reserved state
                break;
            }

            // Check consistency, this should never fail
            if (!((ctr == CTR_COMMITTED) && (dlc > 0) && (dlc <= XCPTL_MAX_DTO_SIZE) && (entry->data[1] == 0xAA || entry->data[0] >= 0xFC))) {
                DBG_PRINTF_ERROR("queuePop: inconsistent commit - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (dlc=0x%04X, ctr=0x%04X, res=0x%02X)\n", head, tail, level, dlc,
                                 ctr, entry->data[1]);
                assert(false); // Fatal error, corrupt committed state
                break;
            }

            uint16_t len = dlc + XCPTL_TRANSPORT_LAYER_HEADER_SIZE;

            // Check if this entry fits into the segment
            if (total_len + len > XCPTL_MAX_SEGMENT_SIZE) {
                break; // Max segment size reached
            }

            // Add this entry
            total_len += len;
            offset += len;

            ctr_dlc = ((uint32_t)XcpTlGetCtr() << 16) | dlc;
            atomic_store_explicit(&entry->ctr_dlc, ctr_dlc, memory_order_release);

        } // for(;;)
    }
#endif // QUEUE_ACCUMULATE_PACKETS

    assert(total_len > 0 && total_len <= XCPTL_MAX_SEGMENT_SIZE);
    tQueueBuffer ret = {
        .buffer = (uint8_t *)first_entry,
        .size = total_len,
    };
    return ret;
}

// Advance the transmit queue tail by the message length obtained from the last queuePop call
// Segments obtained from queuePop must be released immediately with this function
void queueRelease(tQueueHandle queue_handle, const tQueueBuffer *queue_buffer) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);
    assert(queue_buffer->size > 0 && queue_buffer->size <= XCPTL_MAX_SEGMENT_SIZE);

#if defined(QUEUE_NO_LOCK)
    // Clear the entire memory completely, to avoid inconsistent reserved states after incrementing the head in the producer
    // This is the tradeoff of not using a seq lock, more cache activity, but no producer-consumer syncronization need
    // This might be optimal for medium data throughput
    memset(queue_buffer->buffer, 0, queue_buffer->size);
    atomic_fetch_add_explicit(&queue->h.tail, queue_buffer->size, memory_order_release);
#else
    atomic_fetch_add_explicit(&queue->h.tail, queue_buffer->size, memory_order_relaxed);
#endif
}

tQueueBuffer queuePeek(tQueueHandle queue_handle, uint32_t index, uint32_t *packets_lost) {
    assert(index == 0); // Only support peeking the first entry for now, this can be implemented later if needed
    return queuePop(queue_handle, false, false, packets_lost);
}

#endif // OPTION_QUEUE_64_VAR_SIZE
#endif
