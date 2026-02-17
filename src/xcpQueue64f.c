/*----------------------------------------------------------------------------
| File:
|   xcpQueue64f.c
|
| Description:
|   XCP transport layer lockless, fixed max entry size queue
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

// Use xcpQueue32.c for 32 Bit platforms or on Windows
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

// Assume a maximum cache line size of 128 bytes
#define CACHE_LINE_SIZE 128u // Cache line size, used to align the queue header

// Check for 64 Bit non Windows platform
#if !defined(PLATFORM_64BIT) || defined(_WIN)
#error "This implementation requires a 64 Bit Posix platform"
#endif
static_assert(sizeof(void *) == 8, "This implementation requires a 64 Bit platform");

// Check for atomic support and atomic_uint_least32_t availability
#ifdef __STDC_NO_ATOMICS__
#error "C11 atomics are not supported on this platform, but required for xcpQueue64.c"
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

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Test queue acquire lock timing and spin performance test
// For high contention use
//   cargo test  --features=a2l_reader  -- --test-threads=1 --nocapture  --test test_multi_thread
// Note that this tests have significant performance impact, do not turn on for production use !!!!!!!!!!!

#define TEST_ACQUIRE_LOCK_TIMING
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

#endif

//-------------------------------------------------------------------------------------------------------------------------------------------------------

// Check preconditions
#define MAX_ENTRY_SIZE (XCPTL_MAX_DTO_SIZE + XCPTL_TRANSPORT_LAYER_HEADER_SIZE)
#if (MAX_ENTRY_SIZE % XCPTL_PACKET_ALIGNMENT) != 0
#error "MAX_ENTRY_SIZE should be aligned to XCPTL_PACKET_ALIGNMENT"
#endif

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
// Padded to cache line size
typedef struct {
    // Shared state
    atomic_uint_fast64_t head;         // Consumer reads from head
    atomic_uint_fast64_t tail;         // Producers write to tail
    atomic_uint_fast64_t peek_offset;  // Current peek position
    atomic_uint_fast32_t packets_lost; // Packet lost counter, incremented by producers when a queue entry could not be acquired
    ATOMIC_BOOL flush;                 // Flush request from producer

    // Constant
    uint32_t queue_size; // Size of queue in bytes (for entry offset wrapping)
    uint8_t padding[CACHE_LINE_SIZE - 8 - 8 - 8 - 4 - 8 - 4];
} tQueueHeader;

static_assert(((sizeof(tQueueHeader) % CACHE_LINE_SIZE) == 0), "QueueHeader size must be CACHE_LINE_SIZE");

// Queue
typedef struct {
    tQueueHeader h;
    uint8_t buffer[];
} tQueue;

//-------------------------------------------------------------------------------------------------------------------------------------------------------

// Clear the queue
void QueueClear(tQueueHandle queueHandle) {
    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);

    atomic_store_explicit(&queue->h.head, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.tail, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.packets_lost, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->h.flush, false, memory_order_relaxed);
    atomic_store_explicit(&queue->h.peek_offset, 0, memory_order_relaxed);

    DBG_PRINT4("QueueClear\n");
}

// Create and initialize a new queue with a given size in bytes
tQueueHandle QueueInit(uint32_t queue_memory_size) {

    tQueue *queue = NULL;

    // Allocate the queue memory, rounded up to cache line size, allocated memory includes the queue header
    uint32_t aligned_size = (queue_memory_size + CACHE_LINE_SIZE) & ~(CACHE_LINE_SIZE - 1); // Round up to cache line size
    queue = (tQueue *)aligned_alloc(CACHE_LINE_SIZE, aligned_size);
    assert(queue != NULL);
    assert(((uint64_t)queue % CACHE_LINE_SIZE) == 0); // Check alignment
    memset(queue, 0, aligned_size);                   // Clear memory
    queue->h.queue_size = aligned_size - (uint32_t)sizeof(tQueueHeader);

    DBG_PRINT3("Init XCP transport layer queue\n");
    DBG_PRINTF3("  XCPTL_MAX_SEGMENT_SIZE=%u, XCPTL_PACKET_ALIGNMENT=%u, queue: %u DTOs of max %u bytes, %uKiB\n", //
                XCPTL_MAX_SEGMENT_SIZE, XCPTL_PACKET_ALIGNMENT, queue->h.queue_size / MAX_ENTRY_SIZE, MAX_ENTRY_SIZE - XCPTL_TRANSPORT_LAYER_HEADER_SIZE,
                (uint32_t)(aligned_size / 1024));

    QueueClear((tQueueHandle)queue); // Clear the queue

    // Checks
    assert(atomic_is_lock_free(&(queue->h.head)));
    assert((queue->h.queue_size & (XCPTL_PACKET_ALIGNMENT - 1)) == 0);
    assert((queue->h.queue_size & (CACHE_LINE_SIZE - 1)) == 0);
    assert((queue->h.queue_size % MAX_ENTRY_SIZE) == 0);

    return (tQueueHandle)queue;
}

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
    // In reserved state, the message entry has a valid dlc and ctr is set to CTR_RESERVED
    uint64_t tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed);
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_acquire);
    for (;;) {

        // Check for overrun this head
        if (queue->h.queue_size - msg_len < head - tail) {
            break; // Overrun
        }

        // Try to get and increment this head by fixed max entry size (MAX_ENTRY_SIZE)
        // Compare exchange weak in acq_rel/acq mode serialize with other producers, false negatives will spin
        if (atomic_compare_exchange_weak_explicit(&queue->h.head, &head, head + MAX_ENTRY_SIZE, memory_order_acq_rel, memory_order_acquire)) {
            entry = (tXcpDtoMessage *)(queue->buffer + (head % queue->h.queue_size));
            atomic_store_explicit(&entry->ctr_dlc, (CTR_RESERVED << 16) | (uint32_t)(msg_len - XCPTL_TRANSPORT_LAYER_HEADER_SIZE), memory_order_release);
            break;
        }
    } // for (;;)

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

    if (entry == NULL) { // Overflow
        uint32_t lost = (uint32_t)atomic_fetch_add_explicit(&queue->h.packets_lost, 1, memory_order_acq_rel);
        if (lost == 0)
            DBG_PRINTF_WARNING("Queue overrun, msg_len=%u, h=%" PRIu64 ", t=%" PRIu64 ", level=%u, size=%u\n", msg_len, head, tail, (uint32_t)(head - tail), queue->h.queue_size);
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }

    tQueueBuffer ret = {
        // Return a pointer to the payload data of this message entry
        .buffer = entry->data,
        // Return the actual aligned size of the requested payload data size, which is the len stored in the transport layer header
        .size = msg_len - XCPTL_TRANSPORT_LAYER_HEADER_SIZE,
    };
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

    assert(queueBuffer != NULL);
    assert(queueBuffer->buffer != NULL);
    tXcpDtoMessage *entry = (tXcpDtoMessage *)(queueBuffer->buffer - XCPTL_TRANSPORT_LAYER_HEADER_SIZE);

    // Set commit state (in ctr) and keep the dlc in the transport layer header
    // Release - complete data is then visible to the consumer
    atomic_store_explicit(&entry->ctr_dlc, (CTR_COMMITTED << 16) | (uint32_t)(queueBuffer->size), memory_order_release);
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Consumer functions
//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Single consumer thread !!!!!!!!!!
// The consumer does not contend against the providers

// Get current transmit queue level in bytes (remaining after peek)
// Is thread safe (no undefined behaviour), but may have false negatives for queue not empty in other threads
// Returns 0 when the queue is empty
uint32_t QueueLevel(tQueueHandle queueHandle) {
    tQueue *queue = (tQueue *)queueHandle;
    if (queue == NULL)
        return 0;
    uint64_t head = atomic_load_explicit(&queue->h.head, memory_order_relaxed);
    uint64_t peek_tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed) + atomic_load_explicit(&queue->h.peek_offset, memory_order_relaxed);
    assert(head >= peek_tail);
    assert(head - peek_tail <= queue->h.queue_size);
    return (uint32_t)(head - peek_tail);
}

// Check if there is a packet in the transmit queue
// Return the packet length and a pointer to the message
// Returns the number of packets lost since the last call
// May be called multiple times, but the entries obtained must be released in the same order
// Is not thread safe, must be called from one single consumer thread only
tQueueBuffer QueuePeek(tQueueHandle queueHandle, bool flush, uint32_t *packets_lost) {
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

    uint64_t head, tail;
    uint32_t level;
    uint32_t offset;
    tXcpDtoMessage *entry;

    tail = atomic_load_explicit(&queue->h.tail, memory_order_relaxed) + atomic_load_explicit(&queue->h.peek_offset, memory_order_relaxed);
    head = atomic_load_explicit(&queue->h.head, memory_order_relaxed);

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

    // Get a pointer to the entry in the queue
    offset = (uint32_t)(tail % queue->h.queue_size);
    entry = (tXcpDtoMessage *)(queue->buffer + offset);

    // Check the entry commit state
    uint32_t ctr_dlc = atomic_load_explicit(&entry->ctr_dlc, memory_order_acquire);
    uint16_t dlc = ctr_dlc & 0xFFFF;          // Transport layer packet data length
    uint16_t ctr = (uint16_t)(ctr_dlc >> 16); // Transport layer counter
    if (ctr != CTR_COMMITTED) {

        // This should never happen
        // An entry is consistent, if it is neither in reserved or committed state
        if (ctr != CTR_RESERVED) {
            DBG_PRINTF_ERROR("QueuePeek inconsistent reserved - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (dlc=0x%04X, ctr=0x%04X)\n", head, tail, level, dlc, ctr);
            assert(false); // Fatal error, inconsistent state
        }

        // Nothing to read, the first entry is still in reserved state
        tQueueBuffer ret = {
            .buffer = NULL,
            .size = 0,
        };
        return ret;
    }

    // This should never happen
    // An committed entry must have a valid length and an XCP ODT in it
    if (!((ctr == CTR_COMMITTED) && (dlc > 0) && (dlc <= XCPTL_MAX_DTO_SIZE) && (entry->data[1] == 0xAA || entry->data[0] >= 0xFC))) {
        DBG_PRINTF_ERROR("QueuePeek: inconsistent commit - h=%" PRIu64 ", t=%" PRIu64 ", level=%u, entry: (dlc=0x%04X, ctr=0x%04X, res=0x%02X)\n", head, tail, level, dlc, ctr,
                         entry->data[1]);
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
    atomic_store_explicit(&entry->ctr_dlc, ctr_dlc, memory_order_relaxed);

    // Increment the peek offset
    atomic_fetch_add_explicit(&queue->h.peek_offset, MAX_ENTRY_SIZE, memory_order_relaxed);

    tQueueBuffer ret = {
        .buffer = (uint8_t *)entry,
        .size = dlc + XCPTL_TRANSPORT_LAYER_HEADER_SIZE, // Include the transport layer header size
    };
    return ret;
}

// Advance the transmit queue tail
// Entries obtained from QueuePeek must be released in the same order !!!
// Is not thread safe, must be called from one single consumer thread only
void QueueRelease(tQueueHandle queueHandle, tQueueBuffer *const queueBuffer) {
    tQueue *queue = (tQueue *)queueHandle;
    assert(queue != NULL);
    assert(queueBuffer->size > 0 && queueBuffer->size <= XCPTL_MAX_SEGMENT_SIZE);

    // Clear the entries state
    tXcpDtoMessage *entry = (tXcpDtoMessage *)queue->buffer;
    atomic_store_explicit(&entry->ctr_dlc, 0, memory_order_release);

    // Decrement peek offset
    uint64_t peek_offset = atomic_fetch_sub_explicit(&queue->h.peek_offset, MAX_ENTRY_SIZE, memory_order_relaxed);
    if (peek_offset == 0) {
        DBG_PRINT_ERROR("QueueRelease: peek_offset underflow\n");
        assert(false); // Fatal error, peek_offset underflow
    }

    // Increment tail
    atomic_fetch_add_explicit(&queue->h.tail, MAX_ENTRY_SIZE, memory_order_release);
}

#endif
#endif // OPTION_QUEUE_64_FIX_SIZE
