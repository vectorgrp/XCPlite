# Building XCPlite

## Requirements

- **C Standard:** C11
- **C++ Standard:** C++17 (C++20 on Windows)
- **Platforms:** Linux, macOS, QNX, FreeRTOS, Windows (with limitations)

Most of the examples require **CANape 23 or later**, because they use A2L TYPEDEFs and relative memory addressing.

## Build configurations

The library has **mutually exclusive build configurations** selected via `XCPLITE_CONFIGURATION`. Each configuration compiles a different set of features into the library and uses a separate build directory.

| Configuration | Build directory | Config override | Description |
|---------------|----------------|-----------------|-------------|
| `default` | `build/` | *(none)* | 64-bit, on-target A2L generation, filesystem, Ethernet UDP/TCP |
| `no_a2l` | `build-no_a2l/` | `xcplib_no_a2l_cfg.h` | Like default, but without on-target A2L; A2L generated externally from ELF by xcpclient |
| `ptp` | `build-ptp/` | `xcplib_ptp_cfg.h` | Like default with socket hardware timestamps; requires Linux and a PTP-capable NIC |
| `shm` | `build-shm/` | `xcplib_shm_cfg.h` | Shared-memory multi-application mode (shmtool, xcpdaemon) |
| `rtos` | `build-rtos/` | `xcplib_rtos_cfg.h` | FreeRTOS embedded targets: reduced footprint, no filesystem, 32-bit |
| `raw` | `build-raw/` | `xcplib_raw_cfg.h` | Raw Ethernet transport: UDP/IPv4 inside xcplib, no TCP/IP stack (Linux only) |

Each `src/xcplib_<name>_cfg.h` header documents the exact overrides applied on top of the defaults in `src/xcplib_cfg.h`.

> **Use a separate build directory per configuration.** Configurations apply different compile definitions to the library object files; mixing them in one build directory produces incorrect results.

### Application specific configuration override

The shipped configurations are override headers `src/xcplib_<name>_cfg.h`, which `src/xcplib_cfg.h` includes at its end when the preprocessor symbol `XCPLIB_CFG_OVERRIDE` names them. An application can supply its own header in the same style (`#undef`/`#define` of `OPTION_*`, see `docs/xcplib_cfg.md` for the tunables) via the CMake variable `XCPLITE_CFG_OVERRIDE`:

```bash
# Standalone library build
cmake -B build -S . -DXCPLITE_CFG_OVERRIDE=/path/to/xcplib_app_cfg.h
```

```cmake
# Consuming project (add_subdirectory / FetchContent), before FetchContent_MakeAvailable()
set(XCPLITE_CFG_OVERRIDE "${CMAKE_CURRENT_SOURCE_DIR}/config/xcplib_app_cfg.h")
```

xcplite defines `XCPLIB_CFG_OVERRIDE="<file name>"` and adds the header's directory to the include path, both as PUBLIC usage requirements of the `xcplite` target, so the library and every consumer see the same options (struct layouts and macro expansions depend on them). With install rules enabled the header is installed next to `xcplib_cfg.h`, so `find_package` consumers get the identical configuration.

- Only valid with `XCPLITE_CONFIGURATION=default`. To build on a shipped configuration, `#include` its header (e.g. `"xcplib_no_a2l_cfg.h"`) at the top of the custom header.
- The file name must differ from the shipped headers, since `src/` is searched first.

`examples/fetchcontent_example/config/xcplib_app_cfg.h` is a complete example.

## Build options

Within a chosen configuration, the following options control what gets built:

| Option | Default | Description |
|--------|---------|-------------|
| `XCPLITE_BUILD_EXAMPLES` | `OFF` | Build example targets for the selected configuration (see table below) |
| `XCPLITE_BUILD_TESTS` | `OFF` | Build test targets for the selected configuration (see table below) |
| `XCPLITE_BUILD_TOOLS` | `OFF` | Build tool targets for the selected configuration (see table below) |
| `XCPLITE_BUILD_RUST_TOOLS` | `OFF` | Build Rust tools `xcpclient` and `bintool` via cargo (any configuration; requires Rust toolchain) |
| `XCPLITE_BUILD_BPF_DEMO` | `OFF` | Build `bpf_demo` (default configuration, Linux only; requires libbpf) |
| `XCPLITE_INSTALL` | `ON` top-level, `OFF` as subproject | Generate install rules (`cmake --install`). Automatically `OFF` when xcplite is consumed via `add_subdirectory`/`FetchContent`, so a consuming project's install does not pull in xcplite unless requested |
| `XCPLITE_CFG_OVERRIDE` | *(empty)* | Path to an application specific configuration override header, applied on top of `src/xcplib_cfg.h`. Only with `XCPLITE_CONFIGURATION=default`; see [Application specific configuration override](#application-specific-configuration-override) |

### Targets per configuration

| Configuration | Examples (`BUILD_EXAMPLES`) | Tests (`BUILD_TESTS`) | Tools (`BUILD_TOOLS`) |
|---------------|-----------------------------|-----------------------|-----------------------|
| `default` | hello_xcp, hello_xcp_cpp, c_demo, cpp_demo, point_cloud_demo, struct_demo, multi_thread_demo, ptp4l_demo¹, bpf_demo¹² | a2l_test, cal_test, daq_test, clock_test, queue_test, xcp_test, type_detection_test_* | *(none)* |
| `no_a2l` | no_a2l_demo, no_a2l_demo_cpp | *(none)* | *(none)* |
| `ptp` | ptp4l_demo¹ | clock_test | ptptool¹ |
| `shm` | hello_xcp (SHM), hello_xcp_cpp (SHM) | *(none)* | shmtool, xcpdaemon³ |
| `rtos` | freertos_emu_demo³ (downloads FreeRTOS-Kernel) | *(none)* | *(none)* |
| `raw` | udp_raw_demo (Linux only) | socket_raw_test | *(none)* |

¹ Linux only  ² requires libbpf  ³ not supported on Windows

### Standalone examples (built separately)

These examples have their own `CMakeLists.txt` and consume xcplite either from an installed package (`find_package(xcplite)`) or directly from source (`FetchContent`). They are not built from the root CMake project:

- **`examples/fetchcontent_example/`** — Minimal C/C++ consumer example using `FetchContent`. Clones and builds xcplite from git as part of the example's own build; no install step needed. See `examples/fetchcontent_example/README.md` and [Using xcplite via FetchContent](#using-xcplite-via-fetchcontent).
- **`examples/silkit_demo/`** — Requires [SilKit](https://github.com/vectorgrp/sil-kit) and an installed xcplite (shm configuration recommended). See `examples/silkit_demo/README.md`.
- **`examples/external_example/`** — Minimal C/C++ consumer example. Shows how to use xcplite from an installed package. See `examples/external_example/README.md`.
- **`examples/esp32_freertos_demo/`** — ESP32 FreeRTOS target. Uses the same `xcplib_rtos_cfg.h` override as the `rtos` CMake configuration, but is built with [PlatformIO](https://platformio.org/). Not a CMake project. The CMake `rtos` configuration builds `freertos_emu_demo` instead, which runs the same FreeRTOS xcplite code on a POSIX simulator for host-side testing (Linux/macOS only).

## Quick Build

### Linux or macOS

#### Using build.sh

`build.sh` wraps CMake and provides a convenient interface for common workflows:

```bash
./build.sh [build_type] [configuration] [target] [options]
```

| Argument group | Values | Default |
|----------------|--------|---------|
| Build type | `debug` \| `release` \| `relwithdebinfo` | `debug` |
| Configuration | `default` \| `no_a2l` \| `ptp` \| `shm` \| `rtos` \| `raw` | `default` |
| Target | `lib` \| `examples` \| `tests` \| `tools` \| `rust_tools` \| `all` | `examples` |
| Options | `clean` `cleanall` `install` `install=<path>` `cargo_install` `tidy` | — |

Arguments can be given in any order. Unrecognised arguments cause an error.

> **Which examples, tests and tools are actually built depends on the selected configuration.** For example, `tools` builds `ptptool` only for `ptp`, and `shmtool`/`xcpdaemon` only for `shm`. See the [Targets per configuration](#targets-per-configuration) table above.
>
> **`install` and `cargo_install` are independent:**
> - `install` runs `cmake --install` — copies the xcplite library headers and cmake config to the install prefix. Never touches Rust tools.
> - `cargo_install` runs `cargo install --path --locked` for `xcpclient` and `bintool` (the dependency versions from `Cargo.lock` are used) — installs their binaries to `~/.cargo/bin`. Only meaningful with the `rust_tools` or `all` target. Requires the Rust toolchain.

```bash
./build.sh --help
```

Examples:

```bash
# Library + examples, default config, debug (default)
./build.sh

# Release build
./build.sh release

# Tests, default config
./build.sh tests

# shm config: shmtool + xcpdaemon
./build.sh shm tools

# ptp config: ptptool (Linux only)
./build.sh ptp tools

# no_a2l config: no_a2l_demo, no_a2l_demo_cpp
./build.sh no_a2l examples

# rtos config: freertos_demo (Linux/macOS only)
./build.sh rtos examples

# raw config: udp_raw_demo, raw Ethernet transport (Linux only, needs CAP_NET_RAW)
./build.sh raw examples

# Library only, install to build/install
./build.sh lib install

# Release build, install to /usr/local
./build.sh release lib install=/usr/local

# Rust tools: build only (binaries in tools/xcpclient/target/debug/)
./build.sh rust_tools

# Rust tools: build + install to ~/.cargo/bin  (requires cargo)
./build.sh rust_tools cargo_install

# Clean rebuild
./build.sh clean examples

# Clean all build directories
./build.sh cleanall

# Build library and run clang-tidy
./build.sh lib tidy

# Build with GCC, all targets
CC=gcc CXX=g++ ./build.sh release all

# Build with Clang, all targets
CC=clang CXX=clang++ ./build.sh release all

```

#### Using pure CMake

All `build.sh` workflows have direct CMake equivalents. Use `-DCMAKE_BUILD_TYPE=` to select the build type (`Debug`, `Release`, `RelWithDebInfo`).

```bash
# Default configuration — library only
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel

# Default configuration — library + examples
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DXCPLITE_BUILD_EXAMPLES=ON
cmake --build build --parallel

# Default configuration — tests
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DXCPLITE_BUILD_TESTS=ON
cmake --build build --parallel

# no_a2l configuration — library + no_a2l_demo + no_a2l_demo_cpp
cmake -B build-no_a2l -S . -DXCPLITE_CONFIGURATION=no_a2l -DXCPLITE_BUILD_EXAMPLES=ON
cmake --build build-no_a2l --parallel

# ptp configuration — library + ptptool + clock_test (Linux only)
cmake -B build-ptp -S . -DXCPLITE_CONFIGURATION=ptp -DXCPLITE_BUILD_TOOLS=ON -DXCPLITE_BUILD_TESTS=ON
cmake --build build-ptp --parallel

# shm configuration — library + shmtool + xcpdaemon
cmake -B build-shm -S . -DXCPLITE_CONFIGURATION=shm -DXCPLITE_BUILD_TOOLS=ON
cmake --build build-shm --parallel

# rtos configuration — freertos_demo (downloads FreeRTOS-Kernel; Linux/macOS only)
cmake -B build-rtos -S . -DXCPLITE_CONFIGURATION=rtos -DXCPLITE_BUILD_EXAMPLES=ON
cmake --build build-rtos --parallel

# raw configuration — udp_raw_demo, raw Ethernet transport (Linux only)
cmake -B build-raw -S . -DXCPLITE_CONFIGURATION=raw -DXCPLITE_BUILD_EXAMPLES=ON
cmake --build build-raw --parallel

# Build a specific target
cmake --build build --target hello_xcp

# Release build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

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
cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build-msvc -DXCPLITE_BUILD_EXAMPLES=ON
cmake --build build-msvc --target hello_xcp
build-msvc/debug/hello_xcp.exe
```

To create a Visual Studio 'Visual Studio 17 2022' solution:

```bash
./build.bat
```

## CMake Reference

All options can be passed to `cmake` with `-D<OPTION>=ON|OFF` or `-D<OPTION>=<value>`:

```bash
cmake -B build -S . -DXCPLITE_CONFIGURATION=ptp -DXCPLITE_BUILD_TOOLS=ON
```

See [Build configurations](#build-configurations) and [Build options](#build-options) above for the full reference.

## Installing the Library

XCPlite can be installed for use by external projects. The library uses CMake's standard installation mechanism.

> **Note:** Only one configuration is installed at a time. The installed cmake package records `XCPLITE_CONFIGURATION_INSTALLED` so consumers can verify they are using the expected configuration. To install multiple configurations, use separate install prefixes (e.g., `/usr/local/xcplite-default`, `/usr/local/xcplite-ptp`).

### Installing to Local Staging Directory

```bash
# Default configuration
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --install build
# Installed to: build/install/

# ptp configuration
cmake -B build-ptp -S . -DXCPLITE_CONFIGURATION=ptp -DCMAKE_BUILD_TYPE=Release
cmake --install build-ptp
# Installed to: build-ptp/install/
```

### Installing to Custom Location

```bash
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
sudo cmake --install build
```

### Using the Installed Library

```cmake
# In your CMakeLists.txt
find_package(xcplite REQUIRED)
target_link_libraries(your_target PRIVATE xcplite::xcplite)
```

```bash
# Point CMake to the install location
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/xcplite/build/install
```

### Using xcplite via FetchContent

Instead of installing xcplite first, a project can build it from source as part of its own build. CMake clones the repository at configure time into `<build>/_deps/xcplite-src` and adds it as a subdirectory:

```cmake
include(FetchContent)

FetchContent_Declare(xcplite
    GIT_REPOSITORY https://github.com/vectorgrp/XCPlite.git
    GIT_TAG        V2.2.2
    GIT_SHALLOW    TRUE
)

# Select the configuration and targets before FetchContent_MakeAvailable()
# (the CACHE ... FORCE form works as well)
set(XCPLITE_CONFIGURATION  "default")   # default | no_a2l | ptp | shm | raw
set(XCPLITE_BUILD_EXAMPLES OFF)
set(XCPLITE_BUILD_TESTS    OFF)

FetchContent_MakeAvailable(xcplite)

target_link_libraries(your_target PRIVATE xcplite::xcplite)
```

Notes:

- `xcplite::xcplite` is provided by both consumption paths, so the link line is the same as with `find_package`. The plain target name `xcplite` also works in the FetchContent case.
- When consumed this way xcplite does not touch the consuming project's `CMAKE_INSTALL_PREFIX` or `CMAKE_<LANG>_FLAGS_<CONFIG>` and, with the default `XCPLITE_INSTALL=OFF`, adds no install rules. Set `-DXCPLITE_INSTALL=ON` to install xcplite together with your project.
- The library needs only a C compiler; the root project also enables C++ because the C++ examples and tests live in the same tree.
- For local development against a checked-out source tree, skip the clone with the standard override `-DFETCHCONTENT_SOURCE_DIR_XCPLITE=/path/to/XCPlite`.
- An application specific override header can be applied with `XCPLITE_CFG_OVERRIDE`, see [Application specific configuration override](#application-specific-configuration-override).
- The `rtos` configuration requires the consuming project to provide the FreeRTOS and lwIP headers and to define `_FREE_RTOS` on the `xcplite` target; see `examples/freertos_demo/freertos_emu_demo/CMakeLists.txt` for the pattern.

See `examples/fetchcontent_example/` for a complete standalone project.


### Building Standalone Examples Against the Installed Library

`silkit_demo` and `external_example` are standalone projects that consume an installed xcplite:

```bash
# external_example (any configuration)
cd examples/external_example
cmake -B build -S . -DCMAKE_PREFIX_PATH=../../build/install
cmake --build build --parallel

# silkit_demo (shm configuration recommended; also requires SilKit)
cd examples/silkit_demo
cmake -B build -S . \
    -DCMAKE_PREFIX_PATH=../../build-shm/install \
    -DSilKit_DIR=/path/to/SilKit/lib/cmake/SilKit
cmake --build build --parallel
```

See `examples/external_example/README.md` and `examples/silkit_demo/README.md` for details.

## Troubleshooting Compilation Issues

First of all, note that XCPlite requires C11 (and C++17 for C++ support).

The 64-bit lockless transmit queue (`queue64v.c`) requires `atomic_uint_least64_t`. On some ARM platforms, Clang emits calls to `__atomic_*` runtime helpers for 64-bit atomics and needs `-latomic`. CMakeLists.txt detects and links this library automatically. On 32-bit platforms, or when `OPTION_ATOMIC_EMULATION` is set, the build falls back to the mutex-based `queue32.c` automatically.

### Testing Different Compilers

Use `build.sh` with `CC`/`CXX` environment variables — the script passes them through to CMake explicitly, so the selection is always honoured even for existing build directories:

```bash
# Test with GCC
CC=gcc CXX=g++ ./build.sh clean

# Test with Clang
CC=clang CXX=clang++ ./build.sh clean
```

Or use raw CMake (note: a fresh build directory is required when switching compilers, because CMake caches the compiler in `CMakeCache.txt` and ignores `CC`/`CXX` on re-configures):

```bash
# Test with system default
cmake -B build -S . && cmake --build build

# Test with GCC (fresh directory)
rm -rf build && CC=gcc CXX=g++ cmake -B build -S . && cmake --build build

# Test with Clang (fresh directory)
rm -rf build && CC=clang CXX=clang++ cmake -B build -S . && cmake --build build
```

### Using build.sh for Diagnostics

`build.sh` is useful for checking which targets have build issues. Run without arguments to build library + examples with the default configuration:

```bash
./build.sh
```

If there are failures, copy & paste the complete output and provide it.

To test all configurations:

```bash
./build.sh tests                   # default config, all tests
./build.sh shm tools               # shm config, tools
./build.sh ptp tools               # ptp config, tools (Linux only)
./build.sh no_a2l examples         # no_a2l config
./build.sh rtos examples           # rtos config (Linux/macOS only)
./build.sh raw examples            # raw config (Linux only, see docs/SOCKET_RAW.md)
```

