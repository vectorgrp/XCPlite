# hello_xcp_cpp — C++ API Example

XCPlite example using the C++ API. Demonstrates the same features as
[hello_xcp](../hello_xcp/README.md) in idiomatic C++ with the RAII calibration wrapper
and the variadic macro/template API.

---

## What it demonstrates

| Feature | How it is demonstrated |
|---|---|
| XCP server startup | `XcpInit` + `XcpEthServerInit` with runtime A2L generation |
| C++ RAII calibration wrapper | `xcp::CalSeg<ParametersT>` — created via `gCalSeg.emplace(...)`, locked/unlocked via the RAII guard returned by `.lock()` |
| Global variable measurement | `DaqEventVar` + `A2L_MEAS` on global variables |
| Stack variable measurement | `DaqEventVar` + `A2L_MEAS` / `A2L_MEAS_PHYS` on local variables (a heap-allocated instance alternative is shown commented out) |
| Class instance member measurement | `A2lCreateTypedef` + `A2L_MEASUREMENT_COMPONENT` / `A2L_MEASUREMENT_ARRAY_COMPONENT` for the `FloatingAverage` class, plus `A2L_MEAS` on individual member variables |
| Addressing modes | Selected automatically per variable by `DaqEventVar`/`A2L_MEAS` (stack vs. absolute) and by `A2lCreateTypedef`'s component macros (calibration segment relative) — no explicit `A2lSet*AddrMode` call needed, unlike the C example's manual code path |
| Variadic macro/template API | `DaqEventVar` + `A2L_MEAS*` (measurements) and `A2lCreateTypedef` + `A2L_PARAMETER_COMPONENT` / `A2L_MEASUREMENT_COMPONENT` (typedefs) — compact all-in-one registration with automatic addressing and type detection |
| Function instrumentation | Measure local variables and parameters inside `FloatingAverage::calc()` |

### Files

| File | Purpose |
|---|---|
| `src/main.cpp` | Demo application — server setup, calibration segment, events, measurement |
| `CANape/` | CANape project (A2L auto-upload, XCP UDP, port 5555) |

---

## Building

```bash
./build.sh examples
./build/hello_xcp_cpp
```

Or with CMake directly:

```bash
cmake -B build -S . -DXCPLITE_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target hello_xcp_cpp
./build/hello_xcp_cpp
```

---

## Quick Test

Use xcpclient to check the server works:

```bash

# Start the demo XCP server (in a different terminal or in background)
build/hello_xcp_cpp &

# Tune/calibrate counter_max
xcpclient --udp --cal params.counter_max 2000

# Measure counter
xcpclient --udp --mea counter --verbose 2

# If in a different folder on a different machine, use  --upload-a2l to get the A2L file from the client
# Note that an AML include file XCP_104.aml is needed beside the uploaded A2L file
xcpclient --udp --dest-addr 192.168.0.206 --upload-a2l --mea counter --verbose 2

# Stop the demo XCP server 
pkill hello_xcp_cpp

```



## CANape

Open `CANape/CANape.ini` in CANape. The project is pre-configured for XCP on UDP, port 5555,
with automatic A2L upload. If CANape cannot connect, verify the IP address in
*Device Configuration / Devices / XCP / Protocol / Transport Layer*.

![CANape Screenshot](CANape.png)

---

## Next steps

More advanced topics are covered by the other examples:

- **c_demo** — complex calibration objects (maps, curves), consistent atomic updates, polling
- **multi_thread_demo** — thread-local events, shared calibration, context/span instrumentation
- **cpp_demo** — signal generators, lookup tables with shared axis, two class instances
- **struct_demo** — nested structs, arrays of structs, typedef-based measurement types

