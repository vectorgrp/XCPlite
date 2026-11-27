# XCPlite Shared Memory Transport Layer - Technical Details 

This document contains advanced technical information about XCPlite's multi application mode (SHM mode)


## Overview

When compiled in multi application mode, all application processes use a shared transmit queue, shared calibration RCU, and shared XCP state.  
Exactly one application is the XCP server, this may be the first one running (XCP leader) or a dedicated application (XCP daemon).  
Even in the later case, the applications may start in any order.  
Requires a POSIX-compliant platform (Linux / macOS / QNX). Not supported on Windows.  


## Technical Details

This is possible, because the complete XCP state shared among threads in a single application is memory-safe and lock-less.  
The contention between threads in different application is exactly the same as the contention between threads in a single application, so no additional locking or synchronization is needed.  
The contention for read access to calibration parameters is zero, unless they would be shared between applications.  
In this simple implementation, all applications share the same transmit queue for measurement data acquisition, so the contention for write access to the transmit queue is caused by sharing a cache line for the queue head and tail offsets. This could easily be changed by introducing an individual queue for every application.  

The shared memory mode is enabled by defining `XCP_SHM_MODE` in the configuration file `xcplib_cfg.h`.  

The shared memory has a header section with an application list, were multiple applications can register. Each application gets an application id. The first application creates the shared memory segment (leader), and subsequent applications attach to it (follower).  

Application processes can terminate and restart anytime and reattach to their previous state, even during running measurement, which allows simple first cycle measurement data acquisition with startup delay for XCP initialization far below a millisecond.  
  

There are new 3 new modes of operation, which can be specified when calling `XcpInit`:

```c

#define XCP_MODE_SHM 0x80         ///< Initialize and activate XCP, allocate state in POSIX shared memory
#define XCP_MODE_SHM_AUTO 0x04    ///< Set this flag to automatically choose leader as XCP server
#define XCP_MODE_SHM_SERVER 0x08  ///< Set this flag, to make this application the XCP server, regardless which application is started first

XcpInit("MyProject" /* Project name*/, "V1.0.1" /* EPK version string*/,  XCP_MODE_SHM_AUTO /* activate XCP in multi application mode */);

```

Note:  
The server is never automatically migrated to another running application. If the server dies, the next application in mode XCP_MODE_SHM_AUTO or XCP_MODE_SHM_SERVER will become the new server and recreates the shared memory.  


## BIN persistence file usage

The persistence file manages event and calibration segment information for all applications, so they all shared the same number space for calibration segments and events.

Contexts and spans (`OPTION_DAQ_CONTEXT_LIST`, C++ API `xcp::Context`, `xcp::Scope`, `DaqSpan`, `DaqEventCtxVar`) are available in SHM mode. The context list and the span name list are part of the shared state, like the event list:

- Each application has its own namespace for context names. Two applications may both have a context `SigGen1`; these are two contexts with different context ids, and their events and measurements are `<app>.SigGen1.<event>` and `<app>.SigGen1.<variable>` in the A2L file.
- Span names and span ids are common to all applications, the server writes one enum conversion `conv.spans`.
- `OPTION_DAQ_CONTEXT_COUNT`, `OPTION_DAQ_SPAN_COUNT` and `OPTION_DAQ_EVENT_COUNT` are limits for all applications together.
- The persistence file stores the context list with the owning application of each context, the contexts are restored with their ids on load, before the events.
- `shmtool status -v` lists the contexts and span names. 

Using the binary persistence mode is mandatory for the shared memory mode, because the shared memory state is initialized from the persistence file by the leader.  
From BIN format V2.5, there is a new section in the persistence file for the application list. This allows to keep the application id assignment stable, regardless of startup order.  

When any application gets a new version (EPK), the BIN file is invalidated, which means the overall A2L file will be updated.  
The EPK written to the overall A2L file and represented by the XCP server, is a hash generated from all application EPKs. There is only one EPK memory segment managed by the server for all applications.  


## A2L file generation

A2L file generation works as usual. Each application creates an individual A2L file. All A2L files will be merged by the XCP server and provided for download via XCP as a single file. Application namespaces are created by prepending the applications name to each A2L object. The server can trigger the applications to freeze and finalize their A2L files.  



## Tools

There are tools included for working with or demonstrating the shared memory transport layer:

- `shmtool` - A helper tool to check and clear the state of the XCPlite shared memory and to trigger A2L file generation in the instrumented applications
- `xcpdaemon` - An XCP on Ethernet server, which can attach to multiple XCP on SHM instrumented applications. This is just another (empty) application in XCP_MODE_SHM_SERVER, assuming all user applications are in mode XCP_MODE_SHM.  

See the readme files in the [tools](tools/) folder for more information about how to use these tools.


## Known limitations

The shared memory mode is experimental. Context and span has not been tested yet.

This list covers limitations and problems which are known and not solved yet.

### General

- **Platforms.** POSIX only (Linux, macOS, QNX), not supported on Windows.
- **Shared limits.** `OPTION_DAQ_EVENT_COUNT` (16), `OPTION_DAQ_CONTEXT_COUNT` (8), `OPTION_DAQ_SPAN_COUNT` (32), the calibration segment limit and `OPTION_DAQ_MEM_SIZE` are limits for all applications together, not per application. The defaults are sized for a single application.
- **A full event list aborts the application.** When the event list is full, `DaqEventVar` and the other macros which register measurements for a new event end in an assertion in the A2L generator (`A2lGetAddr_`). The context-aware `DaqEventCtxVar` skips the measurement instead.
- **Event creation is not process safe.** The event list is protected by a process local mutex only. Two applications must not create events at the same time, in practice: do not start applications which register at startup at the same moment. The context list and the span name list are lock-free name lists ([NAMELIST.md](NAMELIST.md)), which are designed for concurrent registration by several processes, but an `xcp::Context` also creates its `spans` event, so the restriction applies to contexts as well. Concurrent registration by several processes has not been tested.
- **Event names by protocol are not unique.** `GET_DAQ_EVENT_INFO` returns the event name without the application name, only the A2L file has `<app>.<event>`. Equal event names in two applications, for example `mainloop`, give duplicate names. xcpclient without `--upload-a2l` stops with a `Duplicate` error in this case (observed with context events, expected for any equal event name). Use `--upload-a2l`.
- **The server does not migrate.** If the server application ends, the shared memory is recreated by the next application in mode `XCP_MODE_SHM_AUTO` or `XCP_MODE_SHM_SERVER`, see above.
- **A2L timing.** Each application must have registered all its events and measurements before the server finalizes the main A2L file. Later registrations are not in the A2L file.
- **Layout version.** All applications and tools (`shmtool`, `xcpdaemon`) must be built from the same XCPlite version and configuration. The layout version `SHM_VERSION` is checked, a mismatch needs `shmtool clean`.
- **ECU project name.** Setting the ECU project name is not implemented, the main A2L file and the persistence file are named `main`.
- **Stale A2L state.** It is not checked whether the partial A2L file of a preloaded application still exists on disk.

### Contexts and spans

- **Span names are registered at runtime.** Link time registration (`OPTION_DAQ_SPAN_SECTION_REGISTRATION`) is not available in SHM mode, each process would have its own span ids.
- **Span ids are shared.** Span names of all applications are in one list and one conversion `conv.spans`. A span name used by two applications has one id. The limit `OPTION_DAQ_SPAN_COUNT` is reached sooner with several applications.
- **Span names are not persisted.** Span ids are assigned in the order of first execution and may differ between runs, when the applications start in a different order. An existing A2L file (`A2L_MODE_WRITE_ONCE`) then shows wrong span names.
- **Context ids are not stable across restarts** when the set of applications changes. The names are stable, the per code location caches are per process and are rebuilt.
- **Declining timestamps.** When several threads trigger the same event at the same time, a sample can have an older timestamp than the sample before. This applies to the `spans` event of a context in which several threads work at the same time, and to a context-aware event triggered by several threads in the same context at the same code location. Nothing is serialized. A context should be executed by one thread at a time, see [TIME_AND_CAUSALITY.md](TIME_AND_CAUSALITY.md).
- **Span duration** `span_dt` belongs to the span which just ended, the sample does not name it.
- **`DaqSpan` before `XcpInit`.** A span which is executed for the first time before XCP is activated keeps the span id 0 (none).
- **A full context list** makes further contexts behave as "no context": their code measures on the events without context, mixed with other instances.
- **Name length.** Context and event names are limited to 15 characters each, the application name is prepended in the A2L file.



### Not tested

- `xcpdaemon` as the XCP server for applications which use contexts. The tests used an application in mode `XCP_MODE_SHM_AUTO` as server.
- `thread_context_demo` is built in the default configuration only, there is no SHM example which uses contexts in the repository.
