# multi_thread_demo — Worker Threads

Demonstrates XCP measurement of work distributed over a fixed set of worker threads.
The main loop starts batches of work items; worker threads fetch work items from a shared
lock-free counter, execute them and report completion. A single global event, triggered by
any worker, measures each completed work item.

---

## What it demonstrates

| Feature | How it is demonstrated |
|---|---|
| Event triggered from multiple threads | One global event `work_done`, triggered by whichever worker completed a work item |
| Stack addressing across threads | Worker local variables registered once, captured from the stack frame of the triggering thread |
| Lock-free work distribution | Shared atomic counter, fetched with a compare-and-swap loop, no thread pool library |
| Batch completion detection | Separate atomic completion counter with release/acquire ordering |
| Calibration as external request | Main loop starts a batch when `params.start_request` changes |
| Parallel speedup | Sum of work item durations / batch runtime |
| Generic work interface | The work is defined in `work.c` behind the interface `work.h`, `main.c` only knows work item indices |

### Files

| File | Purpose |
|---|---|
| `src/main.c` | Generic part — worker threads, work distribution, batch control, instrumentation |
| `src/work.h` | Work interface and threading contract |
| `src/work.c` | Work implementation — placeholder with simulated work, replace with the real work |
| `CANape/` | CANape project (A2L auto-upload, XCP UDP, port 5555) |

---

## How it works

```
main loop                                   worker threads (THREAD_COUNT)
---------                                   -----------------------------
work_init()
start_request changed or auto_restart:      loop:
  n = work_begin_batch()                      fetch_work(&index):  CAS gWorkRemaining n -> n-1 if n > 0
  gWorkDone = 0                               if fetched:
  gWorkRemaining = n  (release)                 work_execute(index)
                                                trigger event work_done
cyclic:                                         gWorkDone++  (release)
  gWorkDone == n (acquire)?                   else:
    -> work_complete(n)                         sleep poll_us
  trigger event mainloop
work_shutdown()
```

- Work item indices are handed out in ascending order `0..n-1`, `n` is the number of work items returned by `work_begin_batch`.
- A plain `atomic_fetch_sub` is not used, because idle workers polling an empty counter would underflow it.
- `gWorkRemaining == 0` means all work items are fetched, not completed. Completion is tracked by `gWorkDone`.
- A new batch is only started when the previous batch is completed.
- Idle workers poll with `params.poll_us`, so `work_latency` includes up to one poll interval.

### Work interface (`work.h`)

| Function | Called by | Purpose |
|---|---|---|
| `bool work_init(void)` | main thread, once, before the worker threads are started | Create calibration segments, register parameters and measurements, allocate resources |
| `uint32_t work_begin_batch(void)` | main thread, no batch running | Define the work items of a new batch and prepare their input data, return the number of work items (0 = no batch) |
| `void work_execute(uint32_t index)` | worker threads, concurrently, each index exactly once | Execute one work item |
| `bool work_complete(uint32_t items)` | main thread, after all work items are executed | Evaluate the batch result, false counts as batch error |
| `void work_shutdown(void)` | main thread, after the worker threads are joined | Release resources |

Data written in `work_begin_batch` is visible in `work_execute`, data written in `work_execute` is visible in `work_complete`.
Concurrent work items may write to data owned by their index (e.g. `result[index]`) without synchronization.

The placeholder in `work.c` creates batches of `work_params.items` work items, each sleeps for a pseudo random duration between 10% and 100% of `work_params.time_us`, deterministic per index,
and checks in `work_complete` that every work item was executed exactly once.

### Calibration parameters (`params`, `work_params`)

| Parameter | Default | Description |
|---|---|---|
| `start_request` | 0 | Increment to start a new batch |
| `auto_restart` | false | Start the next batch automatically when the previous one is completed |
| `poll_us` | 1000 | Worker poll interval when there is nothing to do |
| `cycle_us` | 10000 | Main loop cycle time |
| `work_params.items` | 100 | Number of work items in a batch (placeholder work) |
| `work_params.time_us` | 10000 | Maximum duration of a simulated work item (placeholder work) |

### Measurements

| Event | Variable | Description |
|---|---|---|
| `work_done` | `worker_id` | Index of the worker thread which completed the work item |
| `work_done` | `work_index` | Index of the completed work item |
| `work_done` | `work_time` | Duration of the work item (us) |
| `work_done` | `work_latency` | Time from batch start to start of the work item (us) |
| `mainloop` | `batch_count` | Number of completed batches |
| `mainloop` | `batch_errors` | Number of batches for which `work_complete` returned false |
| `mainloop` | `batch_running` | Batch is running |
| `mainloop` | `batch_size`, `work_remaining`, `work_done` | Batch progress |
| `mainloop` | `batch_time` | Runtime of the current or last batch (us) |
| `mainloop` | `batch_speedup` | Sum of work item durations / batch runtime of the last batch |

---

## Building

```bash
./build.sh examples
./build/multi_thread_demo
```

Or with CMake directly:

```bash
cmake -B build -S . -DXCPLITE_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target multi_thread_demo
./build/multi_thread_demo
```

Measure with xcpclient, starting batches continuously:

```bash
xcpclient --udp --upload-a2l --cal params.auto_restart 1 --mea worker_id work_index work_time work_latency batch_speedup --time 5
```

---

## CANape

Open `CANape/CANape.ini` in CANape. The project is pre-configured for XCP on UDP, port 5555,
with automatic A2L upload. If CANape cannot connect, verify the IP address in
*Device Configuration / Devices / XCP / Protocol / Transport Layer*.

The CANape configurations `xcp_demo.cna` and `context_demo.cna` still show the signals of the previous version of this demo
(`task_<n>.*`, `ctx_<n>`, `params.ampl`, ...) and need to be recreated for the worker thread signals.
