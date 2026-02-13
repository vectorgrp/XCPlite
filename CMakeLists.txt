
cmake_minimum_required(VERSION 3.10)

# Compiler selection options (must be set before project() call)
option(USE_CLANG "Use clang/clang++ compiler" OFF)
option(USE_GCC "Use gcc/g++ compiler" OFF)

# Set compilers based on options (must be set before project() call)
if(USE_CLANG AND USE_GCC)
    message(FATAL_ERROR "Cannot specify both USE_CLANG and USE_GCC. Choose one or neither.")
elseif(USE_CLANG)
    message(STATUS "Forcing clang compiler")
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
elseif(USE_GCC)
    message(STATUS "Forcing GCC compiler")
    set(CMAKE_C_COMPILER gcc)
    set(CMAKE_CXX_COMPILER g++)
else()
    message(STATUS "Using default system compiler")
endif()

project(xcplite VERSION 1.2.0 LANGUAGES C CXX)

# Option to build examples when xcplite is a subproject
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(XCPLITE_BUILD_EXAMPLES_DEFAULT ON)
else()
    set(XCPLITE_BUILD_EXAMPLES_DEFAULT OFF)
endif()
option(XCPLITE_BUILD_EXAMPLES "Build xcplite example targets" ${XCPLITE_BUILD_EXAMPLES_DEFAULT})

# Find required packages
find_package(Threads REQUIRED)

# Enable compile commands for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# Enable test targets
if(NOT DEFINED BUILD_TEST)
    set(BUILD_TEST FALSE)
endif()
if(BUILD_TEST)
    message(STATUS "Build test targets enabled")    
endif()

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
set(xcplite_SOURCES src/xcpAppl.c src/xcpLite.c src/xcpEthServer.c src/xcpEthTl.c src/xcpQueue32.c src/xcpQueue64.c src/a2l.c src/persistence.c src/platform.c)   

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

    # Example no_a2l_demo
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

    # Example ptp4l_demo
    add_executable(ptp4l_demo examples/ptp4l_demo/src/main.cpp)
    target_link_libraries(ptp4l_demo PRIVATE xcplite)


    # Example bpf_demo (only build on Linux with libbpf available)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_library(LIBBPF_LIBRARY bpf)
        if(LIBBPF_LIBRARY)
            add_executable(bpf_demo examples/bpf_demo/src/main.c)
            target_link_libraries(bpf_demo PRIVATE xcplite ${LIBBPF_LIBRARY})
            message(STATUS "Found libbpf: ${LIBBPF_LIBRARY} - building bpf_demo")
        else()
            message(STATUS "libbpf not found - skipping bpf_demo")
        endif()
    endif()

    # Tool ptptool (main.cpp as C++, rest as C)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(ptptool_CPP_SOURCES tools/ptptool/src/main.cpp)
        set(ptptool_C_SOURCES tools/ptptool/src/ptp/ptp.c tools/ptptool/src/ptp/ptp_client.c tools/ptptool/src/ptp/ptp_observer.c tools/ptptool/src/ptp/ptp_master.c tools/ptptool/src/ptp/filter.c tools/ptptool/src/ptp/phc.c)
        set_source_files_properties(${ptptool_C_SOURCES} PROPERTIES LANGUAGE C)
        add_executable(ptptool ${ptptool_CPP_SOURCES} ${ptptool_C_SOURCES})
        target_link_libraries(ptptool PRIVATE xcplite)
    else()
        message(STATUS "Non Linux platform - skipping ptptool build")
    endif()
endif()

# Tests
if(BUILD_TEST)

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
install(DIRECTORY inc/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Install platform.h for platform abstraction usage by consumers
install(FILES src/platform.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

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
