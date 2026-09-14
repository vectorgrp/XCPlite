/*----------------------------------------------------------------------------
| File:
|   queue32m.c
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

#include "platform.h"   // for platform abstraction
#include "xcplib_cfg.h" // for OPTION_xxx

// Using queue32m.c for 32 Bit RTOS platforms
#if defined(_FREE_RTOS) && defined(OPTION_QUEUE_32)

#include "queue.h"

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <stdbool.h>  // for bool
#include <stddef.h>   // for offsetof
#include <stdint.h>   // for uint32_t, uint64_t, uint8_t, int64_t

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
// The accumulated messages carry a tXcpMessage header of two uint16_t fields, which is accessed
// while walking a segment, so the message alignment must be a multiple of 2.
#if (XCPTL_PACKET_ALIGNMENT % 2) != 0
#error "XCPTL_PACKET_ALIGNMENT must be a multiple of 2 in this queue variant: the 16 bit message header fields require it"
#endif

typedef struct {
    uint32_t magic;       // Magic number to identify the segment buffer
    uint16_t uncommitted; // Number of uncommitted messages in this segment
    uint16_t size;        // Number of overall bytes in this segment
#if QUEUE_SEGMENT_HEADER_SIZE > 0
    // Space for the consumer to prepend a header to the whole segment without copying it,
    // used by the raw Ethernet transport for the Ethernet/IPv4/UDP header. See queue.h.
    uint8_t segment_header[QUEUE_SEGMENT_HEADER_SIZE];
#endif
    uint8_t msg_buffer[XCPTL_MAX_SEGMENT_SIZE]; // Segment/UDP MTU - concatenated transport layer messages tXcpMessage
} tXcpSegmentBuffer;

// The segment payload must stay aligned, the accumulated tXcpMessage headers are read as words
static_assert((offsetof(tXcpSegmentBuffer, msg_buffer) % XCPTL_PACKET_ALIGNMENT) == 0, "segment payload must stay aligned to XCPTL_PACKET_ALIGNMENT");

typedef struct Queue {

    uint32_t queue_buffer_size; // Size of queue memory allocated in bytes
    uint32_t queue_size;        // Size of queue in segments of type tXcpSegmentBuffer

    // Transmit segment queue
    tXcpSegmentBuffer *queue;   // Array of tXcpSegmentBuffer, each segment is a UDP payload (MAX_SEGMENT_SIZE)
    uint32_t queue_rp;          // rp = read index
    uint32_t queue_len;         // rp+len = write index (the next free entry), len=0 is empty, len=XCPTL_QUEUE_SIZE is full
    tXcpSegmentBuffer *msg_ptr; // current incomplete or not fully committed segment

    uint32_t packets_lost; // Number of packets lost since last call to queuePop

#ifdef OPTION_QUEUE32_MUTEX
    MUTEX Mutex_Queue;
#endif

} tQueue;

/*
STM32H7 memory placement — DTCM vs AXI SRAM vs non-cacheable
The STM32H7 has distinct memory regions with very different characteristics:

Region	            Access	            Cache	    DMA
DTCM (128KB)	    0-wait-state CPU	No	        No (MDMA only)
AXI SRAM (512KB)	~3 cycles	        D-Cache	    Yes (all DMAs)
Non-cacheable SRAM	varies	            No	        Yes

Place the tQueue header struct (hot: queue_rp, queue_len, msg_ptr) in DTCM via __attribute__((section(".dtcm"))) — zero-wait-state, no cache needed.
Place the queue->queue segment buffer array in a non-cacheable AXI SRAM region — avoids the need for SCB_CleanDCacheByAddr before DMA Ethernet TX and SCB_InvalidateDCacheByAddr
after RX

Without non-cacheable placement, the D-Cache creates correctness hazards: the CPU writes data into the queue buffer, the cache line is dirty, but the Ethernet DMA reads stale data
from AXI SRAM. You need explicit SCB_CleanDCacheByAddr before queueRelease hands the buffer to the network stack.
*/

// STM32
// Place the queue in DTCM for better performance on Cortex-M targets (zero-wait-state, no cache needed)
#ifndef OPTION_QUEUE_32_ATTRIBUTE
#if !defined(FREE_RTOS_POSIX_SIM) && !defined(ESP_PLATFORM)
#define OPTION_QUEUE_32_ATTRIBUTE __attribute__((section(".dtcm")))
#else
#define OPTION_QUEUE_32_ATTRIBUTE
#endif
#endif

#ifndef OPTION_QUEUE_32_BUFFER_ATTRIBUTE
#if !defined(FREE_RTOS_POSIX_SIM) && !defined(ESP_PLATFORM)
#define OPTION_QUEUE_32_BUFFER_ATTRIBUTE __attribute__((section(".noncacheable")))
#else
#define OPTION_QUEUE_32_BUFFER_ATTRIBUTE
#endif
#endif

static tQueue OPTION_QUEUE_32_ATTRIBUTE sXcpQueue;
static tXcpSegmentBuffer OPTION_QUEUE_32_BUFFER_ATTRIBUTE sXcpQueueBuf[OPTION_QUEUE_32_SIZE / sizeof(tXcpSegmentBuffer)];

/*

Place the sXcpQueue.queue segment buffer array in a non-cacheable AXI SRAM region —
avoids the need for SCB_CleanDCacheByAddr before DMA Ethernet TX and SCB_InvalidateDCacheByAddr after RX.

Without non-cacheable placement, the D-Cache creates correctness hazards:
the CPU writes data into the queue buffer, the cache line is dirty,
but the Ethernet DMA reads stale data from AXI SRAM. You need explicit SCB_CleanDCacheByAddr before queueRelease hands the buffer to the network stack

static tQueue          s_queue       __attribute__((section(".dtcm")));
static tXcpSegmentBuffer s_queue_buf[N] __attribute__((section(".noncacheable")));

*/

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Locking

/*
FreeRTOS mutex or critical section

mutexLock(&queue->Mutex_Queue)  -  taskENTER_CRITICAL();
mutexUnlock(&queue->Mutex_Queue)  -  taskEXIT_CRITICAL();

On STM32 taskENTER_CRITICAL() uses BASEPRI to mask interrupts up to configMAX_SYSCALL_INTERRUPT_PRIORITY — it's a single MSR instruction. No context switch possible, no scheduler
involvement, no priority inversion

taskENTER_CRITICAL() and taskEXIT_CRITICAL() must not be called from an interrupt service routine (ISR)
*/

#ifdef OPTION_QUEUE32_MUTEX
#define LOCK mutexLock(&sXcpQueue.Mutex_Queue)
#define UNLOCK mutexUnlock(&sXcpQueue.Mutex_Queue)
#elif defined(ESP_PLATFORM)
static portMUX_TYPE sXcpQueueMux = portMUX_INITIALIZER_UNLOCKED;
#define LOCK taskENTER_CRITICAL(&sXcpQueueMux)
#define UNLOCK taskEXIT_CRITICAL(&sXcpQueueMux)
#else
#define LOCK taskENTER_CRITICAL()
#define UNLOCK taskEXIT_CRITICAL()
#endif

//-------------------------------------------------------------------------------------------------------------------------------------------------------

// Allocate a new segment buffer (in sXcpQueue.msg_ptr)
// Not thread safe!
static void newSegmentBuffer(void) {

    tXcpSegmentBuffer *b;

    /* Check if there is space in the queue */
    if (sXcpQueue.queue_len >= sXcpQueue.queue_size) {
        /* Queue overflow */
        sXcpQueue.msg_ptr = NULL;
    } else {
        unsigned int i = sXcpQueue.queue_rp + sXcpQueue.queue_len;
        if (i >= sXcpQueue.queue_size)
            i -= sXcpQueue.queue_size;
        b = &sXcpQueue.queue[i];
        b->size = 0;
        b->uncommitted = 0;
        sXcpQueue.msg_ptr = b;
        sXcpQueue.queue_len++;
        assert(sXcpQueue.msg_ptr->magic == 0x12345678); // Check magic number
    }
}

static void clearQueue(void) {
    LOCK;
    sXcpQueue.queue_rp = 0;
    sXcpQueue.queue_len = 0;
    sXcpQueue.msg_ptr = NULL;
    UNLOCK;
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------

// Clear the queue
void queueClear(tQueueHandle _queue_handle) { clearQueue(); }

// Create and initialize the new queue
// For performance reasons, the queue is a singleton and the given size is ignored
tQueueHandle queueInit(size_t queue_buffer_size) {

    assert((OPTION_QUEUE_32_SIZE % sizeof(tXcpSegmentBuffer)) == 0);
    assert(queue_buffer_size == 0); // Make sure the user understands that the queue buffer size is fixed for this queue variant and the parameter is ignored

    queue_buffer_size = OPTION_QUEUE_32_SIZE; // The queue buffer size is fixed for this queue variant, the parameter is ignored

    tQueue *queue = &sXcpQueue;
    assert(queue != NULL);

    // Size of the queue buffer in entries of type tXcpSegmentBuffer
    size_t queue_entries = queue_buffer_size / sizeof(tXcpSegmentBuffer);

    // Size of the queue buffer in bytes
    sXcpQueue.queue_buffer_size = queue_buffer_size;
    sXcpQueue.queue_size = queue_entries;
    sXcpQueue.queue = sXcpQueueBuf;

    for (uint32_t i = 0; i < sXcpQueue.queue_size; i++) {
        sXcpQueue.queue[i].magic = 0x12345678; // Magic number to identify the segment buffer
        sXcpQueue.queue[i].uncommitted = 0;    // No uncommitted messages
        sXcpQueue.queue[i].size = 0;           // No data in this segment
    }

#ifdef OPTION_QUEUE32_MUTEX
    mutexInit(&sXcpQueue.Mutex_Queue, false, 1000);
#endif

    LOCK;
    sXcpQueue.queue_rp = 0;
    sXcpQueue.queue_len = 0;
    sXcpQueue.packets_lost = 0;
    sXcpQueue.msg_ptr = NULL;
    newSegmentBuffer();
    UNLOCK;

    assert(sXcpQueue.msg_ptr);
    return (tQueueHandle)queue;
}

// Deinitialize the queue
void queueDeinit(tQueueHandle _queue_handle) {
    clearQueue(); // Clear the queue
    sXcpQueue.queue = NULL;
    sXcpQueue.queue_buffer_size = 0;
    sXcpQueue.queue_size = 0;
#ifdef OPTION_QUEUE32_MUTEX
    mutexDestroy(&sXcpQueue.Mutex_Queue);
#endif
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Producer functions
// For multiple producers !!

// Get a buffer for a message with size
tQueueBuffer queueAcquire(tQueueHandle _queue_handle, uint16_t packet_size) {

    tXcpMessage *p = NULL;
    tXcpSegmentBuffer *b = NULL;
    uint16_t msg_size;

    if (!(packet_size > 0 && packet_size <= XCPTL_MAX_DTO_SIZE)) {
        assert(false); // Invalid packet size
        tQueueBuffer ret = {
            .buffer = NULL,
            .handle = NULL,
            .size = 0,
        };
        return ret;
    }

    // Round up to XCPTL_PACKET_ALIGNMENT, queue.h checks it to be a power of two.
    //
    // @@@@ TODO: The fill is included in the message dlc below, so LEN on the wire is larger than
    // the actual XCP packet and the XCP client sees trailing filler bytes. The padding only exists
    // so that the NEXT message in the segment starts aligned, so it is unnecessary for the last
    // (or only) message in a datagram - a single message datagram is padded for no reason, which
    // has surprised users. Trimming it would need the true unpadded length kept per message, it is
    // not stored anywhere today. Note command responses are NOT padded (XcpTlSendCrm sets dlc
    // directly), so the behaviour is asymmetric between DAQ and CRM.
    // To be checked against ASAM XCP Part 3 (XCP on Ethernet): is a LEN larger than the packet
    // content legal, and is any alignment required at all? See docs/TECHNICAL.md, Known Issues.
    packet_size = (uint16_t)((packet_size + (XCPTL_PACKET_ALIGNMENT - 1)) & ~(XCPTL_PACKET_ALIGNMENT - 1));

    msg_size = (uint16_t)(packet_size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE);

    LOCK;

    // Get another message buffer from the queue when the active buffer is full
    b = sXcpQueue.msg_ptr;
    if (b == NULL || (uint16_t)(b->size + msg_size) > XCPTL_MAX_SEGMENT_SIZE) {
        newSegmentBuffer();
        b = sXcpQueue.msg_ptr;
    }
    if (b != NULL) {
        p = (tXcpMessage *)&b->msg_buffer[b->size];
        b->size = (uint16_t)(b->size + msg_size);
        b->uncommitted++;

    } else {
        sXcpQueue.packets_lost++; // No segment buffer available, queue overflow
    }

    UNLOCK;

    if (p == NULL) {
        tQueueBuffer ret = {.buffer = NULL, .handle = NULL, .size = 0};
        return ret;
    } else {

        // Build XCP message header (ctr+dlc) and store in DTO buffer
        p->ctr = 0xEEEE; // Reserved value, indicates that this message is not yet committed (for assertion only)
        p->dlc = (uint16_t)packet_size;

        tQueueBuffer ret = {
            .buffer = p->packet, // Pointer to the message data (after the XCP header)
            .handle = b,         // Pointer to the segment buffer (tXcpSegmentBuffer *)
            .size = packet_size, // Size of the message buffer in bytes
        };
        return ret;
    };
}

// Commit a buffer (returned from XcpTlGetTransmitBuffer)
void queuePush(tQueueHandle _queue_handle, const tQueueBuffer *queue_buffer, bool flush) {

    LOCK;

    ((tXcpSegmentBuffer *)queue_buffer->handle)->uncommitted--;

    tXcpMessage *p = (tXcpMessage *)(queue_buffer->buffer - XCPTL_TRANSPORT_LAYER_HEADER_SIZE);
    assert(p->dlc > 0 && p->dlc <= XCPTL_MAX_DTO_SIZE);
    assert(p->ctr == 0xEEEE); // Check if the message is in reserved state
    p->ctr = 0xCCCC;          // Mark the message as committed, CTR value is not important yet, it will be set by the consumer (for assertion only)

    // Flush (high priority data committed)
    if (flush && sXcpQueue.msg_ptr != NULL && sXcpQueue.msg_ptr->size > 0) {
        newSegmentBuffer();
    }

    UNLOCK;
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------
// Consumer functions
// Single consumer thread !!!!!!!!!!

// Get transmit queue level in segments
// This function is thread safe, any thread can ask for the queue level
// Not used by the queue implementation itself
uint32_t queueLevel(tQueueHandle _queue_handle, uint32_t *queue_max_level) {
    uint32_t level = 0;

    LOCK;
    if (queue_max_level != NULL)
        *queue_max_level = sXcpQueue.queue_size;
    if (sXcpQueue.queue_len > 1 || (sXcpQueue.queue_len == 1 && sXcpQueue.msg_ptr != NULL && sXcpQueue.msg_ptr->size > 0)) {
        level = sXcpQueue.queue_len;
    }
    UNLOCK;

    return level;
}

// Check if there is a message segment in the transmit queue
// Return the message length and a pointer to the message
// Returns the number of packets lost since the last call to queuePop
// May not be called twice, each buffer must be released with queueRelease
// Is not thread safe, must be called from the consumer thread only
tQueueBuffer queuePop(tQueueHandle _queue_handle, bool accumulate, bool flush, uint32_t *packets_lost) {

    assert(accumulate == true);

    tXcpSegmentBuffer *b = NULL;

    LOCK;

    // Return the number of packets lost since the last call to queuePop
    if (packets_lost != NULL) {
        *packets_lost = sXcpQueue.packets_lost;
        sXcpQueue.packets_lost = 0; // Reset lost packets count
    }

    // Check if there is a message segment ready in the transmit queue
    if (sXcpQueue.queue_len >= 1) {

        b = &sXcpQueue.queue[sXcpQueue.queue_rp];

        // Flush tail segment buffer if it is not empty
        if (sXcpQueue.queue_len == 1 && b->size > 0 && flush) {
            newSegmentBuffer();
        }

        // Return tail segment buffer if it is not empty, fully committed and there are more segments in the queue
        if (!(sXcpQueue.queue_len > 1 && b->uncommitted == 0 && b->size > 0)) {
            b = NULL;
        }
    }

    UNLOCK;

    if (b == NULL) {

        tQueueBuffer ret = {
            .buffer = NULL,
            .handle = NULL,
            .size = 0,
        };
        return ret;

    }

    else {

        // Update the transport layer message counters
        uint8_t *p = b->msg_buffer;
        uint8_t *pl = &b->msg_buffer[b->size] - XCPTL_TRANSPORT_LAYER_HEADER_SIZE; // Pointer to the last possible byte in the segment buffer
        while (p < pl) {
            tXcpMessage *m = (tXcpMessage *)p;                  // Pointer to the current message
            assert(m->dlc > 0 && m->dlc <= XCPTL_MAX_DTO_SIZE); // Check if the message length is valid
            assert(m->ctr == 0xCCCC);                           // Check if the message is in committed state
            m->ctr = XcpTlGetCtr();                             // Set the transport layer message counter
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
void queueRelease(tQueueHandle _queue_handle, const tQueueBuffer *queue_buffer) {

    // Free this segment buffer when successfully sent
    LOCK;
    if (++sXcpQueue.queue_rp >= sXcpQueue.queue_size)
        sXcpQueue.queue_rp = 0;
    sXcpQueue.queue_len--;
    UNLOCK;
}

#endif
