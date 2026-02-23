
# Queue test


## Proposal for API chnges of the MC queue implementation in reference.h


Unifiy the 2 different queue APIs in reference.h.


Key API differences to bridge:

Handle types: tQueueHandle / McQueueHandle
Buffer types: tQueueBuffer (size=uint16_t) / McQueueBuffer (size=int64_t)
queuePush(h, buf, priority) vs mc_queue_push(h, buf) (no priority)
queuePeek(h, idx, &lost) vs mc_queue_peak(h, idx) (no lost count)
queuePop(h, accum, prio, &lost) vs mc_queue_pop(h) (no accumulation, no lost count)
MC queue has no XCP transport-layer header space (QUEUE_ENTRY_USER_HEADER_SIZE = 0)
Several QUEUE_* macros come from queue.h — must not reference them for MC queue



## Temporary modifications to reference.c for testing on macOS

Added #if !defined(__APPLE__)

Disable #include <link.h>
Disable mc_dump_phdr and dl_iterate_phdr usage in init_app_shm
Temporarily disable robust mutexes on macOS 

#if !defined(__APPLE__)
...
#else
static void init_robust_mutex(pthread_mutex_t* pMutex) {}
static bool try_acquire_robust_mutex(pthread_mutex_t* pMutex) {
  return true;
}
#endif


#if 0


// ============================================================================
// From reference.h
// ============================================================================


// mc_queue mapping
typedef struct Queue *McQueueHandle;
typedef struct QueueBuffer McQueueBuffer;
#define tQueueHandle McQueueHandle
#define tQueueBuffer McQueueBuffer
#define queueInit mc_queue_init
#define queueInitFromMemory mc_queue_init_from_memory
#define queueDeinit mc_queue_deinit
#define queueAcquire mc_queue_acquire
#define queuePush mc_queue_push
#define queuePeek mc_queue_peek
#define queuePop mc_queue_pop
#define queueRelease mc_queue_release

// @@@@ Differences to mc_queue_xxx API:
// Findings
//   Renamed mc_queue_peak to mc_queue_peek
//   Clarify the semantics of mc_queue_pop, does need a release, documentation is misleading
// Signature changes:
//   mc_queue_init_from_memory out_buffer_site is uint64_t * instead of int64_t *
//   mc_queue_acquire has uint16_t size parameter, payload size is limited to uint16_t
//   mc_queue_push has a priority parameter
//   mc_queue_peek has a packet_lost out parameter
//   mc_queue_peek parameter index is uint32_t
//   Using const west for parameters

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
McQueueHandle mc_queue_init_from_memory(void *queue_memory, size_t queue_memory_size, bool clear_queue, uint64_t *out_buffer_size);

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
McQueueBuffer mc_queue_acquire(McQueueHandle handle, uint16_t payload_size);

/// Push buffer to indicate that the data is written an can be popped from the queue.
/// @param handle Queue handle.
/// @param queue_buffer Queue buffer.
/// @param priority Indicate producer priority, the queue may take measures to optimize. Implementation specific.
/// @return Queue buffer.
void mc_queue_push(McQueueHandle handle, const McQueueBuffer *queue_buffer, bool priority);

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
McQueueBuffer mc_queue_peek(McQueueHandle handle, uint32_t index, uint32_t *packets_lost);

/// Release acquired buffer. This is required to notify the queue that it can reuse a memory region.
/// @param handle Queue handle.
/// @param queue_buffer Queue buffer.
void mc_queue_release(McQueueHandle handle, const McQueueBuffer *queue_buffer);

#endif
