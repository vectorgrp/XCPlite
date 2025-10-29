# XCPlite

## What is XCP?

XCP is a measurement and parameter tuning (calibration) protocol commonly used in the automotive industry (ASAM standard). It provides real-time signal acquisition and parameter tuning over various transport protocols with minimal impact on the target system.

**New to XCP?** See the [detailed XCP introduction](docs/XCP_INTRODUCTION.md) or visit:
- [Vector XCP Book](https://www.vector.com/int/en/know-how/protocols/xcp-measurement-and-calibration-protocol/xcp-book#)
- [Virtual VectorAcademy E-Learning](https://elearning.vector.com/)



## About XCPlite

XCPlite extends XCP use cases beyond traditional embedded microcontrollers to **modern multicore microprocessors** and SoCs running POSIX-compliant operating systems (Linux, QNX) or real-time operating systems (RTOS) such as ThreadX.

Designed exclusively for **Ethernet transport** (TCP/UDP with jumbo frames), XCPlite solves the challenges of measurement and calibration in systems with true parallelism and multithreading:

- **Thread-safe & lock-free** - Consistent data acquisition and parameter modification across multiple cores
- **Memory-safe** - Measure and calibrate variables in any storage location: stack, heap, thread-local, or global
- **Runtime A2L generation** - Define events, parameters, and metadata as code; A2L description file generated and uploaded automatically
- **Complex type support** - Handle basic types, structs, arrays, and nested structures
- **Calibration segments** - Page switching, consistent atomic modification, and parameter persistence (freeze)
- **PTP timestamps** - Prepared for high-precision synchronized timestamps

The API provides instrumentation macros for developers to mark measurement points and calibration parameters. Thread safety and data consistency are guaranteed even in high-load multicore scenarios.

Optimized for 64-bit architectures, compatible with 32-bit platforms. Requires C11 (C++20 for C++ support). Serves as the C library foundation for [XCP-Lite Rust](https://github.com/vectorgrp/xcp-lite).

**Other XCP Implementations:**
- **XCPbasic** - Free implementation for smaller Microcontrollers (8-bit+), optimized for CAN
- **XCPprof** - Commercial product in Vector's AUTOSAR MICROSAR and CANbedded portfolio

## Requirements

- **C Standard:** C11
- **C++ Standard:** C++20
- **Platforms:** Linux, QNX, macOS, ThreadX, Windows (with some limitations)
- **Tools:** Most examples require CANape 23+ (uses A2L TYPEDEFs and relative addressing)

## Getting Started

### Examples

Multiple examples demonstrating different features are available in the [examples](examples/) folder.

**Start here:**
- `hello_xcp` - Basic XCP server setup and instrumentation in C
- `hello_xcp_cpp` - Basic instrumentation in C++

**Advanced examples:**
- `c_demo` - Complex data objects, calibration objects, and page switching
- `struct_demo` - Nested structs and multidimensional arrays
- `multi_thread_demo` - Multi-threaded measurement and parameter sharing
- `cpp_demo` - C++ class instrumentation and RAII wrappers
- `no_a2l_demo` - Workflow without runtime A2L generation
- `bpf_demo` - Experimental syscall tracing

For detailed information about each example and how to set up CANape projects, see the [examples documentation](examples/README.md).

### Build

**Linux/macOS:**
```bash
./build.sh                  # Build all targets
# or
cmake -S . -B build && cmake --build build --target hello_xcp
```

**Windows:**
```bash
./build.bat                 # Creates Visual Studio solution
# or
cmake -S . -B build-msvc && cmake --build build-msvc --target hello_xcp
```

**Note:** Windows has some limitations (atomic operations emulated, mutex-based transmit queue).

For detailed build instructions and troubleshooting, see [Building Documentation](docs/BUILDING.md).

## Documentation

- **[API Reference](docs/xcplib.md)** - XCP instrumentation API
- **[Configuration](docs/xcplib_cfg.md)** - Configuration options
- **[Examples](examples/README.md)** - Example applications and CANape setup
- **[Technical Details](docs/TECHNICAL.md)** - Addressing modes, A2L generation, instrumentation costs
- **[Building](docs/BUILDING.md)** - Detailed build instructions and troubleshooting
- **[XCP Introduction](docs/XCP_INTRODUCTION.md)** - What is XCP?
- **[Changelog](CHANGELOG.md)** - Version history

## License

See [LICENSE](LICENSE) file for details.
