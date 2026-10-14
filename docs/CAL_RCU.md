# XCPlite memory-safe, lock-less and wait-free calibration data access

This document describes XCPlite's lock-less and wait-free access to calibration parameters: the contract the application has to follow, the guarantees and compromises of the implementation, the algorithm and its memory ordering, test results, known limitations and proposals for improvement.

The base mechanism is not specific to XCP. It can be used wherever software parameters are modified by one thread and read by many, and the readers must be memory-safe, lock-less and wait-free: a REST API, a web server, test stimulation or a shared memory interface. XCPlite adds the XCP specific features (page switching, freeze, copy, init, atomic transactions for consistency) on top of it.

Terminology: a *calibration segment* is a calibration parameter block which represents an XCP/A2L MEMORY_SEGMENT and implements the XCP specific functionality. A *calibration block* wraps a calibration parameter struct without a MEMORY_SEGMENT. Both use the same mechanism; where the difference does not matter, this document says segment.

Chapter 2 is the complete user contract. Chapters 4 and 5 are the technical background and are not required to use the API.

XCPlite has two RCU algorithms for calibration segments, selected at compile time with `OPTION_CAL_RCU_REFCOUNT` in `xcplib_cfg.h` (default: undefined, lock count algorithm; per page reference counting is only enabled for special needs). The reader API and the user contract are identical for both, chapter 3 lists where they differ.

Contents:
1. Functional overview
2. User contract
3. Guarantees and compromises
4. Algorithm
5. Memory ordering
6. Test results
7. Known limitations and open issues
8. Proposals for improvement
9. Change history


## 1. Functional overview

### 1.1 Roles

- **Writer**: exactly one thread modifies calibration data. In XCPlite this is the XCP server thread, which handles the XCP commands. The writer works on a private page and publishes its changes.
- **Readers**: any number of application threads read calibration data. A reader takes a lock, receives a pointer to a consistent snapshot, uses it and releases the lock. Lock and unlock are wait-free: a reader is never blocked, neither by the writer nor by other readers.

### 1.2 Application (reader) API

C API (`xcplib.h`):

- `XcpLockCalSeg(index)` returns a pointer to the active page of the segment (working page or reference page, as selected by the XCP tool). The pointer is valid until the matching unlock.
- `XcpUnlockCalSeg(index)` releases the lock. With `OPTION_CAL_RCU_REFCOUNT` the signature is `XcpUnlockCalSeg(index, page)`, `page` is the pointer returned by the matching lock.
- The macros `CalSegLock(name)`/`CalSegUnlock(name)` and `CalBlkLock(name)`/`CalBlkUnlock(name)` (`CalSegUnlock(name, ptr)`/`CalBlkUnlock(name, ptr)` with `OPTION_CAL_RCU_REFCOUNT`) are typed wrappers, which also handle passive mode (see 2.1).

C++ API (`xcplib.hpp`): `xcp::CalSeg<T>`, `xcp::CalBlk<T>` and `xcp::CalSegRef<T>` provide `lock()`, which returns a RAII guard `CalSegGuard` with pointer semantics. The guard releases the lock in its destructor and is not copyable.

### 1.3 Creation and registration

Segments are created and registered in a global list by the application threads:

- `XcpCreateCalSeg` creates an XCP/A2L MEMORY_SEGMENT with all related XCP features (page switching, freeze, copy, init, ...).
- `XcpCreateCalBlk` creates a calibration block without a MEMORY_SEGMENT.
- The C macros `CalSegCreate`/`CalBlkCreate` (immediate creation) and `CalSegDecl`/`CalBlkDecl` (section registration, created by `XcpInit()`) wrap these, see `xcplib.h`.

Creation is lock-free but not wait-free (a CAS loop in a bump allocator). The one time registration of a new segment in the global list is protected by a mutex. Duplicate names are not allowed, the registry returns the existing segment. Registration is required because the XCP server iterates over all segments for XCP commands and for A2L generation.

### 1.4 XCP server side API

Used by the writer thread only:

- Page handling: `XcpCalSegSetCalPage`, `XcpCalSegGetCalPage` (reference/working page switching), `XcpCalSegCopyCalPage` (copy reference to working page), `XcpSetCalSegMode` (freeze request), `XcpGetCalSegMode`, `XcpGetSegInfo`, `XcpGetSegPageInfo`.
- Data access: `XcpCalSegWriteMemory`, `XcpCalSegReadMemory`.
- Consistency: `XcpCalSegBeginAtomicTransaction`, `XcpCalSegEndAtomicTransaction`.
- Publishing: `XcpCalSegPublishAll(wait)`, called non blocking from `XcpBackgroundTasks()` and blocking at the end of an atomic transaction and from `XcpDisconnect()`.

An atomic transaction is a lock-less, wait-free global operation without any runtime cost for the readers: writes are collected in each segment's writer page until the transaction ends and are then published together. No finer grained locking or synchronization is needed for calibration updates.


## 2. User contract

This chapter is the complete list of rules the application has to follow. All guarantees in chapter 3 are conditional on them. The rules are checked in debug builds only, see 2.5.

### 2.1 Initialization, activation and lifetime

1. `XcpInit()` must have completed before any calibration segment is created or locked and before any other XCP function is called.
2. The activation state is decided once in `XcpInit()`: `XCP_MODE_DEACTIVATE` initializes XCP in passive mode, every other mode activates it. The state does not change until `XcpDeinit()`. There is no runtime deactivation.
3. `XcpDeinit()` may only be called when no thread is inside a lock and no thread will lock afterwards. This is the same rule that applies to `pthread_mutex_destroy()`. Debug builds assert that the lock count of every segment is zero.
4. Passive mode: no segments are created, `XcpCreateCalSeg`/`XcpCreateCalBlk` return `XCP_UNDEFINED_CALSEG`. The macros `CalSegLock()`/`CalBlkLock()` and the C++ wrappers then return a pointer to the default page instance without calling into the library, and their unlock does nothing. This also applies to `CalSegDecl` segments before `XcpInit()` has run its section scan. `XcpLockCalSeg()`/`XcpUnlockCalSeg()` must not be called directly in passive mode or with `XCP_UNDEFINED_CALSEG`.

### 2.2 Single writer

5. Exactly one thread writes calibration data and uses the server side API of 1.4. In XCPlite this is the XCP server thread. `XcpBackgroundTasks()` must be called cyclically from this thread.
6. `XcpDisconnect()` publishes pending changes and therefore belongs to the writer thread as well, or must be called when the XCP server thread is not running (@@@@ TODO: clarify, see limitation 7.4).

### 2.3 Reader locking rules

7. The pointer returned by a lock is valid until the matching unlock. It must not be stored beyond the unlock and calibration data must not be written through it.
8. Every lock is released exactly once, with the pointer it returned, by the thread which acquired it. Locks are balanced per thread, like the read side of a rwlock. The pointer is the reader's handle to its lock, there is no thread local state in the library.
9. Locks may be nested (recursive) and may be held by any number of threads at the same time, up to a total of 65535 locks per segment. There is no owner tracking.
10. A nested lock is not guaranteed to return the same snapshot as the outer lock: every lock returns a pointer to a valid and consistent page, but a nested lock may return the next published page.
11. Lock and unlock are wait-free and may be called from any thread at any priority, including real-time threads. Critical sections should be short: calibration changes become visible to all readers only when the lock count of the segment returns to zero (see 3.2).
12. C++: `CalSegGuard` is not copyable and must be held in a scope. Pass the pointer or the reference down, not the guard.

### 2.4 Contract violations and their consequences

- Direct `XcpLockCalSeg()`/`XcpUnlockCalSeg()` calls before `XcpInit()`, in passive mode or with an invalid index: assertion in debug builds, undefined behavior in release builds.
- `XcpDeinit()` while a lock is held: assertion in debug builds. In release builds the lock is leaked (see below).
- **Leaked lock**: a lock which is never released, because the thread was terminated or cancelled inside its critical section, because of an early return between `CalSegLock()` and `CalSegUnlock()`, or because the thread blocks forever inside the section. The consequences are contained: the readers of the segment are not affected, lock and unlock keep working, memory safety is intact and nothing is ever reclaimed. But publishing stops: with the lock count algorithm immediately, with reference counting when the pinned page is the only remaining candidate (immediately with 3 pages, after one more leaked reader per additional page). The readers keep the last published page, no further calibration change becomes visible. The writer sees this as permanently pending writes: in lazy mode `XcpCalSegWriteMemory` still returns `CRC_CMD_OK` to the tool and a read-back shows the written value, while `XcpBackgroundTasks()` retries forever and logs a warning. At the end of an atomic transaction and in `XcpDisconnect()` the writer stalls for `XCP_CALSEG_AQUIRE_FREE_PAGE_TIMEOUT` (500 ms) per pending segment and then returns `CRC_ACCESS_DENIED`. In SHM mode the lock count is in shared memory, a leak from a crashed process persists until the shared memory is cleared with `shmtool` (@@@@ TODO).
- **Unbalanced release**: an unlock without a matching lock or with a wrong page pointer, for example by copying a guard, or by unlocking after a direct `XcpLockCalSeg()` call which returned NULL. This is **not contained**: it can bring the lock count to zero while another reader is inside its critical section, and the page this reader is using can then be handed to the writer and overwritten. This is why `CalSegGuard` is not copyable and why the C macros must be paired in the same scope.
- More than 65535 locks on one segment (lock count algorithm) or on one page (reference counting): the count wraps, with the same consequences as an unbalanced release.

### 2.5 How the contract is checked

The contract checks (activation state, index range, unlock without lock or with a wrong page pointer, locks held in `XcpDeinit()`) are `assert()` statements with diagnostic output, compiled out with `NDEBUG` (Release and RelWithDebInfo builds). They add no cost to release builds. The memory ordering of the algorithm (chapter 5) is not a check, it is part of the mechanism and always active.

There is no runtime detection of leaked locks or unbalanced releases in release builds. The implementation has no thread identity, a lock count of two is indistinguishable from two threads or one nested lock. How the XCP tool can be informed about a stalled segment is an open topic (@@@@ TODO see 8.5).


## 3. Guarantees and compromises

The two RCU algorithms:

- **Per page reference counting** (`OPTION_CAL_RCU_REFCOUNT` defined, not default, for special needs): each RCU page has a reference count. A reader pins the published page for the duration of its lock, the writer publishes into any page which is neither the published page nor pinned. Publishing is independent of reader progress. `XCP_CALSEG_RCU_PAGES` pages, 3 to 6.
- **Lock count and page hand-over** (`OPTION_CAL_RCU_REFCOUNT` undefined, default): one lock count per segment. The reader which takes the first lock takes a published page over and hands the old page back to the writer, the next first lock confirms it as unused. Publishing depends on the lock count returning to zero. 3 pages.

Both were derived from the same requirements, the second one is the original XCPlite implementation. The reader API and the user contract are identical, chapter 4 describes both algorithms.

### 3.1 Guarantees

Under the contract of chapter 2, both algorithms guarantee:

- **Memory safety**: a reader holding a lock reads a page which is not modified concurrently. Torn reads are impossible.
- **Consistency**: every lock returns a snapshot which respects atomic transactions. Either all writes of a transaction are visible in a snapshot, or none of them.
- **Readers never wait for another thread.** With the lock count algorithm, lock and unlock are wait-free: one atomic read-modify-write each, plus a few loads and stores. With reference counting the unlock is wait-free and the lock is lock-free: it retries when a publish lands between its two loads of the published page index, a window of a few nanoseconds. Publishes on one segment are separated by at least one XCP command or one background cycle, so in practice a lock retries at most once.
- **Bounded memory**: 3 pages (lock count) or `XCP_CALSEG_RCU_PAGES` pages (reference counting) per segment, independent of the number of reader threads. Classic RCU implementations need memory proportional to the number of readers.
- **Data is never lost**: a write which is acknowledged to the tool stays in the writer page until it is published. A stalled publish delays the visibility, it does not lose the data.

### 3.2 Compromises

Common to both algorithms:

1. **Exactly one writer thread.**
2. **Publishing may be delayed and may time out.** When no page is available, changes accumulate in the writer page and are published together (not sequentially) as soon as a page is available. In lazy mode (`XCP_ENABLE_CALSEG_LAZY_WRITE`, the default) the writer retries in `XcpBackgroundTasks()`. A blocking publish (end of an atomic transaction, `XcpDisconnect()`, every write without lazy mode) waits up to `XCP_CALSEG_AQUIRE_FREE_PAGE_TIMEOUT` (500 ms) per segment and returns `CRC_ACCESS_DENIED`, which delays the XCP command response by that time.
3. **Acknowledge before visibility.** In lazy mode a write is acknowledged to the tool (`CRC_CMD_OK`) before it is visible to the readers. `XcpCalSegReadMemory` reads from the writer page, so a read-back verifies the written value even while it is still pending. There is no risk of failure after the acknowledge, see 3.1.
4. **Creation uses a mutex.** Registration of a new segment is protected by a mutex to keep the segment list consistent between threads. This is a one time cost per segment, not on the reader path.
5. **Memory.** Each segment needs a 64 byte header plus its RCU pages (3, or `XCP_CALSEG_RCU_PAGES`), plus a copy of the default page unless the default page has static lifetime (`XCP_ENABLE_ABS_ADDRESSING` with `XCP_ADDR_EXT_ABS == 0`). The copy is mandatory in SHM mode. The page size is rounded up to 8 bytes. The segment name is limited to `XCP_MAX_CALSEG_NAME` characters (23 on 64 bit, 27 on 32 bit platforms), because the header is padded to exactly 64 bytes.
6. **Background processing.** Lazy publishing needs `XcpBackgroundTasks()` to be called cyclically in the writer thread. With blocking sockets this requires a receive timeout (`SO_RCVTIMEO`), which was difficult to abstract over all platforms. Non blocking sockets with a waitable event would be a better approach.
7. **Nested locks are not coherent.** See contract rule 10. This is the price for not needing thread local reader state.

The two algorithms differ in when they can publish and when a change becomes visible:

| | Lock count and page hand-over | Per page reference counting |
|---|---|---|
| Publishing is possible when | a free page exists and is confirmed as unused, which needs two first level locks (lock count 0 to 1) since the previous publish: one hands the page over, the next one confirms it | a candidate page is not pinned, which means no reader still holds a page published before the previous publish. With 3 pages: no reader holds the page before the current one |
| A published change is visible | with the next first level lock. Together with the confirmation above, a write becomes visible with the first or the second first level lock after it | with the next lock, on any thread, regardless of locks held by other threads |
| Readers delay publishing | while any lock is held on the segment, including readers which entered after the publish and read the current page | only readers which still hold the previously published page |
| Segment which is never locked | accepts exactly one publish, afterwards its writes stay pending and blocking publishes time out | publishes without limit |
| Leaked lock (contract violation) | publishing stops permanently | the pinned page is retired. With 3 pages publishing stops, each additional page tolerates one more leaked reader |
| Reader lock | wait-free | lock-free, see 3.1 |
| Memory | 64 bytes + 3 pages | 64 bytes + `XCP_CALSEG_RCU_PAGES` pages, the header is the same size |


## 4. Algorithms

### 4.1 Pages and shared state

Each segment has RCU pages in addition to the default (reference) page. Pages are stored as offsets into the segment memory, not as pointers, so a segment can be placed in shared memory and used by several processes. All state is in the 64 byte segment header, one cache line, and the RCU state of both algorithms occupies the same 16 bytes of it.

Common to both algorithms:

- `xcp_page` - the private page of the writer, it accumulates all changes. Writer private, like `xcp_access` and `write_pending`.
- `ecu_access` (atomic) - selects the working page or the immutable default page for the readers, written by the writer on `XcpCalSegSetCalPage`.

Lock count and page hand-over:

- `ecu_page` (atomic) - the page the readers currently use, written by the reader which takes the first lock
- `ecu_page_next` (atomic) - the page published by the writer, taken over into `ecu_page` by the first lock
- `free_page` (atomic) - the page handed back by the readers, the reclamation list with exactly one element
- `lock_count` (atomic, 16 bit) - number of locks held on the segment by all readers
- `free_page_hazard` (plain bool, ordered through `free_page`, see 5.3) - the free page is not confirmed as unused yet

Per page reference counting:

- `published_page` (atomic) - index of the published page, written by the writer only. RCU page 0 is the writer page, it is never published.
- `page_refs[XCP_CALSEG_RCU_PAGES]` (atomic, 16 bit each) - number of readers pinning each page

### 4.2 Lock count and page hand-over

Three RCU pages: `ecu_page`, `xcp_page` and `free_page`. The algorithm is an RCU pattern with exactly one element in its memory reclamation list, the free page. Without a free page, changes simply accumulate in the writer page. This is also how atomic transactions are implemented: while a transaction is open, publishing is suspended and the writer page collects all writes.

```
// Multithreaded lock, wait-free
function lock(segment) {
    old = lock_count.fetch_add(1, acquire)
    if (old == 0) {                                   // First level lock: this thread does the hand-over
        next = ecu_page_next.load(acquire)
        cur  = ecu_page.load(relaxed)                 // No other thread stores ecu_page while we hold the first lock
        if (cur != next) {                            // A new page has been published
            free_page_hazard = true                   // The old ecu page may still be in use by a reader which locked before us
            ecu_page.store(next, release)             // Take over the new page
            free_page.store(cur, release)             // Hand the old page to the writer, this also publishes free_page_hazard
        } else {
            free_page_hazard = false                  // Lock count was 0 and nothing is pending: nobody uses the free page anymore
        }
    }
    if (ecu_access == default_page) return default_page
    return ecu_page.load(acquire)                     // May be a newer page than an outer lock got, see contract rule 10
}

// Multithreaded unlock, wait-free
function unlock(segment, page) {
    lock_count.fetch_sub(1, release)                  // The page is not needed, the lock count is per segment
}

// Single threaded publish
// Returns true when the changes in xcp_page have been published, they become visible with the next first level lock
function try_publish(segment) -> bool {
    page = free_page.load(acquire)
    if (page == NULL || free_page_hazard) {
        return false                                  // No free page or not yet confirmed, the changes stay pending in xcp_page
    }
    free_page.store(NULL)
    memcpy(page, xcp_page)                            // The new writer page starts with the current content
    old = xcp_page
    xcp_page = page
    ecu_page_next.store(old, release)                 // Publish
    return true
}
```

Hand-over sequence, starting from a quiet state (`ecu_page == ecu_page_next`, a confirmed free page):

1. The writer writes into `xcp_page` and publishes: the free page becomes the new `xcp_page`, the old `xcp_page` is announced in `ecu_page_next`.
2. The next first level lock sees `ecu_page != ecu_page_next`, takes the new page over and hands the old ECU page to `free_page`. It sets `free_page_hazard`, because a reader which locked concurrently (lock count already 1, before the hand-over stores) may still use the old page. This lock and all following locks return the new page: the change is visible.
3. The next first level lock sees `ecu_page == ecu_page_next` and clears `free_page_hazard`. The lock count was zero, so every reader of the old page has released its lock. The free page is confirmed and the writer may publish again.

Step 3 is the reason for the "second lock" in the table of 3.2: a write which arrives between step 2 and step 3 cannot be published until step 3 has happened.

### 4.3 Per page reference counting

`XCP_CALSEG_RCU_PAGES` RCU pages: page 0 is the writer page and is never published, one page is published, the others are reclamation candidates. Reclamation is driven by the reference count of each page instead of by reader progress.

```
// Multithreaded lock, lock-free
function lock(segment) {
    if (ecu_access == default_page) return default_page       // The default page is immutable, no pin needed
    loop {
        idx = published_page.load(seq_cst)
        page_refs[idx].fetch_add(1, seq_cst)                  // Pin
        if (published_page.load(seq_cst) == idx) {            // Still published: the page is complete and the writer will not target it
            return page(idx)
        }
        page_refs[idx].fetch_sub(1, seq_cst)                  // A publish landed in between, unpin and retry
    }
}

// Multithreaded unlock, wait-free
function unlock(segment, page) {
    if (page == default_page) return                          // Nothing was pinned
    page_refs[index_of(page)].fetch_sub(1, seq_cst)           // Unpin, the page pointer identifies the pinned page
}

// Single threaded publish
// Returns true when the changes in xcp_page have been published, they are visible with the next lock
function try_publish(segment) -> bool {
    cur = published_page.load(seq_cst)
    target = NONE
    for idx in candidates after cur, round robin, skipping page 0 and cur {   // A retired page is not reused before the other candidates
        if (page_refs[idx].load(seq_cst) == 0) { target = idx; break }
    }
    if (target == NONE) return false                          // Every candidate is pinned, the changes stay pending in xcp_page
    memcpy(page(target), xcp_page)                            // Copy first ...
    published_page.store(target, seq_cst)                     // ... then announce
    return true
}
```

Why it is correct:

- The writer never selects the published page as a target, and it announces a page only after the copy into it is complete.
- A reader uses a page only after re-validating that it is still the published page. If the validation reads an old publication of the page, the reader's pin precedes the writer's reference count check in the total order of the sequentially consistent operations, so the writer sees the pin and does not select the page. If it reads a new publication, the copy is complete. See 5.6 for the ordering argument and for why no generation counter is needed.
- The reader's handle to its pin is the page pointer it received, so no thread local state is needed. Nested locks pin independently, they may return different pages (contract rule 10).
- With 3 pages the memory is the same as for the lock count algorithm. Each additional page tolerates one more reader which never unlocks and one more concurrently pinned page.

### 4.4 Lazy mode, blocking mode and atomic transactions

Common to both algorithms:

- **Lazy mode** (`XCP_ENABLE_CALSEG_LAZY_WRITE`, default): `XcpCalSegWriteMemory` tries a non blocking publish and returns `CRC_CMD_OK` in any case. A failed publish leaves the segment marked `write_pending`, and `XcpBackgroundTasks()` retries in every cycle. A warning is logged when a publish stays pending for more than 200 ms.
- **Blocking mode**: every write waits for its publish, up to the timeout, and returns `CRC_ACCESS_DENIED` on timeout.
- **Atomic transactions**: `XcpCalSegBeginAtomicTransaction` suspends publishing, all following writes accumulate in the writer pages. `XcpCalSegEndAtomicTransaction` publishes all segments with pending writes, blocking with the timeout per segment. Changes from earlier writes which were still pending when the transaction began stay pending and are published together with the transaction.
- **`XcpDisconnect()`** publishes all pending writes, blocking.

In `cal.c` the two algorithms are the functions `CalSegRcuReset`, `CalSegRcuInit`, `CalSegRcuLock`, `CalSegRcuUnlock`, `CalSegRcuTryPublish` and `CalSegRcuIsUnlocked`, everything else is common.


## 5. Memory ordering

Sections 5.1 to 5.5 describe the lock count algorithm, 5.6 the reference counting algorithm, 5.7 the preconditions on microcontrollers with FreeRTOS SMP.

The lock count algorithm has two independent synchronization relations: between the readers (who is the last reader of a page) and between the writer and the readers (page content and page hand-over). Both need the ordering described here. The cost is one acquire load and the ordered flavour of two atomic read-modify-write operations per lock/unlock pair, there is no barrier instruction in the reader path.

### 5.1 lock_count

The increment in `lock` is `acquire`, the decrement in `unlock` is `release`. This is the usual pairing for a reference count: the increment orders the announcement of the lock before the reader's loads of the page, the decrement orders the reader's loads before the release of the lock. A thread which observes a lock count of zero with its acquire increment therefore synchronizes with all previous unlocks and knows that every previous reader has completed its page accesses. Without this ordering, a weakly ordered CPU may satisfy a reader's page loads before its increment is visible, or after its decrement is visible, and the hand-over logic would consider a page free while it is still being read. This is a hardware requirement, compiler barriers are not sufficient.

### 5.2 ecu_page and the page content

The page content is written by the writer and published with a release store to `ecu_page_next`. The reader doing the hand-over loads `ecu_page_next` with acquire and therefore sees the content. It stores the new offset to `ecu_page` with release. Every other reader loads `ecu_page` with acquire, which completes the happens-before chain from the writer's `memcpy` over the hand-over reader to the reading thread. The relaxed load of `ecu_page` inside the hand-over branch is sufficient, because the acquire increment already synchronized with the unlock of the previous hand-over thread, and no other thread can store `ecu_page` while the first lock is held.

@@@@ NOTE Finding by AI analysis of < V2.3.x implementation:
Before V2.3, `ecu_page` was a plain variable and the fast path had no acquire at all. It worked on x86 and ARMv8 for hardware reasons (see 5.5), but not by the rules of the language. An alternative with a relaxed load and reliance on the address dependency (the Linux `rcu_dereference()` recipe) would save the acquire on the reader path, but requires that the dependency chain is never broken by the compiler, which is a rule without support in the language. The difference is one instruction flavour (`LDAR` instead of `LDR` on AArch64, nothing on x86), which was not measurable in the test of chapter 6.

### 5.3 free_page and free_page_hazard

`free_page_hazard` is intentionally a plain bool. It is set to true immediately before the release store to `free_page`, and the writer loads `free_page` with acquire before it reads the flag, so a writer which sees the new free page also sees the flag. The reset to false is not ordered. This is conservative: a writer which misses the reset only delays a publish, it can never take a page which is still in use.  
Thread sanitizers may report this unordered accesses, they are not a defect.

### 5.4 Cost

| Operation | x86-64 | AArch64 |
|---|---|---|
| `lock_count` increment, acquire | `lock xadd` (unchanged) | `LDADDA` (or `LDAXR`/`STXR` loop without LSE) |
| `lock_count` decrement, release | `lock xadd` (unchanged) | `LDADDL` (or `LDXR`/`STLXR` loop without LSE) |
| `ecu_page` load, acquire, every lock | `mov` (unchanged) | `LDAR` instead of `LDR` |
| `ecu_page` store, release, hand-over only | `mov` (unchanged) | `STLR` instead of `STR` |

There is no `DMB` or `mfence` in the reader path.  
Measured with `test/cal_test` (chapter 6), the average lock times did not change between the relaxed and the acquire/release ecu_page and lock_count version in V2.3.x.

### 5.5 Background: why the plain ecu_page load worked on ARMv8 in versions < V2.3

Before V2.3, the fast path in `XcpLockCalSeg` (the branch for a lock count greater than zero) had no acquire operation at all. What a reader thread T executed was effectively:

```
uint32_t off = c->h.ecu_page;      // plain load, written by ANOTHER reader thread H
const uint8_t *p = &c->b[off];     // address computed from the loaded value
... x = p[i] ...                   // plain loads of the page content
```

The page content was written by a third thread, the XCP writer W:

```
W:  memcpy(&c->b[page], ...);                                  // calibration data
W:  atomic_store_explicit(&ecu_page_next, page, release);      // publish
H:  next = atomic_load_explicit(&ecu_page_next, acquire);      // hand-over reader
H:  c->h.ecu_page = next;                                      // plain store
T:  off = c->h.ecu_page;                                       // plain load, sees H's store
T:  read c->b[off]                                             // must see W's data
```

The question is whether T can observe the new page offset but stale page content. Three threads, and the only release/acquire pair is between W and H. T is outside that pair. Formally, C11 gives no guarantee here: H's plain store is not a release, there is no synchronizes-with edge reaching T, and T's plain accesses race with H's plain store, which is undefined behavior. The code nevertheless worked because of two hardware properties.

**Address dependency.** T's second load reads from an address computed from the value of the first load. The CPU cannot issue the page read before it knows the address. ARM makes this a guarantee of the architecture: an address dependency orders the two loads, the dependent load cannot return a value older than what the first load observed. This is the property RCU is built on, `rcu_dereference()` in Linux relies on it, and it is what C11's `memory_order_consume` was meant to expose and never did usably. Alpha was the one production architecture without this guarantee.

This is a statement about the CPU, not about the compiler. The compiler is free to destroy the dependency, and the shape of this code invites it: the page offsets are a handful of values known from the segment layout. A compiler which proves that the value is one of a few constants may replace the dependent load by a compare and select against constant addresses, which is a control dependency, and control dependencies do not order loads on ARM. Then the hardware guarantee is gone and the page read can be issued speculatively.

**Multi-copy atomicity.** The address dependency only orders T's own two loads. It says nothing about whether W's data was visible to T at that moment. On a machine which is not multi-copy atomic, a store can propagate to different observers at different times, so H could see W's data while T still has a stale copy, and H's subsequent store could reach T first. POWER works this way, and so did the formal ARMv7 model. ARMv8 was revised in 2017/2018 to be *other-multi-copy atomic*: a store becomes visible to all other observers at the same time (the storing thread may see its own store earlier through its store buffer). Together with the fact that H's `LDAR` prevents H's subsequent store from being hoisted above it, this gives the chain: T observes H's offset store, so H's acquire has completed, so W's release store and everything before it was already visible to all observers including T, so T's address dependent page read cannot be stale. Every link holds on ARMv8 hardware. None of them is guaranteed by the C standard.

**Why it was fixed anyway.** The compiler, not the CPU, was the likelier failure: with a plain object written concurrently, the compiler may load it twice with different results, move the load above the relaxed increment (a relaxed read-modify-write is not a barrier for surrounding plain accesses), cache it in a register or invent a reload. The old code already loaded `ecu_page` twice, once for the comparison and once for the return value. The argument was architecture luck rather than portability: on x86 the question is void, on ARMv8 it holds for the reasons above, on POWER or a non multi-copy atomic ARMv7 SMP part the transitivity argument collapses. The fix costs one instruction flavour and replaces an argument which depends on the ARM revision, the compiler version and the constant folding of page offsets with a guarantee the language makes.

The general lesson: dependency ordering is real on hardware and unusable in portable C. The standard's answer is `memory_order_consume`, every compiler implements it as acquire, and so the practical rule is to write the acquire.


### 5.6 Per page reference counting: sequential consistency

The four operations which couple readers and writer are sequentially consistent: the reader's pin (`fetch_add` on the reference count), its validation load of `published_page`, the writer's load of the reference count and its announcement store to `published_page`. The correctness argument needs a single total order S over them, which acquire/release does not provide.

Take a reader R whose validation load returns page P, so R pins P. Either that load read an old publication of P or a new one:

- Old (the value from before the writer retired P by announcing another page Q): in S, `R.validate < W.store(Q)`. The writer only selects P as a target after retiring it, so `W.store(Q) < W.load_refs(P)`. For the writer to have read a reference count of zero, R's pin must come after that load: `W.load_refs(P) < R.pin`. And `R.pin < R.validate` by program order. The chain `R.validate < W.store(Q) < W.load_refs(P) < R.pin < R.validate` is a contradiction, so the writer cannot have seen zero and cannot be copying into P.
- New (the value from a re-publication of P, after the writer copied into it): `W.store(P) <= R.validate` in S, the copy completed before the store, and R's page reads happen after the validation load, so R reads complete data. Any later writer check of the reference count of P comes after the writer's next announcement (it has to un-publish P first), which comes after `R.validate` (otherwise R's load would have returned that newer value), which comes after `R.pin`. So the writer sees the pin.

In both cases R holds a complete page which the writer will not touch until R releases it. Acquire/release alone does not order `W.load_refs(P)` against `R.pin` and `R.validate` against `W.store(Q)` in one consistent order, which is what closes the first case.

This is the store buffering (SB) litmus shape: each side writes its own variable and then reads the other's (reader: pin `page_refs[P]`, then load `published_page`; writer: store `published_page`, then load `page_refs[P]`). The forbidden outcome, both loads missing the other side's store, is allowed by acquire/release and by a hardware store buffer, and excluded by `seq_cst` or a full fence. Its classic instance is the entry protocol of Dekker's mutual exclusion algorithm (E. W. Dijkstra, *Cooperating Sequential Processes*, EWD123, 1965), and S. V. Adve, K. Gharachorloo, *Shared Memory Consistency Models: A Tutorial* (WRL Research Report 95/7, 1995) uses it to show how write buffers break sequential consistency. The Linux kernel memory model has it as `tools/memory-model/litmus-tests/SB+fencembonceonces.litmus`. The reader protocol, pin and re-validate with a store-load fence in between, is structurally the hazard pointer protocol, with a reference count per page instead of a hazard pointer per thread (M. M. Michael, *Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects*, IEEE TPDS 15(6), 2004; C++26 `<hazard_pointer>`, proposals P0233 and P2530).

No generation counter is needed in `published_page` to protect against the ABA case (P retired and re-published under the same index while a slow reader validates): that case is the second branch above, the reader simply gets the later generation of P, which is a valid and monotonic snapshot. A generation counter would only make the reader retry and then pin the same page.

Cost on the reader path: a `seq_cst` load is a plain load on x86 and `LDAR` on AArch64, a `seq_cst` read-modify-write is `lock xadd` on x86 (like any atomic read-modify-write) and `LDADDAL` on AArch64 with LSE. There is no barrier instruction. The difference to the acquire/release pairing of 5.1 is the `AL` flavour of the same instruction, which was not measurable in the test of chapter 6. As in 5.4, the cost of both algorithms is dominated by the two atomic read-modify-writes on a shared cache line per lock/unlock pair.

### 5.7 Microcontrollers and FreeRTOS SMP

The orderings of 5.1 to 5.6 are guarantees of the C11 memory model, which the compiler implements for the target. FreeRTOS does not change them, XCPlite uses the compiler's `<stdatomic.h>` on FreeRTOS as on any other platform. The guarantee holds for all cores in one cache coherent shared memory domain, which is the configuration FreeRTOS SMP (`configNUMBER_OF_CORES > 1`) requires.

**Cache coherence, definition.** A memory system is coherent if, for every single memory location:

1. All writes to the location are serialized: there is one total order of these writes (the coherence order) on which all cores agree, consistent with the program order of each core.
2. Every write eventually becomes visible to all cores (write propagation).
3. Every read returns a value from that order, and a core never observes an older value of the location after it has observed a newer one.

In hardware terms (single writer, multiple readers invariant): at any time a location is either writable by exactly one core or readable by any number of cores, and a read sees the value of the last write. C11 states the same for each atomic object as its modification order and the four coherence rules (write-write, read-read, read-write, write-read). Coherence is a property per location. It says nothing about the order in which writes to different locations become visible, for example the page content and the published page index. That is the memory consistency model, and what the memory orders of the atomics control.

**Ordering, not coherence.** A release or `seq_cst` store does not flush or clean any cache, neither completely nor by address range. Cache coherence is a hardware property, on SMP microprocessors (x86, Cortex-A) as well as on SMP microcontrollers: on SMP capable microcontrollers there is either no data cache in front of the shared SRAM or the caches are kept coherent by hardware. What remains is the ordering of a core's own accesses, which store buffers, write buffers and the bus may reorder. The compiler handles this with a barrier instruction which waits until the core's outstanding accesses have completed, a few cycles on in-order microcontroller cores, tens of cycles on Cortex-M7 with AXI. A cache clean would be orders of magnitude more expensive.

Typical code generation (GCC/Clang):

| Core | release store | acquire load | `seq_cst` store / load | atomic read-modify-write |
|---|---|---|---|---|
| ARMv7-M (Cortex-M3/M4/M7) | `DMB; STR` | `LDR; DMB` | `DMB; STR; DMB` / `LDR; DMB` | `DMB; LDREX/STREX` loop`; DMB`, same for acq_rel and `seq_cst` |
| ARMv8-M Mainline (Cortex-M33/M55/M85) | `STL` | `LDA` | `STL` / `LDA` | `LDAEX/STLEX` loop |
| Xtensa LX6/LX7 (ESP32, ESP32-S3) | `MEMW; S32I` | `L32I; MEMW` | same | `MEMW; S32C1I` compare-and-swap loop, 8/16 bit emulated inline on the 32 bit word |
| RISC-V RVWMO with A extension | `fence rw,w; sw` | `lw; fence r,rw` | adds a leading `fence rw,rw` to the load | `amoadd.w.aqrl` |

The store-load ordering which the reference counting algorithm needs (5.6) is exactly what a store buffer breaks. `seq_cst` emits the full barrier for it (the trailing `DMB`, `MEMW` or `fence rw,rw`), on most microcontroller cores the same instruction as for acquire/release.

**Preconditions.** The build checks the first one, the others are the responsibility of the integration:

1. The 8, 16 and 32 bit atomics must be lock-free. `cal.h` asserts `ATOMIC_CHAR/SHORT/INT/LONG_LOCK_FREE == 2`. Cores without exclusive access instructions, such as Cortex-M0+ (ARMv6-M, RP2040), fall back to `__atomic_*` library calls. The default single core implementation of these disables interrupts, which is not multicore safe. A multicore safe implementation based on hardware spinlocks exists for RP2040 (pico-sdk `pico_atomic`), but it is not wait-free, so the lock count algorithm loses its wait-free guarantee there. Such targets fail to build.
2. Calibration segments must be located in shared, coherent memory: not in core private memory such as the DTCM of Cortex-M7/R5 if the reader tasks may run on more than one core, and not in external RAM behind a non-coherent cache. Check the vendor documentation for atomic operations on external RAM, for example PSRAM on ESP32.
3. All reader tasks and the XCP server task run under the same FreeRTOS SMP kernel instance.

**FreeRTOS principles.** FreeRTOS defines no memory model of its own. The SMP kernel assumes identical cores sharing one coherent memory. Its critical sections disable interrupts on the local core and take a spinlock (`portGET_TASK_LOCK`, `portGET_ISR_LOCK`), which the port implements with hardware atomics and barriers, so a critical section is a full barrier. Kernel objects (queues, semaphores, task notifications) are synchronization points because they go through these critical sections. ESP-IDF ships its own SMP variant with explicit spinlock arguments (`taskENTER_CRITICAL(&mux)`), the principle is the same. The FreeRTOS `atomic.h` functions are based on critical sections and depend on the port for multicore safety, XCPlite uses the compiler atomics instead.

**Not supported: asymmetric multicore (AMP).** Parts such as STM32H7 dual core (Cortex-M7 + M4), i.MX RT1170 or Cortex-R5 in split mode run one FreeRTOS instance per core, the cores are not cache coherent with each other, and exclusive accesses across cores need a global exclusive monitor in the interconnect, which is often missing (hence hardware semaphores such as the STM32 HSEM). C11 atomics give no guarantee across such cores and the compiler emits no cache maintenance. FreeRTOS handles AMP with message or stream buffers in shared memory, inter-core interrupts and explicit cache handling (non-cacheable MPU regions or maintenance by address). Calibration segments shared across AMP cores would need the same: the writer cleans the page by address before the publish store, the reader invalidates it before reading, and pages must be cache line aligned and padded, otherwise an invalidate destroys neighbouring dirty data. XCPlite does not implement this. Run XCPlite and all tasks which access its calibration segments within one kernel instance.


## 6. Test results

The test application `test/cal_test` creates multiple reader threads on a shared calibration segment. The writer thread updates the segment with a pattern, mixing single writes and atomic transactions, and the reader threads check every snapshot for consistency and count the changes they observe. The lock duration is measured and shown as a histogram.

Test parameters are compile-time constants in `test/cal_test/src/main.cpp`:

- `TEST_THREAD_COUNT`: number of reader threads, default 4
- `TEST_WRITE_COUNT`: number of writes by the writer thread, default 10000
- `TEST_MAIN_LOOP_DELAY_US`: loop delay of the writer thread, default 100 us
- `TEST_ATOMIC_CAL`: every N writes are done in an atomic transaction, default 10
- `TEST_TASK_LOOP_DELAY_US`: loop delay of the reader threads, default 50 us
- `TEST_TASK_LOCK_DELAY_US`: time a reader holds the lock, default 0 (off)
- `TEST_DATA_SIZE`: size of the test data in bytes, default 8
- `TEST_LOCK_TIMING`: create the lock duration histogram
- `TEST_CALBLK`: use a calibration block instead of a segment

The share of reads which observe a change depends on the ratio of the reader loop delay to the writer loop delay, it is not comparable between runs with different parameters. A result of 10000 changes per thread means that every write was observed by every thread.

### 6.1 V2.3, lock count and page hand-over, MacBook Pro M2, default parameters

```
Final Statistics:
===========================================================
Test parameters:
TEST_WRITE_COUNT = 10000
TEST_THREAD_COUNT = 4
TEST_CALBLK = OFF
TEST_ATOMIC_CAL = ON
TEST_TASK_LOOP_DELAY_US = 50
TEST_TASK_LOCK_DELAY_US = 0
TEST_MAIN_LOOP_DELAY_US = 100
TEST_DATA_SIZE = 8
Thread 0: reads=25045, changes=10001, avg_time=0.10us, max_time=18.08us
Thread 1: reads=25049, changes=10001, avg_time=0.10us, max_time=17.33us
Thread 2: reads=25039, changes=9999, avg_time=0.11us, max_time=48.58us
Thread 3: reads=25055, changes=10000, avg_time=0.10us, max_time=16.50us
Total Results:
  Total writes: 10000
  Total atomic writes: 1000
  Total reads: 100188
  Total changes observed: 40001 (39.9%)
  Total errors: 0
  Average lock time: 0.11 us
  Maximum lock time: 48.58 us
Producer acquire lock time statistics:
  count=94123  max=36026ns  avg=89ns (cal=16ns)
Lock time histogram (94123 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                         2    0.00%
  20-40ns                     8207    8.72%  ####
  40-80ns                    52317   55.58%  ##############################
  80-120ns                   27009   28.70%  ###############
  120-160ns                   4711    5.01%  ##
  160-200ns                    963    1.02%
  200-300ns                    386    0.41%
  300-400ns                    138    0.15%
  400-500ns                     92    0.10%
  500-600ns                     46    0.05%
  600-800ns                     87    0.09%
  800-1000ns                    46    0.05%
  1000-1500ns                   41    0.04%
  1500-2000ns                   16    0.02%
  2000-3000ns                   18    0.02%
  3000-4000ns                    3    0.00%
  4000-6000ns                    8    0.01%
  6000-8000ns                    7    0.01%
  8000-10000ns                  13    0.01%
  10000-20000ns                 11    0.01%
  20000-40000ns                  2    0.00%
```

### 6.2 V2.3, per page reference counting, MacBook Pro M2, default parameters

Same machine and build type (Debug) as 6.1, `OPTION_CAL_RCU_REFCOUNT` with 3 pages. In a Release build the average lock time is 0.06 us for both algorithms.

```
Final Statistics:
===========================================================
Test parameters:
TEST_WRITE_COUNT = 10000
TEST_THREAD_COUNT = 4
TEST_CALBLK = OFF
TEST_ATOMIC_CAL = ON
TEST_TASK_LOOP_DELAY_US = 50
TEST_TASK_LOCK_DELAY_US = 0
TEST_MAIN_LOOP_DELAY_US = 100
TEST_DATA_SIZE = 8
Thread 0: reads=24973, changes=9983, avg_time=0.13us, max_time=17.67us
Thread 1: reads=24976, changes=9983, avg_time=0.13us, max_time=26.71us
Thread 2: reads=24964, changes=9982, avg_time=0.13us, max_time=27.46us
Thread 3: reads=24992, changes=9990, avg_time=0.13us, max_time=27.96us
Total Results:
  Total writes: 10000
  Total atomic writes: 1000
  Total reads: 99905
  Total changes observed: 39938 (40.0%)
  Total errors: 0
  Average lock time: 0.13 us
  Maximum lock time: 27.96 us
Producer acquire lock time statistics:
  count=95332  max=27943ns  avg=115ns (cal=16ns)
Lock time histogram (95332 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  20-40ns                     2789    2.93%  ##
  40-80ns                    36327   38.11%  ###########################
  80-120ns                   39169   41.09%  ##############################
  120-160ns                  11131   11.68%  ########
  160-200ns                   2539    2.66%  #
  200-300ns                   2034    2.13%  #
  300-400ns                    483    0.51%
  400-500ns                    293    0.31%
  500-600ns                     91    0.10%
  600-800ns                    165    0.17%
  800-1000ns                    93    0.10%
  1000-1500ns                   87    0.09%
  1500-2000ns                   43    0.05%
  2000-3000ns                   15    0.02%
  3000-4000ns                    6    0.01%
  4000-6000ns                    7    0.01%
  6000-8000ns                    8    0.01%
  8000-10000ns                  20    0.02%
  10000-20000ns                 27    0.03%
  20000-40000ns                  5    0.01%
SUCCESS: No errors occurred during the test
```

### 6.3 V2.2, MacBook Pro M3 and Raspberry Pi 5, TEST_TASK_LOOP_DELAY_US = 100

Measured with the lock count algorithm before the V2.3 changes (relaxed lock count, plain `ecu_page`) and a reader loop delay of 100 us. The lock time histogram is comparable to 6.1 and 6.2, the share of observed changes is not.

MacBook Pro M3:
```
Thread 0: reads=13360, changes=9284, avg_time=0.12us, max_time=5.08us
Thread 1: reads=13360, changes=9268, avg_time=0.14us, max_time=25.29us
Thread 2: reads=13360, changes=9258, avg_time=0.13us, max_time=25.71us
Thread 3: reads=13361, changes=9272, avg_time=0.14us, max_time=10.17us

Total Results:
  Total writes: 10000
  Total atomic writes: 1000
  Total reads: 53441
  Total changes observed: 37082 (69.4%)
  Total writes pending: 136
  Total publish all count: 1001
  Total errors: 0
  Average lock time: 0.13 us
  Maximum lock time: 25.71 us

Lock time histogram (53441 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                         2    0.00%
  40-80ns                     4116    7.70%  #######
  80-120ns                   15014   28.09%  #########################
  120-160ns                  17386   32.53%  ##############################
  160-200ns                  10999   20.58%  ##################
  200-240ns                   3543    6.63%  ######
  240-280ns                   1243    2.33%  ##
  280-320ns                    597    1.12%  #
  320-360ns                    222    0.42%
  360-400ns                     71    0.13%
  400-600ns                     76    0.14%
  600-800ns                     50    0.09%
  800-1000ns                    54    0.10%
  1000-1500ns                   41    0.08%
  1500-2000ns                    6    0.01%
  2000-3000ns                    2    0.00%
  3000-4000ns                    1    0.00%
  4000-6000ns                    5    0.01%
  6000-8000ns                    7    0.01%
  8000-10000ns                   1    0.00%
  10000-20000ns                  3    0.01%
  20000-40000ns                  2    0.00%
```

Raspberry Pi 5:
```
Thread 0: reads=12872, changes=8522, avg_time=0.32us, max_time=6.74us
Thread 1: reads=12928, changes=9963, avg_time=0.39us, max_time=8.72us
Thread 2: reads=13312, changes=9968, avg_time=0.39us, max_time=8.59us
Thread 3: reads=12891, changes=9927, avg_time=0.34us, max_time=4.76us

Total Results:
  Total writes: 10000
  Total atomic writes: 1000
  Total reads: 52003
  Total changes observed: 38380 (73.8%)
  Total writes pending: 1
  Total publish all count: 1001
  Total errors: 0
  Average lock time: 0.36 us
  Maximum lock time: 8.72 us

Lock time histogram (52003 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  160-200ns                    579    1.11%  #
  200-240ns                    747    1.44%  #
  240-280ns                   6638   12.76%  ###########
  280-320ns                   7584   14.58%  #############
  320-360ns                  10654   20.49%  ##################
  360-400ns                   8396   16.15%  ##############
  400-600ns                  17368   33.40%  ##############################
  600-800ns                     22    0.04%
  800-1000ns                     1    0.00%
  3000-4000ns                    1    0.00%
  4000-6000ns                    8    0.02%
  6000-8000ns                    3    0.01%
  8000-10000ns                   2    0.00%
```


## 7. Known limitations and open issues

Remaining after the V2.3 changes. Items which are consequences of the design are listed as compromises in 3.2, this chapter lists what could be improved within the current design.

1. **Segment list iteration.** Iterating over the segment list does not give a consistent view while segments are created concurrently in other threads. Iteration may only be used after the application has finalized its registrations. Segments created after finalization should not be registered. A clean solution needs shared state between all processes.
2. **Duplicate name race.** `XcpFindCalSeg` is used to check for duplicate names. Two segments with the same name created at the same time in different threads can still both be registered. The linear name search could also be replaced by a hash table.
3. **Memory segment numbering.** The increment of `memory_segment_count` is not atomic. A2L MEMORY_SEGMENT numbers are a global namespace across all processes in SHM mode.
4. **Publish from a foreign thread.** `XcpCalSegPublishAll` is reachable from `XcpDisconnect()`, which is a user function and may be called from any thread. This violates the single writer rule: the free page is acquired with a load followed by a store, not with an exchange, so two concurrent publishers could take the same page.
5. **Stalled publishes are invisible to the tool.** In lazy mode a write is acknowledged with `CRC_CMD_OK`, and a read-back shows the written value from the writer page. A segment whose publishes stall (a leaked lock, a segment which is never locked) is only reported by a warning in the log. See 8.5.
6. **Blocking publish stalls the command thread.** The end of an atomic transaction and `XcpDisconnect()` wait up to 500 ms **per pending segment** in the XCP command thread, which delays all command responses accordingly.
7. **Atomic transactions do not flush first.** Pending changes from earlier writes are not published before a transaction begins, they are published together with the transaction when it ends.
8. **Nested lock coherence.** A nested lock may return a newer page than the outer lock (contract rule 10). Closing this window needs per thread state, see the note in 8.1.
9. **free_page_hazard is a plain bool.** Lock count algorithm, intentional (see 5.3), but thread sanitizers report it.
10. **Never locked segments.** Lock count algorithm: a segment which is never locked accepts one publish only (table in 3.2).
11. **No runtime diagnostics for contract violations.** Leaked locks are not detectable without thread identity, unbalanced releases only when the page's count is already zero. In SHM mode leaked counts persist across process restarts, and the `XcpDeinit()` lock check is not possible because other processes may legitimately hold locks.
12. **`ptptool` holds a lock for its lifetime.** `ptp_master.c` and `ptp_observer.c` take an initial lock on their parameter segment at startup and release it at shutdown, to keep a stable pointer. With either algorithm this blocks publishing after the first publish: the lock count never returns to zero, or the pinned page is the only reclamation candidate with 3 pages. The periodic lock and copy in these tools therefore never sees a calibration change. To be fixed in the tool.
13. **EPK segment update writes into the published page.** `XcpCalUpdateEpkSeg` writes the EPK directly into the ECU page from the writer thread while a reader may hold it. Marked in the code, to be checked.


## 8. Proposals for improvement

### 8.1 Full RCU with a reclamation list

Replace the single free page by per page reference counting with a list of pages, so that publishing is driven by the writer and not by reader progress. This would remove the second lock visibility delay, the starvation of never locked segments and the coupling between readers.  

Replace the single free page by per page reference counting with a list of pages, so that publishing is driven by the writer and not by reader progress. This would remove the second lock visibility delay, the starvation of never locked segments and the coupling between readers. An analysis and a design in the style of this document is in [rcu_improvements/CAL_RCU_IMPROVEMENT.md](rcu_improvements/CAL_RCU_IMPROVEMENT.md).

Status: implemented in V2.3 as the per page reference counting algorithm (`OPTION_CAL_RCU_REFCOUNT`, chapter 4.3), without thread local state. Differences to the analysis document: the reader's handle is the page pointer it received, so there is no per thread nesting depth and nested locks pin independently (contract rule 10, the coherence of nested locks was not adopted); the publication stamp has no generation counter (5.6); the page count is `XCP_CALSEG_RCU_PAGES`, default 3. With three pages the memory is the same as for the lock count algorithm and the header is smaller. Pages beyond three are a tuning option: each one tolerates one more reader which never releases and improves the publish rate under a bursting writer, which shifts the probability of a stall by a leaked lock but does not establish a contract. The outermost lock is lock-free instead of wait-free (3.1). Thread local storage was not taken because of the concerns below.

The all `seq_cst` memory ordering of that design is not a cost argument against it: on x86 a `seq_cst` load is a plain load and every atomic read-modify-write is a full barrier anyway, on AArch64 the difference to acquire/release is the `LDADDAL` flavour of the same instruction. The reader cost of both designs is dominated by the two atomic read-modify-writes on a shared cache line, which are identical. The generation counter in the publication stamp of the analysis document is not needed either, the re-validation of the published page index after the pin is sufficient, and the implementation in `rcu.h` dropped it.

**Note on thread local storage on POSIX systems.** The reader state (`depth`, `pinned`) must be per thread and per segment. On ELF platforms this means a TLS variable with a *model* chosen at compile time, and the model trades three properties against each other, of which a shared library can have two:

- `initial-exec`/`local-exec` (proposed in the analysis document): fixed offset in the static TLS block, fast, allocated at thread creation, no lazy allocation. But a shared object with this model can only be loaded after program start if it fits into the loader's static TLS surplus (glibc: 1-2 KB, tunable with `GLIBC_TUNABLES=glibc.rtld.optional_static_tls`, musl: not supported at all). Otherwise `dlopen()` fails with "cannot allocate memory in static TLS block". This affects every host which loads `libxcplite.so` at runtime: Python (`ctypes`, PyO3), JNI, LabVIEW, Simulink, plugin architectures, the Rust crate as a `cdylib`.
- `general-dynamic` (the default for shared objects): safe for `dlopen()`, but every access goes through `__tls_get_addr` (TLSDESC fast path on AArch64), and the first access per thread and module allocates the TLS block of the module inside the dynamic linker. This is neither real-time safe nor async-signal safe, and it happens in the thread which locks first, the real-time reader. Mitigation is to touch the variable once at thread start, a rule the application has to know.
- Static linking (the XCPlite default, `BUILD_SHARED_LIBS` is off): the TLS is in the executable's own static block, `local-exec`, no surplus problem. The concern moves to the application: when the application itself is a `dlopen()`ed module, its TLS is dynamic TLS again.

Further concerns:

- In glibc the static TLS block is placed inside the thread's stack allocation, within the requested stack size. `pthread_create()` fails with `EINVAL` when the stack size is smaller than guard page plus static TLS plus a minimum, otherwise the usable stack shrinks. The memory growth comes out of the stack budget of every thread, including the threads which never lock, and tightly sized real-time threads are where it shows.
- A `thread_local` defined in a header (template static or `inline`) is one variable per DSO when the symbol has hidden visibility. A segment locked in one DSO and released in another would use two different `depth` counters, which is an unbalanced release by construction. The reader state needs default visibility or a single definition inside the library.
- A C++ `thread_local` with dynamic initialization carries a guard check on every access, one with a destructor registers `__cxa_thread_atexit`, which prevents `dlclose()` until all threads have exited. The reader state must stay POD and constant initialized.
- macOS allocates thread local variables lazily on first access per thread (dyld TLV, malloc) in every model. QNX supports ELF TLS with the same static/dynamic split and its own static TLS surplus, the limit depends on the SDP version.
- Only threads with a TLS block may run the code. All POSIX threads have one, threads created with a raw `clone()` do not.

XCPlite already depends on TLS in this shape: the public header macros for per thread event instances and the A2L once pattern declare `static THREAD_LOCAL` variables (`a2l.h`, `xcplib.h`), and `util.c` has one inside the library. All concerns above apply to these today, the reader state would add a variable of the same kind.

The algorithm itself does not need TLS. `Rcu` in `rcu.h` takes the reader state as a parameter, only the convenience layer which makes nested locks coherent without passing a handle needs it. An API where the caller owns the reader state (a stack object per outermost critical section, or a member of the application's per thread context) has none of these concerns. Nested locks with independent reader states remain safe, they pin independently, which is the nesting caveat of contract rule 10.

### 8.2 Owned calibration segments

The second lock visibility delay is not necessary for a segment which is used by a single reader thread only. An `owned` mode, set with a function like `XcpCalSegSetOwnedMode(handle)`, could hand pages over without the hazard confirmation.

The type-safe way to express this in Rust is the typestate pattern, with two distinct wrapper types, which could also be a blueprint for the C++ API:

```Rust
// Excerpt from the current Rust implementation

// Calibration pages must be Sized + Send + Sync + Copy + Clone + 'static
pub trait CalPageTrait
where Self: Sized + Send + Sync + Copy + Clone + 'static,
{
    // This trait is empty, it's just a marker for the page type
}

// CalSeg
// Is Send + !Sync + Clone: freely shareable across threads by cloning (like an Arc<T>), but not shareable by reference (no &CalSeg<T>)
pub struct SharedCalSeg<T: CalPageTrait> {
    index: xcplib::tXcpCalSegIndex, // The calibration segment handle from the C implementation
    default_page: &'static T, // The static immutable reference to the default page
    _not_sync_marker: PhantomData<std::cell::Cell<()>>, // CalSeg is send, not sync (like a Cell)
}

impl<T: CalPageTrait> SharedCalSeg<T> {
    pub fn new(instance_name: &'static str, default_page: &'static T) -> SharedCalSeg<T> {
    ...
    }
}

// Implement clone for CalSeg, which is a simple copy of the handle and the default page reference
impl<T: CalPageTrait> Clone for SharedCalSeg<T> {
    fn clone(&self) -> Self {
        SharedCalSeg {
            index: self.index,
            default_page: self.default_page, // &T is Copy, so this is fine
            _not_sync_marker: PhantomData,
        }
    }
}

// New owned mode CalSeg
// Single reader thread, no deferred visibility
// Send: can be moved to another thread
// !Sync: cannot be shared across threads (enforced by not implementing Sync)
// !Clone: no accidental sharing (enforced by not implementing Clone)
pub struct OwnedCalSeg<T: CalPageTrait> { inner: SharedCalSeg<T> }

impl<T: CalPageTrait> OwnedCalSeg<T> {
    pub fn new(instance_name: &'static str, default_page: &'static T) -> OwnedCalSeg<T> {
        let calseg = SharedCalSeg::new(instance_name, default_page);
        xcplib::XcpCalSegSetOwnedMode(calseg.index);
        OwnedCalSeg { inner: calseg }
    }

    // Consuming transition back to shared mode
    pub fn into_shared(self) -> SharedCalSeg<T> {
        xcplib::XcpCalSegClearOwnedMode(self.inner.index);
        self.inner
    }
}

// Deref gives zero-boilerplate access to all SharedCalSeg<T> methods.
// Clone is not forwarded through Deref, OwnedCalSeg stays !Clone.
// Send/!Sync are inherited automatically from SharedCalSeg<T> through the inner field.
// This is the same pattern Rust's standard library uses, e.g. Box<T> derefs to T, String derefs to str.
use std::ops::Deref;
impl<T: CalPageTrait> Deref for OwnedCalSeg<T> {
    type Target = SharedCalSeg<T>;
    fn deref(&self) -> &SharedCalSeg<T> {
        &self.inner
    }
}

// Add DerefMut if mutable access to SharedCalSeg methods is needed
```

### 8.3 Hash table for the segment registry

Replace the linear search in `XcpFindCalSeg` by a hash table (limitation 7.2).

### 8.4 Background processing without receive timeouts

Use non blocking sockets and a waitable event for the XCP server thread, so that `XcpBackgroundTasks()` does not depend on `SO_RCVTIMEO` (compromise 8).

### 8.5 Diagnostics for stalled segments

Report a segment whose publishes stall to the tool and the user: expose the lock count and the pending state of each segment as measurement signals, detect on the writer side that a segment has been pending for longer than a threshold while its lock count did not return to zero, and stop acknowledging writes to such a segment with `CRC_CMD_OK`. Whether and how the XCP tool should be informed is an open discussion.

### 8.6 Scope guard for the C API

A `CalSegScopedLock(name, ptr)` based on `__attribute__((cleanup))` (GCC, Clang) would give the C API the same protection against early returns that the C++ guard has.


## 9. Change history

### V2.3.x

Review of the RCU implementation exposed some possible improvements and bugs which were fixed.  
The specification of the contract was improved.   
This document was clarified.  

Bugfixes:

1. `XcpCalSegPublish`, blocking mode: after the wait timeout, only the existence of a free page was checked, not `free_page_hazard`. A timeout with a free page which was not yet confirmed took that page as the new writer page while a reader could still be using it. The hazard flag is now checked after the wait as well, the publish fails with `CRC_ACCESS_DENIED` and the changes stay pending.
2. `lock_count` was incremented and decremented with `memory_order_relaxed`. The reclamation logic needs `acquire` on the increment and `release` on the decrement (see 5.1). On x86 this was masked by the full fence of `lock xadd`, on AArch64 it was a real race.
3. `XcpCalSegBeginAtomicTransaction` reset the `write_pending` flags of all segments. A change which was still pending from an earlier write was then never published until the next write to the same segment. The flags are no longer touched, pending changes are published together with the transaction.
4. `ecu_page` was a plain `uint32_t`, written by one reader thread and read by all others and by the writer. It is now atomic with release/acquire ordering (see 5.2).
5. `lock_count` was 8 bit, 257 concurrent or nested locks wrapped it to zero. It is now 16 bit. The two bytes were taken from the name padding, `XCP_MAX_CALSEG_NAME` is 23 (64 bit) or 27 (32 bit) instead of 25 or 29. `XcpUnlockCalSeg` returns `uint16_t`.

Contract and API changes:

6. The user contract of chapter 2 was written down. The `isActivated()` and index checks in `XcpLockCalSeg`/`XcpUnlockCalSeg` are contract assertions and are compiled out with `NDEBUG`.
7. Passive mode (`XCP_MODE_DEACTIVATE`) is handled in the `CalSegLock`/`CalBlkLock` macros and in the C++ wrappers, they return the default page without calling the library. Before, the C macros called `XcpLockCalSeg` with `XCP_UNDEFINED_CALSEG` in passive mode, which asserted and returned NULL.
8. The C++ `CalSegGuard` classes are not copyable. A copy released the lock twice.
9. `XcpDeinit()` asserts in debug builds that no segment is locked (not in SHM mode).
10. `XcpUnlockCalSeg(index, page)` takes the page pointer returned by the matching lock and returns `void`. `CalSegUnlock(name, ptr)`/`CalBlkUnlock(name, ptr)` accordingly. The C++ guards are unchanged for the user. This is a breaking change for direct C API users, an unlock which does not know its page cannot be implemented for the reference counting algorithm. Only with `OPTION_CAL_RCU_REFCOUNT`, the default lock count algorithm keeps `XcpUnlockCalSeg(index)`.
11. Per page reference counting added as second RCU algorithm (chapter 4.3), selected with `OPTION_CAL_RCU_REFCOUNT` (not default) in `xcplib_cfg.h` and `XCP_CALSEG_RCU_PAGES` in `xcp_cfg.h`. The RCU specific code in `cal.c` is confined to the `CalSegRcu*` functions, the lock count algorithm remains available.
12. `cal.h` asserts at compile time that the 8, 16 and 32 bit atomics are lock-free (not with `OPTION_ATOMIC_EMULATION`). Targets which would fall back to `__atomic_*` library calls, such as Cortex-M0+, fail to build (chapter 5.7).


