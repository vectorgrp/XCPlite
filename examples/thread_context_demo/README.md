# thread_context_demo — context and spans for multiple instances

Experimental. Explores how XCPlite can measure the same code when it runs for several class instances
or in several threads, using a concept borrowed from OpenTelemetry (OTel): context, scope and span.

The API is part of the XCPlite C++ API, in [inc/xcplib_context.hpp](../../inc/xcplib_context.hpp)
(included by `xcplib.hpp`). It needs the option `OPTION_DAQ_CONTEXT_LIST`, which is enabled in the default
configuration only.

---

## The problem

An XCP event is bound to a code location. The demo has two instances of a signal generator class, each
running its own thread. Instrumented in the obvious way, with one `DaqEventVar(siggen, A2L_MEAS(value_, ...))`
in the task function, both instances trigger the same event `siggen` and the tool receives one signal
`value_` with the values of both instances mixed.

The existing solutions (`cpp_demo`, `multi_thread_demo`) need the instrumented code to know its instance
and pass the name along: one event per instance, measurements registered with the instance name as
prefix. Shared helper functions, which do not know for which instance they are working, can not be
attributed at all.

## The concept

| Term | Meaning |
|---|---|
| `xcp::Context` | A named identity, "who is executing". One per class instance here (`SigGen1`, `SigGen2`). |
| `xcp::Scope` | Makes a context the current context of the calling thread and restores the previous one at the end of the block. The current context is in thread-local storage. |
| `DaqEventCtxVar` | Context-aware variant of `DaqEventVar`. Uses a separate event and separate measurements for each context, the event is named `<context>.<event>`. |
| `DaqSpan` | A named, timed section of code. Begin and end are recorded on the event `<context>.spans` of the current context. |

```cpp
xcp::Context context_;                                  // class member, constructed with the instance name
xcp::Scope scope(context_);                             // in the thread or method which works for this instance
DaqEventCtxVar(siggen, A2L_MEAS_PHYS(value_, ...));     // event SigGen1.siggen, measurement SigGen1.value_
{ DaqSpan(calc); ... }                                  // span "calc", ends at the end of the block
```

With a current context, the instrumented code needs no instance parameter. The shared function `Clip()`
in [src/sig_gen.cpp](src/sig_gen.cpp) is instrumented without knowing its instance and still delivers
`SigGen1.clipped` and `SigGen2.clipped`.

## What the demo measures

| Event | Measurements | Triggered by |
|---|---|---|
| `SigGen1.siggen`, `SigGen2.siggen` | `SigGen1.value_`, `SigGen2.value_` | the task thread of each instance |
| `SigGen1.clip`, `SigGen2.clip` | `SigGen1.input`, `SigGen1.clipped` (same for `SigGen2`) | the shared function `Clip()` |
| `SigGen1.poll`, `SigGen2.poll` | `SigGen1.polled`, `SigGen2.polled` | the main thread, method `Poll()` attaches the context of its instance |
| `SigGen1.spans`, `SigGen2.spans` | `.span`, `.level`, `.span_dt` | begin and end of each span in this context |
| `mainloop` | `channel1`, `channel2` | the main loop |

The span signals of one context show the sequence calc (level 1), clip (level 2), calc, none, with the
duration of the span that just ended in `span_dt`. Span names are displayed through the enum conversion
`conv.spans`, which the A2L generator creates from all span names registered when the A2L file is finalized.

The event names carry the context name, so the event list and the event groups of the tool show what an
event contains (`SigGen1.siggen` instead of a numbered event instance).

Test with xcpclient:

```bash
xcpclient --udp --mea '.*' --time 5 --csv mea.csv
```

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

## OpenTelemetry version for comparison

[otel/](otel/) contains the same demo instrumented with opentelemetry-cpp instead of XCP, as a reference
for reading. It is not built by the XCPlite build and has not been compiled. Build and run notes,
including a receiver for visualisation, are in the header of [otel/src/main.cpp](otel/src/main.cpp).

## Findings and limitations

- **Event budget.** One event per context, plus one per context and context-aware code location
  (9 events in this demo, `OPTION_DAQ_EVENT_COUNT` is 16 by default). The number of contexts is limited
  by `OPTION_DAQ_CONTEXT_COUNT` (8), the number of span names by `OPTION_DAQ_SPAN_COUNT` (32).
- **Monotonic timestamps.** The timestamps of an XCP event must be monotonic. A context can be attached
  to several threads at the same time (task thread and `Poll()` from the main thread), so the span events
  of a context are serialized with a spin lock. A context-aware event must not be triggered concurrently
  by several threads in the same context at the same code location.
- **A2L timing.** Contexts, context-aware events and spans must have been executed once before the A2L
  file is finalized.
- **Span duration.** `span_dt` belongs to the span that just ended, the end sample does not name it.
- **Attach starts without a span.** The current span of the capturing thread is not carried over.
- **Default configuration only.** Needs runtime event creation and on-target A2L generation. Not
  available in the SHM configuration yet.
- **Library implementation.** The context names are kept in a context list in the C core
  ([src/ctx.c](../../src/ctx.c)), an event stores the id of its context. The binary persistence file
  stores the context name with each event, so event ids are stable across restarts. There is no C
  instrumentation API for contexts.
- **Names.** Context and event names are limited to 15 characters each.


---

## Building

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

---

#

## Implementation notes 

Contexts and spans in the library, anonymous event instances removed

An event index > 0 now always means a context. The library knows the
context names, and an event which belongs to a context is named
<context>.<event> in the A2L event list, the event groups, conv.events
and GET_DAQ_EVENT_INFO (SigGen1.siggen instead of siggen_1).

New
- Option OPTION_DAQ_CONTEXT_LIST with OPTION_DAQ_CONTEXT_COUNT (8) and
  OPTION_DAQ_SPAN_COUNT (32). Enabled in the default configuration,
  undefined in shm, ptp, raw, no_a2l and rtos.
- src/ctx.c, src/ctx.h: context list and span name list next to the
  event list. XcpCreateContext, XcpGetContextName, XcpGetContextCount,
  XcpCreateContextEvent, XcpRegisterSpanName.
- inc/xcplib_context.hpp, included by xcplib.hpp: xcp::Context,
  xcp::Scope, xcp::Span, DaqSpan, DaqEventCtxVar, DaqEventCtxVar_.
  Moved from the prototype in examples/thread_context_demo. There is
  no C instrumentation API for contexts.
- The span event of a context is <context>.spans.
- The A2L writer creates conv.spans at finalize from the registered
  span names, xcp::A2lCreateSpanConversion() is gone.
- test/context_test, registered with CTest (unit).

Breaking
- Removed XcpCreateEventInstance, DaqCreateEventInstance,
  DaqGetEventInstanceId, XcpCreateEventWithIndex and XcpGetEventIndex.
  XcpGetEventContext returns the context id of an event.
- XcpFindEvent and XcpCreateEvent consider events without context
  only, so an event <name> and <context>.<name> can coexist.
- The A2L short name of an event is its name without the context.

Persistence
- The context name is stored with each event in previously reserved
  bytes of the event record, the context list is rebuilt from the
  events on load. BIN_VERSION is unchanged. A file which contains
  anonymous event instances is rejected.
- tools/bintool is not updated yet and shows the event name without
  the context name.

Other
- daq_test: each task thread creates a plain event task_<n>.
- thread_context_demo uses the library API, A2L fixture regenerated.
- Documentation updated: API_OVERVIEW, TECHNICAL, xcplib_cfg,
  CHANGELOG, example and test READMEs, CLAUDE.md.





## Remaining todos, grouped by how soon they matter.

### Postponed 

tools/bintool: read the context name from the event record and show <context>.<event>.
SHM configuration: enable OPTION_DAQ_CONTEXT_LIST there, bump SHM_VERSION, make context creation process safe, and test with shmtool and xcpdaemon.

### Not verified

Windows/MSVC build of xcplib_context.hpp and context_test.
Linux build (gcc and clang) and a run on the Pi. Everything was built and run on macOS only.
ptp, raw and rtos configurations were not built after the change; only shm and no_a2l were.
clang-tidy (./build.sh lib tidy) on ctx.c.


### Known gaps in the feature

.bin/.hex matching in test/test_examples.sh still does not cover fixed-version file names, so cpp_demo.hex is not compared.
span_dt is not attributed to a span name; the end sample only carries the duration.
Two xcp::Context objects with the same name share the context but not the spin lock, so their span events are not serialized. Currently only documented as “create one object per name”.
DaqSpan executed before XcpInit keeps span id 0 permanently, because the id is a function-local static.
A context-aware event triggered concurrently by several threads in the same context at the same code location can produce non-monotonic timestamps. Only the span event is serialized.
No CANape project for thread_context_demo.
The OTel version in examples/thread_context_demo/otel/ is still uncompiled.

### Open design questions

A parent group per context in the A2L file, containing its event groups.
Whether a class instance that does not know its name should capture the current context.
How far the naming should follow OTel (trace id, span id).
Optional bridge to a real OTel backend.
Contexts for no_a2l/rtos (needs link-time registration and xcpclient support).

### Separate from this branch

bugfix/daq_event_template (once-flag fix for DaqEventVar)



## Open questions

- Should a context capture the current context when the instance does not know its own name?
- How far should the naming follow OTel? An OTel tracer has a name but no id, it is only a factory for
  spans. The trace id belongs to the trace, which is the set of all spans sharing that id.
- Contexts in the SHM configuration, context names in `tools/bintool`.
- A group per context in the A2L file, containing the event groups of the context.
- Optional connection to a real OTel backend, on target through opentelemetry-cpp or off target by
  exporting span records from xcpclient.



  