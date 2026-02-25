/**********************************************************************************************************************
 *  COPYRIGHT
 *  -------------------------------------------------------------------------------------------------------------------
 *  \verbatim
 *  Copyright (c) 2026 by Vector Informatik GmbH. All rights reserved.
 *
 *                This software is copyright protected and proprietary to Vector Informatik GmbH.
 *                Vector Informatik GmbH grants to you only those rights as set out in the license conditions.
 *                All other rights remain with Vector Informatik GmbH.
 *  \endverbatim
 *  -------------------------------------------------------------------------------------------------------------------
 *  FILE DESCRIPTION
 *  -----------------------------------------------------------------------------------------------------------------*/
/*!       \file   reference.h
 *        \brief  -
 *
 *********************************************************************************************************************/
#ifndef LIB_REFERENCE_INCLUDE_REFERENCE_H_
#define LIB_REFERENCE_INCLUDE_REFERENCE_H_

/**********************************************************************************************************************
 *  INCLUDES
 *********************************************************************************************************************/
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus


uint64_t get_timestamp_ns(void);




/// Opaque queue handle for allowing multiple queues.
typedef struct McQueue* McQueueHandle;

/// Buffer acquired from the queue.
/// Cannot be transferred across processes via shared memory or serialization.
typedef struct {
  int64_t offset;
  int64_t size;
  // NOTE: the buffer pointer is redundant to the offset and is only meant for convience for the user
  uint8_t* buffer;
} McQueueBuffer;



static char const* const kMcGlobalLockName = "/tmp/ves_mc_global_lock";


int acquire_lock(char const* name);
int release_lock(int file_descriptor);


// ============================================================================
// Queues
// ============================================================================

#ifndef MC_USE_XCPLITE_QUEUE



/// Create new heap allocated queue. Free using `QueueDeinit`.
/// @param buffer_size Queue buffer size. Does not include the queue header size.
McQueueHandle mc_queue_init(size_t buffer_size);

/// Creates a queue inside the user provided buffer.
/// @precondition queue_buffer_size must at least fit the queue header and kQueueMaxBufferPayloadSize size.
/// This can be used to place the queue inside shared memory which is tested to work there as well.
/// @param queue_buffer Buffer to place the queue in including the queue header.
/// @param queue_buffer_size Buffer size including the queue header.
/// @param clear_queue Clear the queue memory or keep the passed buffer untouched.
/// @param out out_buffer_size Optional out parameter can be used to get the remaining buffer size.
/// @return Queue handle.
McQueueHandle mc_queue_init_from_memory(void* queue_buffer, size_t queue_buffer_size, bool clear_queue,
                                        int64_t* out_buffer_size);

/// Deinitialize queue. Does **not** free user allocated memory provided by `QueueInitFromMemory`.
/// @param handle Queue handle.
void mc_queue_deinit(McQueueHandle handle);

/// Acquire a buffer to be pushed into the queue. The buffer can be written by the user but will not be popped until it
/// is committed. The buffer must be released using `QueueRelease`.
/// NOTE: the buffer size may exceed the requested size due to padding (may be aligned to the system cache line to avoid
/// false sharing). The full returned buffer size can be safely read and written by the user even if it exceeds the
/// requested size.
/// @param handle Queue handle.
/// @param payload_size Requested buffer size.
/// @return Queue buffer.
McQueueBuffer mc_queue_acquire(McQueueHandle handle, size_t payload_size);

/// Release acquired buffer. This is required to notify the queue that it can reuse a memory region.
/// @param handle Queue handle.
/// @param queue_buffer Queue buffer.
void mc_queue_release(McQueueHandle handle, McQueueBuffer const* queue_buffer);

/// Push buffer to indicate that the data is written an can be popped from the queue.
/// @param handle Queue handle.
/// @param queue_buffer Queue buffer.
/// @return Queue buffer.
void mc_queue_push(McQueueHandle handle, McQueueBuffer const* queue_buffer);

/// Pop buffer from the queue.
/// @param handle Queue handle.
/// @param queue_buffer Queue buffer.
/// @return Queue buffer. Buffer size is 0 if no buffer can be popped from the queue.
McQueueBuffer mc_queue_pop(McQueueHandle handle);

/// Get the next queue buffer size without removing it from the queue.
/// @param handle Queue handle.
/// @param index Buffer index to peak from the current read index.
///        E.g.: 0 returns the size of the next in line buffer to be popped.
/// @return Queue buffer at given index relative to the current read index.
///         Buffer size is 0 if no buffer exists or is ready at that index.
McQueueBuffer mc_queue_peak(McQueueHandle handle, int64_t index);

#else  // MC_USE_XCPLITE_QUEUE

// ============================================================================
// Inline adapters: map mc_queue_* API to the xcplite queue.h implementation.
//
// Buffer pointer conventions in xcplite queue (queue64v / queue64f):
//   queueAcquire (producer): returns buffer pointing AFTER QUEUE_ENTRY_USER_HEADER_SIZE
//   queuePeek    (consumer): returns buffer pointing BEFORE the user header
//                            (i.e. QUEUE_ENTRY_USER_HEADER_SIZE bytes before the payload)
//   queueRelease             needs the original queuePeek buffer pointer and size
//
// Therefore the consumer-side wrappers (pop/peak):
//   - store the original queuePeek buffer ptr in McQueueBuffer.offset (for release)
//   - advance McQueueBuffer.buffer by QUEUE_ENTRY_USER_HEADER_SIZE (to reach payload)
//   - subtract QUEUE_ENTRY_USER_HEADER_SIZE from McQueueBuffer.size
//
// And mc_queue_release restores the original pointer/size from McQueueBuffer.offset.
//
// Note: McQueueBuffer.offset repurposed as "original peek buffer pointer".
//       tQueueBuffer.handle (32-bit/Windows builds) not yet supported.
// ============================================================================


#include "../../../../src/queue.h" // Include the XCPlite queue implementation header 

#if  !defined(OPTION_QUEUE_64_FIX_SIZE) && !defined(OPTION_QUEUE_64_VAR_SIZE)
#error "MC_USE_XCPLITE_QUEUE inline adapters supports only queue64v and queue64f"
#endif

static inline McQueueHandle mc_queue_init(size_t buffer_size) {
    return (McQueueHandle)queueInit(buffer_size);
}

static inline McQueueHandle mc_queue_init_from_memory(void *queue_buffer, size_t queue_buffer_size,
                                                      bool clear_queue, int64_t *out_buffer_size) {
    uint64_t out = 0;
    tQueueHandle h = queueInitFromMemory(queue_buffer, queue_buffer_size, clear_queue, &out);
    if (out_buffer_size) *out_buffer_size = (int64_t)out;
    return (McQueueHandle)h;
}

static inline void mc_queue_deinit(McQueueHandle handle) {
    queueDeinit((tQueueHandle)handle);
}

// Producer-side: queueAcquire returns buffer already past QUEUE_ENTRY_USER_HEADER_SIZE.
static inline McQueueBuffer mc_queue_acquire(McQueueHandle handle, size_t payload_size) {
    tQueueBuffer tb = queueAcquire((tQueueHandle)handle, (uint16_t)payload_size);
    McQueueBuffer mb;
    mb.offset = 0;
    mb.size   = (int64_t)tb.size;
    mb.buffer = tb.buffer;
    return mb;
}

// Producer-side: reconstruct tQueueBuffer from McQueueBuffer (buffer/size set by acquire).
static inline void mc_queue_push(McQueueHandle handle, McQueueBuffer const *queue_buffer) {
    tQueueBuffer tb;
    tb.buffer = queue_buffer->buffer;
    tb.size   = (uint16_t)queue_buffer->size;
    queuePush((tQueueHandle)handle, &tb, false);
}

// Consumer-side helper: queuePeek returns buffer before the user header.
// Advance .buffer past QUEUE_ENTRY_USER_HEADER_SIZE for the caller.
// offset is left as 0 - mc_queue_release reconstructs the original pointer by subtraction.
static inline McQueueBuffer mc_xcplite_peek_to_mc(tQueueBuffer tb) {
    McQueueBuffer mb;
    if (tb.size == 0 || tb.buffer == NULL) {
        mb.offset = 0;
        mb.size   = 0;
        mb.buffer = NULL;
    } else {
        mb.offset = 0;
        mb.size   = (int64_t)(tb.size > QUEUE_ENTRY_USER_HEADER_SIZE ? tb.size - QUEUE_ENTRY_USER_HEADER_SIZE : tb.size);
        mb.buffer = tb.buffer + QUEUE_ENTRY_USER_HEADER_SIZE;
    }
    return mb;
}

static inline McQueueBuffer mc_queue_pop(McQueueHandle handle) {
    return mc_xcplite_peek_to_mc(queuePeek((tQueueHandle)handle, 0, NULL));
}

// Note: intentionally misspelled to match reference.h API
static inline McQueueBuffer mc_queue_peak(McQueueHandle handle, int64_t index) {
    return mc_xcplite_peek_to_mc(queuePeek((tQueueHandle)handle, (uint32_t)index, NULL));
}

// Consumer-side: reconstruct original queuePeek buffer pointer by subtracting QUEUE_ENTRY_USER_HEADER_SIZE.
static inline void mc_queue_release(McQueueHandle handle, McQueueBuffer const *queue_buffer) {
    if (queue_buffer->size == 0) return;
    tQueueBuffer tb;
    tb.buffer = queue_buffer->buffer - QUEUE_ENTRY_USER_HEADER_SIZE;
    tb.size   = (uint16_t)(queue_buffer->size + QUEUE_ENTRY_USER_HEADER_SIZE);
    queueRelease((tQueueHandle)handle, &tb);
}

#endif  // MC_USE_XCPLITE_QUEUE



#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // LIB_REFERENCE_INCLUDE_REFERENCE_H_
