/*----------------------------------------------------------------------------
| File:
|   queue32.c
|
| Description:
|   XCP transport layer queue
|   Multi producer single consumer queue (producer side thread safe, not consumer side)
|   XCP transport layer specific:
|   Queue entries include XCP message header of WORD CTR and WORD LEN type, CTR incremented on pop, overflow indication via CTR
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "platform.h"   // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex, spinlock
#include "xcplib_cfg.h" // for OPTION_xxx

// Use queue32.c for 32 Bit platforms, on Windows or with atomic emulation
// Explictly force with OPTION_QUEUE_32
#if defined(OPTION_QUEUE_32) || defined(PLATFORM_32BIT) || defined(_WIN) || defined(OPTION_ATOMIC_EMULATION)

#include "queue.h"

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uint32_t, uint64_t, uint8_t, int64_t
#include <stdio.h>    // for NULL
#include <stdlib.h>   // for free, malloc
#include <string.h>   // for memcpy, strcmp

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT, ...
#include "xcptl.h"     // for XcpTlGetCtr
#include "xcptl_cfg.h" // for XCPTL_TRANSPORT_LAYER_HEADER_SIZE, XCPTL_MAX_DTO_SIZE, XCPTL_MAX_SEGMENT_SIZE

/*

Transport Layer segment, message, packet:

    segment (UDP payload, MAX_SEGMENT_SIZE = UDP MTU) = message 1 + message 2 ... + message n
    message = WORD len + WORD ctr + (protocol layer packet) + fill

*/

// Check preconditions
#if QUEUE_ENTRY_USER_HEADER_SIZE != XCPTL_TRANSPORT_LAYER_HEADER_SIZE
#error "QUEUE_ENTRY_USER_HEADER_SIZE must be equal to XCPTL_TRANSPORT_LAYER_HEADER_SIZE for this queue variant"
#endif

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Message types

// Assume a maximum cache line size of 128 bytes
#define CACHE_LINE_SIZE 64u // Cache line size, used to align the queue entries and the queue header

typedef struct {
    uint16_t dlc;     // length
    uint16_t ctr;     // message counter
    uint8_t packet[]; // packet
} tXcpMessage;

static_assert(sizeof(tXcpMessage) == XCPTL_TRANSPORT_LAYER_HEADER_SIZE, "tXcpMessage size must be equal to XCPTL_TRANSPORT_LAYER_HEADER_SIZE");

typedef struct {
    uint32_t magic;                             // Magic number to identify the segment buffer
    uint16_t uncommitted;                       // Number of uncommitted messages in this segment
    uint16_t size;                              // Number of overall bytes in this segment
    uint8_t msg_buffer[XCPTL_MAX_SEGMENT_SIZE]; // Segment/UDP MTU - concatenated transport layer messages tXcpMessage
} tXcpSegmentBuffer;

typedef struct Queue {

    uint32_t queue_buffer_size; // Size of queue memory allocated in bytes
    uint32_t queue_size;        // Size of queue in segments of type tXcpSegmentBuffer

    // Transmit segment queue
    tXcpSegmentBuffer *queue;   // Array of tXcpSegmentBuffer, each segment is a UDP payload (MAX_SEGMENT_SIZE)
    uint32_t queue_rp;          // rp = read index
    uint32_t queue_len;         // rp+len = write index (the next free entry), len=0 ist empty, len=XCPTL_QUEUE_SIZE is full
    tXcpSegmentBuffer *msg_ptr; // current incomplete or not fully commited segment

    uint32_t packets_lost; // Number of packets lost since last call to queuePop

    MUTEX Mutex_Queue;

} tQueue;

//-------------------------------------------------------------------------------------------------------------------------------------------------------

// Allocate a new segment buffer (in queue->msg_ptr)
// Not thread save!
static void newSegmentBuffer(tQueue *queue) {

    tXcpSegmentBuffer *b;

    /* Check if there is space in the queue */
    if (queue->queue_len >= queue->queue_size) {
        /* Queue overflow */
        queue->msg_ptr = NULL;
    } else {
        unsigned int i = queue->queue_rp + queue->queue_len;
        if (i >= queue->queue_size)
            i -= queue->queue_size;
        b = &queue->queue[i];
        b->size = 0;
        b->uncommitted = 0;
        queue->msg_ptr = b;
        queue->queue_len++;
        assert(queue->msg_ptr->magic == 0x12345678); // Check magic number
        DBG_PRINTF6("getSegmentBuffer: queue_rp=%" PRIu32 ", queue_len=%" PRIu32 ", msg_ptr=%p\n", queue->queue_rp, queue->queue_len, (void *)queue->msg_ptr);
    }
}

static void clearQueue(tQueue *queue) {
    assert(queue != NULL);
    mutexLock(&queue->Mutex_Queue);
    queue->queue_rp = 0;
    queue->queue_len = 0;
    queue->msg_ptr = NULL;
    mutexUnlock(&queue->Mutex_Queue);
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------

// Clear the queue
void queueClear(tQueueHandle queue_handle) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);
    clearQueue(queue);
}

// Create and initialize a new queue with a given size in bytes
// Will be rounded up to match alignment requirements
tQueueHandle queueInit(size_t queue_buffer_size) {

    tQueue *queue = (tQueue *)malloc(sizeof(tQueue));
    assert(queue != NULL);

    // Target size of the queue buffer in entries of type tXcpSegmentBuffer
    size_t queue_entries = queue_buffer_size / sizeof(tXcpSegmentBuffer) + 1;

    // Size of the queue buffer in bytes (rounded up to cache line size)
    queue->queue_buffer_size = ((queue_entries * sizeof(tXcpSegmentBuffer)) + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1); // Align to cache line size
    // queue->queue = (tXcpSegmentBuffer *)_aligned_alloc(CACHE_LINE_SIZE, queue->queue_buffer_size);
    queue->queue = (tXcpSegmentBuffer *)malloc(queue->queue_buffer_size);
    queue->queue_size = queue->queue_buffer_size / sizeof(tXcpSegmentBuffer); // Number of segments in the queue
    assert(queue->queue != NULL);
    for (uint32_t i = 0; i < queue->queue_size; i++) {
        queue->queue[i].magic = 0x12345678; // Magic number to identify the segment buffer
        queue->queue[i].uncommitted = 0;    // No uncommitted messages
        queue->queue[i].size = 0;           // No data in this segment
    }

    DBG_PRINT3("Init transport layer lockless queue (queue32)\n");
    DBG_PRINTF3("  buffer_size=%" PRIu32 ", queue_size=%" PRIu32 " (%" PRIu32 " Bytes)\n", queue->queue_buffer_size, queue->queue_size, queue->queue_buffer_size);

    mutexInit(&queue->Mutex_Queue, false, 1000);

    mutexLock(&queue->Mutex_Queue);
    queue->queue_rp = 0;
    queue->queue_len = 0;
    queue->msg_ptr = NULL;
    newSegmentBuffer(queue);
    mutexUnlock(&queue->Mutex_Queue);

    assert(queue->msg_ptr);
    return (tQueueHandle)queue;
}

// Deinitialize and free the queue
void queueDeinit(tQueueHandle queue_handle) {

    DBG_PRINTF4("queueDeinit: queue_handle=%p\n", (void *)queue_handle);

    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);

    clearQueue(queue); // Clear the queue

    free(queue->queue);
    queue->queue = NULL;
    queue->queue_buffer_size = 0;
    queue->queue_size = 0;
    mutexDestroy(&queue->Mutex_Queue);
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Producer functions
// For multiple producers !!

// Get a buffer for a message with size
tQueueBuffer queueAcquire(tQueueHandle queue_handle, uint16_t packet_size) {

    tQueue *queue = (tQueue *)queue_handle;
    tXcpMessage *p = NULL;
    tXcpSegmentBuffer *b = NULL;
    uint16_t msg_size;

    DBG_PRINTF6("queueAcquire: queue_handle=%p, packet_size=%u\n", (void *)queue_handle, packet_size);

    if (!(packet_size > 0 && packet_size <= XCPTL_MAX_DTO_SIZE)) {
        DBG_PRINTF_ERROR("Invalid packet_len %u, must be between 1 and %u\n", packet_size, XCPTL_MAX_DTO_SIZE);

        tQueueBuffer ret = {
            .buffer = NULL,
            .handle = NULL,
            .size = 0,
        };
        return ret;
    }

#if XCPTL_PACKET_ALIGNMENT == 4
    packet_size = (uint16_t)((packet_size + 3) & 0xFFFC); // Add fill %4
#else
    assert(false);
#endif

    msg_size = (uint16_t)(packet_size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE);

    mutexLock(&queue->Mutex_Queue);

    // Get another message buffer from queue, when active buffer ist full
    b = queue->msg_ptr;
    if (b == NULL || (uint16_t)(b->size + msg_size) > XCPTL_MAX_SEGMENT_SIZE) {
        newSegmentBuffer(queue);
        b = queue->msg_ptr;
    }

    if (b != NULL) {

        // Build XCP message header (ctr+dlc) and store in DTO buffer
        p = (tXcpMessage *)&b->msg_buffer[b->size];
        p->ctr = 0xEEEE; // Reserved value, indicates that this message is not yet commited
        p->dlc = (uint16_t)packet_size;
        b->size = (uint16_t)(b->size + msg_size);
        b->uncommitted++;
        DBG_PRINTF6("queueAcquire: size=%u, uncommitted=%u\n", b->size, b->uncommitted);
    } else {
        // No segment buffer available, queue overflow
        queue->packets_lost++;
        DBG_PRINTF_ERROR("queueAcquire: queue overflow, packet_size=%u, msg_size=%u, queue_len=%u\n", packet_size, msg_size, queue->queue_len);
    }

    mutexUnlock(&queue->Mutex_Queue);

    if (p == NULL) {

        tQueueBuffer ret = {.buffer = NULL, .handle = NULL, .size = 0};
        return ret;
    } else {
        tQueueBuffer ret = {
            .buffer = p->packet, // Pointer to the message data (after the XCP header)
            .handle = b,         // Pointer to the segment buffer (tXcpSegmentBuffer *)
            .size = packet_size, // Size of the message buffer in bytes
        };
        return ret;
    };
}

// Commit a buffer (returned from XcpTlGetTransmitBuffer)
void queuePush(tQueueHandle queue_handle, const tQueueBuffer *queue_buffer, bool flush) {

    tQueue *queue = (tQueue *)queue_handle;

    DBG_PRINTF6("queuePush: size=%" PRIu16 ", uncommitted=%" PRIu16 "\n", queue_buffer->size, ((tXcpSegmentBuffer *)queue_buffer->handle)->uncommitted);

    mutexLock(&queue->Mutex_Queue);

    ((tXcpSegmentBuffer *)queue_buffer->handle)->uncommitted--;

    tXcpMessage *p = (tXcpMessage *)(queue_buffer->buffer - XCPTL_TRANSPORT_LAYER_HEADER_SIZE);
    assert(p->dlc > 0 && p->dlc <= XCPTL_MAX_DTO_SIZE);
    assert(p->ctr == 0xEEEE); // Check if the message is in reserved state
    p->ctr = 0xCCCC;          // Mark the message as commited, CTR value is not important yet, it will be set by the consumer

    // Flush (high priority data commited)
    if (flush && queue->msg_ptr != NULL && queue->msg_ptr->size > 0) {
        newSegmentBuffer(queue);
    }

    mutexUnlock(&queue->Mutex_Queue);
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Consumer functions
// Single consumer thread !!!!!!!!!!

// Get transmit queue level in segments
// This function is thread safe, any thread can ask for the queue level
// Not used by the queue implementation itself
uint32_t queueLevel(tQueueHandle queue_handle, uint32_t *queue_max_level) {
    tQueue *queue = (tQueue *)queue_handle;
    if (queue == NULL) {
        if (queue_max_level != NULL)
            *queue_max_level = 0;
        return 0;
    }
    if (queue_max_level != NULL)
        *queue_max_level = queue->queue_size;
    if (queue->queue_len > 1 || (queue->queue_len == 1 && queue->msg_ptr != NULL && queue->msg_ptr->size > 0)) {
        return queue->queue_len;
    }
    return 0;
}

// Check if there is a message segment in the transmit queue
// Return the message length and a pointer to the message
// Returns the number of packets lost since the last call to queuePop
// May not be called twice, each buffer must be released with queueRelease
// Is not thread safe, must be called from the consumer thread only
tQueueBuffer queuePop(tQueueHandle queue_handle, bool accumulate, bool flush, uint32_t *packets_lost) {
    tQueue *queue = (tQueue *)queue_handle;
    assert(queue != NULL);
    assert(accumulate == true);

    tXcpSegmentBuffer *b = NULL;

    // Return the number of packets lost since the last call to queuePop
    if (packets_lost != NULL) {
        *packets_lost = queue->packets_lost;
        if (*packets_lost > 0)
            DBG_PRINTF6("queuePop: packets_lost=%" PRIu32 "\n", *packets_lost);
        queue->packets_lost = 0; // Reset lost packets count
    }

    // Check if there is a message segment ready in the transmit queue
    mutexLock(&queue->Mutex_Queue);

    if (queue->queue_len >= 1) {

        b = &queue->queue[queue->queue_rp];

        // Flush tail segment buffer if it is not empty
        if (queue->queue_len == 1 && b->size > 0 && flush) {
            DBG_PRINT6("queuePop: flush\n");
            newSegmentBuffer(queue);
        }

        // Return tail segment buffer if it is not empty, fully committed and there are more segments in the queue
        if (!(queue->queue_len > 1 && b->uncommitted == 0 && b->size > 0)) {
            b = NULL;
        }
    }

    mutexUnlock(&queue->Mutex_Queue);

    if (b == NULL) {

        tQueueBuffer ret = {
            .buffer = NULL,
            .handle = NULL,
            .size = 0,
        };
        return ret;

    }

    else {

        DBG_PRINTF6("queuePop: flush=%d, packets_lost=%" PRIu32 ", size=%" PRIu32 "\n", flush, *packets_lost, b->size);

        // Update the transport layer message counters
        uint8_t *p = b->msg_buffer;
        uint8_t *pl = &b->msg_buffer[b->size] - XCPTL_TRANSPORT_LAYER_HEADER_SIZE; // Pointer to the last possible byte in the segment buffer
        while (p < pl) {
            tXcpMessage *m = (tXcpMessage *)p;                  // Pointer to the current message
            assert(m->dlc > 0 && m->dlc <= XCPTL_MAX_DTO_SIZE); // Check if the message length is valid
            assert(m->ctr == 0xCCCC);                           // Check if the message is in commited state
            m->ctr = XcpTlGetCtr();                             // Set the transport layer message counter
            DBG_PRINTF6("queuePop: p=%p, dlc=%" PRIu16 ", ctr=0x%04X\n", (void *)p, m->dlc, m->ctr);
            p += m->dlc + XCPTL_TRANSPORT_LAYER_HEADER_SIZE;
        };

        tQueueBuffer ret = {
            .buffer = b->msg_buffer,
            .handle = NULL,
            .size = b->size,
        };
        return ret;
    }
}

// Advance the transmit queue tail by the message length obtained from the last queuePop call
void queueRelease(tQueueHandle queue_handle, const tQueueBuffer *queue_buffer) {
    tQueue *queue = (tQueue *)queue_handle;

    DBG_PRINTF6("queueRelease: size=%" PRIu16 "\n", queue_buffer->size);

    // Free this segment buffer when successfully sent
    mutexLock(&queue->Mutex_Queue);
    if (++queue->queue_rp >= queue->queue_size)
        queue->queue_rp = 0;
    queue->queue_len--;
    mutexUnlock(&queue->Mutex_Queue);
}

#endif
