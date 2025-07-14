#pragma once
#define __XCP_QUEUE_h__

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#include <stdbool.h>
#include <stdint.h>

// Handle for queue
typedef struct tQueueHandleType *tQueueHandle;
#define UNDEFINED_QUEUE_HANDLE NULL

// Buffer acquired from the queue with `QueueAcquire` (producer) or from QueuePeek` (consumer)
typedef struct {
    uint8_t *buffer;
    void *handle;
    uint16_t size; // Size of the buffer in bytes
} tQueueBuffer;

// Create new heap allocated queue. Free using `QueueDeinit`
tQueueHandle QueueInit(uint32_t buffer_size);

// Deinitialize queue.
void QueueDeinit(tQueueHandle queueHandle);

// Acquire a queue buffer of size bytes
tQueueBuffer QueueAcquire(tQueueHandle queueHandle, uint16_t size);

// Push an aquired buffer to the queue
void QueuePush(tQueueHandle queueHandle, tQueueBuffer *const handle, bool flush);

// Single consumer: Get next buffer from the queue
// Buffers must be released before aquiring a new one
tQueueBuffer QueuePeek(tQueueHandle queueHandle, bool flush, uint32_t *packets_lost);

// Release buffer from `QueuePeek`
// This is required to notify the queue that it can reuse a memory region.
void QueueRelease(tQueueHandle queueHandle, tQueueBuffer *const queueBuffer);

// Get amount of bytes in the queue, 0 if empty
uint32_t QueueLevel(tQueueHandle queueHandle);

// Clear queue content
void QueueClear(tQueueHandle queueHandle);
