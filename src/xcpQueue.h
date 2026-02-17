#pragma once
#define __XCP_QUEUE_h__

/*----------------------------------------------------------------------------
| File:
|   xcpQueue.h
|
| Description:
|   XCPlite internal header file for xcpQueue64.c or xcpQueue32.c
||
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdint.h>

// Handle for queue
typedef struct tQueueHandleType *tQueueHandle;
#define UNDEFINED_QUEUE_HANDLE NULL

// Buffer acquired from the queue with `QueueAcquire` (producer) or from QueuePeek` (consumer)
typedef struct {
    uint8_t *buffer;
#if defined(PLATFORM_32BIT) || defined(_WIN) || defined(OPTION_ATOMIC_EMULATION)
    void *handle;
#endif
    uint16_t size; // Size of the buffer in bytes
} tQueueBuffer;

// Create new heap allocated queue. Free using `QueueDeinit`
tQueueHandle QueueInit(uint32_t size_in_bytes);

// Deinitialize queue.
void QueueDeinit(tQueueHandle queue_handle);

// Acquire a queue buffer of size bytes
tQueueBuffer QueueAcquire(tQueueHandle queue_handle, uint16_t size);

// Push an aquired buffer to the queue
void QueuePush(tQueueHandle queue_handle, tQueueBuffer *const queue_buffer, bool flush);

// Single consumer: Get next buffer from the queue
// Buffers must be released before acquiring a new one
tQueueBuffer QueuePeek(tQueueHandle queue_handle, bool flush, uint32_t *packets_lost);

// Release buffer from `QueuePeek`
// This is required to notify the queue that it can reuse a memory region.
void QueueRelease(tQueueHandle queue_handle, tQueueBuffer *const queue_buffer);

// Get amount of bytes in the queue, 0 if empty
uint32_t QueueLevel(tQueueHandle queue_handle);

// Clear queue content
void QueueClear(tQueueHandle queue_handle);
