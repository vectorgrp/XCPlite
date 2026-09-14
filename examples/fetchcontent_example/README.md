# FetchContent Example - Building xcplite from Source in Your Project

This example demonstrates how to consume **xcplite via CMake `FetchContent`**: your project names the xcplite git repository and a tag, and CMake clones and builds xcplite as part of your own build. There is no separate install step. This is the typical workflow for:

- **Version pinning in code** - the xcplite version is part of your `CMakeLists.txt`
- **Single build tree** - one `cmake -B build` builds xcplite and your application
- **CI builds** - no pre-installed xcplite on the build machine
- **Cross-compiling** - xcplite is compiled with the same toolchain and flags as your project

For the alternative, consuming a pre-built and installed xcplite with `find_package(xcplite)`, see [external_example](../external_example/).

## Project Structure

```
fetchcontent_example/
├── CMakeLists.txt          # Independent build configuration with FetchContent_Declare(xcplite ...)
├── build.sh                # Build script (optionally against the local working tree)
├── config/
│   └── xcplib_app_cfg.h    # Application specific xcplite configuration override
├── src/
│   ├── main.c              # Simple XCP example application for C
│   └── main.cpp            # Simple XCP example application for C++
└── README.md               # This file
```

## Quick Start

```bash
cd examples/fetchcontent_example
./build.sh
```

This clones xcplite from the repository and tag pinned in `CMakeLists.txt` into `build/_deps/xcplite-src/`, builds it, and builds the two example executables against it.

For testing by building against the xcplite source tree this example is part of (no clone, picks up local changes):

```bash
./test_build.sh local
```

Run:

```bash
./build/fetchcontent_example
./build/fetchcontent_example_cpp
```

## How It Works

### CMakeLists.txt

The relevant part of the `CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(xcplite
    GIT_REPOSITORY https://github.com/vectorgrp/XCPlite.git
    GIT_TAG        V2.2.2
    GIT_SHALLOW    TRUE
)

# xcplite build options, set before FetchContent_MakeAvailable()
set(XCPLITE_CONFIGURATION  "default")   
set(XCPLITE_BUILD_EXAMPLES OFF)
set(XCPLITE_BUILD_TESTS    OFF)
set(XCPLITE_CFG_OVERRIDE   "${CMAKE_CURRENT_SOURCE_DIR}/config/xcplib_app_cfg.h")

FetchContent_MakeAvailable(xcplite)

add_executable(fetchcontent_example src/main.c)
target_link_libraries(fetchcontent_example PRIVATE xcplite::xcplite)
```

`FetchContent_MakeAvailable` runs xcplite's own `CMakeLists.txt` as a subdirectory of this project. The `xcplite::xcplite` target is the same name the installed package exports, so the application part of the build file is identical to `external_example`. Linking it provides the include directories, the compile definitions of the selected configuration, and the dependencies (Threads, m, atomic where needed).

### What xcplite does and does not do as a subproject

When xcplite detects that it is not the top-level project:

- It does not change your `CMAKE_INSTALL_PREFIX`.
- It does not overwrite your `CMAKE_C_FLAGS_<CONFIG>` / `CMAKE_CXX_FLAGS_<CONFIG>`.
- It generates no install rules (`XCPLITE_INSTALL` defaults to `OFF`). Pass `-DXCPLITE_INSTALL=ON` if `cmake --install` of your project should install xcplite too.
- It does not enable `CMAKE_EXPORT_COMPILE_COMMANDS`.


### Application specific configuration override

The tunables of the library (`OPTION_*` in `src/xcplib_cfg.h`, documented in [xcplib_cfg.md](../../docs/xcplib_cfg.md)) are compile time settings. The shipped configurations (`no_a2l`, `ptp`, ...) are nothing more than override headers `src/xcplib_<name>_cfg.h` which `xcplib_cfg.h` includes at its end when the preprocessor symbol `XCPLIB_CFG_OVERRIDE` names them.

An application can provide such a header itself. This example does so with `config/xcplib_app_cfg.h`:

```cmake
set(XCPLITE_CFG_OVERRIDE "${CMAKE_CURRENT_SOURCE_DIR}/config/xcplib_app_cfg.h")
FetchContent_MakeAvailable(xcplite)
```

xcplite then defines `XCPLIB_CFG_OVERRIDE="xcplib_app_cfg.h"` and adds `config/` to the include path, both as PUBLIC usage requirements of the `xcplite` target. This is important, the application includes the same `xcplib_cfg.h` through `xcplib.h`, and struct layouts and macro expansions depend on the options, so library and application must be compiled with identical settings!


Rules:

- `XCPLITE_CFG_OVERRIDE` is only valid with `XCPLITE_CONFIGURATION "default"`. To build on a shipped configuration, just replicate it in your own configuration.
- Give the header a name distinct from the shipped configurations (`xcplib_cfg.h`, `xcplib_<name>_cfg.h`), since xcplite's own `src/` directory is searched first!
- The same variable works for a standalone library build (`cmake -B build -S . -DXCPLITE_CFG_OVERRIDE=/path/to/xcplib_app_cfg.h`); with `XCPLITE_INSTALL=ON` the header is installed next to `xcplib_cfg.h`, so `find_package` consumers get the identical configuration.

### Build types

`CMAKE_BUILD_TYPE` selects the build type as usual, e.g. `cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo`.

For `RelWithDebInfo`, the two example executables are additionally compiled with `-O1`, appended after the `-O2` of the build type. It keeps more local variables addressable in memory, which matters for the event triggers that measure locals directly on the stack (`DaqTriggerEvent`, `DaqEventVar`). The capture variants `DaqTriggerEventCapture`, `DaqTriggerEventCaptureAt` and `DaqCreateAndTriggerEventCapture` copy the variables into a struct which is always in memory, an application which captures all its locals can use the plain build type flags. The library has no measured locals and is built with the CMake defaults of every build type.

No frame pointer or tail call flags are needed. The functions which trigger events use `__builtin_frame_address()`, which forces a frame pointer in exactly these functions, and the trigger macros end with a compiler barrier (`XCP_NO_TAIL_CALL()`) so that a trigger as last statement of a function is not turned into a tail call.

### Overriding the source location

CMake's standard override lets you point FetchContent at an existing checkout instead of cloning. `./test_build.sh local` does exactly this:

```bash
cmake -B build -S . -DFETCHCONTENT_SOURCE_DIR_XCPLITE=/path/to/XCPlite
```

The repository and tag can also be changed on the command line without editing the file:

```bash
cmake -B build -S . -DXCPLITE_GIT_REPOSITORY=https://github.com/<fork>/XCPlite.git -DXCPLITE_GIT_TAG=V2.2.2
```

## See Also

- [Building XCPlite](../../docs/BUILDING.md) - Detailed build instructions, including the FetchContent section
- [external_example](../external_example/) - Same application, consuming an installed xcplite via `find_package`
- [hello_xcp](../hello_xcp/) - Basic XCP example built from the root project
