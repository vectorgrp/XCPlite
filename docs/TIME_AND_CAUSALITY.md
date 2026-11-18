# Time, order and causality: XCP and OpenTelemetry

XCP and OpenTelemetry (OTel) both observe running software, and both attach timestamps to what they observe. They come from different worlds, answer different questions and treat time in different ways. People who know one of them tend to carry its assumptions into the other, and most misunderstandings start there.

This document explains both and compares them. Then it shows what XCPlite takes over from OTel, and why: the OTel context gives XCP a way to measure a consistent time series per identity, for example per instance of a control algorithm, from code which does not know for which instance it is executed. Finally it collects what was found about time and order during this work. It is meant as a common base for discussions about time and causality in concurrent and distributed systems.

How to read it:

- Part 1 to 3 need no previous knowledge of XCP or OTel.
- Part 4 compares the two and lists common misconceptions.
- Part 5 goes into the details of clocks, order and causality.
- Part 6 explains why XCPlite combines the OTel context with XCP events, and what was tried and decided about concurrency.
- Part 7 says where the statements come from and what was not verified.

## Contents

1. [Two kinds of systems, two questions](#1-two-kinds-of-systems-two-questions)
2. [XCP in ten minutes](#2-xcp-in-ten-minutes)
3. [OpenTelemetry in ten minutes](#3-opentelemetry-in-ten-minutes)
4. [Comparison](#4-comparison)
5. [Time, order and causality in detail](#5-time-order-and-causality-in-detail)
6. [Contexts, spans and clocks in XCPlite](#6-contexts-spans-and-clocks-in-xcplite)
7. [Sources and limits of this document](#7-sources-and-limits-of-this-document)

---

## 1. Two kinds of systems, two questions

### 1.1 The cyber-physical system

A cyber-physical system controls something in the physical world: an engine, a brake, a battery, a robot arm. Sensors measure physical quantities, control algorithms compute, actuators act. The physical process does not wait, so the question of *when* something happened is part of whether the system works correctly. A value which is right but 2 ms late can be wrong.

Such a system is observed and tuned with **XCP**. The engineer wants to see signals over time, like on an oscilloscope: the measured speed, the output of a controller, an internal state variable. And they want to lay these curves next to curves from measurement instruments which observe the physical world directly.

Today's cyber-physical systems are distributed systems as well. Not only the sensors and actuators are distributed, the control algorithms are too: a mix of microcontrollers with bare metal software or a small real-time operating system, and microprocessors with a POSIX like operating system, many threads and many cores.

### 1.2 The distributed software service

A service landscape handles requests: a user clicks, a gateway receives the request, calls three services, one of them calls a database. Each part runs in its own process, often on its own machine, written by its own team.

Such a system is observed with **OpenTelemetry**. The engineer wants to know what happened to *this request*: which services were involved, who called whom, where the time went, where it failed. And they want statistics: requests per second, error rates, latency distributions.

### 1.3 The two questions

| | XCP | OpenTelemetry |
|---|---|---|
| Central question | How did this value evolve over time? | What happened because of what? |
| Result | Time series | Traces (causal trees), metrics (statistics), logs |
| Role of time | The primary key of all data | One attribute among others |
| Role of causality | Not recorded, the engineer infers it from the curves | Recorded explicitly, with identifiers |

Everything else in this document follows from this table.

---

## 2. XCP in ten minutes

XCP is an ASAM standard for measurement and calibration. See [XCP_INTRODUCTION.md](XCP_INTRODUCTION.md) for a general introduction. Here only the parts which matter for time are explained.

### 2.1 Signals, events and samples

- A **signal** (measurement variable) is a variable of the program: a global, a local on the stack, a member of an object. A description file (A2L) tells the tool its name, type, address, unit and conversion.
- An **event** is a point in the code where signals can be sampled. In XCPlite it is a line of instrumentation, for example `DaqTriggerEvent(task_10ms)` or `DaqEventVar(...)`.
- At runtime, the tool chooses which signals it wants at which event. This selection is called a DAQ list. Nothing is transferred which the tool did not ask for.
- Each time the code runs through the event, the target takes **one timestamp**, copies the selected signals and sends both to the tool. This is one **sample**.

```c
DaqCreateEvent(task_10ms); // once at startup

void task_10ms(void) {
    speed = read_sensor();
    torque = controller(speed);
    DaqTriggerEvent(task_10ms); // one timestamp, a consistent copy of speed and torque
}
```

Two properties are important:

- **Consistency.** All signals of one sample are copied at the same point of the program. They belong together.
- **Order.** The samples of an event are sent in the order in which they were taken. The tool reads this order as the order in time.

The result in the tool is a time series per signal: a list of (time, value) pairs.

### 2.2 The timestamp

The timestamp is taken on the target, at the event, from a clock of the target. In XCPlite the clock has a resolution of 1 ns or 1 µs. 

### 2.3 Which clock is it?

A timestamp is only useful if the tool knows which clock produced it. XCP therefore describes the clock to the tool:

- **Resolution and unit** of the timestamp.
- **Current time on request.** With the command `GET_DAQ_CLOCK` the tool asks the target for its current event sampling clock value. By comparing it with its own reference clock, the tool learns offset and drift of the target clock and can map target timestamps to its own time base. This is called time correlation.
- **Timestamp pairs as event.** the target transmit precise simulatanous captured pairs of its event sampling wall clock and a gPTP or PTP synchronized to the tool, and the tool correlates offset and drift.
- **Clock description.** With `TIME_CORRELATION_PROPERTIES` the target describes its clocks: a unique identifier of the clock, its epoch (TAI, UTC or arbitrary), its quality (stratum level), and whether it is synchronized to a grandmaster clock, for example by PTP, and to which one. Two devices which report the same grandmaster are in the same time domain, their timestamps can be compared directly.

So XCP has two ways to bring several clocks together: correlate them afterwards in the tool, or synchronize them beforehand and tell the tool that they are synchronized. Both are part of the protocol. What XCPlite implements of this is listed in [6.8](#68-clocks-in-xcplite).

### 2.4 What XCP does not record

XCP has no notion of a request, an operation or a relation between two samples. There are no identifiers on the wire which say "this sample was caused by that one". If a sensor value in controller A leads to an actuator command in controller B, the engineer sees two curves on a common time axis and concludes the relation from timing and knowledge of the system.

---

## 3. OpenTelemetry in ten minutes

OpenTelemetry is an open standard and a set of libraries for telemetry of software services. It defines several kinds of data, called signals (not to be confused with an XCP signal): traces, metrics and logs.

### 3.1 Spans and traces

- A **span** is one operation with a beginning and an end: handling a request, a database query, a function. It has a name, a start time, an end time and free attributes.
- Each span has a **span id**, the id of its **parent span**, and a **trace id** which is the same for all spans of one request.
- A **trace** is the set of all spans with the same trace id. Through the parent ids they form a tree: who called whom.

```cpp
auto span = tracer->StartSpan("calc");      // start time is taken here
auto scope = tracer->WithActiveSpan(span);  // spans started below become children
// ... work ...
span->End();                                // end time is taken here, the span is handed over for export
```

A span is exported as **one self-describing record when it has ended**. Nothing is sent at its start. The records of a trace arrive at the backend in any order, from several processes, and the backend puts the tree together by the ids.

```
trace 4bf9...
└─ GET /order            gateway     0 ms ......................... 48 ms
   ├─ check stock        service A      3 ms ....... 15 ms
   ├─ price              service B      3 ms ............. 22 ms
   └─ store order        service C                        24 ms ..... 45 ms
      └─ INSERT          database                            27 ms .. 41 ms
```

### 3.2 Context and propagation

How does a span know its parent? Through the **context**. A context is a small immutable value which carries the current span. It is attached to the running thread (more general: to the execution unit, which may also be a coroutine or a fiber). A span started while a context is attached becomes a child of the span in that context.

When work crosses a boundary, the context has to travel with it:

- To another thread: the program captures the context and attaches it in the other thread.
- To another process: the context is written into the message, for HTTP into a header. This is called **propagation**. The header contains the trace id, the id of the calling span and some flags. It contains **no timestamp**.

This is the core of OTel: causality is carried along with the work itself, as identifiers.

### 3.3 Metrics and logs

- A **metric** is a statistic: a counter, a current value, a histogram. The program reports individual measurements, the library aggregates them, and what is exported is one data point per time window, for example "1523 requests between 10:00:00 and 10:00:10". The individual measurements are normally not kept.
- A **log record** is a text or structured message with a timestamp. It can carry a trace id and span id and is then linked to a span.

### 3.4 What OpenTelemetry does not define

OTel timestamps are wall clock time: nanoseconds since 1970, taken from the clock of the local machine. The specification pages which were read for this document say nothing about clock synchronization, about clock quality, or about a way to tell the backend which clock a timestamp comes from. There is no requirement that the machines have synchronized clocks. See [5.6](#56-what-opentelemetry-says-about-time) for the details.

---

## 4. Comparison

### 4.1 Side by side

| | XCP | OpenTelemetry |
|---|---|---|
| Typical system | Cyber-physical: controllers, sensors, actuators | Software services: requests, calls, queues |
| Main result | Time series of signals | Trace trees, aggregated metrics, logs |
| What a record is | A sample: one timestamp and the values of the selected signals | A span: an interval with ids, name and attributes |
| When it is sent | At each event | When the span has ended |
| Who decides what is recorded | The tool, at runtime, signal by signal | The program (instrumentation), configuration, sampling |
| Description of the data | Beforehand, in the A2L file: every signal with name, type, address, unit | Self-describing records, free names and attributes |
| Identity on the wire | Number of the DAQ list, nothing else | Trace id, span id, parent span id |
| Order | The order in the stream is the order in time | No order of export, the backend sorts and builds the tree |
| Relation between records | None, only the common time axis | Parent and child, links |
| Clock | Described to the tool: resolution, epoch, identity, synchronization state | "The local machine", not described |
| Several clocks | Correlated by the tool or synchronized and declared (time domains) | Not addressed, causality does not need it |
| Timestamp size | 32 bit per sample, extended by the tool | 64 bit per timestamp |
| Typical rate | Up to many thousand samples per second and event | Depends on request rate, often reduced by sampling |
| Cost model | A copy of a few bytes per sample, no allocation, no waiting | Creating a span object, attributes, export pipeline |
| Changing the system | Yes, calibration: parameters are modified at runtime | No |

### 4.2 The same word, different meaning

| Word | In XCP / XCPlite | In OpenTelemetry |
|---|---|---|
| Signal | A measured variable | A category of telemetry: traces, metrics, logs |
| Event | A place in the code where a sample is taken | A timestamped annotation inside a span |
| Context | XCPlite: a named, long-lived identity such as a class instance, with its own events ([6.1](#61-the-motivation-a-time-series-per-identity)) | An immutable value which carries the current span |
| Span | XCPlite: a named section of code, recorded as a state change of a context | A record with ids, start and end time |
| Sampling | Taking a sample at an event | Deciding whether a trace is recorded at all |
| Trace | Informal: a recorded measurement | The tree of all spans of one request |

### 4.3 Common misconceptions

Coming from XCP:

- **"A timestamp tells me the order of two events."** Only if the two events are further apart than the uncertainty of the clocks involved. See [5.2](#52-what-a-timestamp-can-tell).
- **"The data arrives in time order."** True for one thread. Not true by itself for several threads which trigger the same event, see [6.4](#64-several-threads-on-one-event). In OTel it is not even a goal.
- **"An OTel span is an XCP event with a begin and an end."** A span is one record which is sent when it ends. It is found by its ids, not by its position in a stream.
- **"A trace needs synchronized clocks."** The tree does not: it is built from ids. The duration of a span does not: start and end come from the same clock. Only the comparison of timestamps from different machines does.
- **"An OTel context is a track, like a task."** It is a value which says "the current span is X". Every thread can have a different one, and it changes with every span.

Coming from OTel:

- **"XCP is logging with a fixed schema."** The tool decides at runtime which variables are sampled at which event. What is not selected costs nothing. The values are copied from the memory of the program, there is no formatting and no message.
- **"Timestamps are approximate anyway."** In XCP the time is the primary key. The clock is described to the tool, and the timestamps are meant to be compared with those of measurement instruments.
- **"If the order is unclear, the ids will tell."** There are no ids. Order is what the stream and the timestamps say.
- **"Just add an attribute for the instance."** A2L has no free attributes. Every signal is declared beforehand with a name. Several instances mean several signals, see [6.1](#61-the-motivation-a-time-series-per-identity), [6.2](#62-splitting-in-the-target-or-in-the-tool) and the [thread_context_demo](../examples/thread_context_demo/README.md).
- **"Concurrency is normal, the backend sorts it out."** An XCP tool reads a stream of samples as a time series. Concurrent writers to the same series are a problem of the series, see [6.4](#64-several-threads-on-one-event).
- **"Instrumentation may take a lock, it is rare."** In a control system a thread which waits for another thread because of the measurement is a changed system. See [5.5](#55-the-observer-must-not-change-the-system).

---

## 5. Time, order and causality in detail

### 5.1 Clocks are not equal

A timestamp is a reading of a clock, and clocks differ a lot in what they can deliver.

**Measurement instruments.** Instruments which capture the physical world (bus interfaces, analog measurement modules) create the timestamp in hardware, at the moment of the physical event. These timestamps are very precise and the instruments can be synchronized with each other.

**Microcontrollers.** The software has direct access to hardware timers. An interrupt or a capture unit can latch a counter at an external edge. Reading a timer is a register access with a known, constant duration. Tasks run with fixed priorities, often without preemption inside a task. A timestamp taken in the code is close to the event, and the distance is known.

**POSIX like systems.** Reading the clock is much less certain:

- The clock is read through the operating system. On Linux this is normally done in user space and takes some tens of nanoseconds, but depending on platform and clock source it can be a real system call.
- There are several clocks with different behaviour. A real time clock (`CLOCK_REALTIME`) is corrected by NTP or PTP. It can be slowed down, accelerated, and it can jump, also backwards. A monotonic clock (`CLOCK_MONOTONIC`) never jumps, but its rate may still be adjusted. A raw monotonic clock (`CLOCK_MONOTONIC_RAW`) is not adjusted at all, but it is then not related to any other clock.
- A clock which is synchronized by PTP follows its grandmaster through a control loop. The loop corrects the clock all the time, which adds jitter.
- A thread can be preempted at any point: between the event and the clock read, or between the clock read and whatever is done with the timestamp. The timestamp is then correct as a clock reading, but it is not the time of the event any more.

**Several clocks.** A distributed system has many clocks. Without synchronization they drift apart, typically by some ten microseconds per second. With NTP over a network, the remaining error is typically in the range of milliseconds. With PTP and hardware timestamps in the network interface it is typically below a microsecond. These are orders of magnitude.

### 5.2 What a timestamp can tell

> Two events can be put in order by their timestamps only if they are further apart in time than the uncertainty of the clocks involved.

For two samples from the same thread, the uncertainty is small and the order is given by the program anyway. For two threads on one machine with a common clock, the uncertainty is the jitter of reading the clock plus possible preemption. For two machines, it is the synchronization error of their clocks.

If two events are closer together than this, the timestamps may show them in either order, and neither order is wrong. There is nothing more to know.

### 5.3 Causality: the order which does not need a clock

There is a second kind of order, described by Leslie Lamport in 1978 under the name *happened-before*. It needs no clock:

- Two steps of the same thread are ordered by the program.
- Sending a message happens before receiving it. The same holds for every hand-over between threads: putting work into a queue happens before taking it out, releasing a mutex happens before the next thread gets it, a thread ends before the join returns.
- If A happens before B and B before C, then A happens before C.

Two events for which neither is before the other are called **concurrent**. This is not a lack of knowledge. The order of concurrent events is not defined, a different run of the same program may produce a different interleaving, and both are correct.

A simple example shows why clocks cannot replace this. Machine A sends a request at 10:00:00.010 by its clock. Machine B receives it at 10:00:00.007 by its clock, because its clock is 5 ms behind. By the timestamps, the request was received before it was sent. By causality, there is no doubt.

### 5.4 Two answers to the same problem

**OTel records causality.** The parent id says who caused whom. The tree of a trace is correct even if every machine has a different time. Timestamps are then mainly needed for durations, and the start and end of a span come from the same local clock, so the duration is good. When time is ruled by causality information, precise absolute time becomes less important. For metrics it matters even less: a data point describes a time window of seconds, and a clock error far below the window size changes nothing.

**XCP makes time trustworthy.** There are no ids. The relation between two samples is the time between them. So the protocol invests in the clock: it has a fine resolution, it is described to the tool, it can be correlated with the clock of the tool, and it can be declared as synchronized to a known time domain. With a good common time base, the curves of several controllers and of the instruments can be laid on one axis, and the engineer sees cause and effect as a delay between curves.

Each answer is right for its question. A control engineer cannot use a trace tree to judge a control loop, they need the curve. A service engineer cannot find the slow call in ten thousand curves, they need the tree.

### 5.5 The observer must not change the system

Measuring influences the system. The copy of the data takes time, this cannot be avoided, and it is small and constant. The more serious influence is **coupling**: if the instrumentation makes one thread wait for another, the timing of the system depends on the measurement, and a thread with high priority may be delayed by one with low priority.

XCPlite therefore triggers events without waiting: the transmit queue is lock-free on 64 bit platforms, and a thread never waits for another thread at an event. The price is discussed in [6.4](#64-several-threads-on-one-event): without a lock, nothing forces two threads into an order.

This is a real conflict, and it is one of principle. A requirement of the kind "the samples of this event are in time order" cannot be fulfilled across threads without a common point at which the threads are put in order. Any such point couples the threads. Inside one thread there is no conflict, the order is given.

The pages of the OTel specification read for this document require that the API is safe to use from several threads at the same time. They do not require that it never blocks.

### 5.6 What OpenTelemetry says about time

From the specification pages listed in [7](#7-sources-and-limits-of-this-document):

- **Which clock.** Timestamps are nanoseconds since the UNIX epoch. The protocol definition describes the start and end time of a span as the time kept by the local machine. Nothing is said about which clock that is, how good it is or how it is synchronized. NTP and PTP are not mentioned.
- **No requirement for synchronized machines.** None was found.
- **No time in propagation.** The header which carries the context to another process contains version, trace id, parent id and flags. It contains no timestamp and no clock information.
- **When a timestamp is taken.** Start time, end time and the timestamps of events inside a span are recorded when the corresponding function is called. A program may pass its own timestamp, for example for something that has already happened.
- **Monotonic time.** Not required. The protocol definition says that the end time is expected to be not before the start time. Events inside a span may have timestamps before its start or after its end, the specification explicitly does not ask for a correction.
- **Order.** There is no rule for the order in which spans are exported. Inside one span, events should keep the order in which they were recorded, which is normally, but not necessarily, the order of their timestamps.
- **Concurrency.** A child span may still run when its parent has ended. Fork and join patterns are named as a use case. So concurrent operations in one trace are expected, each one has its own span.
- **Logs have two timestamps.** A log record has the time when the event occurred, by the clock of its origin, and the time when it was observed by the collecting system, by the clock of that system. The first one is optional. This is the only place found where OTel distinguishes clocks.
- **Metrics have time windows.** A data point of a sum or a histogram covers a window with a start and an end time. Each series has exactly one writer.

Whether backends correct timestamps with the help of the parent and child relations was not verified for this document.

### 5.7 What this means for a mixed system

A modern vehicle or machine contains both worlds: control loops on microcontrollers and service-like software on microprocessors. Some consequences:

- A timestamp from a POSIX thread and one from a microcontroller are not of the same quality, even if both are printed with nanoseconds.
- Declaring the time domain, as XCP does, is what makes a comparison across devices honest: the tool knows whether two timestamps can be compared directly, have to be correlated, or cannot be compared at all.
- Where the order of two events on different threads or devices matters and they are close together, a timestamp cannot decide it. Either the events are far enough apart, or the relation has to be known from the design, or it has to be recorded as causality, as OTel does.

---

## 6. Contexts, spans and clocks in XCPlite

### 6.1 The motivation: a time series per identity

An XCP event is bound to a code location. When the same code is executed for several identities, for example several instances of a control algorithm or several workers, all of them trigger the same event. The tool receives one series in which the values of all instances are mixed. If the instances run in different threads at the same time, the timestamps of this series may also decline, see [6.4](#64-several-threads-on-one-event). This is not a time series of anything. It is not the time series of one instance, and it is not the time series of a physical quantity either.

Without contexts, the instrumentation has to get the identity from somewhere. The repository contains three ways:

| Way | Example | Result |
|---|---|---|
| One event per instance, created with the instance name, and measurements registered with the instance name as prefix | [cpp_demo](../examples/cpp_demo/README.md) | A time series per instance. The instance has to know its name, and every instrumented function it calls has to get the name passed as a parameter. A shared helper function without this parameter cannot be attributed. |
| One shared event and a measurement which identifies the instance | [multi_thread_demo](../examples/multi_thread_demo/README.md), measurement `worker_id` | One mixed series. The tool would have to split it by the value of the identity signal, which XCP tools do not do, see [6.2](#62-splitting-in-the-target-or-in-the-tool). The instrumented code still has to know the identity, and the timestamps of the shared event may decline. |
| One event per thread, kept in thread-local storage | The first C version of the thread context, quoted at the end of the [thread_context_demo README](../examples/thread_context_demo/README.md) | A time series per thread. Shared code needs no parameter, the identity comes from the thread. But the identity is then the thread, not the instance. It is wrong when one thread works for several instances, or when the work of one instance moves from one thread to another. |

Only the first way gives a real time series per instance, and it requires that the identity is passed along explicitly to every instrumented code location.

OpenTelemetry has solved the corresponding problem for traces with its context ([3.2](#32-context-and-propagation)): the current span travels with the work, in thread-local storage within a thread and by capture and attach between threads. A library function creates its span in the right trace without a parameter. XCPlite takes over this mechanism and combines it with the XCP event:

- The current context of a thread is the identity, for example an instance of a class. It is attached with a scope, inherited by every function called in this scope, and can be handed to another thread.
- A context-aware event (`DaqEventCtxVar`) is triggered at one code location, but it is a separate XCP event with separate measurements for each context, named `<context>.<event>`. The instrumented code is the same for all instances and does not know for which one it is executed.
- What the tool receives is plain XCP: one event per instance and code location, one timestamp per sample, the signals of a sample copied at the same point of the program. A **consistent time series per identity**, from code locations which are executed in different contexts.

From OTel this takes the identity which travels with the work. From XCP it takes the time series of signals with a described clock. Neither has the combination alone: OTel would separate the instances by an attribute in a self-describing record, which A2L does not have ([4.3](#43-common-misconceptions)), and XCP had no way to carry an identity into code which does not know it.

In the [thread_context_demo](../examples/thread_context_demo/README.md), two signal generator instances, each executed by its own thread, call the shared function `Clip()`. `Clip()` is instrumented once, has no instance parameter, and delivers the events `SigGen1.clip` and `SigGen2.clip` with the measurements `SigGen1.input` and `SigGen2.input`. The method `Poll()` of both instances is called by the main loop, so it is executed in the context `Mainloop` of the caller, and its span `poll` is nested in the span of the caller.

Spans add a second time series per context: which section of code the identity is executing, recorded on the event `<context>.spans`, see [6.3](#63-what-is-taken-from-otel). `xcpclient --perfetto` shows each context as a process with its span track and its signals.

The same mechanism also removes the concurrency problem of [6.4](#64-several-threads-on-one-event) for these events: two instances in two threads no longer share an event. The problem remains only where one context is executed by several threads at the same time. It was found with the span event of a context, which all spans of the context share, when the span integration and the Perfetto export of `xcpclient` were built. [6.4](#64-several-threads-on-one-event) to [6.7](#67-the-decision-and-the-rule) describe it.

### 6.2 Splitting in the target or in the tool

A common objection: it is enough to measure the current context as one more signal of the event, and to let the tool demultiplex the samples into one time series per context. The context mechanism of [6.1](#61-the-motivation-a-time-series-per-identity) is needed in both cases, and in both cases the instrumented code does not know its instance. The difference is where the samples are split:

- **In the target**, as XCPlite does it: `DaqEventCtxVar` triggers a separate event for each context. The tool receives one event per instance and code location.
- **In the tool**: each code location has one event. Every sample carries the id of the current context, and the tool sorts the samples by this id into one series per context. This is the OTel data model: one signal with an attribute, grouped by the backend.

Both give consistent time series. The id and the values are copied in the same sample, so the split itself is exact. The differences are elsewhere. With C contexts and L context-aware code locations:

| | Split in the target: one event per context | Split in the tool: context id as a signal |
|---|---|---|
| Events | C × L, plus one span event per context. Limited by `OPTION_DAQ_EVENT_COUNT`, see the memory section of the [thread_context_demo README](../examples/thread_context_demo/README.md) | L |
| Number of contexts | Limited by `OPTION_DAQ_CONTEXT_COUNT` and by the event budget | Limited only by the range of the id |
| Context which appears after the A2L file is written | Its events and measurements are not in the A2L file, it cannot be measured | Measured. The tool shows its id, its name is missing |
| A2L file | One measurement per context and signal | One measurement per signal, plus the id and a conversion with the context names |
| Selection by the tool | Per instance: the tool measures only the instances it wants, each event with its own rate | All instances or none: every instance triggers the same event, and XCP has no filter on a value |
| Bandwidth and target CPU | For the selected instances | Always for all instances |
| Knowledge needed in the tool | Nothing beyond the A2L file | Which signal is the id, for which events it applies, which name belongs to which id |
| Standard XCP tool, data logger, measurement file | One channel per instance, works unchanged | One mixed channel. Splitting needs post-processing or a tool which knows the convention |
| Timestamps in the stream | In order per event, as long as a context is executed by one thread at a time ([6.7](#67-the-decision-and-the-rule)) | The shared event is triggered by all threads, so the stream has declining timestamps ([6.4](#64-several-threads-on-one-event)). After the split, each series is in order again if a context is executed by one thread at a time. But the tool has to accept the declines before it splits |
| Per instance online in the tool: display, trigger conditions, limits | Ordinary signals | Only in a tool which splits |
| Cost at the code location | Look up the event of the current context in a small array, create it at the first execution in a context | Copy the id |

Evaluation of the arguments:

- **Fewer events.** True, and it is the main advantage: L instead of C × L. Further advantages follow from it: no fixed limit on the number of contexts, contexts which appear late can still be measured, and the A2L file is smaller.
- **All instances are always measured.** True. The tool cannot select one instance, and it cannot measure one instance at a higher rate than another. Bandwidth and target CPU are spent on every instance, even if only one is of interest. In a control system with many instances this is the price which the target pays for the saved events.
- **Where does the tool get the knowledge from?** Not from the A2L file. XCPlite writes no such information, and no A2L construct for "this measurement partitions the samples of this event" is known to the authors (the ASAM text was not consulted, see [7](#7-sources-and-limits-of-this-document)). It needs a naming convention, a vendor-specific extension or a configuration outside the A2L file, and each tool has to implement it.
- **A standard tool or a logger cannot do it.** True. A tool which records one channel per measurement records the mixed series, and the split moves into post-processing: no per-instance display, trigger or limit during the measurement. It is even worse: the shared event has declining timestamps. A tool which takes a decline for a wrap around of the 32 bit timestamp corrupts the time axis of all instances, before any split could happen.

XCPlite uses both, for different dimensions. The instance is split in the target: each context has its own events. The thread within a context is split in the tool: with `OPTION_DAQ_SPAN_THREAD` the span event carries a thread index, and `xcpclient --perfetto` sorts the spans into one track per thread, found by the naming convention `<context>.thread` which only `xcpclient` knows. This is acceptable there, because the thread is a secondary dimension, its consumer is `xcpclient` itself, and the recommended use, one thread per context at a time, does not need it ([6.7](#67-the-decision-and-the-rule)).

The conclusion: splitting in the tool saves events in the target and moves the work into the tool, which then has to know a convention that the A2L file does not carry. Splitting in the target costs events and memory in the target, and the result is plain XCP: any tool shows one time series per instance with nothing more than the A2L file. Because the goal is the time series in standard tools, XCPlite splits in the target and pays with the event budget.

### 6.3 What is taken from OTel

XCPlite has an experimental extension which borrows three terms from OTel, see [thread_context_demo](../examples/thread_context_demo/README.md) and `inc/xcplib_context.hpp`:

- A **context** is a named identity, "who is executing": a class instance, a worker, a phase of a thread. It is long-lived, has a name in the A2L file and its own events. A thread enters a context with a scope and leaves it at the end of the block, the current context is thread-local, as in OTel.
- A **span** is a named section of code. Its begin and its end are each recorded as a sample on the event `<context>.spans`, with the current span, the nesting level and the duration of the span that ended.

The mechanics (thread-local current context, capture and attach for a new thread, nesting) are taken from OTel. The data model is XCP: a context is a **track**, a time series of "what is this identity doing now". An OTel context is not a track, and OTel has no direct counterpart to an XCPlite context. The closest are a trace or a parent span.

| | XCPlite span | OTel span |
|---|---|---|
| Recorded as | Two samples on the event of the context: state after begin, state after end | One record at the end |
| Identity | Span name (a small number), nesting level, context | Span id, parent span id, trace id |
| Nesting | Reconstructed by the tool from the sequence of samples | Given by the parent id |
| Needs ordered samples | Yes | No |
| Begin in one thread, end in another | Not possible | Possible |

### 6.4 Several threads on one event

Triggering an event consists of two steps: take the timestamp, then reserve a place in the transmit queue. The place in the queue decides the order in the stream to the tool. Between the two steps, a thread can be overtaken:

```
Thread A:  read clock: 100 ................................ enter queue    stream: second, timestamp 100
Thread B:             read clock: 200 ... enter queue                      stream: first,  timestamp 200
```

The tool receives 200, then 100: a **declining timestamp**. Both timestamps are correct. Only the order in the stream differs from the order in time. The size of the step back is the time by which thread A was delayed, some ten nanoseconds without preemption, any time with it.

This can happen wherever several threads trigger the **same event** at the same time. Without contexts, this is the normal case for code which is executed for several instances in several threads, and it comes on top of the mixed values ([6.1](#61-the-motivation-a-time-series-per-identity)). With contexts, each instance has its own events, and the problem is reduced to one context executed by several threads at once. For a span event that means several threads working in the same context at once.

### 6.5 What was tried

**A spin lock** around both steps, per context. It gives the exact order. It was removed, because it makes a thread wait for another one: if the holder is preempted, the others spin for that time, and with fixed priorities on one core a thread of higher priority can spin on a lock whose holder never runs.

**Correcting the timestamp** without a lock: the context keeps its newest timestamp, and a thread which took an older one uses the newest instead. This does not close the gap, because the correction is again a step *before* the queue, and a thread can still be overtaken after it. A stress test showed that it can even make things worse. The test is a model of the path, not the real XCP path: several threads in a tight loop take a timestamp, optionally correct it, and take a place from an atomic counter which stands for the queue. Declining timestamps in the order of the places, of 2 million events per run:

| Machine, threads | No correction | With correction |
|---|---|---|
| Raspberry Pi, 4 threads | 0.16 to 0.23 % | 2.3 to 2.8 % |
| Mac, 4 threads | 0.0004 to 0.27 % | 0.07 to 0.33 % |
| Mac, 2 threads | 0.001 to 0.003 % | 0.0001 to 0.0003 % |
| Raspberry Pi, 2 threads | 0.19 to 1.2 % | 0.12 to 0.89 % |

With two threads the correction helps. With more it creates new declines: a thread which is raised to the newest timestamp ends up above a third thread which holds an older timestamp and enters the queue after it. The shared variable also lengthens the time in which a thread can be overtaken. The threads in this test trigger far more densely than real spans do, and the numbers on the Mac vary a lot between runs. The correction was removed again.

### 6.6 What would be exact

- **A lock** around timestamp and queue. Exact, couples the threads.
- **The timestamp inside the queue.** The lock-free queue reserves a place with a compare-and-swap loop: load the head, try to advance it, repeat on failure. If the clock is read after loading the head and before the compare-and-swap, and again on every repetition, the timestamps are in the order of the places. The thread with the later place has loaded the head after the earlier thread succeeded, so it has read the clock later. This is exact and lock-free and needs no additional state. It is not implemented. It changes all queue implementations, puts a clock read into the most contended piece of code, and each DAQ list of an event would get its own timestamp. The cost is not measured.
- **Sorting in the tool.** The timestamps are correct, so a tool can sort the samples of an event within a short window. This costs the target nothing. `xcpclient` does not sort, but it accepts a declining timestamp: it places the sample before the newest one instead of taking it for a wrap around of the 32 bit timestamp.
- **Not sharing the event.** If each thread has its own event, or each context is executed by one thread at a time, the problem does not exist. This is the natural XCP solution.

### 6.7 The decision and the rule

XCPlite does not put the threads of a context in order. `TriggerSpanEvent` takes the clock and triggers the event, no thread ever waits for another one, and the context has no shared mutable state for this.

The rule which follows:

> **A context should be executed by one thread at a time.** Several threads may work in it one after the other.

Then the timestamps of its events are monotonic without any lock. The reason is the happened-before relation of [5.3](#53-causality-the-order-which-does-not-need-a-clock): a hand-over between threads (a queue, a mutex, a join) puts the last action of the first thread before the first action of the next one. Clock reads and queue places follow this order.

This is also what a context means. A context and its spans describe **one track**: one sequence of activities which may move across threads. Monotonic timestamps define order within one thread. A context extends this order to a chain of threads which hand the work over. Two threads which execute spans in the same context at the same moment are not one track: their spans overlap without being nested, and "the current span of the context" has no single answer.

A program may still do it, for example a method which distributes its work to a thread pool. Two ways to handle that:

- Give each worker its own context. This is the recommended way.
- Keep one context and record the thread. With `OPTION_DAQ_SPAN_THREAD` (not defined by default) each span sample carries an index of the thread, and `xcpclient --perfetto` shows one track per thread inside the context. The samples of each thread are in order among themselves. The samples of the event as a whole may have declining timestamps.

The same holds for a context-aware event (`DaqEventCtxVar`) triggered by several threads in the same context at the same code location, and for any plain XCP event triggered by several threads.

How other XCP tools react to a declining timestamp is not tested.

### 6.8 Clocks in XCPlite

| Topic | XCPlite |
|---|---|
| Clock source | `OPTION_CLOCK_EPOCH_ARB` (default): arbitrary epoch, monotonic, not adjusted (`CLOCK_MONOTONIC_RAW` on Linux). `OPTION_CLOCK_EPOCH_PTP`: `CLOCK_REALTIME`, which may be disciplined by NTP or PTP |
| Resolution | 1 ns (default) or 1 µs (`rtos` configuration) |
| Timestamp in a sample | 32 bit, in the first packet of a DAQ list |
| `GET_DAQ_CLOCK` | Implemented, with 64 bit clock value and synchronization state |
| `TIME_CORRELATION_PROPERTIES` | Implemented. With `XCP_ENABLE_PTP` the application provides the grandmaster identity, epoch, stratum level and synchronization state through callbacks, otherwise the clock is reported as free running |
| Example | [ptp4l_demo](../examples/ptp4l_demo/README.md): DAQ timestamps from a clock synchronized with `ptp4l` and `phc2sys` |

See [xcplib_cfg.md](xcplib_cfg.md) for the options.

With the default clock, the timestamps of all threads of a process come from one monotonic clock and never decline within a thread. With `OPTION_CLOCK_EPOCH_PTP` the clock is a real time clock, which the operating system may adjust. This is a second, independent source of irregular timestamps and has nothing to do with threads.

---

## 7. Sources and limits of this document

**OpenTelemetry.** The statements were checked against these pages in November 2026: the trace API, context, propagators, trace SDK, overview, glossary, logs data model and metrics data model of the [OpenTelemetry specification](https://github.com/open-telemetry/opentelemetry-specification/tree/main/specification), the span definition of the [OTLP protocol](https://github.com/open-telemetry/opentelemetry-proto/blob/main/opentelemetry/proto/trace/v1/trace.proto), and [W3C Trace Context](https://www.w3.org/TR/trace-context/). The second half of the trace SDK page, of the metrics data model and of W3C Trace Context was only available as a summary. "Not found" in this document means not found on these pages. Implementations in the individual languages and the behaviour of backends were not examined.

**XCP.** The statements describe XCPlite and its tool `xcpclient`, taken from the source code and the documentation of this repository. The text of the ASAM XCP standard was not consulted. In particular, whether the standard literally requires monotonic timestamps for an event is not verified. In practice tools depend on it, because the timestamp is truncated and because a measurement is read as a time series.


**Further reading.**

- Leslie Lamport: Time, Clocks, and the Ordering of Events in a Distributed System. Communications of the ACM, 1978.
- [XCP introduction](XCP_INTRODUCTION.md) and the Vector XCP book linked there.
- [thread_context_demo](../examples/thread_context_demo/README.md): contexts and spans in XCPlite, with a comparison to OTel at the level of the API.
