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
#include "mc_logger.h"
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

// ============================================================================
// Internal helpers
// ============================================================================
static void sleep_ms(uint32_t ms) {
  if (ms == 0) {
    sleep(0);
  } else {
    struct timespec timeout = {0};
    static int32_t const kMsInS = 1000;
    static int32_t const kMsToNs = 1000000;
    timeout.tv_sec = (int32_t)ms / kMsInS;
    timeout.tv_nsec = (int64_t)(ms % kMsInS) * kMsToNs;

    struct timespec timerem = {0};
    nanosleep(&timeout, &timerem);
  }
}

#if defined(__QNXNTO__)
#define CLOCK_TYPE CLOCK_MONOTONIC
#else
#define CLOCK_TYPE CLOCK_MONOTONIC_RAW
#endif

#if !defined(__APPLE__)

#if defined(__QNXNTO__)
static int mc_dump_phdr(const struct dl_phdr_info* pinfo, size_t size, void* data) {
#else
static int mc_dump_phdr(struct dl_phdr_info* pinfo, size_t size, void* data) {
#endif
#if defined(__QNXNTO__)
  // On QNX, the application module name is the full path to the executable
#if _NTO_VERSION >= 800
  // QNX 8.0 is the first version to introduce qh_get_progname()
  // Strip off the path from the module name and compare it to the retrieved program name to find the corresponding phdr
  // entry
  char const* pName = strrchr(pinfo->dlpi_name, '/');
  char const* pAppName = qh_get_progname();
  if (NULL != pName) {
    pName += 1;
  } else {
    pName = pinfo->dlpi_name;
  }
  if (0 == strncmp(pName, pAppName, strlen(pAppName))) {
    *(uint8_t**)data = (uint8_t*)pinfo->dlpi_addr;
  }
#else
  // On QNX 7.1 or less, there is no API to retrieve the name of the current program
  // Name must be forwarded from args[0] to xcpAppl
  // Workaround for now: Assume that entry 0 always contains the application module
  if (*(uint8_t**)data) != NULL) {
        *(uint8_t **)data = (uint8_t *)pinfo->dlpi_addr;
    }
#endif
#else
  if (0 == strlen(pinfo->dlpi_name)) {
    *(uint8_t**)data = (uint8_t*)pinfo->dlpi_addr;
  }
#endif
  (void)size;
  return 0;
}

#endif


// ============================================================================
// General
// ============================================================================
typedef struct {
  uint8_t* shared_memory_pointer;
  size_t shared_memory_size;

  atomic_int_fast64_t current_value_offset;
  uint64_t block_offset;
} MeasurementBlock;

typedef struct {
  // Shared memory layout: cache line aligned blocks of:
  // * Header for shared meta data (page switching and synchronization).
  // * Working page content.
  // * Initial page content.
  McCalibrationBlockHeader* shared_memory_pointer_header;
  uint8_t* shared_memory_pointer_working;
  uint8_t* shared_memory_pointer_initial;
  size_t shared_memory_size;

  atomic_int_fast64_t current_value_offset;
} CalibrationBlock;

typedef struct {
  int shared_memory_file_desriptor;
  int32_t pid;
  uint8_t* shared_memory_pointer;
  size_t shared_memory_size;
  uint8_t* base_address;

  atomic_uint_fast64_t* bump_allocator_head;

  int uuid_lookup_file_descriptor;
  uint8_t* uuid_lookup_pointer;
  size_t uuid_lookup_size;

  MeasurementBlock* measurement_blocks[UINT8_MAX];
  McEventIdTable* event_id_table;
  pthread_mutex_t event_table_mutex;
  char const* assigned_event_block_names[UINT8_MAX];

  CalibrationBlock* calibration_blocks[UINT8_MAX];
  pthread_mutex_t calibration_table_mutex;
  char const* assigned_calibration_block_names[UINT8_MAX];

} Application;

typedef struct {
  uint8_t const* segment_base_pointer;
  pthread_mutex_t* lock;
} BlockReadLockImpl;
static_assert(sizeof(BlockReadLockImpl) == sizeof(McBlockReadLock), "Read lock size does not match opaque type size");

typedef struct {
  union {
    McBlockReadLock data;
    BlockReadLockImpl impl;
  };
} BlockReadLockUnion;

typedef struct {
  uint8_t* segment_base_pointer;
  pthread_mutex_t* lock;
} BlockWriteLockImpl;
static_assert(sizeof(BlockWriteLockImpl) == sizeof(McBlockWriteLock),
              "Write lock size does not match opaque type size");

typedef struct {
  union {
    McBlockWriteLock data;
    BlockWriteLockImpl impl;
  };
} BlockWriteLockUnion;

// Must be align to pointer size due to atomic.
typedef struct {
  atomic_uint_fast32_t offset;
} SharedStringPoolHeader;

typedef struct {
  McDriverVersion version;

  int global_shared_memory_file_descriptor;

  uint8_t* global_shared_memory_pointer;
  size_t global_shared_memory_size;

  atomic_int_fast64_t application_count;
  Application* applications[UINT8_MAX];

  McQueueHandle measurement_queue;
  McQueueHandle command_queue;
  McQueueHandle service_discovery_queue;

  XcpDaqLists const* daq_lists;

  McAppIdTable* app_id_table;
  McCalibrationBlockIdTable* calibration_block_id_table;
} State;

static State initial_state = {
    .version =
        {
            .major_version = MC_DRIVER_MAJOR_VERSION,
            .minor_version = MC_DRIVER_MINOR_VERSION,
            .patch_version = MC_DRIVER_PATCH_VERSION,
        },

    .global_shared_memory_file_descriptor = -1,
    .global_shared_memory_pointer = NULL,
    .global_shared_memory_size = 0,
    .application_count = 0,
    .measurement_queue = NULL,
    .command_queue = NULL,
    .service_discovery_queue = NULL,
    .daq_lists = NULL,
    .app_id_table = NULL,
    .calibration_block_id_table = NULL,
};

static State* state = &initial_state;

// Fowler–Noll–Vo (FNV) hash function extended to 128bit
static McUuid string_to_u128_fnv(char const* string, size_t string_length) {
  static uint64_t const kFnvOffset = 14695981039346656037ULL;
  static uint64_t const kFnvPrime = 1099511628211ULL;

  uint64_t hash_high = kFnvOffset;
  uint64_t hash_low = kFnvOffset;

  for (size_t i = 0; i < string_length; ++i) {
    hash_high = (hash_high ^ (uint64_t)string[i]) * kFnvPrime;
    hash_low = (hash_low ^ (uint64_t)(string[i] << 8)) * kFnvPrime;
  }

  McUuid result = {hash_high, hash_low};
  return result;
}



#if !defined(__APPLE__)


static void init_robust_mutex(pthread_mutex_t* pMutex) {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
  pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
  pthread_mutex_init(pMutex, &attr);
}

static bool try_acquire_robust_mutex(pthread_mutex_t* pMutex) {
  int err = pthread_mutex_trylock(pMutex);
  if (err == EOK) {
    return true;
  }

  if (err == EOWNERDEAD) {
    mc_log_warn("Previous owner of mutex died, marking state as consistent and re-acquire mutex\n");
    err = pthread_mutex_consistent(pMutex);
  }

  if (err == EOK) {
    mc_log_debug("Successfully locked mutex\n");
    return true;
  }

  mc_log_error("Locking mutex failed with err %d\n", err);
  return false;
}

#else

static void init_robust_mutex(pthread_mutex_t* pMutex) {}
static bool try_acquire_robust_mutex(pthread_mutex_t* pMutex) {
  return true;
}

#endif

void init_app_shm(void* pMemory) {
  // Zero-initialize EventTriggers to prevent garbage values in active_arg_count
  // which can cause out-of-bounds access in XcpStopMeasurement and other functions
  EventTriggers* pEventTriggers = (EventTriggers*)((uint8_t*)pMemory + kMcAppEventTriggersOffset);
  memset(pEventTriggers, 0, sizeof(EventTriggers));
}

void init_global_shm(void* pMemory) {
  McSharedIdTable* pMcAppIdTable = (McSharedIdTable*)((uint8_t*)pMemory + kMcAppIdMappingTableOffset);
  for (uint32_t i = 0; i < UINT8_MAX; i++) {
    init_robust_mutex(&pMcAppIdTable->entries[i].mutex);
  }
}

static uint8_t mc_shared_idtable_find_free(McSharedIdTable* table, const uint8_t max_id) {
  for (uint8_t i = 1; i <= max_id; ++i) {
    if (try_acquire_robust_mutex(&table->entries[i].mutex)) {
      mc_log_info("Successfully acquired ID %u\n", i);
      return i;
    } else {
      mc_log_debug("ID %u is in use\n", i);
    }
  }

  mc_log_error("No free ID found\n");

  return 0;
}

static uint8_t acquire_app_id(McSharedIdTable* table, uint8_t app_identifier) {
  if (app_identifier > MC_MAX_APP_ID) {
    mc_log_error("App ID %u is larger than the maximum of %u.\n", app_identifier, MC_MAX_APP_ID);
    return MC_INVALID_ID;
  } else if (app_identifier == MC_INVALID_ID) {
    mc_log_warn(
        "Database may change during every application restart, since no unique App ID has been assigned by the "
        "user.\n");
    return mc_shared_idtable_find_free(table, MC_MAX_APP_ID);
  }

  if (try_acquire_robust_mutex(&table->entries[app_identifier].mutex)) {
    mc_log_info("Successfully acquired App ID %u\n", app_identifier);
    return app_identifier;
  } else {
    mc_log_error("Requested App ID %u is in use\n", app_identifier);
  }

  return MC_INVALID_ID;
}

static McAppId get_app_id(McSharedIdTable* table, McUuid target_uuid, uint8_t app_identifier) {
  uint8_t appId = acquire_app_id(table, app_identifier);
  if (appId == MC_INVALID_ID) {
    mc_log_error("Failed to get App ID\n");
  }

  table->entries[appId].uuid = target_uuid;
  return (McAppId)appId;
}

static uint8_t mc_idtable_find_or_insert(McIdTable* table, McUuid target_uuid) {
  uint8_t free_index = MC_INVALID_ID;  // 0 means not found (reserved); valid indices are 1..UINT8_MAX-1
  for (uint16_t i = 1; i < UINT8_MAX; ++i) {
    McUuid* entry = &table->entries[i];
    if (entry->high == target_uuid.high && entry->low == target_uuid.low) {
      return (uint8_t)i;
    }
    if (free_index == 0 && entry->high == 0 && entry->low == 0) {
      free_index = (uint8_t)i;
    }
  }
  if (free_index != 0) {
    table->entries[free_index] = target_uuid;
    return free_index;
  }
  mc_log_error("ID table is full\n");
  return free_index;
}

// Insert name into assigned_names if not present; return error code on duplicate or full.
// This must be called while holding the corresponding table mutex.
// NOTE: (gjshk) For calibration block names this is not strictly necessary here but leaving it in for completeness.
// For events it is, events with the same name would receive the same id
static McResult mc_entity_name_track_unique(char const** assigned_names, char const* name, size_t name_length,
                                            char const* what) {
  size_t free_slot = SIZE_MAX;
  for (size_t i = 0; i < UINT8_MAX; ++i) {
    char const* existing = assigned_names[i];
    if (existing && (strncmp(existing, name, name_length) == 0)) {
      mc_log_error("%s name '%.*s' is already assigned\n", what, (int)name_length, name);
      return MC_RESULT_ERROR;
    }
    if ((free_slot == SIZE_MAX) && (existing == NULL)) {
      free_slot = i;
    }
  }

  if (free_slot == SIZE_MAX) {
    mc_log_error("%s name table is full\n", what);
    return MC_RESULT_ERROR;
  }

  char* copy = strndup(name, name_length);
  if (!copy) {
    mc_log_error("strndup failed, daemon will abort\n");
    return MC_RESULT_ERROR;
  }
  assigned_names[free_slot] = copy;
  return MC_RESULT_SUCCESS;
}

static bool is_letter(char character) {
  if ((character >= 'a') && (character <= 'z')) return true;
  if ((character >= 'A') && (character <= 'Z')) return true;
  return false;
}

static bool is_number(char character) {
  if ((character >= '0') && (character <= '9')) return true;
  return false;
}

int acquire_lock(char const* name) {
  mc_log_debug("Try to acquire global lock '%s'\n", name);
  int file_descriptor = open(name, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
  if (file_descriptor == -1) {
    mc_log_error("Opening file for inter-process locking failed with %s\n", strerror(errno));
    return -1;
  }

  struct timespec start = {0}, now = {0};
  long const sleep_ns = 100000;  // 100 microseconds per retry
  struct timespec sleep_ts = {0, 0};
  sleep_ts.tv_nsec = sleep_ns;

  if (clock_gettime(CLOCK_TYPE, &start) != 0) {
    mc_log_error("clock_gettime failed with errno %d\n", errno);
    close(file_descriptor);
    return -1;
  }

  while (1) {
    if (flock(file_descriptor, LOCK_EX | LOCK_NB) == 0) {
      break;
    }

    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      mc_log_error("flock failed with errno %d\n", errno);
      close(file_descriptor);
      return -1;
    }

    if (clock_gettime(CLOCK_TYPE, &now) != 0) {
      mc_log_error("clock_gettime failed with errno %d\n", errno);
      close(file_descriptor);
      return -1;
    }

    uint64_t elapsed_ms =
        (uint64_t)(now.tv_sec - start.tv_sec) * 1000ULL + (uint64_t)(now.tv_nsec - start.tv_nsec) / 1000000ULL;
    if (elapsed_ms >= 10 /*ms*/) {
      mc_log_error("flock timeout after %u ms\n", 1);
      close(file_descriptor);
      return -1;
    }

    // Sleep a short time and retry
    nanosleep(&sleep_ts, NULL);
  }

  mc_log_debug("Acquired lock\n");
  return file_descriptor;
}

int release_lock(int file_descriptor) {
  if (flock(file_descriptor, LOCK_UN) != 0) {
    mc_log_error("flock LOCK_UN failed with errno %d\n", errno);
    return -1;
  }

  if (close(file_descriptor) != 0) {
    mc_log_error("close lock failed with errno %d\n", errno);
    return -1;
  }

  mc_log_debug("Released global lock\n");
  return 0;
}

static int atomic_open_or_create_shm(char const* name, uint32_t const size, uint8_t** pShmPointer, bool forceCreation,
                                     void (*init_cb)(void*)) {
  static void* const kNoPointerHint = NULL;
  bool shm_created = true;
  *pShmPointer = NULL;

  int lock = acquire_lock(kMcGlobalLockName);
  if (lock == -1) {
    mc_log_error("Failed to acquire global lock\n");
    return -1;
  }

  mc_log_info("Opening app shared memory, name = '%s'\n", name);
  // Exclusively create the file to check whether it is already open
  int shared_memory_file_descriptor = shm_open(name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
  if (shared_memory_file_descriptor == -1) {
    // If creation fails, the file is already existing or something bad happened
    if (errno == EEXIST) {
      mc_log_info("'%s' already exists\n", name);
      if (forceCreation) {
        mc_log_info("Unlink '%s' from file system and re-create\n", name);
        shm_unlink(name);
        shared_memory_file_descriptor = shm_open(name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
      } else {
        mc_log_info("Open existing '%s'\n", name);
        shm_created = false;
        // File already opened, just open the file and skip initialization
        shared_memory_file_descriptor = shm_open(name, O_RDWR, S_IRUSR | S_IWUSR);
      }
    }
    // If opening the existing file fails, or creation failed with errors other than EEXIST, the error must be
    // resolved externally by the user
    if (shared_memory_file_descriptor == -1) {
      mc_log_error("shared memory open failed with errno %d\n", errno);
      release_lock(lock);

      return -1;  // abort for reference only, you can return the result and
                  // handle the failure externally
    }
  } else {
    // This application just created the global SHM
  }

  if (shm_created) {
    mc_log_info("Created '%s'\n", name);
    int truncate_result = ftruncate(shared_memory_file_descriptor, size);
    if (truncate_result != 0) {
      mc_log_error("ftruncate failed with errno %d\n", errno);
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
    mc_log_error("mmap failed with errno %d\n", errno);
    if (shm_created) {
      shm_unlink(name);  // Unlink so that the SHM object is removed after all file handles of other apps are closed
    }
    close(shared_memory_file_descriptor);
    release_lock(lock);
    return -1;  // abort for reference only, you can return the result and
                // handle the failure externally
  }

  mc_log_info("Shared memory '%s': address = %p, size = %u\n", name, shared_memory_pointer, size);

  if (shm_created) {
    mc_log_info("Initialization '%s'\n", name);
    if (init_cb != NULL) {
      init_cb(shared_memory_pointer);
    }
  }

  *pShmPointer = shared_memory_pointer;
  int unlock_result = release_lock(lock);
  if (unlock_result != 0) {
    mc_log_error("Failed to release global lock\n");
  }
  return shared_memory_file_descriptor;
}

bool mc_check_identifier(char const* identifier, size_t identifier_length) {
  if (identifier_length > MC_MAX_STRLEN) return false;
  if ((!is_letter(identifier[0])) && (identifier[0] != '_')) return false;

  for (size_t i = 1; i < identifier_length; ++i) {
    if ((!is_letter(identifier[i])) && (!is_number(identifier[i]))  //
        && (identifier[i] != '_')                                   //
        && (identifier[i] != '.')) {
      return false;
    }
  }
  return true;
}

McDriverHandle mc_get_driver_handle(McDriverVersion* out_driver_version) {
  if (out_driver_version != NULL) *out_driver_version = state->version;
  return (McDriverHandle)state;
}

bool mc_set_driver_handle(McDriverHandle handle) {
  if (handle == NULL) return false;
  State* new_state = (State*)handle;
  if ((new_state->version.major_version == state->version.major_version) &&
      (new_state->version.minor_version >= state->version.minor_version)) {
    state = (State*)handle;
    return true;
  }
  return false;
}

McResult mc_application_init(char const* name, size_t name_length, McUuid binary_uuid, McAppId* out_app_id,
                             uint8_t app_identifier) {
  if (out_app_id == NULL) {
    mc_log_error("out_app_id parameter is NULL pointer\n");
    return MC_RESULT_ERROR;
  }

  // Print memory layout sizes to verify no overlap
  mc_log_debug("MEMORY LAYOUT: sizeof(TriggerArg)=%zu, sizeof(EventTrigger)=%zu, sizeof(EventTriggers)=%zu\n",
               sizeof(struct TriggerArg), sizeof(struct EventTrigger), sizeof(EventTriggers));
  mc_log_debug("MEMORY LAYOUT: kMcAppBumpAllocatorOffset=%zu, kMcAppEventTriggersOffset=%zu\n",
               kMcAppBumpAllocatorOffset, kMcAppEventTriggersOffset);
  mc_log_debug("MEMORY LAYOUT: kMcAppEventTriggersSize=%zu, kMcAppDynamicBlocksOffset=%zu\n", kMcAppEventTriggersSize,
               kMcAppDynamicBlocksOffset);
  mc_log_debug("MEMORY LAYOUT: kMcAppSharedMemorySize=%zu\n", kMcAppSharedMemorySize);
  if (kMcAppDynamicBlocksOffset >= kMcAppSharedMemorySize) {
    mc_log_error(
        "CRITICAL ERROR: EventTriggers (%zu bytes) + offset (%zu) exceeds shared memory size (%zu)! No space for "
        "calibration blocks!\n",
        kMcAppEventTriggersSize, kMcAppDynamicBlocksOffset, kMcAppSharedMemorySize);
  }

  assert(name_length < kMcMaxAppNameLength);
  char full_name[kMcMaxPathLength];
  size_t adjustedNameLength = name_length;
  // Ensure that the shared memory identifier always starts with a '/' to comply with POSIX standard
  if (name[0] == '/') {
    // Name already starts with '/', do not add another
    memcpy(&full_name[0], name, name_length);
  } else {
    full_name[0] = '/';
    memcpy(&full_name[1], name, name_length);
    adjustedNameLength++;
  }
  memcpy(&full_name[adjustedNameLength], kMcAppSharedMemoryPathSuffix, kMcAppSharedMemorySuffixLength);
  uint8_t* shared_memory_pointer;
  int shared_memory_file_descriptor =
      atomic_open_or_create_shm(full_name, kMcAppSharedMemorySize, &shared_memory_pointer, true, init_app_shm);
  if (shared_memory_file_descriptor == -1 || shared_memory_pointer == NULL) {
    mc_log_error("Failed to open app shared memory\n");
    return MC_RESULT_ERROR;
  }
  mc_log_debug("App shared memory pointer: %p, size = %lu\n", shared_memory_pointer, kMcAppSharedMemorySize);

  memcpy(&full_name[adjustedNameLength], kMcAppUuidLookupPathSuffix, kMcAppUuidLookupSuffixLength);
  uint8_t* uuid_lookup_shared_memory_pointer;
  int uuid_lookup_shared_memory_file_descriptor =
      atomic_open_or_create_shm(full_name, kMcAppUuidLookupSize, &uuid_lookup_shared_memory_pointer, false, NULL);
  if (uuid_lookup_shared_memory_file_descriptor == -1 || uuid_lookup_shared_memory_pointer == NULL) {
    mc_log_error("Failed to open UUID lookup shared memory\n");
    close(shared_memory_file_descriptor);
    munmap(shared_memory_pointer, kMcAppSharedMemorySize);
    return MC_RESULT_ERROR;
  }
  mc_log_debug("UUID lookup shared memory pointer: %p, size = %lu\n", uuid_lookup_shared_memory_pointer,
               kMcAppUuidLookupSize);

  // Check for McUuid in the first 128 bits (16 bytes) of local shared memory and generate if not present
  McUuid* shm_uuid = (McUuid*)uuid_lookup_shared_memory_pointer;
  bool shm_uuid_set = (shm_uuid->high != 0 || shm_uuid->low != 0);
  if (!shm_uuid_set) {
    memcpy(shm_uuid, &binary_uuid, sizeof(McUuid));
  }

  // Lazily map the global shared memory file once
  if (state->global_shared_memory_file_descriptor == -1) {
    char const* shm_name = getenv(kMcShmNameEnvVar);
    if (shm_name == NULL) {
      shm_name = kMcGlobalSharedMemoryName;
      mc_log_info("Environment variable '%s' not set, using '%s'\n", kMcShmNameEnvVar, shm_name);
    }
    mc_log_info("Open global shared memory, name = '%s'\n", shm_name);
    state->global_shared_memory_file_descriptor = atomic_open_or_create_shm(
        shm_name, kMcGlobalSharedMemorySize, &state->global_shared_memory_pointer, false, init_global_shm);
    if (state->global_shared_memory_file_descriptor == -1) {
      mc_log_error("open failed, app will abort (errno=%d)\n", errno);
      munmap(uuid_lookup_shared_memory_pointer, kMcAppUuidLookupSize);
      close(uuid_lookup_shared_memory_file_descriptor);
      munmap(shared_memory_pointer, kMcAppSharedMemorySize);
      close(shared_memory_file_descriptor);
      return MC_RESULT_ERROR;
    }
    mc_log_debug("Global shared memory pointer: %p, size=%lu\n", (void*)state->global_shared_memory_pointer,
                 kMcGlobalSharedMemorySize);

    atomic_store_explicit(&state->application_count, 0, memory_order_relaxed);

    state->global_shared_memory_size = kMcGlobalSharedMemorySize;
    state->daq_lists = (XcpDaqLists const*)(state->global_shared_memory_pointer + kMcMeasurementListOffset);

    mc_log_debug("global_shm_ptr=%p, kMcMeasurementQueueOffset=%zu, kMcMeasurementQueueSize=%zu\n",
                 (void*)state->global_shared_memory_pointer, kMcMeasurementQueueOffset, kMcMeasurementQueueSize);
    mc_log_debug("measurement_queue will be at %p\n",
                 (void*)(state->global_shared_memory_pointer + kMcMeasurementQueueOffset));

    state->measurement_queue = mc_queue_init_from_memory(
        state->global_shared_memory_pointer + kMcMeasurementQueueOffset, kMcMeasurementQueueSize, false, NULL);

    mc_log_debug("state->measurement_queue = %p\n", (void*)state->measurement_queue);

    state->command_queue = mc_queue_init_from_memory(state->global_shared_memory_pointer + kMcCommandQueueOffset,
                                                     kMcCommandQueueSize, false, NULL);
    state->service_discovery_queue =
        mc_queue_init_from_memory(state->global_shared_memory_pointer + kMcServiceDiscoveryQueueOffset,
                                  kMcServiceDiscoveryQueueSize, false, NULL);

    state->app_id_table = (McSharedIdTable*)(state->global_shared_memory_pointer + kMcAppIdMappingTableOffset);
    state->calibration_block_id_table =
        (McCalibrationBlockIdTable*)(state->global_shared_memory_pointer + kMcCalibrationIdMappingTableOffset);

    mc_log_debug("Global shared memory file descriptor: %d\n", state->global_shared_memory_file_descriptor);
  }

  McSharedIdTable* table = state->app_id_table;
  if (table == NULL) {
    mc_log_error("App ID table not initialized\n");
    munmap(uuid_lookup_shared_memory_pointer, kMcAppUuidLookupSize);
    close(uuid_lookup_shared_memory_file_descriptor);
    munmap(shared_memory_pointer, kMcAppSharedMemorySize);
    close(shared_memory_file_descriptor);
    return MC_RESULT_ERROR;
  }

  int lock = acquire_lock(kMcGlobalLockName);
  if (lock == -1) {
    mc_log_error("Failed to acquire global lock\n");
    munmap(uuid_lookup_shared_memory_pointer, kMcAppUuidLookupSize);
    close(uuid_lookup_shared_memory_file_descriptor);
    munmap(shared_memory_pointer, kMcAppSharedMemorySize);
    close(shared_memory_file_descriptor);
    return MC_RESULT_ERROR;
  }

  McAppId app_id = get_app_id(table, *shm_uuid, app_identifier);
  if (app_id == MC_INVALID_ID) {
    mc_log_error("Failed to get App ID\n");
    release_lock(lock);
    munmap(uuid_lookup_shared_memory_pointer, kMcAppUuidLookupSize);
    close(uuid_lookup_shared_memory_file_descriptor);
    munmap(shared_memory_pointer, kMcAppSharedMemorySize);
    close(shared_memory_file_descriptor);
    return MC_RESULT_ERROR;
  }

  // Check if the binary and shm UUIDs match i.e: if the app was recompiled
  bool match = (shm_uuid->high == binary_uuid.high) && (shm_uuid->low == binary_uuid.low);
  if (!match) {
    mc_log_warn("App with id %d recompiled, resetting event and calseg mappings\n", app_id);

    // If recompiled we have to reset all the event mappings
    McEventIdTable* event_id_table =
        (McEventIdTable*)((uint8_t*)uuid_lookup_shared_memory_pointer + kMcAppEventIdTableOffset);
    memset(event_id_table, 0, kMcAppEventIdTableSize);

    // If recompiled we have to remove all calibration block mappings from global table
    McCalibrationBlockIdTable* calseg_table = state->calibration_block_id_table;
    for (size_t i = 1; i < UINT8_MAX; ++i) {
      McUuid entry = calseg_table->entries[i];
      McAppId encoded_app_id = (McAppId)((entry.high >> 56) & 0xFFU);
      if (encoded_app_id == app_id) {
        mc_log_warn("Deleting calibration block id %zu with app id %d\n", i, app_id);
        calseg_table->entries[i] = (McUuid){0, 0};
      }
    }

    shm_uuid->high = binary_uuid.high;
    shm_uuid->low = binary_uuid.low;
    table->entries[app_id].uuid = *shm_uuid;
  }

  if (release_lock(lock) != 0) {
    mc_log_error("Failed to release global lock\n");
    return MC_RESULT_ERROR;
  }

  Application** app = &state->applications[app_id];
  if (*app != NULL) {
    mc_log_error("Application slot for ID %d is already in use\n", app_id);
    return MC_RESULT_ERROR;
  }
  *app = malloc(sizeof(Application));
  if (*app == NULL) {
    mc_log_error("Failed to allocate memory for Application structure\n");
    return MC_RESULT_ERROR;
  }

  atomic_uint_fast64_t* bump_allocator_head = (atomic_uint_fast64_t*)((uint8_t*)shared_memory_pointer);
  // Measurement and calibration blocks start after EventTriggers, aligned to kMcBlockAlignment
  atomic_store_explicit(bump_allocator_head, kMcAppDynamicBlocksOffset, memory_order_relaxed);

  uint8_t* base_address = NULL;
  #ifndef __APPLE__
  dl_iterate_phdr(mc_dump_phdr, &base_address);
  if (base_address == NULL) {
    mc_log_error("Failed to retrieve base address\n");
    free(*app);
    *app = NULL;
    return MC_RESULT_ERROR;
  }
  #endif

  Application new_app = {
      .pid = (int32_t)getpid(),
      .shared_memory_file_desriptor = shared_memory_file_descriptor,
      .shared_memory_pointer = shared_memory_pointer,
      .shared_memory_size = kMcAppSharedMemorySize,
      .bump_allocator_head = bump_allocator_head,
      .uuid_lookup_file_descriptor = uuid_lookup_shared_memory_file_descriptor,
      .uuid_lookup_pointer = uuid_lookup_shared_memory_pointer,
      .uuid_lookup_size = kMcAppUuidLookupSize,
      .base_address = base_address,
      .event_id_table = (McEventIdTable*)((uint8_t*)uuid_lookup_shared_memory_pointer + kMcAppEventIdTableOffset),
  };
  **app = new_app;

  if (pthread_mutex_init(&(*app)->event_table_mutex, NULL) != 0) {
    mc_log_error("pthread_mutex_init(event) failed (errno=%d)\n", errno);
    free(*app);
    *app = NULL;
    return MC_RESULT_ERROR;
  }
  if (pthread_mutex_init(&(*app)->calibration_table_mutex, NULL) != 0) {
    mc_log_error("pthread_mutex_init(calib) failed (errno=%d)\n", errno);
    pthread_mutex_destroy(&(*app)->event_table_mutex);
    free(*app);
    *app = NULL;
    return MC_RESULT_ERROR;
  }

  mc_log_debug("App ID: %d (0x%02X). UUID: 0x%016llx 0x%016llx\n", app_id, app_id, (unsigned long long)shm_uuid->high,
               (unsigned long long)shm_uuid->low);

  // NOTE: (gjshk) Leaving this here for future debugging
  // Print the layout of the local shared memory
  // printf("Local SHM layout (shared between app and daemon)\n");
  // printf("  Shared memory size:         %zu\n", kMcAppSharedMemorySize);
  // printf("  Shared memory pointer:      %p\n", shared_memory_pointer);
  // printf("  Bump allocator offset:      %zu\n", atomic_load_explicit(bump_allocator_head, memory_order_relaxed));
  // printf("  Bump allocator pointer:     %p\n", (void *)((uint8_t *)shared_memory_pointer));

  // printf("UUID lookup layout (used to persist UUIDs in between application restarts)\n");
  // printf("  UUID offset:                %zu\n", kMcUuidOffset);
  // printf("  EventIdTable offset:        %zu\n", kMcAppEventIdTableOffset);
  // printf("  EventIdTable size:          %zu\n", kMcAppEventIdTableSize);
  // printf("  EventIdTable pointer:       %p\n", (void *)((uint8_t *)uuid_lookup_shared_memory_pointer +
  // kMcAppEventIdTableOffset));

  // Set everything after uuid and event table to 'X" for debugging
  // Fill measurement and calibration block area with 'X' for debugging (after EventTriggers)
  memset((uint8_t*)shared_memory_pointer + kMcAppDynamicBlocksOffset, 'X',
         kMcAppSharedMemorySize - kMcAppDynamicBlocksOffset);
  (void)atomic_fetch_add_explicit(&state->application_count, 1, memory_order_relaxed);

  *out_app_id = app_id;
  return MC_RESULT_SUCCESS;
}

// TODO: (gjshk) Free calsegs and app id from global when deinitializing?
McResult mc_application_deinit(McAppId app_id) {
  if (state->global_shared_memory_file_descriptor == -1) {
    mc_log_error("Global shared memory not initialized\n");
    return MC_RESULT_ERROR;
  }

  Application* app = state->applications[app_id];
  if (app == NULL) {
    mc_log_error("Application with ID %d not initialized\n", app_id);
    return MC_RESULT_ERROR;
  }

  for (McEventId event_i = 0; event_i < sizeof(app->measurement_blocks) / sizeof(app->measurement_blocks[0]);
       ++event_i) {
    MeasurementBlock** messurement_segment = &app->measurement_blocks[event_i];
    if (*messurement_segment != NULL) {
      free(*messurement_segment);
      *messurement_segment = NULL;
    }
  }

  for (McCalibrationBlockId calseg_i = 0;
       calseg_i < sizeof(app->calibration_blocks) / sizeof(app->calibration_blocks[0]); ++calseg_i) {
    CalibrationBlock** calseg = &app->calibration_blocks[calseg_i];
    if (*calseg != NULL) {
      pthread_mutex_destroy(&(*calseg)->shared_memory_pointer_header->lock);
      free(*calseg);
      *calseg = NULL;
    }
  }

  int unmap_result = munmap(app->shared_memory_pointer, app->shared_memory_size);
  if (unmap_result != 0) {
    mc_log_error("Failed to unmap app shared memory (errno=%d)\n", errno);
    return MC_RESULT_ERROR;
  }

  int close_result = close(app->shared_memory_file_desriptor);
  if (close_result != 0) {
    mc_log_error("close shared memory failed (errno=%d)\n", errno);
    return MC_RESULT_ERROR;
  }

  close_result = close(app->uuid_lookup_file_descriptor);
  if (close_result != 0) {
    mc_log_error("close uuid lookup failed (errno=%d)\n", errno);
    return MC_RESULT_ERROR;
  }

  // Destroy per-app table mutexes
  pthread_mutex_destroy(&app->event_table_mutex);
  pthread_mutex_destroy(&app->calibration_table_mutex);

  free(app);
  state->applications[app_id] = NULL;

  // Deinitialize global shared memory when closing the last application
  int64_t last_application_count = atomic_fetch_sub_explicit(&state->application_count, 1, memory_order_relaxed);
  if (last_application_count <= 1) {  // atomic fetch returns last value -> 1 on last deinit, not 0
    int global_unmap_result = munmap(state->global_shared_memory_pointer, state->global_shared_memory_size);
    if (global_unmap_result != 0) {
      mc_log_error("Failed to unmap global shared memory (errno=%d)\n", errno);
      return MC_RESULT_ERROR;
    }

    int global_close_result = close(state->global_shared_memory_file_descriptor);
    if (global_close_result != 0) {
      mc_log_error("Failed to close global shared memory file descriptor (errno=%d)\n", errno);
      return MC_RESULT_ERROR;
    }

    state->global_shared_memory_file_descriptor = -1;
  }

  return MC_RESULT_SUCCESS;
}

bool mc_application_enqueue_service_discovery_message(McAppId app_id, uint8_t const* message, size_t message_size) {
  assert(state->service_discovery_queue);
  assert(message_size <= INT64_MAX);
  (void)app_id;  // unused. All SD messages are pushed to the same global queue
  static uint32_t const kTimeout = 5;
  for (uint32_t timeout = 0; timeout < kTimeout; timeout++) {
    McQueueBuffer buffer = mc_queue_acquire(state->service_discovery_queue, message_size);
    if (buffer.size >= (int64_t)message_size) {
      memcpy(buffer.buffer, message, message_size);
      mc_queue_push(state->service_discovery_queue, &buffer);
      return true;
    }
    // Wait some time and try again
    sleep_ms(10);
  }
  return false;
}

McQueueBuffer mc_application_acquire_service_discovery_message(McAppId app_id, size_t message_size) {
  assert(state->service_discovery_queue);
  assert(message_size <= INT64_MAX);
  (void)app_id;  // unused. All SD messages are pushed to the same global queue
  static uint32_t const kTimeout = 5;
  for (uint32_t timeout = 0; timeout < kTimeout; timeout++) {
    McQueueBuffer buffer = mc_queue_acquire(state->service_discovery_queue, message_size);
    if (buffer.size >= (int64_t)message_size) {
      return buffer;
    }

    // Wait some time and try again
    sleep_ms(10);
  }

  McQueueBuffer buffer = {
      .offset = 0,
      .size = 0,
      .buffer = NULL,
  };
  return buffer;
}

void mc_application_push_service_discovery_message(McQueueBuffer const* buffer) {
  mc_queue_push(state->service_discovery_queue, buffer);
}

void mc_application_write_message(McAppId app_id, char const* message) {
  // TODO: NYI. Not important now.
  (void)app_id;
  (void)message;
  mc_log_warn("mc_application_write_message is not implemented yet\n");
}

void mc_application_notify_model_change(McAppId app_id, McModelChecksum model_checksum) {
  // TODO: NYI. Not important now.
  (void)app_id;
  (void)model_checksum;
  mc_log_warn("mc_application_notify_model_change is not implemented yet\n");
}

EventTriggers* mc_get_event_triggers(McAppId app_id) {
  if (app_id == 0 || app_id >= UINT8_MAX) {
    return NULL;
  }
  Application* app = state->applications[app_id];
  if (app == NULL || app->shared_memory_pointer == NULL) {
    return NULL;
  }
  return (EventTriggers*)(app->shared_memory_pointer + kMcAppEventTriggersOffset);
}

McQueueHandle mc_get_measurement_queue(void) {
  mc_log_debug("mc_get_measurement_queue: returning %p\n", (void*)state->measurement_queue);
  return state->measurement_queue;
}

static size_t align_block_address(size_t address) {
  return (address + (kMcBlockAlignment - 1U)) & (~(kMcBlockAlignment - 1U));
}

static size_t align_block_value_address(size_t address) {
  return (address + (kMcBlockValueAlignment - 1U)) & (~(kMcBlockValueAlignment - 1U));
}

static uint64_t allocate_block(Application* app, size_t block_size) {
  size_t aligned_size = align_block_address(block_size);
  uint64_t segment_offset = atomic_fetch_add_explicit(app->bump_allocator_head, aligned_size, memory_order_acq_rel);
  assert((segment_offset % kMcBlockAlignment) == 0);  // blocks must be aligned for pointer casting external types.

  // NOTE(pwr): optional sentinel values for simpler debugging.
  {
    void* shared_memory_pointer = app->shared_memory_pointer + segment_offset;
    memset(shared_memory_pointer, 'C', aligned_size);
  }

  return segment_offset;
}

// ============================================================================
// Measurement
// ============================================================================
McEventId mc_measurement_event_init(McAppId app_id, char const* const event_name, size_t event_name_length,
                                    size_t maximum_measurement_buffer_size) {
  assert(state->global_shared_memory_file_descriptor != -1);
  Application* app = state->applications[app_id];
  assert(app != NULL);

  McUuid event_uuid = string_to_u128_fnv(event_name, event_name_length);
  if (pthread_mutex_lock(&app->event_table_mutex) != 0) {
    mc_log_error("Failed to lock event table mutex\n");
    return MC_INVALID_ID;
  }
  McResult name_result =
      mc_entity_name_track_unique(app->assigned_event_block_names, event_name, event_name_length, "Event");
  if (name_result != MC_RESULT_SUCCESS) {
    pthread_mutex_unlock(&app->event_table_mutex);
    mc_log_error("Failed to track unique event name, error code: %d\n", name_result);
    return MC_INVALID_ID;
  }
  McEventId event_id = (McEventId)mc_idtable_find_or_insert(app->event_id_table, event_uuid);
  pthread_mutex_unlock(&app->event_table_mutex);

  if (event_id == MC_INVALID_ID) {
    mc_log_error("Failed to get event ID\n");
    return event_id;
  }

  MeasurementBlock** segment = &app->measurement_blocks[event_id];
  assert(*segment == NULL);
  *segment = malloc(sizeof(MeasurementBlock));

  uint64_t block_offset = allocate_block(app, maximum_measurement_buffer_size);
  void* shared_memory_pointer = app->shared_memory_pointer + block_offset;
  memset(shared_memory_pointer, 'E', maximum_measurement_buffer_size);

  MeasurementBlock new_segment = {
      .shared_memory_pointer = shared_memory_pointer,
      .shared_memory_size = maximum_measurement_buffer_size,
      .block_offset = block_offset,
      .current_value_offset = 8,
  };
  **segment = new_segment;

  mc_log_info("Initialized event %s with id=%d in app %d\n", event_name, event_id, app_id);
  return event_id;
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

void mc_measurement_event_trigger(McAppId app_id, McEventId event_id, uint8_t const* stack_pointer) {
  assert(state->global_shared_memory_file_descriptor != -1);

  // Take the timestamp as soon and accurately as possible
  // We assume that all applications use the same clock as the daemon, thus
  // need no additional time synchronization
  uint64_t timestamp_ns = get_timestamp_ns();

  Application* app = state->applications[app_id];
  assert(app != NULL);

  XcpDaqLists const* const daq_lists = state->daq_lists;
  assert(daq_lists);

  MeasurementBlock* measurement_segment = app->measurement_blocks[event_id];

  McQueueHandle queue = state->measurement_queue;

  // ========================================================================
  // XCP specific DAQ measurment stuff starting here.
  // NOTE: this is only an optimization: preparing the measurments in the XCP
  // format avoids additional copies when packing the measurments into XCP on
  // Ethernet frames inside the Daemon with vectored IO instead of copies.
  // ========================================================================
  // The event channel stored in the DAQ lists is a combination of the 8bit
  // app ID and 8bit event ID
  EventChannel const event_channel = (EventChannel)(((uint16_t)app_id) << 8) | (uint16_t)event_id;

  static uint32_t const kOdtTimeStampSize = 4;  // ODT_TIMESTAMP_SIZE
  static uint32_t const kOdtHeaderSize = 4;     // ODT_HEADER_SIZE: ODT, align, DAQ_WORD header
  // This outer loop can be optimized using an alternative data structure that
  // is not supported in this reference
  for (uint16_t daq_list_index = 0; daq_list_index < daq_lists->daq_count; ++daq_list_index) {
    XcpDaqList const* const daq_list = &daq_lists->daq_list[daq_list_index];
    int32_t const daq_pid = daq_list->app_pid;

    if ((daq_list->state & kDaqStateRunning) == 0) {
      continue;
    }

    if (daq_list->event_channel != event_channel) {
      continue;
    }

    if (daq_pid != app->pid) {
      mc_log_debug("App %d skipping event trigger due to PID mismatch (App PID=%d, DAQ PID=%d)\n", app_id, app->pid,
                   daq_pid);
      continue;
    }

    // DAQ types reside in the same buffer in the order:
    // DAQ lists, ODT tables, ODT entry addresses, ODT entry sizes.
    XcpOdt const* const odt_table = (XcpOdt*)&state->daq_lists->daq_list[state->daq_lists->daq_count];

    size_t payload_offset = kOdtHeaderSize + kOdtTimeStampSize;
    for (uint32_t odt_index = daq_list->first_odt; odt_index <= daq_list->last_odt; ++odt_index) {
      XcpOdt const* const odt = &odt_table[odt_index];

      McQueueBuffer packet = mc_queue_acquire(queue, odt->size + payload_offset);
      if (packet.size == 0) {
        // fprintf(stderr, "measurement queue overflow. Dropping remaining
        // measurement data of event %d\n", event_id);
        return;
      }

      // ODT header: ODT8, FIL8, DAQ16 LE (XCP allows different header
      // formats that are not supported)
      assert(odt_index >= daq_list->first_odt);
      assert((odt_index - daq_list->first_odt) <= UINT8_MAX);
      uint8_t const relative_odt_index = (uint8_t)(odt_index - daq_list->first_odt);
      packet.buffer[0] = relative_odt_index;
      packet.buffer[1] = 0xAA;  // Align byte

      // DAQ list index.
      packet.buffer[2] = daq_list_index & 0xff;
      packet.buffer[3] = (daq_list_index >> 8) & 0xff;

      // Timestamp 32 bit
      // Only on the first ODT.
      if (payload_offset == kOdtHeaderSize + kOdtTimeStampSize) {
        packet.buffer[4] = timestamp_ns & 0xff;
        packet.buffer[5] = (timestamp_ns >> 8) & 0xff;
        packet.buffer[6] = (timestamp_ns >> 16) & 0xff;
        packet.buffer[7] = (timestamp_ns >> 24) & 0xff;
      }

      // DAQ lists, ODT tables, ODT entry addresses, ODT entry sizes.
      int32_t const* const odt_entry_address_table = (int32_t*)&odt_table[daq_lists->odt_count];
      uint8_t const* const odt_entry_size_table = (uint8_t*)&odt_entry_address_table[daq_lists->odt_entry_count];
      uint8_t const* const odt_entry_extension_table = (uint8_t*)&odt_entry_size_table[daq_lists->odt_entry_count];

      uint8_t* destination = &packet.buffer[payload_offset];
      for (uint32_t odt_entry_index = odt->first_odt_entry; odt_entry_index <= odt->last_odt_entry; ++odt_entry_index) {
        int32_t const offset = odt_entry_address_table[odt_entry_index];

        uint8_t const size = odt_entry_size_table[odt_entry_index];
        assert(size != 0);
        uint8_t const ext = odt_entry_extension_table[odt_entry_index];

        if (ext == McAddressExtensionAbsolute) {
          assert(app->base_address != NULL);
          uint8_t const* const absolute_address = app->base_address + offset;
          memcpy(destination, absolute_address, size);
        } else if (ext == McAddressExtensionEventRelative) {
          assert(measurement_segment != NULL);
          uint8_t const* shm_base = measurement_segment->shared_memory_pointer;
          assert(shm_base != NULL);
          uint8_t const* const shm_address = shm_base + offset;
          memcpy(destination, shm_address, size);
        } else if (ext == McAddressExtensionStackRelative) {
          assert(stack_pointer != NULL);
          uint8_t const* const stack_address = stack_pointer + offset;
          memcpy(destination, stack_address, size);
        } else {
          mc_log_error("Unsupported address extension %d\n", ext);
        }

        destination += size;
      }

      // TODO: (gjsh) Debug only. Remove this
      // Print packet contents before pushing
      mc_log_debug("Pushing ODT %u, packet contents (hex): \n", odt_index);
      for (int64_t j = 0; j < packet.size; ++j) {
        mc_log_debug("%02x ", packet.buffer[j]);
      }
      mc_log_debug("\n");

      mc_queue_push(queue, &packet);

      payload_offset = kOdtHeaderSize;
    }  // odt loop
  }
}

McMeasurementValueId mc_measurement_value_init_untyped(McAppId app_id, McEventId event_id, uint8_t const* value,
                                                       size_t value_bytes) {
  assert(state->global_shared_memory_file_descriptor != -1);
  Application* app = state->applications[app_id];
  assert(app != NULL);

  MeasurementBlock* block = app->measurement_blocks[event_id];
  assert(block != NULL);

  size_t const aligned_size = align_block_value_address(value_bytes);
  assert(aligned_size <= INT64_MAX);
  McMeasurementValueId const measurement_value_offset = (McMeasurementValueId)(atomic_fetch_add_explicit(
      &block->current_value_offset, (int64_t)aligned_size, memory_order_acq_rel));

  uint8_t* data = block->shared_memory_pointer + measurement_value_offset;
  if (value) {
    memcpy(data, value, value_bytes);
  } else {
    memset(data, 0, value_bytes);
  }
  return measurement_value_offset;
}

void mc_measurement_value_capture_untyped(McAppId app_id, McEventId event_id, McMeasurementValueId measurement_id,
                                          uint8_t const* value, size_t value_bytes) {
  assert(state->global_shared_memory_file_descriptor != -1);
  Application* app = state->applications[app_id];
  assert(app != NULL);

  MeasurementBlock* segment = app->measurement_blocks[event_id];
  assert(segment != NULL);

  uint8_t* data = segment->shared_memory_pointer + measurement_id;
  assert(value);
  memcpy(data, value, value_bytes);
}

// ============================================================================
// Calibration
// ============================================================================
McCalibrationBlockId mc_calibration_block_init(McAppId app_id, char const* calibration_block_name,
                                               size_t calibration_block_name_length, size_t calibration_block_size) {
  assert(state->global_shared_memory_file_descriptor != -1);
  Application* app = state->applications[app_id];
  assert(app != NULL);

  // Compute a UUID from the name and encode the app id in the first 8 high bits to avoid cross-app collisions.
  McUuid calseg_uuid = string_to_u128_fnv(calibration_block_name, calibration_block_name_length);
  calseg_uuid.high = (calseg_uuid.high & 0x00FFFFFFFFFFFFFFULL) | (((uint64_t)app_id) << 56);

  if (pthread_mutex_lock(&app->calibration_table_mutex) != 0) {
    mc_log_error("Failed to lock calibration table mutex\n");
    return MC_INVALID_ID;
  }
  McResult name_result = mc_entity_name_track_unique(app->assigned_calibration_block_names, calibration_block_name,
                                                     calibration_block_name_length, "Calibration block");
  if (pthread_mutex_unlock(&app->calibration_table_mutex) != 0) {
    mc_log_warn("Failed to unlock calibration table mutex\n");
  }
  if (name_result != MC_RESULT_SUCCESS) {
    mc_log_error("Failed to track unique calibration block name, error code: %d\n", name_result);
    return MC_INVALID_ID;
  }

  // Assign a globally unique calibration block ID using the global mapping table.
  assert(state->calibration_block_id_table != NULL);
  int lock = acquire_lock(kMcGlobalLockName);
  if (lock == -1) {
    mc_log_error("Failed to acquire global lock\n");
    return MC_INVALID_ID;
  }

  McCalibrationBlockId block_id =
      (McCalibrationBlockId)mc_idtable_find_or_insert((McIdTable*)state->calibration_block_id_table, calseg_uuid);
  if (release_lock(lock) != 0) {
    mc_log_error("Failed to release global lock\n");
    return MC_INVALID_ID;
  }

  if (block_id == MC_INVALID_ID) {
    mc_log_error("Failed to get event ID\n");
    return block_id;
  }

  CalibrationBlock** segment = &app->calibration_blocks[block_id];
  assert(*segment == NULL);
  *segment = malloc(sizeof(CalibrationBlock));

  mc_log_debug("mc_calibration_block_init: app_id=%d, block_name=%s, block_size=%zu\n", app_id, calibration_block_name,
               calibration_block_size);
  mc_log_debug("mc_calibration_block_init: app->shared_memory_pointer=%p, app->shared_memory_size=%zu\n",
               (void*)app->shared_memory_pointer, app->shared_memory_size);
  mc_log_debug("mc_calibration_block_init: current bump_allocator_head=%lu\n",
               (unsigned long)atomic_load_explicit(app->bump_allocator_head, memory_order_relaxed));
  mc_log_debug(
      "mc_calibration_block_init: kMcAppEventTriggersOffset=%zu, kMcAppEventTriggersSize=%zu, "
      "kMcAppDynamicBlocksOffset=%zu\n",
      kMcAppEventTriggersOffset, kMcAppEventTriggersSize, kMcAppDynamicBlocksOffset);

  uint64_t block_offset_header = allocate_block(app, sizeof(McCalibrationBlockHeader));
  assert((block_offset_header % kMcBlockAlignment) == 0);  // blocks must be aligned for pointer casting external types.
  uint8_t* shared_memory_pointer_header = app->shared_memory_pointer + block_offset_header;

  uint64_t block_offset_working = allocate_block(app, calibration_block_size);
  assert((block_offset_working % kMcBlockAlignment) ==
         0);  // blocks must be aligned for pointer casting external types.
  uint8_t* shared_memory_pointer_working = app->shared_memory_pointer + block_offset_working;

  uint64_t block_offset_initial = allocate_block(app, calibration_block_size);
  assert((block_offset_initial % kMcBlockAlignment) ==
         0);  // blocks must be aligned for pointer casting external types.
  uint8_t* shared_memory_pointer_initial = app->shared_memory_pointer + block_offset_initial;

  CalibrationBlock new_segment = {
      .shared_memory_pointer_header = (McCalibrationBlockHeader*)shared_memory_pointer_header,
      .shared_memory_pointer_working = shared_memory_pointer_working,
      .shared_memory_pointer_initial = shared_memory_pointer_initial,
      .shared_memory_size = calibration_block_size,
      .current_value_offset = 0,
  };

  // Initialize header content.
  new_segment.shared_memory_pointer_header->active_ecu_page_id = MC_CALIBRATION_PAGE_WORKING;
  new_segment.shared_memory_pointer_header->working_page_offset =
      (uint64_t)(shared_memory_pointer_working - shared_memory_pointer_header);
  new_segment.shared_memory_pointer_header->initial_page_offset =
      (uint64_t)(shared_memory_pointer_initial - shared_memory_pointer_header);
  new_segment.shared_memory_pointer_header->modification_counter = 0;

  pthread_mutexattr_t mutex_attribute = {0};  // NOLINT(missing-includes) // do **not** include internal "bits"
                                              // headers directly.
  if (pthread_mutexattr_init(&mutex_attribute) != 0) {
    mc_log_error("pthread_mutexattr_init failed (errno=%d)\n", errno);
    return MC_INVALID_ID;
  }

  if (pthread_mutexattr_setpshared(&mutex_attribute, PTHREAD_PROCESS_SHARED) != 0) {
    mc_log_error("pthread_mutexattr_setpshared failed (errno=%d)\n", errno);
    return MC_INVALID_ID;
  }

  if (pthread_mutex_init(&new_segment.shared_memory_pointer_header->lock, &mutex_attribute) != 0) {
    mc_log_error("pthread_mutex_init failed (errno=%d)\n", errno);
    return MC_INVALID_ID;
  }

  **segment = new_segment;

  mc_log_debug("mc_calibration_block_init: Initialized calibration block '%s' id=%d in app %d\n",
               calibration_block_name, block_id, app_id);
  mc_log_debug("mc_calibration_block_init: header_offset=%lu, working_offset=%lu, initial_offset=%lu\n",
               (unsigned long)block_offset_header, (unsigned long)block_offset_working,
               (unsigned long)block_offset_initial);
  mc_log_debug("mc_calibration_block_init: header_ptr=%p, working_ptr=%p, initial_ptr=%p\n",
               (void*)shared_memory_pointer_header, (void*)shared_memory_pointer_working,
               (void*)shared_memory_pointer_initial);
  mc_log_debug("mc_calibration_block_init: final bump_allocator_head=%lu (shm ends at %lu)\n",
               (unsigned long)atomic_load_explicit(app->bump_allocator_head, memory_order_relaxed),
               (unsigned long)kMcAppSharedMemorySize);
  return block_id;
}

uint64_t mc_calibration_block_get_offset(McAppId app_id, McCalibrationBlockId block_id) {
  assert(state->global_shared_memory_file_descriptor != -1);
  Application* app = state->applications[app_id];
  assert(app != NULL);
  CalibrationBlock* block = app->calibration_blocks[block_id];
  assert(block != NULL);

  assert((uint8_t*)block->shared_memory_pointer_header >= app->shared_memory_pointer);
  return (uint64_t)((uint8_t*)block->shared_memory_pointer_header - app->shared_memory_pointer);
}

uint64_t mc_calibration_block_get_size(McAppId app_id, McCalibrationBlockId block_id) {
  assert(state->global_shared_memory_file_descriptor != -1);
  Application* app = state->applications[app_id];
  assert(app != NULL);
  CalibrationBlock* block = app->calibration_blocks[block_id];
  assert(block != NULL);
  return (uint64_t)block->shared_memory_size;
}

uint64_t mc_calibration_block_get_modification_counter(McAppId app_id, McCalibrationBlockId block_id) {
  assert(state->global_shared_memory_file_descriptor != -1);
  Application* app = state->applications[app_id];
  assert(app != NULL);
  CalibrationBlock* block = app->calibration_blocks[block_id];
  assert(block != NULL);
  return block->shared_memory_pointer_header->modification_counter;
}

McCalibrationValueId mc_calibration_value_init_untyped(McAppId app_id, McCalibrationBlockId calibration_block_id,
                                                       uint8_t const* initial_value, size_t value_bytes) {
  assert(state->global_shared_memory_file_descriptor != -1);
  Application* app = state->applications[app_id];
  assert(app != NULL);

  CalibrationBlock* block = app->calibration_blocks[calibration_block_id];
  assert(block != NULL);

  size_t const aligned_size = align_block_value_address(value_bytes);
  assert(aligned_size <= INT64_MAX);
  McCalibrationValueId const calibration_value_offset = (McCalibrationValueId)(atomic_fetch_add_explicit(
      &block->current_value_offset, (int64_t)aligned_size, memory_order_acq_rel));

  uint8_t* data = block->shared_memory_pointer_working + calibration_value_offset;
  uint8_t* data_initial = block->shared_memory_pointer_initial + calibration_value_offset;
  if (initial_value) {
    memcpy(data, initial_value, value_bytes);
    memcpy(data_initial, initial_value, value_bytes);
  } else {
    memset(data, 0, value_bytes);
    memset(data_initial, 0, value_bytes);
  }

  return calibration_value_offset;
}

McBlockReadLock mc_calibration_block_read_begin(McAppId app_id, McCalibrationBlockId calibration_block_id) {
  assert(state->global_shared_memory_file_descriptor != -1);
  Application* app = state->applications[app_id];
  assert(app != NULL);

  CalibrationBlock* segment = app->calibration_blocks[calibration_block_id];
  assert(segment != NULL);

  if (pthread_mutex_lock(&segment->shared_memory_pointer_header->lock) != 0) {
    mc_log_error("Failed to lock calibration block mutex in read_begin\n");
    return (McBlockReadLock){0, 0};
  }
  uint8_t const* data = NULL;
  switch (segment->shared_memory_pointer_header->active_ecu_page_id) {
    case MC_CALIBRATION_PAGE_INITIAL:
      data = segment->shared_memory_pointer_initial;
      break;

    case MC_CALIBRATION_PAGE_WORKING:
      data = segment->shared_memory_pointer_working;
      break;

    default:
      // unreachable. Page switch outside of range is not allowed.
      assert((segment->shared_memory_pointer_header->active_ecu_page_id == MC_CALIBRATION_PAGE_INITIAL) ||
             (segment->shared_memory_pointer_header->active_ecu_page_id == MC_CALIBRATION_PAGE_WORKING));
      break;
  }

  BlockReadLockUnion lock = {
      .impl =
          {
              .segment_base_pointer = data,
              .lock = &segment->shared_memory_pointer_header->lock,
          },
  };

  return lock.data;
}

void mc_calibration_block_read_end(McBlockReadLock calibration_segment_lock) {
  BlockReadLockImpl const* lock_impl = (BlockReadLockImpl*)&calibration_segment_lock;
  pthread_mutex_unlock(lock_impl->lock);
}

uint8_t const* mc_calibration_value_read_untyped(McBlockReadLock calibration_segment_lock,
                                                 McCalibrationValueId value_id) {
  BlockReadLockImpl const* lock_impl = (BlockReadLockImpl*)&calibration_segment_lock;
  uint8_t const* data = lock_impl->segment_base_pointer + value_id;
  return data;
}

McBlockWriteLock mc_calibration_block_write_begin(McAppId app_id, McCalibrationBlockId calibration_block_id) {
  assert(state->global_shared_memory_file_descriptor != -1);
  Application* app = state->applications[app_id];
  assert(app != NULL);

  CalibrationBlock* segment = app->calibration_blocks[calibration_block_id];
  assert(segment != NULL);

  if (pthread_mutex_lock(&segment->shared_memory_pointer_header->lock) != 0) {
    mc_log_error("Failed to lock calibration block mutex in write_begin\n");
    return (McBlockWriteLock){0};
  }
  uint8_t* data = NULL;
  switch (segment->shared_memory_pointer_header->active_ecu_page_id) {
    case MC_CALIBRATION_PAGE_INITIAL:
      data = segment->shared_memory_pointer_initial;
      break;

    case MC_CALIBRATION_PAGE_WORKING:
      data = segment->shared_memory_pointer_working;
      break;

    default:
      // unreachable. Page switch outside of range is not allowed.
      assert((segment->shared_memory_pointer_header->active_ecu_page_id == MC_CALIBRATION_PAGE_INITIAL) ||
             (segment->shared_memory_pointer_header->active_ecu_page_id == MC_CALIBRATION_PAGE_WORKING));
      break;
  }

  segment->shared_memory_pointer_header->modification_counter++;

  BlockWriteLockUnion lock = {
      .impl =
          {
              .segment_base_pointer = data,
              .lock = &segment->shared_memory_pointer_header->lock,
          },
  };
  return lock.data;
}

void mc_calibration_block_write_end(McBlockWriteLock calibration_segment_lock) {
  BlockWriteLockImpl const* lock_impl = (BlockWriteLockImpl*)&calibration_segment_lock;
  pthread_mutex_unlock(lock_impl->lock);
}

uint8_t* mc_calibration_value_write_untyped(McBlockWriteLock calibration_segment_lock, McCalibrationValueId value_id) {
  BlockWriteLockImpl const* lock_impl = (BlockWriteLockImpl*)&calibration_segment_lock;
  uint8_t* data = lock_impl->segment_base_pointer + value_id;
  return data;
}

McResult mc_calibration_page_switch(McAppId app_id, McCalibrationBlockId calibration_block_id,
                                    McCalibrationPageId page_id) {
  assert(state->global_shared_memory_file_descriptor != -1);
  Application* app = state->applications[app_id];
  assert(app != NULL);

  CalibrationBlock* segment = app->calibration_blocks[calibration_block_id];
  assert(segment != NULL);

  if (pthread_mutex_lock(&segment->shared_memory_pointer_header->lock) != 0) {
    mc_log_error("Failed to lock calibration block mutex in page_switch\n");
    return MC_RESULT_ERROR;
  }
  switch (page_id) {
    case MC_CALIBRATION_PAGE_INITIAL:
    case MC_CALIBRATION_PAGE_WORKING:
      segment->shared_memory_pointer_header->active_ecu_page_id = page_id;
      break;

    default:
      mc_log_error("calibration page ID %d is not supported. Current page %d remains active\n", page_id,
                   segment->shared_memory_pointer_header->active_ecu_page_id);
      break;
  }
  if (pthread_mutex_unlock(&segment->shared_memory_pointer_header->lock) != 0) {
    mc_log_warn("Failed to unlock calibration block mutex in page_switch\n");
  }
  return MC_RESULT_SUCCESS;
}

McCalibrationPageId mc_get_calibration_page(McAppId app_id, McCalibrationBlockId calibration_block_id) {
  assert(state->global_shared_memory_file_descriptor != -1);
  Application* app = state->applications[app_id];
  assert(app != NULL);
  CalibrationBlock* segment = app->calibration_blocks[calibration_block_id];
  assert(segment != NULL);
  return segment->shared_memory_pointer_header->active_ecu_page_id;
}

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

static void spinlock_lock(atomic_int_fast64_t* lock) {
  int64_t expected = 0;
  int64_t const desired = 1;
  while (!atomic_compare_exchange_weak_explicit(lock, &expected, desired, memory_order_acquire, memory_order_relaxed)) {
    expected = 0;
  }

  // A call to your target specific spinlock hint should be placed here.
  // For x86, _mm_pause from #include <immintrin.h> can be used. This reference
  // does not provide an abstraction.
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
    mc_log_error("queue buffer size must be least %lli bytes, got %lli bytes\n", (long long)kMinimumBufferSize,
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

  mc_log_debug("mc_queue_init_from_memory: queue_buffer=%p, BEFORE setting buffer_size=%lld\n", queue_buffer,
               (long long)queue->header.buffer_size);

  assert(queue_buffer_size - sizeof(QueueHeader) < INT64_MAX);
  queue->header.buffer_size = (int64_t)(queue_buffer_size - sizeof(QueueHeader));

  mc_log_debug(
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

  mc_log_debug(
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
  atomic_store_explicit(&header->is_ready, 1, memory_order_relaxed);
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
  if (!atomic_load_explicit(&node_header->is_ready, memory_order_relaxed)) {
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

uint8_t const* mc_get_app_base_address(McAppId app_id) {
  assert(state->global_shared_memory_file_descriptor != -1);
  Application* app = state->applications[app_id];
  assert(app != NULL);
  return app->base_address;
}

//=================================
// Hashing
//=================================
static inline void sha256(uint8_t const* data, size_t len, uint32_t hash[8]) {
#define ROTR(n, x) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIGMA0(x) (ROTR(2, x) ^ ROTR(13, x) ^ ROTR(22, x))
#define SIGMA1(x) (ROTR(6, x) ^ ROTR(11, x) ^ ROTR(25, x))
#define sigma0(x) (ROTR(7, x) ^ ROTR(18, x) ^ ((x) >> 3))
#define sigma1(x) (ROTR(17, x) ^ ROTR(19, x) ^ ((x) >> 10))

  // clang-format off
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
        0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
        0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
        0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
        0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
  // clang-format on

  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

  size_t padded_len = ((len + 8) / 64 + 1) * 64;
  uint8_t* padded = (uint8_t*)calloc(padded_len, 1);

  if (!padded) {
    // Fallback - copy initial values
    for (int i = 0; i < 8; i++) hash[i] = h[i];
    return;
  }

  memcpy(padded, data, len);
  padded[len] = 0x80;

  // Add length in bits as  64-bit integer at the end
  uint64_t bit_len = len * 8;
  for (int i = 7; i >= 0; i--) {
    padded[padded_len - 8U + (size_t)i] = (bit_len >> (8U * (7U - (size_t)i))) & 0xFFU;
  }

  // Process each 512-bit chunk
  for (size_t chunk = 0U; chunk < padded_len; chunk += 64U) {
    uint32_t w[64];

    // Copy chunk to first 16 words of w array
    for (uint32_t i = 0U; i < 16U; i++) {
      w[i] = (uint32_t)(padded[chunk + i * 4U] << 24U) | (uint32_t)(padded[chunk + i * 4U + 1U] << 16U) |
             (uint32_t)(padded[chunk + i * 4U + 2U] << 8U) | (uint32_t)(padded[chunk + i * 4U + 3U]);
    }

    // Extend the first 16 words into the remaining 48 words
    for (uint32_t i = 16U; i < 64U; i++) {
      w[i] = sigma1(w[i - 2U]) + w[i - 7U] + sigma0(w[i - 15U]) + w[i - 16U];
    }

    // Initialize working variables
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], h_var = h[7];

    // Compression function main loop
    for (uint32_t i = 0U; i < 64U; i++) {
      uint32_t t1 = h_var + SIGMA1(e) + CH(e, f, g) + k[i] + w[i];
      uint32_t t2 = SIGMA0(a) + MAJ(a, b, c);
      h_var = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }

    // Add compressed chunk to current hash value
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += h_var;
  }

  // Copy result
  for (uint32_t i = 0; i < 8U; i++) {
    hash[i] = h[i];
  }

  free(padded);
}

// Function to get the compilation UUID
McUuid mc_get_application_uuid(const char* app_name, uint8_t global_unique_identifier, const char* date_time_version) {
  static McUuid cached_uuid = {0U, 0U};
  static int initialized = 0;

  if (!initialized) {
    // Create input string for hashing
    char input_buffer[512];
    int len = snprintf(input_buffer, sizeof(input_buffer), "%s%s%u%zu", date_time_version, app_name,
                       global_unique_identifier, sizeof(McUuid));

    if (len < 0 || len >= (int)sizeof(input_buffer)) {
      len = sizeof(input_buffer) - 1;
    }

    // Compute SHA-256 hash
    uint32_t hash[8];
    sha256((uint8_t const*)input_buffer, (size_t)len, hash);

    // Convert first 128 bits (4 uint32_t values) to McUuid format
    McUuid uuid;
    uuid.high = ((uint64_t)hash[0] << 32) | hash[1];
    uuid.low = ((uint64_t)hash[2] << 32) | hash[3];
    initialized = 1;
    cached_uuid = uuid;
  }

  return cached_uuid;
}
