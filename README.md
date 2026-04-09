# XCPlite

## What is XCP?

XCP is a data acquisition (measurement) and parameter tuning (calibration) protocol commonly used in the automotive industry (ASAM standard). Originally designed to run on microcontrollers, it provides real-time data monitoring and parameter tuning over various transport protocols with minimal impact on the target system.

## About XCPlite

XCPlite extends XCP use cases beyond traditional embedded microcontrollers to **modern multicore microprocessors** and SoCs running POSIX-compliant operating systems (Linux, QNX) or real-time operating systems (RTOS) such as ThreadX.

Designed exclusively for the **XCP on Ethernet Transport Layer** (TCP or UDP with jumbo frames), XCPlite solves the challenges of measurement and calibration in systems with true parallelism and multithreading:

- **Thread-safe & lock-free** - Consistent data acquisition and parameter modification across multiple cores, free of blocking and inter-thread contention
- **Memory-safe** - Measure and calibrate variables in any storage location: stack, heap, thread-local, and global
- **Deterministic runtime and resource consumption** - No heap allocations, static memory usage, zero copy and predictable execution times for real-time applications
- **Runtime A2L generation** - Define events, measurements and parameters, with metadata as code; the description file (A2L format) is generated at runtime and uploaded automatically
- **Complex type support** - Handles basic types, structs, arrays, and nested structures
- **Calibration segments** - Page switching, consistent atomic modification, and parameter persistence
- **PTP timestamps** - Prepared for high-precision PTP synchronized timestamps

Compared to many other logging, tracing, observability, or telemetry solutions, XCPlite reaches the above goals by accessing variables in place in their original ABI, without unnecessary copying, buffering or reserialization.  

The C or C++ API provides instrumentation macros for developers to define measurement points, calibration parameters, and meta data.  
Lock-free implementations ensure thread safety and data consistency without blocking latencies, even under high contention on multicore systems.

XCPlite is optimized for 64-bit architectures, compatible with 32-bit platforms. Requires C 11 and C++ 17.  

libxcplite serves as the C library foundation for the experimental [XCP-Lite Rust](https://github.com/vectorgrp/xcp-lite) API.  


## Other XCP Implementations

- **XCPbasic** - Free implementation for smaller Microcontrollers (8-bit+), optimized for CAN
- **XCPprof** - Commercial product in Vector's AUTOSAR MICROSAR and CANbedded portfolio

## New to XCP?

See the [detailed XCP introduction](docs/XCP_INTRODUCTION.md) or visit:  
- [Vector XCP Book](https://www.vector.com/int/en/know-how/protocols/xcp-measurement-and-calibration-protocol/xcp-book#)
- [Virtual VectorAcademy E-Learning](https://elearning.vector.com/)


## Getting Started

### Examples

Multiple examples demonstrating different features are available in the [examples](examples/) folder.

**Start here:**
- `hello_xcp` - Basic XCP server setup and instrumentation in C
- `hello_xcp_cpp` - Basic XCP server setup and instrumentation in C++

**Advanced examples:**
- `c_demo` - Complex data objects, calibration objects, and page switching
- `cpp_demo` - C++ class instrumentation and RAII wrappers
- `struct_demo` - Nested structs and multidimensional arrays
- `multi_thread_demo` - Multi-threaded measurement and parameter sharing among threads
- `point_cloud_demo` - Visualizing dynamic data structures in CANape 3D scene window
- `ptp4l_demo` - Using a PTP synchronized clock as XCP timestamp source
- `no_a2l_demo` - Workflow without runtime A2L generation (experimental, supported by xcpclient tool)
- `bpf_demo` - eBPF based syscall tracing (experimental)

For detailed information about each example and how to set up CANape projects, see the [examples documentation](examples/README.md).

**Requirements:**

XCPlite examples are designed to showcase advanced XCP capabilities and are tested with **CANape 23+** (free demo version available).  
The examples leverage:

- **Runtime A2L upload** - No manual A2L file management required
- **A2L TYPEDEFs** - Complex data structures with reusable type definitions
- **Address extensions** - Support for relative addressing and multiple memory spaces
- **Shared axis in typedefs** - Advanced calibration structures (CANape 24+, see `cpp_demo`)

These features enable efficient workflows for modern multicore HPC applications. While XCPlite is XCP-compliant and works with any XCP tool, the examples take full advantage of CANape's support for dynamic systems and advanced A2L features.

**Download:** [CANape demo version](https://www.vector.com/de/de/support-downloads/download-center)

### Build

XCPlite uses CMake as the build system.  
For quick builds of all examples, use the provided build scripts.  
Details how to build for Linux, QNX, macOS, and Windows are in the [building documentation](docs/BUILDING.md).


## Documentation

- **[Changelog](CHANGELOG.md)** - Version history

- **[API Reference](docs/xcplib.md)** - XCP instrumentation API
- **[Configuration](docs/xcplib_cfg.md)** - Configuration options
- **[Examples](examples/README.md)** - Example applications and CANape setup
- **[Technical Details](docs/TECHNICAL.md)** - Addressing modes, A2L generation, instrumentation costs
- **[Building](docs/BUILDING.md)** - Detailed build instructions and troubleshooting
- **[XCP Introduction](docs/XCP_INTRODUCTION.md)** - What is XCP?

## License

See [LICENSE](LICENSE) file for details.
