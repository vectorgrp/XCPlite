# XCPlite memory-safe, lock-less and wait-free calibration data access

This document describes XCPlite's lock-less and wait-free access to calibration parameters: the contract the application has to follow, the guarantees and compromises of the implementation, the algorithm and its memory ordering, test results, known limitations and proposals for improvement.

The base mechanism is not specific to XCP. It can be used wherever software parameters are modified by one thread and read by many, and the readers must be memory-safe, lock-less and wait-free: a REST API, a web server, test stimulation or a shared memory interface. XCPlite adds the XCP specific features (page switching, freeze, copy, init, atomic transactions for consistency) on top of it.

Terminology: a *calibration segment* is a calibration parameter block which represents an XCP/A2L MEMORY_SEGMENT and implements the XCP specific functionality. A *calibration block* wraps a calibration parameter struct without a MEMORY_SEGMENT. Both use the same mechanism; where the difference does not matter, this document says segment.

Chapter 2 is the complete user contract. Chapters 4 and 5 are the technical background and are not required to use the API.

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
- `XcpUnlockCalSeg(index)` releases the lock and returns the lock count before the unlock.
- The macros `CalSegLock(name)`/`CalSegUnlock(name)` and `CalBlkLock(name)`/`CalBlkUnlock(name)` are typed wrappers, which also handle passive mode (see 2.1).

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
8. Every lock is released exactly once, by the thread which acquired it. Locks are balanced per thread, like the read side of a rwlock.
9. Locks may be nested (recursive) and may be held by any number of threads at the same time, up to a total of 65535 locks per segment. There is no owner tracking.
10. A nested lock is not guaranteed to return the same snapshot as the outer lock: every lock returns a pointer to a valid and consistent page, but a nested lock may return the next published page.
11. Lock and unlock are wait-free and may be called from any thread at any priority, including realtime threads. Critical sections should be short: calibration changes become visible to all readers only when the lock count of the segment returns to zero (see 3.2).
12. C++: `CalSegGuard` is not copyable and must be held in a scope. Pass the pointer or the reference down, not the guard.

### 2.4 Contract violations and their consequences

- Direct `XcpLockCalSeg()`/`XcpUnlockCalSeg()` calls before `XcpInit()`, in passive mode or with an invalid index: assertion in debug builds, undefined behavior in release builds.
- `XcpDeinit()` while a lock is held: assertion in debug builds. In release builds the lock is leaked (see below).
- **Leaked lock**: a lock which is never released, because the thread was terminated or cancelled inside its critical section, because of an early return between `CalSegLock()` and `CalSegUnlock()`, or because the thread blocks forever inside the section. The consequences are contained: the readers of the segment are not affected, lock and unlock keep working wait-free, memory safety is intact and nothing is ever reclaimed. But the ECU page of the segment is frozen, no calibration change becomes visible anymore. The writer sees this as permanently pending writes: in lazy mode `XcpCalSegWriteMemory` still returns `CRC_CMD_OK` to the tool and a read-back shows the written value, while `XcpBackgroundTasks()` retries forever and logs a warning. At the end of an atomic transaction and in `XcpDisconnect()` the writer stalls for `XCP_CALSEG_AQUIRE_FREE_PAGE_TIMEOUT` (500 ms) per pending segment and then returns `CRC_ACCESS_DENIED`. In SHM mode the lock count is in shared memory, a leak from a crashed process persists until the shared memory is cleared with `shmtool` (@@@@ TODO).
- **Unbalanced release**: an unlock without a matching lock, for example by copying a guard, or by unlocking after a direct `XcpLockCalSeg()` call which returned NULL. This is **not contained**: it can bring the lock count to zero while another reader is inside its critical section, and the page this reader is using can then be handed to the writer and overwritten. This is why `CalSegGuard` is not copyable and why the C macros must be paired in the same scope.
- More than 65535 locks on one segment: the lock count wraps, with the same consequences as an unbalanced release.

### 2.5 How the contract is checked

The contract checks (activation state, index range, unlock without lock, locks held in `XcpDeinit()`) are `assert()` statements with diagnostic output, compiled out with `NDEBUG` (Release and RelWithDebInfo builds). They add no cost to release builds. The memory ordering of the algorithm (chapter 5) is not a check, it is part of the mechanism and always active.

There is no runtime detection of leaked locks or unbalanced releases in release builds. The implementation has no thread identity, a lock count of two is indistinguishable from two threads or one nested lock. How the XCP tool can be informed about a stalled segment is an open topic (@@@@ TODO see 8.5).


## 3. Guarantees and compromises

### 3.1 Guarantees

Under the contract of chapter 2, the implementation guarantees:

- **Memory safety**: a reader holding a lock reads a page which is not modified concurrently. Torn reads are impossible.
- **Consistency**: every lock returns a snapshot which respects atomic transactions. Either all writes of a transaction are visible in a snapshot, or none of them.
- **Wait-free readers**: lock and unlock are one atomic read-modify-write each, plus a few loads and stores. No loop, no blocking, no dependency on the progress of other threads. A reader is never delayed by the writer or by other readers.
- **Bounded memory**: three RCU pages per segment, independent of the number of reader threads. Classic RCU implementations need memory proportional to the number of readers.
- **Data is never lost**: a write which is acknowledged to the tool stays in the writer page until it is published. A stalled publish delays the visibility, it does not lose the data.

### 3.2 Compromises

The writer side accepts the following compromises in exchange for the wait-free readers:

1. **Exactly one writer thread.**
2. **Visibility needs reader progress.** A published change becomes visible to the readers with the next first level lock (a lock which brings the lock count from 0 to 1). Publishing itself needs a free page which is confirmed as unused, and this confirmation is also done by a first level lock. So a write becomes visible with the first or with the second first level lock after the write, depending on whether the previous hand-over was already confirmed. In lazy mode, the publish of a delayed write happens with the next `XcpBackgroundTasks()` cycle.
3. **Visibility delay is non deterministic under contention.** Publishing is only possible when the lock count of the segment returns to zero. While there is always at least one reader inside its critical section, or when the segment is locked rarely, pending changes accumulate in the writer page and are published together (not sequentially) as soon as a free page is available. In lazy mode (`XCP_ENABLE_CALSEG_LAZY_WRITE`, the default) the writer retries in `XcpBackgroundTasks()`. In blocking mode, every write waits for the publish.
4. **Publishing may starve and time out.** A segment which is never locked accepts exactly one publish, afterwards its writes stay pending. A blocking publish times out after `XCP_CALSEG_AQUIRE_FREE_PAGE_TIMEOUT` (500 ms) per segment and returns `CRC_ACCESS_DENIED`, which delays the XCP command response by that time. Blocking publishes happen at the end of an atomic transaction, in `XcpDisconnect()`, and for every write when lazy mode is disabled.
5. **Acknowledge before visibility.** In lazy mode a write is acknowledged to the tool (`CRC_CMD_OK`) before it is visible to the readers. `XcpCalSegReadMemory` reads from the writer page, so a read-back verifies the written value even while it is still pending. There is no risk of failure after the acknowledge, see 3.1.
6. **Creation uses a mutex.** Registration of a new segment is protected by a mutex to keep the segment list consistent between threads. This is a one time cost per segment, not on the reader path.
7. **Memory.** Each segment needs a 64 byte header plus three pages (working page, writer page and swap page) when the default page has static lifetime (`XCP_ENABLE_ABS_ADDRESSING` with `XCP_ADDR_EXT_ABS == 0`), otherwise plus four pages, including a copy of the default page, which is mandatory in SHM mode. The page size is rounded up to 8 bytes. The segment name is limited to `XCP_MAX_CALSEG_NAME` characters (23 on 64 bit, 27 on 32 bit platforms), because the header is padded to exactly 64 bytes.
8. **Background processing.** Lazy publishing needs `XcpBackgroundTasks()` to be called cyclically in the writer thread. With blocking sockets this requires a receive timeout (`SO_RCVTIMEO`), which was difficult to abstract over all platforms. Non blocking sockets with a waitable event would be a better approach (@@@@ TODO postponed, currently only macOS/BSG has a more theoretical with WAITALL timeout behaviour, see regression tests).
9. **Nested locks are not coherent.** See contract rule 10.


## 4. Algorithm

### 4.1 Pages and shared state

Each segment has three RCU pages, in addition to the default (reference) page:

1. `ecu_page` - the page the readers currently use
2. `xcp_page` - the private page of the writer, it accumulates all changes
3. `free_page` - the swap page, handed from the readers to the writer

The algorithm is an RCU pattern with exactly one element in its memory reclamation list, the free page. Without a free page, changes simply accumulate in the writer page. This is also how atomic transactions are implemented: while a transaction is open, publishing is suspended and the writer page collects all writes.

All state is in the 64 byte segment header, which is one cache line:

- Shared between writer and readers: `ecu_page_next` (atomic), `free_page` (atomic), `ecu_access` (atomic), `free_page_hazard` (plain bool, ordered through `free_page`, see 5.3).
- Shared between readers: `lock_count` (atomic, 16 bit), `ecu_page` (atomic).
- Private to the writer: `xcp_page`, `xcp_access`, `write_pending`.

Pages are stored as offsets into the segment memory, not as pointers, so a segment can be placed in shared memory and used by several processes.

### 4.2 Pseudo code

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
function unlock(segment) {
    lock_count.fetch_sub(1, release)
}

// Single threaded write
function write(segment, offset, data[]) {
    xcp_page[offset] = data
    write_pending = true
    if (!transaction_open) {
        try_publish(segment, wait = !lazy_mode)
    }
}

// Single threaded publish
// Returns true when the changes in xcp_page have been published, they become visible with the next first level lock
function try_publish(segment, wait) -> bool {
    page = free_page.load(acquire)
    if (wait) {
        wait up to 500 ms for (page != NULL && !free_page_hazard)
    }
    if (page == NULL || free_page_hazard) {
        return false                                  // No free page or not yet confirmed, the changes stay pending in xcp_page
    }
    free_page.store(NULL)
    memcpy(page, xcp_page)                            // The new writer page starts with the current content
    old = xcp_page
    xcp_page = page
    write_pending = false
    ecu_page_next.store(old, release)                 // Publish
    return true
}
```

### 4.3 Hand-over sequence

Starting from a quiet state (`ecu_page == ecu_page_next`, a confirmed free page):

1. The writer writes into `xcp_page` and publishes: the free page becomes the new `xcp_page`, the old `xcp_page` is announced in `ecu_page_next`.
2. The next first level lock sees `ecu_page != ecu_page_next`, takes the new page over and hands the old ECU page to `free_page`. It sets `free_page_hazard`, because a reader which locked concurrently (lock count already 1, before the hand-over stores) may still use the old page. This lock and all following locks return the new page: the change is visible.
3. The next first level lock sees `ecu_page == ecu_page_next` and clears `free_page_hazard`. The lock count was zero, so every reader of the old page has released its lock. The free page is confirmed and the writer may publish again.

Step 3 is the reason for the "second lock" in compromise 2: a write which arrives between step 2 and step 3 cannot be published until step 3 has happened.

### 4.4 Lazy mode, blocking mode and atomic transactions

- **Lazy mode** (`XCP_ENABLE_CALSEG_LAZY_WRITE`, default): `XcpCalSegWriteMemory` tries a non blocking publish and returns `CRC_CMD_OK` in any case. A failed publish leaves the segment marked `write_pending`, and `XcpBackgroundTasks()` retries in every cycle. A warning is logged when a publish stays pending for more than 200 ms.
- **Blocking mode**: every write waits for its publish, up to the timeout, and returns `CRC_ACCESS_DENIED` on timeout.
- **Atomic transactions**: `XcpCalSegBeginAtomicTransaction` suspends publishing, all following writes accumulate in the writer pages. `XcpCalSegEndAtomicTransaction` publishes all segments with pending writes, blocking with the timeout per segment. Changes from earlier writes which were still pending when the transaction began stay pending and are published together with the transaction.
- **`XcpDisconnect()`** publishes all pending writes, blocking.


## 5. Memory ordering

The algorithm has two independent synchronization relations: between the readers (who is the last reader of a page) and between the writer and the readers (page content and page hand-over). Both need the ordering described here. The cost is one acquire load and the ordered flavour of two atomic read-modify-write operations per lock/unlock pair, there is no barrier instruction in the reader path.

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


## 6. Test results

The test application `test/cal_test` creates multiple reader threads on a shared calibration segment. The writer thread updates the segment with a pattern, mixing single writes and atomic transactions, and the reader threads check every snapshot for consistency and count the changes they observe. The lock duration is measured and shown as a histogram.

Test parameters are compile time constants in `test/cal_test/src/main.cpp`:

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

### 6.1 V2.3, MacBook Pro M2, default parameters

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

### 6.2 V2.2, MacBook Pro M3 and Raspberry Pi 5, TEST_TASK_LOOP_DELAY_US = 100

Measured with the implementation before the V2.3 changes (relaxed lock count, plain `ecu_page`) and a reader loop delay of 100 us. The lock time histogram is comparable to 6.1, the share of observed changes is not.

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
8. **Nested lock coherence.** A nested lock may return a newer page than the outer lock (contract rule 10). Closing this window needs per thread state.
9. **free_page_hazard is a plain bool.** Intentional (see 5.3), but thread sanitizers report it.
10. **Never locked segments.** A segment which is never locked accepts one publish only (compromise 4).
11. **No runtime diagnostics for contract violations.** Leaked locks and unbalanced releases are not detectable without thread identity. In SHM mode leaked lock counts persist across process restarts, and the `XcpDeinit()` lock check is not possible because other processes may legitimately hold locks.


## 8. Proposals for improvement

### 8.1 Full RCU with a reclamation list

Replace the single free page by per page reference counting with a list of pages, so that publishing is driven by the writer and not by reader progress. This would remove the second lock visibility delay, the starvation of never locked segments and the coupling between readers.  


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

Report a segment whose publishes stall to the tool and the user: expose the lock count and the pending state of each segment, detect on the writer side that a segment has been pending for longer than a threshold while its lock count did not return to zero, and stop acknowledging writes to such a segment with `CRC_CMD_OK`. Whether and how the XCP tool should be informed is an open discussion.

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


