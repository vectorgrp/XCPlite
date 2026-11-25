# XCPlite

## What is XCP?

XCP is a data acquisition (measurement) and parameter tuning (calibration) protocol commonly used in the automotive industry (ASAM standard). Originally designed to run on microcontrollers, it provides real-time data monitoring and parameter tuning over various transport protocols with minimal impact on the target system.

## About XCPlite

XCPlite extends XCP use cases beyond traditional embedded microcontrollers to **modern multicore microprocessors** and SoCs running POSIX-compliant operating systems (Linux, QNX) or real-time operating systems (RTOS) such as FreeRTOS or ThreadX.

Designed exclusively for the **XCP on Ethernet Transport Layer** (using TCP or UDP socketswith jumbo frames), XCPlite solves the challenges of measurement and calibration in systems with true parallelism and multithreading:

- **Relative Addressing** - Measure and calibrate variables in any storage location: on stack, heap, thread-local, and global memory using relative addressing schemes and event driven data access
- **Thread-safe & lock-free** - Consistent data acquisition and parameter modification across multiple cores, free of blocking and inter-thread contention
- **Deterministic runtime and low resource consumption** - No heap allocations, static memory usage, zero copy and predictable execution times for real-time applications
- **Runtime A2L generation and download** - Define events, measurements and parameters, with metadata as code; the description file (A2L format, see note below) is generated on target and downloaded from target automatically
- **Build-time A2L generation** - Define events, measurements and parameters, with metadata as code and use a XCPlite code instrumentation aware ELF->A2L generator at build time
- **Complex type support** - Handles basic types, structs, arrays, and nested structures
- **Calibration segments** - Calibration parameter page switching, consistent atomic modification, and persistence
- **PTP timestamps** - Prepared for high-precision PTP synchronized timestamps

Note: An **A2L file** is a tool- and human-readable standardized description of the ECU measurements, parameters, data types, meta data (comments, physical units, limits, ...) and XCP protocol settings, - comparable to a manifest that tells measurement and calibration tools what exists and how to access it.

Compared to many other logging, tracing, observability, or telemetry solutions, XCPlite reaches the above goals by accessing variables in global memory, stack or heap with their original ABI, without unnecessary copying, buffering or reserialization.  

The C or C++ API provides instrumentation for developers to define measurement points (events), transparent wrappers for calibration parameters, and meta data.
Lock-free implementations ensure thread safety and data consistency without blocking latencies, even under high contention on multicore systems.

XCPlite > v2.1 is optimized for 64-bit microprocessor and 32-bit microcontroller platform architectures.

XCPlite requires C 11 and C++ 17.  

libxcplite serves as the C library foundation for the experimental [XCP-Lite Rust](https://github.com/vectorgrp/xcp-lite) API.   

XCPlite for Rust demonstrates that direct-memory access for measurement and calibration can be implemented while preserving Rust safety guarantees. It also showcases advanced Rust features, including a derive macro that reflects types and provides metadata attributes for A2L generation. It demonstrates measurement of container types with variable size. It has an in memory registry for all A2L artifacts and supports serialization to non binary parameter persistence formats (JSON). 


## Getting Started

### Minimal application with runtime A2L generation

The public API is `inc/xcplib.h` (C) and `inc/xcplib.hpp` (C++), runtime A2L generation is `inc/a2l.h` and `inc/a2l.hpp`. The headers document every function and macro, [docs/API_OVERVIEW.md](docs/API_OVERVIEW.md) indexes them by category with the configuration options and the examples using them.

The basic sequence of an instrumented C++ application with runtime A2L generation (default configuration):

```cpp
#include <a2l.hpp>
#include <xcplib.hpp>

struct ParamsT {
    uint32_t delay_us;
    float gain;
};
const ParamsT kParams = {.delay_us = 1000, .gain = 0.5f}; // Default values (reference page)

int main() {
    XcpSetLogLevel(3); // Optional, 1 = error ... 4 = XCP command trace

    // Always initialize XCP first, XCP_MODE_DEACTIVATE keeps all instrumentation passive
    XcpInit("my_project" /* project name */, "V1.0.0" /* EPK version */, XCP_MODE_LOCAL);
    const uint8_t addr[4] = {0, 0, 0, 0}; // Bind to any address
    XcpEthServerInit(addr, 5555 /* port */, false /* UDP */, 32 * 1024 /* measurement queue size */);
    A2lInit(addr, 5555, false, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS);

    // Calibration segment for params, described in the A2L file as a typedef instance
    A2lCreateTypedef(ParamsT, "Calibration parameters",
                     A2L_PARAMETER_COMPONENT(gain, "Gain parameter", "", 0.0, 1.0),
                     A2L_PARAMETER_COMPONENT(delay_us, "Mainloop cycle delay", "us", 0.0, 100000.0));
    xcp::CalSeg<ParamsT> calseg("params", &kParams);
    calseg.CreateA2lTypedefInstance("ParamsT", "Calibration parameters");

    float value = 0.0f; // Measurement variable on the stack

    for (;;) {
        uint32_t delay_us;
        {
            auto params = calseg.lock(); // RAII guard, consistent, wait-free access until end of scope
            value += params->gain;
            delay_us = params->delay_us;
        }

        // Create event mainloop and register its measurement variables once, timestamp and measure them on every call
        DaqEventVar(mainloop, A2L_MEAS(value, "Output value"));
        sleepUs(delay_us);
    }

    XcpEthServerShutdown();
    return 0;
}
```

[hello_xcp_cpp](examples/hello_xcp_cpp/README.md) is the complete version of this sequence, [hello_xcp](examples/hello_xcp/README.md) the C version with `XcpCreateCalSeg`/`XcpLockCalSeg` and `DaqCreateEvent`/`DaqTriggerEvent`.


### Minimal application without runtime A2L generation

In the `no_a2l` configuration, the library has no A2L generator and no file system dependency. The instrumentation macros put the events, calibration segments, EPK and metadata into ELF sections, `XcpInit` registers them from there, so the event ids and segment numbers are fixed at link time. The A2L file is generated offline from the ELF file and its DWARF debug information with the xcpclient tool.

The same application in the `no_a2l` configuration:

```cpp
#include <xcplib.hpp>

struct ParamsT {
    uint32_t delay_us;
    float gain;
};
const ParamsT params = {.delay_us = 1000, .gain = 0.5f}; // Default values (reference page), the variable name is the segment name
CalSegDecl(params); // Calibration segment params with the typed handle params_calseg, created by XcpInit

// Metadata for the A2L generator, '__' separates the struct fields
XCP_COMMENT(params__gain, "Gain parameter");
XCP_LIMITS(params__gain, 0.0, 1.0);
XCP_COMMENT(params__delay_us, "Mainloop cycle delay");
XCP_UNIT(params__delay_us, "us");
XCP_LIMITS(params__delay_us, 0.0, 100000.0);

int main() {
    XcpSetLogLevel(3); // Optional, 1 = error ... 4 = XCP command trace

    // Always initialize XCP first, XcpInit registers the events and calibration segments from the ELF sections
    XcpCreateEpk("V1.0.0"); // EPK version string for the A2L generator, same as in XcpInit
    XcpInit("my_project" /* project name */, "V1.0.0" /* EPK version */, XCP_MODE_LOCAL);
    const uint8_t addr[4] = {0, 0, 0, 0}; // Bind to any address
    XcpEthServerInit(addr, 5555 /* port */, false /* UDP */, 32 * 1024 /* measurement queue size */);

    DaqCreateEvent(mainloop); // Measurement event, the event id is known at link time

    XCP_COMMENT(value, "Output value");
    float value = 0.0f; // Measurement variable

    for (;;) {
        uint32_t delay_us;
        {
            auto p = params_calseg.lock(); // RAII guard, consistent, wait-free access until end of scope
            value += p->gain;
            delay_us = p->delay_us;
        }

        // Copy value into a capture struct on the stack, timestamp and measure it, works with optimized builds
        DaqTriggerEventCapture(mainloop, value);
        sleepUs(delay_us);
    }

    XcpEthServerShutdown();
    return 0;
}
```

Build it with debug information (`-g`) and generate the A2L file from the executable. `--dest-addr` is the target address written into the A2L file, `--elf-unit-filter main` restricts the variables to the compilation unit `main.cpp`:

```bash
xcpclient --offline --udp --dest-addr 192.168.0.206 --elf my_project --elf-unit-filter main --create-a2l --a2l my_project.a2l
```

[no_a2l_demo_cpp](examples/no_a2l_demo_cpp/README.md) is the complete version of this sequence, [no_a2l_demo](examples/no_a2l_demo/README.md) the C version. Workflow, naming rules and metadata macros are described in [Offline A2L Generation](docs/OFFLINE_A2L.md).

## Tool Compatibility

XCPlite is XCP >1.4 compliant and interoperates with CANape, CANoe, and other third-party ASAM-compliant XCP tools and data loggers. It is tested regularly with CANape 23+.

For save operation, the client tool must respect fixed event definitions and handle address extensions correctly, because XCPlite uses them to encode relative memory addressing. The address extension and address together can be treated as an opaque handle that identifies a data object. Disrespecting this could lead to inconsistent or corrupt data acquisition or parameter modification.  

If a tool understands ABI details of complex data instances, it may still perform the usual address calculations to access individual fields of composite types, array elements, or merge memory ranges to optimize upload, download, and data acquisition.

General-purpose A2L editors/creators typically cannot reconstruct XCPlite-specific relative address encoding automatically. In this case, you are limited to use only global memory objects. For full featured offline A2L generation, use the XCPlite-aware xcpclient tool, see [Offline A2L Generation](docs/OFFLINE_A2L.md).

Support for A2L TYPEDEF and shared axis references with `this.` is beneficial, but not strictly required.

Support for A2L upload via GET_ID is useful, but in most cases the A2L file generated by the XCPlite on-target A2L creator is stable per build, which means the generated A2L does not change when the application is started again. Note that there are some, more experimental examples, which showcase dynamic per thread event creation for thread local data, where this is not true. In this case, the A2L needs to be uploaded always, not only on build version string (EPK) change. 

The AML file for XCP 1.4 is not embedded in the A2L file, to keep the A2l file small. A tool will need `XCP_104.aml` beside the A2L file.  

## Examples

Multiple examples demonstrating different features are available in the [examples](examples/README.md) folder.

**Start here:**
- [hello_xcp](examples/hello_xcp/README.md) - Basic XCP server setup and instrumentation in C
- [hello_xcp_cpp](examples/hello_xcp_cpp/README.md) - Basic XCP server setup and instrumentation in C++
- [no_a2l_demo](examples/no_a2l_demo/README.md) - Linux workflow without runtime A2L generation (offline A2L generation from ELF file by using the included xcpclient tool)
- [no_a2l_demo_cpp](examples/no_a2l_demo_cpp/README.md) - C++ workflow with offline A2L generation

**Advanced examples:**
- [freertos_demo](examples/freertos_demo/README.md) - FreeRTOS examples using lwIP on STM32/ESP32, host sockets, and FreeRTOS-Plus-TCP with libpcap networking and loopback tests (Linux)
- [silkit_demo](examples/silkit_demo/README.md) - SIL-Kit multi-participant measurement and calibration (via shared memory (SHM mode))
- [ptp4l_demo](examples/ptp4l_demo/README.md) - Using a PTP synchronized clock as XCP timestamp source
- [bpf_demo](examples/bpf_demo/README.md) - eBPF based syscall tracing
- [point_cloud_demo](examples/point_cloud_demo/README.md) - Measure and visualize dynamic length data structures in CANape (point cloud in 3D scene window)
- [c_demo](examples/c_demo/README.md) - More detailed complex data objects, calibration objects, and calibration page switching
- [cpp_demo](examples/cpp_demo/README.md) - More detailed C++ class instrumentation and RAII wrappers
- [multi_thread_demo](examples/multi_thread_demo/README.md) - More demanding multi-threaded measurement and parameter sharing among many threads
- [struct_demo](examples/struct_demo/README.md) - More detailed nested structs and multidimensional arrays

For detailed information about each example and how to set up CANape projects, see the [examples overview](examples/README.md).

Some XCPlite examples are designed to showcase advanced CANape capabilities and require **CANape 24+** (free demo version available).

**Download:** [CANape demo version](https://www.vector.com/de/de/support-downloads/download-center)


## Build

XCPlite uses CMake as the build system.  
For quick builds of all examples, use the provided build scripts.  
Details how to build for Linux, QNX, macOS, and Windows are in the [building documentation](docs/BUILDING.md).  
For microcontroller builds, refer to the READMEs in the RTOS example folders.  


## Documentation

- **[Changelog](CHANGELOG.md)** - Version history

- **[API Overview](docs/API_OVERVIEW.md)** - Index of the public C and C++ API, documented in the headers `inc/xcplib.h`, `inc/a2l.h`, `inc/xcplib.hpp` and `inc/a2l.hpp`
- **[Configuration](docs/xcplib_cfg.md)** - Configuration options
- **[Examples](examples/README.md)** - Example applications and CANape setup
- **[Technical Details](docs/TECHNICAL.md)** - Addressing modes, on-target A2L generation, instrumentation costs and markers
- **[Offline A2L Generation](docs/OFFLINE_A2L.md)** - A2L generation from the ELF file with xcpclient
- **[Building](docs/BUILDING.md)** - Detailed build instructions
- **[XCP Introduction](docs/XCP_INTRODUCTION.md)** - What is XCP?


## Other XCP Implementations

- **XCPbasic** - Free implementation for smaller Microcontrollers (8-bit+), optimized for CAN
- **XCPprof** - Commercial product in Vector's AUTOSAR MICROSAR and CANbedded portfolio


## New to XCP?

See the [detailed XCP introduction](docs/XCP_INTRODUCTION.md) or visit:  
- [Vector XCP Book](https://www.vector.com/int/en/know-how/protocols/xcp-measurement-and-calibration-protocol/xcp-book#)
- [Virtual VectorAcademy E-Learning](https://elearning.vector.com/)


## License

See [LICENSE](LICENSE) file for details.
