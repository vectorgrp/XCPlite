# thread_context_demo — context and spans for multiple instances

Experimental. Explores how XCPlite can measure the same code when it runs for several class instances
or in several threads, using a concept borrowed from OpenTelemetry (OTel): context, scope and span.

The API shown here is a prototype in [src/xcp_context.hpp](src/xcp_context.hpp), it is not part of the
XCPlite library API yet.

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

| Term | Meaning in this prototype |
|---|---|
| `xcp::Context` | A named identity, "who is executing". One per class instance here (`SigGen1`, `SigGen2`). |
| `xcp::Scope` | Makes a context the current context of the calling thread and restores the previous one at the end of the block. The current context is in thread-local storage. |
| `DaqEventCtxVar` | Context-aware variant of `DaqEventVar`. Uses a separate event instance and separate measurements for each context. |
| `DaqSpan` | A named, timed section of code. Begin and end are recorded on the event of the current context. |

```cpp
xcp::Context context_;                                  // class member, constructed with the instance name
xcp::Scope scope(context_);                             // in the thread or method which works for this instance
DaqEventCtxVar(siggen, A2L_MEAS_PHYS(value_, ...));     // event siggen_1, measurement SigGen1.value_
{ DaqSpan(calc); ... }                                  // span "calc", ends at the end of the block
```

With a current context, the instrumented code needs no instance parameter. The shared function `Clip()`
in [src/sig_gen.cpp](src/sig_gen.cpp) is instrumented without knowing its instance and still delivers
`SigGen1.clipped` and `SigGen2.clipped`.

## What the demo measures

| Event | Measurements | Triggered by |
|---|---|---|
| `siggen_1`, `siggen_2` | `SigGen1.value_`, `SigGen2.value_` | the task thread of each instance |
| `clip_1`, `clip_2` | `SigGen1.input`, `SigGen1.clipped` (same for `SigGen2`) | the shared function `Clip()` |
| `poll_1`, `poll_2` | `SigGen1.polled`, `SigGen2.polled` | the main thread, method `Poll()` attaches the context of its instance |
| `SigGen1`, `SigGen2` | `.span`, `.level`, `.span_dt` | begin and end of each span in this context |
| `mainloop` | `channel1`, `channel2` | the main loop |

The span signals of one context show the sequence calc (level 1), clip (level 2), calc, none, with the
duration of the span that just ended in `span_dt`. Span names are displayed through the enum conversion
`conv.spans`.

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
  equivalent, so the prototype creates one signal per instance.
- **One guard instead of two.** OTel separates the lifetime of a span (`span->End()`) from being the
  current span of a thread (scope). `DaqSpan` combines both, which is enough for synchronous sections in
  one thread. A span which begins in one thread and ends in another can not be expressed.

## OpenTelemetry version for comparison

[otel/](otel/) contains the same demo instrumented with opentelemetry-cpp instead of XCP, as a reference
for reading. It is not built by the XCPlite build and has not been compiled. Build and run notes,
including a receiver for visualisation, are in the header of [otel/src/main.cpp](otel/src/main.cpp).

## Findings and limitations

- **Event budget.** One event per context, plus one per context and context-aware code location
  (9 events in this demo, `OPTION_DAQ_EVENT_COUNT` is 16 by default).
- **Monotonic timestamps.** The timestamps of an XCP event must be monotonic. A context can be attached
  to several threads at the same time (task thread and `Poll()` from the main thread), so the span events
  of a context are serialized with a spin lock. A context-aware event must not be triggered concurrently
  by several threads in the same context at the same code location.
- **A2L timing.** Contexts, context-aware events and spans must have been executed once before the A2L
  file is finalized. `xcp::A2lCreateSpanConversion()` must be called once after that, see `main.cpp`.
- **Span duration.** `span_dt` belongs to the span that just ended, the end sample does not name it.
- **Attach starts without a span.** The current span of the capturing thread is not carried over.
- **Default configuration only.** Needs runtime event creation and on-target A2L generation.
- **Library addition.** The prototype uses the new function `XcpCreateEventWithIndex`, which gives an
  event instance a defined index instead of the creation order.

## Open questions

- Move the span conversion into the A2L finalization, so the explicit call in `main.cpp` is not needed.
- Should a context capture the current context when the instance does not know its own name?
- How far should the naming follow OTel? An OTel tracer has a name but no id, it is only a factory for
  spans. The trace id belongs to the trace, which is the set of all spans sharing that id.
- Promotion into the library API (C core plus C++ wrappers), replacing the experimental context code in
  `multi_thread_demo`.
- Optional connection to a real OTel backend, on target through opentelemetry-cpp or off target by
  exporting span records from xcpclient.

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

## CANape

There is no CANape project for this example yet. Use xcpclient as shown above, or create a new CANape project
for XCP on UDP, port 5555, with automatic A2L upload.
