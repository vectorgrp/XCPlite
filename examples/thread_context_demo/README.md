# thread_context_demo — context and spans for multiple instances

Demonstrates how XCPlite can measure individual time series when the same code runs as several class instances or in several threads, using concepts borrowed from OpenTelemetry (OTel): context, scope and span.

## Demo application

The demo has a class called `SignalGenerator`. Each instance of a signal generator creates a thread which calculates a configurable waveform in real time. Each instance has its own calibration parameters like `sample_time_us`, `signal_type`, `period`, `ampl`, ... An instance is created stopped, `Start()` clears its output queue and starts the calculation, `Stop()` stops it. The output samples can be read by other threads. The poll function returns one sample (value, time and sequence number), or nothing when there is no new sample. It may be called from any thread, but from only one thread at a time: it reads from a lock-free single producer single consumer (SPSC) queue owned by the signal generator. The queue size is a constructor parameter of the instance. If the queue size is 1, the queue always holds the latest sample, a new sample overwrites an unread one.

The demo main loop polls 2 instances of `SignalGenerator` until both queues are empty and only then sleeps for calibration parameter `Mainloop.idle_sleep_us`. For each sample it measures the latency, the time from the calculation of the sample to its poll, and the number of samples dropped before it.

## What can be measured and observed

The demo shows how to observe the signal generation, the execution of the poll function, the queue level and the overflow count inside the `SignalGenerator` implementation, without the code knowing which instance it is and who calls the poll function. The instrumentation creates a consistent time series for each instance and from the perspective of each calling thread at runtime, so a standard XCP client tool or logger does not need to know how to interpret and demultiplex the data produced by the capture points in the code. The tool selects which instance or caller it observes, no data is transferred and collected for instances which are not selected. The caller instrumentation provides metrics such as the latency of a sample, and a span link from the consumer span to the producer span of each sample, which Perfetto shows as an arrow, see "Span links and Perfetto arrows".

The xcpclient tool has an option to convert the XCP measurement data to the Chrome JSON trace format, which can be loaded in Perfetto and many other tools.

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
or
```bash
xcpclient --udp --dest-addr 192.168.0.206 --upload-a2l --mea '.*' --time 5 --perfetto trace.json
```

Open `trace.json` with "Open trace file" in the Perfetto UI. Each context is shown as a process with one track `spans` and a counter
track for each of its signals:

- `Main`: the span `initialize`, if the measurement was started before the startup (see "Measuring the startup").
- `Mainloop`: `poll_loop` with the nested spans `get_channel1` and `get_channel2`, a `poll` nested in each of them, followed by
  `analyze`. The signals are `Mainloop.channel1`, `Mainloop.channel2`, `Mainloop.polled` and the sample signals `.value1`, `.latency1`, `.dropped1`, `.value2`, `.latency2`, `.dropped2`.
- `SigGen1`, `SigGen2`: `task` with the nested span `calc`, which has the nested span `clip`, and the signals `.value_`, `.time_us`, `.queue_level`, `.overflows_` and `.input`.

The main loop and the two task threads run independently, so the spans of different contexts overlap in time.
Each `poll` of `Mainloop` which returned a sample has an arrow from the `task` span of the signal generator which calculated it, see
"Span links and Perfetto arrows". Details are in the [xcpclient README](../../tools/xcpclient/README.md).

<img src="perfetto1.png" alt="Perfetto overview: signals and span tracks of the contexts" width="900">

<img src="perfetto2.png" alt="Perfetto zoomed in: nested spans of the contexts" width="900">

<img src="perfetto3.png" alt="Perfetto zoomed in: poll_loop" width="900">

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

The XCPlite API extensions needed are part of the XCPlite C++ API, in [inc/xcplib_context.hpp](../../inc/xcplib_context.hpp) (included by `xcplib.hpp`). They need the option `OPTION_DAQ_CONTEXT_LIST`, which is enabled in the default and the shm configuration, the configurations with on-target A2L generation. They cost nothing when not used.

The API extensions follow the XCPlite principles: lazy registration, lock-free and no heap allocations. Span names are registered at link time (`OPTION_DAQ_SPAN_SECTION_REGISTRATION`), contexts and their events at runtime. Contexts for no_a2l and rtos targets would need link-time registration of contexts and events as well (see "Possible follow-ups").

| Term | Meaning |
|---|---|
| `xcp::Context` | A named identity, "who is executing". One per class instance here (`SigGen1`, `SigGen2`), and one for each phase of the main thread (`Main`, `Mainloop`). |
| `xcp::Scope` | Makes a context the current context of the calling thread and restores the previous one at the end of the block (for context propagation). The current context is ambient in thread-local storage. |
| `DaqContext` | Creates a context with a literal name once per code location and enters it with a scope until the end of the block. Used here for `Main` and `Mainloop`. |
| `DaqEventCtxVar` | Context-aware variant of `DaqEventVar`. Uses a separate event and separate measurements for each context, the event is named `<context>.<event>`. |
| `DaqSpan` | A named, timed section of code. Begin and end are recorded on the same event `<context>.spans` of the current context. |
| `xcp::SpanLink` | The span context of a producer span (context, span, thread, timestamp), captured with `xcp::CaptureSpanLink()` and carried to a consumer, for example in a queue item. The consumer records it with `xcp::LinkSpan(link)` inside its own span, on the event `<context>.links` of its current context. An OTel span link. |

```cpp
xcp::Context context_;                                  // class member, constructed with the instance name
xcp::Scope scope(context_);                             // in the thread or method which works for this instance
DaqEventCtxVar(siggen, A2L_MEAS_PHYS(value_, ...));     // event SigGen1.siggen, measurement SigGen1.value_
{ DaqSpan(calc); ... }                                  // span "calc", ends at the end of the block
DaqContext(Mainloop);                                   // context with a literal name, entered until the end of the block
queue_.Push(SampleT{value_, ..., xcp::CaptureSpanLink()}); // producer, inside span "task": capture the span context
xcp::LinkSpan(sample->link);                            // consumer, inside span "poll": link it to the producer span
```

---

## What the demo measures

An XCP event is bound to a code location. The demo has two instances of a signal generator class, each running its own thread, and a poll function which other threads call to read the values. Instrumented in the obvious way, with one `DaqEventVar(event_name, A2L_MEAS(variable_name, ...))` in the code, both instances and all callers trigger the same event, and the tool receives one signal with the values of all instances or callers mixed.

The existing solution (see `cpp_demo`) needs the instrumented code to know its instance and pass the name along: one dynamically created event per instance, and measurements explicitly registered with the instance name as prefix. Shared helper functions for the signal generator, which do not know for which instance they are working, can not be instrumented at all.

The demo has four contexts. `SigGen1` and `SigGen2` are the two signal generator instances, each attached to its own task thread.
`Main` and `Mainloop` are attached to the main thread, `Main` for the whole run and `Mainloop` for each cycle of the main loop.

| Event | Measurements | Triggered by |
|---|---|---|
| `SigGen1.siggen`, `SigGen2.siggen` | `.value_`, `.time_us` (time stamp of the sample), `.queue_level` (unread samples after the push), `.overflows_` (dropped samples) of `SigGen1` and `SigGen2` | the task thread of each instance |
| `SigGen1.clip`, `SigGen2.clip` | `SigGen1.input`, `SigGen2.input` | the shared function `Clip()` |
| `Mainloop.poll` | `Mainloop.polled` | the method `Poll()` of both instances, called by the main loop |
| `Mainloop.links` | `Mainloop.link_context`, `.link_span`, `.link_time` (the producer span of the sample) | `xcp::LinkSpan()` in `Poll()`, for each sample |
| `Mainloop.sample1`, `Mainloop.sample2` | `Mainloop.value1`, `.latency1`, `.dropped1` and `Mainloop.value2`, `.latency2`, `.dropped2` | the main loop, for each sample polled from instance 1 or 2 |
| `Mainloop.mainloop` | `Mainloop.channel1`, `Mainloop.channel2` (latest values) | the main loop, once per cycle |
| `Main.spans`, `Mainloop.spans`, `SigGen1.spans`, `SigGen2.spans` | `.span`, `.level`, `.span_dt` | begin and end of each span in this context |

That is 13 events: 4 spans events, 2 for `clip` and 2 for `siggen` (one per instance), 4 for the code locations of the main loop and
`Poll()`, and the links event of `Mainloop`.

Spans:

| Context | Spans | Executed by |
|---|---|---|
| `Main` | `initialize`: creates the two signal generator instances | main thread, once |
| `Mainloop` | `poll_loop` with the nested spans `get_channel1` and `get_channel2` in turns until both queues are empty, each with a nested `poll`, then `analyze` | main thread, after an idle sleep of `Mainloop.idle_sleep_us` |
| `SigGen1`, `SigGen2` | `task` with the nested span `calc`, which has the nested span `clip` | task thread of the instance, with a sleep of `sample_time_us` after each cycle (25 us for `SigGen1`, 1 ms for `SigGen2`) |

The span signals of one context show for example the sequence task (level 1), calc (level 2), clip (level 3), calc, task, none, with the
duration of the span that just ended in `span_dt`. Span names are displayed through the enum conversion
`conv.spans`, which the A2L generator creates from all span names registered when the A2L file is finalized.

The event names carry the context name, so the event list and the event groups of the tool show what an
event contains (`SigGen1.siggen` instead of a numbered event instance).

`Poll()` does not attach the context of its instance, it runs in the context of its caller. Its span `poll` is therefore nested in
`get_channel1` or `get_channel2` in the context `Mainloop`, and this tells which instance was polled. The event
`Mainloop.poll` is triggered for both instances, so `Mainloop.polled` contains the mixed values of both (just for demonstration). The alternative is in the code as a comment: with `xcp::Scope scope(context_)` in `Poll()`, the span and the event belong to the context of the instance
(`SigGen1.poll`, `SigGen1.polled`). The span is then not nested in the span of the caller, because attaching a context starts
without a current span, and the context is executed by two threads, see "Same context in multiple threads".

By default the spans show the real execution times: Perfetto displays them with ns resolution, the data rate goes up to about 10 MByte/s
and xcpclient keeps up with it. For a view which is easier to follow, with a much lower data rate, enable `OPTION_SIMULATE_WORK` in
[src/sig_gen.hpp](src/sig_gen.hpp): the spans then contain sleeps which simulate work.

## Measuring the startup

Experimental. Disabled by default (`OPTION_OBSERVE_STARTUP` in [src/main.cpp](src/main.cpp)).

The span `initialize` in the context `Main` is executed once, before the main loop. To get it into a measurement, the demo
waits in `wait_xcp()` until the XCP client has started the measurement, and then begins with the startup.

This works from the second run on. The demo uses `A2L_MODE_WRITE_ONCE`, so the A2L file and the binary persistence file of the
first run are kept. In the next run, the events of all contexts are preloaded from the persistence file, and the client can set up
the measurement before any instrumented code has been executed.

1. First run: start the demo and stop it with Ctrl-C. There is no A2L file yet, the demo does not wait and prints
   `First run, thread_context_demo.a2l is generated now, restart to measure the startup`. The A2L file and the `.bin` file
   are written at shutdown, or earlier when a client connects.
2. Second run: start the demo again. It prints `Waiting for the XCP client to start the measurement (Ctrl-C to abort) ...`.
3. Start the measurement with xcpclient or CANape. The demo continues with the startup and the main loop.

Span ids are not stored in the persistence file and need not be: with `OPTION_DAQ_SPAN_SECTION_REGISTRATION` (default) the span
names are registered at link time, the span ids are the same in every run of the same binary and `conv.spans` of the first run
is complete, including spans which were not executed in the first run. Without the option (Windows, SHM mode), span ids are
assigned in the order in which the spans are executed for the first time, and the A2L file of the first run is valid for the
second run only as long as this order is the same.

`Start()` of the signal generator returns after the first cycle of its task, and the main function starts `SigGen1` before `SigGen2`,
so the events of the contexts are created in the same order in every run.

## OpenTelemetry version for comparison

[otel/](otel/) contains the same demo instrumented with opentelemetry-cpp instead of XCP, as a reference
for reading. It is not built by the XCPlite build and has not been compiled. Build and run notes,
including a receiver for visualisation, are in the header of [otel/src/main.cpp](otel/src/main.cpp).

## Same context in multiple threads

In this demo the spans and context-aware events of each context are triggered by one thread only:

| Context | Thread | Spans |
|---|---|---|
| `Main` | main thread | `initialize` |
| `Mainloop` | main thread | `poll_loop`, `get_channel1`, `get_channel2`, `poll`, `analyze` |
| `SigGen1`, `SigGen2` | task thread of the instance | `task`, `calc`, `clip` |

So the demo does not need `OPTION_DAQ_SPAN_THREAD` in `xcplib_cfg.h`, which is not defined by default. With the option, the spans event
of a context has the additional measurement `.thread`, a small index of the thread which began or ended the span, and xcpclient
writes one track per thread. The indices are assigned in the order in which the threads begin their first span.

Places which look as if they break the rule, but do not:

- `Poll()` runs in the main thread. It does not attach the context of its instance (`xcp::Scope scope(context_)` is commented out),
  so its span `poll` belongs to the context of the caller, `Mainloop`, not to `SigGen1` or `SigGen2`.
- `Clip()` is called from `Task()` only, so it always runs in the task thread of its instance.
- The constructor, `Start()` and `Stop()` run in the main thread. The constructor creates the context, but none of them begins a span
  or triggers an event in it.
- `Main` and `Mainloop` are both executed by the main thread. One thread working in several contexts is fine, the option is about the
  other direction: several threads in one context.

To keep in mind:

- This is a convention, nothing enforces it. With `xcp::Scope scope(context_)` enabled in `Poll()`, `SigGen1` gets spans from two
  threads, the task thread and the main thread. Without the option, xcpclient puts the begin and end samples of both threads on one
  level stack, and they corrupt the nesting of each other.
- The library needs one thread at a time per context, not exactly one. Handing a context from one thread to another without the
  option is fine, as long as the first thread has ended its spans before the next one begins. See also "Findings and limitations".

## Span links and Perfetto arrows

A sample is calculated in the span `task` of a signal generator thread and polled later, in the span `poll` of the main loop, in
another thread and another context. The two spans have no parent and child relation and are not nested, their relation is a span
link, as in OpenTelemetry:

- The producer captures the span context of its current span with `xcp::CaptureSpanLink()` and puts it into the sample
  (`SampleT::link`, a trivially copyable `xcp::SpanLink` of 16 bytes). The link carries the producer context id, the current span id,
  the thread index (`OPTION_DAQ_SPAN_THREAD`) and the clock at the capture. Nothing is measured on the producer side.
- The consumer records the link with `xcp::LinkSpan(sample->link)` inside its span `poll`. This triggers the event `<context>.links` of
  its current context (`Mainloop.links`) with the measurements `.link_context` (enum `conv.contexts`), `.link_span` (enum `conv.spans`)
  and `.link_time`, plus `.link_thread` and `.link_consumer_thread` with `OPTION_DAQ_SPAN_THREAD`. The event is created on the first
  link in a context, a context without links costs nothing.

The link identifies the producer span by its track (context and thread) and a timestamp, not by a span instance id. `xcpclient
--perfetto` writes each link sample as a flow: the start on the producer track at the capture time, the end on the consumer track at
the time of the `LinkSpan` sample. Perfetto binds each end to the innermost slice which encloses its timestamp, so the arrow goes from
the span in which the link was captured (`task`, the push happens after `calc` has ended) to the span in which it was recorded
(`poll`). Nothing is kept per span, in the target or in xcpclient. A link whose producer context is not measured, or which was
captured before the measurement start, is counted and dropped. A link captured in a span which began before the measurement start
has no producer slice, Perfetto does not draw it.

To see the arrows, expand the tracks and click a `poll` slice: the arrow from its `task` is drawn, the details panel lists it under
"Preceding flows" with the delay. Clicking a `task` slice shows its outgoing arrow. With `SigGen1` at 25 us there are many links,
zoom in to a few cycles of the main loop.

The analysis behind this (Perfetto flow formats, OTel links, alternatives) and two example traces are in
[perfetto_flow/README.md](perfetto_flow/README.md).

---

## Implementation notes

An event stores the id of its context, a context id > 0 means that the event belongs to a context (0 = no context). The library knows
the context names, and an event which belongs to a context is named `<context>.<event>` in the A2L event list, the event groups,
`conv.events` and `GET_DAQ_EVENT_INFO` (`SigGen1.siggen`).

### Library

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
  search the context list each time. The demo enters its contexts with a literal name with `DaqContext`: `Main` once at the begin
  of `main()`, `Mainloop` in each cycle of the main loop. The signal generator instances use an `xcp::Context` member, because
  their name is only known at runtime and each instance needs its own context, a static at one code location would be shared.
- The span event of a context is `<context>.spans`. A sample is the state after a span begin or end: `span`, `level`, `span_dt`
  and, with `OPTION_DAQ_SPAN_THREAD`, `thread`. The record has 8 bytes with and without the option, `level` has 8 bit with the
  option and 16 bit without.
- The A2L writer creates `conv.spans` at finalize from the registered span names, and `conv.contexts` (id 0 `none`, 1.. the context
  names) for the producer context of span links.
- Span links: `xcp::SpanLink`, `xcp::CaptureSpanLink()`, `xcp::LinkSpan()`. The event `<context>.links` is created with the same
  claim scheme as `DaqEventCtxVar`, one state per context id for the program, on the first link in the context.
- `XcpFindEvent` and `XcpCreateEvent` consider events without context only, so an event `<name>` and `<context>.<name>` can
  coexist. `XcpGetEventContext` returns the context id of an event. The A2L short name of an event is its name without the context.
- Test: `test/context_test`, registered with CTest (unit).

### Persistence

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
| `OPTION_DAQ_EVENT_COUNT` | 32 | 28 bytes per event | 896 bytes |
| `OPTION_DAQ_CONTEXT_COUNT` | 8 | 16 bytes per context | 128 bytes |
| `OPTION_DAQ_SPAN_COUNT` (only without `OPTION_DAQ_SPAN_SECTION_REGISTRATION`) | 32 | span name list | 1792 bytes |
| Event name buffer | | | 32 bytes |

The event list exists without contexts as well. The context feature itself adds the context list and the event name buffer, about
160 bytes of RAM with the defaults. With link-time registration (default), a span name is a constant descriptor of 32 bytes per
`DaqSpan` code location (9 in this demo), there is no span name list in RAM.

Per use, in the application:

| Item | Size | Count |
|---|---|---|
| `DaqEventCtxVar` code location (event state per context) | 2 x (`OPTION_DAQ_CONTEXT_COUNT` + 1) = 18 bytes | per code location |
| Mutex for the first-time registration | 64 bytes on macOS, 40 bytes on Linux | per distinct combination of measurement types, not per code location |
| `xcp::Context` object | 24 bytes | per context |
| Current context and span | 16 bytes thread-local | per thread which uses a scope or a span |
| Thread index (`OPTION_DAQ_SPAN_THREAD`) | 1 byte thread-local | per thread which uses a span |
| `DaqSpan` code location | none with link-time registration (a 32 byte constant descriptor), otherwise 2 bytes for the cached span id, constant initialized | per code location |

The number of events is what has to be planned. With C contexts and L `DaqEventCtxVar` code locations:

```
events = C (one spans event per context) + C x L + one links event per context which records span links
         + code locations also executed without a context + other events
```

Only combinations which are actually executed create an event. Worst case with the default of 8 contexts:

| Code locations L | Events needed | Event list | Fits the default of 32 events |
|---|---|---|---|
| 1 | 16 | 448 bytes | yes |
| 2 | 24 | 672 bytes | yes |
| 3 | 32 | 896 bytes | yes, with no other event |
| 4 | 40 | 1.1 KB | no |
| 8 | 72 | 2.0 KB | no |
| 16 | 136 | 3.8 KB | no |

This demo has 4 contexts and 6 code locations, of which `clip` and `siggen` are executed in 2 contexts and `poll`, `sample1`, `sample2`
and `mainloop` in one, plus the links event of `Mainloop`, which is 4 + 4 + 4 + 1 = 13 events. When the event list is full (`OPTION_DAQ_EVENT_COUNT`), `DaqEventCtxVar`
measures nothing for this context and code location, and tries to create the event again on every call.

Not affected by the number of contexts or code locations:

- `OPTION_DAQ_MEM_SIZE` (12288 bytes) is used with 6 bytes per signal which the tool selects for
  measurement, independent of how many signals are declared.
- The A2L generator writes to the file, it does not keep the declarations in memory.
- The binary persistence file grows by 256 bytes per event.

## Findings and limitations

- **Event budget.** One event per context, plus one per context and context-aware code location, plus one per context which records
  span links
  (13 events in this demo, `OPTION_DAQ_EVENT_COUNT` is 32 by default). The number of contexts is limited
  by `OPTION_DAQ_CONTEXT_COUNT` (8), the number of span names by `OPTION_DAQ_SPAN_COUNT` (32) without link-time registration.
- **One thread at a time.** A context should be executed by one thread at a time. Several threads may work in it one after
  the other, the hand-over between them (a queue, a mutex, a join) keeps the timestamps of its events in order. A context
  and its spans describe one track of activity. For a worker pool, use one context per worker. See "Same context in multiple threads".
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

With `OPTION_DAQ_CONTEXT_TEST` defined in `xcplib_cfg.h` (debug prints, the main loop stops after one cycle), shortened.
The prints of several threads interleave.

```
Created Context 'Main': 1
Create Event 0: spans context=1, cycle=0ns, prio=0
Register measurements for type SpanRecord
Set scope to context 'Main', thread_id=4021395904
Span begin, 'Main': 'none' -> 'initialize', 1
Trigger span event Main.spans, span 'initialize', level=1, dt=0ns
Create CalSeg 'SigGen1' size=40, type=seg
Created Context 'SigGen1': 2
Create Event 1: spans context=2, cycle=0ns, prio=0
Register measurements for type SpanRecord
Create CalSeg 'SigGen2' size=40, type=seg
Created Context 'SigGen2': 3
Create Event 2: spans context=3, cycle=0ns, prio=0
Register measurements for type SpanRecord
Set scope to context 'SigGen1', thread_id=1797386240
Span end, 'Main': 'initialize' -> 'none', 0
Trigger span event Main.spans, span 'none', level=0, dt=109833ns
Set scope to context 'SigGen2', thread_id=1797959680
Create CalSeg 'Mainloop' size=4, type=seg
Starting main loop...
Starting task: SigGen1
Span begin, 'SigGen1': 'none' -> 'task', 1
Trigger span event SigGen1.spans, span 'task', level=1, dt=0ns
Span begin, 'SigGen1': 'task' -> 'calc', 2
Trigger span event SigGen1.spans, span 'calc', level=2, dt=0ns
Span begin, 'SigGen1': 'calc' -> 'clip', 3
Trigger span event SigGen1.spans, span 'clip', level=3, dt=0ns
Create Event 3: clip context=2, cycle=0ns, prio=0
Span end, 'SigGen1': 'clip' -> 'calc', 2
Trigger span event SigGen1.spans, span 'calc', level=2, dt=12917ns
Span end, 'SigGen1': 'calc' -> 'task', 1
Trigger span event SigGen1.spans, span 'task', level=1, dt=17500ns
Create Event 4: siggen context=2, cycle=0ns, prio=0
Span end, 'SigGen1': 'task' -> 'none', 0
Trigger span event SigGen1.spans, span 'none', level=0, dt=110500ns
...
(lines of the SigGen threads omitted)
Created Context 'Mainloop': 4
Create Event 7: spans context=4, cycle=0ns, prio=0
Register measurements for type SpanRecord
Set scope to context 'Mainloop', thread_id=4021395904
Span begin, 'Mainloop': 'none' -> 'poll_loop', 1
Trigger span event Mainloop.spans, span 'poll_loop', level=1, dt=0ns
Span begin, 'Mainloop': 'poll_loop' -> 'get_channel1', 2
Trigger span event Mainloop.spans, span 'get_channel1', level=2, dt=0ns
Span begin, 'Mainloop': 'get_channel1' -> 'poll', 3
Trigger span event Mainloop.spans, span 'poll', level=3, dt=0ns
Create Event 8: poll context=4, cycle=0ns, prio=0
Span end, 'Mainloop': 'poll' -> 'get_channel1', 2
Trigger span event Mainloop.spans, span 'get_channel1', level=2, dt=12542ns
Create Event 9: sample1 context=4, cycle=0ns, prio=0
...
Stopped task: SigGen1, samples: 38, dropped: 13
End scope of context 'SigGen1', back to context 'none', thread_id=1797386240
Finalizing A2L file ...
A2L include file 'thread_context_demo_include.a2l' finalized: 31 measurements, 0 params, 2 typedefs, 7 components, 3 instances, 1 conversions
Write A2L file thread_context_demo.a2l, project_name 'thread_context_demo', epk 'V100'
XcpSetA2lName set to 'thread_context_demo'
A2L file 'thread_context_demo.a2l' finalized
Disconnect and shutdown XCP!
Terminate server threads
End scope of context 'Main', back to context 'none', thread_id=4021395904
```

## Historical: the first implementation in multi_thread_demo

The first experimental implementation of thread contexts, formerly in `multi_thread_demo` (`EXPERIMENTAL_THREAD_CONTEXT`), no longer
in the code. Kept for reference.

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
