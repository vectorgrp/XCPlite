# hello_xcp — Basic C API Example

The simplest XCPlite example in pure C. The recommended starting point.
For C++ usage see [hello_xcp_cpp](../hello_xcp_cpp/README.md).

---

## What it demonstrates

| Feature | How it is demonstrated |
|---|---|
| XCP server startup | `XcpInit` + `XcpEthServerInit` with runtime A2L generation |
| Calibration segment | `XcpCreateCalSeg` to create it; `A2lSetSegmentAddrMode` + `A2lCreateParameter` to describe its parameters in the A2L file; safe, wait-free (and recursive) read access via `XcpLockCalSeg` / `XcpUnlockCalSeg` |
| Global variable measurement | `DaqEventVar` + `A2L_MEAS` / `A2L_MEAS_PHYS` on global variables |
| Stack variable measurement | `DaqEventVar` + `A2L_MEAS` on local variables inside a function |
| Addressing modes | Absolute (global variables), Stack frame relative (local variables) and Calibration segment relative (calibration parameters) — see `A2lSetAbsoluteAddrMode` / `A2lSetStackAddrMode` / `A2lSetSegmentAddrMode` |
| Function instrumentation | `calc_power()` — register function parameters and local variables, trigger event `calc_power` inside the function |

`src/main.c` shows two equivalent ways to create the same events and measurements, selected by `#define OPTION_USE_VARIADIC_MACROS` (enabled by default):

- **Variadic macros (default)**: `DaqEventVar(event, A2L_MEAS(...), ...)` registers each variable — auto-detecting whether it needs stack or absolute addressing — and triggers the event, all in one call.
- **Manual macros** (undefine `OPTION_USE_VARIADIC_MACROS` in `main.c` to use): separate `DaqCreateEvent`, explicit `A2lSetAbsoluteAddrMode`/`A2lSetStackAddrMode` + `A2lCreateMeasurement` calls, and `DaqTriggerEvent`.

The `#ifndef OPTION_USE_VARIADIC_MACROS` blocks in `main.c` contain the manual variant and are not compiled by default.

### Files

| File | Purpose |
|---|---|
| `src/main.c` | Demo application — server setup, calibration segment, events, measurement |
| `CANape/` | CANape project (A2L auto-upload, XCP UDP, port 5555) |

---

## Building

```bash
./build.sh examples
./build/hello_xcp
```

Or with CMake directly:

```bash
cmake -B build -S . -DXCPLITE_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target hello_xcp
./build/hello_xcp
```

---

## Quick Test

Use xcpclient to check the server works:

```bash

# Start the demo XCP server (in a different terminal or in background)
build/hello_xcp &

# Tune/calibrate counter_max
xcpclient --udp --cal params.counter_max 2000

# Measure counter
xcpclient --udp --mea counter --verbose 2

# If in a different folder on a different machine, use  --upload-a2l to get the A2L file from the client
# Note that an AML include file XCP_104.aml is needed beside the uploaded A2L file
xcpclient --udp --dest-addr 192.168.0.206 --upload-a2l --mea counter --verbose 2

# Stop the demo XCP server 
pkill hello_xcp

```



## CANape

Open `CANape/CANape.ini` in CANape. The project is pre-configured for XCP on UDP, port 5555,
with automatic A2L upload. If CANape cannot connect, verify the IP address in
*Device Configuration / Devices / XCP / Protocol / Transport Layer*.

![CANape Screenshot](CANape.png)

---

## Next steps

More advanced topics are covered by the other examples:

- **hello_xcp_cpp** — C++ RAII calibration segment wrapper, variadic macro API
- **c_demo** — complex calibration objects (maps, curves), consistent atomic updates, polling
- **multi_thread_demo** — thread-local events, shared calibration, context/span instrumentation
- **cpp_demo** — signal generators, lookup tables, class instance measurement
- **struct_demo** — nested structs, arrays of structs, typedef-based measurement types

