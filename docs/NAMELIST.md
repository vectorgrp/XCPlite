# XCPlite name list: a lock-free and wait-free indexed list of named entries

This document describes the generic name list module (`src/namelist.h`, `src/namelist.c`): the requirements, the concepts which were considered, the algorithm with its guarantees and memory ordering, memory overhead, runtime cost, limitations and test results.

A name list maps a name to a small dense id (0..count-1) and an id to an entry (name + optional user data). Entries are only added. Typical objects are span names, contexts, events and calibration segments: created once by any thread, alive until the process ends, looked up by id on fast paths and by name on creation.

Status: the module is part of `libxcplite`. It is used for the context list and for the span name list in `src/ctx.c`, see chapter 11.

Chapter 13 describes a different approach for names which are string literals: registration at link time in a linker section, without any runtime list. It is implemented for span names (`OPTION_DAQ_SPAN_SECTION_REGISTRATION`, the default where it is available), see 13.8.

Contents:
1. Requirements
2. Concepts considered
3. Data structure
4. Algorithm
5. Guarantees
6. Memory ordering
7. Memory overhead
8. Runtime cost
9. API and usage
10. Limitations and notes
11. Integration status and next steps
12. Test results
13. Alternative: link time registration in a linker section


## 1. Requirements

### 1.1 Original requirements

1. An indexed list with names, like the span name list in `ctx.c`.
2. No mutex.
3. Lock-free and wait-free.
4. No spinlocks, no waiting for another thread in any form.
5. Generic: usable for spans, later for contexts, events, calibration segments.
6. The list is empty and cleared in the beginning.
7. Static lifetime: the list is never cleared, entries are never removed.
8. No valid flag in each entry.
9. The current number of entries is always valid.
10. Atomic, with defined visibility of new entries.
11. Implemented in C (C11), not C++.
12. A new module, no changes to existing code.

### 1.2 Additional requirements

These were not stated, but follow from how the existing lists are used. They decide which concepts are possible at all, see chapter 2.

13. **Name uniqueness under concurrency.** Registration is "find or create": two threads which register the same name at the same time must get the same id, and the name must exist only once. The existing span and context lists guarantee this with a mutex. A typical case is a thread pool where all workers execute the same instrumented code for the first time at the same moment.
14. **Dense and stable ids.** Ids are 0..count-1 without gaps, in the order of creation, and never change. They are transmitted as measurement values and used as array index and in A2L enum conversions.
15. **Shared memory capable.** In SHM mode the lists are part of the shared XCP state and used by several processes. The list must be position independent (no pointers inside), must not use process local state or thread local storage, and may only use atomics which work across processes (lock-free, no hidden lock table).
16. **Safe against suspended and killed threads and processes.** A thread which is preempted, cancelled or killed anywhere inside an operation must not block other threads and must not leave a damaged list. This is the practical reason for requirement 2 in SHM mode: a mutex in shared memory which is held by a crashed process blocks all others forever.
17. **Complete before visible.** An entry including its user data is completely initialized before any other thread can see it. No reader ever sees a partial name or partial data.
18. **Immutable keys, stable pointers.** Name and scope of an entry never change, and a pointer to an entry stays valid for the lifetime of the list. This allows callers to cache ids and pointers without any protection.
19. **Monotonic and linearizable count.** The count never decreases, it includes every registration which has returned, and every id below the count is accessible.
20. **Bounded execution time.** Every operation finishes in a number of steps which depends only on the configured capacity, not on the behaviour of other threads. No system calls, no heap.
21. **Static memory.** One caller provided memory block with a size known at compile time, so the list can be a member of a struct such as `tXcpData`.
22. **Namespaces.** Names may have to be unique per application (contexts in SHM mode) or per context (events). The key is therefore (scope, name), with an arbitrary 32 bit scope.
23. **Portable atomics.** Only 32 bit atomics with load, store, exchange and compare-and-swap, which are lock-free on all supported targets, including 32 bit microcontrollers (no 64 bit atomics) and the Windows atomic emulation in `platform.h`.
24. **No ABA problem**, without relying on tags or version counters.

### 1.3 Not required, not supported

- Removing or renaming entries, clearing or reinitializing a list which is in use.
- Fast lookup by name. Finding a name is a linear search, as in the existing lists. Names are registered once and the id is cached by the caller.
- Protection of user data which is modified after the entry was created. This is in the responsibility of the user.


## 2. Concepts considered

| Concept | Unique names under concurrency | Suspended or killed thread | Access by id | Remarks |
|---|---|---|---|---|
| A. Mutex (existing lists) | yes | blocks all others while it holds the mutex | direct | Not allowed, not robust in shared memory |
| B. Allocation counter + valid counter, ordered commit | no | blocks the count for all later entries | direct | A thread must wait until all lower entries are committed, this is a spinlock |
| C. Packed `{allocated, done}` counter | no | delays the count as long as any entry is in work | direct | Wait-free and simple, count is valid whenever allocated == done, may lag indefinitely |
| D. State word per entry (claimed, then ready) | only by waiting for claimed entries | blocks registrations which have to compare with its entry | direct | This is the valid flag which should be avoided |
| E. In place with helping copy | yes | no effect | direct | The pending entry is announced in a staging cell, any thread copies it to its final place |
| F. Slot table + private cells (implemented) | yes | no effect | one additional load | |

Concepts B and C are the original idea of two counters. A thread which has incremented the allocation counter can not increment the valid counter, because it does not know whether the threads with lower entries are done. B solves this by waiting, which is not lock-free. C solves it without waiting by publishing the count only in moments when no entry is in work, which is wait-free, but the count can lag behind for an unlimited time and a killed thread stops it forever.

**Name uniqueness (requirement 13) is the deciding point.** To guarantee it, a thread must compare its name with every earlier entry before it commits its own. With entries written in place, an earlier entry may be claimed but not yet written. The thread then either waits for it (B, D: not lock-free), ignores it (B, C: duplicates), or must be able to read the name from somewhere else (E, F). So the name has to be complete in shared memory *before* the entry is claimed.

Concept E does this with helping in the classical sense: a state word `{count, staging cell}` announces the pending insert, any thread copies the staged entry to `entry[count]` and advances the count. It keeps the plain array, but
- a slow helper may still read a staging cell after the operation was completed by others, so staging cells can never be reused and the staging area must be as large as the list,
- several helpers write the same bytes to the same entry at the same time, which is harmless on real hardware but formally a data race in C11 and reported by ThreadSanitizer,
- it is clearly more complex.

Concept F is E without the copy: if the staged entry can never be reused anyway, it can as well *be* the entry. Publishing then is one compare-and-swap of a cell number, and nothing remains to be helped except the cached count.


## 3. Data structure

One memory block without pointers:

```
+--------------------------+
| tNameList (64 bytes)     |  constant parameters: capacity, cell_count, cell_size, name_size, data_offset, data_size
|                          |  atomic: count (cached number of entries), alloc (cells taken from the pool), park[8]
+--------------------------+
| slot[capacity]           |  atomic uint32 per id: number + 1 of the entry cell of this id, 0 = id does not exist
+--------------------------+
| cell[capacity + spare]   |  entry cells of cell_size bytes:
|                          |      uint32 scope
|                          |      char   name[name_size]     zero terminated
|                          |      padding to a multiple of 8
|                          |      user data[data_size]       aligned to 8, padded to a multiple of 8
+--------------------------+
```

- The **slot table** is the list. `slot[id]` is written exactly once, from 0 to a cell number, and never changes again.
- The **cell pool** is the storage. A cell has one of four states: unused (not yet taken from the pool), private (owned by exactly one thread inside `NameListRegister`), published (referenced by exactly one slot, forever) or parked (see 4.4).
- The parameters in the header are written by `NameListInit` and are constant afterwards. All functions read the layout from the header, a process which attaches to a list in shared memory does not need to know the parameters.


## 4. Algorithm

### 4.1 Register (find or create)

```
NameListRegister(scope, name, data):
    id = 0, cell = none
    loop:
        // 1. Find the key or the first empty slot
        while id < capacity and slot[id] != 0:
            if entry of slot[id] has key (scope, name): give back cell if any, return id
            id++
        if id == capacity: give back cell if any, return UNDEFINED          // full

        // 2. Prepare the entry in a private cell, once
        if cell == none:
            cell = allocate a cell, return UNDEFINED if there is none
            write scope, name and data to the cell

        // 3. Publish
        if compare_and_swap(slot[id], 0 -> cell + 1): raise cached count to id + 1, return id

        // 4. Another thread created this id in the meantime: continue at step 1 with the same id and the same cell
```

Step 3 is the only step which is visible to other threads. It is the linearization point of a successful creation: before it the entry does not exist, after it the entry exists completely.

When the compare-and-swap fails, `slot[id]` is not empty any more and never will be again. Step 1 compares the key with the new entry and moves on. A lost race costs one more name compare, never a wait, and the already written cell is used for the next attempt.

Two threads register different names at the same time:

| Thread A, "foo" | Thread B, "bar" | slot[5] | slot[6] |
|---|---|---|---|
| scans 0..4, slot[5] is empty | scans 0..4, slot[5] is empty | 0 | 0 |
| writes private cell 7 | writes private cell 8 | 0 | 0 |
| CAS slot[5] 0 -> 8 succeeds, returns 5 | | 8 | 0 |
| | CAS slot[5] 0 -> 9 fails | 8 | 0 |
| | compares "bar" with entry 5 ("foo"), no match | 8 | 0 |
| | CAS slot[6] 0 -> 9 succeeds, returns 6 | 8 | 9 |

Two threads register the same name at the same time: the same sequence, but thread B finds its name in entry 5, parks its cell 8 and returns 5. Both threads have the same id, the name exists once.

### 4.2 Find and access by id

- `NameListFind` is step 1 of the registration alone.
- `NameListGetName`, `NameListGetScope`, `NameListGetData`: load `slot[id]`, return NULL when it is 0, otherwise the address of the cell.

### 4.3 Number of entries

The number of entries is *defined* by the slot table: the number of leading slots which are not 0. This is always exact, there is nothing which could be inconsistent with it.

`count` in the header caches this number, so `NameListGetCount` does not have to search for the end of the list. A registering thread raises it after it has published (compare-and-swap to the maximum of the old value and id + 1). A thread which is suspended between publishing and raising leaves the cached value too low, but never wrong: all entries below it exist.

This is where the idea of helping is used: nobody depends on the thread which created an entry.
- `NameListGetCount` starts at the cached value and continues along the slot table until it finds an empty slot. The result is exact. In the normal case this is one load of the count and one load of a slot.
- The next registration stores its own, higher value.

`NameListGetCount` does not write, the list parameter is const.

### 4.4 Entry cells

Cells are taken from the pool with a bump allocator (`alloc`, compare-and-swap loop limited by `cell_count`).

A thread which loses the race for the *same* key holds a written cell which it can not use. It gives the cell back into one of `NAMELIST_PARK_SLOTS` (8) atomic parking slots (compare-and-swap from 0 to cell number + 1). The allocator first tries to take a parked cell with an unconditional atomic exchange, then takes a new cell from the pool.

- The exchange gives exclusive ownership. No thread ever follows a reference to a cell which it does not own or which is not published, so there is no ABA problem (requirement 24).
- A reused cell is completely overwritten before it is published.
- When all parking slots are occupied at the same moment, the cell is lost. This needs more than 8 unused cells at the same time and has not been observed in the tests.

Cells which are in work by other threads are not available. Therefore the pool has `spare` cells in addition to `capacity`, see 9.2.


## 5. Guarantees

### 5.1 Invariants

- **Dense ids.** `slot[k]` is only written by a thread which has seen `slot[0..k-1]` not empty, and slots never become empty again. So the slots which are not 0 are always exactly `slot[0..n-1]`.
- **Unique keys.** A thread publishes at slot k only after it has compared its key with the entries 0..k-1, which are immutable. Every entry behind k is created by a thread which has seen slot k not empty and has compared its key with entry k. So no two entries have the same key, and all threads get the same id for the same key.
- **Complete entries.** A cell is written while it is private and never modified by the module after it is published. There is no state "allocated but not yet valid": an id either does not exist (slot is 0) or is complete (slot is not 0). The slot is the reference to the entry, not a flag in the entry, and there is no second step which could be missing.

### 5.2 Visibility

- The creating thread sees its entry when `NameListRegister` returns: `NameListGetCount` is greater than the id, `NameListFind` returns the id.
- Every operation of any thread or process which starts after the publishing compare-and-swap sees the entry.
- `NameListGetCount` returns n: all ids 0..n-1 exist and are accessible by the caller, and n is not smaller than the result of any earlier call (monotonic).
- Operations are linearizable: each one behaves as if it happened at one single moment between its call and its return.

### 5.3 Wait-freedom

Every loop is bounded by the configuration:

| Operation | Bound |
|---|---|
| `NameListRegister` | `capacity` slot loads and name compares, `capacity` slot compare-and-swaps, 8 + 8 parking slot operations, `capacity + spare` allocator retries |
| `NameListFind` | `capacity` slot loads and name compares |
| `NameListGetCount` | `capacity` slot loads (worst case, normally 1) |
| `NameListGetName`, `NameListGetData`, `NameListGetScope` | 1 slot load |

The retry loops are wait-free and not only lock-free, because the list is finite and only grows: a failed compare-and-swap means that another thread has added an entry or taken a cell, and this can happen at most `capacity` or `capacity + spare` times in the lifetime of the list. In practice a registration fails a compare-and-swap at most once per concurrently registering thread.

### 5.4 Suspended or killed threads

A thread or process which stops forever inside `NameListRegister`:

| Stops | Effect |
|---|---|
| while searching | none |
| after it has taken a cell, before publishing | one cell is lost, the entry does not exist |
| after publishing, before raising the cached count | the entry exists and is complete, the cached count is too low until the next registration, `NameListGetCount` is exact anyway |
| while giving back a cell | one cell is lost |

Nothing is ever locked, so nothing has to be released or repaired. The other functions do not write.

A thread which stops while it modifies *user data* of an existing entry leaves this data as it is, this is outside of the module.

### 5.5 Shared memory

- No pointers and no addresses are stored in the list, only ids and cell numbers. Each process may map the list at a different address.
- No process local or thread local state. An id is valid in all processes, a pointer returned by a getter only in the process which called it.
- Only lock-free 32 bit atomics. `NameListInit` is called once by the process which creates the shared memory, before other processes attach. Attaching processes do not call it.
- All processes must be built for the same ABI (size and representation of the 32 bit atomics, endianness).
- Registration is process safe without further measures. A list which uses this module does not have the restriction of the existing lists ("two application processes must not create contexts or events at the same time").

### 5.6 32 bit microcontrollers and FreeRTOS

The module is written for 32 bit targets as well: it uses only 32 bit atomics (load, exchange, compare-and-swap), no 64 bit atomics, no operating system calls, no heap, no thread local storage, and nothing in it depends on the size of a pointer. The header is 64 bytes on 32 and 64 bit platforms. The only 64 bit arithmetic is the size check in `NameListInit`.

What the compiler generates for the atomics depends on the CPU (`-O2`, FreeRTOS branch of `platform.h`):

| Target | Atomic operations | Code size |
|---|---|---|
| Cortex-M3/M4/M7/M33 (STM32, arm-none-eabi-gcc 12.3) | inline, LDREX/STREX | 1.1 KB |
| ESP32, ESP32-S3 (Xtensa LX6/LX7, gcc 14.2) | inline, S32C1I | 1.2 KB |
| RISC-V with atomic extension (rv32imac, gcc 8.4) | inline, LR/SC | 1.4 KB |
| RISC-V without atomic extension (rv32imc, ESP32-C3) | library calls `__atomic_compare_exchange_4`, `__atomic_exchange_4` | 1.4 KB |
| ESP32-S2 (Xtensa without S32C1I) | library calls | 1.2 KB |
| Cortex-M0/M0+ (ARMv6-M) | library calls | 1.3 KB |

- With inline atomics all statements of this document hold unchanged, also on multi core (FreeRTOS SMP, ESP32 dual core).
- With library calls the runtime library has to provide the two functions. ESP-IDF does, a bare metal newlib toolchain for Cortex-M0 normally does not. They are implemented with a short critical section (interrupts disabled, plus a spinlock on multi core). The algorithm above them is unchanged: no task ever waits for another task inside the name list. The term lock-free is then only true above the level of a single atomic operation.
- The module adds no new requirement to a port: `xcplite.c` and `cal.c` already use 32 bit compare-and-swap in the `rtos` configuration.

Notes for real time systems:

- **No priority inversion.** A low priority task which is preempted in the middle of a registration does not delay a high priority task which registers or reads. With a mutex this needs priority inheritance.
- **Compare-and-swap on LL/SC CPUs** (ARM LDREX/STREX, RISC-V LR/SC, ARMv8 without LSE) is itself a small retry loop: it is repeated when an interrupt or a context switch hits between load and store. This is true for every use of C11 atomics on these CPUs and is bounded by the interrupt rate, but it is the reason why "wait-free" is counted in atomic operations and not in instructions.
- **Interrupt service routines.** The functions do not block and may be called from an ISR when the atomics are inline or the library versions are ISR safe. `NameListRegister` contains a search with string compares, so it does not belong into an ISR for runtime reasons. Access by id is a single load.
- **Memory.** Static, size known at compile time, see chapter 7. The slot table and all cell members are aligned as on 64 bit platforms, the sizes in chapter 7 are identical.


## 6. Memory ordering

| Access | Ordering | Purpose |
|---|---|---|
| `slot[id]` compare-and-swap (publish) | release | makes the content of the cell visible |
| `slot[id]` load | acquire | every thread which sees the slot not empty sees the complete cell |
| `park[i]` compare-and-swap (give back) | release | hands over the ownership of the cell |
| `park[i]` exchange (take) | acquire | the new owner overwrites the cell after the old owner has finished |
| `park[i]` load before the exchange | relaxed | only avoids an exchange on an empty parking slot |
| `count` compare-and-swap (raise) | release | after the slots below have been loaded with acquire |
| `count` load in `NameListGetCount` | acquire | all entries below the count are visible to the caller |
| `alloc` | relaxed | a counter, exclusive ownership results from the atomic increment, new cells were never written by another thread |

There is no place which needs sequential consistency and no store which has to be seen by a later load of a different variable in another thread (no store-load pattern). On x86 the loads are plain loads and the compare-and-swaps are `lock cmpxchg`. On ARMv8 the loads are plain loads and the compare-and-swaps are LL/SC or `CAS` instructions. On 32 bit microcontrollers a 32 bit compare-and-swap is LDREX/STREX or the equivalent.

Reading the constant header parameters and the content of published cells needs no atomics: they are written before the list or the cell becomes visible.


## 7. Memory overhead

```
cell size = align8(4 + name_size) + align8(data_size)
list size = 64 + align8(4 * capacity) + (capacity + spare) * cell size        = NAMELIST_SIZE(capacity, spare, name_size, data_size)
```

Compared to a plain array `struct { char name[name_size]; data; } entry[capacity]` with a count:

- 64 bytes header, once.
- 4 bytes per entry for the slot table.
- 4 bytes per entry for the scope, plus the padding to a multiple of 8. A `name_size` with remainder 4 modulo 8 (12, 20, 28, ...) has no padding: `name_size` 28 gives a 32 byte cell, `name_size` 32 a 40 byte cell.
- `spare` additional cells, once.

| List | capacity | spare | name_size | data_size | Cell | Total | Plain array |
|---|---|---|---|---|---|---|---|
| Span names as today | 32 | 8 | 32 | 0 | 40 | 1792 | about 1030 |
| Span names, name_size reduced | 32 | 8 | 28 | 0 | 32 | 1472 | about 900 |
| Contexts as today | 8 | 8 | 16 | 0 | 24 | 480 | about 130 |
| Medium list with data | 256 | 8 | 32 | 8 | 48 | 13760 | about 10250 |
| Large list with data | 1000 | 8 | 16 | 16 | 40 | 44384 | about 32000 |

For small lists the fixed part (header and spare cells) dominates, for large lists the overhead approaches 8 bytes per entry plus padding.

Code size: about 1.9 KB (arm64, clang -O2). No static data, no heap, stack usage is a few words.


## 8. Runtime cost

### 8.1 Complexity

| Operation | Cost | Remarks |
|---|---|---|
| `NameListGetName`, `NameListGetData`, `NameListGetScope` | O(1) | one atomic load more than a plain array: id -> slot -> cell |
| `NameListGetCount` | O(1) | load of the count and of one slot, more only directly after another thread was suspended inside a registration |
| `NameListRegister`, existing name | O(position) | one slot load and one `strncmp` per entry in front, no write to shared memory |
| `NameListRegister`, new name | O(count) | compares with all entries, writes one cell, 3 atomic read-modify-write operations (cell, slot, count) |
| `NameListRegister`, list full, new name | O(capacity) | compares with all entries on every call, the caller should remember the failure |
| `NameListFind` | O(position) or O(count) | |

A registration of an existing name only reads. Any number of threads can do this at the same time without disturbing each other: no cache line is written, nothing is contended. This is the main difference to the mutex version, where every lookup writes the mutex.

### 8.2 Measurements

MacBook Air M2 (4 performance + 4 efficiency cores), Apple clang 21, `-O2`, names like `span_name_17`, `name_size` 32, no user data, 2026-11-18. The baseline is the algorithm of `ctx.c`: a plain array with a count, linear search with `strcmp` under a `pthread_mutex`.

Single thread, time per call:

| Entries | Register existing name, average position | Register existing name, last position | Find, name not in list | Register new name, average | Mutex baseline, existing name, average position | GetName | GetCount |
|---|---|---|---|---|---|---|---|
| 8 | 51 ns | 51 ns | 44 ns | 97 ns | 34 ns | 1.5 ns | 1.1 ns |
| 32 | 121 ns | 212 ns | 168 ns | 297 ns | 100 ns | 1.6 ns | 1.2 ns |
| 128 | 430 ns | 836 ns | 505 ns | 819 ns | 355 ns | 1.5 ns | 1.1 ns |
| 1024 | 3.3 us | 7.3 us | 2.3 us | 3.7 us | 2.9 us | 1.7 ns | 1.5 ns |

8 threads register existing names at the same time, time per call:

| Entries | Name list | Mutex baseline |
|---|---|---|
| 8 | 53 ns | 787 ns |
| 32 | 178 ns | 2.1 us |
| 128 | 638 ns | 7.3 us |
| 1024 | 5.1 us | 35 us |

- Without contention the name list is 15 to 50 % slower than the mutex version: about 7 ns instead of 5.6 ns per compared entry, because of the indirection and the scope compare.
- With contention it is 7 to 15 times faster, and the time per call stays close to the single thread value (the efficiency cores are slower).
- Access by id costs about 1.5 ns.

These are micro benchmarks on one machine, they show the order of magnitude and the scaling, not more.

### 8.3 Consequences for the caller

- Register once and cache the id, for example in a static variable per call site. `NameListRegister` is not meant for a fast path.
- On fast paths which need the entry (not only the id), cache the pointer returned by `NameListGetData`. It is valid forever and saves the slot load.
- A failed registration (list full) should be cached as well, otherwise every call searches the whole list.


## 9. API and usage

### 9.1 Example

```c
#include "namelist.h"

typedef struct {
    uint32_t cycle_time_ns;
    atomic_uint_least32_t trigger_count; // Modified after creation, therefore atomic
} tMyData;

// Memory for 64 entries, 8 spare cells, names up to 31 characters
static NAMELIST_STORAGE(64, 8, 32, sizeof(tMyData)) gMyList;

void init(void) {
    // Once, before any other thread or process uses the list
    NameListInit(&gMyList.list, sizeof(gMyList), 64, 8, 32, sizeof(tMyData));
}

uint32_t create(const char *name, uint32_t cycle_time_ns) {
    tMyData data = {.cycle_time_ns = cycle_time_ns};
    bool created;
    uint32_t id = NameListRegister(&gMyList.list, 0 /* scope */, name, &data, &created);
    if (id == NAMELIST_UNDEFINED) { /* list is full */ }
    return id; // Same id for the same name, in all threads and processes
}

void iterate(void) {
    uint32_t n = NameListGetCount(&gMyList.list);
    for (uint32_t id = 0; id < n; id++) {
        const char *name = NameListGetName(&gMyList.list, id);       // Never NULL for id < n
        const tMyData *data = NameListGetConstData(&gMyList.list, id);
        // ...
    }
}
```

### 9.2 Sizing: capacity and spare

- `capacity` is the maximum number of entries.
- `spare` is the number of additional cells. A registration of a new name needs a free cell although the list is not full, and cells held by other threads in the same moment are not available. With `spare` >= (number of threads which register new names at the same time) - 1, all `capacity` entries can be registered. With less, a registration of one of the last entries may return `NAMELIST_UNDEFINED` although the list is not completely full. The list stays consistent in any case.
- Recommended: `spare` = number of threads which may register concurrently, 8 if unknown. A spare cell costs one cell size.

### 9.3 Rules

- **Init.** `NameListInit` exactly once, not concurrently, before the list is visible to other threads or processes. It clears the memory block. The block must be aligned to 8 bytes, `NAMELIST_STORAGE` takes care of this.
- **Names.** Zero terminated, not empty. Names longer than `name_size - 1` are truncated, two names which differ only behind this length are the same name. A caller which wants to reject long names checks the length before.
- **Scope.** Part of the key. Use 0 when there is only one namespace, the application id for names per application, the context id for names per context.
- **User data.** `data_size` bytes are copied into a new entry before it becomes visible (cleared when `data` is NULL). When the entry already exists, `data` is ignored and `created` is false. The data is aligned to 8 bytes. Members which are modified later must be protected by the user, for example as atomics. Members which are constant after creation can be read without protection.
- **Ids.** `uint32_t`, `NAMELIST_UNDEFINED` (0xFFFFFFFF) when there is no id. Callers which store ids in 16 bits limit `capacity` to 65535 or less.
- **Reserved ids.** An id with a special meaning (span id 0 "none") is registered first, directly after `NameListInit`.


## 10. Limitations and notes

- **No removal**, no renaming, no clearing while the list is in use. This is not an omission, the algorithm depends on it: slots and published cells never change.
- **Linear search by name.** Sufficient for some dozen to some hundred entries which are registered once. For much larger lists a hash index could be added in front (see chapter 11), the list itself would stay as it is.
- **One additional load for the access by id**, and the cells are not ordered by id in memory. Iterating over all entries jumps between cells in the order in which they were allocated, which is nearly the id order.
- **Early "full".** See 9.2: with fewer spare cells than concurrently registering threads, one of the last registrations may fail early.
- **Lost cells.** A cell is lost when a thread is killed while it holds one, or when more than 8 cells are parked at the same time. A lost cell reduces the spare cells, nothing else. There is no counter for lost cells, they can be computed in a quiet state: `alloc` - count - occupied parking slots.
- **A truncated name can hide a different name.** Same behaviour as the existing span list.
- **`created` is exact.** Exactly one call returns `created` = true for each entry, in all threads and processes. This can be used for one time initialization which must follow the registration, but such initialization is not atomic with the registration: other threads can see the entry before it is done.
- **Not a persistent format.** The layout depends on the ABI and contains cell numbers which depend on the history of races. To store a list, iterate by id and store names and data.
- **Signal handlers and interrupts.** The functions do not block and do not call the operating system, they may be used where blocking is not allowed. They are reentrant: a registration which interrupts another registration of the same thread is the same situation as two threads.
- **False sharing.** The header with the cached count and the parking slots is written by registrations and may share a cache line with the first slots. This only matters while new names are registered at a high rate, registrations of existing names do not write.


## 11. Integration status and next steps

The module is part of `libxcplite` (`src/namelist.c` in `xcplite_SOURCES`), `test/namelist_test` is registered with CTest (not on Windows).

### 11.1 Users

| List | Where | Parameters | Remarks |
|---|---|---|---|
| Context list | `tXcpData.context_list`, `src/ctx.c` | capacity `OPTION_DAQ_CONTEXT_COUNT` (8), 8 spare cells, name size 16, no user data: 480 bytes | Context id = list id + 1. The scope is the application id in SHM mode, 0 otherwise. `XcpCreateContext` no longer uses the event list mutex |
| Span name list | `tXcpData.span_list`, `src/ctx.c` | capacity `OPTION_DAQ_SPAN_COUNT` (32), 8 spare cells, name size 32, no user data: 1792 bytes | Only without `OPTION_DAQ_SPAN_SECTION_REGISTRATION` (SHM mode, Windows). Span id = list id, "none" is registered as id 0 by `XcpInitContextList` |

Notes:

- `XcpInitContextList` initializes the lists. It is called by `XcpInit`, in SHM mode by the leader only, before other threads or processes use the lists. `SHM_VERSION` was changed, because the layout of `tXcpData` is different.
- Creating an `xcp::Context` is still not lock-free as a whole: it also creates the `spans` event of the context (`XcpCreateContextEvent`, event list mutex) and registers measurements (A2L lock). The name list removes the mutex from the context list only. For the same reason, creating contexts is not process safe in SHM mode, although the context list itself is.
- `XcpDeinit` followed by `XcpInit` initializes the lists again. Ids which are cached at code locations (see 11.2) are then stale. This only happens in tests.
- Tested on macOS (arm64, Apple clang 21) and on Linux (Raspberry Pi, aarch64, GCC 12.2 and clang 14), see 13.8. Not tested: SHM mode with several processes, Windows (atomic emulation). The `shm` configuration builds, its examples do not use contexts.

### 11.2 No search at the code location

A registration is a linear search by name. It must not be executed each time a code location is executed, the id is cached at the code location:

| Object | Cache | Search |
|---|---|---|
| Span, link time registration | none needed, the id is computed from the address of the descriptor | never |
| Span, runtime registration | `DaqSpan` declares a `static std::atomic<uint16_t>` at the code location: 0 is "not registered yet", 0xFFFF is "registration failed" | once per code location, more than once only when several threads execute the code location for the first time at the same moment |
| Context, runtime name | the `xcp::Context` object holds the context id and the event id of its `spans` event | once per object, in the constructor |
| Context, literal name | `DaqContext(name)` declares a static `xcp::Context` at the code location, created by its first execution | once per code location, more than once only when several threads execute the code location for the first time at the same moment |
| Context event (`DaqEventCtxVar`) | a static array of states at the code location, indexed by the context id: new, claimed, failed, or the event id | once per code location and context, by the one thread which claims the state |

The span cache needs no lock and no once guard, because the registration is idempotent: every thread gets the same id for the same name, so all threads store the same value. Before this change, `DaqSpan` used a C++ function static with dynamic initialization (`static const uint16_t id = XcpRegisterSpanName(...)`), which also searched only once, but took a lock in `__cxa_guard_acquire` on the first execution, and cached "no span" for ever when it was executed before `XcpInit`. With the atomic cache nothing is cached before `XcpInit`.

For contexts the rule is: create the `xcp::Context` object once and keep it (class member, or in the thread function before the loop), do not construct it in a function which is called cyclically. For a context with a literal name which is entered at a code location, `DaqContext(name)` does this: it declares a static `xcp::Context` at the code location and attaches it with an `xcp::Scope` for the enclosing block.

The static context of `DaqContext` follows the same principle as the span cache:

- **No guard.** `xcp::Context` has a constexpr default constructor and is trivially destructible, so the static object is constant initialized. The macro requires this (`constinit`, or the equivalent attribute of clang and GCC 10 or later before C++20). There is no `__cxa_guard_acquire` at the code location.
- **Created on first execution.** `Context::Create` creates the context when its `created` flag is not set. Creating is idempotent: several threads which execute the code location for the first time at the same moment each create or find the context and its `spans` event and store the same values, exactly one of them registers the measurements. No thread waits for another one to finish. The members of the class are atomics for this reason.
- **Before `XcpInit`** nothing is created and nothing is cached, the context is created by the first execution after `XcpInit`. Until then the code behaves as without context.
- **Still not lock-free on the first execution**: creating the `spans` event takes the event list mutex and registering the measurements takes the A2L lock. Afterwards, entering the context is one load and the attach of the scope.

Which static is initialized under a guard depends on the compiler and the standard library, so the macros require constant initialization explicitly (`XCP_CONSTINIT`) instead of relying on it. Example found on Linux: `static std::atomic<bool> flags[N];` without initializer gets a guard with GCC 12 and libstdc++ in C++17 mode, but not with clang and not in C++20 mode. With `{}` it is constant initialized everywhere (the flag array in `contextRegisterOnce`).

`DaqEventCtxVar` is different, because registering the measurements of an event is not idempotent, it must happen exactly once per code location and context:

- **No guard.** The states of a code location are an aggregate without constructor (`ContextEventIds`), declared with `{}` and required to be constant initialized. State 0 is "new".
- **Claim instead of mutex.** The first thread which executes the code location in a context claims the state with a compare-and-swap (new to claimed), creates the event, registers the measurements and publishes the event id (event id + 3). A thread which finds the state claimed **skips this one trigger** and does not wait. This replaces a mutex and a double check.
- **Price.** A sample of the other threads is lost while the event is being created. If the claiming thread never finishes (killed), the event of this code location and context is never triggered, the other threads do not block.
- **Failure.** When the event can not be created (event list full), the state becomes "failed" and the code location does not try again in this context.
- **Still not lock-free for the claiming thread**: it takes the event list mutex and the A2L lock.

With this, no code location of the context API has a compiler generated guard or a mutex of its own: the test and demo binaries contain no guard variable (macOS, Linux with GCC and with clang).

### 11.3 Possible next steps

1. **Event list and calibration segment list**: these have large entries which are accessed on the measurement fast path. The entry would be the user data of the list, and the fast path would use a cached pointer or accept the additional load. Creation has side effects beyond the list (A2L, persistence), which have to be ordered behind the registration with the `created` result. A lock-free event list is what is missing for process safe creation of contexts and events in SHM mode.
2. The same once patterns in the event API outside the context API: `std::call_once` in `inc/xcplib.hpp` (`DaqTriggerVarTemplate`, `DaqEventVarTemplate`) and in `inc/a2l.hpp` blocks concurrent callers on the first execution of a code location. They were not changed.
3. Possible extensions, only when needed: a hash index for large lists, a diagnostic counter for lost cells, a `NameListRegister` variant with an initialization callback instead of a copy.


## 12. Test results

`test/namelist_test/src/main.c`, POSIX only (pthreads), self-checking. Build without CMake:

```bash
cc -std=c11 -O1 -g -fsanitize=thread -Isrc -Iinc src/namelist.c test/namelist_test/src/main.c -o build/namelist_test
./build/namelist_test [rounds]
```

Content:

- Single threaded: parameter checks, register, find, existing names, user data, scope, truncation, full list, list without user data.
- Concurrent: in each round 8 writer threads register the same 96 keys (24 names in 4 scopes) into an empty list, in the same or in different order, while 2 reader threads continuously check that the count does not decrease, that every entry below the count exists, is complete (the user data contains a hash of the key) and is found by its name under its own id. After each round: all writers got the same id for the same key, ids are unique and dense, every entry was created exactly once (`created`), the cached count is exact.
- Three variants: all keys fit, the list overflows (exactly `capacity` entries must exist), no spare cells (registrations may fail early, the list must stay consistent).

Results on a MacBook Air M2 (arm64, weak memory ordering), Apple clang 21, 2026-11-18:

| Build | Rounds per variant | Result |
|---|---|---|
| `-O1 -fsanitize=thread` | 300 | passed, no data race reported |
| `-O1 -fsanitize=address,undefined` | 300 | passed |
| `-O2` | 5000 | passed, 45 million entries checked by the readers, more than one creating writer in over 95 % of the rounds, 0 cells lost |

Two deliberately broken versions of the module are detected by the test: publishing with a plain store instead of the compare-and-swap (entries created twice), and skipping the compare with the winner after a lost compare-and-swap (duplicate names).

Not tested:

- Several processes on shared memory. The arguments in 5.5 apply, but there is no test.
- Killed threads. The table in 5.4 is an analysis, not a test result.
- 32 bit and FreeRTOS: `namelist.c` compiles without warnings for the targets in 5.6 (gcc, `-D_FREE_RTOS`, FreeRTOS headers replaced by stubs), and the generated atomics were inspected. It has not been linked into a firmware and not been run on a 32 bit target. The parameter checks of `NameListInit` for sizes which overflow 32 bit are only executed on the 64 bit host.
- x86, Windows (atomic emulation). The code uses only the atomics of `platform.h` which are available there. Besides macOS, the test has been run on Linux (Raspberry Pi, aarch64, 4 cores, GCC 12.2 and clang 14, 2026-11-18): passed with 2000 rounds per variant, and with `-fsanitize=address,undefined`. ThreadSanitizer does not run on this machine (unsupported address space size).
- More than 8 concurrent writers.


## 13. Alternative: link time registration in a linker section

This chapter evaluates a different approach for the case that the multi application requirement (15, shared memory) and the namespaces (22) are dropped. 13.1 to 13.7 are the analysis with throwaway prototypes. Variant A is implemented for span names, see 13.8.

Assumptions:

- One application, one linked image. No SHM mode, no scope.
- Names are string literals (or identifiers which the macro turns into a literal).
- FreeRTOS microcontrollers and POSIX (ELF and Mach-O).
- Same principle as events and calibration segments with `OPTION_SECTION_REGISTRATION` (sections `xcp_evts` and `xcp_cals`, see `docs/TECHNICAL.md`).

### 13.1 Principle

Each use site emits one constant name record into a dedicated linker section. The linker concatenates the records of all object files, the section is the list:

```c
typedef struct {
    char name[32];
} tNameRec;

extern const tNameRec __start_xcp_spans[] __attribute__((weak)); // ELF, Mach-O: section$start$__DATA$xcp_spans
extern const tNameRec __stop_xcp_spans[] __attribute__((weak));

#define SPAN_DECL(n)                                                                                    \
    static const tNameRec span__##n __attribute__((section("xcp_spans"), used, aligned(8))) = {#n};     \
    static_assert(sizeof(#n) <= sizeof(tNameRec), "name too long")
#define SPAN_ID(n) ((uint16_t)(&span__##n - __start_xcp_spans))
```

- id = position of the record in the section, count = `__stop_xcp_spans - __start_xcp_spans`, name = `__start_xcp_spans[id].name`.
- With fixed size records **no index list has to be built at initialization**. The section already is the indexed list. An initialization pass is only needed for unique ids (variant B in 13.3) or for records of variable size.
- A reserved id (span id 0 "none") is handled by using position + 1 as id, the position of a particular record in the section can not be controlled.

Consequences:

- **Nothing to protect.** The list is constant data. No registration at runtime, no atomics, no mutex, no once pattern. The original requirements 2, 3, 4, 8, 9 and 10 are fulfilled trivially, and the count is a link time constant.
- **No runtime state.** No RAM, no initialization, no capacity to configure (`OPTION_DAQ_SPAN_COUNT` would not be needed). Usable before `XcpInit`, in interrupts, on CPUs without compare-and-swap.
- **Cost at the use site.** The id is the difference of two addresses, a link time constant which the compiler can not fold: two address loads, a subtraction and a shift. Cortex-M4: 10 bytes of code and 8 bytes of literals. The present `DaqSpan` macro uses a C++ function static with dynamic initialization, which is a guard check on every execution, `__cxa_guard_acquire` and the mutex in `XcpRegisterSpanName` on the first one.
- **String literals are enforced.** A name which is not a literal can not initialize the record, a name which is too long fails the `static_assert`. Both are compile errors.
- **The list is complete.** It contains the names of all code which was linked, also of code which never executes. For spans the rule "must have been executed once before the A2L file is finalized" goes away.
- **Deterministic ids, no races.** The ids depend on the link order only, they are the same in every run of a binary and can be read from the ELF file. `xcpclient` can create the enum conversion offline in the same way as it reads `xcp_evts`. This would make spans possible in the `no_a2l` and `rtos` configurations.
- The id is not a constant expression in C (`static const uint16_t id = SPAN_ID(n);` is an error), so it is computed at each use, as the event id with GCC today.

### 13.2 Record layout

| Layout | Access | Remarks |
|---|---|---|
| Fixed size `char name[N]` (prototype) | id by address difference, name by index | No pointers and no relocations, read only, 32 bytes of flash per name. The tool splits the section into records |
| Descriptor with `const char *name`, like `tXcpEventDescriptor` | id by address difference, name by one more load | Strings of any length in `.rodata`, additional fields possible. Needs relocations in position independent executables (the reason for `__DATA` on Mach-O). The tool has to follow the pointer, which `xcpclient` does for events |
| Strings of variable length directly in the section | needs an index built at initialization and a lookup from address to id | The compiler may insert alignment padding between strings. Not recommended |

### 13.3 Duplicate names

The same name used at two places gives two records: two functions with `SPAN_DECL(calc)`, or a C `static inline` function in a header which is included by several files. This is the central design decision, the runtime list does not have this problem.

| Variant | Ids | Cost | Limits |
|---|---|---|---|
| A. Position is the id, duplicates tolerated | a name can have several ids | none at runtime | The enum conversion has the same text for several values, which is allowed in a `COMPU_VTAB`, but a value can not be selected by its name unambiguously |
| B. Unique ids assigned at initialization | unique, in the order of the first occurrence in the section | records in RAM with an id field which `XcpInit` writes, one load at each use | The id is 0 ("none") before `XcpInit`. `xcpclient` must apply the same rule |
| C. Duplicates removed by the linker | unique at link time | none at runtime | ELF only, the record is emitted with inline assembler, fragile, see below |

Variant B is single threaded at initialization and deterministic, there is still no race. It gives up the "no RAM" and "usable before `XcpInit`" properties.

Variant C emits the record as a COMDAT group whose key is the name, so the linker keeps exactly one record per name, independent of the compiler:

```c
#define SPAN_ASM(n)                                                                                     \
    ".ifndef xcp_span__" n "\n.pushsection xcp_spans,\"aG\",%progbits,xcp_span__" n ",comdat\n"        \
    ".weak xcp_span__" n "\n.balign 8\nxcp_span__" n ":\n.asciz \"" n "\"\n.org xcp_span__" n " + 32\n" \
    ".popsection\n.endif"
#define SPAN_DECL(n) __asm__(SPAN_ASM(#n)); extern const tNameRec xcp_span__##n __asm__("xcp_span__" #n)
#define SPAN_ID(n) ((uint16_t)(&xcp_span__##n - __start_xcp_spans))
```

It worked in the prototype (13.4), but only after three corrections, which shows how fragile it is:

- Without `.ifndef` the same name used twice in one file, or one template instantiated twice, is an assembler error.
- The section flag `R` (retain) is unknown to the assembler of the GCC 8.4 toolchains. Without it the records survive `--gc-sections` only because of the reference from `__start_xcp_spans`.
- GCC ignores the `__asm__` symbol name of a block scope `extern` declaration inside a template. The C identifier must therefore be identical to the symbol name, which probably fails for a template inside a namespace (not tried).

A Mach-O version would need different directives and was not tried.

### 13.4 Prototype results

All prototypes are small test programs outside the repository. Compilers: Apple clang 21 (macOS, arm64, executed), GCC 12.3 `arm-none-eabi` (Cortex-M4), GCC 14.2 `xtensa-esp-elf` (ESP32-S3), GCC 8.4 `riscv32-esp-elf` and `xtensa-esp32s3-elf`. The embedded builds were linked without startup code and the section content was inspected, nothing was executed on a target.

| Check | Result |
|---|---|
| C, two files, 7 records (macOS) | ids by position, count 7, names readable by id |
| Order of the object files swapped | all ids change, deterministic for a given link command |
| LTO and dead code removal (macOS: `-flto -Wl,-dead_strip`) | unchanged |
| No record at all (macOS, current linker) | links, count 0 |
| Name is not a literal, name too long | compile errors |
| AddressSanitizer on macOS | **broken**: red zones between the records, count 14 instead of 7, crash. Fixed with `__attribute__((no_sanitize("address")))` on the record |
| Cortex-M4, `-ffunction-sections -fdata-sections -Wl,--gc-sections`, default linker script, no `KEEP` | section kept with `__start_`/`__stop_` symbols, including the records of functions which were removed. GCC emits the records of one file in reverse source order |
| C++ inline function, template, in-class member function in two files, clang (macOS) | works, one record each, the copies are merged by the linker. Two instantiations of a template give two records with the same name |
| The same with GCC 8.4, 12.3, 14.2 | **compile error** "section type conflict", when one file has a record in such a function and another one in an ordinary function |
| Record as inline static member of a class template with the name as template argument (linker should merge by name) | clang: works. GCC 12.3: the section attribute is silently ignored, the records are in `.rodata` and the count is 0. GCC 14.2: records in the section, duplicates not removed, and the conflict with ordinary records remains |
| Variant C, 10 use sites of 7 names in three files (C and C++, inline function, template, in-class member function, lambda, C `static inline`) | 7 records with GCC 12.3 (also with `-flto`), 14.2 and 8.4 |

Not tested with the prototypes: Linux (GCC and clang, ELF with a dynamic loader, sanitizers; the implementation was tested there later, see 13.8), execution on a 32 bit target, the ESP-IDF linker script, MSVC, variant B, the pointer descriptor layout for names.

### 13.5 Limits

- **Runtime names are not possible.** `xcp::Context(const char *name)`, contexts which are recreated from the binary persistence file, `DaqCreateEvent_s` and indexed events use strings which do not exist at link time.
- **GCC and C++ inline code.** With the plain section attribute, a record inside an inline function, a template or a member function defined in the class body does not compile with GCC as soon as the same file has a record in an ordinary function. The error is loud, not a silent malfunction, and the same restriction already applies to `DaqCreateEvent` with section registration. For a C++ API such as `DaqSpan` this is the main practical obstacle. Only variant C removes it.
- **Sanitizers.** The record needs `no_sanitize("address")`, at least on macOS. The existing `xcp_evts` and `xcp_cals` records use the same pattern without it, whether they are affected was not checked.
- **One list per linked image.** Each shared library has its own section and its own boundary symbols, the ids of the executable and of a library would collide. For the same reason it can not be used in SHM mode, where each process would have its own ids.
- **Ids are not stable between builds.** They change with the link order, with added code and possibly with LTO. They are fixed for a binary, so the A2L file must belong to the binary, as with events. Ids must not be stored in persistent data.
- **Linker script.** The default GNU scripts keep the section and create the boundary symbols. A script which places sections explicitly needs a `KEEP` and the two symbols, for ESP-IDF this is one more block in `examples/freertos_demo/freertos_esp32_demo/extra_linker_script.py`.
- **Windows.** Not supported by XCPlite for section registration today. MSVC has an equivalent mechanism (`#pragma section`, `__declspec(allocate)`), which was not analyzed.
- **Alignment.** The record size must be a multiple of its alignment, and the alignment must be given explicitly, otherwise a compiler may align records more strictly and leave gaps in the section.

### 13.6 Comparison with the runtime list

| | Runtime list (chapter 3 to 5) | Link time section |
|---|---|---|
| Names | any string | string literals only |
| Thread safety | compare-and-swap, wait-free | constant data, nothing to protect |
| Unique names | yes | A no, B and C yes |
| Ids | creation order, depends on timing | link order, the same in every run |
| List complete without executing the code | no | yes |
| RAM | 1792 bytes for 32 span names | none (A, C), the records (B) |
| Flash | 1.1 to 1.9 KB code | 32 bytes per name, no code |
| First use of a name | linear search and 3 atomic operations | same as every other use |
| Atomics | 32 bit compare-and-swap | none |
| Offline A2L generation (`xcpclient`, ELF file) | no, the names exist only at runtime | yes |
| Shared memory, several processes | yes | no |
| Shared libraries | yes | no |
| Windows | yes | no |
| C++ inline functions with GCC | yes | only variant C |

Which objects fit:

| Object | Names | Link time registration |
|---|---|---|
| Span names | identifier in `DaqSpan(name)` | fits well |
| Events, calibration segments | identifier in `DaqCreateEvent`, `CalSegDecl` | exists (`OPTION_SECTION_REGISTRATION`), runtime variants remain |
| Contexts | runtime string, per instance or per thread | only for a new macro with literal names |

The two approaches can be combined, as the event list already does with `OPTION_SECTION_REGISTRATION` and `OPTION_DAQ_EVENT_LIST`: `XcpInit` registers the records of the section in the runtime list, single threaded and therefore with deterministic ids 0..n-1, and runtime names are appended behind them.

### 13.7 Conclusion

- For span names, link time registration is simpler and cheaper than any runtime list, and it makes spans possible without on-target A2L generation. Variant A is the natural start, it is the scheme which events already use.
- It does not replace a runtime list: contexts with runtime names, SHM mode and Windows still need one.
- Open decisions: how to treat duplicate names (A, B or C), and whether `DaqSpan` must work in C++ inline functions with GCC. If it must, variant C is the only way found, at the price of an assembler level macro for ELF and a second implementation for Mach-O.

### 13.8 Implementation for span names

Variant A is implemented for span names with the option `OPTION_DAQ_SPAN_SECTION_REGISTRATION` (`src/xcplib_cfg.h`, defined by default).

- **Availability.** ELF and Mach-O, with `OPTION_DAQ_CONTEXT_LIST`, not in SHM mode. Where it is not available (Windows, `shm` configuration) the option is undefined automatically and the span names are registered at runtime in a name list, see 11.1.
- **Descriptor.** `tXcpSpanDescriptor` is 32 bytes, the span name with terminator and nothing else. Section `xcp_spans` (`__DATA,xcp_spans` on macOS), attribute `used`, alignment 8, and `no_sanitize("address")` with clang.
- **Macros** in `inc/xcplib.h`: `XCP_SPAN_SECTION_DECL(name)` declares the descriptor inside a function, a name with more than 31 characters is a compile error. `XCP_SPAN_SECTION_ID(name)` is the span id, position + 1. `DaqSpan(name)` in `inc/xcplib_context.hpp` uses both.
- **Library** (`src/ctx.c`): `XcpGetSpanCount` and `XcpGetSpanName` read the section, span id 0 is "none". The section is located once in `XcpInitContextList`: with the weak boundary symbols on ELF, with `getsectiondata` on macOS, so a program without any span links on both. `XcpRegisterSpanName` does not exist in this mode. The A2L writer is unchanged, it creates `conv.spans` from these two functions.
- **Duplicates.** A span name used at several code locations has several span ids with the same text in `conv.spans`. This is harmless for the measurement, each value is displayed with its name. A tool can not select one of the code locations by the name.
- **Number of span names.** Not limited by a configuration parameter (`OPTION_DAQ_SPAN_COUNT` is not used), the upper limit of the `span` measurement is the size of the section.
- **GCC.** `DaqSpan` in an inline function, a template or a member function defined in the class body is a compile error ("section type conflict") as soon as the same file has a `DaqSpan` in an ordinary function, see 13.4 and 13.5. A file which has spans only in such functions compiles, but GCC then puts all its descriptors into one COMDAT group. This gave duplicate descriptors in the prototype and can not be relied on. Rule: with GCC, use `DaqSpan` in ordinary functions only, or undefine the option.
- **Shared libraries.** The library reads the section of the executable. Spans in a shared library, or `libxcplite` itself built as a shared library, are not supported.

Verification:

| Check | Result |
|---|---|
| All 12 CTest tests (macOS arm64 with Apple clang 21, Linux aarch64 with GCC 12.2 and with clang 14) | passed |
| `context_test` with runtime registration (option undefined with a configuration override), macOS and Linux with GCC | passed |
| `context_test` and `namelist_test` with `-fsanitize=address,undefined` (macOS, Linux with GCC and with clang) | passed |
| `context_test` and `namelist_test` with `-fsanitize=thread` (macOS) | passed. ThreadSanitizer does not run on the Linux test machine |
| `context_test` repeated (200 times on macOS, 200 times on Linux with GCC and with clang, 30 times with ThreadSanitizer on macOS). It includes 4 threads which enter the same `DaqContext` code location and trigger the same `DaqEventCtxVar` for the first time at the same moment, and 100 rounds of 4 threads racing for a context event | no failure |
| `thread_context_demo`: first run, then a run with the A2L file of the first run (`A2L_MODE_WRITE_ONCE`), measured with `xcpclient` (macOS, and Linux with GCC measured over the network) | 8 span names in the section, `conv.spans` complete in the first run, each context shows exactly the span names of its code, including the startup span. The span ids differ between the clang and the GCC binary, as expected |
| Guard variables in the `context_test` and `thread_context_demo` binaries (macOS, Linux with GCC and with clang) | none. The demo no longer references `__cxa_guard_acquire` |
| `DaqSpan` in a member function defined in the class body plus one in an ordinary function of the same file (Linux) | GCC 12.2: compile error "section type conflict", clang 14: compiles |
| Configurations `shm`, `no_a2l`, `rtos` (macOS), `shm` (Linux) | build, the option is undefined automatically |
| The span macros and the section reader, extracted from the sources, compiled and linked with GCC 12.3 (Cortex-M4), 14.2 (ESP32-S3), 8.4 (RISC-V), `-Wall -Wextra -pedantic`, `--gc-sections` | no warnings, 4 descriptors from 4 code locations (one name twice), a program without spans links |

Not tested: Windows (fallback to runtime registration), SHM mode with contexts, x86, `XCP_SPAN_SECTION_DECL` used from C code inside the project (the prototypes in 13.4 are C).

The demo did not compile on Linux before: its helper function `sleep(uint32_t)` conflicted with `sleep` of the C library. It is renamed to `sleep_us`, the complete default configuration (examples and tests) now builds on Linux with GCC and with clang.
