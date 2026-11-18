#pragma once
#define __NAMELIST_H__

/*----------------------------------------------------------------------------
| File:
|   namelist.h
|
| Description:
|   Generic lock-free and wait-free indexed list of named entries
|
|   A name list maps a name to a small dense id (0..count-1) and an id to an entry (name + optional user data).
|   Entries are only added, never removed or modified, the list has static lifetime.
|   No mutex, no spinlock, no per entry valid flag, see namelist.c for the algorithm and its guarantees.
|
|   The list lives in one caller provided memory block and contains no pointers,
|   it may be placed in shared memory and used by several processes (SHM mode).
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <stdbool.h> // for bool
#include <stddef.h>  // for size_t
#include <stdint.h>  // for uint32_t, uint64_t

#include "platform.h" // for atomics

#ifdef __cplusplus
extern "C" {
#endif

// Returned instead of an id, when a name is not found or can not be registered
#define NAMELIST_UNDEFINED 0xFFFFFFFFu

// Number of parking slots for entry cells, which lost a race for the same name and are reused by the next registration
#define NAMELIST_PARK_SLOTS 8

// List header, followed by the slot table and the entry cells in the same memory block
// The parameters are written once by NameListInit and are constant afterwards, all other state is atomic
typedef struct {
    uint32_t capacity;                               // Maximum number of entries, ids are 0..capacity-1
    uint32_t cell_count;                             // Number of entry cells, capacity + spare
    uint32_t cell_size;                              // Size of an entry cell in bytes
    uint32_t name_size;                              // Size of the name buffer in an entry cell, maximum name length + 1
    uint32_t data_offset;                            // Offset of the user data in an entry cell
    uint32_t data_size;                              // Size of the user data in bytes, may be 0
    atomic_uint_least32_t count;                     // Number of entries, a lower bound which may lag, see NameListGetCount
    atomic_uint_least32_t alloc;                     // Number of entry cells taken from the cell pool
    atomic_uint_least32_t park[NAMELIST_PARK_SLOTS]; // Unused entry cells (cell number + 1), 0 is empty
} tNameList;

// Memory size of a name list in bytes
//   capacity   maximum number of entries
//   spare      additional entry cells, see NameListRegister, recommended is the number of threads which register names concurrently
//   name_size  maximum name length + 1
//   data_size  size of the user data of an entry in bytes, may be 0
#define NAMELIST_ALIGN8(n) (((size_t)(n) + 7u) & ~(size_t)7u)
#define NAMELIST_CELL_SIZE(name_size, data_size) (NAMELIST_ALIGN8(sizeof(uint32_t) + (size_t)(name_size)) + NAMELIST_ALIGN8(data_size))
#define NAMELIST_SIZE(capacity, spare, name_size, data_size)                                                                                                                       \
    (sizeof(tNameList) + NAMELIST_ALIGN8((size_t)(capacity) * sizeof(uint32_t)) + ((size_t)(capacity) + (size_t)(spare)) * NAMELIST_CELL_SIZE(name_size, data_size))

// Declare the memory of a name list with static size, as a variable or as a member of a struct
// Example:
//   static NAMELIST_STORAGE(64, 8, 32, 0) span_names;
//   NameListInit(&span_names.list, sizeof(span_names), 64, 8, 32, 0);
#define NAMELIST_STORAGE(capacity, spare, name_size, data_size)                                                                                                                    \
    union {                                                                                                                                                                        \
        tNameList list;                                                                                                                                                            \
        uint64_t mem[NAMELIST_SIZE(capacity, spare, name_size, data_size) / sizeof(uint64_t)];                                                                                     \
    }

// Initialize an empty name list in the memory block list with mem_size bytes
// Must be called exactly once, before any other function and before the list is visible to other threads or processes
// The memory block must be aligned to 8 bytes and have at least NAMELIST_SIZE(capacity, spare, name_size, data_size) bytes
// Returns false, when the parameters are invalid or the memory block is too small
bool NameListInit(tNameList *list, size_t mem_size, uint32_t capacity, uint32_t spare, uint32_t name_size, uint32_t data_size);

// Register a name or return the id of the existing entry with this name
// The key of an entry is (scope, name), scope is an arbitrary namespace number, for example an application id, use 0 when there is only one namespace
// Names longer than name_size-1 are truncated, names which differ only after the truncation are the same name
// data is copied to the user data of a new entry (data_size bytes) before the entry becomes visible, NULL clears the user data, ignored when the entry exists
// created (may be NULL) is set to true, when this call created the entry, to false when the entry already existed
// Returns the entry id (0..capacity-1), ids are assigned in ascending order without gaps
// Returns NAMELIST_UNDEFINED when the name is NULL or empty, or when the list is full
// Thread safe, wait-free: all threads and processes get the same id for the same key, even when they register it at the same time
// Registering the last entries of a list may fail early, when more than spare+1 threads register new names at the same time
uint32_t NameListRegister(tNameList *list, uint32_t scope, const char *name, const void *data, bool *created);

// Find an entry, returns the entry id or NAMELIST_UNDEFINED
// Thread safe, wait-free
uint32_t NameListFind(const tNameList *list, uint32_t scope, const char *name);

// Get the current number of entries n
// All entries with id 0..n-1 exist, are completely initialized and visible to the caller
// Every registration which returned before this call started is included, the result never decreases
// Thread safe, wait-free
uint32_t NameListGetCount(const tNameList *list);

// Get the maximum number of entries
uint32_t NameListGetCapacity(const tNameList *list);

// Get the name, the scope or the user data of an entry
// The name and the scope are immutable, the pointers stay valid for the lifetime of the list
// The user data is initialized by NameListRegister, later modifications are in the responsibility of the user (for example with atomic members)
// Returns NULL (scope 0), when the entry does not exist, NameListGetData returns NULL when data_size is 0
// Thread safe, wait-free
const char *NameListGetName(const tNameList *list, uint32_t id);
uint32_t NameListGetScope(const tNameList *list, uint32_t id);
void *NameListGetData(tNameList *list, uint32_t id);
const void *NameListGetConstData(const tNameList *list, uint32_t id);

#ifdef __cplusplus
} // extern "C"
#endif
