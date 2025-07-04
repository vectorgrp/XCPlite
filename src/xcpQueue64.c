/*----------------------------------------------------------------------------
| File:
|   xcpQueue64.c
|
| Description:
|   XCP transport layer queue
|   Multi producer single consumer queue (producer side is thread safe and lockless)
|   Hardcoded for (ODT BYTE, fill BYTE, DAQ WORD,) 4 Byte XCP ODT header types
|   Queue entries include XCP message header, queue can accumulate multiple XCP packets to a segment
|   Lock free with minimal wait implementation using a seq_lock and a spin loop on the producer side
|   Optional mutex based mode for higher consumer throughput as a tradeoff for higher producer latency
|   Tested on ARM weak memory model
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "platform.h" // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex, spinlock

// Use xcpQueue32.c for 32 Bit platforms or on Windows
#if defined(PLATFORM_64BIT) && !defined(_WIN)

#include "xcpQueue.h"

#include <assert.h>    // for assert
#include <inttypes.h>  // for PRIu64
#include <stdatomic.h> // for atomic_
#include <stdbool.h>   // for bool
#include <stdint.h>    // for uint32_t, uint64_t, uint8_t, int64_t
#include <stdio.h>     // for NULL, snprintf
#include <stdlib.h>    // for free, malloc
#include <string.h>    // for memcpy, strcmp

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...

#include "xcpEthTl.h"  // for XcpTlGetCtr
#include "xcptl_cfg.h" // for XCPTL_TRANSPORT_LAYER_HEADER_SIZE, XCPTL_MAX_DTO_SIZE, XCPTL_MAX_SEGMENT_SIZE

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

// Assume a maximum cache line size of 128 bytes
#define CACHE_LINE_SIZE 128u // Cache line size, used to align the queue header

// Check for 64 Bit platform
#if (!defined(_LINUX64) && !defined(_MACOS)) || !defined(PLATFORM_64BIT)
#error "This implementation requires a 64 Bit Posix platform (_LINUX64 or _MACOS)"
#endif
static_assert(sizeof(void *) == 8, "This implementation requires a 64 Bit platform"); // This implementation requires 64 Bit Posix platforms

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

// Use a mutex for queue producers, this is a convenient default
// Might be the best solution for high throughput, when worst case producer latency is acceptable
// #define QUEUE_MUTEX

// Use a seq_lock to protect against inconsistency during the entry acquire, the queue is lockfree with minimal CAS spin wait in contention for increasing the head
// The consumer may heavily spin, to acquire a safe consistent head
// #define QUEUE_SEQ_LOCK

// No synchronisation between producer and consumer, producer CAS loop increments the head, consumer clears memory completely for consistent reservation state
// Tradeoff between consumer spin activity and consumer cache activity, might be the optimal solution for medium throughput
#define QUEUE_NO_LOCK

// Accumulate XCP packets to multiple XCP messages in a segment obtained with QueuePeek
#define QUEUE_ACCUMULATE_PACKETS // Accumulate XCP packets to multiple XCP messages obtained with QueuePeek

// Wait for at least QUEUE_PEEK_THRESHOLD bytes in the queue before returning a segment, to optimize efficiency
// @@@@ Experimental, not tested yet, could improve performance for high throughput
#define QUEUE_PEEK_THRESHOLD XCPTL_MAX_SEGMENT_SIZE

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Test queue acquire lock timing and spin lock performance test
// For high contention use
//   cargo test  --features=a2l_reader  -- --test-threads=1 --nocapture  --test test_multi_thread
// Note that this tests have significant performance impact, do not turn on for production use !!!!!!!!!!!

// #define TEST_ACQUIRE_LOCK_TIMING
#ifdef TEST_ACQUIRE_LOCK_TIMING
static MUTEX lockMutex = MUTEX_INTIALIZER;
static uint64_t lockTimeMax = 0;
static uint64_t lockTimeSum = 0;
static uint64_t lockCount = 0;
#define LOCK_TIME_HISTOGRAM_SIZE 100 // max 100us in 1us steps
#define LOCK_TIME_HISTOGRAM_STEP 10
#define HISTOGRAM_STEP 100
static uint64_t lockTimeHistogram[LOCK_TIME_HISTOGRAM_SIZE] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// There should be better alternatives in your target specific environment than this portable reference
// Select a clock mode appropriate for your platform, CLOCK_MONOTONIC_RAW is a good choice for high resolution and monotonicity
static uint64_t get_timestamp_ns(void) {
    static const uint64_t kNanosecondsPerSecond = 1000000000ULL;
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts); // NOLINT(missing-includes) // do **not** include internal "bits" headers directly.
    return ((uint64_t)ts.tv_sec) * kNanosecondsPerSecond + ((uint64_t)ts.tv_nsec);
}

// inline auto RdtscNow() noexcept -> std::uint64_t {
//     std::uint32_t low{0};
//     std::uint64_t high{0};
//     asm volatile("rdtsc" : "=a"(low), "=d"(high)); // NOLINT(hicpp-no-assembler)
//     return (high << 32) | low;                     // NOLINT
// }

#endif

// #define TEST_ACQUIRE_SPIN_COUNT
#ifdef TEST_ACQUIRE_SPIN_COUNT
#define SPIN_COUNT_HISTOGRAM_SIZE 100 // Up to 100 loops
static atomic_uint_least32_t spinCountHistogramm[SPIN_COUNT_HISTOGRAM_SIZE] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

};
#endif

// #define TEST_CONSUMER_SEQ_LOCK_SPIN_COUNT
#ifdef TEST_CONSUMER_SEQ_LOCK_SPIN_COUNT
#define SEQ_LOCK_HISTOGRAM_SIZE 200  // Up to 200 loops
static uint32_t seqLockMaxLevel = 0; // Maximum queue level reached
static atomic_uint_least32_t seqLockHistogramm[SEQ_LOCK_HISTOGRAM_SIZE] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

};
#endif

/*
--------------------------------------------------------------------------------------------------------
Comparison of different synchronisation methods

------------------------------------------------------
Results for test_multi_thread on MacBook Pro M3 Pro
Weak ARM memory model

Configuration:

const TEST_TASK_COUNT: usize = 50; // Number of test tasks to create
const TEST_SIGNAL_COUNT: usize = 32; // Number of signals is TEST_SIGNAL_COUNT + 5 for each task
const TEST_DURATION_MS: u64 = 10 * 1000; // Stop after TEST_DURATION_MS milliseconds
const TEST_CYCLE_TIME_US: u32 = 100; // Cycle time in microseconds
const TEST_QUEUE_SIZE: u32 = 1024 * 1024; // Size of the XCP server transmit queue in Bytes

Daq test results:
[INFO ]   cycle time = 100us
[INFO ]   task count = 50
[INFO ]   signals = 650
[INFO ]   events per task = 76011
[INFO ]   events total = 3800489
[INFO ]   bytes total = 1094540832
[INFO ]   events/s = 380011
[INFO ]   datarate = 109.443 MByte/s
[INFO ]   average task cycle time = 131.6u

Note that the figures below include the time to read the system clock

// QUEUE_MUTEX:
---------------
Producer acquire lock time statistics: lockCount=3800489, maxLockTime=150000ns,  avgLockTime=348ns
0us: 3764525
10us: 20200
20us: 7675
30us: 3692
40us: 1905
50us: 1136
60us: 644
70us: 346
80us: 218
90us: 97
100us: 33
110us: 14
120us: 2
130us: 1
150us: 1



// QUEUE_NO_LOCK:
------------------
Producer acquire lock time statistics: lockCount=3762386, maxLockTime=57958ns,  avgLockTime=127ns
0us: 3755968
10us: 5702
20us: 614
30us: 82
40us: 18
50us: 2

Producer acquire spin count statistics:
2: 126063
3: 27016
4: 5953
5: 1292
6: 127

Max queue level reached: 417268 of 2096124, 19%

// QUEUE_SEQ_LOCK:
------------------
Producer acquire lock time statistics: lockCount=3802893, maxLockTime=64000ns,  avgLockTime=104ns
0us: 3798678
10us: 3796
20us: 323
30us: 73
40us: 18
50us: 2
60us: 3

Producer acquire spin count statistics:
2: 94017
3: 17269
4: 3386
5: 591
6: 64
7: 1

Consumer seq lock spin loop statistics:
Max queue level reached: 163812 of 1047548, 15%
1: 83
2: 48352
3: 1084
4: 1050
5: 714
6: 502
7: 451
8: 362
9: 308
10: 289
11: 283
12: 268
13: 258
14: 241
15: 228
16: 215
17: 207
18: 199
19: 189
20: 185
21: 182
22: 176
23: 172
24: 161
25: 152
26: 147
27: 139
28: 135
29: 133
30: 130
31: 124
32: 117
33: 113
34: 111
35: 106
36: 103
37: 102
38: 101
39: 100
40: 97
41: 97
42: 95
43: 92
44: 91
45: 90
46: 90
47: 89
48: 86
49: 85
50: 85
51: 84



------------------------------------------------------
Results for test_multi_thread on Raspberry Pi 5

const TEST_TASK_COUNT: usize = 50; // Number of test tasks to create
const TEST_SIGNAL_COUNT: usize = 32; // Number of signals is TEST_SIGNAL_COUNT + 5 for each task
const TEST_DURATION_MS: u64 = 10 * 1000; // Stop after TEST_DURATION_MS milliseconds
const TEST_CYCLE_TIME_US: u32 = 200; // Cycle time in microseconds
const TEST_QUEUE_SIZE: u32 = 1024 * 256; // Size of the XCP server transmit queue in Bytes


Lock timing statistics: lockCount=1891973, maxLockTime=109167ns,  avgLockTime=146ns
0us: 1891855
10us: 52
20us: 8
30us: 27
40us: 23
50us: 4
70us: 1
80us: 1
90us: 1
100us: 1


// QUEUE_NO_LOCK:
------------------

Producer acquire lock time statistics: lockCount=105758, maxLockTime=10222ns,  avgLockTime=90ns
0us: 105757
10us: 1

Producer acquire spin count statistics:
2: 187

Max queue level reached: 220460 of 523260, 42%




Producer acquire lock time statistics: lockCount=1899659, maxLockTime=266315ns,  avgLockTime=89ns
0us: 1899619
10us: 8
20us: 3
30us: 12
40us: 16
260us: 1

Producer acquire spin count statistics:
2: 3125
3: 10

Max queue level reached: 77964 of 523260, 14%

*/

//-------------------------------------------------------------------------------------------------------------------------------------------------------

// Check preconditions
#define MAX_ENTRY_SIZE (XCPTL_MAX_DTO_SIZE + XCPTL_TRANSPORT_LAYER_HEADER_SIZE)
#if (MAX_ENTRY_SIZE % XCPTL_PACKET_ALIGNMENT) != 0
#error "MAX_ENTRY_SIZE should be aligned to XCPTL_PACKET_ALIGNMENT"
#endif

// Queue entry states
#define CTR_RESERVED 0x0000u  // Reserved by producer
#define CTR_COMMITTED 0xCCCCu // Committed by producer

static_assert(sizeof(atomic_uint_least32_t) == 4, "atomic_uint_least32_t must be 4 bytes");

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
    atomic_uint_fast64_t head;          // Consumer reads from head
    atomic_uint_fast64_t tail;          // Producers write to tail
    atomic_uint_least32_t packets_lost; // Packet lost counter, incremented by producers when a queue entry could not be acquired
    atomic_bool flush;

#if defined(QUEUE_SEQ_LOCK)
    // seq_lock is used to acquire an entry safely
    // A spin loop is used to increment the head
    // It is incremented by 0x0000000100000000 on lock and 0x0000000000000001 on unlock
    atomic_uint_fast64_t seq_lock;
    uint64_t seq_head; // Last head detected as consistent by the seq lock
#elif defined(QUEUE_MUTEX)
    MUTEX mutex; // Mutex for queue producers, producers contend on each other but not on the consumer
#endif
    // Constant
    uint32_t queue_size;  // Size of queue in bytes (for entry offset wrapping)
    uint32_t buffer_size; // Size of overall queue data buffer in bytes
    bool from_memory;     // Queue memory from QueueInitFromMemory
    uint8_t reserved[7];  // Header must be 8 byte aligned
} tQueueHeader;

static_assert(((sizeof(tQueueHeader) % 8) == 0), "QueueHeader size must be %8");

// Queue
typedef struct {
    tQueueHeader h;
    uint8_t buffer[];
} tQueue;

//-------------------------------------------------------------------------------------------------------------------------------------------------------

// Initialize a queue from given memory, a given existing queue or allocate a new queue
static tQueueHandle QueueInitFromMemory(void *queue_memory, uint32_t queue_memory_size, bool clear_queue) {

    tQueue *queue = NULL;

    // Allocate the queue memory
    if (queue_memory == NULL) {
        uint32_t aligned_size = (queue_memory_size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1); // Align to cache line size
        queue = (tQueue *)aligned_alloc(CACHE_LINE_SIZE, aligned_size);
        assert(queue != NULL);
        assert(queue && ((uint64_t)queue % CACHE_LINE_SIZE) == 0); // Check alignment
        memset(queue, 0, aligned_size);                            // Clear memory
        queue->h.from_memory = false;
        queue->h.buffer_size = queue_memory_size - (uint32_t)sizeof(tQueueHeader);
        queue->h.queue_size = queue->h.buffer_size - MAX_ENTRY_SIZE;
        clear_queue = true;
    }
    // Queue memory is provided by the caller
    else if (clear_queue) {
        queue = (tQueue *)queue_memory;
        memset(queue, 0, queue_memory_size); // Clear memory
        queue->h.from_memory = true;
        queue->h.buffer_size = queue_memory_size - (uint32_t)sizeof(tQueueHeader);
        queue->h.queue_size = queue->h.buffer_size - MAX_ENTRY_SIZE;
    }

    // Queue is provided by the caller and already initialized
    else {
        queue = (tQueue *)queue_memory;
        assert(queue->h.from_memory == true);
        assert(queue->h.queue_size == queue->h.buffer_size - MAX_ENTRY_SIZE);
    }

    DBG_PRINT3("Init XCP transport layer queue\n");
    DBG_PRINTF3("  XCPTL_MAX_SEGMENT_SIZE=%u, XCPTL_PACKET_ALIGNMENT=%u, queue: %u DTOs of max %u bytes, %uKiB\n", XCPTL_MAX_SEGMENT_SIZE, XCPTL_PACKET_ALIGNMENT,
                queue->h.queue_size / MAX_ENTRY_SIZE, MAX_ENTRY_SIZE, (uint32_t)((queue->h.buffer_size + sizeof(tQueueHeader)) / 1024));
#if defined(QUEUE_SEQ_LOCK)
    DBG_PRINT3("  QUEUE_SEQ_LOCK\n");
#endif
#if defined(QUEUE_NO_LOCK)
    DBG_PRINT3("  QUEUE_NO_LOCK\n");
#endif
#if defined(QUEUE_MUTEX)
    DBG_PRINT3("  QUEUE_MUTEX\n");
#endif

    if (clear_queue) {

#if defined(QUEUE_SEQ_LOCK)
        queue->h.seq_head = 0;
        atomic_store_explicit(&queue->h.seq_lock, 0, memory_order_relaxed); // Initialize the seq_lock
#elif defined(QUEUE_MUTEX)
        mutexInit(&queue->h.mutex, false, 1000);
#endif

        QueueClear((tQueueHandle)queue); // Clear the queue
    }

    // Checks
    assert(atomic_is_lock_free(&((tQueue *)queue_memory)->h.head));
    assert((queue->h.queue_size & (XCPTL_PACKET_ALIGNMENT - 1)) == 0);

    DBG_PRINT4("QueueInitFromMemory\n");
    return (tQueueHandle)queue;
}

// Clear the queue
void QueueClear(tQueueHandle queueHandle) {
    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);

    atomic_store_explicit(&queue->h.head, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.tail, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.packets_lost, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.flush, false, memory_order_relaxed);
#if defined(QUEUE_SEQ_LOCK)
    queue->h.seq_head = 0;
    atomic_store_explicit(&queue->h.seq_lock, 0, memory_order_relaxed);
#endif
    DBG_PRINT4("QueueClear\n");
}

// Create and initialize a new queue with a given size
tQueueHandle QueueInit(uint32_t queue_buffer_size) { return QueueInitFromMemory(NULL, queue_buffer_size + sizeof(tQueueHeader), true); }

// Deinitialize and free the queue
void QueueDeinit(tQueueHandle queueHandle) {
    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);

    // Print statistics
#ifdef TEST_ACQUIRE_LOCK_TIMING
    printf("\nProducer acquire lock time statistics: lockCount=%" PRIu64 ", maxLockTime=%" PRIu64 "ns,  avgLockTime=%" PRIu64 "ns\n", lockCount, lockTimeMax,
           lockTimeSum / lockCount);
    for (int i = 0; i < LOCK_TIME_HISTOGRAM_SIZE - 1; i++) {
        if (lockTimeHistogram[i])
            printf("%dus: %" PRIu64 "\n", i * LOCK_TIME_HISTOGRAM_STEP, lockTimeHistogram[i]);
    }
    if (lockTimeHistogram[LOCK_TIME_HISTOGRAM_SIZE - 1])
        printf(">%uus: %" PRIu64 "\n", LOCK_TIME_HISTOGRAM_SIZE * LOCK_TIME_HISTOGRAM_STEP, lockTimeHistogram[LOCK_TIME_HISTOGRAM_SIZE - 1]);
    printf("\n");
#endif
#ifdef TEST_ACQUIRE_SPIN_COUNT
    printf("Producer acquire spin count statistics: \n");
    for (int i = 0; i < SPIN_COUNT_HISTOGRAM_SIZE - 1; i++) {
        if (spinCountHistogramm[i] > 0)
            printf("%d: %u\n", i + 1, spinCountHistogramm[i]);
    }
    if (spinCountHistogramm[SPIN_COUNT_HISTOGRAM_SIZE - 1] > 0)
        printf(">%u: %u\n", SPIN_COUNT_HISTOGRAM_SIZE, spinCountHistogramm[SPIN_COUNT_HISTOGRAM_SIZE - 1]);
    printf("\n");
#endif
#ifdef TEST_CONSUMER_SEQ_LOCK_SPIN_COUNT
    printf("Consumer seq lock spin loop statistics: \n");
    printf("Max queue level reached: %u of %u, %u%%\n", seqLockMaxLevel, queue->h.queue_size, (seqLockMaxLevel * 100) / queue->h.queue_size);
    for (int i = 0; i < SEQ_LOCK_HISTOGRAM_SIZE - 1; i++) {
        if (seqLockHistogramm[i] > 0)
            printf("%d: %u\n", i + 1, seqLockHistogramm[i]);
    }
    if (seqLockHistogramm[SEQ_LOCK_HISTOGRAM_SIZE - 1] > 0)
        printf(">%u: %u\n", SEQ_LOCK_HISTOGRAM_SIZE, seqLockHistogramm[SEQ_LOCK_HISTOGRAM_SIZE - 1]);
    printf("\n");
#endif

    QueueClear(queueHandle);
#if defined(QUEUE_MUTEX)
    mutexDestroy(&queue->h.mutex);
#endif

    if (queue->h.from_memory) {
        // @@@@ TODO: QueueDeinit resets the shared flag, so the memory is freed multiple times if there are more than two queues accessing the same memory.
        queue->h.from_memory = false;
    } else {
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
tQueueBuffer QueueAcquire(tQueueHandle queueHandle, uint16_t packet_len) {

    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);
    assert(packet_len > 0 && packet_len <= XCPTL_MAX_DTO_SIZE);

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

    assert(msg_len <= MAX_ENTRY_SIZE);

#ifdef TEST_ACQUIRE_LOCK_TIMING
    uint64_t c = get_timestamp_ns();
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

#ifdef TEST_ACQUIRE_SPIN_COUNT
    uint32_t spin_count = 0;
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
#ifdef TEST_ACQUIRE_SPIN_COUNT
        if (spin_count++ >= SPIN_COUNT_HISTOGRAM_SIZE)
            spin_count = SPIN_COUNT_HISTOGRAM_SIZE - 1;
        atomic_fetch_add_explicit(&spinCountHistogramm[spin_count], 1, memory_order_relaxed);
#endif

    } // for (;;)
#if defined(QUEUE_SEQ_LOCK)
    atomic_fetch_add_explicit(&queue->h.seq_lock, 0x0000000000000001, memory_order_acq_rel);
#endif

    //----------------------------------------------
    // Use a mutex to protect the entry acquire
#elif defined(QUEUE_MUTEX)

    mutexLock(&queue->h.mutex);

    uint64_t tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_acquire);
    assert(head >= tail);
    if (queue->h.queue_size - msg_len >= head - tail) {
        entry = (tXcpDtoMessage *)(queue->buffer + (head % queue->h.queue_size));
        atomic_store_explicit(&entry->ctr_dlc, (CTR_RESERVED << 16) | (uint32_t)(msg_len - XCPTL_TRANSPORT_LAYER_HEADER_SIZE), memory_order_release);
        atomic_store_explicit(&queue->h.head, head + msg_len, memory_order_release);
    }

    mutexUnlock(&queue->h.mutex);

#endif

#ifdef TEST_ACQUIRE_LOCK_TIMING
    uint64_t d = get_timestamp_ns() - c;
    mutexLock(&lockMutex);
    if (d > lockTimeMax)
        lockTimeMax = d;
    int i = (d / 1000) / LOCK_TIME_HISTOGRAM_STEP;
    if (i < LOCK_TIME_HISTOGRAM_SIZE)
        lockTimeHistogram[i]++;
    else
        lockTimeHistogram[LOCK_TIME_HISTOGRAM_SIZE - 1]++;
    lockTimeSum += d;
    lockCount++;
    mutexUnlock(&lockMutex);
#endif

    if (entry == NULL) {
        uint32_t lost = atomic_fetch_add_explicit(&queue->h.packets_lost, 1, memory_order_acq_rel);
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

// Commit a buffer (returned from QueueAcquire)
void QueuePush(tQueueHandle queueHandle, tQueueBuffer *const queueBuffer, bool flush) {

    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);

    // Set flush request
    if (flush) {
        atomic_store_explicit(&queue->h.flush, true, memory_order_relaxed); // Set flush flag, used by the consumer to prioritize packets
    }

    assert(queueBuffer != NULL);
    assert(queueBuffer->buffer != NULL);
    tXcpDtoMessage *entry = (tXcpDtoMessage *)(queueBuffer->buffer - XCPTL_TRANSPORT_LAYER_HEADER_SIZE);

    // Go to commit state
    // Complete data is then visible to the consumer
    atomic_store_explicit(&entry->ctr_dlc, (CTR_COMMITTED << 16) | (uint32_t)(queueBuffer->size - XCPTL_TRANSPORT_LAYER_HEADER_SIZE), memory_order_release);
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
uint32_t QueueLevel(tQueueHandle queueHandle) {
    tQueue *queue = (tQueue *)queueHandle;
    if (queue == NULL)
        return 0;
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
    assert(head >= tail);
    assert(head - tail <= queue->h.queue_size);
    return (uint32_t)(head - tail);
}

// Check if there is a message segment (one or more accumulated packets) in the transmit queue
// Return the message length and a pointer to the message
// Returns the number of packets lost since the last call
// May not be called twice, each buffer must be released immediately with QueueRelease
// Is not thread safe, must be called from one consumer thread only
tQueueBuffer QueuePeek(tQueueHandle queueHandle, bool flush, uint32_t *packets_lost) {
    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);

    // Return the number of packets lost in the queue
    if (packets_lost != NULL) {
        uint32_t lost = atomic_exchange_explicit(&queue->h.packets_lost, 0, memory_order_acq_rel);
        *packets_lost = lost;
        if (lost) {
            DBG_PRINTF_WARNING("QueuePeek: packets lost since last call: %u\n", lost);
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
                sleepNs(100000); // Sleep for 100us to reduce CPU load
                spin_count = 0;  // Reset
            }

            // Get spin count statistics
#ifdef TEST_CONSUMER_SEQ_LOCK_SPIN_COUNT
            if (spin_count >= SEQ_LOCK_HISTOGRAM_SIZE)
                spin_count = SEQ_LOCK_HISTOGRAM_SIZE - 1;
            atomic_fetch_add_explicit(&seqLockHistogramm[spin_count], 1, memory_order_relaxed);
#endif

        } while ((seq_lock1 != seq_lock2) || ((seq_lock1 >> 32) != (seq_lock2 & 0xFFFFFFFF)));

        queue->h.seq_head = head; // Set the last head detected as consistent by the seq lock
    }

#elif defined(QUEUE_MUTEX)

    mutexLock(&queue->h.mutex);
    head = atomic_load_explicit(&queue->h.head, memory_order_relaxed);
    mutexUnlock(&queue->h.mutex);

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

#ifdef TEST_CONSUMER_SEQ_LOCK_SPIN_COUNT
    if (level > seqLockMaxLevel) {
        seqLockMaxLevel = level;
    }
#endif

    // Require a minimum amount of data, to optimize segment usage (less Ethernet frames)
    // Don't when there is a flush request from producer or consumer
#if defined(QUEUE_ACCUMULATE_PACKETS) && defined(QUEUE_PEEK_THRESHOLD)

    // Flush request ?
    if (atomic_load_explicit(&queue->h.flush, memory_order_relaxed)) {
        flush = true; // Flush request, set by the producer
        atomic_store_explicit(&queue->h.flush, false, memory_order_relaxed);
    }

    if ((level <= QUEUE_PEEK_THRESHOLD && !flush)) { // Queue is not above the minimum segment size
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }

#else
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
            DBG_PRINTF_ERROR("QueuePeek initial: inconsistent reserved - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (dlc=0x%04X, ctr=0x%04X)\n", head, tail, level, dlc, ctr);
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
        DBG_PRINTF_ERROR("QueuePeek initial: inconsistent commit - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (dlc=0x%04X, ctr=0x%04X, res=0x%02X)\n", head, tail, level, dlc,
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
// @@@@ TODO maybe optimize the duplicate code below
#ifdef QUEUE_ACCUMULATE_PACKETS
    uint32_t offset = first_offset + total_len;
    uint32_t max_offset = first_offset + level - 1;
    if (max_offset >= queue->h.queue_size) {
        max_offset = queue->h.queue_size - 1; // Don't read over wrap around
        DBG_PRINTF5("%u-%u: QueuePeek: max_offset wrapped around, head=%" PRIu64 ", tail=%" PRIu64 ", level=%u, queue_size=%u\n", first_offset, max_offset, head, tail, level,
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
                DBG_PRINTF_ERROR("QueuePeek: inconsistent reserved - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (dlc=0x%04X, ctr=0x%04X)\n", head, tail, level, dlc, ctr);
                assert(false);
            }

            // Nothing more to concat, the entry is still in reserved state
            break;
        }

        // Check consistency, this should never fail
        if (!((ctr == CTR_COMMITTED) && (dlc > 0) && (dlc <= XCPTL_MAX_DTO_SIZE) && (entry->data[1] == 0xAA || entry->data[0] >= 0xFC))) {
            DBG_PRINTF_ERROR("QueuePeek: inconsistent commit - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (dlc=0x%04X, ctr=0x%04X, res=0x%02X)\n", head, tail, level, dlc, ctr,
                             entry->data[1]);
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
#endif // QUEUE_ACCUMULATE_PACKETS

    assert(total_len > 0 && total_len <= XCPTL_MAX_SEGMENT_SIZE);
    tQueueBuffer ret = {
        .buffer = (uint8_t *)first_entry,
        .size = total_len,
    };
    return ret;
}

// Advance the transmit queue tail by the message length obtained from the last QueuePeek call
// Segments obtained from QueuePeek must be released immediately with this function
void QueueRelease(tQueueHandle queueHandle, tQueueBuffer *const queueBuffer) {
    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);
    assert(queueBuffer->size > 0 && queueBuffer->size <= XCPTL_MAX_SEGMENT_SIZE);

#if defined(QUEUE_NO_LOCK)
    // Clear the entires memory completely, to avoid inconsistent reserved states after incrementing the head in the producer
    // This is the tradeoff of not using a seq lock, more cache activity, but no producer-consumer syncronization need
    // This might be optimal for medium data throughput
    memset(queueBuffer->buffer, 0, queueBuffer->size);
    atomic_fetch_add_explicit(&queue->h.tail, queueBuffer->size, memory_order_release);
#else
    atomic_fetch_add_explicit(&queue->h.tail, queueBuffer->size, memory_order_relaxed);
#endif
}

#endif
