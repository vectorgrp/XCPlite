# XCPlite API Reference Guide

**Version**: 0.9.2

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
- Lock‑free calibration segments
- Measurement events to capture global, stack and heap data
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
   XcpSetLogLevel(5); // (1=error, 2=warning, 3=info, 4=debug, 5=trace):
```

3. **Initialise the XCP core** *once*:

```c
   XcpInit(true);
```

4. **Start the Ethernet server** (TCP or UDP, IP addr, port):

```c
   const uint8_t addr[4] = {0,0,0,0};
   XcpEthServerInit(addr, 5555, false /* UDP */, 32*1024 /* DAQ queue size */);
```

6. **Create calibration segments and events** as required (see sections 3.2 & 3.3).  

7. **Add DAQ triggers**, trigger DAQ events with the `DaqEvent`/`DaqEventRelative` macros.  

8. On shutdown, call `XcpEthServerShutdown()`.  

---

## 3 · API Reference

### 3.0 · Initialisation

#### void XcpInit( bool activate)

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

### 3.3 Events

See function and macro documentation in xcplib.h

### 3.5 Miscellaneous Functions

| Function                                                         | Purpose                                                         |
| ---------------------------------------------------------------- | --------------------------------------------------------------- |
| `void XcpSetLogLevel(uint8_t level);`                            | 1 = error, 2 = warn, 3 = info, 4 = commands, 5 = trace.         |
| `void XcpInit(void);`                                            | Initialise core singleton; must precede all other API usage.    |
| `void ApplXcpSetA2lName(const char *name);`                      | Manually set the A2L file name for upload.                      |
| `void XcpSetEpk(const char *epk);`                               | Set 32‑byte EPK software identifier (for A2L).                  |
| `void XcpDisconnect(void);`                                      | Force client disconnect, stop DAQ, flush pending operations.    |
| `void XcpSendTerminateSessionEvent(void);`                       | Notify client of a terminated session.                          |
| `void XcpPrint(const char *str);`                                | Send arbitrary text to the client (channel 0xFF).               |
| `uint64_t ApplXcpGetClock64(void);`                              | Retrieve 64‑bit DAQ timestamp.                                  |

---

## 4 · Example Applications

See examples folder and README.md for a short descriptions of the example applications.

## 5 · A2L Creator

All A2l generation macros and functions are not thread safe. It is up to the user to take care for thread safety, as well as for once execution, when definitions are called multiple times in nested functions or from different threads.  
The functions A2lLock() and A2lUnlock() may be used to lock sequences of A2L definitions.  
The macro A2lOnce may be used to create a once execution pattern for a block of A2L definitions.  

Also note that A2L definitions may be lazy, but the A2L file is finalized when a XCP tool connects. All definitions after that point are ignored and not visible to the tool.  

All definitions of instances follow the sample principle: Set the addressing mode first. The addressing mode is valid for all following definitions.  
The examples in the examples/folder show various way how to create A2L artifacts.  

## 6 · Glossary


- **A2L** – ASAM MCD‑2 MC description file (measurement & calibration meta‑data).
- **DAQ** – Data Acquisition (periodic or sporadic transmit of ECU variables).
- **EPK** – Embedded Program Identifier (software version string embedded in the ECU).
- **ECU** – Electronic Control Unit (target device exposing the XCP protocol).

---

*End of document.*
