# XCPlite API Overview

This is a one-page index of the whole public API of libxcplite: functions, macros, templates, classes, constants and types. For each entry it lists the configuration option that enables it and the examples that use it. For details, see the doc comments in the headers, and the examples for their usage. The basic call sequence of an application is shown in [Getting Started](../README.md#getting-started).

| Header | Language | Content |
|---|---|---|
| [inc/xcplib.h](../inc/xcplib.h) | C, usable from C++ | XCP server, calibration segments, DAQ events, instrumentation macros, callbacks, constants |
| [inc/a2l.h](../inc/a2l.h) | C, usable from C++ | Runtime A2L generation (requires `OPTION_ENABLE_A2L_GENERATOR`) |
| [inc/xcplib.hpp](../inc/xcplib.hpp) | C++17 | RAII calibration segment wrappers, variadic DAQ templates (includes `xcplib.h`, and `a2l.h` when the A2L generator is enabled) |
| [inc/a2l.hpp](../inc/a2l.hpp) | C++17 | Once-patterns and variadic typedef creation for runtime A2L generation |

## How to read the tables

**Option**: what has to be enabled for the entry to exist or to have an effect. `—` means it is always available. The options are explained in [xcplib_cfg.md](xcplib_cfg.md) and set in `src/xcplib_cfg.h` and the configuration override headers `src/xcplib_<config>_cfg.h`. A `!` in front of an option means the entry requires that option to be **off**.

**Examples**: the examples that use the entry, based on their source code. `—` means no example uses it directly. Some of those entries are tested in `test/`, and many are used internally by other macros.

### Options per build configuration

| Option | default | no_a2l | ptp | shm | rtos | raw |
|---|:-:|:-:|:-:|:-:|:-:|:-:|
| `OPTION_ENABLE_A2L_GENERATOR` (runtime A2L generation, `a2l.h`) | ✓ | | ✓ | ✓ | | ✓ |
| `OPTION_ENABLE_A2L_UPLOAD` (A2L upload via `GET_ID`) | ✓ | | ✓ | ✓ | | ✓ |
| `OPTION_ENABLE_ELF_UPLOAD` (ELF upload via `GET_ID`) | | ✓ | | | | |
| `OPTION_DAQ_EVENT_LIST` (runtime event management) | ✓ | | ✓ | ✓ | | ✓ |
| `OPTION_SECTION_REGISTRATION` (link-time events and segments) | | ✓ | | | ✓ | |
| `OPTION_CAL_SEGMENTS` (calibration segment management) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| `OPTION_CAL_SEGMENTS_ABS` (ACSDD addressing, disables application addressing) | | ✓ | | | ✓ | |
| `OPTION_CAL_RCU_REFCOUNT` (reference counting RCU, only for special needs; changes the `XcpUnlockCalSeg` signature) | | | | | | |
| `OPTION_ENABLE_PERSISTENCE` (binary `.bin` persistence file) | ✓ | | ✓ | ✓ | | ✓ |
| `OPTION_SHM_MODE` (multi-application shared memory) | | | | ✓ | | |
| `OPTION_ENABLE_TCP` / `OPTION_ENABLE_UDP` | ✓/✓ | ✓/✓ | ✓/✓ | ✓/✓ | –/✓ | –/– |
| `OPTION_ENABLE_UDP_RAW` (raw Ethernet transport) | | | | | | ✓ |
| `OPTION_CLOCK_TICKS_1NS` / `_1US` (DAQ clock resolution) | 1ns | 1ns | 1ns | 1ns | 1µs | 1ns |

### Examples

| Example | Lang | Configuration |
|---|---|---|
| [hello_xcp](../examples/hello_xcp/) | C | default, shm |
| [hello_xcp_cpp](../examples/hello_xcp_cpp/) | C++ | default, shm |
| [c_demo](../examples/c_demo/) | C | default |
| [cpp_demo](../examples/cpp_demo/) | C++ | default |
| [struct_demo](../examples/struct_demo/) | C | default |
| [multi_thread_demo](../examples/multi_thread_demo/) | C | default |
| [point_cloud_demo](../examples/point_cloud_demo/) | C++ | default |
| [cmp_demo](../examples/cmp_demo/) | C | default |
| [bpf_demo](../examples/bpf_demo/) | C | default (Linux, libbpf) |
| [ptp4l_demo](../examples/ptp4l_demo/) | C++ | default, ptp |
| [udp_raw_demo](../examples/udp_raw_demo/) | C | raw |
| [no_a2l_demo](../examples/no_a2l_demo/) | C | no_a2l |
| [no_a2l_demo_cpp](../examples/no_a2l_demo_cpp/) | C++ | no_a2l |
| [freertos_demo](../examples/freertos_demo/) (`xcp_demo.c`, shared by the emu/esp32/stm32 targets, compiled as C and C++) | C/C++ | rtos |
| [silkit_demo](../examples/silkit_demo/) | C++ | shm (standalone project) |
| [external_example](../examples/external_example/), [fetchcontent_example](../examples/fetchcontent_example/) | C/C++ | default (standalone projects) |

---

## Part 1: C API ([xcplib.h](../inc/xcplib.h))

The C API can be used from C++ as well. Entries marked **C only** are hidden by `#ifndef __cplusplus`, and `xcplib.hpp` provides C++ replacements for them.

### 1.1 Initialization and lifecycle

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `XcpInit(name, epk, mode)` | function | Initialize the XCP singleton with project name, EPK version string and `XCP_MODE_*` flags. Must be called first | — | all examples |
| `XcpDeinit()` | function | Internal, used by the Rust `build.rs` | — | — |
| `XcpSetLogLevel(level)` | function | Set the log level 0..5, works before `XcpInit` | `OPTION_ENABLE_DBG_PRINTS`, no effect with `OPTION_FIXED_DBG_LEVEL` | almost all examples |
| `XcpDisconnect()` | function | Force disconnect: stop DAQ, flush the queue and pending calibrations | — | almost all examples |
| `XcpSendTerminateSessionEvent()` | function | Ask the XCP client to terminate the session | — | — |
| `XcpPrint(str)` | function | Send a text message to the client's write window (SERV_TEXT) | — | c_demo, bpf_demo |
| `XCP_MODE_DEACTIVATE` | const | Passive mode: XCP is not activated, instrumentation is a no-op and calibration reads the default pages | — | — |
| `XCP_MODE_LOCAL` | const | Activate XCP with process-local state | — | almost all examples |
| `XCP_MODE_PERSISTENCE` | const | Load the `.bin` persistence file (stable event and segment numbering, persisted calibration data) | `OPTION_ENABLE_PERSISTENCE` (ignored without it) | hello_xcp, hello_xcp_cpp, c_demo, cpp_demo, struct_demo, point_cloud_demo, cmp_demo, udp_raw_demo, silkit_demo, fetchcontent_example |
| `XCP_MODE_SHM` | const | Allocate the XCP state in POSIX shared memory | `OPTION_SHM_MODE` | silkit_demo |
| `XCP_MODE_SHM_AUTO` | const | The first application becomes the XCP server (leader) | `OPTION_SHM_MODE` | hello_xcp, hello_xcp_cpp, silkit_demo |
| `XCP_MODE_SHM_SERVER` | const | Force this application to become the XCP server | `OPTION_SHM_MODE` | silkit_demo |

### 1.2 XCP on Ethernet server

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `XcpEthServerInit(addr, port, use_tcp, queue_size)` | function | Start the XCP on Ethernet server, after `XcpInit` | `OPTION_ENABLE_UDP`, `OPTION_ENABLE_TCP` (for `use_tcp`) or `OPTION_ENABLE_UDP_RAW` | all examples |
| `XcpEthServerShutdown()` | function | Stop the server | as above | almost all examples |
| `XcpEthServerStatus()` | function | `true` while the server is running | as above | c_demo |

### 1.3 Status and information

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `XcpIsActivated()` | function | XCP was initialized in an active mode | — | multi_thread_demo |
| `XcpIsStarted()` | function | Protocol layer is started | — | freertos_demo (esp32) |
| `XcpIsConnected()` | function | A client is connected | — | freertos_demo (esp32) |
| `XcpIsDaqRunning()` | function | DAQ measurement is running | — | freertos_demo (esp32) |
| `XcpGetInitMode()` | function | The mode passed to `XcpInit` | — | silkit_demo |
| `XcpIsInShmMode()` | function | Library runs in shared memory mode | `OPTION_SHM_MODE` (otherwise always `false`) | silkit_demo |
| `XcpGetProjectName()` | function | Project name given to `XcpInit` | — | — |
| `XcpGetLocalEpk()` | function | EPK of this application | — | — |
| `XcpGetEcuEpk()` | function | EPK of the whole ECU. It differs from the local EPK only in SHM mode | — | — |
| `XcpSetA2lName(name)` / `XcpGetA2lName()` | function | Announce an existing A2L file for upload with `GET_ID` | `OPTION_ENABLE_A2L_UPLOAD` | — (`A2lInit`/`A2lFinalize` call it) |
| `XcpSetElfName(name)` / `XcpGetElfName()` | function | Announce the ELF file for upload with `GET_ID` | `OPTION_ENABLE_ELF_UPLOAD` | hello_xcp, hello_xcp_cpp, cmp_demo, udp_raw_demo, no_a2l_demo, no_a2l_demo_cpp |

### 1.4 Calibration segments: functions

Calibration segments are thread safe and lock free (RCU, see [CAL_RCU.md](CAL_RCU.md)). They are managed by index.

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `XcpCreateCalSeg(name, default_page, size)` | function | Create a calibration segment with working and reference page and an A2L `MEMORY_SEGMENT` | `OPTION_CAL_SEGMENTS` | hello_xcp, c_demo, cmp_demo, multi_thread_demo, udp_raw_demo |
| `XcpCreateCalBlk(name, default_page, size)` | function | Create a calibration block (two pages, no `MEMORY_SEGMENT`) | `OPTION_CAL_SEGMENTS` | — |
| `XcpLockCalSeg(index)` | function | Lock a segment and return the active page. Wait-free, may be nested | `OPTION_CAL_SEGMENTS` | hello_xcp, c_demo, cmp_demo, multi_thread_demo, udp_raw_demo |
| `XcpUnlockCalSeg(index)` | function | Unlock a segment | `OPTION_CAL_SEGMENTS`, `!OPTION_CAL_RCU_REFCOUNT` | hello_xcp, c_demo, cmp_demo, multi_thread_demo, udp_raw_demo |
| `XcpUnlockCalSeg(index, page)` | function | Unlock a segment, with the page pointer returned by the lock | `OPTION_CAL_SEGMENTS`, `OPTION_CAL_RCU_REFCOUNT` | — (tests only) |
| `XcpGetCalSegCount()` | function | Number of segments and blocks | `OPTION_CAL_SEGMENTS` | — |
| `XcpFindCalSeg(name)` | function | Look up a segment by name | `OPTION_CAL_SEGMENTS` | — |
| `XcpGetCalSegName(index)` | function | Segment name | `OPTION_CAL_SEGMENTS` | — |
| `XcpGetCalSegSize(index)` | function | Segment size in bytes | `OPTION_CAL_SEGMENTS` | — |
| `XcpGetCalSegNumber(index)` | function | XCP/A2L segment number (`XCP_UNDEFINED_CALSEG_NUM` for blocks) | `OPTION_CAL_SEGMENTS` | — |
| `XcpResetAllCalSegs()` | function | Switch all segments to their default page (emergency) | `OPTION_CAL_SEGMENTS`, `OPTION_ENABLE_PERSISTENCE` | — |
| `XcpFreeze()` | function | Write the working pages to the persistence file; they become the defaults of the next run | `OPTION_CAL_SEGMENTS`, `OPTION_ENABLE_PERSISTENCE` | silkit_demo |
| `XcpBinWrite(epk)` | function | Create the persistence file with the current working pages as defaults | `OPTION_ENABLE_PERSISTENCE` | — |

### 1.5 Calibration segments: macros (C only)

The macros take the default page variable as an identifier: `name` is the segment name, `&name` the default page, `sizeof(name)` the size. They define the static index variable `calseg_id_<name>` (or `calblk_id_<name>`) and a section descriptor `calseg__<name>`, which also serves as a marker for `xcpclient`.

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `CalSegCreate(name)` / `CalBlkCreate(name)` | macro | Create the segment or block immediately, if it does not exist yet. Can be used inside functions and loops | `OPTION_CAL_SEGMENTS`. With `OPTION_SECTION_REGISTRATION`, `XcpInit` also pre-registers the segment | no_a2l_demo, freertos_demo |
| `CalSegDecl(name)` / `CalBlkDecl(name)` | macro | File scope declaration only. `XcpInit` creates the segment from the `xcp_cals` section | `OPTION_SECTION_REGISTRATION` (compile error without it) | no_a2l_demo, freertos_demo |
| `CalSegLock(name)` / `CalBlkLock(name)` | macro | Typed lock, returns `const T*`. In passive mode it returns the default page | `OPTION_CAL_SEGMENTS` | no_a2l_demo, freertos_demo |
| `CalSegUnlock(name)` / `CalBlkUnlock(name)` | macro | Unlock | `!OPTION_CAL_RCU_REFCOUNT` | no_a2l_demo, freertos_demo |
| `CalSegUnlock(name, ptr)` / `CalBlkUnlock(name, ptr)` | macro | Unlock with the pointer returned by the lock | `OPTION_CAL_RCU_REFCOUNT` | — |
| `CalSegIndex(name)` | macro | The `tXcpCalSegIndex` of the segment | — | — |

### 1.6 DAQ events: creation

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `XcpCreateEvent(name, cycle_ns, prio)` | function | Create an event, or return the existing one with this name. Thread safe | `OPTION_DAQ_EVENT_LIST` | multi_thread_demo, bpf_demo, freertos_demo |
| `XcpCreateEventInstance(name, cycle_ns, prio)` | function | Create a new indexed instance (`name_<n>`) of an event | `OPTION_DAQ_EVENT_LIST` | — |
| `XcpFindEvent(name)` | function | Look up an event id by name | — | — (used by the trigger macros) |
| `XcpGetEventIndex(event)` | function | Instance index (1..) of an indexed event | `OPTION_DAQ_EVENT_LIST` | multi_thread_demo |
| `DaqCreateEvent(name)` | macro | Create a sporadic event (inside a function). It is created at runtime, or registered at link time with section registration | `OPTION_DAQ_EVENT_LIST` or `OPTION_SECTION_REGISTRATION` | most C examples, cpp_demo, silkit_demo, no_a2l_demo, no_a2l_demo_cpp, freertos_demo |
| `DaqCreateEventExt(name, cycle_us, prio)` | macro | Like `DaqCreateEvent`, with a cycle time and priority | as above | — |
| `DaqDeclareEvent(name)` | macro | File scope event declaration, so callbacks can trigger it by name | `OPTION_SECTION_REGISTRATION` | — |
| `DaqDeclareEventExt(name, cycle_us, prio)` | macro | As above, with a cycle time and priority | `OPTION_SECTION_REGISTRATION` | — |
| `DaqCreateEvent_s(name_str)` | macro | Create an event with a runtime string name, once per thread | `OPTION_DAQ_EVENT_LIST` | cpp_demo |
| `DaqCreateEventInstance(name)` | macro | Create a new event instance on every call | `OPTION_DAQ_EVENT_LIST` | multi_thread_demo |
| `DaqGetEventInstanceId(name)` | macro | Id of the instance created by `DaqCreateEventInstance` | `OPTION_DAQ_EVENT_LIST` | multi_thread_demo |

### 1.7 DAQ events: trigger functions

All trigger functions are thread safe. They are lock free with the 64-bit queues.

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `XcpEvent(event)` | function | Trigger, absolute addressing only | — | — |
| `XcpEventAt(event, clock)` | function | As above, with an explicit timestamp | — | — |
| `XcpEventExt(event, base)` | function | Trigger with one base address (stack or relative addressing) | — | bpf_demo |
| `XcpEventExtAt(event, base, clock)` | function | As above, with a timestamp | — | — |
| `XcpEventExt_Var(event, count, ...)` | function | Trigger with a variadic list of base addresses (address extensions 2..) | — | — (used by the trigger macros) |
| `XcpEventExtAt_Var(event, clock, count, ...)` | function | As above, with a timestamp | — | multi_thread_demo |
| `XcpEventExtAt_(event, count, bases, clock)` | function | Base address array variant, used by the C++ templates | — | — |
| `XcpEventEnable(event, enable)` | function | Enable or disable an event | `OPTION_DAQ_EVENT_LIST` | — |

### 1.8 DAQ events: trigger macros

The `trg__<modes>__<event>` static variables emitted by these macros are the markers that `xcpclient` uses for offline A2L generation (see [TECHNICAL.md](TECHNICAL.md)). Functions which measure their local variables must not be inlined (`XCP_NOINLINE`).

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `DaqTriggerEvent(name)` | macro | Trigger by name, with absolute and stack addressing (AAS) | — (with section registration, the event descriptor must be visible) | hello_xcp, cpp_demo, struct_demo, cmp_demo, bpf_demo, udp_raw_demo, no_a2l_demo, freertos_demo, external_example, fetchcontent_example |
| `DaqTriggerEventAt(name, clock)` | macro | As above, with a timestamp | — | bpf_demo |
| `DaqTriggerEvent_i(id)` / `DaqTriggerEventAt_i(id, clock)` | macro | Trigger by event id, no lookup | — | multi_thread_demo |
| `DaqTriggerEventExt(name, base)` | macro | Trigger by name with absolute, stack and relative addressing (AASD) | — | c_demo, struct_demo, no_a2l_demo, silkit_demo |
| `DaqTriggerEventExt_s(name_str, base)` | macro | As above, with the name as a runtime string (lookup once per thread) | — | cpp_demo |
| `DaqTriggerEventExt_i(id, base)` | macro | As above, by event id | — | — |
| `DaqCreateAndTriggerEvent(name)` | macro | Create and trigger in one statement (AAS) | — | no_a2l_demo, no_a2l_demo_cpp, freertos_demo |
| `DaqEventVar(name, A2L_MEAS(var, comment), ...)` | macro | **C only.** Create the event, register the listed variables once and trigger. In C, `A2L_MEAS`/`A2L_MEAS_PHYS` are empty markers in front of the tuples `(var, comment)` and `(var, comment, unit, min, max)` | `OPTION_ENABLE_A2L_GENERATOR` | hello_xcp |
| `DaqEventEnable(name)` / `DaqEventDisable(name)` | macro | Enable or disable an event by name | `OPTION_DAQ_EVENT_LIST` | — |

### 1.9 Capturing local variables

The capture macros copy register-allocated locals into a struct on the stack, which is measured with address extension 3 (AASR). Not available with MSVC.

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `DaqTriggerEventCapture(name, vars...)` | macro | Trigger and capture up to 16 variables | — (not MSVC) | no_a2l_demo_cpp |
| `DaqTriggerEventCaptureAt(name, clock, vars...)` | macro | As above, with a timestamp | — (not MSVC) | — |
| `DaqCreateAndTriggerEventCapture(name, vars...)` | macro | Create, trigger and capture | — (not MSVC) | no_a2l_demo, no_a2l_demo_cpp, freertos_demo |
| `XCP_CAPTURE_MAX_COUNT` | const | Maximum number of captured variables (16) | — | — |

### 1.10 Instrumentation helpers and offline A2L metadata

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `XCP_NOINLINE` | macro | Function attribute: keep a function with measured locals out of line | — | no_a2l_demo, no_a2l_demo_cpp, freertos_demo |
| `XCP_MEAS` | macro | `volatile`: keep a local variable in memory for offline A2L | — (meant for `no_a2l`/`rtos`) | no_a2l_demo, no_a2l_demo_cpp, freertos_demo |
| `XCP_FORCE_TO_STACK(var)` | macro | Force an aggregate local to stay on the stack | — | no_a2l_demo, no_a2l_demo_cpp |
| `XCP_MEMORY_BARRIER()` | macro | Compiler memory barrier | — | — |
| `XCP_NO_TAIL_CALL()` | macro | Prevent tail call optimization after a trigger that passes the frame address | — | — (used by the trigger macros) |
| `XCP_COMMENT(name, text)` | macro | A2L comment for a variable, stored in the `xcp_meta` section | `OPTION_SECTION_REGISTRATION` | no_a2l_demo, no_a2l_demo_cpp, freertos_demo |
| `XCP_UNIT(name, unit)` | macro | A2L physical unit | `OPTION_SECTION_REGISTRATION` | no_a2l_demo, no_a2l_demo_cpp, freertos_demo |
| `XCP_LIMITS(name, min, max)` | macro | A2L limits | `OPTION_SECTION_REGISTRATION` | no_a2l_demo, no_a2l_demo_cpp, freertos_demo |
| `XCP_READ_WRITE(name)` | macro | Mark a measurement as writable | `OPTION_SECTION_REGISTRATION` | no_a2l_demo, no_a2l_demo_cpp, freertos_demo |
| `XcpCreateEpk(epk)` | macro | Emit the EPK string `xcp_epk__` for `xcpclient` (in the `xcp_epk` section with section registration) | — (meant for offline A2L) | no_a2l_demo, no_a2l_demo_cpp, freertos_demo |
| `THREAD_LOCAL` | macro | Portable thread-local storage specifier | — | multi_thread_demo |
| `XCP_MAYBE_UNUSED` | macro | Portable "maybe unused" attribute | — | — |

### 1.11 Addressing helpers

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `xcp_get_frame_addr()` | macro | Stack frame base address for stack-relative addressing (clang: frame pointer, GCC: CFA, MSVC: intrinsic) | — | multi_thread_demo, bpf_demo, freertos_demo |
| `XCP_FRAME_ADDR_OFFSET` | const | Offset subtracted from the frame address, which sets the maximum frame size (0x10000) | — | — |
| `xcp_get_base_addr()` / `gXcpBaseAddr` | macro / variable | Base address of absolute addressing (fast path) | — | — |
| `ApplXcpGetBaseAddr()` | function | Base address of absolute addressing | — | — |
| `ApplXcpSetBaseAddr(addr)` | function | Override the base address (`NULL` restores the module address) | — | c_demo, external_example |
| `ApplXcpGetModuleAddr()` | function | Module load address, the default base | — (Linux/QNX 64-bit, macOS, Windows) | c_demo |
| `ApplXcpGetAddr(ptr)` | function | 32-bit XCP/A2L address of a pointer | — | no_a2l_demo |
| `ApplXcpGetAddrExt(ptr)` | function | XCP address extension for absolute addressing | — | no_a2l_demo |

### 1.12 Clock

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `ApplXcpGetClock64()` | function | Current DAQ clock in `CLOCK_TICKS_PER_S` ticks | `OPTION_CLOCK_TICKS_1NS`/`_1US`, `OPTION_CLOCK_EPOCH_ARB`/`_PTP` | multi_thread_demo, point_cloud_demo, freertos_demo (esp32) |
| `ApplXcpGetClockState()` | function | Clock synchronization state (`CLOCK_STATE_*`) | — | — |
| `ApplXcpGetClockInfoGrandmaster(...)` | function | Client and grandmaster UUID, epoch, stratum | — | — |
| `ApplXcpRegisterGetClockCallback(cb)` | function | Replace the clock source | — | ptp4l_demo, silkit_demo, freertos_demo |
| `ApplXcpRegisterGetClockStateCallback(cb)` | function | Replace the clock state source | — | ptp4l_demo |
| `ApplXcpRegisterGetClockInfoGrandmasterCallback(cb)` | function | Replace the grandmaster info source | — | ptp4l_demo |
| `CLOCK_STATE_SYNCH_IN_PROGRESS`, `CLOCK_STATE_SYNCH`, `CLOCK_STATE_FREE_RUNNING`, `CLOCK_STATE_GRANDMASTER_STATE_SYNCH` | const | Clock states | — | ptp4l_demo |
| `CLOCK_STRATUM_LEVEL_UNKNOWN`, `_ARB`, `_UTC` | const | Stratum levels | — | ptp4l_demo |
| `CLOCK_EPOCH_TAI`, `_UTC`, `_ARB` | const | Clock epochs | — | ptp4l_demo |

### 1.13 Application callbacks

If no callback is registered, XCPlite uses its own default behaviour from `src/xcpappl.c`.

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `ApplXcpRegisterConnectCallback(cb)` | function | Accept or reject `CONNECT` | — | — |
| `ApplXcpRegisterPrepareDaqCallback(cb)` | function | Veto a DAQ start | — (protocol layer ≥ 1.4) | — |
| `ApplXcpRegisterStartDaqCallback(cb)` | function | DAQ started | — | — |
| `ApplXcpRegisterStopDaqCallback(cb)` | function | DAQ stopped | — | — |
| `ApplXcpRegisterFreezeDaqCallback(cb)` | function | Reserved, not invoked yet | — | — |
| `ApplXcpRegisterGetCalPageCallback(cb)` | function | Custom calibration page handling: get the page | `!OPTION_CAL_SEGMENTS` | — |
| `ApplXcpRegisterSetCalPageCallback(cb)` | function | Custom calibration page handling: set the page | `!OPTION_CAL_SEGMENTS` | — |
| `ApplXcpRegisterFreezeCalCallback(cb)` | function | Custom freeze of the working page | `!OPTION_CAL_SEGMENTS`, `OPTION_ENABLE_PERSISTENCE` | — |
| `ApplXcpRegisterInitCalCallback(cb)` | function | Custom `COPY_CAL_PAGE` | `!OPTION_CAL_SEGMENTS` | — |
| `ApplXcpRegisterCheckCallback(cb)` | function | Check access rights for MTA based memory access | — | — |
| `ApplXcpRegisterReadCallback(cb)` | function | Application addressing: read memory | `!OPTION_CAL_SEGMENTS_ABS` or `OPTION_SHM_MODE` | c_demo, external_example |
| `ApplXcpRegisterWriteCallback(cb)` | function | Application addressing: write memory, with delayed atomic calibration | as above | c_demo, external_example |
| `ApplXcpRegisterFlushCallback(cb)` | function | Application addressing: apply the delayed writes | as above | — |
| `ApplXcpRegisterIdleCallback(cb)` | function | Periodic background work on the server thread | — | freertos_demo |

### 1.14 Utilities and debug

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `clockGetMonotonicNs()` / `clockGetMonotonicUs()` | function | Monotonic clock | — | cpp_demo, multi_thread_demo |
| `sleepUs(us)` / `sleepMs(ms)` | function | Portable sleep | — | most examples |
| `XcpEthServerDebugInfo(&rx, &tx)` | function | Stack usage of the server threads | values with `TEST_STACK_SIZE` | freertos_demo (esp32) |
| `clockGetPrintStatistic()` | function | Print clock call statistics | values with `TEST_CLOCK_GET_STATISTIC` | multi_thread_demo |

### 1.15 Types and constants

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `tXcpCalSegIndex` | type | Calibration segment handle (`uint16_t`) | — | hello_xcp, c_demo, cmp_demo, multi_thread_demo, udp_raw_demo, freertos_demo |
| `tXcpCalSegNumber` | type | XCP/A2L segment number (`uint8_t`) | — | — |
| `tXcpEventId` | type | DAQ event handle (`uint16_t`) | — | multi_thread_demo, bpf_demo |
| `tXcpCalSegDescriptor` | type | 32-byte calibration segment descriptor in the `xcp_cals` section | — | — |
| `tXcpEventDescriptor` | type | 16-byte event descriptor in the `xcp_evts` section | — | — |
| `XCP_UNDEFINED_CALSEG` | const | Invalid segment index (0xFFFF) | — | hello_xcp, c_demo, cmp_demo, multi_thread_demo, udp_raw_demo, freertos_demo |
| `XCP_UNDEFINED_CALSEG_NUM` | const | Invalid segment number (0xFF) | — | — |
| `XCP_UNDEFINED_EVENT_ID` | const | Invalid event id (0xFFFF) | — | multi_thread_demo |
| `XCP_CALSEG_TYPE_SEGMENT`, `XCP_CALSEG_TYPE_BLOCK` | const | Descriptor type tags | — | — |
| `CRC_CMD_OK`, `CRC_ACCESS_DENIED`, … (24 values) | const | XCP command return codes, for callback return values | — | c_demo, external_example |

### 1.16 Internal building blocks

These macros are public because the instrumentation macros expand to them. Applications normally don't use them directly: `XCP_CAL_SECTION_ATTR`, `XCP_EVENT_SECTION_ATTR`, `XCP_EPK_SECTION_ATTR`, `XCP_METADATA_SECTION_ATTR`, `XCP_EVENT_DESCRIPTOR_INIT`, `XCP_EVENT_SECTION_GET_LINKTIME_ID`, `XCP_EVENT_SECTION_SET_ID`, `XCP_EVENT_SECTION_LOOKUP_ID`, `XCP_CALSEG_DECL_CHECK`, `XCP_CAPTURE`, `XCP_CAP_*`, `XCPLIB_FOR_EACH_MEAS_*`, `XCPLIB_FE_*`, `XCPLIB_APPLY_`, `A2L_UNPACK_AND_REG_*`, `A2L_TUPLE_SIZE_*`. With section registration, `__start_xcp_evts`/`__stop_xcp_evts` are the linker-generated bounds of the event section.

---

## Part 2: C A2L generation API ([a2l.h](../inc/a2l.h))

Everything in this part requires **`OPTION_ENABLE_A2L_GENERATOR`** (configurations default, ptp, shm and raw). The **Option** column lists only additional requirements. The A2L generator state (addressing mode, group, typedef) is not thread safe: protect registrations with `A2lLock`/`A2lUnlock` or a once-pattern.

### 2.1 Initialization and file

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `A2lInit(addr, port, use_tcp, mode)` | function | Start A2L generation, or reuse an existing A2L file that matches the EPK | — | all default-config examples, silkit_demo, external/fetchcontent |
| `A2lFinalize()` | function | Write the A2L file and the persistence file | — | all default-config examples, silkit_demo, external/fetchcontent |
| `A2lGetFilename()` | function | Name of the A2L file | — | — |
| `A2lGetAppFilename(project, epk)` | function | File name of an application's partial A2L file | `OPTION_SHM_MODE` | — |
| `A2L_MODE_WRITE_ALWAYS` | const | Always regenerate the A2L file | — | multi_thread_demo, ptp4l_demo, bpf_demo, external_example, fetchcontent_example |
| `A2L_MODE_WRITE_ONCE` | const | Generate once per EPK. Use with `XCP_MODE_PERSISTENCE` | `OPTION_ENABLE_PERSISTENCE` | hello_xcp, hello_xcp_cpp, c_demo, cpp_demo, struct_demo, point_cloud_demo, cmp_demo, udp_raw_demo, silkit_demo, … |
| `A2L_MODE_FINALIZE_ON_CONNECT` | const | Finalize automatically when a client connects | — | almost all examples |
| `A2L_MODE_AUTO_GROUPS` | const | Create groups per event and per segment automatically | — | almost all examples |
| `A2L_MODE_EVENT_CONVERSION` | const | Create an enum conversion with all event ids | — | multi_thread_demo |
| `A2L_MODE_SYMBOL_PREFIX` | const | Prefix all symbols with the project name (always set in SHM mode) | — | — |
| `A2L_MODE_WRITE_TEMPLATE` | const | Write only a template: settings, IF_DATA, events, segments | — | — |
| `A2L_MODE_EMBED_AML_FILE` | const | Reference the AML file with `/include` instead of embedding it | — | — |
| `A2L_MODE_DEACTIVATE` | const | No A2L generation | — | — |
| `XCP_A2L_MAX_SYMBOL_NAME_LENGTH`, `XCP_A2L_MAX_LINE_LENGTH`, `XCP_A2L_MAX_COMMENT_LENGTH` | const | Size limits | — | — |

### 2.2 Addressing modes

Set the addressing mode before the registrations it applies to. It stays active until the next `A2lSet*AddrMode`. Each macro has an `_s` variant (event or segment name as a string) and an `_i` variant (id or index). See [TECHNICAL.md](TECHNICAL.md) for the address extension mapping.

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `A2lSetSegmentAddrMode(index, instance)`, `_s` | macro | Parameters relative to a calibration segment | `OPTION_CAL_SEGMENTS` | hello_xcp, c_demo, cmp_demo, multi_thread_demo, udp_raw_demo |
| `A2lSetAbsoluteAddrMode(event)`, `_s`, `_i` | macro | Globals and static variables at absolute addresses | — | hello_xcp, c_demo, cpp_demo, struct_demo, cmp_demo, bpf_demo, udp_raw_demo, fetchcontent_example |
| `A2lSetStackAddrMode(event)` | macro | Locals relative to the stack frame of the event trigger | — | hello_xcp, c_demo, cpp_demo, struct_demo, cmp_demo, udp_raw_demo |
| `A2lSetStackAddrMode_s` / `_i` | macro | As above, by name string or id | — | cpp_demo / multi_thread_demo |
| `A2lSetRelativeAddrMode(event, base)` | macro | Heap or pointer-relative variables (address extension 3) | — | c_demo, struct_demo, silkit_demo |
| `A2lSetRelativeAddrMode_s` / `_i` | macro | As above, by name string or id | — | cpp_demo / multi_thread_demo |
| `A2lSetAutomaticAddrMode(event, base)`, `_s`, `_i` | macro | Choose stack or relative addressing for each variable | — | — (used by the C `DaqEventVar`) |
| `A2lSetAutoAddrMode(id, frame, base)` | function | Function form of the automatic mode | — | — |
| `A2lSetApplicationAddrMode()` | function | Variables accessed through the read/write callbacks | `!OPTION_CAL_SEGMENTS_ABS` | c_demo, external_example |

### 2.3 Calibration parameters

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `A2lCreateParameter(name, comment, unit, min, max)` | macro | Scalar parameter | — | hello_xcp, c_demo, cmp_demo, multi_thread_demo, udp_raw_demo, external_example, fetchcontent_example |
| `A2lCreateCurve(name, xdim, …)` | macro | 1D curve | — | — |
| `A2lCreateCurveWithSharedAxis(…, x_axis)` | macro | 1D curve with a shared axis | — | — |
| `A2lCreateMap(name, xdim, ydim, …)` | macro | 2D map | — | — |
| `A2lCreateMapWithSharedAxis(…, x_axis, y_axis)` | macro | 2D map with shared axes | — | — |
| `A2lCreateAxis(name, xdim, …)` | macro | Standalone shared axis | — | — |
| `A2lSetInputQuantity_x(name)` / `_y(name)` | function | Input quantity of the following curve or map axes | — | — |

### 2.4 Conversions

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `A2lCreateLinearConversion(name, comment, unit, factor, offset)` | macro | Linear conversion, referenced as `"conv.<name>"` | — | hello_xcp, cpp_demo |
| `A2lCreateEnumConversion(name, description)` | macro | Value table conversion, referenced as `"conv.<name>"` | — | hello_xcp_cpp, cpp_demo |

### 2.5 Measurements

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `A2lCreateMeasurement(name, comment)` | macro | Scalar | — | hello_xcp, c_demo, cpp_demo, struct_demo, multi_thread_demo, cmp_demo, bpf_demo, udp_raw_demo, silkit_demo, external_example, fetchcontent_example |
| `A2lCreatePhysMeasurement(name, comment, unit_or_conv, min, max)` | macro | Scalar with unit or conversion and limits | — | hello_xcp, cpp_demo, cmp_demo, bpf_demo, udp_raw_demo |
| `A2lCreateMeasurementArray(name, comment)` | macro | 1D array | — | c_demo, cpp_demo |
| `A2lCreatePhysMeasurementArray(…)` | macro | 1D array with a unit or conversion and limits | — | — |
| `A2lCreateMeasurementMatrix(name, comment)` | macro | 2D array | — | c_demo |
| `A2lCreatePhysMeasurementMatrix(…)` | macro | 2D array with a unit | — | — |
| `A2lCreateMeasurementInstance(inst, name, comment)` | macro | Scalar with an instance name prefix | — | multi_thread_demo |
| `A2lCreatePhysMeasurementInstance(inst, name, …)` | macro | As above, with a unit | — | hello_xcp, cpp_demo, multi_thread_demo |
| `A2lCreateMeasurementArrayInstance(inst, name, comment)` | macro | 1D array with an instance name prefix | — | multi_thread_demo |
| `A2lCreateMeasurementMatrixInstance(inst, name, comment)` | macro | 2D array with an instance name prefix | — | — |

### 2.6 Typedefs and instances

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `A2lTypedefBegin(type, instance_ptr, comment)` … `A2lTypedefEnd()` | macro | Bracket for a struct typedef definition | — | c_demo, cpp_demo, struct_demo, multi_thread_demo, point_cloud_demo, silkit_demo |
| `A2lTypedefComponent(field, type_name, dim)` | macro | Field that is itself a typedef | — | cpp_demo, struct_demo |
| `A2lTypedefMeasurementComponent(field, comment)` | macro | Scalar measurement field | — | c_demo, struct_demo, multi_thread_demo |
| `A2lTypedefMeasurementArrayComponent(field, comment)` | macro | Array measurement field | — | struct_demo |
| `A2lTypedefPhysMeasurementComponent(field, comment, unit, min, max)` | macro | Scalar measurement field with a unit | — | multi_thread_demo |
| `A2lTypedefPhysMeasurementArrayComponent(…)` | macro | Array measurement field with a unit | — | — |
| `A2lTypedefParameterComponent(field, comment, unit, min, max)` | macro | Scalar parameter field | — | c_demo, cpp_demo, point_cloud_demo, silkit_demo |
| `A2lTypedefCurveComponent(field, x_dim, …)` | macro | Curve field | — | c_demo, cpp_demo |
| `A2lTypedefCurveComponentWithSharedAxis(…, x_axis)` | macro | Curve field with a shared axis | — | c_demo, cpp_demo |
| `A2lTypedefMapComponent(field, x_dim, y_dim, …)` | macro | Map field | — | c_demo |
| `A2lTypedefMapComponentWithSharedAxis(…, x_axis, y_axis)` | macro | Map field with shared axes | — | — |
| `A2lTypedefAxisComponent(field, x_dim, …)` | macro | Axis field | — | c_demo, cpp_demo |
| `A2lCreateTypedefInstance(name, type, comment)` | macro | Instance of a typedef | — | c_demo, struct_demo, silkit_demo |
| `A2lCreateTypedefNamedInstance(name_str, var, type, comment)` | macro | Instance with an A2L name different from the variable name | — | multi_thread_demo |
| `A2lCreateTypedefArray` / `A2lCreateTypedefInstanceArray(name, type, dim, comment)` | macro | Array of typedef instances | — | struct_demo |
| `A2lCreateTypedefReference` / `A2lCreateTypedefInstanceReference(ptr, type, comment)` | macro | Instance behind a pointer | — | struct_demo |
| `A2lCreateInstance(name_str, type, dim, ptr, comment)` | macro | Generic instance | — | — |

### 2.7 Groups

Use these functions when `A2L_MODE_AUTO_GROUPS` is not set.

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `A2lBeginGroup(name, comment, is_param, is_root)` / `A2lAddToGroup(name)` / `A2lEndGroup()` | function | Build a group step by step | — | — |
| `A2lCreateParameterGroup(name, count, ...)` / `…FromList(name, names, count)` | function | Parameter group | — | — |
| `A2lCreateMeasurementGroup(name, count, ...)` / `…FromList(name, names, count)` | function | Measurement group | — | — |

### 2.8 Thread safety and once-patterns

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `A2lLock()` / `A2lUnlock()` | function | Mutex around a block of registrations | — | cpp_demo, multi_thread_demo |
| `A2lOnce(name)` | macro | **C only.** Run the following block once (atomic CAS, wait-free). The C++ version is in Part 4 | — | hello_xcp, cmp_demo, multi_thread_demo, udp_raw_demo |
| `A2lThreadOnce(name)` | macro | **C only.** Run the following block once per thread | — | — |
| `A2lOnce_(flag)` | function | Helper used by `A2lOnce` | — | — |
| `A2L_ONCE_TYPE`, `A2L_ONCE_ATOMIC_TYPE` | macro | Once flag types | — | — |

### 2.9 Type detection

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `tA2lTypeId` | type | A2L basic type id (`int8_t`, sign = signedness, value = size) | — | — |
| `A2L_TYPE_UINT8` … `A2L_TYPE_DOUBLE`, `A2L_TYPE_UNDEFINED` | const | Type ids | — | bpf_demo |
| `A2lGetTypeId(expr)` | macro | Type id of an expression (C: `_Generic`, C++: template) | — | — (used by all registration macros) |
| `A2lGetArray1DElementTypeId(a)` / `A2lGetArray2DElementTypeId(a)` | macro | Element type id of an array | — | — |
| `A2lGetTypeIdFromPtr(ptr)` | macro | **C only.** Type id from a pointer | — | — |
| `A2lGetTypeName[1D\|2D][_M\|_C](x)` | macro | A2L type name strings | — | — |
| `A2lGetRecordLayoutName[1D\|2D](x)` | macro | A2L record layout name | — | — |
| `A2lGetA2lTypeName`, `A2lGetA2lTypeName_M`, `A2lGetA2lTypeName_C`, `A2lGetA2lRecordLayoutName` | function | Name lookup by type id | — | — |
| `A2lGetTypeMin(id)` / `A2lGetTypeMax(id)` | function | Value range of a type | — | — |

### 2.10 Low-level functions

The registration macros expand to these functions. Call them directly only for names, types or addresses that are known only at runtime: `A2lSetSegmentAddrMode__i/__s`, `A2lSetAbsoluteAddrMode__i/__s`, `A2lSetStackAddrMode__i/__s`, `A2lSetRelativeAddrMode__i/__s`, `A2lSetAutoAddrMode__i/__s`, `A2lCreateParameter_`, `A2lCreateCurve_`, `A2lCreateMap_`, `A2lCreateAxis_`, `A2lCreateLinearConversion_`, `A2lCreateEnumConversion_`, `A2lCreateMeasurement_` (used directly in bpf_demo), `A2lCreateMeasurementArray_`, `A2lTypedefBegin_`, `A2lTypedefEnd_`, `A2lTypedefComponent_`, `A2lTypedefMeasurementComponent_`, `A2lTypedefParameterComponent_`, `A2lCreateInstance_`.

`A2lSetSymbolPrefix(enable)` switches the project name prefix (`A2L_MODE_SYMBOL_PREFIX`) on or off for the symbols created after the call. Call it after `A2lInit`. In SHM mode the prefix is mandatory and cannot be switched off.

---

## Part 3: C++ API ([xcplib.hpp](../inc/xcplib.hpp))

Namespace `xcp`. In C++, the C calibration segment macros of section 1.5 are replaced by the classes and macros below. Everything in Part 1 except the **C only** entries is available as well.

### 3.1 Calibration segments

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `xcp::CalSeg<T>` | class template | Owning RAII wrapper: the constructor `(name, &default)` creates the segment, `lock()` returns a guard, `getIndex()` the index | `OPTION_CAL_SEGMENTS` | hello_xcp_cpp, cpp_demo, point_cloud_demo, silkit_demo |
| `xcp::CalBlk<T>` | class template | As above, for a calibration block | `OPTION_CAL_SEGMENTS` | — |
| `xcp::CalSegRef<T>` | class template | Non-owning typed handle over an index variable, for section registered segments | `OPTION_CAL_SEGMENTS` | no_a2l_demo_cpp, freertos_demo |
| `CalSeg<T>::CalSegGuard` (also in `CalBlk`, `CalSegRef`) | nested class | Lock guard with `->`, `*` and `get()`. Not copyable (the `CalSegRef` guard is movable). Unlocks in its destructor. In passive mode it reads the default page | — | via `lock()` in the examples above |
| `CalSeg<T>::CreateA2lTypedefInstance(type, comment)` (also `CalBlk`, `CalSegRef`) | method | Register the A2L instance of the segment | `OPTION_ENABLE_A2L_GENERATOR` | hello_xcp_cpp, cpp_demo, point_cloud_demo, silkit_demo |
| `CalSegCreate(value)` | macro | Expression: `auto seg = CalSegCreate(params);` returns `xcp::CalSeg<T>` and emits the `xcp_cals` descriptor | `OPTION_CAL_SEGMENTS` | cpp_demo, freertos_demo |
| `CalBlkCreate(value)` | macro | As above, returns `xcp::CalBlk<T>` | `OPTION_CAL_SEGMENTS` | — |
| `CalSegDeclRef(value, handle)` | macro | File scope declaration plus a typed handle `xcp::CalSegRef<T> handle`. `XcpInit` creates the segment | `OPTION_SECTION_REGISTRATION` | no_a2l_demo_cpp, freertos_demo |
| `CalSegDecl(value)` | macro | `CalSegDeclRef(value, value_calseg)` | `OPTION_SECTION_REGISTRATION` | no_a2l_demo_cpp |

### 3.2 Variadic DAQ templates

The whole section requires **`!OPTION_SECTION_REGISTRATION`**. Measurements use individual relative address extensions (AASDD).

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `DaqEventVar(name, A2L_MEAS(...), ...)` | macro | Create the event, register the measurements once and trigger | registration needs `OPTION_ENABLE_A2L_GENERATOR` | hello_xcp_cpp, cpp_demo, ptp4l_demo, silkit_demo |
| `DaqEventAtVar(name, clock, ...)` | macro | As above, with a timestamp | as above | point_cloud_demo |
| `DaqTriggerEventVar(name, bases...)` | macro | Create the event once and trigger with a variadic list of base addresses | `OPTION_DAQ_EVENT_LIST` | — |
| `xcp::DaqEventVarTemplate(id, clock, measurements...)` | function template | Implementation of `DaqEventVar` | — | — |
| `xcp::DaqTriggerVarTemplate(name, bases...)` | function template | Implementation of `DaqTriggerEventVar` | `OPTION_DAQ_EVENT_LIST` | — |
| `A2L_MEAS(var, comment)` | macro | Measurement info for `DaqEventVar` | `OPTION_ENABLE_A2L_GENERATOR` | hello_xcp_cpp, cpp_demo, ptp4l_demo, point_cloud_demo, silkit_demo |
| `A2L_MEAS_PHYS(var, comment, unit, min, max)` | macro | As above, with a unit and limits | `OPTION_ENABLE_A2L_GENERATOR` | hello_xcp_cpp, cpp_demo, silkit_demo |
| `A2L_MEAS_ARRAY(var, comment)` / `A2L_MEAS_ARRAY_PHYS(…)` | macro | Array measurement | `OPTION_ENABLE_A2L_GENERATOR` | cpp_demo / — |
| `A2L_MEAS_INST(var, type, comment)` / `A2L_MEAS_INST_ARRAY(…)` | macro | Typedef instance (array) | `OPTION_ENABLE_A2L_GENERATOR` | hello_xcp_cpp, silkit_demo / point_cloud_demo |
| `A2L_MEAS_INST_PTR(var, ptr, type, comment)` / `A2L_MEAS_INST_ARRAY_PTR(…)` | macro | Typedef instance behind a pointer | `OPTION_ENABLE_A2L_GENERATOR` | hello_xcp_cpp / — |
| `xcp::MeasurementInfo<T>`, `xcp::InstanceInfo<T>` | struct template | Descriptors created by the `A2L_MEAS*` macros | `OPTION_ENABLE_A2L_GENERATOR` | — |
| `xcp::registerDynMeasurement(index, event, info)` | function template | Register one measurement with a relative address extension | `OPTION_ENABLE_A2L_GENERATOR` | — |
| `XCPLIB_ALWAYS_INLINE` | macro | Force inlining, so that the templates capture the caller's stack frame | — | — |

---

## Part 4: C++ A2L generation API ([a2l.hpp](../inc/a2l.hpp))

Namespace `xcp::a2l`. Everything requires **`OPTION_ENABLE_A2L_GENERATOR`**. The C macros of Part 2 can be used from C++ as well, except `A2lOnce(name)`, `A2lThreadOnce(name)` and `A2lGetTypeIdFromPtr`.

### 4.1 Once-patterns

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `A2lOnce()` | macro | `if (A2lOnce()) {…}` runs once across all threads (`std::call_once`, unique for each source line) | — | cpp_demo, point_cloud_demo |
| `A2lThreadOnce()` | macro | Runs once per thread | — | — |
| `xcp::a2l::A2lOnceGuard<WithMutex, PerThread, Location>` | class template | Implementation of both macros | — | — |

### 4.2 Variadic typedef creation

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `A2lCreateTypedef(Type, comment, components...)` | macro | Create the typedef for a struct, once and thread safe | — | hello_xcp_cpp, point_cloud_demo, silkit_demo |
| `A2L_PARAMETER_COMPONENT(field, comment, unit, min, max)` | macro | Scalar parameter field | — | hello_xcp_cpp |
| `A2L_CURVE_COMPONENT(field, …)` | macro | Curve field, dimension detected from the type | — | hello_xcp_cpp |
| `A2L_CURVE_WITH_AXIS_COMPONENT(field, …, axis)` | macro | Curve field with a shared axis | — | — |
| `A2L_MAP_COMPONENT(field, …)` | macro | Map field, dimensions detected from the type | — | hello_xcp_cpp |
| `A2L_MAP_WITH_AXIS_COMPONENT(field, …, x_axis, y_axis)` | macro | Map field with shared axes | — | — |
| `A2L_AXIS_COMPONENT(field, …)` | macro | Axis field | — | hello_xcp_cpp |
| `A2L_MEASUREMENT_COMPONENT(field, comment, unit)` | macro | Scalar measurement field | — | hello_xcp_cpp, point_cloud_demo, silkit_demo |
| `A2L_MEASUREMENT_ARRAY_COMPONENT(field, comment, unit)` | macro | Array measurement field | — | hello_xcp_cpp |
| `A2L_TYPEDEF_COMPONENT(field, type_name, dim)` | macro | Nested typedef field | — | point_cloud_demo |
| `A2lParameterComponentInfo<T>`, `A2lMeasurementComponentInfo<T>`, `A2lTypedefComponentInfo<T>` | struct template | Component descriptors | — | — |
| `A2lCreateTypedefTemplate<T>(…)`, `A2lCreateTypedefComponentTemplate(info)` | function template | Implementation of `A2lCreateTypedef` | — | — |

### 4.3 Type detection (defined in `a2l.h` for C++)

| Name | Kind | Description | Option | Examples |
|---|---|---|---|---|
| `xcp::a2l::TypeId<T>` | struct template | Maps a C++ type to `tA2lTypeId` | — | — |
| `xcp::a2l::RemoveCVRef<T>` | struct template | Removes cv-qualifiers and references | — | — |
| `xcp::a2l::GetTypeId<T>()` | constexpr function | Type id of a type | — | — |
| `xcp::a2l::GetTypeIdFromExpr(expr)` | constexpr function | Type id of an expression, used by `A2lGetTypeId` in C++ | — | — |
