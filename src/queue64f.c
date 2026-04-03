/*----------------------------------------------------------------------------
| File:
|   queue64f.c
|
| Description:
|   Lockless, fixed entry size queue
|   Multi producer single consumer (producer side is thread safe and lockless)
|   Designed for x86 strong and ARM weak memory model
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "platform.h"   // for PLATFORM_64BIT
#include "xcplib_cfg.h" // for OPTION_QUEUE_64_FIX_SIZE  and OPTION_ENABLE_DBG_PRINTS

// Only for 64 Bit platforms, no Windows, requires Atomics
#if defined(PLATFORM_64BIT) && !defined(_WIN) && !defined(OPTION_ATOMIC_EMULATION)
#ifdef OPTION_QUEUE_64_FIX_SIZE

#include "queue.h"

#include <assert.h>    // for assert
#include <inttypes.h>  // for PRIu64
#include <stdatomic.h> // for atomic_
#include <stdbool.h>   // for bool
#include <stdint.h>    // for uint32_t, uint64_t, uint8_t, int64_t
#include <stdio.h>     // for NULL, snprintf
#include <stdlib.h>    // for free, malloc
#include <string.h>    // for memcpy, strcmp

#ifdef OPTION_ENABLE_DBG_PRINTS
#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#else
#define DBG_PRINTF_ERROR(s, ...)
#define DBG_PRINTF_WARNING(s, ...)
#define DBG_PRINTF3(s, ...)
#define DBG_PRINTF6(s, ...)
#define DBG_PRINT_ERROR(s)
#define DBG_PRINT_WARNING(s)
#define DBG_PRINT3(s)
#define DBG_PRINT6(s)
#endif
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Settings

// Assume a cache line size of 64 bytes for alignment and padding to avoid false sharing and to optimize performance on high contention
#define CACHE_LINE_SIZE 64u

// Overall fixed maximaum size of the queue entries in the queue buffer
// Including the user header and payload and the internal atomic queue entry state of 4 bytes
// Should be a multiple of cache line size to optimize performance and to avoid false sharing
#define QUEUE_ENTRY_SIZE (QUEUE_ENTRY_USER_SIZE + 4) // Includes entry_header sizeof(atomic_uint_least32_t) = 4 bytes

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Checks

// Turn of misaligned atomic access warnings for entry_header, alignment is assured
#ifdef __GNUC__
#endif
#ifdef __clang__
#pragma GCC diagnostic ignored "-Watomic-alignment"
#endif
#ifdef _MSC_VER
#endif

// Test atomic_uint_least32_t availability
static_assert(sizeof(atomic_uint_least32_t) == 4, "atomic_uint_least32_t must be 4 bytes");

// Check for 64 Bit non Windows platform
#if !defined(PLATFORM_64BIT) || defined(_WIN)
#error "This implementation requires a 64 Bit Posix platform"
#endif
static_assert(sizeof(void *) == 8, "This implementation requires a 64 Bit platform");

// Check for atomic support
#ifdef __STDC_NO_ATOMICS__
#error "C11 atomics are not supported on this platform, but required for queue64f.c"
#endif

// For optimal performance
#if (QUEUE_ENTRY_SIZE % CACHE_LINE_SIZE) != 0
#error "(QUEUE_ENTRY_USER_PAYLOAD_SIZE+8) should be modulo CACHE_LINE_SIZE for optimal performance"
#endif

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Test

// Queue acquire lock timing and spin
// For high contention test use example queue_test or daq_test with xcpclient --upload-a2l --udp --mea .  --dest-addr 192.168.0.206
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

Producer acquire lock time statistics:
  count=8178646  max_spins=5  max=55417ns  avg=95ns

Lock time histogram (8178646 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                    442594    5.41%  ##
  40-80ns                  4779332   58.44%  ##############################
  80-120ns                 1637800   20.03%  ##########
  120-160ns                 421861    5.16%  ##
  160-200ns                 413763    5.06%  ##
  200-240ns                 286929    3.51%  #
  240-280ns                  77721    0.95%
  280-320ns                  24987    0.31%
  320-360ns                  10565    0.13%
  360-400ns                   6956    0.09%
  400-600ns                  40240    0.49%
  600-800ns                   1853    0.02%
  800-1000ns                  1132    0.01%
  1000-1500ns                 3297    0.04%
  1500-2000ns                 3155    0.04%
  2000-3000ns                 4674    0.06%
  3000-4000ns                 4387    0.05%
  4000-6000ns                 3967    0.05%
  6000-8000ns                 2245    0.03%
  8000-10000ns                3315    0.04%
  10000-20000ns               7418    0.09%
  20000-40000ns                443    0.01%
  40000-80000ns                 12    0.00%


Results on Raspberry Pi 5 with 32 producers
---------------------------------------------------------------------------

Producer acquire lock time statistics:
  count=5453269  max_spins=2  max=134037ns  avg=127ns

Lock time histogram (5453269 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  40-80ns                  1910357   35.03%  ##############################
  80-120ns                  820903   15.05%  ############
  120-160ns                 456866    8.38%  #######
  160-200ns                1899104   34.83%  #############################
  200-240ns                 319835    5.87%  #####
  240-280ns                  37519    0.69%
  280-320ns                   5498    0.10%
  320-360ns                   1512    0.03%
  360-400ns                    373    0.01%
  400-600ns                    272    0.00%
  600-800ns                      7    0.00%
  1000-1500ns                   68    0.00%
  1500-2000ns                   49    0.00%
  2000-3000ns                  227    0.00%
  3000-4000ns                  143    0.00%
  4000-6000ns                  152    0.00%
  6000-8000ns                  357    0.01%
  8000-10000ns                  13    0.00%
  10000-20000ns                  3    0.00%
  20000-40000ns                  8    0.00%
  40000-80000ns                  2    0.00%
  80000-160000ns                 1    0.00%
*/
#endif

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Types tQueueEntry, tQueueHeader, tQueue (tQueueBuffer defined in queue.h)

// Queue entry states
#define ENTRY_COMMITTED 0xCCCCUL // high word of the entry_header if entry is committed by the producer, otherwise always 0
#define ENTRY_SIZE_MASK 0xFFFFUL // low word of the entry_header encodes the size of the payload in bytes

// Queue entry
// The atomic 32 bit entry_header is used for producer/consumer acq/rel synchronization
// It encodes the entry commit state and the entry payload length in a single atomic value
// The commit state must always be 0 (initial state of all entries) or ENTRY_COMMITTED

#pragma pack(push, 1)
typedef struct {
    atomic_uint_least32_t entry_header; // commit state (must be 0 or ENTRY_COMMITTED<<16) and user payload size in bytes
    uint8_t data[];                     // user header + user payload
} tQueueEntry;
#pragma pack(pop)

#define QUEUE_MAGIC 0x26031961DEADBEEFULL

// Queue
// Padded to cache line size
typedef union QueueHeader {
    struct {
        // Shared state
        atomic_uint_fast64_t head;         // Consumer reads from head
        atomic_uint_fast64_t tail;         // Producers write to tail
        atomic_uint_fast32_t packets_lost; // Packet lost counter, incremented by producers when a queue entry could not be acquired
        atomic_uint_fast64_t flush_offset; // Flush request head, set by producers to request the consumer to prioritize packets

        // Constant
        uint64_t magic;       // Magic value for sanity checks
        uint32_t buffer_size; // Exact size of queue data buffer in bytes
        bool from_memory;     // Indicates whether the queue was initialized from user provided memory (true) or allocated by the queue implementation (false)
    };
    uint8_t padding[CACHE_LINE_SIZE]; //  Padding to cache line size
} tQueueHeader;

static_assert(sizeof(tQueueHeader) == CACHE_LINE_SIZE, "QueueHeader size must be CACHE_LINE_SIZE");

// Queue
typedef struct Queue {
    tQueueHeader h;
    uint8_t buffer[];
} tQueue;

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Implementation

tQueueHandle queueInitFromMemory(void *queue_memory, size_t queue_memory_size, bool clear_queue, uint64_t *out_buffer_size) {

    tQueue *queue = NULL;

    DBG_PRINTF6("queueInitFromMemory: queue_memory=%p, queue_memory_size=%zu, clear_queue=%d\n", queue_memory, queue_memory_size, clear_queue);

    // Allocate the queue memory
    if (queue_memory == NULL) {
        assert(queue_memory_size > 0);
        // Allocate the queue memory, rounded up to (QUEUE_ENTRY_USER_PAYLOAD_SIZE+8) size
        // Allocated memory includes the queue descriptor
        size_t aligned_memory_size =
            sizeof(tQueueHeader) + ((queue_memory_size / QUEUE_ENTRY_SIZE) * QUEUE_ENTRY_SIZE); // Round down to multiple of QUEUE_ENTRY_SIZE size plus header size
        assert(aligned_memory_size <=
               100ULL * 1024 * 1024); // Sanity check for size, 100 MiB should be more than enough for a queue, if you need more, increase this limit or remove it
        assert(aligned_memory_size % (CACHE_LINE_SIZE) == 0); // Check that the aligned size is a multiple of the cache line size
        queue = (tQueue *)aligned_alloc(CACHE_LINE_SIZE, aligned_memory_size);
        assert(queue != NULL);
        assert(((uint64_t)queue % CACHE_LINE_SIZE) == 0);         // Check alignment of the allocated memory
        assert(((uint64_t)queue->buffer % CACHE_LINE_SIZE) == 0); // Check alignment of the buffer memory
        memset(queue, 0, aligned_memory_size);                    // Clear complete queue memory
        queue->h.from_memory = false;
        queue->h.magic = QUEUE_MAGIC;
        queue->h.buffer_size = aligned_memory_size - (uint32_t)sizeof(tQueueHeader); // Set the queue buffer size (excluding the queue descriptor)
        assert((queue->h.buffer_size % QUEUE_ENTRY_SIZE) == 0);                      // Check that the queue buffer size is a multiple of the entry size
        assert((queue->h.buffer_size % CACHE_LINE_SIZE) == 0);                       // Check that the queue buffer size is a multiple of the cache line size
        assert((queue->h.buffer_size % QUEUE_PAYLOAD_SIZE_ALIGNMENT) == 0);          // Check that the queue buffer size is a multiple of the required alignment

        DBG_PRINT3("Init transport layer lockless queue (queue64f)\n");
        DBG_PRINTF3("  %u entries of max %u bytes user payload, %u bytes user header, %uKiB used\n", queue->h.buffer_size / QUEUE_ENTRY_SIZE, QUEUE_ENTRY_USER_PAYLOAD_SIZE,
                    QUEUE_ENTRY_USER_HEADER_SIZE, (uint32_t)(aligned_memory_size / 1024));
        clear_queue = true;

    }
    // Queue memory is provided by the caller and should be initialized
    else if (clear_queue) {
        queue = (tQueue *)queue_memory;
        memset(queue, 0, queue_memory_size);
        queue->h.from_memory = true;
        queue->h.magic = QUEUE_MAGIC;
        queue->h.buffer_size = (((queue_memory_size - sizeof(tQueueHeader)) / QUEUE_ENTRY_SIZE) * QUEUE_ENTRY_SIZE);
    }

    // Queue is provided by the caller and already initialized
    else {
        queue = (tQueue *)queue_memory;
        assert(queue->h.magic == QUEUE_MAGIC);
    }

    if (clear_queue) {
        queueClear((tQueueHandle)queue); // Clear the queue
    }

    if (out_buffer_size) {
        *out_buffer_size = 0;
    }

    // Checks
    assert(atomic_is_lock_free(&(queue->h.head)));

    return (tQueueHandle)queue;
}

void queueClear(tQueueHandle queue_handle) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);
    atomic_store_explicit(&queue->h.head, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.tail, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.packets_lost, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.flush_offset, 0xFFFFFFFFFFFFFFFFULL, memory_order_relaxed);
    memset(queue->buffer, 0, queue->h.buffer_size); // Clear queue buffer memory
    DBG_PRINT6("queueClear\n");
}

tQueueHandle queueInit(size_t queue_buffer_size) { return queueInitFromMemory(NULL, queue_buffer_size + sizeof(tQueueHeader), true, NULL); }

void queueDeinit(tQueueHandle queue_handle) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    // Print statistics
#ifdef TEST_ACQUIRE_LOCK_TIMING
    lock_test_print_results();
#endif

    queueClear(queue_handle);
    free(queue);
    DBG_PRINT6("QueueDeInit\n");
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Producer functions
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// For multiple producers !!

tQueueBuffer queueAcquire(tQueueHandle queue_handle, uint16_t packet_len) {

    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    if (!(packet_len > 0 && packet_len <= QUEUE_ENTRY_USER_PAYLOAD_SIZE)) {
        DBG_PRINTF_ERROR("Invalid packet_len %u, must be between 1 and %u\n", packet_len, QUEUE_ENTRY_USER_PAYLOAD_SIZE);
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }

    // Align the packet_len if required (improves the alignment of accumulated messages in a segment)
    uint16_t aligned_packet_len = packet_len;
#if QUEUE_PAYLOAD_SIZE_ALIGNMENT == 2
    msg_len = (uint16_t)((msg_len + 1) & 0xFFFE); // Add fill %2
#error "QUEUE_PAYLOAD_SIZE_ALIGNMENT == 2 is not supported, use 4"
#endif
#if QUEUE_PAYLOAD_SIZE_ALIGNMENT == 4
    aligned_packet_len = (uint16_t)((aligned_packet_len + 3) & 0xFFFC); // Add fill %4
#endif
#if QUEUE_PAYLOAD_SIZE_ALIGNMENT == 8
    aligned_packet_len = (uint16_t)((aligned_packet_len + 7) & 0xFFF8); // Add fill %8
#error "QUEUE_PAYLOAD_SIZE_ALIGNMENT == 8 is not supported, use 4"
#endif

    // Calculate the message len (the number of bytes used in the fixed size entry including the user header size)
    uint16_t msg_len = aligned_packet_len + QUEUE_ENTRY_USER_HEADER_SIZE;
    assert(msg_len <= QUEUE_ENTRY_USER_SIZE);

#ifdef TEST_ACQUIRE_LOCK_TIMING
    uint64_t spin_start = get_timestamp_ns();
    uint32_t spin_count = 0;
#endif

    // Prepare a new entry in reserved state
    tQueueEntry *entry = NULL;

    // Load the head first will synchronize the queue header cache line
    // The tail is read relaxed, head and tail are in the same cache line, but even if the tail could be stale, it is no problem
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_acquire);
    uint64_t tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
    uint32_t level;

    // Spin loop
    // In reserved state, the message entry is between tail and head, has valid dlc and ctr must be 0
    // This means the ctr must be 0 before the head is increment
    for (;;) {

        // Check for overrun
        level = (uint32_t)(head - tail);
        assert(queue->h.buffer_size >= level);
        assert((level % QUEUE_ENTRY_SIZE) == 0);
        if (queue->h.buffer_size == level) {
            break; // Overrun
        }

        // Try to increment the head
        // Compare exchange weak in acq_rel/acq mode serializes with other producers, false negatives will spin
        if (atomic_compare_exchange_weak_explicit(&queue->h.head, &head, head + QUEUE_ENTRY_SIZE, memory_order_acq_rel, memory_order_acquire)) {
            entry = (tQueueEntry *)(queue->buffer + (head % queue->h.buffer_size));
            // Store the overall user length (header+payload) (msg_len) in the entry_header
            // High word is still 0, which is the reserved state, not committed yet
            atomic_store_explicit(&entry->entry_header, (uint32_t)msg_len, memory_order_release);
            break;
        }
#ifdef TEST_ACQUIRE_LOCK_TIMING
        spin_count++;
#endif
    } // for (;;)

#ifdef TEST_ACQUIRE_LOCK_TIMING
    lock_test_add_sample(get_timestamp_ns() - spin_start, spin_count);
#endif

    if (entry == NULL) { // Overflow
        uint32_t lost = (uint32_t)atomic_fetch_add_explicit(&queue->h.packets_lost, 1, memory_order_acq_rel);
        if (lost == 0)
            DBG_PRINTF6("Queue overrun, msg_len=%u, h=%" PRIu64 ", t=%" PRIu64 ", level=%u, size=%u\n", msg_len, head, tail, level / QUEUE_ENTRY_SIZE, queue->h.buffer_size);
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }

    tQueueBuffer ret = {
        // Return a byte pointer to the payload data of this message entry, not to the user header
        .buffer = entry->data + QUEUE_ENTRY_USER_HEADER_SIZE,
        // Return the actual aligned size of the requested payload data size
        .size = aligned_packet_len, // Not including the user header size
    };

    assert((uint32_t)((uint8_t *)entry - queue->buffer) / QUEUE_ENTRY_SIZE < (queue->h.buffer_size / (QUEUE_ENTRY_SIZE)));
    DBG_PRINTF6("queueAcquire: acquired entry %u with size %u\n", (uint32_t)((uint8_t *)entry - queue->buffer) / QUEUE_ENTRY_SIZE, ret.size);
    return ret;
}

void queuePush(tQueueHandle queue_handle, const tQueueBuffer *queue_buffer, bool flush) {

    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    DBG_PRINTF6("queuePush: push entry of size %u\n", queue_buffer->size);

    assert(queue_buffer != NULL);
    assert(queue_buffer->buffer != NULL);
    tQueueEntry *entry = (tQueueEntry *)(queue_buffer->buffer - QUEUE_ENTRY_USER_HEADER_SIZE - 4);
    assert((uint32_t)((uint8_t *)entry - queue->buffer) % QUEUE_ENTRY_SIZE == 0); // Check that the entry pointer is correctly aligned to the entry size

    // Set flush request
    if (flush) {
        uint64_t flush_offset = (uint8_t *)entry - queue->buffer;                          // Get the entry offset from the buffer pointer
        atomic_store_explicit(&queue->h.flush_offset, flush_offset, memory_order_relaxed); // Set flush offset, used by the consumer to prioritize packets
    }

    // Set commit state and the complete user payload size (header+payload) in the entry_header
    // Release store - complete data is then visible to the consumer
    atomic_store_explicit(&entry->entry_header, (ENTRY_COMMITTED << 16) | (uint32_t)(queue_buffer->size + QUEUE_ENTRY_USER_HEADER_SIZE), memory_order_release);

    DBG_PRINTF6("queuePush: committed entry %u with size %u\n", (uint32_t)((uint8_t *)entry - queue->buffer) / QUEUE_ENTRY_SIZE, queue_buffer->size);
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Consumer functions
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Single consumer thread !!!!!!!!!!
// The consumer does not contend against the providers

uint32_t queueLevel(tQueueHandle queue_handle, uint32_t *queue_max_level) {
    tQueue *queue = (tQueue *)queue_handle;
    if (queue == NULL) {
        if (queue_max_level != NULL)
            *queue_max_level = 0;
        return 0;
    }
    if (queue_max_level != NULL)
        *queue_max_level = queue->h.buffer_size / QUEUE_ENTRY_SIZE;
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
    assert(head >= tail);
    uint32_t level = (uint32_t)(head - tail);
    assert(level <= queue->h.buffer_size);
    assert((level % QUEUE_ENTRY_SIZE) == 0);
    return level / QUEUE_ENTRY_SIZE;
}

// Check if there is a packet in the transmit queue at index
// Return the packet length and a pointer to the message
// Returns the number of packets lost since the last call
// May be called multiple times, even with the same index, but the entries obtained must be released in sequential index order
// Not thread safe, queuePeek and queueRelease must be called from one single consumer thread only
tQueueBuffer queuePeek(tQueueHandle queue_handle, uint32_t index, uint32_t *packets_lost, bool *flush_requested) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    // Return the number of packets lost in the queue
    if (packets_lost != NULL) {
        uint32_t lost = (uint32_t)atomic_exchange_explicit(&queue->h.packets_lost, 0, memory_order_acq_rel);
        *packets_lost = lost;
        if (lost) {
            DBG_PRINTF6("queuePeek: packets lost since last call: %u\n", lost);
        }
    }

    uint64_t tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed) + (index * QUEUE_ENTRY_SIZE);
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_relaxed);

    // Check if there is data in the queue at index
    if (head <= tail) {
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }
    uint32_t level = (uint32_t)(head - tail);
    assert(level <= queue->h.buffer_size);
    assert((level % QUEUE_ENTRY_SIZE) == 0);

    // Get a pointer to the entry in the queue
    tQueueEntry *entry = (tQueueEntry *)(queue->buffer + (tail % queue->h.buffer_size));
    assert((uint32_t)((uint8_t *)entry - queue->buffer) % QUEUE_ENTRY_SIZE == 0); // Check that the entry pointer is correctly aligned to the entry size

    //  Check the entry commit state
    uint32_t entry_header = atomic_load_explicit(&entry->entry_header, memory_order_acquire);
    uint16_t payload_length = (uint16_t)(entry_header & 0xFFFF); // Payload length
    uint16_t commit_state = (uint16_t)(entry_header >> 16);      // Commit state
    if (commit_state != ENTRY_COMMITTED) {

        // This should never happen
        // An entry is consistent, if it is either in initial or committed state
        if (commit_state != 0) {
            DBG_PRINTF_ERROR("queuePeek inconsistent reserved - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (entry_header=%" PRIx32 ")\n", head, tail, level, entry_header);
            assert(false); // Fatal error, inconsistent state
        }

        // Nothing to read, the entry is still in reserved state, currently being written by the producer
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        DBG_PRINTF6("queuePeek: entry %u is still in reserved state, queue level=%u \n", (uint32_t)((uint8_t *)entry - queue->buffer) / QUEUE_ENTRY_SIZE, level / QUEUE_ENTRY_SIZE);
        return ret;
    }

    // This should never fail
    // An committed entry must have a valid length
    if (!((commit_state == ENTRY_COMMITTED) && (payload_length > 0) && (payload_length <= QUEUE_ENTRY_USER_SIZE))) {
        DBG_PRINTF_ERROR("queuePeek: inconsistent commit - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (entry_header=%" PRIx32 ", res=0x%02X)\n", head, tail, level,
                         entry_header, entry->data[1]);
        assert(false); // Fatal error, corrupt committed state
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }

    tQueueBuffer ret = {
        .buffer = (uint8_t *)entry + 4, // Return a byte pointer to the user header of this message entry, not to the payload
        .size = payload_length,         // Includes the user header size
    };
    assert((uint32_t)((uint8_t *)entry - queue->buffer) % QUEUE_ENTRY_SIZE == 0);

    // Return whether a flush request is pending on this entry
    if (flush_requested != NULL) {
        uint64_t flush_offset = atomic_load_explicit(&queue->h.flush_offset, memory_order_relaxed); // We use relaxed, assuming the cache line is already up to date
        if (flush_offset == (uint8_t *)entry - queue->buffer) {
            *flush_requested = true;
            // Don't clear the flush offset, to avoid overwriting an updated flush offset, false flushes are less problem than missing flushes
        }
    }

    DBG_PRINTF6("queuePeek: returning entry %u with payload size %u\n", (uint32_t)((uint8_t *)entry - queue->buffer) / QUEUE_ENTRY_SIZE, ret.size);
    return ret;
}

void queueRelease(tQueueHandle queue_handle, const tQueueBuffer *queue_buffer) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);
    assert(queue_buffer != NULL);
    assert(queue_buffer->buffer != NULL);
    assert(queue_buffer->size > 0 && queue_buffer->size <= QUEUE_SEGMENT_SIZE);

    DBG_PRINTF6("queueRelease: releasing entry %u with payload size %u\n", (uint32_t)((uint8_t *)queue_buffer->buffer - queue->buffer - 4) / QUEUE_ENTRY_SIZE, queue_buffer->size);

    // Clear the entries commit state
    tQueueEntry *entry = (tQueueEntry *)(queue_buffer->buffer - 4);               // Get the pointer to the queue entry from the user header buffer pointer
    assert((uint32_t)((uint8_t *)entry - queue->buffer) % QUEUE_ENTRY_SIZE == 0); // Check that the entry pointer is correctly aligned to the entry size
    // @@@@ TODO: Check this release store is not strictly required for correctness, but removing it does not show any performance benefits
    atomic_store_explicit(&entry->entry_header, 0, memory_order_release);

    //  Increment the tail
    atomic_fetch_add_explicit(&queue->h.tail, QUEUE_ENTRY_SIZE, memory_order_release);
}

#endif // OPTION_QUEUE_64_FIX_SIZE
#endif // PLATFORM_64BIT && !defined(_WIN) && !defined(OPTION_ATOMIC_EMULATION)
