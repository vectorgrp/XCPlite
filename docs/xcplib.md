# XCPlite API Reference Guide

**Version**: 1.0.0

## Table of Contents

1. [Overview](#1-overview)
2. [Getting Started](#2-getting-started)
3. [API Reference](#3-api-reference)
4. [Example Application](#4-example-application)
5. [A2l Creator](#5-a2l-creator)
6. [Glossary](#6-glossary)

---

## 1 · Overview

This guide documents the C API exposed by **xcplib** and demonstrates its integration in a host application.\
All functions are *C linkage* and can therefore be consumed directly from C/C++, or via FFI from Rust and other languages.

Key features:

- Single‑instance XCP server (TCP or UDP)
- Lock‑free calibration parameter segments
- Timestamped measurement events to capture global, stack and heap data
- Optional A2L file meta data and type generation at runtime

---

## 2 · Getting Started

1. **Include headers**

```c
#include "xcplib.h"
#include "a2l.h" // for A2l generation
```

2. **Set log level (optional)**

```c
   XcpSetLogLevel(3); // (1=error, 2=warning, 3=info, 4=debug (XCP commands), 5=trace)
```

3. **Initialise the XCP core** *once*:

```c
   XcpInit("MyProject", "V1.0.1",true);
```

4. **Start the Ethernet server** (TCP or UDP, IP addr, port):

```c
   const uint8_t addr[4] = {0,0,0,0};
   XcpEthServerInit(addr, 5555, false /* UDP */, 32*1024 /* DAQ queue size */);
```

6. **Create calibration segments and events** as required (see sections 3.2 & 3.3).  

7. **Add DAQ triggers**, trigger DAQ events with the `DaqTriggerEvent`/`DaqTriggerEventExt` macros.  

8. On shutdown, call `XcpEthServerShutdown()`.  

---

## 3 · API Reference

### Initialization

#### void XcpInit(const char *project_name, const char*epk, bool activate)

*Initialize XCP*

XCP must be initialized always, but it may be initialized in inactive mode.  
Calling other XCP API functions without prior initialization may create undefined behaviour.  
In inactive mode, all XCP and A2L code instrumentation remains passive, disabled with minimal runtime overhead.  

### 3.1 · XCP on Ethernet Server

#### bool XcpEthServerInit(uint8_t *address, uint16_t port, bool use_tcp, uint32_t measurement\_queue_size)

*Initialise the XCP server singleton.*

- **Preconditions**: `XcpInit()` has been called; only one server instance may be active.
- **Parameters**
  - `address` – IPv4 address to bind to (`0.0.0.0` = any).
  - `port` – Port number.
  - `use_tcp` – `true` → TCP, `false` → UDP.
  - `measurement_queue_size` – Queue size in bytes (multiple of 8; includes header + alignment).
- **Returns**: `true` on success, otherwise `false`.

#### bool XcpEthServerShutdown(void)

Stop the running server and free internal resources.

#### bool XcpEthServerStatus(void)

*Get server status*

Query whether the server instance is currently running.

#### void XcpEthServerGetInfo(bool \*out\_is\_tcp, uint8\_t \*out\_mac, uint8\_t \*out\_address, uint16\_t \*out\_port)

Retrieve run‑time information about the active server.\
All out‑parameters are *optional* and may be passed as `NULL`.

---

### 3.2 Calibration Segments

See function and macro documentation in xcplib.h

---

### 3.3 Events

See function and macro documentation in xcplib.h

---

### 3.4 A2L Generation

#### bool A2lInit(const uint8_t *address, uint16_t port, bool use_tcp, uint32_t mode_flags)

Initializes the A2L generation system of XCPlite. This function must be called once before any A2L-related macros or API functions are used. It performs the following actions:

- Allocates and initializes all internal data structures and files required for A2L file creation.
- Enables runtime definition of parameters, measurements, type definitions, groups and conversion rules.

**Parameters:**

- `project_name`: Name of the project, used for the A2L and BIN file names.
- `epk`: Unique software version string (EPK) for version checking of A2L and parameter files.
- `address`: Default IPv4 address of the XCP server.
- `port`: Port number of the XCP server.
- `use_tcp`: If `true`, TCP transport is used; if `false`, UDP transport.
- `mode_flags`: Bitwise combination of A2L generation mode flags controlling file creation and runtime behavior:
  - `A2L_MODE_WRITE_ALWAYS` (0x01): Always write the A2L file, overwriting any existing file.
  - `A2L_MODE_WRITE_ONCE` (0x02): Write the A2L file (.a2l) once after a rebuild; Uses the binary persistence file (.bin) to keep calibration segment and event numbers stable, even if the registration order changes.
  - `A2L_MODE_FINALIZE_ON_CONNECT` (0x04): Finalize the A2L file when an XCP client connects, later registrations are not visible to the tool. If not set, the A2L file must be finalized manually.
  - `A2L_MODE_AUTO_GROUPS` (0x08): Automatically create groups for measurement events and parameter segments in the A2L file.

#### void A2lFinalize(void)

Finalizes and writes the A2L file to disk. This function should be called when all measurements, parameters, events, and calibration segments have been registered and no further A2L definitions are expected. After finalization, the A2L file becomes visible to XCP tools and cannot be modified further during runtime.

#### void A2lSetXxxAddrMode(...)

XCPlite uses relative memory addressing. There are 4 different addressing modes. When an addressing mode is set, it is valid for all subsequent definitions of parameters and measurement variables.

| Macro                   | Description                                                                                  |
|------------------------ |---------------------------------------------------------------------------------------------|
| `A2lSetSegmentAddrMode` | Sets segment-relative addressing mode for calibration parameters in a specific segment.      |
| `A2lSetAbsoluteAddrMode`| Sets absolute addressing mode for variables in global memory space.                         |
| `A2lSetStackAddrMode`   | Sets stack-relative addressing mode for variables on the stack (relative to frame pointer). |
| `A2lSetRelativeAddrMode`| Sets relative addressing mode for variables relative to a base address (e.g., heap objects or class instances).|

#### void A2lCreateXxxx(...)

All A2L generation macros and functions are not thread safe. It is up to the user to ensure thread safety and to use once-patterns when definitions are called multiple times in nested functions or from different threads.
The functions `A2lLock()` and `A2lUnlock()` may be used to lock sequences of A2L definitions. The macro `A2lOnce` may be used to create a once execution pattern for a block of A2L definitions.

Also note that A2L definitions may be lazy, but the A2L file is finalized when an XCP tool connects (or when `A2lFinalize()` is called). All definitions after that point are ignored and not visible to the tool.

All definitions of instances follow the same principle: Set the addressing mode first. The addressing mode is valid for all following definitions. The examples in the `examples/` folder show various ways how to create A2L artifacts.

---

### 3.5 Miscellaneous Functions

| Function                                                         | Purpose                                                         |
| ---------------------------------------------------------------- | --------------------------------------------------------------- |
| `void XcpSetLogLevel(uint8_t level);`                            | 1 = error, 2 = warn, 3 = info, 4 = commands, 5 = trace.         |
| `void XcpInit(const char *name, const char *epk, bool activate);`| Initialize core singleton; must precede all other API usage.    |
| `void XcpDisconnect(void);`                                      | Force client disconnect, stop DAQ, flush pending operations.    |
| `void XcpSendTerminateSessionEvent(void);`                       | Notify client of a terminated session.                          |
| `void XcpPrint(const char *str);`                                | Send arbitrary text to the client (channel 0xFF).               |
| `uint64_t ApplXcpGetClock64(void);`                              | Retrieve 64‑bit DAQ timestamp.                                  |

---

## 4 · Example Applications

See examples folder and README.md for a short descriptions of the example applications.

## 5 · Appendix

### Static Markers

The code instrumentations creates static variables, to help the A2L Creater or an XCP tool reading linker map files, to identify calibration segments, events, capture buffers and the scope where an event is triggered.

```c
//Create calibration segment macro segment index once pattern
static tXcpCalSegIndex cal__##name;

// Create measurement event macro event id once pattern
static THREAD_LOCAL tXcpEventId evt__##name
static THREAD_LOCAL tXcpEventId evt__

// Daq capture macro capture buffer
static __typeof__(var) daq__##event##__##var

// Daq event macro event id once pattern
static THREAD_LOCAL tXcpEventId evt___aas0__##name
static THREAD_LOCAL tXcpEventId evt___aasr__##name
static THREAD_LOCAL tXcpEventId evt___aasrr__##name
```

*End of document.*
