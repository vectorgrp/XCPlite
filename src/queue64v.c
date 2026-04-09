/*----------------------------------------------------------------------------
| File:
|   queue64v.c
|
| Description:
|   Lockless, variable entry size queue
|   Multi producer single consumer (producer side is thread safe and lockless)
|   Designed for x86 strong and ARM weak memory model
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "platform.h"   // for PLATFORM_64BIT
#include "xcplib_cfg.h" // for OPTION_QUEUE_64_VAR_SIZE and OPTION_ENABLE_DBG_PRINTS

// Only for 64 Bit platforms, no Windows, requires Atomics
#if defined(PLATFORM_64BIT) && !defined(_WIN) && !defined(OPTION_ATOMIC_EMULATION)
#ifdef OPTION_QUEUE_64_VAR_SIZE

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

// Turn of misaligned atomic access warnings
// Alignment is assured by the queue header and the queue entry size alignment
#ifdef __GNUC__
#endif
#ifdef __clang__
#pragma GCC diagnostic ignored "-Watomic-alignment"
#endif
#ifdef _MSC_VER
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
#error "C11 atomics are not supported on this platform, but required for queue64v.c"
#endif

// Test atomic_uint_least32_t availability
static_assert(sizeof(atomic_uint_least32_t) == 4, "atomic_uint_least32_t must be 4 bytes");

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

#endif

//-------------------------------------------------------------------------------------------------------------------------------------------------------

// Queue entry states (higher 16 bit of header, lower 16 bit is used for payload size)
#define CTR_RESERVED 0x0000u  // Reserved by producer, must be 0 because consumer clears the memory before releasing the entry
#define CTR_COMMITTED 0xCCCCu // Committed by producer

// Queue entry with header and payload
// Header is used for synchronization and state management, payload is used for user payload and optional user header data
#pragma pack(push, 1)
typedef struct {
    atomic_uint_least32_t header;
    uint8_t data[];
} tQueueEntry;
#pragma pack(pop)

#define QUEUE_MAGIC 0x26031961DEADBEEFULL
#define QUEUE_ENTRY_HEADER_SIZE sizeof(atomic_uint_least32_t)

// Queue header
// Aligned to cache line size
typedef union QueueHeader {
    struct {
        // Shared state
        atomic_uint_fast64_t head;         // Consumer reads from head
        atomic_uint_fast64_t tail;         // Producers write to tail
        atomic_uint_fast32_t packets_lost; // Packet lost counter, incremented by producers when a queue entry could not be acquired
        atomic_uint_fast64_t flush_offset; // Flush request head, set by producers to request the consumer to prioritize packets

        // Consumer state for optimized peek loop
        uint32_t cached_peek_index; // Cached index for optimized peek loop
        uint64_t cached_peek_tail;  // Cached offset for optimized peek loop

        // Constant
        uint64_t magic;      // Magic value for sanity checks
        uint32_t queue_size; // Size of queue data buffer in bytes (for entry offset wrapping, with wrap around space at the end)
        bool from_memory;    // Indicates whether the queue was initialized from user provided memory (true) or allocated by the queue implementation (false)
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

tQueueHandle queueInitFromMemory(void *queue_memory, size_t queue_memory_size, bool clear_queue, uint64_t *out_buffer_size) {

    tQueue *queue = NULL;

    DBG_PRINTF6("queueInitFromMemory: queue_memory=%p, queue_memory_size=%zu, clear_queue=%d\n", queue_memory, queue_memory_size, clear_queue);
    // Allocate the queue memory
    if (queue_memory == NULL) {
        assert(queue_memory_size > 0);
        size_t aligned_size = (queue_memory_size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1); // Align to cache line size
        assert(aligned_size <= 100ULL * 1024 * 1024); // Sanity check for size, 100 MiB should be enough for a queue, if you need more, increase this limit or remove it
        queue = (tQueue *)aligned_alloc(CACHE_LINE_SIZE, aligned_size);
        assert(queue != NULL);
        assert(queue && ((uint64_t)queue % CACHE_LINE_SIZE) == 0);
        memset(queue, 0, aligned_size);
        queue->h.from_memory = false;
        queue->h.magic = QUEUE_MAGIC;
        // Reserve header and entry wrap around space at the end of the buffer for maximum entry size QUEUE_MAX_ENTRY_SIZE
        queue->h.queue_size = ((aligned_size - sizeof(tQueueHeader)) - (QUEUE_MAX_ENTRY_SIZE + QUEUE_ENTRY_HEADER_SIZE)) & ~(QUEUE_PAYLOAD_SIZE_ALIGNMENT - 1);
        clear_queue = true;
    }

    // Queue memory is provided by the caller and should be initialized
    else if (clear_queue) {
        assert(queue_memory != NULL);
        queue = (tQueue *)queue_memory;
        assert(queue_memory_size > 0);
        assert(queue_memory_size <= 100ULL * 1024 * 1024); // Sanity check for size, 100 MiB should be enough for a queue, if you need more, increase this limit or remove it
        memset(queue, 0, queue_memory_size);
        queue->h.from_memory = true;
        queue->h.magic = QUEUE_MAGIC;
        // Reserve header and entry wrap around space at the end of the buffer for maximum entry size QUEUE_MAX_ENTRY_SIZE
        queue->h.queue_size = ((queue_memory_size - sizeof(tQueueHeader)) - (QUEUE_MAX_ENTRY_SIZE + QUEUE_ENTRY_HEADER_SIZE)) & ~(QUEUE_PAYLOAD_SIZE_ALIGNMENT - 1);
    }

    // Queue is provided by the caller and is already initialized
    else {
        queue = (tQueue *)queue_memory;
        if (queue->h.magic != QUEUE_MAGIC)
            return NULL; // Invalid queue
    }

    DBG_PRINT3("Init transport layer lockless queue (queue64v)\n");
    DBG_PRINTF3("  alignment=%u, data buffer size=%u, max payload %u bytes, overall %uKiB used\n", //
                QUEUE_PAYLOAD_SIZE_ALIGNMENT, queue->h.queue_size, QUEUE_MAX_ENTRY_SIZE - QUEUE_ENTRY_USER_HEADER_SIZE,
                (uint32_t)((queue->h.queue_size + sizeof(tQueueHeader)) / 1024));

    if (clear_queue) {
        queueClear((tQueueHandle)queue); // Clear the queue
    }
    queue->h.cached_peek_index = 0;
    queue->h.cached_peek_tail = 0;

    if (out_buffer_size) {
        *out_buffer_size = 0;
    }

    // Checks
    assert(atomic_is_lock_free(&((tQueue *)queue_memory)->h.head));
    assert((queue->h.queue_size & (QUEUE_PAYLOAD_SIZE_ALIGNMENT - 1)) == 0);

    return (tQueueHandle)queue;
}

void queueClear(tQueueHandle queue_handle) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    atomic_store_explicit(&queue->h.head, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.tail, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.packets_lost, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.flush_offset, 0xFFFFFFFFFFFFFFFFULL, memory_order_relaxed);
    queue->h.cached_peek_index = 0;
    queue->h.cached_peek_tail = 0;
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

    if (!queue->h.from_memory) {
        free(queue);
    }

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

    DBG_PRINTF6("queueAcquire: acquire entry of size %u\n", packet_len);

    // Align the entry length
    uint16_t entry_len = packet_len + QUEUE_ENTRY_USER_HEADER_SIZE;
#if QUEUE_PAYLOAD_SIZE_ALIGNMENT == 2
    entry_len = (uint16_t)((entry_len + 1) & 0xFFFE); // Add fill %2
#error "QUEUE_PAYLOAD_SIZE_ALIGNMENT == 2 is not supported, use 4"
#endif
#if QUEUE_PAYLOAD_SIZE_ALIGNMENT == 4
    entry_len = (uint16_t)((entry_len + 3) & 0xFFFC); // Add fill %4
#endif
#if QUEUE_PAYLOAD_SIZE_ALIGNMENT == 8
    entry_len = (uint16_t)((entry_len + 7) & 0xFFF8); // Add fill %8
#endif
    assert(entry_len <= QUEUE_MAX_ENTRY_SIZE);

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

    // CAS loop
    // In reserved state, the message entry is between tail and head, has valid dlc and ctr must be 0
    // This means the ctr must be 0 before the head is incremented
    for (;;) {

        // Check for overrun
        if (queue->h.queue_size - (entry_len + QUEUE_ENTRY_HEADER_SIZE) < (head - tail)) {
            break; // Overrun
        }

        // Try to increment the head
        // Compare exchange weak in acq_rel/acq mode serializes with other producers, false negatives will spin
        if (atomic_compare_exchange_weak_explicit(&queue->h.head, &head, head + (entry_len + QUEUE_ENTRY_HEADER_SIZE), memory_order_acq_rel, memory_order_acquire)) {
            entry = (tQueueEntry *)(queue->buffer + (head % queue->h.queue_size));
            atomic_store_explicit(&entry->header, (CTR_RESERVED << 16) | (uint32_t)entry_len, memory_order_release);
            break;
        }

        // Refresh tail on each iteration ?
        // It is probably more efficient, to keep the tail stale and save the cost for the atomic load on each iteration
        // If the queue is already saturated, packet loss will happen anyway

        // No hint, spin count is usually very low and we prefer the locked sequence as fast as possible
        // spin_loop_hint();

        // Get spin count statistics
#ifdef TEST_ACQUIRE_LOCK_TIMING
        spin_count++;
        // assert(spin_count < 100); // No reason to be afraid about the spin count, enable spin count statistics to check
#endif

    } // for (;;)

#ifdef TEST_ACQUIRE_LOCK_TIMING
    lock_test_add_sample(get_timestamp_ns() - spin_start, spin_count);
#endif

    if (entry == NULL) {
        uint32_t lost = (uint32_t)atomic_fetch_add_explicit(&queue->h.packets_lost, 1, memory_order_acq_rel);
        if (lost == 0) {
            DBG_PRINTF6("Queue overrun, len=%u, head=%" PRIu64 ", tail=%" PRIu64 ", level=%u, size=%u\n", entry_len, head, tail, (uint32_t)(head - tail), queue->h.queue_size);
        }
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }

    tQueueBuffer ret = {
        .buffer = entry->data + QUEUE_ENTRY_USER_HEADER_SIZE, // Return the user buffer pointer, which is after the entry header and the optional user header
        .size = entry_len - QUEUE_ENTRY_USER_HEADER_SIZE,     // Return the size of the user buffer, excluding the entries user header
    };
    return ret;
}

void queuePush(tQueueHandle queue_handle, const tQueueBuffer *queue_buffer, bool flush) {

    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    DBG_PRINTF6("queuePush: push entry of size %u\n", queue_buffer->size);

    assert(queue_buffer != NULL);
    assert(queue_buffer->buffer != NULL);
    tQueueEntry *entry = (tQueueEntry *)(queue_buffer->buffer - QUEUE_ENTRY_HEADER_SIZE - QUEUE_ENTRY_USER_HEADER_SIZE); // Get the entry pointer from the user buffer pointer

    // Set flush request
    if (flush) {
        uint64_t flush_offset = (uint8_t *)entry - queue->buffer;                          // Get the entry offset from the buffer pointer
        atomic_store_explicit(&queue->h.flush_offset, flush_offset, memory_order_relaxed); // Set flush offset, used by the consumer to prioritize packets
    }

    // Set commit state and the complete user payload size (header+payload) in the entry_header
    // Release store - complete data is then visible to the consumer
    atomic_store_explicit(&entry->header, (CTR_COMMITTED << 16) | (uint32_t)(queue_buffer->size + QUEUE_ENTRY_USER_HEADER_SIZE), memory_order_release);
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
        *queue_max_level = queue->h.queue_size;
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
    assert(head >= tail);
    assert(head - tail <= queue->h.queue_size);
    return (uint32_t)(head - tail);
}

tQueueBuffer queuePeek(tQueueHandle queue_handle, uint32_t peek_index, uint32_t *packets_lost, bool *flush_requested) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    DBG_PRINTF6("queuePeek: peek_index=%u\n", peek_index);

    // Return the number of packets lost since the last call
    if (packets_lost != NULL) {
        uint32_t lost = (uint32_t)atomic_exchange_explicit(&queue->h.packets_lost, 0, memory_order_acq_rel);
        *packets_lost = lost;
        if (lost) {
            DBG_PRINTF6("queuePeek: packets lost since last call: %u\n", lost);
        }
    }

    uint16_t entry_size;
    tQueueEntry *entry;
    uint64_t peek_tail;
    uint32_t index;

    // Get the head which synchronizes the queue header cache line
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_acquire);

    // Start at index 0 or with the cached peek index and tail if feasible, to optimize the common case of sequential peeks without releases
    // Cache will be reset if a release happens
    if (peek_index > 0 && peek_index >= queue->h.cached_peek_index) {
        peek_tail = queue->h.cached_peek_tail; // Use cached tail for optimized peek loop
        index = queue->h.cached_peek_index;
        DBG_PRINTF6("queuePeek: using cached peek_index=%u\n", index);
    } else {
        peek_tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
        index = 0;
    }

    for (;;) {

        // Check if there is data in the queue at peek_tail
        if (head <= peek_tail) {
            queue->h.cached_peek_index = index;
            queue->h.cached_peek_tail = peek_tail;
            tQueueBuffer ret = {
                .buffer = NULL,
                .size = 0,
            };
            return ret;
        }

        entry = (tQueueEntry *)(queue->buffer + peek_tail % queue->h.queue_size);

        // Check if the entry is in commit state
        uint32_t header = atomic_load_explicit(&entry->header, memory_order_acquire);
        entry_size = header & 0xFFFF;                    // Entry size (excluding the entry header, but including the optional user header)
        uint16_t entry_state = (uint16_t)(header >> 16); // Commit state
        if (entry_state != CTR_COMMITTED) {

            // This should never happen
            // An entry is consistent, if it is neither in reserved or committed state
            if (entry_state != CTR_RESERVED) {
                DBG_PRINTF_ERROR("queuePeek: inconsistent reserved - h=%" PRIu64 ", t=%" PRIu64 ", entry: (entry_size=0x%04X, entry_state=0x%04X)\n", head, peek_tail, entry_size,
                                 entry_state);
                assert(false); // Fatal error, inconsistent state
                tQueueBuffer ret = {
                    .buffer = NULL,
                    .size = 0,
                };
                return ret;
            }

            // Nothing to read, the first entry is still in reserved state
            queue->h.cached_peek_index = index;
            queue->h.cached_peek_tail = peek_tail;

            // @@@@ TODO Add a timeout
            // Maybe add a timeout parameter to the peek function and return an error if the entry is in reserved state for too long, this would allow the consumer to detect
            // stalled producers and recover from it, e.g. by skipping the entry or resetting the queue A stalled producer is a producer that has reserved an entry but never
            // commits it, this can happen if the producer thread is stalled or crashes after reserving an entry, this would lead to a permanent stall of the consumer if the
            // consumer is waiting for the entry to be committed
            tQueueBuffer ret = {
                .buffer = NULL,
                .size = 0,
            };
            return ret;
        }

        // This should never fail
        // An committed entry must have a valid length
        if (!((entry_state == CTR_COMMITTED) && (entry_size > 0) && (entry_size <= QUEUE_ENTRY_USER_SIZE))) {
            DBG_PRINTF_ERROR("queuePeek: inconsistent commit - h=%" PRIu64 ", t=%" PRIu64 ",  entry: (entry_size=0x%04X, entry_state=0x%04X)\n", //
                             head, peek_tail, entry_size, entry_state);
            assert(false); // Fatal error, corrupt committed state
            tQueueBuffer ret = {
                .buffer = NULL,
                .size = 0,
            };
            return ret;
        }

        // Found the entry at the peek index, return it
        if (index == peek_index) {
            tQueueBuffer ret = {
                .buffer = (uint8_t *)entry + QUEUE_ENTRY_HEADER_SIZE,
                .size = entry_size,

            };

            // Advance the cached peek index and tail for optimized peek loop
            // The consumer can savely overwrite the header
            queue->h.cached_peek_index = index + 1;
            queue->h.cached_peek_tail = peek_tail + (entry_size + QUEUE_ENTRY_HEADER_SIZE);

            // Return whether a flush request is pending on this entry
            if (flush_requested != NULL) {
                uint64_t flush_offset = atomic_load_explicit(&queue->h.flush_offset, memory_order_relaxed); // We use relaxed, assuming the cache line is already up to date
                if (flush_offset == (uint64_t)((uint8_t *)entry - queue->buffer)) {
                    *flush_requested = true;
                    // Don't clear the flush offset, to avoid overwriting an updated flush offset, false flushes are less problem than missing flushes
                }
            }

            return ret;
        }

        // Move peek_tail to the next entry and increment index
        index++;
        peek_tail += (entry_size + QUEUE_ENTRY_HEADER_SIZE);
    }
}

void queueRelease(tQueueHandle queue_handle, const tQueueBuffer *queue_buffer) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);
    assert(queue_buffer->size > 0 && queue_buffer->size <= QUEUE_ENTRY_USER_SIZE);

    DBG_PRINTF6("queueRelease: release entry of size %u\n", queue_buffer->size);

    // Clear the entire memory completely, to avoid inconsistent reserved states after incrementing the head in the producer
    // This is the tradeoff of not using a fixed entry size, this approach might be optimal for medium data throughput,
    memset(queue_buffer->buffer - QUEUE_ENTRY_HEADER_SIZE, 0, queue_buffer->size + QUEUE_ENTRY_HEADER_SIZE);
    atomic_fetch_add_explicit(&queue->h.tail, queue_buffer->size + QUEUE_ENTRY_HEADER_SIZE, memory_order_relaxed); // Write access to tail is single threaded

    // Reset cached index and tail for optimized peek
    queue->h.cached_peek_index = 0;
    queue->h.cached_peek_tail = 0;
}

#endif // OPTION_QUEUE_64_VAR_SIZE
#endif // defined(PLATFORM_64BIT) && !defined(_WIN) && !defined(OPTION_ATOMIC_EMULATION)
