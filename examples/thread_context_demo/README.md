# thread_context_demo — context and spans for multiple instances

Demonstrates how XCPlite can measure individual time series when the same code runs as several class instances or in several threads, using concepts borrowed from OpenTelemetry (OTel): context, scope and span.

## Demo application

The demo has a class called `SignalGenerator`. Each instance of a signal generator creates a thread which calculates a configurable waveform in real time. Each instance has its own calibration parameters like `sample_time_us`,`signal_type`, `period`, `ampl`, ... .  The output samples can be read by other threads. A thread-safe poll function returns one sample (value, time and sequence number), or nothing when there is no new sample. The poll function reads from a lock-free SPSC queue owned by the signal generator. The queue size is a constructor parameter of the instance. If queue size is 1, the queue always holds the latest sample, a new sample overwrites an unread one.

The demo main loop polls 2 instances of `SignalGenerator` until both queues are empty and only then sleeps for calibration parameter `Mainloop.idle_sleep_us`. For each sample it measures the latency, the time from the calculation of the sample to its poll, and the number of samples dropped before it.

## What can be measured and observed

The demo shows how to instrument observation of generating the signal, the execution of the polling function, the queue level and overflow count in the SignalGenerator implementation, without knowing which instance it will be and who is calling the poll function. The code instrumentation creates consistent time series for each instance and from each calling threads perspective at runtime, so a standard XCP client tool or logger does not need to know how to interpret and demultiplex the data produced by capture points in code. The tool is able to select, which instance or caller it wants to observe. No data shall is transfered and collected for instances not configured. Metrics are provided by the caller instrumentation, such as linking to the producing span and latency_time of the link.  

The xcpclient tool has an option to convert the XCP measurement data to 'Chrome JSON trace format', which can be loaded in 'Perfetto' or many other tools. 



## Building and running

```bash
./build.sh examples
./build/thread_context_demo
```

Or with CMake directly:

```bash
cmake -B build -S . -DXCPLITE_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target thread_context_demo
./build/thread_context_demo
```

Measure all signals with xcpclient for 5 seconds into a CSV file:

```bash
xcpclient --udp --mea '.*' --time 5 --csv mea.csv
```


## Using CANape

The CANape project included can measure the span event signal, but can not provide a Gantt-Chart for the span nesting in a context.

<img src="CANape.png" alt="CANape overview: signals and span tracks" width="900">


## Using Perfetto

xcpclient can write the measurement as a trace file for [Perfetto](https://ui.perfetto.dev):

```bash
xcpclient --udp --mea '.*' --time 5 --perfetto trace.json
```

Open `trace.json` with "Open trace file" in the Perfetto UI. Each context is shown as a process with one track `spans` and a counter
track for each of its signals:

- `Startup`: the span `initialize`, if the measurement was started before the startup (see "Measuring the startup").
- `Mainloop`: `poll_loop` with the nested spans `get_channel1` and `get_channel2`, a `poll` nested in each of them, followed by
  `analyze`. The signals are `Mainloop.channel1`, `Mainloop.channel2`, `Mainloop.polled` and the sample signals `.value1`, `.latency1`, `.dropped1`, `.value2`, `.latency2`, `.dropped2`.
- `SigGen1`, `SigGen2`: `calc` with the nested span `clip`, and the signals `.value_`, `.time_us`, `.queue_level`, `.overflows_` and `.input`.

The main loop and the two task threads run independently, so the spans of different contexts overlap in time.
Details are in the [xcpclient README](../../tools/xcpclient/README.md).

<img src="perfetto1.png" alt="Perfetto overview: signals and span tracks of the contexts" width="900">

<img src="perfetto2.png" alt="Perfetto zoomed in: nested spans of the contexts" width="900">

## How it differs from OpenTelemetry

The thread-local current context, the rules for threads (capture in the parent, attach in the new thread,
detach at the end) and spans with nesting are taken over unchanged. Three things are different:

- **A context is named and long-lived.** In OTel a context is an anonymous trace id and span id, created
  per request, and every span is exported as a self-describing record. An XCP tool needs every signal
  declared in the A2L file with a name, an address and a fixed event. So there are a few named contexts,
  and each one gets its own events and measurements.
- **OTel separates instances with attributes, not with the context.** The idiomatic OTel solution is one
  signal name with an attribute such as `siggen.instance`, and the backend groups by it. A2L has no
  equivalent, so there is one signal per instance.
- **One guard instead of two.** OTel separates the lifetime of a span (`span->End()`) from being the
  current span of a thread (scope). `DaqSpan` combines both, which is enough for synchronous sections in
  one thread. A span which begins in one thread and ends in another can not be expressed.

The differences in the data model and in the treatment of time, order and causality are described in
[docs/TIME_AND_CAUSALITY.md](../../docs/TIME_AND_CAUSALITY.md).




## XCPlite API extensions

The XCPlite API extensions needed are part of the XCPlite C++ API, in [inc/xcplib_context.hpp](../../inc/xcplib_context.hpp) (included by `xcplib.hpp`). They need the option `OPTION_DAQ_CONTEXT_LIST`, which is currently enabled in the default and the shm configuration, the configurations with on-target A2L generation. It costs nothing, when not used.  

The API extensions follow the XCPlite principles: lazy registration, lock-less and no heap allocations. Currently only in the standard and shm configuration with on-target A2L generation. Link-time registration is work in progress.



| Term | Meaning |
|---|---|
| `xcp::Context` | A named identity, "who is executing". One per class instance here (`SigGen1`, `SigGen2`), and one for each phase of the main thread (`Startup`, `Mainloop`). |
| `xcp::Scope` | Makes a context the current context of the calling thread and restores the previous one at the end of the block (for context propagation). The current context is ambient in thread-local storage. |
| `DaqEventCtxVar` | Context-aware variant of `DaqEventVar`. Uses a separate event and separate measurements for each context, the event is named `<context>.<event>`. |
| `DaqSpan` | A named, timed section of code. Begin and end are recorded on the same event `<context>.spans` of the current context. |

```cpp
xcp::Context context_;                                  // class member, constructed with the instance name
xcp::Scope scope(context_);                             // in the thread or method which works for this instance
DaqEventCtxVar(siggen, A2L_MEAS_PHYS(value_, ...));     // event SigGen1.siggen, measurement SigGen1.value_
{ DaqSpan(calc); ... }                                  // span "calc", ends at the end of the block
```



---


# What the demo measures

An XCP event is bound to a code location. The demo has two instances of a signal generator class, each running its own thread. It has a thread-safe poll function, to be called by other threads to read values. Instrumenting both functions in the obvious way, with one `DaqEventVar(event_name, A2L_MEAS(variable_name, ...))` in the code, both instances and each caller triggers the same event and the tool receives one signal with mixed the values for instances or callers.

The existing solution (see `cpp_demo`) needs the instrumented code to know its instance and pass the name along: one dynamically created event per instance, and measurements explicitly registered with the instance name as prefix. Shared helper functions for the signal generator, which do not know for which instance they are working, can not be instrumented at all.



The demo has four contexts. `SigGen1` and `SigGen2` are the two signal generator instances, each attached to its own task thread.
`Startup` and `Mainloop` are attached to the main thread, one after the other.

| Event | Measurements | Triggered by |
|---|---|---|
| `SigGen1.siggen`, `SigGen2.siggen` | `.value_`, `.time_us` (time stamp of the sample), `.queue_level` (unread samples after the push), `.overflows_` (dropped samples) of `SigGen1` and `SigGen2` | the task thread of each instance |
| `SigGen1.clip`, `SigGen2.clip` | `SigGen1.input`, `SigGen2.input` | the shared function `Clip()` |
| `Mainloop.poll` | `Mainloop.polled` | the method `Poll()` of both instances, called by the main loop |
| `Mainloop.sample1`, `Mainloop.sample2` | `Mainloop.value1`, `.latency1`, `.dropped1` and `Mainloop.value2`, `.latency2`, `.dropped2` | the main loop, for each sample polled from instance 1 or 2 |
| `Mainloop.mainloop` | `Mainloop.channel1`, `Mainloop.channel2` (latest values) | the main loop, once per cycle |
| `Startup.spans`, `Mainloop.spans`, `SigGen1.spans`, `SigGen2.spans` | `.span`, `.level`, `.span_dt` | begin and end of each span in this context |

Spans:

| Context | Spans | Executed by |
|---|---|---|
| `Startup` | `initialize`: creates the two signal generator instances | main thread, once |
| `Mainloop` | `poll_loop` with the nested spans `get_channel1` and `get_channel2` in turns until both queues are empty, each with a nested `poll`, then `analyze` | main thread, after an idle sleep of `Mainloop.idle_sleep_us` |
| `SigGen1`, `SigGen2` | `calc` with the nested span `clip` | task thread of the instance, every 5 ms |

The span signals of one context show for example the sequence calc (level 1), clip (level 2), calc, none, with the
duration of the span that just ended in `span_dt`. Span names are displayed through the enum conversion
`conv.spans`, which the A2L generator creates from all span names registered when the A2L file is finalized.

The event names carry the context name, so the event list and the event groups of the tool show what an
event contains (`SigGen1.siggen` instead of a numbered event instance).

`Poll()` does not attach the context of its instance, it runs in the context of its caller. Its span `poll` is therefore nested in
`get_channel1` or `get_channel2` in the context `Mainloop`, and this tells which instance was polled. The event
`Mainloop.poll` is triggered for both instances, so `Mainloop.polled` contains the mixed values of both (just for demonstration). The alternative is in the code as a comment: with `xcp::Scope scope(context_)` in `Poll()`, the span and the event belong to the context of the instance
(`SigGen1.poll`, `SigGen1.polled`). The span is then not nested in the span of the caller, because attaching a context starts
without a current span, and the context is executed by two threads. That needs `OPTION_DAQ_SPAN_THREAD` in 'xcplib_cfg.h' to tell their spans apart.

`OPTION_DAQ_SPAN_THREAD` is not defined by default. With the option, the spans event of a context has the additional measurement
`.thread`, a small index of the thread which began or ended the span. The indices are assigned in the order in which the threads
begin their first span. In this demo each context is executed by one thread at a time, so the option is not needed.

The spans of the demo contain sleeps to simulate work, so that they are easy to see in a trace viewer with reduced datarate. Without that, on a hiher performancecomputer, the datarate goes up to about 10MByte/s and Perfetto show the precise span execution time in ns resolution.
The sleeps are enabled with `OPTION_SIMULATE_WORK` in [src/sig_gen.hpp](src/sig_gen.hpp), comment it out for a performance test.





### Measuring the startup

Experimental. Disabled by default.

The span `initialize` in the context `Startup` is executed once, before the main loop. To get it into a measurement, the demo
waits in `wait_xcp()` until the XCP client has started the measurement, and then begins with the startup.

This works from the second run on. The demo uses `A2L_MODE_WRITE_ONCE`, so the A2L file and the binary persistence file of the
first run are kept. In the next run, the events of all contexts are preloaded from the persistence file, and the client can set up
the measurement before any instrumented code has been executed.

1. First run: start the demo and stop it with Ctrl-C. There is no A2L file yet, the demo does not wait and prints
   `First run, thread_context_demo_V0.9.a2l is generated now, restart to measure the startup`. The A2L file and the `.bin` file
   are written at shutdown, or earlier when a client connects.
2. Second run: start the demo again. It prints `Waiting for the XCP client to start the measurement (Ctrl-C to abort) ...`.
3. Start the measurement with xcpclient or CANape. The demo continues with the startup and the main loop.

Span ids are not stored in the persistence file and need not be: with `OPTION_DAQ_SPAN_SECTION_REGISTRATION` (default) the span
names are registered at link time, the span ids are the same in every run of the same binary and `conv.spans` of the first run
is complete, including spans which were not executed in the first run. Without the option (Windows, SHM mode), span ids are
assigned in the order in which the spans are executed for the first time, and the A2L file of the first run is valid for the
second run only as long as this order is the same.

The constructor of the signal generator waits for the first cycle of its task, so the events of the contexts are created in the
same order in the first run.




## OpenTelemetry version for comparison

[otel/](otel/) contains the same demo instrumented with opentelemetry-cpp instead of XCP, as a reference
for reading. It is not built by the XCPlite build and has not been compiled. Build and run notes,
including a receiver for visualisation, are in the header of [otel/src/main.cpp](otel/src/main.cpp).


## Same context in multiple threads

In the current version of the demo each context's spans and context-aware events are triggered from only one thread.
Option OPTION_DAQ_SPAN_THREAD is not defined by default.  

Some places that look as if they might break this, but don't:

Poll(): it runs on the main thread, but the xcp::Scope scope(context_) is commented out, so its poll span lands in the caller's context Mainloop, not in SigGen1/SigGen2. Clip(): it's only called from Task(), so it's always on one of the generator threads.
Constructor, Start(), Stop(): they run on the main thread and create the context, but they don't begin any span or trigger any event in it.
Main and Mainloop: both on the main thread. One thread working in several contexts is fine. The option is only about the other direction: several threads in one context.

Two things to keep in mind:

It's a convention, nothing enforces it. If you enable the Scope in Poll(), SigGen1 gets spans from two threads (task and main), and without the option their begin and end records end up in one level stack and corrupt each other's nesting. The comment in Poll() already says that needs the option.
What the library actually needs is "one thread at a time" per context, not "exactly one". Handing a context from one thread to another without the option is fine, as long as the first thread has ended its spans before the next one begins.


## Span links and Perfetto arrows

Work in progress

---

## Implementation notes

An event index > 0 always means a context. The library knows the context names, and an event which belongs to a context is named
`<context>.<event>` in the A2L event list, the event groups, `conv.events` and `GET_DAQ_EVENT_INFO` (`SigGen1.siggen`).

Library

- Options `OPTION_DAQ_CONTEXT_LIST`, `OPTION_DAQ_CONTEXT_COUNT` (8), `OPTION_DAQ_SPAN_SECTION_REGISTRATION`,
  `OPTION_DAQ_SPAN_COUNT` (32) and `OPTION_DAQ_SPAN_THREAD`,
  see [docs/xcplib_cfg.md](../../docs/xcplib_cfg.md). Enabled in the default and the shm configuration, undefined in ptp, raw,
  no_a2l and rtos. `OPTION_DAQ_SPAN_THREAD` is not defined by default.
- [src/ctx.c](../../src/ctx.c), `src/ctx.h`: context list next to the event list, a lock-free name list
  ([docs/NAMELIST.md](../../docs/NAMELIST.md)). `XcpCreateContext`, `XcpGetContextName`, `XcpGetContextCount`,
  `XcpCreateContextEvent`.
- Span names: with `OPTION_DAQ_SPAN_SECTION_REGISTRATION` (default, not on Windows and not in SHM mode) `DaqSpan` emits a constant
  descriptor into the linker section `xcp_spans`, the span id is the position in the section. Otherwise `XcpRegisterSpanName`
  registers the name at runtime in a span name list, and `DaqSpan` caches the span id at the code location.
- [inc/xcplib_context.hpp](../../inc/xcplib_context.hpp), included by `xcplib.hpp`: `xcp::Context`, `xcp::Scope`, `xcp::Span`,
  `DaqContext`, `DaqSpan`, `DaqEventCtxVar`, `DaqEventCtxVar_`. There is no C instrumentation API for contexts.
- `DaqContext(name)` enters a context with a literal name for the enclosing block. The context object is a static at the code
  location and is created when the code location is executed for the first time, so code which is executed repeatedly does not
  search the context list each time. The demo creates its contexts with `xcp::Context` objects, because they are entered once
  (`Startup`, `Mainloop`) or have a name which is only known at runtime (the signal generator instances).
- The span event of a context is `<context>.spans`. A sample is the state after a span begin or end: `span`, `level`, `span_dt`
  and, with `OPTION_DAQ_SPAN_THREAD`, `thread`. The record has 8 bytes with and without the option, `level` has 8 bit with the
  option and 16 bit without.
- The A2L writer creates `conv.spans` at finalize from the registered span names.
- `XcpFindEvent` and `XcpCreateEvent` consider events without context only, so an event `<name>` and `<context>.<name>` can
  coexist. `XcpGetEventContext` returns the context id of an event. The A2L short name of an event is its name without the context.
- Test: `test/context_test`, registered with CTest (unit).

Persistence

- The context name is stored with each event in the binary persistence file, the context list is rebuilt from the events on load.
- `tools/bintool` shows an event which belongs to a context as `<context>.<event>`, with the context name and id.
- The demo uses `A2L_MODE_WRITE_ONCE`: after a change which affects the A2L file without a new EPK, for example toggling
  `OPTION_DAQ_SPAN_THREAD` or adding, renaming or reordering spans, delete the `.a2l` and `.bin` file in the working directory.
  The span ids are fixed for a binary, not for an EPK: a new build may have different span ids.
- Span names and span ids are not stored in the binary persistence file.

## Memory consumption

Memory is almost entirely fixed by three configuration options in `xcplib_cfg.h`. The number of
`DaqEventCtxVar` code locations matters mainly because each one needs event list entries.
The sizes are measured on a 64 bit build of the default configuration.

Fixed, allocated whether or not a context is used:

| Setting | Default | Per entry | Total |
|---|---|---|---|
| `OPTION_DAQ_EVENT_COUNT` | 32 | 28 bytes per event | 452 bytes |
| `OPTION_DAQ_CONTEXT_COUNT` | 8 | 16 bytes per context | 130 bytes |
| `OPTION_DAQ_SPAN_COUNT` | 32 | 32 bytes per span name | 1026 bytes |
| Event name buffer | | | 32 bytes |

The context feature itself adds about 0.5 KB of RAM with the defaults. With link time registration, a span name is a constant of
32 bytes per `DaqSpan` code location (8 in this demo), there is no span name table in RAM.

Per use, in the application:

| Item | Size | Count |
|---|---|---|
| `DaqEventCtxVar` code location (event state per context) | 2 x (`OPTION_DAQ_CONTEXT_COUNT` + 1) = 18 bytes | per code location |
| Mutex for the first-time registration | 64 bytes on macOS, 40 bytes on Linux | per distinct combination of measurement types, not per code location |
| `xcp::Context` object | 24 bytes | per context |
| Current context and span | 16 bytes thread-local | per thread which uses a scope or a span |
| Thread index (`OPTION_DAQ_SPAN_THREAD`) | 1 byte thread-local | per thread which uses a span |
| `DaqSpan` code location | 2 bytes and a static initialization guard | per code location |

The number of events is what has to be planned. With C contexts and L `DaqEventCtxVar` code locations:

```
events = C (one spans event per context) + C x L + code locations also executed without a context + other events
```

Only combinations which are actually executed create an event. Worst case with the default of 8 contexts:

| Code locations L | Events needed | Event list | Fits the default of 16 events |
|---|---|---|---|
| 1 | 16 | 448 bytes | yes, with no other event |
| 2 | 24 | 672 bytes | no |
| 4 | 40 | 1.1 KB | no |
| 8 | 72 | 2.0 KB | no |
| 16 | 136 | 3.8 KB | no |

This demo has 4 contexts and 4 code locations, of which `clip` and `siggen` are executed in 2 contexts and `poll` and `mainloop` in one,
which is 10 events. The limit is
1000 events (`OPTION_DAQ_EVENT_COUNT`). When the event list is full, `DaqEventCtxVar` measures nothing
for this context and code location, and tries to create the event again on every call.

Not affected by the number of contexts or code locations:

- `OPTION_DAQ_MEM_SIZE` (12288 bytes) is used with 6 bytes per signal which the tool selects for
  measurement, independent of how many signals are declared.
- The A2L generator writes to the file, it does not keep the declarations in memory.
- The binary persistence file grows by 256 bytes per event.


## Findings and limitations

- **Event budget.** One event per context, plus one per context and context-aware code location
  (10 events in this demo, `OPTION_DAQ_EVENT_COUNT` is 16 by default). The number of contexts is limited
  by `OPTION_DAQ_CONTEXT_COUNT` (8), the number of span names by `OPTION_DAQ_SPAN_COUNT` (32).
- **One thread at a time.** A context should be executed by one thread at a time. Several threads may work in it one after
  the other, the hand-over between them (a queue, a mutex, a join) keeps the timestamps of its events in order. A context
  and its spans describe one track of activity. For a worker pool, use one context per worker.
- **Declining timestamps.** The timestamps of an XCP event should not decline. If a context is attached
  to several threads at the same time (for example a task thread and a method called from another thread), this is not
  guaranteed. The order of the samples is decided when the event is queued, after the timestamp was taken, so a thread which
  is overtaken in between produces a sample with a declining timestamp. This applies to the span events of a context in which
  several threads work at the same time, and to a context-aware event triggered by several threads in the same context at the
  same code location. Nothing is serialized, a thread never waits for another one. With the thread index
  (`OPTION_DAQ_SPAN_THREAD`) the samples of each thread are still in order among themselves.
  `xcpclient` accepts a declining timestamp, other tools may not. Background, what was tried and why there is no lock:
  [docs/TIME_AND_CAUSALITY.md](../../docs/TIME_AND_CAUSALITY.md).
- **First execution.** The first execution of a `DaqEventCtxVar` code location in a context creates the event and registers
  the measurements. One thread does this (event list mutex, A2L lock). Other threads which execute the code location in the same
  context at this moment do not wait, they skip this one trigger. There is no mutex and no compiler generated once guard at the
  code locations of `DaqSpan`, `DaqContext` and `DaqEventCtxVar`.
- **A2L lazy generation.** Contexts and context-aware events must have been executed once before the A2L
  file is finalized (spans as well without `OPTION_DAQ_SPAN_SECTION_REGISTRATION`). Code which runs only once at startup can be measured from the second run on, see "Measuring the startup".
- **Span ids.** With `OPTION_DAQ_SPAN_SECTION_REGISTRATION` (default), span ids depend on the link order only: they are the same
  in every run of a binary, which is what `A2L_MODE_WRITE_ONCE` needs. A span name used at several code locations gets several
  span ids with the same name. With GCC, `DaqSpan` can not be used in inline functions, templates and member functions defined
  in the class body. Without the option, span ids depend on the order in which the spans are executed for the first time, this
  order must then not change between runs, otherwise `conv.spans` of the existing A2L file shows wrong span names.
- **Thread index.** `.thread` is an index local to the process, not the thread id of the operating system, and limited to 255 threads.
  Without `OPTION_DAQ_SPAN_THREAD`, the spans of several threads in one context can not be told apart.
- **Attach starts without a span.** The current span of the capturing thread is not carried over.
- **Default and shm configuration only.** Needs runtime event creation and on-target A2L generation.
  In SHM mode, each application has its own namespace for context names, see [docs/SHM.md](../../docs/SHM.md).
- **Library implementation.** The context names are kept in a context list in the C core
  ([src/ctx.c](../../src/ctx.c)), an event stores the id of its context. The binary persistence file
  stores the context name with each event, so event ids are stable across restarts. 
- **C++ only.** There is no C instrumentation API for contexts.



## Open questions

- Should a context capture the current context when the instance does not know its own name?
- How far should the naming follow OTel? An OTel tracer has a name but no id, it is only a factory for
  spans. The trace id belongs to the trace, which is the set of all spans sharing that id.
- SHM configuration: how to make event creation process safe? The context list and the span name list are lock-free name lists
  which several processes may use at the same time, but a context needs its `spans` event and the event list is protected by a
  process local mutex. Not tested with xcpdaemon yet.

## Remaining todos

- `span_dt` is not attributed to a span name, it is attributed on the context, an end sample carries the duration.
- Declining timestamps are possible when several threads trigger the same event at the same time (span events of a context,
  context-aware events). Using the newest timestamp of the context instead of an older one was tried and removed: with more
  than two threads it gave more declining timestamps than no correction. An exact solution without a lock
  is to take the timestamp inside the acquire loop of the transmit queue, between loading the head and the compare-and-swap.
  Not implemented, the cost in the queue is not measured.
- Update the CANape project for thread_context_demo.
- The OTel version in [otel/](otel/) has never been compiled.

## Possible follow-ups

- Optimize: Avoid the repeated lookup of names
- Perfetto native protobuf format, if the JSON files get too large (about 90 bytes per span or signal sample).
- Contexts and spans for no_a2l/rtos targets (needs link-time registration).
- A group per context in the A2L file, containing the event groups of the context.
- Optional connection to a real OTel backend, on target through opentelemetry-cpp or off target by
  exporting span records from xcpclient.

## Things to know about xcpclient

- **Span signals must be measured.** `--perfetto` finds a context by its signals `<context>.span`, `.level` and `.span_dt`. Other
  signals are assigned to a context only by the name prefix `<context>.`.
- **Trace timestamps** are relative to the measurement start. The file is not sorted by time, Perfetto sorts on import.
- **Bool signals are enums.** `conv.bool` is read as `0 "false" 1 "true"`, so bools are printed as labels and are state tracks
  in Perfetto.




## Example debug output

```
Created Context 'Main': 1
Create Event 0: spans context=1, cycle=0ns, prio=0
Register measurements for type SpanRecord 
Set scope to context 'Main', thread_id=4021395904
Register span 'initialize': id = 1
Trigger span event 0, span 'initialize': level=1, dt=0ns
Span begin, 'Main': 'none' -> 'initialize', 1
Create CalSeg 'SigGen1' size=40, type=seg
Created Context 'SigGen1': 2
Create Event 1: spans context=2, cycle=0ns, prio=0
Register measurements for type SpanRecord 
Starting task: SigGen1
Set scope to context 'SigGen1', thread_id=1796435968
Register span 'calc': id = 2
Trigger span event 1, span 'calc': level=1, dt=0ns
Span begin, 'SigGen1': 'none' -> 'calc', 1
Register span 'clip': id = 3
Trigger span event 1, span 'clip': level=2, dt=0ns
Span begin, 'SigGen1': 'calc' -> 'clip', 2
Create Event 2: clip context=2, cycle=0ns, prio=0
Span end, 'SigGen1': 'clip' -> 'calc', 2
Trigger span event 1, span 'calc': level=1, dt=1266541ns
Span end, 'SigGen1': 'calc' -> 'none', 1
Trigger span event 1, span 'none': level=0, dt=3542833ns
Create Event 3: siggen context=2, cycle=0ns, prio=0
Create CalSeg 'SigGen2' size=40, type=seg
Created Context 'SigGen2': 3
Create Event 4: spans context=3, cycle=0ns, prio=0
Register measurements for type SpanRecord 
Starting task: SigGen2
Set scope to context 'SigGen2', thread_id=1797009408
Trigger span event 4, span 'calc': level=1, dt=0ns
Span begin, 'SigGen2': 'none' -> 'calc', 1
Trigger span event 4, span 'clip': level=2, dt=0ns
Span begin, 'SigGen2': 'calc' -> 'clip', 2
Create Event 5: clip context=3, cycle=0ns, prio=0
Trigger span event 1, span 'calc': level=1, dt=0ns
Span begin, 'SigGen1': 'none' -> 'calc', 1
Span end, 'SigGen2': 'clip' -> 'calc', 2
Trigger span event 4, span 'calc': level=1, dt=1621542ns
Span end, 'SigGen2': 'calc' -> 'none', 1
Trigger span event 4, span 'none': level=0, dt=5522334ns
Create Event 6: siggen context=3, cycle=0ns, prio=0
Trigger span event 1, span 'clip': level=2, dt=0ns
Span begin, 'SigGen1': 'calc' -> 'clip', 2
Span end, 'SigGen1': 'clip' -> 'calc', 2
Trigger span event 1, span 'calc': level=1, dt=1288166ns
Span end, 'SigGen1': 'calc' -> 'none', 1
Trigger span event 1, span 'none': level=0, dt=3827916ns
Trigger span event 4, span 'calc': level=1, dt=0ns
Span begin, 'SigGen2': 'none' -> 'calc', 1
Trigger span event 4, span 'clip': level=2, dt=0ns
Span begin, 'SigGen2': 'calc' -> 'clip', 2
Trigger span event 1, span 'calc': level=1, dt=0ns
Span begin, 'SigGen1': 'none' -> 'calc', 1
Span end, 'SigGen2': 'clip' -> 'calc', 2
Trigger span event 4, span 'calc': level=1, dt=1280625ns
Span end, 'SigGen2': 'calc' -> 'none', 1
Trigger span event 4, span 'none': level=0, dt=3811459ns
Trigger span event 1, span 'clip': level=2, dt=0ns
Span begin, 'SigGen1': 'calc' -> 'clip', 2
Span end, 'Main': 'initialize' -> 'none', 1
Trigger span event 0, span 'none': level=0, dt=31860875ns
Create CalSeg 'Mainloop' size=4, type=seg
Starting main loop...
Created Context 'Mainloop': 4
Create Event 7: spans context=4, cycle=0ns, prio=0
Register measurements for type SpanRecord 
Set scope to context 'Mainloop', thread_id=4021395904
Register span 'poll_loop': id = 4
Trigger span event 7, span 'poll_loop': level=1, dt=0ns
Span begin, 'Mainloop': 'none' -> 'poll_loop', 1
Register span 'get_channel1': id = 5
Trigger span event 7, span 'get_channel1': level=2, dt=0ns
Span begin, 'Mainloop': 'poll_loop' -> 'get_channel1', 2
Register span 'poll': id = 6
Trigger span event 7, span 'poll': level=3, dt=0ns
Span begin, 'Mainloop': 'get_channel1' -> 'poll', 3
Span end, 'SigGen1': 'clip' -> 'calc', 2
Trigger span event 1, span 'calc': level=1, dt=1267875ns
Span end, 'SigGen1': 'calc' -> 'none', 1
Trigger span event 1, span 'none': level=0, dt=3808708ns
Create Event 8: poll context=4, cycle=0ns, prio=0
Span end, 'Mainloop': 'poll' -> 'get_channel1', 3
Trigger span event 7, span 'get_channel1': level=2, dt=1166875ns
Create Event 9: sample1 context=4, cycle=0ns, prio=0
Span end, 'Mainloop': 'get_channel1' -> 'poll_loop', 2
Trigger span event 7, span 'poll_loop': level=1, dt=1473459ns
Register span 'get_channel2': id = 7
Trigger span event 7, span 'get_channel2': level=2, dt=0ns
Span begin, 'Mainloop': 'poll_loop' -> 'get_channel2', 2
Trigger span event 7, span 'poll': level=3, dt=0ns
Span begin, 'Mainloop': 'get_channel2' -> 'poll', 3
Span end, 'Mainloop': 'poll' -> 'get_channel2', 3
Trigger span event 7, span 'get_channel2': level=2, dt=1261167ns
Create Event 10: sample2 context=4, cycle=0ns, prio=0
Span end, 'Mainloop': 'get_channel2' -> 'poll_loop', 2
Trigger span event 7, span 'poll_loop': level=1, dt=1533750ns
Trigger span event 7, span 'get_channel1': level=2, dt=0ns
Span begin, 'Mainloop': 'poll_loop' -> 'get_channel1', 2
Trigger span event 7, span 'poll': level=3, dt=0ns
Span begin, 'Mainloop': 'get_channel1' -> 'poll', 3
Span end, 'Mainloop': 'poll' -> 'get_channel1', 3
Trigger span event 7, span 'get_channel1': level=2, dt=1264416ns
Trigger span event 4, span 'calc': level=1, dt=0ns
Span begin, 'SigGen2': 'none' -> 'calc', 1
Span end, 'Mainloop': 'get_channel1' -> 'poll_loop', 2
Trigger span event 7, span 'poll_loop': level=1, dt=1529166ns
Trigger span event 7, span 'get_channel2': level=2, dt=0ns
Span begin, 'Mainloop': 'poll_loop' -> 'get_channel2', 2
Trigger span event 7, span 'poll': level=3, dt=0ns
Span begin, 'Mainloop': 'get_channel2' -> 'poll', 3
Span end, 'Mainloop': 'poll' -> 'get_channel2', 3
Trigger span event 7, span 'get_channel2': level=2, dt=4500ns
Span end, 'Mainloop': 'get_channel2' -> 'poll_loop', 2
Trigger span event 7, span 'poll_loop': level=1, dt=269458ns
Trigger span event 7, span 'get_channel1': level=2, dt=0ns
Span begin, 'Mainloop': 'poll_loop' -> 'get_channel1', 2
Trigger span event 7, span 'poll': level=3, dt=0ns
Span begin, 'Mainloop': 'get_channel1' -> 'poll', 3
Span end, 'Mainloop': 'poll' -> 'get_channel1', 3
Trigger span event 7, span 'get_channel1': level=2, dt=1280667ns
Span end, 'Mainloop': 'get_channel1' -> 'poll_loop', 2
Trigger span event 7, span 'poll_loop': level=1, dt=1574500ns
Trigger span event 7, span 'get_channel2': level=2, dt=0ns
Span begin, 'Mainloop': 'poll_loop' -> 'get_channel2', 2
Trigger span event 7, span 'poll': level=3, dt=0ns
Span begin, 'Mainloop': 'get_channel2' -> 'poll', 3
Span end, 'Mainloop': 'poll' -> 'get_channel2', 3
Trigger span event 7, span 'get_channel2': level=2, dt=13750ns
Span end, 'Mainloop': 'get_channel2' -> 'poll_loop', 2
Trigger span event 7, span 'poll_loop': level=1, dt=300709ns
Trigger span event 7, span 'get_channel1': level=2, dt=0ns
Span begin, 'Mainloop': 'poll_loop' -> 'get_channel1', 2
Trigger span event 7, span 'poll': level=3, dt=0ns
Span begin, 'Mainloop': 'get_channel1' -> 'poll', 3
Span end, 'Mainloop': 'poll' -> 'get_channel1', 3
Trigger span event 7, span 'get_channel1': level=2, dt=15333ns
Trigger span event 4, span 'clip': level=2, dt=0ns
Span begin, 'SigGen2': 'calc' -> 'clip', 2
Span end, 'Mainloop': 'get_channel1' -> 'poll_loop', 2
Trigger span event 7, span 'poll_loop': level=1, dt=295459ns
Trigger span event 7, span 'get_channel2': level=2, dt=0ns
Span begin, 'Mainloop': 'poll_loop' -> 'get_channel2', 2
Trigger span event 7, span 'poll': level=3, dt=0ns
Span begin, 'Mainloop': 'get_channel2' -> 'poll', 3
Span end, 'Mainloop': 'poll' -> 'get_channel2', 3
Trigger span event 7, span 'get_channel2': level=2, dt=9417ns
Span end, 'Mainloop': 'get_channel2' -> 'poll_loop', 2
Trigger span event 7, span 'poll_loop': level=1, dt=282208ns
Span end, 'Mainloop': 'poll_loop' -> 'none', 1
Trigger span event 7, span 'none': level=0, dt=7280042ns
Create Event 11: mainloop context=4, cycle=0ns, prio=0
Register span 'analyze': id = 8
Trigger span event 7, span 'analyze': level=1, dt=0ns
Span begin, 'Mainloop': 'none' -> 'analyze', 1
Trigger span event 1, span 'calc': level=1, dt=0ns
Span begin, 'SigGen1': 'none' -> 'calc', 1
Span end, 'SigGen2': 'clip' -> 'calc', 2
Trigger span event 4, span 'calc': level=1, dt=1269000ns
Span end, 'SigGen2': 'calc' -> 'none', 1
Trigger span event 4, span 'none': level=0, dt=3798959ns
Span end, 'Mainloop': 'analyze' -> 'none', 1
Trigger span event 7, span 'none': level=0, dt=1259333ns
End scope of context 'Mainloop', back to context 'Main', thread_id=4021395904
Trigger span event 1, span 'clip': level=2, dt=0ns
Span begin, 'SigGen1': 'calc' -> 'clip', 2
Span end, 'SigGen1': 'clip' -> 'calc', 2
Trigger span event 1, span 'calc': level=1, dt=1211667ns
Span end, 'SigGen1': 'calc' -> 'none', 1
Trigger span event 1, span 'none': level=0, dt=3777084ns
End scope of context 'SigGen2', back to context 'none', thread_id=1797009408
Stopped task: SigGen2, samples: 3, dropped: 1
End scope of context 'SigGen1', back to context 'none', thread_id=1796435968
Stopped task: SigGen1, samples: 4, dropped: 0
Finalizing A2L file ...
A2L include file 'thread_context_demo_include.a2l' finalized: 31 measurements, 0 params, 2 typedefs, 7 components, 3 instances, 1 conversions
Write A2L file thread_context_demo.a2l, project_name 'thread_context_demo', epk 'V100'
XcpSetA2lName set to 'thread_context_demo'
A2L file 'thread_context_demo.a2l' finalized
Disconnect and shutdown XCP!
Terminate server threads
End scope of context 'Main', back to context 'none', thread_id=4021395904
```



## Old implementation in multi_thread_demo

```c

//-----------------------------------------------------------------------------------------------------
// Thread context
// Experimental

#ifdef EXPERIMENTAL_THREAD_CONTEXT

// Thread context structure
typedef struct {
    char name[MAX_THREAD_NAME_LENGTH + 1]; // Human-readable thread name
    tXcpEventId id;                        // XCP event ID for this thread
    tXcpEventId span_id;                   // XCP event ID for the current span
    uint32_t level;                        // Current span level
} tXcpContext;

// Global thread-local context
THREAD_LOCAL tXcpContext gXcpContext = {0};

// Inline accessors for maximum performance
static inline tXcpContext *XcpGetContext(void) { return &gXcpContext; }
static inline const char *XcpGetContextName(void) { return gXcpContext.name; }

// Begin a span, create a span event once
// Trigger the context event on entry
#define BeginSpan(name)                                                                                                                                                            \
    uint64_t span_t1 = ApplXcpGetClock64();                                                                                                                                        \
    uint64_t span_dt;                                                                                                                                                              \
    static _Atomic tXcpEventId span_id = XCP_UNDEFINED_EVENT_ID;                                                                                                                   \
    tXcpEventId old_span_id = XCP_UNDEFINED_EVENT_ID;                                                                                                                              \
    if (atomic_compare_exchange_strong_explicit(&span_id, &old_span_id, 1, memory_order_relaxed, memory_order_relaxed)) {                                                          \
        A2lLock();                                                                                                                                                                 \
        span_id = XcpCreateEvent(name, 0, 0);                                                                                                                                      \
        A2lSetStackAddrMode_i(span_id);                                                                                                                                            \
        A2lCreatePhysMeasurementInstance(name, span_dt, "Span runtime", "ns", 0.0, 0.1000000);                                                                                     \
        A2lUnlock();                                                                                                                                                               \
    }                                                                                                                                                                              \
    tXcpContext *ctx = XcpGetContext();                                                                                                                                            \
    tXcpEventId previous_span_id = ctx->span_id;                                                                                                                                   \
    ctx->span_id = span_id;                                                                                                                                                        \
    ctx->level++;                                                                                                                                                                  \
    XcpEventExtAt_Var(ctx->id, span_t1, 2, xcp_get_frame_addr(), (const uint8_t *)ctx);

// End span
// Trigger the span event and the context event on exit
// Measure execution time of the span
#define EndSpan()                                                                                                                                                                  \
    uint64_t span_t2 = ApplXcpGetClock64();                                                                                                                                        \
    span_dt = span_t2 - span_t1;                                                                                                                                                   \
    XcpEventExtAt_Var(ctx->span_id, span_t2, 2, xcp_get_frame_addr(), (const uint8_t *)ctx);                                                                                       \
    ctx->span_id = previous_span_id;                                                                                                                                               \
    ctx->level--;                                                                                                                                                                  \
    XcpEventExtAt_Var(ctx->id, span_t2, 2, xcp_get_frame_addr(), (const uint8_t *)ctx);

// Create a named context
// Create the context event (name is 'context_name'_'context_index')
// Register the context struct  as A2L typedef instance tied to the context event
static uint16_t XcpCreateContext(const char *context_name, uint16_t context_index) {

    if (!XcpIsActivated()) {
        return XCP_UNDEFINED_EVENT_ID; // Return undefined event ID if XCP is not activated
    }

    // Once:
    // Create a typedef for the thread context
    // Uses the predefined enum conversion rule for the event names
    A2lOnce() {
        A2lLock();
        A2lTypedefBegin(tXcpContext, &gXcpContext, "A2L typedef for tXcpContext");
        A2lTypedefPhysMeasurementComponent(span_id, "function span id", "conv.events", 0, 32);
        A2lTypedefMeasurementComponent(level, "function span nesting level");
        A2lTypedefEnd();
        A2lUnlock();
    }

    // Init thread local context
    // Create a unique name from index
    // Create an XCP event for this context
    snprintf(gXcpContext.name, sizeof(gXcpContext.name), "%s_%u", context_name, context_index);
    gXcpContext.id = XcpCreateEvent(gXcpContext.name, 0, 0);
    gXcpContext.span_id = gXcpContext.id;
    gXcpContext.level = 0;

    // Create a measurement typedef instance with the context name
    A2lLock();
    A2lSetRelativeAddrMode_i(gXcpContext.id, &gXcpContext); // Set relative addressing mode for the thread context
    A2lCreateTypedefNamedInstance(gXcpContext.name, gXcpContext, tXcpContext, "Thread local context");
    A2lUnlock();

    printf("Thread context created: name = %s, id = %u\n", gXcpContext.name, gXcpContext.id);
    return gXcpContext.id;
}

#else
#define BeginSpan(name)
#define EndSpan()
#endif

```