
cmake_minimum_required(VERSION 3.14)

project(xcplite VERSION 2.1.0 LANGUAGES C CXX)

# Conditional building of examples based on project context
# When xcplite is the top-level project (standalone build), examples are built by default,
# UNLESS a dedicated demo option (e.g. XCPLITE_BUILD_FREERTOS_DEMO) is passed explicitly —
# in that case the default flips to OFF so only the requested demo is configured.
# When xcplite is included as a subproject (add_subdirectory / FetchContent),
# examples are always disabled by default.
#if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
#    # If the user passed -DXCPLITE_BUILD_FREERTOS_DEMO=ON on the command line, default
#    # the general examples flag to OFF so no other examples are configured.
#    if(DEFINED XCPLITE_BUILD_FREERTOS_DEMO AND XCPLITE_BUILD_FREERTOS_DEMO)
#        set(XCPLITE_BUILD_EXAMPLES_DEFAULT OFF)
#    else()
#        set(XCPLITE_BUILD_EXAMPLES_DEFAULT ON)
#    endif()
#else()
#    set(XCPLITE_BUILD_EXAMPLES_DEFAULT OFF)
# endif()

# =============================================================================
# Build options
# All XCPLITE_* options can be passed on the cmake command line:
#   cmake -B build -S . -D<OPTION>=ON|OFF
# =============================================================================
option(XCPLITE_BUILD_EXAMPLES      "Build xcplite example targets" OFF) # ${XCPLITE_BUILD_EXAMPLES_DEFAULT}
option(XCPLITE_BUILD_BPF_DEMO      "Build bpf_demo (requires libbpf; Linux only)" OFF)
option(XCPLITE_BUILD_TESTS         "Build xcplite test targets" OFF)
option(XCPLITE_BUILD_SHM_TOOLS     "Build tool targets: shmtool, xcpdaemon" OFF)
option(XCPLITE_BUILD_PTP_TOOLS     "Build tool targets: ptptool" OFF)
option(XCPLITE_BUILD_RUST_TOOLS    "Build Rust tool targets: xcpclient, bintool (requires cargo)" OFF)
option(XCPLITE_BUILD_NO_A2L_DEMO   "Build no_a2l_demo with no-A2L config xcplib_(no_a2l_cfg.h), use an isolated build directory" OFF)
option(XCPLITE_BUILD_FREERTOS_DEMO "Build freertos_demo with RTOS config xcplib_rtos_cfg.h (downloads FreeRTOS-Kernel via FetchContent)" OFF)

# Find required packages
find_package(Threads REQUIRED)

# Enable compile commands for VS Code IntelliSense (standalone builds only; not propagated to consumers)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Set default install prefix to build/install for local staging (avoid system installation)
# Users can override with: cmake -DCMAKE_INSTALL_PREFIX=/usr/local
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
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
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # For Release builds: use -O2 optimization and remove debug symbols and assertions
    set(CMAKE_C_FLAGS_RELEASE "-O2  -DNDEBUG" CACHE STRING "C flags for Release build" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2  -DNDEBUG" CACHE STRING "C++ flags for Release build" FORCE)
    # For RelWithDebInfo builds: release build with moderate optimization and with debug symbols, preserve stack frames
    # -fno-omit-frame-pointer: Always generate frame pointer for better stack traces
    # -fno-optimize-sibling-calls: Prevent tail call optimization that can confuse debuggers
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O1 -g -fno-omit-frame-pointer -fno-optimize-sibling-calls -DNDEBUG" CACHE STRING "C flags for RelWithDebInfo build" FORCE)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O1 -g -fno-omit-frame-pointer -fno-optimize-sibling-calls -DNDEBUG" CACHE STRING "C++ flags for RelWithDebInfo build" FORCE)
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
set(xcplite_SOURCES src/xcpappl.c src/xcplite.c src/xcpethserver.c src/xcpethtl.c src/queue32.c src/queue64v.c src/queue64f.c src/shm.c src/cal.c src/a2l.c src/a2l_writer.c src/persistence.c src/platform.c src/util.c )

# Create xcplite library
add_library(xcplite ${xcplite_SOURCES})

# Set include directories using generator expressions for proper installation
# BUILD_INTERFACE: Used when building within this project
# INSTALL_INTERFACE: Used when installed and consumed by external projects
target_include_directories(xcplite PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

# Apply compiler-specific flags
if(COMMON_WARNING_FLAGS)
    target_compile_options(xcplite PRIVATE ${COMMON_WARNING_FLAGS})
endif()

# Platform-specific compile definitions
if(WIN32)
    # Define _CRT_SECURE_NO_WARNINGS for Windows to suppress secure CRT warnings
    target_compile_definitions(xcplite PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # Define _GNU_SOURCE for Linux/Unix to make POSIX/BSD extensions available
    target_compile_definitions(xcplite PRIVATE _GNU_SOURCE)
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


if(XCPLITE_BUILD_EXAMPLES)
    # Example hello_xcp
    add_executable(hello_xcp examples/hello_xcp/src/main.c)
    target_link_libraries(hello_xcp PRIVATE xcplite)

    # Example no_a2l_demo (built without config override — A2L generator still compiled in)
    # To build with proper no-A2L configuration use: cmake -DXCPLITE_BUILD_NO_A2L_DEMO=ON
    add_executable(no_a2l_demo examples/no_a2l_demo/src/main.c)
    target_link_libraries(no_a2l_demo PRIVATE xcplite)

    # Example hello_xcp_cpp
    add_executable(hello_xcp_cpp examples/hello_xcp_cpp/src/main.cpp)
    target_link_libraries(hello_xcp_cpp PRIVATE xcplite)

    # Example point_cloud_demo
    add_executable(point_cloud_demo examples/point_cloud_demo/src/main.cpp)
    target_link_libraries(point_cloud_demo PRIVATE xcplite)

    # Example c_demo
    add_executable(c_demo examples/c_demo/src/main.c)
    target_link_libraries(c_demo PRIVATE xcplite)

    # Example cpp_demo
    add_executable(cpp_demo examples/cpp_demo/src/main.cpp examples/cpp_demo/src/sig_gen.cpp examples/cpp_demo/src/lookup.cpp)
    target_link_libraries(cpp_demo PRIVATE xcplite)

    # Example struct_demo
    add_executable(struct_demo examples/struct_demo/src/main.c)
    target_link_libraries(struct_demo PRIVATE xcplite)

    # Example multi_thread_demo
    add_executable(multi_thread_demo examples/multi_thread_demo/src/main.c)
    target_link_libraries(multi_thread_demo PRIVATE xcplite)

    # Example ptp4l_demo (Linux only)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(ptp4l_demo examples/ptp4l_demo/src/main.cpp)
        target_link_libraries(ptp4l_demo PRIVATE xcplite)
    endif()


    # Example bpf_demo (only build on Linux with libbpf available)
    # Pass -DXCPLITE_BUILD_BPF_DEMO=OFF to skip 
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
        message(STATUS "bpf_demo: skipped (XCPLITE_BUILD_BPF_DEMO=OFF)")
    endif()

endif()

# Tests
if(XCPLITE_BUILD_TESTS)

    # Type detection test (C version)
    add_executable(type_detection_test_c test/type_detection_test/src/main.c test/type_detection_test/src/c_version_test.c)
    target_include_directories(type_detection_test_c PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/inc")
    # target_link_libraries(type_detection_test_c PRIVATE xcplite)

    # Type detection test (C++ version)
    add_executable(type_detection_test_cpp test/type_detection_test/src/main.cpp test/type_detection_test/src/cpp_version_test.cpp)
    target_include_directories(type_detection_test_cpp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/inc")
    # target_link_libraries(type_detection_test_cpp PRIVATE xcplite)

    # A2L generation test (C version)
    add_executable(a2l_test test/a2l_test/src/main.c)
    target_link_libraries(a2l_test PRIVATE xcplite)

    # Calibration segment multi-threading test (C++ version)
    add_executable(cal_test test/cal_test/src/main.cpp)
    target_link_libraries(cal_test PRIVATE xcplite)

    # Data acquisition multi-threading test (C version)
    add_executable(daq_test test/daq_test/src/main.c)
    target_link_libraries(daq_test PRIVATE xcplite)

    # Clock synchronisation test
    add_executable(clock_test test/clock_test/src/main.cpp)
    target_link_libraries(clock_test PRIVATE xcplite)

    # Queue test (C version)   
    add_executable(queue_test test/queue_test/src/main.c)
    target_link_libraries(queue_test PRIVATE xcplite)
endif()

# PTP tools
if(XCPLITE_BUILD_PTP_TOOLS)

    # PTP tools (Linux only)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(ptptool_CPP_SOURCES tools/ptptool/src/main.cpp)
        set(ptptool_C_SOURCES tools/ptptool/src/ptp/ptp.c tools/ptptool/src/ptp/ptp_client.c tools/ptptool/src/ptp/ptp_observer.c tools/ptptool/src/ptp/ptp_master.c tools/ptptool/src/ptp/phc.c)
        set_source_files_properties(${ptptool_C_SOURCES} PROPERTIES LANGUAGE C)
        add_executable(ptptool ${ptptool_CPP_SOURCES} ${ptptool_C_SOURCES})
        target_link_libraries(ptptool PRIVATE xcplite)
        target_compile_definitions(xcplite PRIVATE "XCPLIB_CFG_OVERRIDE=\"xcplib_ptptool_cfg.h\"")
        target_compile_definitions(ptptool PRIVATE "XCPLIB_CFG_OVERRIDE=\"xcplib_ptptool_cfg.h\"")
        message(STATUS "Building ptptool with XCPLIB_CFG_OVERRIDE=\"xcplib_ptptool_cfg.h\" for hardware timestamping support (Linux only)")
    else()
        message(STATUS "Non Linux platform - skipping ptptool build")
    endif()

endif()

# SHM tools
if(XCPLITE_BUILD_SHM_TOOLS)

    # SHM tools (macOS + Linux only)
    if(NOT WIN32)

        # Tool shmtool
        add_executable(shmtool tools/shmtool/src/main.cpp)
        target_link_libraries(shmtool PRIVATE xcplite)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(shmtool PRIVATE rt)
        endif()
        message(STATUS "Building shmtool")

        # Tool xcpdaemon
        add_executable(xcpdaemon tools/xcpdaemon/src/main.c)
        target_link_libraries(xcpdaemon PRIVATE xcplite)
        message(STATUS "Building xcpdaemon")

    else()
        message(STATUS "Windows platform - skipping shmtool build")
    endif()


endif()

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

        # Install Rust tools system-wide via cargo install (to ~/.cargo/bin / $CARGO_HOME/bin)
        install(CODE "
            message(STATUS \"Installing xcpclient (cargo install)\")
            execute_process(
                COMMAND ${CARGO_EXECUTABLE} install --path \"${CMAKE_CURRENT_SOURCE_DIR}/tools/xcpclient\" --force
                RESULT_VARIABLE _cargo_result
            )
            if(NOT _cargo_result EQUAL 0)
                message(WARNING \"cargo install xcpclient failed\")
            endif()
            message(STATUS \"Installing bintool (cargo install)\")
            execute_process(
                COMMAND ${CARGO_EXECUTABLE} install --path \"${CMAKE_CURRENT_SOURCE_DIR}/tools/bintool\" --force
                RESULT_VARIABLE _cargo_result
            )
            if(NOT _cargo_result EQUAL 0)
                message(WARNING \"cargo install bintool failed\")
            endif()
        ")
    endif()
endif()

# no_a2l_demo: opt-in, independent of XCPLITE_BUILD_EXAMPLES.
# Applies XCPLIB_CFG_OVERRIDE=xcplib_no_a2l_cfg.h — disables A2L generator/upload.
# Incompatible with other examples that use A2L. Use an isolated build directory.
if(XCPLITE_BUILD_NO_A2L_DEMO)
    target_compile_definitions(xcplite PRIVATE "XCPLIB_CFG_OVERRIDE=\"xcplib_no_a2l_cfg.h\"")
    if(NOT TARGET no_a2l_demo)
        add_executable(no_a2l_demo examples/no_a2l_demo/src/main.c)
        target_link_libraries(no_a2l_demo PRIVATE xcplite)
    endif()
    message(STATUS "no_a2l_demo: enabled (XCPLIB_CFG_OVERRIDE=xcplib_no_a2l_cfg.h)")
endif()

# freertos_demo: opt-in, independent of XCPLITE_BUILD_EXAMPLES.
# Downloads FreeRTOS-Kernel via FetchContent. Requires macOS or Linux.
if(XCPLITE_BUILD_FREERTOS_DEMO)
    add_subdirectory(examples/freertos_demo)
endif()

# ============================================================================
# Installation Configuration
# ============================================================================

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

# Install xcplib_cfg.h for configuration options 
install(FILES src/xcplib_cfg.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Install platform.h for platform abstraction usage by consumers
install(FILES src/platform.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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
