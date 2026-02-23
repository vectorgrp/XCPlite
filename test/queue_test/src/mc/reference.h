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

#define MC_DRIVER_MAJOR_VERSION 0
#define MC_DRIVER_MINOR_VERSION 2
#define MC_DRIVER_PATCH_VERSION 0

// ============================================================================
// Types
// ============================================================================
typedef uint8_t McVersionMajor;
typedef uint8_t McVersionMinor;
typedef uint8_t McVersionPatch;

typedef struct McDriverVersion {
  McVersionMajor major_version;
  McVersionMinor minor_version;
  McVersionPatch patch_version;
} McDriverVersion;

/// Result code for operations that can fail.
typedef enum {
  MC_RESULT_SUCCESS = 0,  // Operation completed successfully.
  MC_RESULT_ERROR = 1     // Generic error.
} McResult;

/// Driver opaque handle that can be used to share a driver instance across multiple compilation units or shared
/// objects.
typedef struct State* McDriverHandle;

/// Application Identifier. Unique per Daemon instance.
typedef uint8_t McAppId;

/// Event Identifier. Unique per application.
typedef uint8_t McEventId;

/// Measurement value identifier. Unique per event.
typedef int16_t McMeasurementValueId;

/// Calibration block Identifier. Unique globally across all applications.
typedef uint8_t McCalibrationBlockId;

/// Calibration value identifier. Unique per calibration block. A calibration value always resides in a calibration
/// block.
typedef uint16_t McCalibrationValueId;

/// Calibration page identifier. Unique per calibration page per block.
typedef uint8_t McCalibrationPageId;

/// Model checksum.
typedef int64_t McModelChecksum;

/// Block read lock handle.
typedef struct {
  uint64_t data0;
  uint64_t data1;
} McBlockReadLock;

/// Block write lock handle.
typedef struct {
  uint8_t data[16];  // opaque data to the user
} McBlockWriteLock;

/// Calibration block header with meta data for page switching and synchronization.
typedef struct {
  // Working (RAM) page offset relative to the block base pointer in the application shared memory.
  uint64_t working_page_offset;
  // Initial page offset relative to the block base pointer in the application shared memory.
  uint64_t initial_page_offset;
  // Active page from the ECU perspective. A calibration driver may access a different page in the background.
  McCalibrationPageId active_ecu_page_id;

  // Modification counter increments after each modification for optimization purposes.
  // Readers can operate on a local copy of the data instead of locking the mutex on unchanged data.
  uint64_t modification_counter;

  // Locks the calibration value blocks for sporadic write access from the Daemon.
  pthread_mutex_t lock;  // NOLINT(missing-includes) // do **not** include internal "bits" headers directly.
} McCalibrationBlockHeader;

typedef struct {
  uint64_t high;
  uint64_t low;
} McUuid;

typedef struct {
  pthread_mutex_t mutex;
  McUuid uuid;
} McSharedIdTableEntry;

/// Mapping table between unique 128bit UUIDs and 8bit identifiers
typedef struct {
  McSharedIdTableEntry entries[UINT8_MAX];
} McSharedIdTable;

typedef struct {
  McUuid entries[UINT8_MAX];
} McIdTable;

/// Mappings between 128bit UUIDs and 8bit McAppIds for applications
typedef McSharedIdTable McAppIdTable;

/// Mappings between 128bit UUIDs and 8bit McEventId for events
typedef McIdTable McEventIdTable;

/// Mappings between 128bit UUIDs and 8bit McCalibrationBlockId for calibration blocks
typedef McIdTable McCalibrationBlockIdTable;

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

typedef uint16_t EventChannel;  // encodes 8bit App ID + 8bit Event ID

// ODT (a set of packet parts similar to POSIX msghdr)
// size = 8 byte
#pragma pack(push, 1)
typedef struct {
  uint16_t first_odt_entry;  // Absolute odt entry number
  uint16_t last_odt_entry;   // Absolute odt entry number
  uint16_t size;             // Number of bytes
  uint16_t reserved;         // Reserved
} XcpOdt;
#pragma pack(pop)

// DAQ list
// size = 12 byte
#pragma pack(push, 1)
typedef struct {
  uint16_t last_odt;           // Absolute odt number
  uint16_t first_odt;          // Absolute odt number
  EventChannel event_channel;  // Associated event
  uint16_t reserved;           // Reserved
  uint8_t mode;
  uint8_t state;
  uint8_t priority;
  uint8_t address_extension;
  int32_t app_pid;
} XcpDaqList;
#pragma pack(pop)

// Amount of memory for DAQ tables, each ODT entry (e.g. measurement variable or memory block) needs 5 bytes
// NOTE: (gjsh) IMPORTANT: Must match OPTION_DAQ_MEM_SIZE in daemon's main_cfg.h
#define XCP_DAQ_MEM_SIZE (6000 * 5)

// Dynamic DAQ list structure in a linear memory block with size
// XCP_DAQ_MEM_SIZE + 8
// TODO: (gjsh) The XcpDaqLists structure is shared between xcplite and this implementation and should be refactored
// into a shared header We have already had some tricky bugs due to slight mismatches here.
#pragma pack(push, 1)
typedef struct {
  uint16_t odt_entry_count;  // Total number of ODT entries in ODT entry addr
                             // and size arrays
  uint16_t odt_count;        // Total number of ODTs in ODT array
  uint16_t daq_count;        // Number of DAQ lists in DAQ list array
  uint16_t reserved;         // Reserved

  union {
    XcpDaqList daq_list[XCP_DAQ_MEM_SIZE / sizeof(XcpDaqList)];
    XcpOdt odt[XCP_DAQ_MEM_SIZE / sizeof(XcpOdt)];
    uint32_t odt_entry_addr[XCP_DAQ_MEM_SIZE / 4];
    uint8_t odt_entry_size[XCP_DAQ_MEM_SIZE];
    uint8_t odt_entry_addr_ext[XCP_DAQ_MEM_SIZE];
    // NOTE: (gjsh) Added to exactly match tXcpDaqLists in xcpLite.h to avoid overlapping issues
    uint64_t b[XCP_DAQ_MEM_SIZE / 8 + 1];
  };
} XcpDaqLists;
#pragma pack(pop)

// ============================================================================
// Constants
// ============================================================================

// value for invalid MC_ID which is 0.
#define MC_INVALID_ID 0
// Maximum number of applications that can be registered is limited to 127.
// For calibration segments and measurement instances, the A2L addresses encode a 7-bit app id.
// The 8th bit is used to distinguish calibration segments from measurements.
#define MC_MAX_APP_ID 127

// Only 4k page granularity is supported (optional huge page optimizations may be possible in future)
static size_t const kMcPageSize = 4096;
// Maximum queue buffer must fit maximum XCP packet size (XCP_MAX_DTO_SIZE+XCPTL_TRANSPORT_LAYER_HEADER_SIZE).
static size_t const kMcQueueMaxBufferPayloadSize = 1024UL * 8UL;
static size_t const kMcQueueBufferAlignment = 64;  // Aligned to 64 byte x86_64 cache line size.
static size_t const kMcBlockAlignment = 64;        // Aligned to 64 byte x86_64 cache line size.
static size_t const kMcBlockValueAlignment = 8;    // Assuming native pointer alignment of 8 byte.

// Environment variable configuration values.
static char const* const kMcShmNameEnvVar = "VES_MC_GLOBAL_SHM_NAME";

static uint8_t const kDaqStateStoppedUnselected = 0x00; /* Not selected, stopped */
static uint8_t const kDaqStateSelected = 0x01;          /* Selected */
static uint8_t const kDaqStateRunning = 0x02;           /* Running */
static uint8_t const kDaqStateOverrun = 0x04;           /* Overrun */

// DAQ List to trigger arg conversion structures
// Arbitrary limit for maximum number of trigger args per event.
#define kMcMaxTriggerArgsPerEvent 32

// Each DAQ Odt entry is mapped to a TriggerArg by the daemon. Each trigger arg describes how to copy a measurement
// value or parts of a measurement value into an odt buffer
#pragma pack(push, 1)
struct TriggerArg {
  uint8_t argument_index;    // Mapped to the reference passed to the trigger function.
  uint8_t daq_state;         // The state of the daq list this trigger arg belongs to
  uint8_t dto_pid;           // PID in DTO
  uint8_t reserved;          // Reserved for alignment
  uint16_t dto_packet_size;  // Size of the packet this will be written to. Stored here for efficiency/convenience
                             // during DAQ list processing.
  uint16_t dto_daq;          // DAQ identifier in DTO
  uint32_t size;             // Number of bytes to copy
  uint32_t sub_measurement_offset;  // Only relevant for arrays/structs. Offset within the measurement value
};
#pragma pack(pop)

#pragma pack(push, 1)
struct EventTrigger {
  uint32_t pid;  // Process ID of the application that registered this event.
  uint8_t active_arg_count;
  uint8_t reserved;
  uint16_t reserved2;
  struct TriggerArg active_args[kMcMaxTriggerArgsPerEvent];
};
#pragma pack(pop)

typedef struct EventTrigger EventTriggers[UINT8_MAX];

// Header for meta data. Currently: unused
// Guard pages may be added between blocks in future.
static size_t const kMcGlobalHeaderOffset = 0;
static size_t const kMcGlobalHeaderSize = 0;
static size_t const kMcMeasurementListOffset = kMcGlobalHeaderSize;
static size_t const kMcDaqListsSize = sizeof(XcpDaqLists);
// EventTriggers moved to app local shared memory
static size_t const kMcMeasurementQueueOffset = kMcMeasurementListOffset + kMcDaqListsSize;
static size_t const kMcMeasurementQueueSize = 2048 * kMcPageSize;
static size_t const kMcCommandQueueOffset = kMcMeasurementQueueOffset + kMcMeasurementQueueSize;
static size_t const kMcCommandQueueSize = 32 * kMcPageSize;
static size_t const kMcServiceDiscoveryQueueOffset = kMcCommandQueueOffset + kMcCommandQueueSize;
static size_t const kMcServiceDiscoveryQueueSize = 64 * kMcPageSize;
static size_t const kMcAppIdMappingTableOffset = kMcServiceDiscoveryQueueOffset + kMcServiceDiscoveryQueueSize;
static size_t const kMcAppIdMappingTableSize = sizeof(McIdTable);
static size_t const kMcCalibrationIdMappingTableOffset = kMcAppIdMappingTableOffset + kMcAppIdMappingTableSize;
static size_t const kMcCalibrationIdMappingTableSize = sizeof(McCalibrationBlockIdTable);
static size_t const kMcGlobalSharedMemorySize =
    (216 * kMcPageSize)  // FIXME(pwr): why were hardcoded 216 pages added?
    + kMcGlobalHeaderSize + kMcMeasurementListOffset + kMcMeasurementQueueOffset + kMcCommandQueueOffset +
    kMcServiceDiscoveryQueueOffset + kMcAppIdMappingTableSize + kMcCalibrationIdMappingTableSize;

// Application specific shared memory layout
static char const* const kMcAppSharedMemoryPathSuffix = ".shm";
static size_t const kMcAppSharedMemorySuffixLength = 5;
// Layout: [Bump Allocator (8 bytes)] [EventTriggers (~461KB)] [Dynamic Blocks (bump-allocated)]
static size_t const kMcAppBumpAllocatorOffset = 0;
static size_t const kMcAppBumpAllocatorSize = 8;  // sizeof(atomic_uint_fast64_t)
static size_t const kMcAppEventTriggersOffset = kMcAppBumpAllocatorOffset + kMcAppBumpAllocatorSize;  // 8
static size_t const kMcAppEventTriggersSize = sizeof(EventTriggers);  // ~461KB (255 * ~1808 bytes)
// Measurement and calibration blocks start after EventTriggers, aligned to kMcBlockAlignment (64 bytes)
static size_t const kMcAppDynamicBlocksOffset =
    ((kMcAppEventTriggersOffset + kMcAppEventTriggersSize + (kMcBlockAlignment - 1)) & ~(kMcBlockAlignment - 1));
static size_t const kMcStaticShmSize = kMcAppDynamicBlocksOffset;
static size_t const kMcSpaceReservedForDynBlocks = (120 * kMcPageSize);
static size_t const kMcAppSharedMemorySize = kMcStaticShmSize + kMcSpaceReservedForDynBlocks;

// Application UUID lookup layout
static char const* const kMcAppUuidLookupPathSuffix = "_id.shm";
static size_t const kMcAppUuidLookupSuffixLength = 8;
static size_t const kMcUuidOffset = 0;
static size_t const kMcAppEventIdTableOffset = sizeof(McUuid);
static size_t const kMcAppEventIdTableSize = sizeof(McEventIdTable);
static size_t const kMcAppUuidLookupSize = kMcAppEventIdTableOffset + kMcAppEventIdTableSize;
static uint8_t const McAddressExtensionAbsolute =
    0x01;  // Absolute addressing mode, address is a 32 Bit pointer offset from base address
static uint8_t const McAddressExtensionEventRelative =
    0x03;  // Address is relative to the start of the associated event (measurement block) in shm
static uint8_t const McAddressExtensionStackRelative =
    0x04;  // Address is relative to the stack pointer of the triggering function

// Global shared memory layout
static char const* const kMcGlobalSharedMemoryName = "/global_shm.shm";
// Global file lock for shared memory initialization and reservation of app id or segment ids
static char const* const kMcGlobalLockName = "/tmp/ves_mc_global_lock";
static size_t const kMcMaxPathLength = 512;
static size_t const kMcMaxAppNameLength =
    kMcMaxPathLength - kMcAppUuidLookupSuffixLength - 2 /* One character for '/' and one for null terminator */;

// Builtin Page IDs.
// NOTE(pwr): these numbers are currently hardcoded in the MC daemon A2L generation and equal the CANape default values.
// NOTE(pwr): A2L allows overwriting the working page using "DEFAULT_PAGE_NUMBER" which is not supported here.
// RAM
#define MC_CALIBRATION_PAGE_WORKING 0
// FLASH
#define MC_CALIBRATION_PAGE_INITIAL 1

// NOTE(pwr): value for invalid references may change in future.
#define MC_INVALID_REF 0xff

// Empty strings have a reserved identifier to not replicate the value several times.
#define MC_EMPTY_SHARED_STRING 0

// TODO(pwr): remove if no longer needed.
#define MC_MAX_STRLEN 256

// ============================================================================
// Utilities.
// ============================================================================
/// Check if an identifier string fulfills the constraints for identifiers.
//  Identifiers are strings with the following constraints:
//  * Maximum of 256 characters.
//  * Valid characters:
//    * A-Z, a-z, 0-9, _, .
//    * First character must be a letter or an underscore.
/// @param identifier An identifier to check.
/// @param identifier_length Identifier length in bytes.
/// @return true if the string fulfills all identifier contstraints.
bool mc_check_identifier(char const* identifier, size_t identifier_length);

/// Get the current active driver handle to be shared with shared objects or other translation units that might link
/// against a separate driver that should share the same state.
/// @param out_driver_version Optional out parameter for the linked driver version. Null pointer if not required.
/// @return Active driver handle.
McDriverHandle mc_get_driver_handle(McDriverVersion* out_driver_version);

/// Set the current driver handle. Does not set the driver handle if there is a semantic version mismatch.
/// @param handle Driver handle.
/// @return True if the driver handle is compatible with the linked driver implementation.
bool mc_set_driver_handle(McDriverHandle handle);

// ============================================================================
// Applications
// ============================================================================
/// Initialize a new application instance.
/// @precondition The application name is globally unique per Daemon instance.
/// Initialize an application and allocate its measurement and calibration resources.
/// @precondition Not all possible 8bit application IDs are already assigned.
/// @param name Globally unique application name. Does **not** require a null terminator.
/// @param name_length Name length in bytes.
/// @param binary_uuid Precomputed binary UUID
/// @param out_app_id Output parameter for the assigned globally unique application ID.
/// @param app_identifier System-wide unique app identifier between 1 and 254 or MC_INVALID_APP_ID to have an ID
/// assigned automatically
/// @return McResult indicating success or error code.
McResult mc_application_init(char const* name, size_t name_length, McUuid binary_uuid, McAppId* out_app_id,
                             uint8_t app_identifier);

// TODO(api): This is just here to avoid duplicate code and not meant to be customer API
void init_global_shm(void* pMemory);
int acquire_lock(char const* name);
int release_lock(int file_descriptor);

/// Deinitialized an application and all its calibration and measurement data.
/// @param app_id Application identifier.
/// @return McResult indicating success or error code.
McResult mc_application_deinit(McAppId app_id);

/// Enqueue service discovery message.
/// @precondition The application is initialized.
/// @param app_id Application identifier.
/// @param message Service discovery message buffer.
/// @param message_size Service discovery message buffer size in bytes.
bool mc_application_enqueue_service_discovery_message(McAppId app_id, uint8_t const* message, size_t message_size);

/// Acquire a service discovery message buffer to be pushed into the service discovery queue.
/// The buffer can be written by the user but will not be popped until it is committed.
/// Use `mc_application_push_service_discovery_message` to commit the message to the queue.
/// @param app_id Application identifier.
/// @param message_size Requested message size.
/// @return Queue buffer.
McQueueBuffer mc_application_acquire_service_discovery_message(McAppId app_id, size_t message_size);

/// Push buffer to indicate that the data is written and can be popped from the queue.
/// @param handle Queue handle.
/// @param queue_buffer Queue buffer.
/// @return Queue buffer.
void mc_application_push_service_discovery_message(McQueueBuffer const* buffer);

/// Send message to offboard sink (e.g. CANape displays the message in the write window).
/// @precondition The application is initialized.
/// @param app_id Application identifier.
/// @param message Service discovery message buffer.
void mc_application_write_message(McAppId app_id, char const* message);

/// Notify a model change to the offboard sink (e.g. CANape).
/// @param app_id Application identifier.
/// @precondition The application is initialized.
void mc_application_notify_model_change(McAppId app_id, McModelChecksum model_checksum);

/// Get pointer to the EventTriggers structure in application local shared memory.
/// @param app_id Application identifier.
/// @return Pointer to EventTriggers in application shared memory, or NULL if not initialized.
EventTriggers* mc_get_event_triggers(McAppId app_id);

/// Get the measurement queue handle.
/// @return Measurement queue handle, or NULL if not initialized.
McQueueHandle mc_get_measurement_queue(void);

// ============================================================================
// Measurement
// ============================================================================
/// Create a measurement event block.
/// @precondition Referenced application is initialized.
/// @precondition Event name is unique for the referenced application.
/// @precondition The total size of measurement event blocks does not exceed the shared memory capacity.
/// @precondition The total size of measurement event blocks does not exceed the maximum capacity of the underlying
/// measurement list format.
///               The upper limit is 4Gb in case of XCP DAQ lists.
/// @param app_id Application identifier.
/// @param event_name Event name. Does **not** require a null terminator.
/// @param event_name_length Event name length in bytes.
/// @param maximum_measurement_buffer_size Maximum measurement block buffer size associated with the event.
/// @return Assigned event ID unique for the application.
McEventId mc_measurement_event_init(McAppId app_id, char const* const event_name, size_t event_name_length,
                                    size_t maximum_measurement_buffer_size);

/// Trigger a measurement event for an individual memory base address.
/// The timestamp is taken as soon as possible when calling this function.
/// @precondition Event identifier must reside in the same stack frame or memory block as the associated measurement
/// values.
/// @precondition Referenced application and event are initialized.
/// @precondition Event identifier is associated to the application identifier.
/// @param app_id Application identifier.
/// @param event_id Event identifier pointer.
/// @param stack_pointer Current stack pointer of the triggering function. Only necessary if the event measures a stack
/// variable. The variable must be in the same stack frame.
void mc_measurement_event_trigger(McAppId app_id, McEventId event_id, uint8_t const* stack_pointer);

/// Create a measurement value in the measurement event block filled with an initial value.
/// @precondition Measurement identifier is associated to the event identifier.
/// @precondition Event identifier is associated to the application identifier.
/// @param app_id Application identifier.
/// @param event_id Event identifier.
/// @param measurement_id Measurement value identifier.
/// @param value Value pointer to be copied into the measurement block. A null pointer writes zeroes instead.
/// @param value_bytes Value size pointer to by the value pointer.
/// @return Assigned measurement value identifier that is unique within the event measurement block.
McMeasurementValueId mc_measurement_value_init_untyped(McAppId app_id, McEventId event_id, uint8_t const* value,
                                                       size_t value_bytes);

/// Capture a measurement value in the measurement event block.
/// @precondition Measurement identifier is associated to the event identifier.
/// @precondition Event identifier is associated to the application identifier.
/// @param app_id Application identifier in which the block resides.
/// @param event_id Event identifier.
/// @param measurement_id Measurement value identifier.
/// @param value Value pointer to be copied into the measurement value.
/// @param value_bytes Value size pointer to by the value pointer.
void mc_measurement_value_capture_untyped(McAppId app_id, McEventId event_id, McMeasurementValueId measurement_id,
                                          uint8_t const* value, size_t value_bytes);

// ============================================================================
// Calibration
// ============================================================================
/// Create calibration block.
/// @precondition The application is initialized.
/// @param app_id Application identifier in which the block resides.
/// @param calibration_block_name Calibration block name. Does **not** require a null terminator.
/// @param calibration_block_name_length Calibration block name length in bytes.
/// @param calibration_block_size Calibration block buffer size in bytes.
/// @return Assigned calibration value identifier that is unique within the calibration block.
McCalibrationBlockId mc_calibration_block_init(McAppId app_id, char const* calibration_block_name,
                                               size_t calibration_block_name_length, size_t calibration_block_size);

/// Get calibration block shared memory offset in the application shared memory region.
/// @precondition Application is initialized.
/// @precondition Calibration block is initialized.
/// @param app_id Application identifier in which the block resides.
/// @param calibration_block_id Calibration block identifier.
/// @return Assigned calibration block shared memory offset within the application shared memory region.
uint64_t mc_calibration_block_get_offset(McAppId app_id, McCalibrationBlockId block_id);

/// Get calibration block shared memory size in the application shared memory region.
/// @precondition Application is initialized.
/// @precondition Calibration block is initialized.
/// @param app_id Application identifier in which the block resides.
/// @param calibration_block_id Calibration block identifier.
/// @return Assigned calibration block shared memory size within the application shared memory region.
uint64_t mc_calibration_block_get_size(McAppId app_id, McCalibrationBlockId block_id);

/// Get the calibration block modification counter which is incremented after each write lock is released.
/// This can be used for optimization in order to operate on local copies of the block instead of holding the lock.
/// NOTE: this is a temporary optimization API due to simple mutex usage in this reference.
/// @precondition Application is initialized.
/// @precondition Calibration block is initialized.
/// @param app_id Application identifier in which the block resides.
/// @param calibration_block_id Calibration block identifier.
/// @return Modification counter value.
uint64_t mc_calibration_block_get_modification_counter(McAppId app_id, McCalibrationBlockId block_id);

/// Create a calibration value in the calibration block filled with an initial value.
/// @precondition Calibration identifier is associated to the calibration block identifier.
/// @precondition Calibration block identifier is associated to the application identifier.
/// @param app_id Application identifier in which the block resides.
/// @param calibration_block_id Calibration block identifier.
/// @param value Value pointer to be copied into the calibration value. A null pointer writes zeroes instead.
/// @param value_bytes Value size pointer to by the value pointer.
/// @return Assigned calibration value identifier that is unique within the calibration block.
McCalibrationValueId mc_calibration_value_init_untyped(McAppId app_id, McCalibrationBlockId calibration_block_id,
                                                       uint8_t const* value, size_t value_bytes);

/// Read lock a calibration block to gain consistent access.
/// Unlocking an unlock calibration block has no effect.
/// @precondition The application is initialized.
/// @precondition Calibration block identifier is associated to the application identifier.
/// @param app_id Application identifier in which the block resides.
/// @param calibration_block_id Calibration block identifier.
/// @return Read lock handle required to read the calibration values. Must be released using the read end call or
/// returns NULL on error.
McBlockReadLock mc_calibration_block_read_begin(McAppId app_id, McCalibrationBlockId calibration_block_id);

/// Unlock calibration block.
/// @param calibration_block_lock Calibration block lock.
void mc_calibration_block_read_end(McBlockReadLock calibration_segment_lock);

/// Acquire a conistent read access to a calibration value from a read locked block.
/// @param calibration_block_lock Calibration block lock.
/// @return Read access to the calibration value. Valid and consistent until read end is called.
uint8_t const* mc_calibration_value_read_untyped(McBlockReadLock calibration_block_lock, McCalibrationValueId value_id);

/// Switch the active page within a calibration block.
/// @precondition The application is initialized.
/// @precondition Calibration block identifier is associated to the application identifier.
/// @param app_id Application identifier in which the block resides.
/// @param calibration_block_id Calibration block identifier.
/// @param page_id Calibration page ID to switch to.
/// @return MC_RESULT_SUCCESS on success, MC_RESULT_ERROR on mutex lock failure.
McResult mc_calibration_page_switch(McAppId app_id, McCalibrationBlockId calibration_block_id,
                                    McCalibrationPageId page_id);

/// Gets the active page within a calibration block.
/// @precondition The application is initialized.
/// @precondition Calibration block identifier is associated to the application identifier.
/// @param app_id Application identifier in which the block resides.
/// @param calibration_block_id Calibration block identifier.
/// @return Calibration page ID.
uint8_t mc_get_calibration_page(McAppId app_id, McCalibrationBlockId calibration_block_id);

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

uint8_t const* mc_get_app_base_address(McAppId app_id);

uint64_t get_timestamp_ns(void);

#define mc_get_stack_frame_pointer() (const uint8_t*)__builtin_frame_address(0)

// Function to get the compilation UUID
McUuid mc_get_application_uuid(const char* app_name, uint8_t global_unique_identifier, const char* date_time_version);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // LIB_REFERENCE_INCLUDE_REFERENCE_H_
