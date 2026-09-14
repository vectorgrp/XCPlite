# no_a2l_demo - XCPlite example

Demonstrates XCPlite usage without runtime on-target A2L database generation.

libxcplite can be built without support for A2L generation and persistency, reducing code size
and file system dependencies for microcontroller / RTOS targets (FreeRTOS, Zephyr, ThreadX, ...).
The configuration is applied via `xcplib_no_a2l_cfg.h` using the `XCPLIB_CFG_OVERRIDE` mechanism —
see the [Library Configuration Override](#library-configuration-override) section below.

The A2L database is instead generated offline by a tool.  
The `xcpclient` test tool, which is part of this repository under `tools/xcpclient/`, includes an XCPlite-specific ELF->A2L generator that reads the ELF file and DWARF debug information to create a complete, plug&play A2L database for the application.  


## Offline A2L generation

The concept, the workflow, the rules for the application code, the naming of types and variables, the supported types and the
diagnostics of the ELF/DWARF to A2L generator are described in [docs/OFFLINE_A2L.md](../../docs/OFFLINE_A2L.md). In short:

- Use the instrumentation macros, not the raw C API, only the macros emit the ELF markers.
- Build with debug information (`-g`, `Debug` or `RelWithDebInfo`).
- Mark local measurement variables `volatile`, so that they stay on the stack frame in optimized builds.
- Declare calibration segments with `CalSegDecl` or `CalSegDeclRef`, the default page needs static storage duration.

## Library Configuration Override

The default configuration options for xcplite are set in `src/xcplib_cfg.h`.
All defaults are defined there; nothing needs to be edited for a `no_a2l` build.

For this use case a dedicated override file `src/xcplib_no_a2l_cfg.h` adjusts only the settings that
differ from the defaults:

```c
// src/xcplib_no_a2l_cfg.h — lean override, only differences from xcplib_cfg.h defaults
#undef  OPTION_ENABLE_PERSISTENCE       // no file system needed on bare-metal / RTOS targets
#define OPTION_CAL_SEGMENTS_ABS         // absolute addressing has address extension 0, calibration segments may be accessed with the absolute address of their static default page
#undef  OPTION_CAL_SEGMENT_EPK          // no EPK segment
#define OPTION_QUEUE_32                 // 32-bit queue
#undef  OPTION_QUEUE_64_VAR_SIZE
#undef  OPTION_QUEUE_64_FIX_SIZE
#undef  OPTION_ENABLE_A2L_GENERATOR     // no on-target A2L generation
#undef  OPTION_ENABLE_A2L_UPLOAD        // no A2L upload via XCP
#undef OPTION_ENABLE_ELF_UPLOAD         // no ELF upload via XCP, A2L creator works with ELF file on disk as well
#define OPTION_DAQ_ASYNC_EVENT          // Create event 'async' with id 0 as default event for global variables

```

The override is applied at the end of `xcplib_cfg.h` via:

```c
#ifdef XCPLIB_CFG_OVERRIDE
#include XCPLIB_CFG_OVERRIDE
#endif
```

Pass the override file to xcplite at compile time:

```cmake
# CMakeLists.txt
target_compile_definitions(xcplite PRIVATE "XCPLIB_CFG_OVERRIDE=\"xcplib_no_a2l_cfg.h\"")
```

This is already set when building with `XCPLITE_CONFIGURATION=no_a2l`.
The same pattern can be used to create any other application-specific configuration override.

> **Note:** The no_a2l_demo must be built in isolation because the override disables the A2L generator that other examples depend on. A dedicated build directory `build-no_a2l` avoids cache conflicts.

---

## API subset for no-A2L workflows

The following API subset remains fully available and is unchanged:

### Server

| Function | Purpose |
|---|---|
| `XcpInit(name, epk, mode)` | Initialize the XCP core |
| `XcpEthServerInit(addr, port, tcp, queue_size)` | Start the XCP/Ethernet server |
| `XcpEthServerShutdown()` | Stop the server |
| `XcpSetLogLevel(level)` | Set log verbosity |

### Events and measurement triggers (`xcplib.h`)

| Macro | Emits to ELF | Purpose |
|---|---|---|
| `DaqCreateEvent(name)` | `xcp_evts` | Register a named DAQ event; emits `tXcpEventDescriptor` |
| `DaqCreateEventExt(name)` | `xcp_evts` | Register a named DAQ event with cycle time and priority; emits `tXcpEventDescriptor` |
| `DaqCreateAndTriggerEvent(name)` | `xcp_evts` + DWARF anchor | Declare and trigger in one step — ideal for short-lived functions |
| `DaqTriggerEvent(name)`, `DaqTriggerEventAt` | DWARF anchor (`trg__AAS__`) | Trigger and anchor local variable scope for xcpclient |
| `DaqTriggerEventExt(name, base)`, `DaqTriggerEventExt_s(name, base)` | DWARF anchor (`trg__AASD__`) | Trigger with explicit base pointer (heap / relative addressing) |
| `DaqEventVar(event_name, ...)` | — | Declare a variadic DAQ event |
| `DaqEventEnable(name)` / `DaqEventDisable(name)` | — | Enable/disable individual events at runtime |


### Calibration (`xcplib.h`)

> **These macros are essential for offline A2L generation.** Only `CalSegDecl` + `CalSegCreate`
> emit the `xcp_cals` section descriptor that xcpclient reads. `XcpCreateCalSeg()` does not.

| Macro / Function | Emits to ELF | Purpose |
|---|---|---|
| `CalSegDecl(name)`, `CalSegCreate(name)` | `xcp_cals` (at file scope) | Declare + emit `tXcpCalSegDescriptor` into `xcp_cals` |
| `CalSegLock(name)` | — | Lock segment for read; returns `const T *` to the active page |
| `CalSegUnlock(name)` | — | Release the lock |

### C++ RAII calibration wrapper (`xcplib.hpp`)

For C++ no-A2L builds, use `CalSegDeclRef` or `CalSegDecl` — these emit the `xcp_cals` section descriptor (like the C `CalSegDecl`) and additionally create a typed `CalSegRef<T>` RAII handle. Registration is done by `XcpInit()` from the section data; no runtime `XcpCreateCalSeg()` call is needed. File scope is the recommended default; local-scope `CalSegDeclRef` is supported for intentionally local visibility.

| Macro / Class | Emits to ELF | Purpose |
|---|---|---|
| `CalSegDeclRef(val, handle)` | `xcp_cals` (file scope recommended; local scope supported) | Declare section descriptor + create named `CalSegRef<T>` handle |
| `CalSegDecl(val)` | `xcp_cals` (at file scope) | Shorthand — handle is named `val##_calseg` |
| `handle.lock()` | — | Lock and return `CalSegGuard` (RAII `const T *`, auto-unlocks) |

> **Note:** `xcp::CalSeg<T>(name, ptr)` registers at runtime via `XcpCreateCalSeg()` and
> is for builds **with** on-target A2L generation. Do not use it in no-A2L builds.

### What is NOT available without on-target A2L

The following are compiled out when `OPTION_ENABLE_A2L_GENERATOR` is not set:

- `A2lCreateMeasurement*` / `A2lCreateParameter*` / `A2lCreateCurve*` / `A2lCreateMap*`
- `A2lTypedefBegin` / `A2lTypedefEnd` / `A2lTypedefXxxComponent`
- `A2lCreateTypedefInstance` / `A2lCreateTypedefReference`
- `A2lSetAbsoluteAddrMode` / `A2lSetRelativeAddrMode` / `A2lSetStackAddrMode` / `A2lSetSegmentAddrMode`
- `A2lCreateLinearConversion` / `A2lCreateEnumConversion`
- `A2lFinalize` / `A2lOnce`

These are replaced by the offline `xcpclient --create-a2l --elf <binary>` workflow.

---

## Using the xcpclient tool for A2L generation

```bash
# Build the no_a2l_demo (isolated build directory build-no_a2l/)
./build.sh no_a2l

# Or directly with CMake
cmake -B build-no_a2l -S . -DXCPLITE_CONFIGURATION=no_a2l -DCMAKE_BUILD_TYPE=Debug -DXCPLITE_BUILD_EXAMPLES=ON
cmake --build build-no_a2l
# or with forcing ARM architecture (macOS)
cmake -B build-no_a2l -S . -DXCPLITE_CONFIGURATION=no_a2l -DCMAKE_BUILD_TYPE=Debug -DXCPLITE_BUILD_EXAMPLES=ON -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-no_a2l

# Generate an A2L file for the no_a2l_demo application offline from its ELF file
# The ELF file must come from a Linux build: executables built on macOS (Mach-O) contain no DWARF debug information
# and are rejected by xcpclient, see create_a2l.sh for a remote build on a Linux target
# Example:
# Add all variables
xcpclient  --offline --elf build-no_a2l/no_a2l_demo --a2l no_a2l_demo.a2l --create-a2l --default-event=mainloop --verbose 1
# Add the given IP address:port and protocol to the generated A2L file
xcpclient --udp --dest-addr 192.168.0.206  --offline --elf build-no_a2l/no_a2l_demo  --a2l no_a2l_demo.a2l  --create-a2l --default-event=mainloop
# Filter on specific variables and compilation units
xcpclient --offline --udp --dest-addr 192.168.0.206 --elf build-no_a2l/no_a2l_demo   --a2l no_a2l_demo.a2l --create-a2l --default-event=mainloop --elf-unit-filter main --elf-var-filter "^(counter|params)"


# Connect to the XCP on UDP server on 192.168.0.206:5555, upload ELF file from target (requires OPTION_ENABLE_ELF_UPLOAD) and create the A2L file
xcpclient --udp --dest-addr 192.168.0.206  --elf no_a2l_demo.elf --upload-elf  --create-a2l --elf-unit-filter main --elf-var-filter "^(counter|params)"

# Measurement of variable global_counter with the ELF file only
xcpclient --udp --dest-addr=192.168.0.206:5555 --elf no_a2l_demo.elf --elf-var-filter "global_counter" --mea ".*" --time 5 --csv no_a2l_demo.csv

```
