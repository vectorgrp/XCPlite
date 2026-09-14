#pragma once
#define __XCP_QUEUE_h__

/*----------------------------------------------------------------------------
| File:
|   queue.h
|
| Description:
|   Generic queue API
|   There are 4 different implementations of the queue API, which are selected based on platform and configuration:
|       queue64v.c  - Generic, lockless, variable entry size
|       queue64f.c  - Generic, lockless, fixed entry size
|       queue32.c   - XCP specific, mutex based, variable entry size with message accumulation (fallback for 32-bit platforms and Windows)
|       queue32m.c  - XCP specific, critical section based, variable entry size with message accumulation (used for FreeRTOS targets)
|
|   Note:
|     The 2 generic queue implementations are not specific for the XCP on Ethernet transport layer
|     But XCP uses a feature to include the 4 byte transport layer message headers (ctr+len) in the queue entries.
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Queue parameter configuration:
// Configuration for XCP on Ethernet transport layer with a 4 byte transport layer header (ctr+len)
// Using XCP parameters from xcptl_cfg.h: XCPTL_MAX_DTO_SIZE, XCPTL_MAX_SEGMENT_SIZE, QUEUE_PAYLOAD_SIZE_ALIGNMENT:
// Queue entries may include space for a consumer header with user defined size
// This allows the consumer to add a header to the queue entry without copying and merging data.
// For XCP use cases, this size is configured to be the XCP transport layer header size (XCPTL_TRANSPORT_LAYER_HEADER_SIZE, which is 4 bytes for ctr+len).
// Other use cases can use this space for other purposes, e.g. to store a timestamp or a protocol header, or it can be set to 0 if not needed.
#include "xcptl_cfg.h" // for XCPTL_TRANSPORT_LAYER_HEADER_SIZE, XCPTL_MAX_DTO_SIZE, XCPTL_MAX_SEGMENT_SIZE, QUEUE_PAYLOAD_SIZE_ALIGNMENT
#define QUEUE_ENTRY_USER_HEADER_SIZE (XCPTL_TRANSPORT_LAYER_HEADER_SIZE) // (for XCP transport layer header with XCPTL_TRANSPORT_LAYER_HEADER_SIZE)

// Space reserved in front of a complete SEGMENT, for a consumer which needs to prepend a header to the whole segment without copying it.
// This is for the 32 bit queue implementation (queue32.c, queue32m.c) which accumulates multiple messages into a segment.
// The reserved space is not relevant in the 64 bit queue implementations (queue64v.c, queue64f.c) which do not accumulate messages into segments, this is done by the vectored IO
// transmit path.. Note the difference to QUEUE_ENTRY_USER_HEADER_SIZE above, they are easy to confuse:
//   QUEUE_ENTRY_USER_HEADER_SIZE  is per MESSAGE  - every message in a segment carries one
//   QUEUE_SEGMENT_HEADER_SIZE     is per SEGMENT  - reserved once, in front of the whole segment
// For XCP on raw Ethernet a segment is one Ethernet frame, so its link header is needed exactly
// once, in front. Reserving it per message instead would put the space inside the datagram payload
// and multiply it by the number of accumulated messages.
// Only meaningful for queue variants which accumulate messages into segments (queue32.c, queue32m.c).
// 0 disables the reservation, and the queue entry layout is then unchanged.
#define QUEUE_SEGMENT_HEADER_SIZE (XCPTL_TX_HEADROOM)

#define QUEUE_ENTRY_USER_PAYLOAD_SIZE (XCPTL_MAX_DTO_SIZE) // In the variable size queue, used for plausibility checking the requested payload size in queueAcquire
#define QUEUE_ENTRY_USER_SIZE (XCPTL_MAX_DTO_SIZE + XCPTL_TRANSPORT_LAYER_HEADER_SIZE)
#define QUEUE_MAX_ENTRY_SIZE (XCPTL_MAX_DTO_SIZE + XCPTL_TRANSPORT_LAYER_HEADER_SIZE)
#define QUEUE_PAYLOAD_SIZE_ALIGNMENT (XCPTL_PACKET_ALIGNMENT)

// For generic uses case without optional header space reserved, use the following configuration:
/*
#define QUEUE_ENTRY_USER_HEADER_SIZE (0)
#define QUEUE_ENTRY_USER_PAYLOAD_SIZE (512)
#define QUEUE_ENTRY_USER_SIZE (QUEUE_ENTRY_USER_PAYLOAD_SIZE)
#define QUEUE_MAX_ENTRY_SIZE (QUEUE_ENTRY_USER_PAYLOAD_SIZE)
#define QUEUE_PAYLOAD_SIZE_ALIGNMENT (4)
*/

// Check preconditions
// The queue implementations round sizes up with a bit mask, so the alignment must be a power of two.
// Each variant additionally requires a minimum, checked in the variant itself, because the reason
// differs: the 64 bit queues need a multiple of 4 for their atomic entry header, the segment
// accumulating queues need a multiple of 2 for the 16 bit message header fields.
#if QUEUE_PAYLOAD_SIZE_ALIGNMENT < 1 || (QUEUE_PAYLOAD_SIZE_ALIGNMENT & (QUEUE_PAYLOAD_SIZE_ALIGNMENT - 1)) != 0
#error "QUEUE_PAYLOAD_SIZE_ALIGNMENT must be a power of two (see XCPTL_PACKET_ALIGNMENT)"
#endif
#if (QUEUE_MAX_ENTRY_SIZE % QUEUE_PAYLOAD_SIZE_ALIGNMENT) != 0
#error "QUEUE_MAX_ENTRY_SIZE should be aligned to QUEUE_PAYLOAD_SIZE_ALIGNMENT"
#endif
#if (QUEUE_SEGMENT_HEADER_SIZE % QUEUE_PAYLOAD_SIZE_ALIGNMENT) != 0
#error "QUEUE_SEGMENT_HEADER_SIZE must be aligned to QUEUE_PAYLOAD_SIZE_ALIGNMENT, otherwise it would misalign the segment payload"
#endif
#if (QUEUE_MAX_ENTRY_SIZE > 0xFFFF)
#error "QUEUE_MAX_ENTRY_SIZE must not exceed 0xFFFF"
#endif

// Note:
// On the producer side, a tQueueBuffer from queueAcquire don't include the user header space
// On the consumer side, a tQueueBuffer from queuePeek includes the space for the user header

// Buffer acquired from the queue with `queueAcquire` (producer) or from queuePeek` (consumer)
typedef struct QueueBuffer {
#if defined(PLATFORM_32BIT) || defined(_WIN) || defined(OPTION_ATOMIC_EMULATION) || (!defined(OPTION_QUEUE_64_FIX_SIZE) && !defined(OPTION_QUEUE_64_VAR_SIZE))
    void *handle;
#endif
    uint8_t *buffer; // Pointer to the buffer to the message payload
    uint16_t size;   // Size of the payload buffer in bytes
} tQueueBuffer;

/// Opaque queue handle for allowing multiple queues
typedef struct Queue *tQueueHandle;
#define UNDEFINED_QUEUE_HANDLE NULL

/// Create new heap allocated queue.
/// Free using `queueDeinit`.
/// @param buffer_size          Queue buffer size in bytes. Does not include the queue header size.
tQueueHandle queueInit(size_t queue_buffer_size);

/// Creates a queue inside the user provided buffer.
/// @precondition queue_buffer_size must at least fit the queue header and minimum payload size.
/// This can be used to place the queue inside shared memory
/// @param queue_buffer         Buffer to place the queue in including the queue header.
/// @param queue_buffer_size    Buffer size including the queue header.
/// @param clear_queue          Clear the queue memory or keep the passed buffer untouched.
/// @param out out_buffer_size  Optional out parameter can be used to get the remaining buffer size.
/// @return Queue handle.
tQueueHandle queueInitFromMemory(void *queue_memory, size_t queue_memory_size, bool clear_queue, uint64_t *out_buffer_size);

/// Deinitialize queue.
/// Does **not** free user allocated memory provided to `queueInitFromMemory`.
/// Must not be called for queue handles created from already initialized queues
/// @param queue_handle         Queue handle.
void queueDeinit(tQueueHandle queue_handle);

/// Acquire a buffer and reserve space in the queue.
/// The buffer can be written by the user, but can not be read by the consumer until it is committed with queuePush.
/// @param queue_handle         Queue handle.
/// @param payload_size         Requested buffer size.
/// @return QueueBuffer
/// NOTE:
/// The QueueBuffer::size returned may exceed the requested size due to padding
/// If QueueBuffer::size is 0, there is no space left in the queue - overflow
/// QueueBuffer::size may be larger than the requested payload_size due to padding, but it will always be at least as large as the requested payload_size
/// The full returned buffer size can be safely read and written by the user even if it exceeds the requested size.
tQueueBuffer queueAcquire(tQueueHandle queue_handle, uint16_t payload_size);

/// Commit an acquired buffer to the queue to indicate that the data written is complete and valid for the consumer.
/// @param queue_handle         Queue handle.
/// @param queue_buffer         Queue buffer.
/// @param priority             Optional: Indicate producer priority, the queue may take measures to optimize. Implementation specific.
/// @return Queue buffer.
void queuePush(tQueueHandle queue_handle, const tQueueBuffer *queue_buffer, bool priority);

/// Peek at a queue entry without removing it from the queue.
/// Only supported by the 64 bit queue implementations (queue64v.c, queue64f.c).
/// Single consumer thread only, not thread safe.
/// @param queue_handle         Queue handle.
/// @param index                Peak ahead index. Can not peak ahead uncommitted entries!!!
/// @param packets_lost         Optional out parameter to get the number of packets lost since the last call. Packet lost happens on the producer side when the queue is full
/// @param flush_requested      Optional out parameter to indicate if a flush was requested.
/// @return Queue buffer, tQueueBuffer::size=0 if no buffer exists (with that index).
/// NOTE: The returned buffer must be released using `queueRelease` and in the same order as they were obtained (sequential index order).
/// NOTE: The payload already includes header space for the XCP transport layer header (ctr+len) in the buffer, but the transport layer counter is not set yet!
/// NOTE: The function may be called multiple times with the same index, but the entries obtained must be released in sequential index order.
#if defined(OPTION_QUEUE_64_FIX_SIZE) || defined(OPTION_QUEUE_64_VAR_SIZE)
tQueueBuffer queuePeek(tQueueHandle queue_handle, uint32_t index, uint32_t *packets_lost, bool *flush_requested);
#endif

/// Get the next entry or multiple accumulated (segment) entries from the queue.
/// Accumulate multiple messages into a segment is only supported by the 32 bit queue implementations (queue32.c, queue32m.c).
/// Single consumer thread only, not thread safe.
/// @param queue_handle         Queue handle.
/// @param accumulate           Accumulate multiple message entries into one segment (sequential memory), up to the maximum segment size (queue32 only).
/// @param flush                Disable any optimizations to guarantee it returns any commited data currently available. No lazy updates, no false negatives.
/// @param packets_lost         Optional out parameter to get the number of packets lost since the last call. Packet lost happens on the producer side when the queue is full
/// @return Queue buffer    tQueueBuffer::size is 0 if no buffer can be popped from the queue.
/// NOTE: The returned buffer must be released using `queueRelease` before any other call to queuePop.
/// NOTE: As there may be multiple accumulated entries, queuePop initializes the XCP transport layer counter in the message by calling XcpTlGetCtr()
#if defined(OPTION_QUEUE_32)
tQueueBuffer queuePop(tQueueHandle queue_handle, bool accumulate, bool priority, uint32_t *packets_lost);
#endif

/// Release a buffer from `queuePeek` or `queuePop`.
/// Single consumer thread only, not thread safe.
/// This is required to notify the queue that it can reuse memory and it will end the lifetime of the buffer obtained from `queuePeek` or `queuePop`.
/// Queue entries must be released in the same order as they were obtained from `queuePop` (FIFO order) or in sequential index order from `queuePeek`.
/// @param queue_handle         Queue handle.
/// @param queue_buffer         Queue buffer to release.
void queueRelease(tQueueHandle queue_handle, const tQueueBuffer *queue_buffer);

/// Get current amount of bytes or entries (implementation specific) in the queue, 0 if queue is empty.
/// @param queue_handle         Queue handle.
/// @param queue_max_level      Optional out parameter to get the maximum level possible.
/// @return Current amount of bytes or entries in the queue.
uint32_t queueLevel(tQueueHandle queue_handle, uint32_t *queue_max_level);

/// Clear queue content.
/// @param queue_handle         Queue handle.
void queueClear(tQueueHandle queue_handle);
