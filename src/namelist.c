/*****************************************************************************
| File:
|   namelist.c
|
| Description:
|   Generic lock-free and wait-free indexed list of named entries
|
|   Purpose:
|     Map a name to a small dense id (0..count-1) and an id to an entry (name + optional user data).
|     Used for objects which are created once and live forever, like span names, contexts, events, calibration segments.
|
|   Assumptions:
|     - The list is empty and cleared in the beginning (NameListInit, not concurrent).
|     - Static lifetime: entries are never removed, the name and the scope of an entry are never modified, the list is never cleared.
|     - The number of entries is limited (capacity).
|
|   Memory layout, one block without pointers (position independent, may be shared between processes):
|
|       tNameList                 header: constant parameters, count, cell allocator
|       slot[capacity]            atomic uint32, slot[id] is the number + 1 of the entry cell of id, 0 as long as id does not exist
|       cell[capacity + spare]    entry cells: uint32 scope, char name[name_size], padding to 8, user data
|
|   Algorithm:
|     An entry is never written in place where other threads may already look at it.
|     NameListRegister
|       1. Scans slot[0..] for the key (scope, name), until it finds the key or the first empty slot k.
|       2. Takes a private entry cell (wait-free bump allocator) and writes scope, name and user data into it.
|          Nobody else knows this cell, there is nothing to protect.
|       3. Publishes the cell with a single compare-and-swap of slot[k] from 0 to the cell number.
|          This is the linearization point: the entry does not exist before and exists completely afterwards.
|       4. When the compare-and-swap fails, another thread created id k in the meantime.
|          The scan continues at k with the same private cell, the lost race costs one more compare, never a wait.
|          When the other thread registered the same key, its id is returned and the private cell is given back.
|
|   Guarantees:
|     - Ids are dense: slot[k] is only written by a thread which has seen slot[0..k-1] not empty, and slots never become empty again.
|     - Keys are unique: a thread publishes at slot k only after it compared its key with all entries 0..k-1,
|       and every later entry is created by a thread which compared with entry k. Two threads which register
|       the same key at the same time always get the same id.
|     - There is no state "allocated but not yet valid": an id is either not existing (slot is 0) or complete (slot is not 0).
|       No valid flag in the entry, no reservation counter, no thread ever waits for another thread to finish its entry.
|     - A thread or process which is suspended or killed anywhere inside NameListRegister never blocks the others
|       and never leaves a damaged list behind, at worst one entry cell is lost.
|     - Wait-free: every loop is bounded. A failed compare-and-swap means that the list has grown, which happens at most capacity times.
|       NameListRegister needs at most capacity name compares, capacity slot compare-and-swaps and capacity + spare allocator retries.
|
|   Requirements, comparison with other concepts, memory and runtime costs, limitations: see docs/NAMELIST.md
|
|   Number of entries:
|     The number of entries is defined by the slot table (the number of leading slots which are not 0), this is always exact.
|     header.count caches it to make NameListGetCount O(1). The cache may lag behind when a thread is suspended between
|     publishing its entry and updating the count. It is never wrong: it only grows, and all entries below it exist.
|     Every thread repairs it (helping): NameListGetCount continues at the cached value and checks the following slots,
|     the next NameListRegister stores a higher value. Nobody depends on the thread which created the entry.
|
|   Entry cells and spare cells:
|     A thread which loses the race for the same key holds an entry cell it can not use.
|     It parks the cell in one of NAMELIST_PARK_SLOTS atomic parking slots, the next NameListRegister takes it from there (atomic exchange,
|     exclusive ownership, no ABA problem). Only when all parking slots are occupied at the same time, the cell is lost.
|     Cells which are currently in work by other threads are not available. Therefore the cell pool has spare cells in addition to capacity.
|     With spare >= (number of threads which register concurrently) - 1 and no lost cells, capacity entries can always be registered.
|
|   Memory ordering:
|     The slot compare-and-swap is a release operation, every load of a slot is an acquire operation.
|     This makes the complete entry cell visible to every thread which sees the slot not empty.
|     The parking slots use the same release/acquire pair to hand over the ownership of a cell.
|     The count is stored with release after the slots it covers have been loaded with acquire, and loaded with acquire.
|
|   Costs, compared to a plain array with a count:
|     - One additional load (id -> slot -> cell) to access an entry by id.
|     - Entry cells are not ordered by id in memory.
|     - 4 bytes per entry for the slot table and the spare cells.
|     - Finding a name is a linear search with a name compare per entry, as before.
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "namelist.h"

#include <assert.h>  // for assert, static_assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uint8_t, uint32_t, uint64_t
#include <string.h>  // for memcpy, memset, strncmp, strncpy

#include "platform.h" // for atomics

// The slot table is an array of 32 bit atomics, which are lock-free on all supported platforms
static_assert(sizeof(atomic_uint_least32_t) == sizeof(uint32_t), "atomic_uint_least32_t must have 4 bytes");
static_assert(sizeof(tNameList) % 8 == 0, "tNameList size must be a multiple of 8");

// Offset of the name in an entry cell, the scope is at offset 0
#define CELL_NAME_OFFSET sizeof(uint32_t)

#define NO_CELL 0xFFFFFFFFu

//----------------------------------------------------------------------------------
// Memory layout

static inline const atomic_uint_least32_t *constSlots(const tNameList *list) { return (const atomic_uint_least32_t *)(const void *)((const uint8_t *)list + sizeof(tNameList)); }

static inline atomic_uint_least32_t *slots(tNameList *list) { return (atomic_uint_least32_t *)(void *)((uint8_t *)list + sizeof(tNameList)); }

static inline size_t cellsOffset(const tNameList *list) { return sizeof(tNameList) + NAMELIST_ALIGN8((size_t)list->capacity * sizeof(uint32_t)); }

static inline const uint8_t *constCell(const tNameList *list, uint32_t cell) { return (const uint8_t *)list + cellsOffset(list) + (size_t)cell * list->cell_size; }

static inline uint8_t *mutCell(tNameList *list, uint32_t cell) { return (uint8_t *)list + cellsOffset(list) + (size_t)cell * list->cell_size; }

// Get the entry cell of id, NULL when id does not exist
// The acquire load of the slot makes the content of the cell visible
static inline const uint8_t *constEntry(const tNameList *list, uint32_t id) {
    if (list == NULL || id >= list->capacity)
        return NULL;
    uint32_t s = (uint32_t)atomic_load_explicit(&constSlots(list)[id], memory_order_acquire);
    return s != 0 ? constCell(list, s - 1) : NULL;
}

//----------------------------------------------------------------------------------
// Entry cells

// Compare the key of a published entry cell
// The cell name has at most name_size-1 characters, longer names compare equal to their truncation
static inline bool cellMatch(const tNameList *list, const uint8_t *cell, uint32_t scope, const char *name) {
    uint32_t cell_scope;
    memcpy(&cell_scope, cell, sizeof(cell_scope));
    return cell_scope == scope && strncmp((const char *)cell + CELL_NAME_OFFSET, name, list->name_size - 1) == 0;
}

// Write a private entry cell, complete overwrite
static void cellWrite(const tNameList *list, uint8_t *cell, uint32_t scope, const char *name, const void *data) {
    memcpy(cell, &scope, sizeof(scope));
    char *cell_name = (char *)cell + CELL_NAME_OFFSET;
    strncpy(cell_name, name, list->name_size - 1); // Fills the rest of the name buffer with 0
    cell_name[list->name_size - 1] = 0;
    if (list->data_size > 0) {
        if (data != NULL) {
            memcpy(cell + list->data_offset, data, list->data_size);
        } else {
            memset(cell + list->data_offset, 0, list->data_size);
        }
    }
}

// Get an entry cell for exclusive use, returns NO_CELL when there is none
// Wait-free: the exchange is unconditional, a failed compare-and-swap means that another thread got a cell from the bounded pool
static uint32_t cellAlloc(tNameList *list) {

    // Reuse a parked cell
    for (uint32_t i = 0; i < NAMELIST_PARK_SLOTS; i++) {
        if (atomic_load_explicit(&list->park[i], memory_order_relaxed) != 0) {
            uint32_t p = (uint32_t)atomic_exchange_explicit(&list->park[i], 0, memory_order_acquire);
            if (p != 0)
                return p - 1;
        }
    }

    // Take a new cell from the pool
    uint32_t n = (uint32_t)atomic_load_explicit(&list->alloc, memory_order_relaxed);
    while (n < list->cell_count) {
        if (atomic_compare_exchange_weak_explicit(&list->alloc, &n, n + 1, memory_order_relaxed, memory_order_relaxed))
            return n;
    }
    return NO_CELL;
}

// Give back an entry cell, which has not been published
// When all parking slots are occupied, the cell is lost
static void cellFree(tNameList *list, uint32_t cell) {
    for (uint32_t i = 0; i < NAMELIST_PARK_SLOTS; i++) {
        uint32_t expected = 0;
        if (atomic_compare_exchange_strong_explicit(&list->park[i], &expected, cell + 1, memory_order_release, memory_order_relaxed))
            return;
    }
}

//----------------------------------------------------------------------------------
// Count

// Raise the cached number of entries to n, the caller has seen slot[n-1] not empty
// Wait-free: a failed compare-and-swap means that the count has grown
static void countAdvance(tNameList *list, uint32_t n) {
    uint32_t c = (uint32_t)atomic_load_explicit(&list->count, memory_order_relaxed);
    while (c < n) {
        if (atomic_compare_exchange_weak_explicit(&list->count, &c, n, memory_order_release, memory_order_relaxed))
            return;
    }
}

//----------------------------------------------------------------------------------
// Public functions

bool NameListInit(tNameList *list, size_t mem_size, uint32_t capacity, uint32_t spare, uint32_t name_size, uint32_t data_size) {

    if (list == NULL || ((uintptr_t)list & 7u) != 0)
        return false;
    if (capacity == 0 || name_size < 2)
        return false;

    // Cell numbers + 1 are stored in 32 bit slots
    // All sizes are calculated in 64 bit (not with the size_t macros), to detect overflows on 32 bit platforms
    uint64_t cell_count = (uint64_t)capacity + spare;
    uint64_t data_offset = ((uint64_t)sizeof(uint32_t) + name_size + 7u) & ~(uint64_t)7u;
    uint64_t cell_size = data_offset + (((uint64_t)data_size + 7u) & ~(uint64_t)7u);
    if (cell_count >= 0x7FFFFFFFu || cell_size >= 0x7FFFFFFFu)
        return false;
    uint64_t size = (uint64_t)sizeof(tNameList) + ((((uint64_t)capacity * sizeof(uint32_t)) + 7u) & ~(uint64_t)7u) + cell_count * cell_size;
    if (size > (uint64_t)mem_size)
        return false;

    // Empty list: all slots 0, no cells allocated, no cells parked
    memset(list, 0, (size_t)size);
    list->capacity = capacity;
    list->cell_count = (uint32_t)cell_count;
    list->cell_size = (uint32_t)cell_size;
    list->name_size = name_size;
    list->data_offset = (uint32_t)data_offset;
    list->data_size = data_size;
    return true;
}

uint32_t NameListRegister(tNameList *list, uint32_t scope, const char *name, const void *data, bool *created) {

    if (created != NULL)
        *created = false;
    if (list == NULL || name == NULL || name[0] == 0)
        return NAMELIST_UNDEFINED;

    atomic_uint_least32_t *slot = slots(list);
    uint32_t cell = NO_CELL; // Private entry cell, allocated and written when the key is not found
    uint32_t id = 0;         // All entries below id have been compared with the key

    for (;;) {

        // Find the key or the first empty slot
        for (; id < list->capacity; id++) {
            uint32_t s = (uint32_t)atomic_load_explicit(&slot[id], memory_order_acquire);
            if (s == 0)
                break;
            if (cellMatch(list, constCell(list, s - 1), scope, name)) {
                if (cell != NO_CELL)
                    cellFree(list, cell); // Another thread registered the same key first
                return id;
            }
        }

        // List is full
        if (id >= list->capacity) {
            if (cell != NO_CELL)
                cellFree(list, cell);
            return NAMELIST_UNDEFINED;
        }

        // Prepare the entry in a private cell
        if (cell == NO_CELL) {
            cell = cellAlloc(list);
            if (cell == NO_CELL)
                return NAMELIST_UNDEFINED; // No cell left
            cellWrite(list, mutCell(list, cell), scope, name, data);
        }

        // Publish the entry as id, this is the only step visible to other threads
        uint32_t expected = 0;
        if (atomic_compare_exchange_strong_explicit(&slot[id], &expected, cell + 1, memory_order_release, memory_order_relaxed)) {
            countAdvance(list, id + 1);
            if (created != NULL)
                *created = true;
            return id;
        }

        // Another thread created id in the meantime, compare with its entry and continue
        // The slot is not empty now and never becomes empty again, id increases with every turn
    }
}

uint32_t NameListFind(const tNameList *list, uint32_t scope, const char *name) {

    if (list == NULL || name == NULL || name[0] == 0)
        return NAMELIST_UNDEFINED;

    const atomic_uint_least32_t *slot = constSlots(list);
    for (uint32_t id = 0; id < list->capacity; id++) {
        uint32_t s = (uint32_t)atomic_load_explicit(&slot[id], memory_order_acquire);
        if (s == 0)
            break;
        if (cellMatch(list, constCell(list, s - 1), scope, name))
            return id;
    }
    return NAMELIST_UNDEFINED;
}

uint32_t NameListGetCount(const tNameList *list) {

    if (list == NULL)
        return 0;

    // Start at the cached count and include the entries of threads, which did not update the count yet
    // Read only, the cached count is raised by the next registration
    const atomic_uint_least32_t *slot = constSlots(list);
    uint32_t n = (uint32_t)atomic_load_explicit(&list->count, memory_order_acquire);
    while (n < list->capacity && atomic_load_explicit(&slot[n], memory_order_acquire) != 0) {
        n++;
    }
    return n;
}

uint32_t NameListGetCapacity(const tNameList *list) { return list != NULL ? list->capacity : 0; }

const char *NameListGetName(const tNameList *list, uint32_t id) {
    const uint8_t *cell = constEntry(list, id);
    return cell != NULL ? (const char *)cell + CELL_NAME_OFFSET : NULL;
}

uint32_t NameListGetScope(const tNameList *list, uint32_t id) {
    const uint8_t *cell = constEntry(list, id);
    uint32_t scope = 0;
    if (cell != NULL)
        memcpy(&scope, cell, sizeof(scope));
    return scope;
}

const void *NameListGetConstData(const tNameList *list, uint32_t id) {
    const uint8_t *cell = constEntry(list, id);
    return (cell != NULL && list->data_size > 0) ? cell + list->data_offset : NULL;
}

void *NameListGetData(tNameList *list, uint32_t id) {
    if (list == NULL || id >= list->capacity || list->data_size == 0)
        return NULL;
    uint32_t s = (uint32_t)atomic_load_explicit(&slots(list)[id], memory_order_acquire);
    return s != 0 ? mutCell(list, s - 1) + list->data_offset : NULL;
}
