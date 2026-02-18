#pragma once
#define __XCP_QUEUE_h__

/*----------------------------------------------------------------------------
| File:
|   xcpQueue.h
|
| Description:
|   XCPlite internal header file for the transmit queue
|   There are 3 different implementations of the queue, which are selected based on the platform and configuration:
|       xcpQueue64.c   - Lockless, variable entry size with message accumulation
|       xcpQueue64f.x  - Lockless, fixed entry size
|       xcpQueue32.c   - Locking, variable entry size with message accumulation (fallback for 32-bit platforms and Windows)
|
|   Note:
|     This queue implementation is not specific to the XCP on Ethernet transport layer, but it includes the 4 byte transport layer message headers (ctr+len) in the queue entries.
|     The amount of reserved header space for the transport layer message header in the queue entries is defined by XCPTL_TRANSPORT_LAYER_HEADER_SIZE in xcptl_cfg.h, currently set
to 4 bytes for ctr+len.
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdint.h>

/// Opaque queue handle for allowing multiple queues
typedef struct tQueue *tQueueHandle;
#define UNDEFINED_QUEUE_HANDLE NULL

// Buffer acquired from the queue with `QueueAcquire` (producer) or from QueuePeek` (consumer)
typedef struct {
#if defined(PLATFORM_32BIT) || defined(_WIN) || defined(OPTION_ATOMIC_EMULATION) || (!defined(OPTION_QUEUE_64_FIX_SIZE) && !defined(OPTION_QUEUE_64_VAR_SIZE))
    void *handle;
#endif
    uint8_t *buffer; // Pointer to the buffer to the message payload
    uint16_t size;   // Size of the payload buffer in bytes
} tQueueBuffer;

/// Create new heap allocated queue. Free using `QueueDeinit`.
/// @param buffer_size Queue buffer size in bytes. Does not include the queue header size.
tQueueHandle QueueInit(uint32_t size_in_bytes);

/// Creates a queue inside the user provided buffer.
/// @precondition queue_buffer_size must at least fit the queue header and minimum payload size.
/// This can be used to place the queue inside shared memory
/// @param queue_buffer Buffer to place the queue in including the queue header.
/// @param queue_buffer_size Buffer size including the queue header.
/// @param clear_queue Clear the queue memory or keep the passed buffer untouched.
/// @param out out_buffer_size Optional out parameter can be used to get the remaining buffer size.
/// @return Queue handle.
/// NOTE: This is currently not implemented, but can be added if needed.
/// tQueueHandle QueueInitFromMemory(void *queue_buffer, int64_t queue_buffer_size, bool clear_queue, int64_t *out_buffer_size);

// Deinitialize queue. Does **not** free user allocated memory provided by `QueueInitFromMemory`.
/// @param queue_handle Queue handle.
void QueueDeinit(tQueueHandle queue_handle);

/// Acquire a buffer to be pushed into the queue. The buffer can be written by the user but will not be popped until it
/// is committed. The buffer must be released using `QueueRelease`.
/// NOTE: the buffer size may exceed the requested size due to padding (may be aligned to the system cache line to avoid false sharing).
/// The full returned buffer size can be safely read and written by the user even if it exceeds the requested size.
/// @param queue_handle Queue handle.
/// @param payload_size Requested buffer size.
/// @return Queue buffer.
tQueueBuffer QueueAcquire(tQueueHandle queue_handle, uint16_t payload_size);

// Push an acquired buffer to the queue

/// Push an acquired buffer to the queue to indicate that the data is written an can be popped from the queue.
/// @param queue_handle Queue handle.
/// @param queue_buffer Queue buffer.
/// @param flush Optional: Indicate producer priority, the queue may take measures to optimize. Implementation specific.
/// @return Queue buffer.
void QueuePush(tQueueHandle queue_handle, tQueueBuffer *const queue_buffer, bool flush);

/// Get a queue entry without removing it from the queue.
/// Single consumer thread only, not thread safe.
/// @param queue_handle Queue handle.
/// @param index Peak peak ahead index (index = 0 next).
/// @param flush Disable optimizations to guarantee it returns any commited data currently available. No lazy updates, no false negatives.
/// @param packets_lost Optional out parameter to get the number of packets lost since the last call.
/// @return Queue buffer, tQueueBuffer::size=0 if no buffer exists (with that index).
/// NOTE: The returned buffer must be released using `QueueRelease` and in the same order as they were obtained (sequential index order).
/// NOTE: The payload already includes header space for the XCP transport layer header (ctr+len) in the buffer, but the transport layer counter is not set yet!
/// NOTE: The function may be called multiple times with the same index, but the entries obtained must be released in sequential index order.
#ifdef OPTION_QUEUE_64_FIX_SIZE
tQueueBuffer QueuePeek(tQueueHandle queue_handle, int32_t index, bool flush, uint32_t *packets_lost);
#endif

/// Get the last entry from the queue.
/// Single consumer thread only, not thread safe.
/// @param queue_handle Queue handle.
/// @param flush Disable optimizations to guarantee it returns any commited data currently available. No lazy updates, no false negatives.
/// @param packets_lost Optional out parameter to get the number of packets lost since the last call.
/// @return Queue buffer. Buffer size is 0 if no buffer can be popped from the queue.
/// NOTE: The returned buffer must be released using `QueueRelease` before any other call to QueuePop.
/// NOTE: QueuePop initializes the XCP transport layer counter in the message by calling XcpTlGetCtr()
#ifndef OPTION_QUEUE_64_FIX_SIZE
tQueueBuffer QueuePop(tQueueHandle queue_handle, bool flush, uint32_t *packets_lost);
#endif

/// Release a buffer from `QueuePeek` or `QueuePop`.
/// Single consumer thread only, not thread safe.
/// This is required to notify the queue that it can reuse memory and it will end the lifetime of the buffer obtained from `QueuePeek` or `QueuePop`.
/// Queue entries must be released in the same order as they were obtained from `QueuePop` (FIFO order) or in sequential index order from `QueuePeek`.
/// @param queue_handle Queue handle.
/// @param queue_buffer Queue buffer to release.
void QueueRelease(tQueueHandle queue_handle, tQueueBuffer *const queue_buffer);

/// Get amount of bytes or entries (implementation specific)in the queue, 0 if queue is empty.
/// @param queue_handle Queue handle.
uint32_t QueueLevel(tQueueHandle queue_handle);

/// Clear queue content.
/// @param queue_handle Queue handle.
void QueueClear(tQueueHandle queue_handle);
