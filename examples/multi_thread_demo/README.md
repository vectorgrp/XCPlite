# multi_thread_demo — Worker Threads

Demonstrates XCP measurement of work distributed over a fixed set of worker threads.
The main loop starts batches of work items; worker threads fetch work items from a shared
lock-free counter, execute them and report completion. A single global event, triggered by
any worker, measures each completed work item.

The example work renders the Mandelbrot set line by line into `mandelbrot.png`, the line number
is the work item index. It is ported from the `rayon_demo` of [XCP-Lite Rust](https://github.com/vectorgrp/xcp-lite),
inspired by the Mandelbrot example in the book "Programming Rust" by Jim Blandy and Jason Orendorff.

---

## What it demonstrates

| Feature | How it is demonstrated |
|---|---|
| Event triggered from multiple threads | One global event `work_done`, triggered by whichever worker completed a work item |
| Stack addressing across threads | Worker local variables registered once, captured from the stack frame of the triggering thread |
| Lock-free work distribution | Shared atomic counter, fetched with a compare-and-swap loop, no thread pool library |
| Batch completion detection | Separate atomic completion counter with release/acquire ordering |
| Calibration as external request | Main loop starts a batch when `params.start_request` changes, or when the work reports changed input (`params.auto_restart`) |
| Parallel speedup | Sum of work item durations / batch runtime |
| Uneven work item cost | Lines crossing the Mandelbrot set take more iterations, balanced by fetching one line at a time |
| Generic work interface | The work is defined in `work.c` behind the interface `work.h`, `main.c` only knows work item indices |

### Files

| File | Purpose |
|---|---|
| `src/main.c` | Generic part — worker threads, work distribution, batch control, instrumentation |
| `src/work.h` | Work interface and threading contract |
| `src/work.c` | Work implementation — Mandelbrot set, replace with the real work |
| `src/stb_image_write.h` | PNG writer, single header library from [stb](https://github.com/nothings/stb), public domain or MIT |
| `CANape/` | CANape project (A2L auto-upload, XCP UDP, port 5555) |

---

## How it works

```
main loop                                   worker threads (THREAD_COUNT)
---------                                   -----------------------------
work_init()
start_request changed or auto_restart:      loop:
  n = work_begin_batch(req)                   fetch_work(&index):  CAS gWorkRemaining n -> n-1 if n > 0
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
| `uint32_t work_begin_batch(bool requested)` | main thread, no batch running, on request or cyclic with `auto_restart` | Define the work items of a new batch and prepare their input data, return the number of work items (0 = no batch). `requested` is true on an explicit `start_request` |
| `void work_execute(uint32_t index)` | worker threads, concurrently, each index exactly once | Execute one work item |
| `bool work_complete(uint32_t items)` | main thread, after all work items are executed | Evaluate the batch result, false counts as batch error |
| `void work_shutdown(void)` | main thread, after the worker threads are joined | Release resources |

Data written in `work_begin_batch` is visible in `work_execute`, data written in `work_execute` is visible in `work_complete`.
Concurrent work items may write to data owned by their index (e.g. `result[index]`) without synchronization.

### Mandelbrot work (`work.c`)

| Function | Mandelbrot |
|---|---|
| `work_init` | Calibration segment `mandelbrot`, pixel buffer, RGB image and color map |
| `work_begin_batch` | Copy the calibration parameters for the whole batch, compute the area on the complex plane, return the number of lines. With auto restart, a new image is only rendered when the parameters changed |
| `work_execute(line)` | Render one line: iteration count per pixel, at most 254 iterations, 255 marks a point in the set |
| `work_complete` | Map the iteration counts to colors of the visible spectrum and write `mandelbrot.png` to the current directory |
| `work_shutdown` | Free the buffers |

The resolution is `X_RES` x `Y_RES` = 2048 x 1536 (`#define` in `work.c`). Each line is written only by the worker which renders it, no synchronization needed.
Rendering takes about 0.6 s with 8 worker threads, writing the PNG file about 0.8 s on the main thread (MacBook Pro M3, debug build).

### Calibration parameters (`params`, `mandelbrot`)

| Parameter | Default | Description |
|---|---|---|
| `start_request` | 0 | Increment to start a new batch |
| `auto_restart` | true | Ask the work for a new batch cyclically while no batch is running, the work decides if there is something to do |
| `poll_us` | 1000 | Worker poll interval when there is nothing to do |
| `cycle_us` | 10000 | Main loop cycle time |
| `mandelbrot.x` | -0.8015 | Center of the area to render, real part |
| `mandelbrot.y` | 0.1561 | Center of the area to render, imaginary part |
| `mandelbrot.width` | 0.0055 | Width of the area to render, the height follows from the aspect ratio (-0.5, 0.0, 3.0 shows the complete set) |

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

The first image is rendered at startup. Change the area with xcpclient and measure the rendering of the new image:

```bash
xcpclient --udp --upload-a2l --cal mandelbrot.width 0.02 --mea worker_id work_index work_time work_latency batch_speedup --time 5
```

---

## CANape

Open `CANape/CANape.ini` in CANape. The project is pre-configured for XCP on UDP, port 5555,
with automatic A2L upload. If CANape cannot connect, verify the IP address in
*Device Configuration / Devices / XCP / Protocol / Transport Layer*.

The CANape configurations `xcp_demo.cna` and `context_demo.cna` still show the signals of the previous version of this demo
(`task_<n>.*`, `ctx_<n>`, `params.ampl`, ...) and need to be recreated for the worker thread signals.
