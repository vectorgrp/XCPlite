
# Queue test


## Temporary modifications to reference.c for testing the reference queue on macOS

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

### Test Results

- Sporadic chrashes observed on macOS 
- Maximum lock time is 8ms in the test use case, in XCP uses case it was 80ms. Anyway 8ms is not acceptable as well.


XCPlite Queue:  (Burst size 3)

Producer acquire lock time statistics:
  count=49759515  max_spins=0  max=2811458ns  avg=72ns

Lock time histogram (49759515 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                   7223459   14.52%  #########
  40-80ns                 22427777   45.07%  ##############################
  80-120ns                11513621   23.14%  ###############
  120-160ns                6922343   13.91%  #########
  160-200ns                 913257    1.84%  #
  200-240ns                 187351    0.38%  
  240-280ns                 146291    0.29%  
  280-320ns                 100813    0.20%  
  320-360ns                  57708    0.12%  
  360-400ns                  27583    0.06%  
  400-600ns                  36866    0.07%  
  600-800ns                  44220    0.09%  
  800-1000ns                 35382    0.07%  
  1000-1500ns                53987    0.11%  
  1500-2000ns                20737    0.04%  
  2000-3000ns                14890    0.03%  
  3000-4000ns                 5477    0.01%  
  4000-6000ns                 6696    0.01%  
  6000-8000ns                 3448    0.01%  
  8000-10000ns                4896    0.01%  
  10000-20000ns              10256    0.02%  
  20000-40000ns               2213    0.00%  
  40000-80000ns                234    0.00%  
  80000-160000ns                 8    0.00%  
  >320000ns                      2    0.00%  

Reference API: (Burst size 3)

Producer acquire lock time statistics:
  count=17796132  max_spins=0  max=10419834ns  avg=242ns

Lock time histogram (17796132 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                   2237619   12.57%  ########
  40-80ns                  8327423   46.79%  ##############################
  80-120ns                 3366736   18.92%  ############
  120-160ns                 912343    5.13%  ###
  160-200ns                 349175    1.96%  #
  200-240ns                 241954    1.36%  
  240-280ns                 182417    1.03%  
  280-320ns                 144753    0.81%  
  320-360ns                 120970    0.68%  
  360-400ns                 102686    0.58%  
  400-600ns                 404561    2.27%  #
  600-800ns                 303064    1.70%  #
  800-1000ns                193226    1.09%  
  1000-1500ns               367694    2.07%  #
  1500-2000ns               180139    1.01%  
  2000-3000ns               163490    0.92%  
  3000-4000ns                66521    0.37%  
  4000-6000ns                52123    0.29%  
  6000-8000ns                23219    0.13%  
  8000-10000ns               17339    0.10%  
  10000-20000ns              34031    0.19%  
  20000-40000ns               3918    0.02%  
  40000-80000ns                601    0.00%  
  80000-160000ns                63    0.00%  
  160000-320000ns               13    0.00%  
  >320000ns                     54    0.00%  

### Findings
- Misspelled, rename mc_queue_peak to mc_queue_peek
- Clarify the semantics of mc_queue_pop, it does need a release, documentation is misleading
- Unclear purpose of the offset field in McQueueBuffer, waste of resources


## Inline wrappers to switch to the XCPlite queue in reference.h

The mc_queue_* declarations are guarded #ifndef MC_USE_XCPLITE_QUEUE. The new #ifdef MC_USE_XCPLITE_QUEUE block:

Includes queue.h
Provides static inline adapters for all 7 functions with the same signatures
Handles the key buffer-offset mismatch between the two APIs:
Acquire (producer) — queueAcquire returns a buffer already past QUEUE_ENTRY_USER_HEADER_SIZE, maps directly
Pop/Peak (consumer) — queuePeek returns a buffer including the user-header prefix
Release — restores the original queuePeek buffer pointer from McQueueBuffer.offset before calling queueRelease
The McQueueBuffer offset field is just set to 0. In the reference API offset is set the write index (head) in mc_queue_acquire, unclear for what purpose. It is never used in the implementation!!! 


Of course QUEUE_ENTRY_USER_HEADER_SIZE could just be defined to be 0

spinlock_lock, spinlock_unlock, align_node_address and all mc_queue_* function bodies wrapped in #ifndef MC_USE_XCPLITE_QUEUE / #endif to prevent multiple-definition conflicts with the inline adapters.

CMakeLists.txt

MC_USE_XCPLITE_QUEUE added alongside TEST_MC_QUEUE in the compile definitions for the QUEUE_TEST_MC_QUEUE=ON build.


## Proposal improvements of the MC queue reference API in reference.h

### Unifiy the 2 different queue APIs in reference.h.

Key API differences to bridge:

Handle types: tQueueHandle / McQueueHandle
Buffer types: tQueueBuffer (size=uint16_t) / McQueueBuffer (size=int64_t)
queuePush(h, buf, priority) vs mc_queue_push(h, buf) (no priority)
queuePeek(h, idx, &lost) vs mc_queue_peak(h, idx) (no lost count)
queuePop(h, accum, prio, &lost) vs mc_queue_pop(h) (no accumulation, no lost count)
MC queue has no XCP transport-layer header space (QUEUE_ENTRY_USER_HEADER_SIZE = 0)
Several QUEUE_* macros come from queue.h — must not reference them for MC queue
Remove McQueueBuffer.offset, this is a redundancy and waste of resources


### Proposed signature changes:
//   mc_queue_init_from_memory out_buffer_site is uint64_t * instead of int64_t *
//   mc_queue_acquire has uint16_t size parameter, payload size is limited to uint16_t
//   mc_queue_push has a priority parameter
//   mc_queue_peek has a packet_lost out parameter
//   mc_queue_peek parameter index is uint32_t

- Using const west for parameters ok ?

#


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
