# XCPlite transmit queue: the two lock-free 64 bit queues

This document describes the two lock-free multi producer, single consumer (MPSC) transmit queues of XCPlite, `src/queue64v.c` (variable entry size, the default) and `src/queue64f.c` (fixed entry size). It lists the aspects by which such a queue is classified, compares the two variants aspect by aspect, says which one to use when, and shows how the XCP configuration parameters influence the efficiency of the whole transmit path.

The mutex and critical section based queues `queue32.c` and `queue32m.c` (32 bit platforms, Windows, FreeRTOS, raw Ethernet) are not covered. They use a different consumer interface (`queuePop` with message accumulation) and a different transmit path.

Status: this is an analysis of the code. Chapter 9 cites the existing benchmark results in `test/queue_test/README.md`. Chapter 10 lists the open points, which are candidates for a change.

Contents:
1. Role of the queue in XCPlite
2. Common design of both queues
3. `queue64v.c`: variable entry size
4. `queue64f.c`: fixed entry size
5. Classification aspects
6. Comparison
7. Which queue to use when
8. Influence of the configuration parameters
9. Existing benchmark results
10. Open points


## 1. Role of the queue in XCPlite

Every thread which triggers a measurement event is a producer. The transmit thread of the XCP server is the single consumer.

**Producer** (`XcpTriggerDaqList_` in `src/xcplite.c`): for each ODT of a triggered DAQ list

1. `queueAcquire(size)` reserves an entry and returns a pointer into the queue memory.
2. The ODT header, the timestamp (first ODT only) and the measurement data are written directly into the entry.
3. `queuePush()` commits the entry. The `priority` argument requests a flush, it is set for the last ODT of a DAQ list with a priority other than 0, and for command responses, events and SERV text messages which go through the queue.

If `queueAcquire` returns size 0 the queue is full. The rest of the event is skipped and the loss is counted.

**Consumer** (`XcpTlHandleTransmitQueue` in `src/xcpethtl.c`):

1. `queuePeek(index)` is called with index 0, 1, 2, ... to collect committed entries without removing them, until one of these conditions holds:
   - the next entry does not fit into `XCPTL_MAX_SEGMENT_SIZE`,
   - `MAX_BUFFERS` (256) entries are collected,
   - an entry carries a flush request,
   - there is no more committed data and either `MIN_UPDATE_TIME_MS` (50 ms) have passed since the last transmit or the queue is more than `MAX_QUEUE_LEVEL` (50 %) full.
   
   If there is no more committed data and none of the conditions holds, the consumer sleeps `MAX_SLEEP_TIME_MS` (1 ms) and tries again.
2. The transport layer header (counter and length) is written into the 4 bytes which the queue reserved in front of each message.
3. All collected entries are sent as one UDP datagram or TCP write with one `sendmsg` call and an `iovec` per entry (`socketSendToV`). The message data is never copied in user space.
4. `queueRelease()` is called for each entry in the same order.

The consumer never wakes up on a signal from a producer. Producers make no system call.


## 2. Common design of both queues

Both files implement the API in `src/queue.h` and share most of their design.

**Memory layout.** One contiguous block: a queue header, followed by the entry buffer. The header is one cache line (64 bytes) in `queue64f.c` and two in `queue64v.c`, see chapter 3. The block is either allocated with `aligned_alloc` (`queueInit`) or provided by the caller (`queueInitFromMemory`, used for the shared memory queue in the `shm` configuration). There are no pointers inside the block.

**Queue header.** `head`, `tail`, `packets_lost` and `flush_offset` are atomics and live in the same cache line, together with the constant fields `magic`, `buffer_size` and `from_memory`. This is the shared header line: producers and the consumer both read and write it.

- `head` and `tail` are 64 bit byte offsets which only ever increase. The position in the buffer is `offset % buffer_size`. They do not wrap in practice, so there is no ABA problem.
- `head` is advanced by producers, `tail` by the consumer. `head - tail` is the number of bytes in use.

**Entry.** Each entry starts with a 32 bit atomic `entry_header`: the low word is the entry length, the high word is the state, 0 (free or reserved) or `0xCCCC` (committed). It is followed by `QUEUE_ENTRY_USER_HEADER_SIZE` bytes (4, the XCP transport layer header) and the payload, rounded up to `QUEUE_PAYLOAD_SIZE_ALIGNMENT` (4).

**Producer algorithm.**

1. Load `tail` and `head`.
2. Check the free space. If the entry does not fit, increment `packets_lost` and return size 0.
3. Compare-and-swap `head` to `head + increment`. On failure repeat from step 2 with the `head` value the CAS returned.
4. Store the length with state 0 into `entry_header` of the reserved entry.
5. The caller writes the payload.
6. `queuePush`: optionally store the entry offset into `flush_offset`, then store length and state `0xCCCC` into `entry_header` with release ordering.

**Consumer algorithm.**

1. `queuePeek`: read `packets_lost` and reset it if it is not 0, load `head`, find the entry for the requested index, load its `entry_header` with acquire ordering. If the state is not committed, return size 0.
2. `queueRelease`: reset the entry so that it reads as state 0 again, then advance `tail`.

**What the two variants differ in** is the increment in step 3 (entry length or constant slot size), how the consumer finds the entry for an index, and how a released entry is reset. Everything else follows from these three points.


## 3. `queue64v.c`: variable entry size

- `head` is advanced by `4 + entry_len`. Entries are packed back to back, an entry may start at any offset which is a multiple of 4.
- An entry never wraps. The buffer has room for one maximum size entry (`QUEUE_MAX_ENTRY_SIZE + 4`) behind `buffer_size`, an entry which starts near the end runs into this area.
- Because an entry header can appear at any position, all memory handed back to producers must read as zero. `queueRelease` therefore clears the complete entry with `memset`, header and payload.
- The cleared memory is published to the producers with a release update of `tail`. Producers load `tail` with acquire ordering. This pair is load-bearing: without it a producer could write its entry before the consumer's clear has arrived, on hardware which reorders stores (ARM).
- `queuePeek(index)` has to walk the entries from `tail`. The consumer caches the position of the last peek (`cached_peek_index`, `cached_peek_tail`), so a sequence of peeks with increasing index costs one step each. The cache is reset by `queueRelease`. It is written on every peek and therefore has its own cache line behind the shared header line, the queue header of `queue64v.c` is 128 bytes. Only the consumer and `queueClear` write to it. A process which attaches to an initialised queue with `queueInitFromMemory` does not touch it.
- Producers do not reload `tail` after a failed CAS. A stale `tail` only makes the free space check more pessimistic.
- `queueLevel` returns bytes.

The clear with `memset` treats bytes which are later accessed as an atomic object as plain memory. The implementation relies on byte cleared storage being read as an atomic zero. This holds on the supported targets (x86 and ARM with lock-free, naturally aligned 32 bit atomics), it is an assumption and not a portable C11 pattern.


## 4. `queue64f.c`: fixed entry size

- `head` is advanced by the constant `QUEUE_ENTRY_SIZE` = `QUEUE_ENTRY_USER_SIZE + 4` = `XCPTL_MAX_DTO_SIZE + 8`. With the default `XCPTL_MAX_DTO_SIZE` of 248 for this variant, a slot is 256 bytes, four cache lines.
- The buffer size is a multiple of the slot size, so every slot has a fixed position and its `entry_header` is always at the same address. The build fails if the slot size is not a multiple of the cache line size.
- `queueRelease` resets only the 4 byte `entry_header` of the slot. The payload is left as it is.
- `queuePeek(index)` computes the slot directly from `tail + index * QUEUE_ENTRY_SIZE`.
- Producers reload `tail` after a failed CAS.
- `queueLevel` returns a number of slots.
- A released slot is published to the producers as in `queue64v.c`: the consumer resets `entry_header` relaxed and updates `tail` with release ordering, producers load `tail` with acquire ordering.


## 5. Classification aspects

These are the aspects which were agreed as relevant for a lock-free MPSC queue in XCPlite. The relevance column says how much the aspect matters for XCP measurement, where the rule is that instrumentation must not disturb the timing of the application and that lost measurement data is acceptable as long as it is counted.

| # | Aspect | Question | Relevance |
|---|---|---|---|
| | **A. Guarantees** | | |
| 1 | Producer progress | Wait-free, lock-free or blocking? Bounded retries? | High |
| 2 | Consumer progress under a stalled producer | Does a producer which stops between reserve and commit block the entries behind it? | High |
| 3 | Ordering | FIFO by reservation or by commit? Per producer order? | Medium |
| 4 | Overflow policy | What happens when the queue is full, how is loss counted and signalled? | High |
| 5 | Memory-model correctness | Which acquire/release pairs carry the happens-before? Formal data races, ABA? | High |
| 6 | Portability assumptions | Lock-free 64 bit atomics, atomics in raw storage, cache line size | Medium |
| | **B. Producer hot path** | | |
| 7 | Atomic operations per message | How many, on which shared cache lines? | High |
| 8 | Contention scaling | Behaviour of the `head` CAS with more producers and cores | High |
| 9 | False sharing | Layout of the queue header line, entries sharing cache lines | High |
| 10 | Coherency traffic caused by the consumer | What the consumer writes on release and what the next producer pays for it | High |
| 11 | Latency distribution | Average and tail of the acquire time, system calls, priority inversion | High |
| 12 | Zero copy | Write in place, space for the transport layer header | Medium |
| | **C. Consumer and transport** | | |
| 13 | Fit for batching and vectored IO | Peek ahead without release, cost of peeking at index n, messages per segment | High |
| 14 | Wake-up and flush strategy | Polling, priority flush, timeout, resulting latency | Medium |
| 15 | Consumer cost per message | Work in peek and release | Medium |
| | **D. Resources** | | |
| 16 | Memory efficiency | Overhead per message, messages per queue byte | High |
| 17 | Sizing predictability | Capacity in bytes or in entries, meaning of the level | Medium |
| 18 | Maximum entry size | How the limit drives ODT splitting | High |
| | **E. Deployment** | | |
| 19 | Shared-memory suitability | Position independence, attach, robustness against a crashed producer process | High for `shm`, low otherwise |

Aspects which are not relevant here, and why:

- **Multiple consumers:** there is exactly one transmit thread.
- **Unbounded or resizable queue:** memory is fixed at initialisation, overflow is handled by dropping.
- **Node allocation and memory reclamation:** both queues are ring buffers, there are no nodes to free.
- **Fairness between producers:** producers never wait for each other for longer than a CAS retry.
- **Back-pressure which blocks the producer:** the opposite is required, a producer must never wait.
- **NUMA placement:** the targets are single socket systems.


## 6. Comparison

### 6.1 Summary

| # | Aspect | `queue64v.c` | `queue64f.c` |
|---|---|---|---|
| 1 | Producer progress | Lock-free, no system call | Same |
| 2 | Stalled producer | Blocks the consumer at that entry | Same |
| 3 | Ordering | FIFO in reservation order | Same |
| 4 | Overflow | New message dropped and counted | Same |
| 5 | Memory model | Reuse published by `tail` release/acquire | Same |
| 6 | Portability | Needs byte cleared storage to read as an atomic zero | Fixed atomic objects, no such assumption |
| 7 | Atomic operations | 2 loads, 1 CAS, 2 stores | Same |
| 8 | Contention scaling | One `head` CAS shared by all producers | Same |
| 9 | False sharing | Neighbouring entries share cache lines | Slots are cache line aligned |
| 10 | Coherency traffic | Consumer writes every byte of every released entry | Consumer writes 4 bytes per released slot |
| 11 | Latency | Depends on payload size through 10 | Independent of payload size |
| 12 | Zero copy | Yes | Yes |
| 13 | Batching | Peek at index n by walking, cached for sequential use | Peek at index n computed directly |
| 14 | Wake-up, flush | Polling consumer, flush by offset | Same |
| 15 | Consumer cost | Proportional to the bytes released | Constant per message |
| 16 | Memory efficiency | 8 bytes overhead per message | 256 bytes per message, whatever its size |
| 17 | Sizing | In bytes | In slots |
| 18 | Maximum entry size | 1024 bytes DTO by default, costs nothing when not used | 248 bytes DTO by default, larger events are split |
| 19 | Shared memory | Supported | Supported |

### 6.2 Guarantees (aspects 1 to 6)

**1. Producer progress.** Both are lock-free and not wait-free: a CAS on `head` fails only because another producer succeeded, so the system always makes progress, but there is no bound for a single thread. `test/queue_test/README.md` reports a maximum of 5 to 8 retries with 10 producer threads. There is no lock, no system call and no waiting for the consumer. A producer which is preempted between reserve and commit does not delay other producers.

**2. Stalled producer.** Neither queue is non-blocking from end to end. The consumer reads entries strictly in reservation order and stops at the first entry which is not committed. All entries behind it wait, although they are committed.

- A preempted producer delays the transmission for as long as it is preempted. The queue has to be large enough to hold the data of all other producers for that time, otherwise they overflow. A low priority thread which is preempted inside an event can in this way cause data loss for high priority threads. The high priority threads themselves are never delayed.
- A producer which dies between `queueAcquire` and `queuePush` leaves the queue blocked. This matters in the `shm` configuration, where producers are separate processes. The queue recovers at the next XCP CONNECT, because `handleXcpCommand` calls `queueClear`.
- The window is the time to copy one ODT. It is shorter in `queue64f.c` only because its entries are smaller.

**3. Ordering.** The order is fixed by the CAS on `head`, so it is the order of reservation, not of commit. Each thread's own messages stay in order. The timestamp of an event is taken before the reservation, so timestamps of different threads may appear slightly out of order in the stream. The transport layer counter is assigned by the consumer at transmit time and always matches the order on the wire.

**4. Overflow.** The new message is dropped, old data is never overwritten. `packets_lost` is incremented, the consumer adds it to the transport layer counter so that the client sees a gap. `XcpTriggerDaqList_` skips the remaining ODTs of the event. The free space check may use a stale `tail` and then reports an overflow slightly too early, it never accepts an entry which does not fit.

**5. Memory model.**

- Commit: the producer's release store to `entry_header` and the consumer's acquire load make the payload visible. Identical in both.
- `head`: relaxed in both. Ownership of the reserved range follows from the atomicity of the CAS, not from ordering. The consumer may see `head` advanced before the entry header is written, it then reads state 0 and stops.
- Reuse of released memory: both use the `tail` release/acquire pair, which gives a happens-before from the consumer's reset to the next producer's write. The pair is load-bearing on ARM. A release store to `entry_header` followed by a relaxed update of `tail` is not sufficient: the update of `tail` may become visible to another core before the reset, and the late reset then overwrites a header which the next producer has already committed. This was observed on a Cortex-A76 with the litmus test `test/queue_test/litmus/slot_reuse.c`.
- ABA: not possible, `head` and `tail` are monotonic 64 bit values.

**6. Portability.** Both require 64 bit atomics which are always lock-free (`ATOMIC_LLONG_LOCK_FREE == 2`, checked at compile time), a 64 bit POSIX platform and a cache line of 64 bytes. `queue64v.c` in addition relies on `memset` cleared bytes being read as an atomic zero. `queue64f.c` resets its headers with atomic stores only.

### 6.3 Producer hot path (aspects 7 to 12)

**7. Atomic operations.** Identical: load `tail`, load `head`, CAS `head`, store `entry_header` (reserved), store `entry_header` (committed), plus a store to `flush_offset` for priority messages. The first three touch the queue header line, the stores touch the first cache line of the entry. Both compute `head % buffer_size` with a 64 bit division by a runtime value.

**8. Contention scaling.** Identical. All producers of all threads, and in the `shm` configuration of all processes, compete for the one cache line with `head`. This is the scaling limit of the design. A per thread or per application queue would remove it, see `docs/SHM.md`.

**9. False sharing.**

- Queue header line: the producers need this line for their CAS on `head`. The consumer reads it in every `queuePeek` and writes to it once per released entry, when it advances `tail`. It does not write to it while peeking. `head` and `tail` share the line in both variants, see 10.2.
- Entries: in `queue64f.c` every slot starts on a cache line boundary when the queue is allocated by `queueInit`, and in the `shm` configuration, where the queue memory follows a 64 byte header. Two producers never write to the same line. In `queue64v.c` consecutive entries share a line at their boundary, so two producers which fill neighbouring entries at the same time disturb each other.

**10. Coherency traffic caused by the consumer.** This is the main structural difference.

- `queue64v.c`: the consumer writes every byte of every entry a second time. The lines end up modified in the consumer's cache, and the producer which reuses the memory has to fetch each of them from the other core. The cost grows with the payload size, on both sides.
- `queue64f.c`: the consumer writes 4 bytes per slot. The payload lines were only read (by the kernel during `sendmsg`), the producer has to regain write access to them, but no data has to be transferred back.

**11. Latency.** Without contention the acquire is a few atomic operations in both. In `queue64v.c` the time to fill an entry depends on how many lines have to be fetched from the consumer's core, so it grows with the payload size. In `queue64f.c` it does not. The long tail in the histograms of `queue_test` is preemption by the scheduler and is the same for both.

**12. Zero copy.** Identical. The producer writes into the queue memory, the queue reserves 4 bytes in front of the payload for the transport layer header, and the consumer hands the entries to the kernel as an `iovec` array.

### 6.4 Consumer and transport (aspects 13 to 15)

**13. Batching and vectored IO.** Both support peeking ahead without releasing, which is what the vectored transmit path needs. `queue64f.c` finds entry n in constant time. `queue64v.c` has to walk and depends on its cache, which only helps for the sequential pattern the transport layer uses.

The number of messages per datagram is `XCPTL_MAX_SEGMENT_SIZE / (4 + message size)`, limited to 256. It is the main factor for the cost per byte on the consumer side, because each datagram is one system call:

| DTO size | Messages per datagram, MTU 1500 (1472 bytes) | MTU 9000 (8968 bytes) |
|---|---|---|
| 16 | 73 | 256 (limited by `MAX_BUFFERS`) |
| 64 | 21 | 131 |
| 248 | 5 | 35 |
| 1024 | 1 | 8 |

With `queue64f.c` the largest message is 252 bytes, so at least 5 messages share a datagram at the standard MTU. With `queue64v.c` and DTOs near 1024 bytes each message is its own datagram.

**14. Wake-up and flush.** Identical, because it is implemented in `xcpethtl.c`. The consumer polls with 1 ms sleeps. Data which does not fill a segment is sent after at most 50 ms, or earlier when the queue is half full or a flush is requested. The flush request is a single `flush_offset` in the queue header, compared against the offset of an entry in the buffer. A later request overwrites an earlier one, and a stale value can trigger a flush again when an entry starts at the same offset after a wrap around. Both effects only change when a segment is sent, not what is sent. In `queue64f.c` every slot offset recurs each time the buffer wraps, so stale flushes are more frequent there.

**15. Consumer cost per message.** `queue64f.c`: constant, one atomic store and one add. `queue64v.c`: a `memset` over the entry, so the consumer needs time and memory bandwidth in proportion to the data rate, in addition to the copy the kernel makes.

### 6.5 Resources (aspects 16 to 18)

**16. Memory efficiency.** A message in `queue64v.c` occupies its DTO size rounded up to 4, plus 8 bytes. In `queue64f.c` it occupies one slot of 256 bytes. For a queue size of 32 KiB (`XcpEthServerInit(..., 32 * 1024)`), which gives 31736 usable bytes in `queue64v.c` and 128 slots in `queue64f.c`:

| DTO size | `queue64v.c` bytes per message | `queue64v.c` messages | `queue64f.c` messages |
|---|---|---|---|
| 16 | 24 | 1322 | 128 |
| 64 | 72 | 440 | 128 |
| 124 | 132 | 240 | 128 |
| 248 | 256 | 123 | 128 |
| 1024 | 1032 | 30 | 25 events of 5 slots |

`queue64f.c` is as memory efficient as `queue64v.c` only when the DTOs are close to the slot size. For small events it needs several times the memory for the same capacity. `queue64v.c` loses one maximum size entry (1032 bytes) of the given size for the wrap around area.

**17. Sizing.** `queue64v.c` is sized in bytes: data rate times the time to cover. `queue64f.c` is sized in slots: messages per second times the time to cover, times 256 bytes. `queueLevel` and the 50 % threshold of the consumer are in the same unit as the capacity, bytes or slots.

**18. Maximum entry size.** `XCPTL_MAX_DTO_SIZE` is reported to the client in the CONNECT response, and the client packs the signals of an event into ODTs of at most this size. Each ODT is one queue entry.

- `queue64v.c`, 1024 bytes: an event with about 1000 bytes of signal data is one entry, one CAS, 1032 bytes of queue memory.
- `queue64f.c`, 248 bytes: the same event is 5 entries, 5 CAS operations, 5 ODT headers and 1280 bytes of queue memory. The window in which another thread's messages can be interleaved is also larger.

In `queue64v.c` a large limit costs nothing for small messages. In `queue64f.c` the limit is the slot size and is paid by every message.

### 6.6 Deployment (aspect 19)

**19. Shared memory.** Both support `queueInitFromMemory` and contain no pointers, so several processes can map the queue at different addresses. Both need atomics which work across processes, which the compile time check for lock-free 64 bit atomics ensures. The risk of a blocked queue after a producer process crash (aspect 2) is the same for both. If the queue memory is not initialised by the attaching process, `queue64v.c` returns NULL for a wrong magic value, `queue64f.c` only asserts.


## 7. Which queue to use when

**Use `queue64v.c` (the default) when**

- the size of the events varies widely or most events are small, which is the usual case for instrumented application code,
- queue memory is limited,
- single events are larger than about 250 bytes and should stay in one ODT,
- the data rate is moderate, so the second write pass of the consumer does not matter.

**Use `queue64f.c` when**

- the data rate is high and the consumer or the memory bandwidth is the bottleneck,
- most ODTs are close to the slot size, for example large structs or arrays measured at a high rate, which the client splits into full ODTs anyway,
- an acquire time which does not depend on the payload size is more important than memory,
- the memory for a queue which is sized by the number of messages is available.

**Do not use `queue64f.c` when** the events are small and frequent: every 16 byte event takes a 256 byte slot, the queue overflows much earlier for the same memory, and a full queue costs more than the `memset` saves.

A measurement on the target decides borderline cases. `queue_test` gives the acquire time histogram and the loss count for a synthetic load. To build it with `queue64f.c` without editing `xcplib_cfg.h`, pass a header which contains `#undef OPTION_QUEUE_64_VAR_SIZE` and `#define OPTION_QUEUE_64_FIX_SIZE` with `-DXCPLITE_CFG_OVERRIDE=<path>` to CMake. For a real application, build with `TEST_ACQUIRE_LOCK_TIMING` or `TEST_ACQUIRE_SPIN_COUNT` defined, the statistics are printed by `queueDeinit`.


## 8. Influence of the configuration parameters

| Parameter | Where | Effect on the transmit path |
|---|---|---|
| `OPTION_QUEUE_64_VAR_SIZE`, `OPTION_QUEUE_64_FIX_SIZE` | `xcplib_cfg.h` | Selects the variant. Exactly one may be defined |
| `XCPTL_MAX_DTO_SIZE` | `xcptl_cfg.h` | Maximum ODT size. `queue64v.c`: only a limit, plus the size of the wrap around area. `queue64f.c`: the slot size is this value plus 8 and is the memory used by every message |
| `OPTION_MTU` | `xcplib_cfg.h` | Sets `XCPTL_MAX_SEGMENT_SIZE` and with it the number of messages per datagram, see 6.4. Jumbo frames reduce the number of system calls per byte by a factor of 6 |
| Queue size | Parameter of `XcpEthServerInit` | Has to cover the data produced while the consumer does not drain, see below |
| `XCPTL_TRANSPORT_LAYER_HEADER_SIZE` | `xcptl_cfg.h` | Bytes reserved in front of each message, the basis for sending without a copy. Fixed at 4 |
| `XCPTL_PACKET_ALIGNMENT` | `xcptl_cfg.h` | Padding of each message. Fixed at 4, the atomic entry header needs it |
| `XCPTL_CRM_VIA_TRANSMIT_QUEUE` | `xcptl_cfg.h` | Command responses go through the queue with a flush request. Each response ends the current segment early |
| DAQ list priority | Set by the client | A priority other than 0 flushes after every event of that list: low latency, but one datagram per event |
| `XCP_ENABLE_OVERRUN_INDICATION_PID` | `xcp_cfg.h` | Marks the first ODT after an overflow, in addition to the gap in the transport layer counter. Not defined by default |
| `MAX_BUFFERS`, `MIN_UPDATE_TIME_MS`, `MAX_QUEUE_LEVEL`, `MAX_SLEEP_TIME_MS` | Constants in `xcpethtl.c` | Batching limit (256 entries), maximum delay of a partial segment (50 ms), early transmit threshold (50 %), polling interval (1 ms) |
| `OPTION_SHM_MODE` | `xcplib_shm_cfg.h` | One queue in shared memory for all processes, so all their threads share the `head` cache line |
| `OPTION_ATOMIC_EMULATION`, `PLATFORM_32BIT`, `OPTION_ENABLE_UDP_RAW` | | Force `OPTION_QUEUE_32`, neither 64 bit queue is used |

**Choosing `XCPTL_MAX_DTO_SIZE` for `queue64f.c`.** The slot size has to be a multiple of 64, so the value has to be 64 n - 8. It also has to be at least `XCPTL_MAX_CTO_SIZE` (248), so the possible values are 248, 312, 376, 440, 504 and so on up to 1016, unless `XCPTL_MAX_CTO_SIZE` is reduced as well. A larger value wastes more memory for small events and reduces the number of messages per datagram. A smaller value splits events into more ODTs. The value should be close to the typical ODT size of the application.

**Queue size.** The consumer sleeps 1 ms whenever it has caught up, so the queue must hold at least the data of 1 ms plus the scheduling delay of the transmit thread, the duration of a `sendmsg` call and the longest preemption of a producer inside an event (aspect 2). `docs/xcplib_cfg.md` recommends 10 ms of the expected data rate. Because the consumer transmits early when the queue is half full, a queue which is twice as large as the expected backlog keeps partial segments from being sent on that condition.

- `queue64v.c`: size = bytes per second x 0.01, where each message counts with its DTO size plus 8.
- `queue64f.c`: size = ODTs per second x 0.01 x 256. A 10 kHz event with one ODT needs 100 slots, 25.6 KiB, whatever the size of the ODT.

**What has no influence on the producers.** The mutex `ctr_mutex` in `xcpethtl.c` is held by the consumer while it sends. It only competes with the thread which sends command responses, never with a producer.


## 9. Existing benchmark results

`test/queue_test/README.md` contains results of `queue_test` (10 producer threads, bursts of 4 messages, 64 KiB queue). The reported time covers `queueAcquire`, writing the payload and `queuePush`, so it grows with the payload size for trivial reasons too.

| Variant | Machine, build | Payload | Messages/s | Average time |
|---|---|---|---|---|
| `queue64v.c` | Raspberry Pi 5 | 64 | 597 k | 173 ns |
| `queue64v.c` | Raspberry Pi 5 | 64 to 1024 | 559 k | 780 ns |
| `queue64v.c` | macOS | 64 | 2132 k | 85 ns |
| `queue64v.c` | macOS | 64 to 1024 | 1869 k | 256 ns |
| `queue64f.c`, `entry_header` release, relaxed `tail` (removed) | MacBook Air M2, release | 64 to 248 | 1322 k | 204 ns |
| `queue64f.c`, `tail` release/acquire | MacBook Air M2, release | 64 to 248 | 1302 k | 200 ns |

These runs use different machines, build types and payload ranges, so they do not compare the two variants. A run of both variants on the same Raspberry Pi 5 as release builds gave 212 ns for `queue64v.c` and about 122 ns for `queue64f.c`, but with the payload range each variant uses by default, 64 to 1024 against 64 to 248 bytes, so it is not a like for like comparison either. The README draws the conclusion that the fixed size queue performs significantly better with large payloads. A run of both variants on one machine with the same payload range, which would quantify this, is not recorded there.

The two `queue64f.c` rows compare a removed synchronisation scheme, which did not order the reuse of a slot (see aspect 5), with the current one. In the release build there is no measurable difference.


## 10. Open points

These are proposals for improvement. None of them is implemented.

### 10.1 Blocked queue after a producer failure

See aspect 2. `queue64v.c` has a TODO for a timeout in `queuePeek`. To skip a reserved entry the consumer needs its length. In `queue64f.c` the slot size is constant. In `queue64v.c` the length is in the entry header only after the producer's first store (length with state 0), a producer which stopped right after its CAS leaves a header which reads 0. In both variants the entry must not be reused while the stalled producer may still write to it, which is the hard part.

### 10.2 Contention on the queue header line

All producers and the consumer share the cache line with `head` and `tail` (aspects 8 and 9). Removing the consumer's write to this line in `queuePeek` made no measurable difference in `queue_test` on a Raspberry Pi 5, with 1, 2 and 10 producers, because the consumer still writes to the line once per message when it advances `tail`. The candidates which remain:

- **Separate cache lines for `head` and `tail`.** The consumer's update of `tail` would no longer invalidate the line the producers need for their CAS, at the price of a second line each producer has to read.
- **Advance `tail` once per transmitted segment** instead of once per released entry. This needs a release function for several entries in `queue.h`.
- **One queue per application or per thread.** This removes the contention between producers on `head` altogether, the consumer then has to merge several queues. `docs/SHM.md` mentions it for the `shm` configuration, where all processes share one queue.
- **Cache line size of the target.** `CACHE_LINE_SIZE` is 64 in both files. On a target with 128 byte cache lines, such as Apple Silicon, the peek cache of `queue64v.c` can still share a physical line with `head`, and two slots of `queue64f.c` can share a line when the queue memory starts in the middle of one.

### 10.3 Smaller items

- The first store to `entry_header` in `queueAcquire` (length with state 0) uses release ordering in both variants. Nothing is published by it, relaxed would be sufficient.
- Both variants compute `head % buffer_size` with a 64 bit division by a runtime value, in `queueAcquire` and in `queuePeek`. A buffer size which is a power of two would allow a mask, in `queue64f.c` a slot index would do as well.
- `flush_offset` holds the offset of an entry in the buffer. A stale value triggers a flush again whenever an entry starts at this offset after a wrap around, in `queue64f.c` once per wrap (aspect 14). Storing the 64 bit `head` position of the entry instead would make the value unique.
- `queue64v.c`: `queuePeek` does not set `*flush_requested` to false, `queue64f.c` does. The only caller initialises the variable itself.
- `queue64f.c` checks the cache line alignment of the queue memory only when it allocates the memory itself, and only asserts on a wrong magic value in `queueInitFromMemory`, where `queue64v.c` returns NULL.
- Stale comments: `queue64v.c` describes the CAS as "acq_rel/acq", it is relaxed. `xcpethtl.c` says the MTU is set to 8000, the default `OPTION_MTU` is 1500.
- TODOs in the code around the queue: `xcpethserver.c` notes a possible race when a process attaches to the shared memory queue before the leader has initialised it. `xcpethtl.c` marks the array of `MAX_BUFFERS` queue buffers on the stack of the transmit thread, `socketSendToV` adds an `iovec` array of the same length.

### 10.4 Tests and measurements

- The shared memory mode of `queue_test` (`TEST_QUEUE_SHM`, `--producer` and `--consumer`) does not compile any more, it refers to types which no longer exist. There is no multi process test of the queue. The attach path of `queueInitFromMemory` is therefore not tested.
- There is no benchmark of both variants on one machine with the same payload range (chapter 9). It would quantify the statement that the fixed size queue is faster for large payloads.
- The slot reuse problem described under aspect 5 was shown with a litmus test only, it was not reproduced in the queue itself. `queue_test` on a Raspberry Pi 5 did not show it, the Apple M2 does not show it at all. A regression in the memory ordering of the queues will not be found by the existing tests.
