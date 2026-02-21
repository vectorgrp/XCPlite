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

#include "platform.h"   // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex, spinlock
#include "xcplib_cfg.h" // for OPTION_xxx

// Use queue32.c for 32 Bit platforms or on Windows
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

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...

#include "xcpEthTl.h" // for XcpTlGetCtr

// Turn of misaligned atomic access warnings
// Alignment is assured by the queue header and the queue entry size alignment
#ifdef __GNUC__
#endif
#ifdef __clang__
#pragma GCC diagnostic ignored "-Watomic-alignment"
#endif
#ifdef _MSC_VER
#endif

// Assume a maximum cache line size of 128 bytes
#define CACHE_LINE_SIZE 128u // Cache line size, used to align the queue header

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
// For high contention use test queue_test or example daq_test with xcp_client --upload-a2l --udp --mea .  --dest-addr 192.168.0.206
// Note that this tests have significant performance impact, do not turn on for production use !!!!!!!!!!!

#define TEST_ACQUIRE_LOCK_TIMING
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

//-------------------------------------------------------------------------------------------------------------------------------------------------------

// Queue entry states
#define CTR_RESERVED 0x0000u  // Reserved by producer
#define CTR_COMMITTED 0xCCCCu // Committed by producer

// Transport layer message header
#pragma pack(push, 1)
typedef struct {
    atomic_uint_least32_t header;
    uint8_t data[];
} tQueueEntry;
#pragma pack(pop)

#if QUEUE_ENTRY_USER_HEADER_SIZE != 4
#error "QUEUE_ENTRY_USER_HEADER_SIZE must be 4 for this implementation"
#endif

// Queue header
// Aligned to cache line size
typedef struct {
    // Shared state
    atomic_uint_fast64_t head;         // Consumer reads from head
    atomic_uint_fast64_t tail;         // Producers write to tail
    atomic_uint_fast32_t packets_lost; // Packet lost counter, incremented by producers when a queue entry could not be acquired
    ATOMIC_BOOL flush;

    // Consumer state for optimized peek loop
    uint32_t cached_peek_index; // Cached index for optimized peek loop
    uint64_t cached_peek_tail;  // Cached offset for optimized peek loop

    // Constant
    uint32_t queue_size;  // Size of queue in bytes (for entry offset wrapping)
    uint32_t buffer_size; // Size of overall queue data buffer in bytes
    bool from_memory;     // Queue memory from queueInitFromMemory
    uint8_t reserved[3];  // Header must be 8 byte aligned
} tQueueHeader;

static_assert(((sizeof(tQueueHeader) % 8) == 0), "QueueHeader size must be %8");

// Queue
typedef struct {
    tQueueHeader h;
    uint8_t buffer[];
} tQueue;

//-------------------------------------------------------------------------------------------------------------------------------------------------------

tQueueHandle queueInitFromMemory(void *queue_memory, size_t queue_memory_size, bool clear_queue, int64_t *out_buffer_size) {

    tQueue *queue = NULL;

    // Allocate the queue memory
    if (queue_memory == NULL) {
        size_t aligned_size = (queue_memory_size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1); // Align to cache line size
        queue = (tQueue *)aligned_alloc(CACHE_LINE_SIZE, aligned_size);
        assert(queue != NULL);
        assert(queue && ((uint64_t)queue % CACHE_LINE_SIZE) == 0); // Check alignment
        memset(queue, 0, aligned_size);                            // Clear memory
        queue->h.from_memory = false;
        queue->h.buffer_size = queue_memory_size - sizeof(tQueueHeader);
        queue->h.queue_size = queue->h.buffer_size - QUEUE_MAX_ENTRY_SIZE;
        clear_queue = true;
    }
    // Queue memory is provided by the caller
    else if (clear_queue) {
        queue = (tQueue *)queue_memory;
        memset(queue, 0, queue_memory_size); // Clear memory
        queue->h.from_memory = true;
        queue->h.buffer_size = queue_memory_size - sizeof(tQueueHeader);
        queue->h.queue_size = queue->h.buffer_size - QUEUE_MAX_ENTRY_SIZE;
    }

    // Queue is provided by the caller and already initialized
    else {
        queue = (tQueue *)queue_memory;
        assert(queue->h.from_memory == true);
        assert(queue->h.queue_size == queue->h.buffer_size - QUEUE_MAX_ENTRY_SIZE);
    }

    DBG_PRINT3("Init transport layer lockless queue\n");
    DBG_PRINTF3("  segmentsize=%u, alignment=%u, queue: %u entries of max %u bytes, %uKiB\n", QUEUE_SEGMENT_SIZE, QUEUE_PAYLOAD_SIZE_ALIGNMENT,
                queue->h.queue_size / QUEUE_MAX_ENTRY_SIZE, QUEUE_MAX_ENTRY_SIZE - QUEUE_ENTRY_USER_HEADER_SIZE, (uint32_t)((queue->h.buffer_size + sizeof(tQueueHeader)) / 1024));

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
    queue->h.cached_peek_index = 0;
    queue->h.cached_peek_tail = 0;
    DBG_PRINT4("queueClear\n");
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

tQueueBuffer queueAcquire(tQueueHandle queue_handle, uint16_t packet_len) {

    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);
    assert(packet_len > 0 && packet_len <= QUEUE_ENTRY_USER_PAYLOAD_SIZE);

    tQueueEntry *entry = NULL;

    // Align the message length
    uint16_t msg_len = packet_len + QUEUE_ENTRY_USER_HEADER_SIZE;
#if QUEUE_PAYLOAD_SIZE_ALIGNMENT == 2
    msg_len = (uint16_t)((msg_len + 1) & 0xFFFE); // Add fill %2
#error "QUEUE_PAYLOAD_SIZE_ALIGNMENT == 2 is not supported, use 4"
#endif
#if QUEUE_PAYLOAD_SIZE_ALIGNMENT == 4
    msg_len = (uint16_t)((msg_len + 3) & 0xFFFC); // Add fill %4
#endif
#if QUEUE_PAYLOAD_SIZE_ALIGNMENT == 8
    msg_len = (uint16_t)((msg_len + 7) & 0xFFF8); // Add fill %8
#error "QUEUE_PAYLOAD_SIZE_ALIGNMENT == 8 is not supported, use 4"
#endif

    assert(msg_len <= QUEUE_MAX_ENTRY_SIZE);

#ifdef TEST_ACQUIRE_LOCK_TIMING
    uint64_t spin_start = get_timestamp_ns();
    uint32_t spin_count = 0;
#endif

    // Prepare a new entry in reserved state
    uint64_t tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_acquire);

    for (;;) {

        // Check for overrun
        if (queue->h.queue_size - msg_len < head - tail) {
            break; // Overrun
        }

        // Try increment the head
        // Compare exchange weak, false negative ok
        if (atomic_compare_exchange_weak_explicit(&queue->h.head, &head, head + msg_len, memory_order_acq_rel, memory_order_acquire)) {
            entry = (tQueueEntry *)(queue->buffer + (head % queue->h.queue_size));
            atomic_store_explicit(&entry->header, (CTR_RESERVED << 16) | (uint32_t)(msg_len - QUEUE_ENTRY_USER_HEADER_SIZE), memory_order_release);
            break;
        }

        // Get spin count statistics
        // spin_loop_hint(); // No hint, spin count is usually low and the locked sequence should be as fast as possible
        // assert(spin_count < 100); // No reason to be afraid about the spin count, enable spin count statistics to check
#ifdef TEST_ACQUIRE_LOCK_TIMING
        spin_count++;
#endif

    } // for (;;)

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

void queuePush(tQueueHandle queue_handle, tQueueBuffer *const queue_buffer, bool flush) {

    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    // Set flush request
    if (flush) {
        atomic_store_explicit(&queue->h.flush, true, memory_order_relaxed); // Set flush flag, used by the consumer to prioritize packets
    }

    assert(queue_buffer != NULL);
    assert(queue_buffer->buffer != NULL);
    tQueueEntry *entry = (tQueueEntry *)(queue_buffer->buffer - QUEUE_ENTRY_USER_HEADER_SIZE); // Get the entry pointer from the user buffer pointer

    // Go to commit state
    // Complete data is then visible to the consumer
    atomic_store_explicit(&entry->header, (CTR_COMMITTED << 16) | (uint32_t)(queue_buffer->size - QUEUE_ENTRY_USER_HEADER_SIZE), memory_order_release);
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

tQueueBuffer queuePeek(tQueueHandle queue_handle, uint32_t peek_index, uint32_t *packets_lost) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    // Return the number of packets lost in the queue
    if (packets_lost != NULL) {
        uint32_t lost = (uint32_t)atomic_exchange_explicit(&queue->h.packets_lost, 0, memory_order_acq_rel);
        *packets_lost = lost;
        if (lost) {
            DBG_PRINTF_WARNING("queuePop: packets lost since last call: %u\n", lost);
        }
    }

    uint16_t entry_size;
    tQueueEntry *entry;
    uint64_t peek_tail;
    uint32_t index;

    // Start at index 0 or with the cached peek index and tail if feasible, to optimize the common case of sequential peeks without releases
    // Cache will be reset if a release happens
    // if (peek_index > 0 && peek_index >= queue->h.cached_peek_index) {
    //     peek_tail = queue->h.cached_peek_tail; // Use cached tail for optimized peek loop
    //     index = queue->h.cached_peek_index;
    // } else

    {
        peek_tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
        index = 0;
    }

    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_relaxed);

    for (;;) {

        // Check if there is data in the queue at peek_tail
        assert(head >= peek_tail);
        uint32_t level = (uint32_t)(head - peek_tail);
        assert(level <= queue->h.queue_size);
        if (level == 0) { // Queue is empty
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
        entry_size = header & 0xFFFF;                    // Payload data length
        uint16_t entry_state = (uint16_t)(header >> 16); // Commit state
        if (entry_state != CTR_COMMITTED) {

            // This should never happen
            // An entry is consistent, if it is neither in reserved or committed state
            if (entry_state != CTR_RESERVED) {
                DBG_PRINTF_ERROR("queuePop initial: inconsistent reserved - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (entry_size=0x%04X, entry_state=0x%04X)\n", head,
                                 peek_tail, level, entry_size, entry_state);
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
            tQueueBuffer ret = {
                .buffer = NULL,
                .size = 0,
            };
            return ret;
        }

        // XCP specific check
        // This should never fail
        // An committed entry must have a valid length and an XCP ODT in it
        if (!((entry_state == CTR_COMMITTED) && (entry_size > 0) && (entry_size <= QUEUE_ENTRY_USER_SIZE) && (entry->data[1] == 0xAA || entry->data[0] >= 0xFC))) {
            DBG_PRINTF_ERROR("queuePop initial: inconsistent commit - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (entry_size=0x%04X, entry_state=0x%04X, res=0x%02X)\n", head,
                             peek_tail, level, entry_size, entry_state, entry->data[1]);
            assert(false); // Fatal error, corrupt committed state
            tQueueBuffer ret = {
                .buffer = NULL,
                .size = 0,
            };
            return ret;
        }

        if (index == peek_index) {
            queue->h.cached_peek_index = index;
            queue->h.cached_peek_tail = peek_tail;
            tQueueBuffer ret = {
                .buffer = (uint8_t *)entry,
                .size = entry_size + QUEUE_ENTRY_USER_HEADER_SIZE,
            };
            return ret;
        }

        // Move peek_tail to the next entry and increment index
        index++;
        peek_tail += (entry_size + QUEUE_ENTRY_USER_HEADER_SIZE);
    }
}

void queueRelease(tQueueHandle queue_handle, tQueueBuffer *const queue_buffer) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);
    assert(queue_buffer->size > 0 && queue_buffer->size <= QUEUE_SEGMENT_SIZE);

    // Clear the entire memory completely, to avoid inconsistent reserved states after incrementing the head in the producer
    // This is the tradeoff of not using a seq lock, more cache activity, but no producer-consumer syncronization need
    // This might be optimal for medium data throughput
    memset(queue_buffer->buffer, 0, queue_buffer->size);
    atomic_fetch_add_explicit(&queue->h.tail, queue_buffer->size, memory_order_release);

    // Reset cached index and tail for optimized peek loop
    queue->h.cached_peek_index = 0;
    queue->h.cached_peek_tail = 0;
}

#endif // OPTION_QUEUE_64_VAR_SIZE
#endif
