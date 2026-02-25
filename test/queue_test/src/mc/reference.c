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
/*!       \file   reference.c
 *        \brief  -
 *
 *********************************************************************************************************************/

/**********************************************************************************************************************
 *  INCLUDES
 *********************************************************************************************************************/
// Define to request: ftruncate, pthread_mutex_consistent
#define _GNU_SOURCE

#include "reference.h"
#include "dbg_print.h"

#if defined(__QNXNTO__)
#include <sys/link.h>     /* dl_iterate_phdr, dl_phdr_info */
#include <sys/neutrino.h> /* _NTO_VERSION */
#if _NTO_VERSION >= 800
#include <qh/misc.h> /* qh_get_progname */
#endif
#else

#if !defined(__APPLE__)
#include <link.h>
#endif

#ifndef EOK
#define EOK 0
#endif

#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>




#if defined(__QNXNTO__)
#define CLOCK_TYPE CLOCK_MONOTONIC
#else
#define CLOCK_TYPE CLOCK_MONOTONIC_RAW
#endif



int acquire_lock(char const* name) {
  DBG_PRINTF4("Try to acquire global lock '%s'\n", name);
  int file_descriptor = open(name, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
  if (file_descriptor == -1) {
    DBG_PRINTF_ERROR("Opening file for inter-process locking failed with %s\n", strerror(errno));
    return -1;
  }

  struct timespec start = {0}, now = {0};
  long const sleep_ns = 100000;  // 100 microseconds per retry
  struct timespec sleep_ts = {0, 0};
  sleep_ts.tv_nsec = sleep_ns;

  if (clock_gettime(CLOCK_TYPE, &start) != 0) {
    DBG_PRINTF_ERROR("clock_gettime failed with errno %d\n", errno);
    close(file_descriptor);
    return -1;
  }

  while (1) {
    if (flock(file_descriptor, LOCK_EX | LOCK_NB) == 0) {
      break;
    }

    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      DBG_PRINTF_ERROR("flock failed with errno %d\n", errno);
      close(file_descriptor);
      return -1;
    }

    if (clock_gettime(CLOCK_TYPE, &now) != 0) {
      DBG_PRINTF_ERROR("clock_gettime failed with errno %d\n", errno);
      close(file_descriptor);
      return -1;
    }

    uint64_t elapsed_ms =
        (uint64_t)(now.tv_sec - start.tv_sec) * 1000ULL + (uint64_t)(now.tv_nsec - start.tv_nsec) / 1000000ULL;
    if (elapsed_ms >= 10 /*ms*/) {
      DBG_PRINTF_ERROR("flock timeout after %u ms\n", 1);
      close(file_descriptor);
      return -1;
    }

    // Sleep a short time and retry
    nanosleep(&sleep_ts, NULL);
  }

  DBG_PRINT4("Acquired lock\n");
  return file_descriptor;
}

int release_lock(int file_descriptor) {
  if (flock(file_descriptor, LOCK_UN) != 0) {
    DBG_PRINTF_ERROR("flock LOCK_UN failed with errno %d\n", errno);
    return -1;
  }

  if (close(file_descriptor) != 0) {
    DBG_PRINTF_ERROR("close lock failed with errno %d\n", errno);
    return -1;
  }

  DBG_PRINT4("Released global lock\n");
  return 0;
}

static int atomic_open_or_create_shm(char const* name, uint32_t const size, uint8_t** pShmPointer, bool forceCreation,
                                     void (*init_cb)(void*)) {
  static void* const kNoPointerHint = NULL;
  bool shm_created = true;
  *pShmPointer = NULL;

  int lock = acquire_lock(kMcGlobalLockName);
  if (lock == -1) {
    DBG_PRINT_ERROR("Failed to acquire global lock\n");
    return -1;
  }

  DBG_PRINTF3("Opening app shared memory, name = '%s'\n", name);
  // Exclusively create the file to check whether it is already open
  int shared_memory_file_descriptor = shm_open(name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
  if (shared_memory_file_descriptor == -1) {
    // If creation fails, the file is already existing or something bad happened
    if (errno == EEXIST) {
      DBG_PRINTF3("'%s' already exists\n", name);
      if (forceCreation) {
        DBG_PRINTF3("Unlink '%s' from file system and re-create\n", name);
        shm_unlink(name);
        shared_memory_file_descriptor = shm_open(name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
      } else {
        DBG_PRINTF3("Open existing '%s'\n", name);
        shm_created = false;
        // File already opened, just open the file and skip initialization
        shared_memory_file_descriptor = shm_open(name, O_RDWR, S_IRUSR | S_IWUSR);
      }
    }
    // If opening the existing file fails, or creation failed with errors other than EEXIST, the error must be
    // resolved externally by the user
    if (shared_memory_file_descriptor == -1) {
      DBG_PRINTF_ERROR("shared memory open failed with errno %d\n", errno);
      release_lock(lock);

      return -1;  // abort for reference only, you can return the result and
                  // handle the failure externally
    }
  } else {
    // This application just created the global SHM
  }

  if (shm_created) {
    DBG_PRINTF3("Created '%s'\n", name);
    int truncate_result = ftruncate(shared_memory_file_descriptor, size);
    if (truncate_result != 0) {
      DBG_PRINTF_ERROR("ftruncate failed with errno %d\n", errno);
      shm_unlink(name);  // Unlink so that the SHM object is removed after all file handles of other apps are closed
      close(shared_memory_file_descriptor);
      release_lock(lock);
      return -1;  // abort for reference only, you can return the result and
                  // handle the failure externally
    }
  }

  void* shared_memory_pointer =
      mmap(kNoPointerHint, size, PROT_READ | PROT_WRITE, MAP_SHARED, shared_memory_file_descriptor, 0);
  if (shared_memory_pointer == MAP_FAILED) {
    DBG_PRINTF_ERROR("mmap failed with errno %d\n", errno);
    if (shm_created) {
      shm_unlink(name);  // Unlink so that the SHM object is removed after all file handles of other apps are closed
    }
    close(shared_memory_file_descriptor);
    release_lock(lock);
    return -1;  // abort for reference only, you can return the result and
                // handle the failure externally
  }

  DBG_PRINTF3("Shared memory '%s': address = %p, size = %u\n", name, shared_memory_pointer, size);

  if (shm_created) {
    DBG_PRINTF3("Initialization '%s'\n", name);
    if (init_cb != NULL) {
      init_cb(shared_memory_pointer);
    }
  }

  *pShmPointer = shared_memory_pointer;
  int unlock_result = release_lock(lock);
  if (unlock_result != 0) {
    DBG_PRINT_ERROR("Failed to release global lock\n");
  }
  return shared_memory_file_descriptor;
}



// There should be better alternatives in your target specific environment than
// this portable reference
uint64_t get_timestamp_ns(void) {
  static uint64_t const kNanosecondsPerSecond = 1000000000ULL;
  struct timespec ts = {0};

  // NOLINTNEXTLINE(missing-includes) // do **not** include internal "bits" headers directly that clangd suggests.
  clock_gettime(CLOCK_TYPE, &ts);
  return ((uint64_t)ts.tv_sec) * kNanosecondsPerSecond + ((uint64_t)ts.tv_nsec);
}


#ifndef MC_USE_XCPLITE_QUEUE

// ============================================================================
// Queue library
// ============================================================================
// #pragma pack(push, 1)
typedef struct {
  // NOTE: monotonically increasing - they never wrap around since 64bit
  // values are enough
  atomic_int_fast64_t write_index;  // aka. head
  atomic_int_fast64_t read_index;   // aka. tail
  atomic_int_fast64_t lock;         // temporary spin lock
  int64_t buffer_size;              // does not include the header size
  bool is_external;                 // free memory only if the data is not external
  int8_t reserved[7];               // padding to 64bit
  int8_t reserved_cacheline[24];    // padding to 64**byte** cache line (assuming
                                    // x86_64)
} QueueHeader;
// #pragma pack(pop)

// #pragma pack(push, 1)
typedef struct {
  QueueHeader header;
  // The buffer follows the header but cannot be represented using a pointer
  // in shared memory.
} Queue;
// #pragma pack(pop)

// #pragma pack(push, 1)
typedef struct {
  int64_t size;  // node size excluding this node header
  atomic_int_fast64_t is_ready;
  // The buffer follows the header but cannot be represented using a pointer
  // in shared memory.
} NodeHeader;
// #pragma pack(pop)


// Hint to the CPU that we are spinning
#if defined(__x86_64__) || defined(__i386__)
#define spin_loop_hint() __asm__ volatile("pause" ::: "memory")
#elif defined(__aarch64__) || defined(__arm__)
#define spin_loop_hint() __asm__ volatile("yield" ::: "memory");
#else
#define spin_loop_hint() // Fallback: do nothing
#endif


static void spinlock_lock(atomic_int_fast64_t* lock) {
  int64_t expected = 0;
  int64_t const desired = 1;
  while (!atomic_compare_exchange_weak_explicit(lock, &expected, desired, memory_order_acquire, memory_order_relaxed)) {
    expected = 0;
    // @@@@ NOTE: A call to your target specific spinlock hint should be placed here.
    spin_loop_hint();
  }
}


static void spinlock_unlock(atomic_int_fast64_t* lock) { atomic_store_explicit(lock, 0, memory_order_release); }

static size_t align_node_address(size_t address) {
  return (address + (kMcQueueBufferAlignment - 1U)) & (~(kMcQueueBufferAlignment - 1U));
}

McQueueHandle mc_queue_init(size_t buffer_size) {
  static_assert(sizeof(QueueHeader) == 64,
                "QueueHeader size must be 64 bytes");  // match cacheline size on x86_64
  static_assert((sizeof(NodeHeader) % 8) == 0, "NodeHeader size must be 8 byte aligned");

  size_t const queue_size = sizeof(QueueHeader) + buffer_size;
  void* queue_buffer = (Queue*)malloc(queue_size);
  McQueueHandle queue = mc_queue_init_from_memory(queue_buffer, queue_size, true, NULL);
  ((Queue*)queue)->header.is_external = false;
  return queue;
}

McQueueHandle mc_queue_init_from_memory(void* queue_buffer, size_t queue_buffer_size, bool clear_queue,
                                        int64_t* out_buffer_size) {
  static_assert(sizeof(QueueHeader) == 64, "QueueHeader size must be 64 bytes");
  static_assert(sizeof(NodeHeader) == 16, "NodeHeader size must be 16 bytes");

  static size_t const kMinimumBufferSize = sizeof(QueueHeader) + kMcQueueMaxBufferPayloadSize;
  if (queue_buffer_size < kMinimumBufferSize) {
    // Otherwise acquire will always return 0 which is confusing to the user.
    DBG_PRINTF_ERROR("queue buffer size must be least %lli bytes, got %lli bytes\n", (long long)kMinimumBufferSize,
                 (long long)queue_buffer_size);
    assert(queue_buffer_size >= kMinimumBufferSize);
  }

  Queue* queue = (Queue*)queue_buffer;
  assert(queue);

  assert(queue_buffer_size >= kMcQueueMaxBufferPayloadSize + sizeof(NodeHeader));
  queue_buffer_size -= (kMcQueueMaxBufferPayloadSize + sizeof(NodeHeader));

  // For multi user shared memory queue, this is always true
  queue->header.is_external = true;

  if (clear_queue) {
    queue->header.write_index = 0;
    queue->header.read_index = 0;
    queue->header.lock = 0;
  }

  DBG_PRINTF4("mc_queue_init_from_memory: queue_buffer=%p, BEFORE setting buffer_size=%lld\n", queue_buffer,
               (long long)queue->header.buffer_size);

  assert(queue_buffer_size - sizeof(QueueHeader) < INT64_MAX);
  queue->header.buffer_size = (int64_t)(queue_buffer_size - sizeof(QueueHeader));

  DBG_PRINTF4(
      "mc_queue_init_from_memory: queue_buffer=%p, input_size=%lld (after adjust), buffer_size=%lld, "
      "sizeof(QueueHeader)=%zu\n",
      queue_buffer, (long long)queue_buffer_size, (long long)queue->header.buffer_size, sizeof(QueueHeader));

  if (out_buffer_size) {
    *out_buffer_size = queue->header.buffer_size;
  }

  return (McQueueHandle)queue;
}

void mc_queue_deinit(McQueueHandle handle) {
  Queue* queue = (Queue*)handle;
  assert(queue);
  if (!queue->header.is_external) {
    free(queue);
  }
}

McQueueBuffer mc_queue_acquire(McQueueHandle handle, size_t payload_size) {
  static_assert((sizeof(QueueHeader) % 8) == 0, "QueueHeader size must be 8 byte aligned");
  static_assert((sizeof(NodeHeader) % 8) == 0, "NodeHeader size must be 8 byte aligned");

  assert(payload_size <= INT64_MAX);

  Queue* queue = (Queue*)handle;
  assert(queue);
  uint8_t* buffer = (uint8_t*)&queue[sizeof(QueueHeader)];

  assert(payload_size <= kMcQueueMaxBufferPayloadSize);
  // NOTE: alignment logic must match mc_queue_release.
  size_t aligned_size = align_node_address(payload_size + sizeof(NodeHeader));
  assert(aligned_size <= INT64_MAX);

  spinlock_lock(&queue->header.lock);

  // Monotonically increasing pointers.
  int64_t read_index = atomic_load_explicit(&queue->header.read_index, memory_order_relaxed);
  int64_t write_index = atomic_load_explicit(&queue->header.write_index, memory_order_relaxed);
  int64_t space_used = write_index - read_index;

  DBG_PRINTF4(
      "mc_queue_acquire: handle=%p, payload_size=%lld, buffer_size=%lld, read_index=%lld, write_index=%lld, "
      "space_used=%lld, aligned_size=%lld\n",
      (void*)handle, (long long)payload_size, (long long)queue->header.buffer_size, (long long)read_index,
      (long long)write_index, (long long)space_used, (long long)aligned_size);

  if (space_used + (int64_t)aligned_size > queue->header.buffer_size) {
    // Handle overflow.
    // fprintf(stderr, "queue overflow: cannot provide the requested %lli byte
    // buffer\n", (long long)payload_size);
    spinlock_unlock(&queue->header.lock);
    McQueueBuffer out = {
        .offset = 0,
        .size = 0,
        .buffer = NULL,
    };
    return out;
  }

  // Ring buffer wrap around.
  int64_t wrapped_index = write_index % queue->header.buffer_size;  // single producer => write_index is still valid.
  uint8_t* node_data = &buffer[wrapped_index];
  NodeHeader* node_header = (NodeHeader*)node_data;
  atomic_store_explicit(&node_header->is_ready, 0, memory_order_relaxed);
  node_header->size = (int64_t)payload_size;

  McQueueBuffer out = {
      .offset = write_index,
      .size = (int64_t)payload_size,
      .buffer = &node_data[sizeof(NodeHeader)],  // skip the node header in the user handle.
  };

  (void)atomic_fetch_add_explicit(&queue->header.write_index, (int64_t)aligned_size, memory_order_release);
  spinlock_unlock(&queue->header.lock);

  return out;
}

void mc_queue_push(McQueueHandle handle, McQueueBuffer const* queue_buffer) {
  Queue* queue = (Queue*)handle;
  assert(queue);
  uint8_t* buffer = (uint8_t*)&queue[sizeof(QueueHeader)];

  int64_t wrapped_index = queue_buffer->offset % queue->header.buffer_size;
  uint8_t* node_data = &buffer[wrapped_index];
  NodeHeader* header = (NodeHeader*)node_data;
  // REVIEW: memory_order_relaxed is incorrect here on weak-memory architectures (ARM).
  // The producer writes payload data to buffer->buffer before calling mc_queue_push().
  // Those plain writes must be visible to the consumer before it reads the payload after
  // observing is_ready==1.  A relaxed store does NOT guarantee that the preceding payload
  // writes are visible to other cores.
  // Fix: use memory_order_release here, paired with memory_order_acquire in mc_queue_pop().
  atomic_store_explicit(&header->is_ready, 1, memory_order_release);
  //atomic_store_explicit(&header->is_ready, 1, memory_order_relaxed); // @@@@ REVIEW: this is incorrect on weak-memory architectures (ARM)
}

McQueueBuffer mc_queue_pop(McQueueHandle handle) {
  Queue* queue = (Queue*)handle;
  assert(queue);
  uint8_t* buffer = (uint8_t*)&queue[sizeof(QueueHeader)];

  // Monotonically increasing pointers.
  // Single consumer => read_index cannot change in parallel but write_index
  // can. write_index could therefore move after the space_used calculation
  // which is not an issue.
  int64_t read_index = atomic_load_explicit(&queue->header.read_index, memory_order_relaxed);
  int64_t write_index = atomic_load_explicit(&queue->header.write_index, memory_order_relaxed);
  int64_t space_used = write_index - read_index;
  if (space_used < (int64_t)sizeof(NodeHeader)) {
    McQueueBuffer out = {
        .size = 0,
        .buffer = NULL,
    };
    return out;
  }

  // Ring buffer wrap around.
  int64_t wrapped_index = read_index % queue->header.buffer_size;  // single consumer => read_index is still valid.
  uint8_t* node_data = &buffer[wrapped_index];
  NodeHeader* node_header = (NodeHeader*)node_data;

  // Each buffer contains an atomic is_ready flag, so the producer can copy the
  // data to the buffer before pushing it.
  // REVIEW: memory_order_relaxed is incorrect here on weak-memory architectures (ARM).
  // Without acquire ordering, the CPU/compiler is free to reorder the subsequent reads of
  // node_header->size and the payload data before this load.  The consumer could observe
  // is_ready==1 but still read stale (pre-write) payload bytes.
  // Fix: use memory_order_acquire here, paired with memory_order_release in mc_queue_push().
  if (!atomic_load_explicit(&node_header->is_ready, memory_order_acquire)) {
  //if (!atomic_load_explicit(&node_header->is_ready, memory_order_relaxed)) { // @@@@ REVIEW: this is incorrect on weak-memory architectures (ARM)
    // Producer called acquire but not push yet which sets the ready flag.
    McQueueBuffer out = {
        .size = 0,
        .buffer = NULL,
    };
    return out;
  }

  McQueueBuffer out = {
      .size = node_header->size,
      .buffer = &node_data[sizeof(NodeHeader)],  // skip the node header in the user handle.
  };
  return out;
}

McQueueBuffer mc_queue_peak(McQueueHandle handle, int64_t index) {
  Queue* queue = (Queue*)handle;
  assert(queue);

  spinlock_lock(&queue->header.lock);
  int64_t read_index = atomic_load_explicit(&queue->header.read_index, memory_order_relaxed);
  int64_t write_index = atomic_load_explicit(&queue->header.write_index, memory_order_relaxed);

  // NOTE(pwr): very simple but inefficient implementation: remove items in
  // locked state and restore on exit.
  // => will be replaced later - not relevant for a reference implementation.
  for (int64_t i = 0; i < index; ++i) {
    McQueueBuffer const buffer = mc_queue_pop(handle);
    mc_queue_release(handle, &buffer);
  }
  McQueueBuffer const buffer = mc_queue_pop(handle);

  // NOTE(pwr): do not use the assignment "=" operator for structs with atomics!
  // memcpy on atomics is undefined behavior as you must always use atomic
  // operations.
  atomic_store_explicit(&queue->header.read_index, read_index, memory_order_relaxed);
  atomic_store_explicit(&queue->header.write_index, write_index, memory_order_relaxed);
  spinlock_unlock(&queue->header.lock);

  return buffer;
}

void mc_queue_release(McQueueHandle handle, McQueueBuffer const* queue_buffer) {
  Queue* queue = (Queue*)handle;
  assert(queue);

  // NOTE: alignment logic must match mc_queue_acquire.
  size_t const aligned_size = align_node_address((size_t)(queue_buffer->size) + sizeof(NodeHeader));
  atomic_fetch_add_explicit(&queue->header.read_index, (int64_t)aligned_size, memory_order_release);
}

#endif  // MC_USE_XCPLITE_QUEUE

