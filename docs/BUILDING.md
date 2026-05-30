# Building XCPlite

## Requirements

- **C Standard:** C11
- **C++ Standard:** C++17 (C++20 on Windows)
- **Platforms:** Linux, macOS, QNX, Windows (with limitations)

Most of the examples require **CANape 23 or later**, because they use A2L TYPEDEFs and relative memory addressing.

## Quick Build

### Linux or macOS

#### Using build.sh

`build.sh` wraps CMake and provides a convenient interface with target selection and diagnostics:

```bash
./build.sh
```

To select a specific compiler, use standard CMake environment variables:

```bash
# Use GCC
CC=gcc CXX=g++ ./build.sh

# Use Clang
CC=clang CXX=clang++ ./build.sh
```

See [Using build.sh for Diagnostics](#using-buildsh-for-diagnostics) for the full option reference.

#### Using pure CMake

All `build.sh` workflows have direct CMake equivalents. Use `-DCMAKE_BUILD_TYPE=` to select the build type (`Debug`, `Release`, `RelWithDebInfo`).

```bash
# Configure (Debug build, examples on by default)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Build everything (add --parallel for faster builds)
cmake --build build 
cmake --build build --parallel

# Build a specific target
cmake --build build --target hello_xcp

# Release build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Build tests (disable examples to avoid conflicts)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DXCPLITE_BUILD_TESTS=ON -DXCPLITE_BUILD_EXAMPLES=OFF
cmake --build build --parallel

# Build tools (ptptool, shmtool, xcpdaemon)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DXCPLITE_BUILD_TOOLS=ON
cmake --build build --parallel

# Build only the library
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DXCPLITE_BUILD_EXAMPLES=OFF
cmake --build build --target xcplite --parallel

# Install (default prefix: build/install)
cmake --install build
```

### QNX

Building QNX targets requires the QNX Software Development Platform (SDP) to be installed on the host.
The installation directory of the QNX SDP to be used for compilation must be given as input argument to the build script.
Note that all CPP targets are currently excluded from the build if QNX SDP 7.0 or lower is used, due to missing support of std::optional.
Currently, two target architectures are supported: x86_64 and aarch64le

Build all suitable targets with QNX 7.1.0 for x86_64 platforms on a Windows host:

```bash
build_qnx.bat Debug "C:\QNX\qnx710" x86_64
```

Build all suitable targets with QNX 8.0.0 for AArch64 platforms on a Linux host:

```bash
 ./build.sh Debug qcc all -q=/home/qnx800 -a=aarch64le
```

### Windows

It is possible to build for Windows with the Microsoft Visual Studio compiler, but there are some limitations and performance penalties under Windows.  
XCPlite is optimized for Posix based systems.  
On Windows, atomic operations are emulated and the transmit queue always uses a mutex on the producer side.

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build-msvc
cmake --build build-msvc --target hello_xcp
build-msvc/debug/hello_xcp.exe
```

To create a Visual Studio solution:

```bash
./build.bat
```

## CMake Build Options

All options can be passed to `cmake` with `-D<OPTION>=ON|OFF`:

```bash
cmake -B build -S . -DXCPLITE_BUILD_TESTS=ON
```

| Option | Default | Description |
|--------|---------|-------------|
| `XCPLITE_BUILD_EXAMPLES` | `ON`¹ | Build all example targets |
| `XCPLITE_BUILD_BPF_DEMO` | `ON` | Build `bpf_demo` (Linux only; requires `libbpf`; set `OFF` to skip the slow `find_library` probe) |
| `XCPLITE_BUILD_TESTS` | `OFF` | Build test targets (`a2l_test`, `cal_test`, `daq_test`, `clock_test`, `queue_test`, `type_detection_test_*`) |
| `XCPLITE_BUILD_TOOLS` | `OFF` | Build tool targets: `ptptool` (Linux only), `shmtool`, `xcpdaemon` |
| `XCPLITE_BUILD_RUST_TOOLS` | `OFF` | Build Rust tools `xcpclient` and `bintool` via `cargo` (requires Rust toolchain) |
| `XCPLITE_BUILD_NO_A2L_DEMO` | `OFF` | Build `no_a2l_demo` with `xcplib_no_a2l_cfg.h` override (disables A2L generator; use an isolated build directory, incompatible with other examples) |
| `XCPLITE_BUILD_FREERTOS_DEMO` | `OFF` | Build `freertos_demo` (downloads FreeRTOS-Kernel via FetchContent; Linux/macOS only; flips `XCPLITE_BUILD_EXAMPLES` default to `OFF`) |

¹ `ON` when building XCPlite standalone; `OFF` when included as a subproject via `add_subdirectory`/`FetchContent`.

## Installing the Library

XCPlite can be installed for use by external projects. The library uses CMake's standard installation mechanism.

### Installing to Local Staging Directory

To build and install to a local staging directory (recommended for development):

```bash
./build.sh lib install
```

This installs the library to `build/install/` with the following structure:
- `lib/` - Library files (`libxcplite.a` or `libxcplite.so`)
- `include/` - Public header files
- `lib/cmake/xcplite/` - CMake package configuration files

### Installing to Custom Location

To install to a custom location (e.g., system-wide installation):

```bash
./build.sh lib install=/usr/local
```

Or for release builds:

```bash
./build.sh release lib install=/usr/local
```

**Note:** System-wide installations may require sudo:

```bash
sudo ./build.sh release lib install=/usr/local
```

### Using the Installed Library

External projects can use the installed library via CMake:

```cmake
# In your CMakeLists.txt
find_package(xcplite REQUIRED)
target_link_libraries(your_target PRIVATE xcplite::xcplite)
```

When configuring your external project, specify the install location:

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/install
```

For local staging directory:

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/XCPlite/build/install
```

See `examples/external_example/` for a complete working example.

### Manual Installation with CMake

You can also use CMake directly:

```bash
# Configure with custom install prefix
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/usr/local

# Build the library
cmake --build build --target xcplite

# Install
cmake --install build
```

## Troubleshooting Compilation Issues

First of all, note that XCPlite requires C11 (and C++17 for C++ support).

A possible problematic requirement is that the 64-bit lockless transmit queue implementation requires `atomic_uint_least64_t`.  
This may cause problems on some platforms when using the clang compiler.  
**Prefer gcc for better compatibility.**  
If this is not an option, the mutex based 32-bit queue may be used instead.

### Testing Different Compilers

Use standard CMake environment variables to test different compilers:

```bash
# Test with system default
cmake -B build -S . && cmake --build build

# Test with GCC
CC=gcc CXX=g++ cmake -B build -S . && cmake --build build

# Test with Clang
CC=clang CXX=clang++ cmake -B build -S . && cmake --build build
```

### Using build.sh for Diagnostics

There is a script `build.sh` to automate the build process and to check which of the targets have build issues.  
If there are failures, copy & paste the complete output and provide it.

```bash
./build.sh
```

`build.sh` has command line parameters to select a release or debug build and target selection:

```bash
./build.sh --help
```

Examples:

```bash
# Debug build with examples (default)
./build.sh

# Release build with all targets
./build.sh release all

# RelWithDebInfo build (optimized with debug symbols)
./build.sh relwithdebinfo

# Build with GCC compiler
CC=gcc CXX=g++ ./build.sh release all

# Build tests only
./build.sh tests

# Build tool targets (ptptool, shmtool, xcpdaemon)
./build.sh tools

# Build Rust tools (xcpclient, bintool)
./build.sh rust

# Clean build directory and rebuild
./build.sh clean

# Clean all build directories and artifacts
./build.sh cleanall
```

### Type Detection Tests

If build fails and if `type_detection_test_c` builds ok, run it:

```bash
./build/type_detection_test_c
```

Copy & paste the complete output and provide it.

Same with `type_detection_test_cpp`:

```bash
./build/type_detection_test_cpp
```

## Platform-Specific Notes

### Windows Limitations

- Atomic operations are emulated
- The transmit queue always uses a mutex on the producer side
- Overall performance is lower compared to POSIX systems

### Compiler Compatibility

- **Recommended:** GCC (best compatibility with atomic operations)
- **Alternative:** Clang (may have issues with `atomic_uint_least64_t` on some platforms)
- **Windows:** MSVC (with limitations noted above)
