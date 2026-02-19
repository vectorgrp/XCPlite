/*----------------------------------------------------------------------------
| File:
|   xcpQueue64f.c
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

#include "platform.h"   // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex, spinlock
#include "xcplib_cfg.h" // for OPTION_xxx

// Not for 32 Bit platforms or on Windows, use xcpQueue32.c instead
#if defined(PLATFORM_64BIT) && !defined(_WIN) && !defined(OPTION_ATOMIC_EMULATION)

#ifdef OPTION_QUEUE_64_FIX_SIZE

#include "xcpQueue.h"

#include <assert.h>    // for assert
#include <inttypes.h>  // for PRIu64
#include <stdatomic.h> // for atomic_
#include <stdbool.h>   // for bool
#include <stdint.h>    // for uint32_t, uint64_t, uint8_t, int64_t
#include <stdio.h>     // for NULL, snprintf
#include <stdlib.h>    // for free, malloc
#include <string.h>    // for memcpy, strcmp

#include "dbg_print.h" // for DBG_PRINT

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Settings

#include "xcptl_cfg.h" // for XCPTL_TRANSPORT_LAYER_HEADER_SIZE, XCPTL_MAX_DTO_SIZE

// Queue entries include space for a consumer header with user defined size
// This size is configured to be the XCP transport layer header size

// Size of a queue entry from user perspective
#define QUEUE_ENTRY_USER_HEADER_SIZE XCPTL_TRANSPORT_LAYER_HEADER_SIZE
#define QUEUE_ENTRY_USER_PAYLOAD_SIZE XCPTL_MAX_DTO_SIZE
#define QUEUE_ENTRY_USER_SIZE (XCPTL_MAX_DTO_SIZE + XCPTL_TRANSPORT_LAYER_HEADER_SIZE)

// Note:
//  On the producer side, queue buffers from QueueAcquire don't include the user header space
//  On the consumer side, queue buffers from QueuePeek include the space for theuser header

// Assume a cache line size of 128 bytes for alignment and padding to avoid false sharing and to optimize performance on high contention
#define CACHE_LINE_SIZE 128u

// Overall size of the queue entries in the queue buffer
// Including the user header and payload and the internal atomic queue entry state of 4 bytes
// Should be a multiple of cache line size to optimize performance and to avoid false sharing
#define QUEUE_ENTRY_SIZE (QUEUE_ENTRY_USER_SIZE + 4) // Includes entry_header

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
#error "C11 atomics are not supported on this platform, but required for xcpQueue64.c"
#endif

// For optimal performance
#if (QUEUE_ENTRY_SIZE % CACHE_LINE_SIZE) != 0
#error "(QUEUE_ENTRY_USER_PAYLOAD_SIZE+8) should be modulo CACHE_LINE_SIZE for optimal performance"
#endif

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Test

// Queue acquire lock timing and spin performance test
// For high contention use
//   cargo test  --features=a2l_reader  -- --test-threads=1 --nocapture  --test test_multi_thread
// Note that this tests have significant performance impact, do not turn on for production use !!!!!!!!!!!

// #define TEST_ACQUIRE_LOCK_TIMING
#ifdef TEST_ACQUIRE_LOCK_TIMING
static MUTEX lock_mutex = MUTEX_INTIALIZER;
static uint64_t lock_time_max = 0;
static uint64_t lock_time_sum = 0;
static uint64_t lock_count = 0;
static uint64_t spin_count_max = 0;
#define LOCK_TIME_HISTOGRAM_SIZE 100
#define LOCK_TIME_HISTOGRAM_STEP 40 // 40ns steps (= 1 tick of ARM 25MHz generic timer on RPi), up to 4us
static uint64_t lock_time_histogram[LOCK_TIME_HISTOGRAM_SIZE] = {
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

/*
Results on MacBook Pro with Apple M3 (ARM64) with 8 producers

Region              Events      % of total   Root cause
0–80 ns             794k        54.8%        Uncontended (commpage clock overhead floor)
80–280 ns           640k        44.2%        Cross-cluster CAS coherence (P↔E cores)
280–3960 ns         13k          0.9%        SLC / interconnect fabric latency (flat tail)
>3960 ns             1.4k        0.1%        macOS scheduler preemption


Producer acquire lock time statistics: lock count=1448203, max spincount=3, max locktime=2736791ns,  avg locktime=119ns
0ns: 49706
40ns: 462944
80ns: 281414
120ns: 397774
160ns: 150411
200ns: 69710
240ns: 17424
280ns: 4951
320ns: 2105
360ns: 1088
400ns: 1066
440ns: 1160
480ns: 503
520ns: 454
560ns: 379
600ns: 308
640ns: 259
680ns: 227
720ns: 232
760ns: 214
800ns: 246
840ns: 228
880ns: 231
920ns: 201
1000ns: 164
>3960ns: 1438

*/
#endif

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Types tQueueEntry, tQueueHeader, tQueue (tQueueBuffer defined in xcpQueue.h)

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

// Queue
// Padded to cache line size
typedef struct {
    // Shared state
    atomic_uint_fast64_t head;         // Consumer reads from head
    atomic_uint_fast64_t tail;         // Producers write to tail
    atomic_uint_fast64_t packets_lost; // Packet lost counter, incremented by producers when a queue entry could not be acquired
    ATOMIC_BOOL flush;                 // Flush request from producer

    // Constant
    uint32_t queue_buffer_size; // Size of queue buffer in bytes
    uint8_t padding[QUEUE_ENTRY_SIZE - 8 - 8 - 8 - 8 - 4];
} tQueueHeader;

static_assert(((sizeof(tQueueHeader) % CACHE_LINE_SIZE) == 0), "QueueHeader size must be CACHE_LINE_SIZE");

// Queue
typedef struct {
    tQueueHeader h;
    uint8_t buffer[];
} tQueue;

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Implementation

// Clear the queue
void QueueClear(tQueueHandle queueHandle) {
    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);
    atomic_store_explicit(&queue->h.head, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.tail, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.packets_lost, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.flush, false, memory_order_relaxed);
    memset(queue->buffer, 0, queue->h.queue_buffer_size); // Clear queue buffer memory
    DBG_PRINT4("QueueClear\n");
}

// Allocate and initialize a new queue with a given size in bytes
tQueueHandle QueueInit(uint32_t queue_memory_size) {

    tQueue *queue = NULL;

    // Allocate the queue memory, rounded up to (QUEUE_ENTRY_USER_PAYLOAD_SIZE+8) size
    // Allocated memory includes the queue descriptor
    uint32_t aligned_memory_size = sizeof(tQueueHeader) + (queue_memory_size + (QUEUE_ENTRY_SIZE - 1)) & ~(QUEUE_ENTRY_SIZE - 1); // Round up to multiple of QUEUE_ENTRY_SIZE size
    queue = (tQueue *)aligned_alloc(CACHE_LINE_SIZE, aligned_memory_size);
    assert(queue != NULL);
    assert(((uint64_t)queue % CACHE_LINE_SIZE) == 0);                                  // Check alignment of the allocated memory
    assert(((uint64_t)queue->buffer % CACHE_LINE_SIZE) == 0);                          // Check alignment of the buffer memory
    memset(queue, 0, aligned_memory_size);                                             // Clear complete queue memory
    queue->h.queue_buffer_size = aligned_memory_size - (uint32_t)sizeof(tQueueHeader); // Set the queue buffer size (excluding the queue descriptor)
    assert((queue->h.queue_buffer_size % QUEUE_ENTRY_SIZE) == 0);                      // Check that the queue buffer size is a multiple of the entry size
    assert((queue->h.queue_buffer_size % CACHE_LINE_SIZE) == 0);                       // Check that the queue buffer size is a multiple of the cache line size
    assert((queue->h.queue_buffer_size % XCPTL_PACKET_ALIGNMENT) == 0);                // Check that the queue buffer size is a multiple of the XCP transport layer packet alignment

    DBG_PRINT3("Init XCP transport layer fixed entry size lockless queue\n");
    DBG_PRINTF3("  %u entries of max %u bytes user payload, %u bytes user header, %uKiB used\n", queue->h.queue_buffer_size / QUEUE_ENTRY_SIZE, QUEUE_ENTRY_USER_PAYLOAD_SIZE,
                QUEUE_ENTRY_USER_HEADER_SIZE, (uint32_t)(aligned_memory_size / 1024));

    QueueClear((tQueueHandle)queue); // Clear the queue

    // Checks
    assert(atomic_is_lock_free(&(queue->h.head)));

    return (tQueueHandle)queue;
}

// Deinitialize and free the queue
void QueueDeinit(tQueueHandle queueHandle) {
    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);

    // Print statistics
#ifdef TEST_ACQUIRE_LOCK_TIMING
    printf("\nProducer acquire lock time statistics: lock count=%" PRIu64 ", max spincount=%" PRIu64 ", max locktime=%" PRIu64 "ns,  avg locktime=%" PRIu64 "ns\n", lock_count,
           spin_count_max, lock_time_max, lock_time_sum / lock_count);
    for (int i = 0; i < LOCK_TIME_HISTOGRAM_SIZE - 1; i++) {
        if (lock_time_histogram[i])
            printf("%dns: %" PRIu64 "\n", i * LOCK_TIME_HISTOGRAM_STEP, lock_time_histogram[i]);
    }
    if (lock_time_histogram[LOCK_TIME_HISTOGRAM_SIZE - 1])
        printf(">%dns: %" PRIu64 "\n", (LOCK_TIME_HISTOGRAM_SIZE - 1) * LOCK_TIME_HISTOGRAM_STEP, lock_time_histogram[LOCK_TIME_HISTOGRAM_SIZE - 1]);
    printf("\n");
#endif

    QueueClear(queueHandle);
    free(queue);
    DBG_PRINT4("QueueDeInit\n");
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Producer functions
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// For multiple producers !!

// Get a buffer for a XCP packet with at least packet_len bytes
// Returns:
//  tQueueBuffer::buffer - pointer to payload buffer
//  tQueueBuffer:size - actual size of the buffer, at least requested packet_len
// Lockless and with minimal spin wait serialization on contention with other producers
tQueueBuffer QueueAcquire(tQueueHandle queueHandle, uint16_t packet_len) {

    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);
    assert(packet_len > 0 && packet_len <= QUEUE_ENTRY_USER_PAYLOAD_SIZE);

    // Align the packet_len if required (improves the alignment of accumulated messages in a segment)
    uint16_t aligned_packet_len = packet_len;
#if XCPTL_PACKET_ALIGNMENT == 2
    msg_len = (uint16_t)((msg_len + 1) & 0xFFFE); // Add fill %2
#error "XCPTL_PACKET_ALIGNMENT == 2 is not supported, use 4"
#endif
#if XCPTL_PACKET_ALIGNMENT == 4
    aligned_packet_len = (uint16_t)((aligned_packet_len + 3) & 0xFFFC); // Add fill %4
#endif
#if XCPTL_PACKET_ALIGNMENT == 8
    aligned_packet_len = (uint16_t)((aligned_packet_len + 7) & 0xFFF8); // Add fill %8
#error "XCPTL_PACKET_ALIGNMENT == 8 is not supported, use 4"
#endif
    assert(aligned_packet_len <= XCPTL_MAX_DTO_SIZE);

    // Calculate the message len (the number of bytes used in the fixed size entry including the user header size)
    uint16_t msg_len = aligned_packet_len + QUEUE_ENTRY_USER_HEADER_SIZE;
    assert(msg_len <= (QUEUE_ENTRY_SIZE - 8));

#ifdef TEST_ACQUIRE_LOCK_TIMING
    uint64_t c = get_timestamp_ns();
    uint32_t spin_count = 0;
#endif

    // Reserve a new entry
    tQueueEntry *entry = NULL;
    // In reserved state, the message entry is between tail and head, has valid dlc and ctr must be 0
    // This means the ctr must be 0 before the head is increment
    uint64_t tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_acquire);
    uint32_t level;
    for (;;) {

        // Check for overrun with the current head
        level = (uint32_t)(head - tail);
        assert(queue->h.queue_buffer_size >= level);
        assert((level % QUEUE_ENTRY_SIZE) == 0);
        if (queue->h.queue_buffer_size == level) {
            break; // Overrun
        }

        // Try to increment the head
        // Compare exchange weak in acq_rel/acq mode serializes with other producers, false negatives will spin
        if (atomic_compare_exchange_weak_explicit(&queue->h.head, &head, head + QUEUE_ENTRY_SIZE, memory_order_acq_rel, memory_order_acquire)) {
            entry = (tQueueEntry *)(queue->buffer + (head % queue->h.queue_buffer_size));
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
    uint64_t d = get_timestamp_ns() - c;
    mutexLock(&lock_mutex);
    if (spin_count > spin_count_max)
        spin_count_max = spin_count;
    if (d > lock_time_max)
        lock_time_max = d;
    int i = d / LOCK_TIME_HISTOGRAM_STEP;
    if (i < LOCK_TIME_HISTOGRAM_SIZE)
        lock_time_histogram[i]++;
    else
        lock_time_histogram[LOCK_TIME_HISTOGRAM_SIZE - 1]++;
    lock_time_sum += d;
    lock_count++;
    mutexUnlock(&lock_mutex);
#endif

    if (entry == NULL) { // Overflow
        uint32_t lost = (uint32_t)atomic_fetch_add_explicit(&queue->h.packets_lost, 1, memory_order_acq_rel);
        if (lost == 0)
            DBG_PRINTF_WARNING("Queue overrun, msg_len=%u, h=%" PRIu64 ", t=%" PRIu64 ", level=%u, size=%u\n", msg_len, head, tail, level / QUEUE_ENTRY_SIZE,
                               queue->h.queue_buffer_size);
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

    assert((uint32_t)((uint8_t *)entry - queue->buffer) / QUEUE_ENTRY_SIZE < (queue->h.queue_buffer_size / (QUEUE_ENTRY_SIZE)));
    DBG_PRINTF5("QueueAcquire: acquired entry %u with size %u\n", (uint32_t)((uint8_t *)entry - queue->buffer) / QUEUE_ENTRY_SIZE, ret.size);
    return ret;
}

// Commit a buffer (which was returned from QueueAcquire)
// Lockless and waitfree
// Indicate priority by setting flush = true
void QueuePush(tQueueHandle queueHandle, tQueueBuffer *const queueBuffer, bool flush) {

    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);

    // Set flush request
    if (flush) {
        atomic_store_explicit(&queue->h.flush, true, memory_order_relaxed); // Set flush flag, used by the consumer to prioritize packets
    }

    // Get the pointer to the queue entry from the payload buffer pointer
    assert(queueBuffer != NULL);
    assert(queueBuffer->buffer != NULL);
    tQueueEntry *entry = (tQueueEntry *)(queueBuffer->buffer - QUEUE_ENTRY_USER_HEADER_SIZE - sizeof(atomic_uint_least32_t));
    assert((uint32_t)((uint8_t *)entry - queue->buffer) % QUEUE_ENTRY_SIZE == 0); // Check that the entry pointer is correctly aligned to the entry size

    // Set commit state and the complete user payload size (header+payload) in the entry_header
    // Release store - complete data is then visible to the consumer
    atomic_store_explicit(&entry->entry_header, (ENTRY_COMMITTED << 16) | (uint32_t)(queueBuffer->size + QUEUE_ENTRY_USER_HEADER_SIZE), memory_order_release);

    DBG_PRINTF5("QueuePush: committed entry %u with size %u\n", (uint32_t)((uint8_t *)entry - queue->buffer) / QUEUE_ENTRY_SIZE, queueBuffer->size);
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Consumer functions
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Single consumer thread !!!!!!!!!!
// The consumer does not contend against the providers

// Get current transmit queue level in entries
// Is thread safe (no undefined behaviour), but may have false negatives for queue not empty in other threads
// Returns 0 when the queue is empty
uint32_t QueueLevel(tQueueHandle queueHandle, uint32_t *queue_max_level) {
    tQueue *queue = (tQueue *)queueHandle;
    if (queue == NULL) {
        if (queue_max_level != NULL)
            *queue_max_level = 0;
        return 0;
    }
    if (queue_max_level != NULL)
        *queue_max_level = queue->h.queue_buffer_size / QUEUE_ENTRY_SIZE;
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
    assert(head >= tail);
    uint32_t level = (uint32_t)(head - tail);
    assert(level <= queue->h.queue_buffer_size);
    assert((level % QUEUE_ENTRY_SIZE) == 0);
    return level / QUEUE_ENTRY_SIZE;
}

// Check if there is a packet in the transmit queue at index
// Return the packet length and a pointer to the message
// Returns the number of packets lost since the last call
// May be called multiple times, even with the same index, but the entries obtained must be released in sequential index order
// Not thread safe, QueuePeek and QueueRelease must be called from one single consumer thread only
tQueueBuffer QueuePeek(tQueueHandle queueHandle, int32_t index, bool flush, uint32_t *packets_lost) {
    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);
    (void)flush; // Unused consumer flush request, suppress warning
    // @@@@ TODO: Handle flush requests from producer

    // Return the number of packets lost in the queue
    if (packets_lost != NULL) {
        uint32_t lost = (uint32_t)atomic_exchange_explicit(&queue->h.packets_lost, 0, memory_order_acq_rel);
        *packets_lost = lost;
        if (lost) {
            DBG_PRINTF_WARNING("QueuePeek: packets lost since last call: %u\n", lost);
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
    assert(level <= queue->h.queue_buffer_size);
    assert((level % QUEUE_ENTRY_SIZE) == 0);

    // Get a pointer to the entry in the queue
    tQueueEntry *entry = (tQueueEntry *)(queue->buffer + (tail % queue->h.queue_buffer_size));
    assert((uint32_t)((uint8_t *)entry - queue->buffer) % QUEUE_ENTRY_SIZE == 0); // Check that the entry pointer is correctly aligned to the entry size

    //  Check the entry commit state
    uint32_t entry_header = atomic_load_explicit(&entry->entry_header, memory_order_acquire);
    uint16_t payload_length = (uint16_t)(entry_header & 0xFFFF); // Payload length
    uint16_t commit_state = (uint16_t)(entry_header >> 16);      // Commit state
    if (commit_state != ENTRY_COMMITTED) {

        // This should never happen
        // An entry is consistent, if it is either in initial or committed state
        if (commit_state != 0) {
            DBG_PRINTF_ERROR("QueuePeek inconsistent reserved - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (entry_header=%" PRIx32 ")\n", head, tail, level, entry_header);
            assert(false); // Fatal error, inconsistent state
        }

        // Nothing to read, the entry is still in reserved state, currently being written by the producer
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        DBG_PRINTF5("QueuePeek: entry %u is still in reserved state, queue level=%u \n", (uint32_t)((uint8_t *)entry - queue->buffer) / QUEUE_ENTRY_SIZE, level / QUEUE_ENTRY_SIZE);
        return ret;
    }

    // XCP use case specific consistency check for committed entries, can be adapted or deleted for other use cases
    // This should never happen
    // Assume this queue carries XCP DTO packets for consistency checks
    // An committed entry must have a valid length and an XCP ODT in it
    if (!((commit_state == ENTRY_COMMITTED) && (payload_length > 0) && (payload_length <= QUEUE_ENTRY_USER_SIZE) && (entry->data[4 + 1] == 0xAA || entry->data[4 + 0] >= 0xFC))) {
        DBG_PRINTF_ERROR("QueuePeek: inconsistent commit - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (entry_header=%" PRIx32 ", res=0x%02X)\n", head, tail, level,
                         entry_header, entry->data[1]);
        assert(false); // Fatal error, corrupt committed state
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }

    tQueueBuffer ret = {
        .buffer = (uint8_t *)entry + sizeof(atomic_uint_least32_t), // Return a byte pointer to the user header of this message entry, not to the payload
        .size = payload_length,                                     // Includes the user header size
    };
    assert((uint32_t)((uint8_t *)entry - queue->buffer) % QUEUE_ENTRY_SIZE == 0);
    DBG_PRINTF5("QueuePeek: returning entry %u with payload size %u\n", (uint32_t)((uint8_t *)entry - queue->buffer) / QUEUE_ENTRY_SIZE, ret.size);
    return ret;
}

// Advance the transmit queue tail
// Entries obtained from QueuePeek must be released in correct order !!!
// Is not thread safe, must be called from one single consumer thread only
void QueueRelease(tQueueHandle queueHandle, tQueueBuffer *const queueBuffer) {
    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);
    assert(queueBuffer != NULL);
    assert(queueBuffer->buffer != NULL);
    assert(queueBuffer->size > 0 && queueBuffer->size <= XCPTL_MAX_SEGMENT_SIZE);

    DBG_PRINTF5("QueueRelease: releasing entry %u with payload size %u\n",
                (uint32_t)((uint8_t *)queueBuffer->buffer - queue->buffer - sizeof(atomic_uint_least32_t)) / QUEUE_ENTRY_SIZE, queueBuffer->size);

    // Clear the entries commit state
    tQueueEntry *entry = (tQueueEntry *)(queueBuffer->buffer - sizeof(atomic_uint_least32_t)); // Get the pointer to the queue entry from the user header buffer pointer
    assert((uint32_t)((uint8_t *)entry - queue->buffer) % QUEUE_ENTRY_SIZE == 0);              // Check that the entry pointer is correctly aligned to the entry size
    atomic_store_explicit(&entry->entry_header, 0, memory_order_release);

    // Increment the tail
    atomic_fetch_add_explicit(&queue->h.tail, QUEUE_ENTRY_SIZE, memory_order_release);
}

#endif
#endif // OPTION_QUEUE_64_FIX_SIZE
