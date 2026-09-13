
cmake_minimum_required(VERSION 3.14)

project(xcplite VERSION 2.2.2 LANGUAGES C CXX)

# Check if xcplite is the top-level project, or is it consumed as a subproject (add_subdirectory / FetchContent)
# Settings which touch the global CMake cache (install prefix, per-build-type compiler flags, install rules) are applied only
# when top-level, so they do not leak into a consuming project.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_xcplite_is_top_level ON)
else()
    set(_xcplite_is_top_level OFF)
endif()

# =============================================================================
# Build configuration
# Selects which xcplib configuration override is compiled into the library.
# Configurations are mutually exclusive — use a separate build directory per configuration.
#
# Usage:
#   cmake -B build        -S . -DXCPLITE_CONFIGURATION=default   (or omit for default)
#   cmake -B build-no_a2l -S . -DXCPLITE_CONFIGURATION=no_a2l
#   cmake -B build-ptp    -S . -DXCPLITE_CONFIGURATION=ptp
#   cmake -B build-shm    -S . -DXCPLITE_CONFIGURATION=shm
#   cmake -B build-rtos   -S . -DXCPLITE_CONFIGURATION=rtos
#   cmake -B build-raw    -S . -DXCPLITE_CONFIGURATION=raw
#
# Configuration descriptions (see src/xcplib_<name>_cfg.h for details):
#   default  - 64-bit, on-target A2L generation, filesystem, Ethernet UDP/TCP
#   no_a2l   - Like default without on-target A2L; A2L generated from ELF by xcpclient
#   ptp      - Like default with socket hardware timestamps (ptptool, Linux only)
#   shm      - Shared-memory multi-application mode (shmtool, xcpdaemon)
#   rtos     - Embedded RTOS targets (FreeRTOS), reduced footprint, no filesystem
#   raw      - Raw Ethernet transport, UDP/IPv4 inside xcplib, no TCP/IP stack (Linux only)
# =============================================================================
# A consuming project (add_subdirectory / FetchContent) may set XCPLITE_CONFIGURATION as a
# normal variable before including xcplite. Honor it (CMP0077).
if(DEFINED XCPLITE_CONFIGURATION AND NOT DEFINED CACHE{XCPLITE_CONFIGURATION})
    set(_xcplite_cfg_default "${XCPLITE_CONFIGURATION}")
else()
    set(_xcplite_cfg_default "default")
endif()
set(XCPLITE_CONFIGURATION "${_xcplite_cfg_default}" CACHE STRING
    "Library build configuration: default | no_a2l | ptp | shm | rtos | raw")
set_property(CACHE XCPLITE_CONFIGURATION PROPERTY STRINGS default no_a2l ptp shm rtos raw)

if(NOT XCPLITE_CONFIGURATION MATCHES "^(default|no_a2l|ptp|shm|rtos|raw)$")
    message(FATAL_ERROR
        "Invalid XCPLITE_CONFIGURATION='${XCPLITE_CONFIGURATION}'. "
        "Must be one of: default, no_a2l, ptp, shm, rtos, raw")
endif()

# Resolve configuration override header (empty = use default xcplib_cfg.h)
if(XCPLITE_CONFIGURATION STREQUAL "no_a2l")
    set(_xcplib_cfg_override "xcplib_no_a2l_cfg.h")
elseif(XCPLITE_CONFIGURATION STREQUAL "ptp")
    set(_xcplib_cfg_override "xcplib_ptp_cfg.h")
elseif(XCPLITE_CONFIGURATION STREQUAL "shm")
    set(_xcplib_cfg_override "xcplib_shm_cfg.h")
elseif(XCPLITE_CONFIGURATION STREQUAL "rtos")
    set(_xcplib_cfg_override "xcplib_rtos_cfg.h")
elseif(XCPLITE_CONFIGURATION STREQUAL "raw")
    set(_xcplib_cfg_override "xcplib_raw_cfg.h")
else()
    set(_xcplib_cfg_override "")
endif()

# Application specific configuration override header (XCPLITE_CFG_OVERRIDE)
# A path to a header which is included at the end of src/xcplib_cfg.h (XCPLIB_CFG_OVERRIDE) and
# may #undef/#define any OPTION_* default, like the shipped src/xcplib_<name>_cfg.h files do.
# Only valid with XCPLITE_CONFIGURATION=default; to build on a shipped configuration, #include
# its header (e.g. "xcplib_no_a2l_cfg.h") at the top of the custom header.
# Like XCPLITE_CONFIGURATION, a normal variable set by a consuming project is honored.
if(DEFINED XCPLITE_CFG_OVERRIDE AND NOT DEFINED CACHE{XCPLITE_CFG_OVERRIDE})
    set(_xcplite_cfg_override_init "${XCPLITE_CFG_OVERRIDE}")
else()
    set(_xcplite_cfg_override_init "")
endif()
set(XCPLITE_CFG_OVERRIDE "${_xcplite_cfg_override_init}" CACHE FILEPATH
    "Application specific configuration override header, applied on top of src/xcplib_cfg.h (XCPLITE_CONFIGURATION=default only)")

set(_xcplib_cfg_override_path "") # full path of the override header to install (shipped or custom)
set(_xcplib_cfg_override_dir "")  # include directory of a custom override header
if(XCPLITE_CFG_OVERRIDE)
    if(NOT XCPLITE_CONFIGURATION STREQUAL "default")
        message(FATAL_ERROR
            "XCPLITE_CFG_OVERRIDE requires XCPLITE_CONFIGURATION=default (got '${XCPLITE_CONFIGURATION}'). "
            "To build on a shipped configuration, #include its header (e.g. \"xcplib_${XCPLITE_CONFIGURATION}_cfg.h\") "
            "at the top of your override header.")
    endif()
    get_filename_component(_xcplib_cfg_override_path "${XCPLITE_CFG_OVERRIDE}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    if(NOT EXISTS "${_xcplib_cfg_override_path}")
        message(FATAL_ERROR "XCPLITE_CFG_OVERRIDE='${XCPLITE_CFG_OVERRIDE}' not found (resolved to '${_xcplib_cfg_override_path}')")
    endif()
    get_filename_component(_xcplib_cfg_override "${_xcplib_cfg_override_path}" NAME)
    get_filename_component(_xcplib_cfg_override_dir "${_xcplib_cfg_override_path}" DIRECTORY)
    if(_xcplib_cfg_override MATCHES "^xcplib_(no_a2l_|ptp_|shm_|rtos_|raw_)?cfg\\.h$")
        message(FATAL_ERROR
            "XCPLITE_CFG_OVERRIDE '${_xcplib_cfg_override}' has the name of a header shipped in src/, "
            "which is searched first. Use a distinct name, e.g. xcplib_app_cfg.h")
    endif()
    message(STATUS "xcplite configuration: ${XCPLITE_CONFIGURATION}  (application override XCPLIB_CFG_OVERRIDE=\"${_xcplib_cfg_override}\" from ${_xcplib_cfg_override_dir})")
elseif(_xcplib_cfg_override)
    set(_xcplib_cfg_override_path "${CMAKE_CURRENT_SOURCE_DIR}/src/${_xcplib_cfg_override}")
    message(STATUS "xcplite configuration: ${XCPLITE_CONFIGURATION}  (XCPLIB_CFG_OVERRIDE=\"${_xcplib_cfg_override}\")")
else()
    message(STATUS "xcplite configuration: ${XCPLITE_CONFIGURATION}  (using default xcplib_cfg.h)")
endif()


# =============================================================================
# Build options
# Control what to build within the selected configuration.
# All XCPLITE_BUILD_* options can be passed on the cmake command line:
#   cmake -B build -S . -D<OPTION>=ON|OFF
#
# Available targets depend on the active XCPLITE_CONFIGURATION:
#   XCPLITE_BUILD_EXAMPLES
#     default  -> hello_xcp, hello_xcp_cpp, c_demo, cpp_demo, point_cloud_demo,
#                 struct_demo, multi_thread_demo, ptp4l_demo (Linux)
#     no_a2l   -> no_a2l_demo, no_a2l_demo_cpp
#     ptp      -> ptp4l_demo (Linux)
#     shm      -> hello_xcp (SHM mode), hello_xcp_cpp (SHM mode)
#                Note: silkit_demo is a standalone project (requires SilKit + installed xcplite);
#                build it separately from examples/silkit_demo/ after installing xcplite.
#                external_example is also a standalone project; see examples/external_example/
#     rtos     -> freertos_demo (downloads FreeRTOS-Kernel via FetchContent)
#   XCPLITE_BUILD_TESTS
#     default  -> a2l_test, cal_test, daq_test, daq_config_test, clock_test, queue_test, type_detection_test_*
#     ptp      -> clock_test
#     others   -> (none)
#   XCPLITE_BUILD_TOOLS
#     ptp      -> ptptool (Linux only)
#     shm      -> shmtool, xcpdaemon
#     others   -> (none)
#   XCPLITE_BUILD_RUST_TOOLS  (any configuration)
#                -> xcpclient, bintool  (requires cargo)
#   XCPLITE_BUILD_BPF_DEMO  (default configuration, Linux only)
#                -> bpf_demo  (requires libbpf; set OFF to skip the slow find_library probe)
#   XCPLITE_INSTALL  (any configuration)
#                -> generate install/export rules; ON when xcplite is the top-level
#                   project, OFF when consumed via add_subdirectory/FetchContent
#   XCPLITE_CFG_OVERRIDE  (configuration 'default' only)
#                -> path to an application specific configuration override header,
#                   applied on top of src/xcplib_cfg.h (see examples/fetchcontent_example)
#
# To consume xcplite from another CMake project see docs/BUILDING.md
# =============================================================================
option(XCPLITE_BUILD_EXAMPLES   "Build example targets for the selected configuration" OFF)
option(XCPLITE_BUILD_TESTS      "Build test targets for the selected configuration" OFF)
option(XCPLITE_BUILD_TOOLS      "Build tool targets for the selected configuration" OFF)
option(XCPLITE_BUILD_RUST_TOOLS "Build Rust tool targets: xcpclient, bintool (requires cargo)" OFF)
option(XCPLITE_BUILD_BPF_DEMO   "Build bpf_demo (default configuration, Linux only; requires libbpf)" OFF)
option(XCPLITE_INSTALL          "Generate install rules for xcplite (default: ON when top-level, OFF when consumed via add_subdirectory/FetchContent)" ${_xcplite_is_top_level})

# Find required packages
find_package(Threads REQUIRED)

# Enable compile commands for VS Code IntelliSense (standalone builds only; not propagated to consumers)
if(_xcplite_is_top_level)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Set default install prefix to build/install for local staging (avoid system installation)
# Users can override with: cmake -DCMAKE_INSTALL_PREFIX=/usr/local
# Top-level only: a consuming project owns its install prefix.
if(_xcplite_is_top_level AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install prefix" FORCE)
    message(STATUS "CMAKE_INSTALL_PREFIX not set, using local staging: ${CMAKE_INSTALL_PREFIX}")
endif()

# Set C and C++ standards globally
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Set C++ standard based on platform
if(WIN32)
    set(CMAKE_CXX_STANDARD 20)
    message(STATUS "Using C++20 for Windows")
else()
    set(CMAKE_CXX_STANDARD 17)
    message(STATUS "Using C++17 for non-Windows platforms")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform detection for library selection
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "64-bit platform")
else()
    message(STATUS "32-bit platform")
endif()

# Platform-specific settings
if(WIN32)
   message(STATUS "Building for Windows")
elseif(APPLE)
   message(STATUS "Building for macOS")
elseif(UNIX)
    message(STATUS "Building for Unix/Linux")
endif()

# Compiler-specific settings for better compatibility
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Enable additional warnings for GCC and Clang
    set(COMMON_WARNING_FLAGS -pedantic -Wall)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        message(STATUS "Using GCC compiler")
    elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        message(STATUS "Using Clang compiler")
    endif()
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    message(STATUS "Using MSVC compiler")
    set(COMMON_WARNING_FLAGS /W3)
endif()

# Custom build type configurations for debug symbols in release builds
# Top-level only: these overwrite the global CMAKE_<LANG>_FLAGS_<CONFIG> cache entries,
# which must stay under the control of a consuming project.
if(NOT _xcplite_is_top_level)
    # keep the consumer's flags
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # For Release builds: use -O2 optimization and remove debug symbols and assertions
    set(CMAKE_C_FLAGS_RELEASE "-O2  -DNDEBUG" CACHE STRING "C flags for Release build" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2  -DNDEBUG" CACHE STRING "C++ flags for Release build" FORCE)
    # For RelWithDebInfo builds: release build with moderate optimization and with debug symbols
    # -O1 keeps more local variables addressable in memory than -O2, for stack relative measurement
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O1 -g -DNDEBUG" CACHE STRING "C flags for RelWithDebInfo build" FORCE)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O1 -g -DNDEBUG" CACHE STRING "C++ flags for RelWithDebInfo build" FORCE)
    # For Debug builds: no optimization, full debug symbols
    set(CMAKE_C_FLAGS_DEBUG "-O0 -g" CACHE STRING "C flags for Debug build" FORCE)
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g" CACHE STRING "C++ flags for Debug build" FORCE)
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    # MSVC equivalent settings
    set(CMAKE_C_FLAGS_RELEASE "/O2 /Zi /DNDEBUG" CACHE STRING "C flags for Release build" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Zi /DNDEBUG" CACHE STRING "C++ flags for Release build" FORCE)
    # /Oy- disables frame pointer omission (equivalent to -fno-omit-frame-pointer)
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "/O2 /Zi /Oy- /DNDEBUG" CACHE STRING "C flags for RelWithDebInfo build" FORCE)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/O2 /Zi /Oy- /DNDEBUG" CACHE STRING "C++ flags for RelWithDebInfo build" FORCE)
    set(CMAKE_C_FLAGS_DEBUG "/Od /Zi" CACHE STRING "C flags for Debug build" FORCE)
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi" CACHE STRING "C++ flags for Debug build" FORCE)
endif()

# xcplite sources
set(xcplite_SOURCES src/xcpappl.c src/xcplite.c src/xcpethserver.c src/xcpethtl.c src/queue32m.c src/queue32.c src/queue64v.c src/queue64f.c src/shm.c src/cal.c src/a2l.c src/a2l_writer.c src/persistence.c src/platform.c src/sockets.c src/socket_raw.c src/socket_raw_hal_linux.c src/util.c )

# Create xcplite library
add_library(xcplite ${xcplite_SOURCES})

# Namespaced alias so that consumers can link xcplite::xcplite regardless of whether
# xcplite comes from an installed package (find_package) or from the source tree
# (add_subdirectory / FetchContent).
add_library(xcplite::xcplite ALIAS xcplite)

# Publish the selected build configuration to consumers of xcplite::xcplite.
# This becomes a preprocessor define for downstream C/C++ targets.
target_compile_definitions(xcplite PUBLIC "XCPLITE_CONFIGURATION=\"${XCPLITE_CONFIGURATION}\"")

# Apply configuration override (empty = use default xcplib_cfg.h)
if(_xcplib_cfg_override)
    target_compile_definitions(xcplite PUBLIC "XCPLIB_CFG_OVERRIDE=\"${_xcplib_cfg_override}\"")
endif()

# Set include directories using generator expressions for proper installation
# BUILD_INTERFACE: Used when building within this project
# INSTALL_INTERFACE: Used when installed and consumed by external projects
target_include_directories(xcplite PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

# A custom override header (XCPLITE_CFG_OVERRIDE) lives outside the xcplite tree: make its
# directory visible to the library and to consumers in the build tree. The installed copy
# lands in include/ next to xcplib_cfg.h (see the install rules below).
if(_xcplib_cfg_override_dir)
    target_include_directories(xcplite PUBLIC $<BUILD_INTERFACE:${_xcplib_cfg_override_dir}>)
endif()

# Apply compiler-specific flags
if(COMMON_WARNING_FLAGS)
    target_compile_options(xcplite PRIVATE ${COMMON_WARNING_FLAGS})
endif()

# Platform-specific compile definitions
if(WIN32)
    # Define _CRT_SECURE_NO_WARNINGS for Windows to suppress secure CRT warnings
    target_compile_definitions(xcplite PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # _GNU_SOURCE: Linux only, propagated PUBLIC so CMake consumers of xcplite::xcplite
    # get it automatically without having to set it themselves.
    # macOS/QNX do not need it; Windows is already excluded by the if(WIN32) above.
    target_compile_definitions(xcplite PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
endif()

# Link threads library to xcplite (needed for pthread usage in platform.h)
target_link_libraries(xcplite PUBLIC Threads::Threads)

# Link math library on Unix/Linux/macOS (needed for math functions)
if(UNIX)
    target_link_libraries(xcplite PUBLIC m)
endif()

# Check for atomic library support (needed on some ARM platforms with clang)
include(CheckLibraryExists)
check_library_exists(atomic __atomic_store_4 "" HAVE_LIBATOMIC)
if(HAVE_LIBATOMIC)
    message(STATUS "Linking with atomic library for C11 atomics support")
    target_link_libraries(xcplite PUBLIC atomic)
endif()


# =============================================================================
# Examples
# Available examples depend on the active XCPLITE_CONFIGURATION.
# =============================================================================
if(XCPLITE_BUILD_EXAMPLES)

    if(XCPLITE_CONFIGURATION STREQUAL "default")

        add_executable(hello_xcp examples/hello_xcp/src/main.c)
        target_link_libraries(hello_xcp PRIVATE xcplite)

        add_executable(hello_xcp_cpp examples/hello_xcp_cpp/src/main.cpp)
        target_link_libraries(hello_xcp_cpp PRIVATE xcplite)

        add_executable(point_cloud_demo examples/point_cloud_demo/src/main.cpp)
        target_link_libraries(point_cloud_demo PRIVATE xcplite)

        add_executable(c_demo examples/c_demo/src/main.c)
        target_link_libraries(c_demo PRIVATE xcplite)

        add_executable(cpp_demo examples/cpp_demo/src/main.cpp examples/cpp_demo/src/sig_gen.cpp examples/cpp_demo/src/lookup.cpp)
        target_link_libraries(cpp_demo PRIVATE xcplite)

        add_executable(struct_demo examples/struct_demo/src/main.c)
        target_link_libraries(struct_demo PRIVATE xcplite)

        add_executable(multi_thread_demo examples/multi_thread_demo/src/main.c)
        target_link_libraries(multi_thread_demo PRIVATE xcplite)

        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_executable(ptp4l_demo examples/ptp4l_demo/src/main.cpp)
            target_link_libraries(ptp4l_demo PRIVATE xcplite)
        endif()

        if(XCPLITE_BUILD_BPF_DEMO AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_library(LIBBPF_LIBRARY bpf)
            if(LIBBPF_LIBRARY)
                add_executable(bpf_demo examples/bpf_demo/src/main.c)
                target_link_libraries(bpf_demo PRIVATE xcplite ${LIBBPF_LIBRARY})
                message(STATUS "Found libbpf: ${LIBBPF_LIBRARY} - building bpf_demo")
            else()
                message(STATUS "libbpf not found - skipping bpf_demo")
            endif()
        else()
            message(STATUS "bpf_demo: skipped (XCPLITE_BUILD_BPF_DEMO=OFF or not Linux)")
        endif()

    elseif(XCPLITE_CONFIGURATION STREQUAL "no_a2l")

        # A2L is generated externally from ELF by xcpclient; no on-target A2L generation
        add_executable(no_a2l_demo examples/no_a2l_demo/src/main.c)
        target_link_libraries(no_a2l_demo PRIVATE xcplite)
        message(STATUS "no_a2l_demo: building with XCPLIB_CFG_OVERRIDE=\"xcplib_no_a2l_cfg.h\"")

        add_executable(no_a2l_demo_cpp examples/no_a2l_demo_cpp/src/main.cpp)
        target_link_libraries(no_a2l_demo_cpp PRIVATE xcplite)
        message(STATUS "no_a2l_demo_cpp: building with XCPLIB_CFG_OVERRIDE=\"xcplib_no_a2l_cfg.h\"")

    elseif(XCPLITE_CONFIGURATION STREQUAL "ptp")

        # Requires Linux with a PTP-capable network interface and ptp4l running
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_executable(ptp4l_demo examples/ptp4l_demo/src/main.cpp)
            target_link_libraries(ptp4l_demo PRIVATE xcplite)
            message(STATUS "ptp4l_demo: building for ptp configuration (Linux)")
        else()
            message(STATUS "ptp4l_demo: skipped (Linux only)")
        endif()

    elseif(XCPLITE_CONFIGURATION STREQUAL "shm")

        # Shared-memory multi-application mode
        add_executable(hello_xcp examples/hello_xcp/src/main.c)
        target_link_libraries(hello_xcp PRIVATE xcplite)

        add_executable(hello_xcp_cpp examples/hello_xcp_cpp/src/main.cpp)
        target_link_libraries(hello_xcp_cpp PRIVATE xcplite)

        # silkit_demo is a standalone project requiring SilKit + an installed xcplite.
        # Build it separately: cd examples/silkit_demo && cmake -B build -DSilKit_DIR=... -Dxcplite_DIR=...
        message(STATUS "silkit_demo: not built from root (standalone project, see examples/silkit_demo/)")

    elseif(XCPLITE_CONFIGURATION STREQUAL "raw")

        # udp_raw_demo: XCP over the raw Ethernet transport (UDP/IPv4 inside xcplib).
        # Linux only - the HAL backend uses AF_PACKET and needs CAP_NET_RAW.
        # See docs/SOCKET_RAW.md for the network setup used to test it.
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_executable(udp_raw_demo examples/udp_raw_demo/src/main.c)
            target_link_libraries(udp_raw_demo PRIVATE xcplite)
            message(STATUS "udp_raw_demo: building for raw configuration (Linux)")
        else()
            message(STATUS "udp_raw_demo: skipped (Linux only, raw Ethernet HAL uses AF_PACKET)")
        endif()

    elseif(XCPLITE_CONFIGURATION STREQUAL "rtos")

        # freertos_demo: FreeRTOS POSIX simulator for testing FreeRTOS xcplite support on the host.
        # Linux only. Downloads FreeRTOS-Kernel via FetchContent (requires network access at configure time).
        # Note: The embedded FreeRTOS examples for eps32 and stm32 are build externally
        if(NOT WIN32)
            add_subdirectory(examples/freertos_demo/freertos_emu_demo)
            message(STATUS "freertos_emu_demo: building with FreeRTOS-Kernel (FetchContent)")
        else()
            message(STATUS "freertos_emu_demo: skipped (not supported on Windows)")
        endif()

    endif()

endif() # XCPLITE_BUILD_EXAMPLES


# =============================================================================
# Tests
# Available tests depend on the active XCPLITE_CONFIGURATION.
# =============================================================================
if(XCPLITE_BUILD_TESTS)

    if(XCPLITE_CONFIGURATION STREQUAL "default")

        # Type detection tests
        add_executable(type_detection_test_c test/type_detection_test/src/main.c test/type_detection_test/src/c_version_test.c)
        target_link_libraries(type_detection_test_c PRIVATE xcplite)

        add_executable(type_detection_test_cpp test/type_detection_test/src/main.cpp test/type_detection_test/src/cpp_version_test.cpp)
        target_link_libraries(type_detection_test_cpp PRIVATE xcplite)

        add_executable(a2l_test test/a2l_test/src/main.c)
        target_link_libraries(a2l_test PRIVATE xcplite)

        add_executable(cal_test test/cal_test/src/main.cpp)
        target_link_libraries(cal_test PRIVATE xcplite)

        add_executable(daq_test test/daq_test/src/main.c)
        target_link_libraries(daq_test PRIVATE xcplite)

        add_executable(daq_config_test test/daq_config_test/src/main.c test/daq_config_test/src/xcptl_stub.c)
        target_link_libraries(daq_config_test PRIVATE xcplite)

        add_executable(clock_test test/clock_test/src/main.cpp)
        target_link_libraries(clock_test PRIVATE xcplite)

        add_executable(queue_test test/queue_test/src/main.c)
        target_link_libraries(queue_test PRIVATE xcplite)

        add_executable(xcp_test test/xcp_test/src/main.cpp)
        if(WIN32)
            target_link_libraries(xcp_test PRIVATE ws2_32)
        endif()

    elseif(XCPLITE_CONFIGURATION STREQUAL "ptp")

        # Clock synchronization test with hardware timestamps
        add_executable(clock_test test/clock_test/src/main.cpp)
        target_link_libraries(clock_test PRIVATE xcplite)
        message(STATUS "clock_test: building for ptp configuration")

    elseif(XCPLITE_CONFIGURATION STREQUAL "raw")

        # Unit tests for the raw Ethernet transport: checksums, wire layout, frame build,
        # receive filter, ARP and ICMP responders. No network and no HAL involved -
        # src/socket_raw.c is compiled into the test with a fake Ethernet HAL (src/stubs.c),
        # so this does not link against xcplite and needs no privileges.
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_executable(socket_raw_test test/socket_raw_test/src/main.c test/socket_raw_test/src/stubs.c)
            target_include_directories(socket_raw_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/inc")
            # This target deliberately does not link xcplite (it supplies its own fake
            # Ethernet HAL), so it does not inherit the PUBLIC _GNU_SOURCE from that target
            target_compile_definitions(socket_raw_test PRIVATE
                "XCPLITE_CONFIGURATION=\"raw\""
                "XCPLIB_CFG_OVERRIDE=\"xcplib_raw_cfg.h\""
                $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>
            )
            message(STATUS "socket_raw_test: building for raw configuration")
        else()
            message(STATUS "socket_raw_test: skipped (Linux only, raw Ethernet HAL uses AF_PACKET)")
        endif()

    else()
        message(STATUS "XCPLITE_BUILD_TESTS=ON: no test targets defined for configuration '${XCPLITE_CONFIGURATION}'")
    endif()

endif() # XCPLITE_BUILD_TESTS


# =============================================================================
# Tools
# Available tools depend on the active XCPLITE_CONFIGURATION.
# =============================================================================
if(XCPLITE_BUILD_TOOLS)

    if(XCPLITE_CONFIGURATION STREQUAL "ptp")

        # ptptool: hardware timestamping tool for PTP synchronization (Linux only)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(ptptool_CPP_SOURCES tools/ptptool/src/main.cpp)
            set(ptptool_C_SOURCES tools/ptptool/src/ptp/ptp.c tools/ptptool/src/ptp/ptp_client.c tools/ptptool/src/ptp/ptp_observer.c tools/ptptool/src/ptp/ptp_master.c tools/ptptool/src/ptp/phc.c)
            set_source_files_properties(${ptptool_C_SOURCES} PROPERTIES LANGUAGE C)
            add_executable(ptptool ${ptptool_CPP_SOURCES} ${ptptool_C_SOURCES})
            target_link_libraries(ptptool PRIVATE xcplite)
            # ptptool's own C sources include platform.h and need OPTION_SOCKET_HW_TIMESTAMPS
            # (defined by xcplib_ptp_cfg.h) to see socketGetSendTime, socketBindToDevice, etc.
            target_compile_definitions(ptptool PRIVATE
                "XCPLIB_CFG_OVERRIDE=\"xcplib_ptp_cfg.h\""
                "OPTION_ENABLE_PTP"
            )
            message(STATUS "ptptool: building for ptp configuration (Linux)")
        else()
            message(STATUS "ptptool: skipped (Linux only)")
        endif()

    elseif(XCPLITE_CONFIGURATION STREQUAL "shm")

        # shmtool, xcpdaemon: shared-memory tools (macOS + Linux only)
        if(NOT WIN32)
            add_executable(shmtool tools/shmtool/src/main.cpp)
            target_link_libraries(shmtool PRIVATE xcplite)
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                target_link_libraries(shmtool PRIVATE rt)
            endif()
            message(STATUS "Building shmtool")

            add_executable(xcpdaemon tools/xcpdaemon/src/main.c)
            target_link_libraries(xcpdaemon PRIVATE xcplite)
            message(STATUS "Building xcpdaemon")
        else()
            message(STATUS "shmtool, xcpdaemon: skipped (Windows not supported)")
        endif()

    else()
        message(STATUS "XCPLITE_BUILD_TOOLS=ON: no tool targets defined for configuration '${XCPLITE_CONFIGURATION}' (use ptp or shm)")
    endif()

endif() # XCPLITE_BUILD_TOOLS

# Rust tools: xcpclient, bintool (built with cargo)
if(XCPLITE_BUILD_RUST_TOOLS)
    find_program(CARGO_EXECUTABLE cargo)
    if(NOT CARGO_EXECUTABLE)
        message(WARNING "cargo not found — XCPLITE_BUILD_RUST_TOOLS=ON but cargo is not in PATH; skipping Rust tools")
    else()
        # Build args: add --release for Release/RelWithDebInfo, otherwise debug (default)
        set(_cargo_args build)
        if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
            list(APPEND _cargo_args --release)
        endif()

        add_custom_target(xcpclient ALL
            COMMAND ${CARGO_EXECUTABLE} ${_cargo_args}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tools/xcpclient"
            COMMENT "Building xcpclient (Rust)"
            VERBATIM
        )
        message(STATUS "Building xcpclient (Rust, cargo)")

        add_custom_target(bintool ALL
            COMMAND ${CARGO_EXECUTABLE} ${_cargo_args}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tools/bintool"
            COMMENT "Building bintool (Rust)"
            VERBATIM
        )
        message(STATUS "Building bintool (Rust, cargo)")
        # Note: 'cmake --install' does NOT run 'cargo install' for these tools.
        # Use 'build.sh rust_tools cargo_install' or run 'cargo install --path tools/xcpclient'
        # and 'cargo install --path tools/bintool' manually to install to ~/.cargo/bin.
    endif()
endif() # XCPLITE_BUILD_RUST_TOOLS

# ============================================================================
# Installation Configuration
# Skipped when XCPLITE_INSTALL=OFF (default when consumed via FetchContent), so
# 'cmake --install' of a consuming project does not install xcplite as a side effect.
# ============================================================================
if(XCPLITE_INSTALL)

include(GNUInstallDirs)

# Install library
install(TARGETS xcplite
    EXPORT xcpliteTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install public headers
install(DIRECTORY inc/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" )

# Install xcplib_cfg.h (default configuration) and platform.h
install(FILES src/xcplib_cfg.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES src/platform.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES src/sockets.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# Raw Ethernet HAL interface, needed by applications which provide their own backend
# (OPTION_UDP_RAW_HAL_EXTERNAL), e.g. an ASAM CMP implementation
install(FILES src/socket_raw_hal.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Install the active configuration override header (if not default)
# This documents which configuration was compiled into the installed library.
if(_xcplib_cfg_override_path)
    install(FILES "${_xcplib_cfg_override_path}" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    message(STATUS "Installing configuration override header: ${_xcplib_cfg_override_path}")
endif()

# Install CMake package configuration files
install(EXPORT xcpliteTargets
    FILE xcpliteTargets.cmake
    NAMESPACE xcplite::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xcplite
)

# Generate and install package config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xcpliteConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/xcpliteConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xcplite
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xcpliteConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/xcpliteConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xcpliteConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xcplite
)

message(STATUS "xcplite will be installed to: ${CMAKE_INSTALL_PREFIX}")

else()
    message(STATUS "xcplite install rules disabled (XCPLITE_INSTALL=OFF)")
endif() # XCPLITE_INSTALL
