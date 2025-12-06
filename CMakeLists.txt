
cmake_minimum_required(VERSION 3.10)

# Compiler selection options (must be set before project() call)
option(USE_CLANG "Use clang/clang++ compiler" OFF)
option(USE_GCC "Use gcc/g++ compiler" OFF)

# Set compilers based on options (before project call)
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

project(xcplib VERSION 0.9.2 LANGUAGES C CXX)

# Find required packages
find_package(Threads REQUIRED)


# Set default generator for Windows to Visual Studio 2022
if(WIN32 AND NOT DEFINED CMAKE_GENERATOR)
    set(CMAKE_GENERATOR "Visual Studio 17 2022")
endif()

# Enable compile commands for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# Enable test
if(NOT DEFINED BUILD_TEST)
    set(BUILD_TEST TRUE)
endif()
if(BUILD_TEST)
    message(STATUS "Build test targets enabled")    
endif()

# Platform detection for library selection
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "64-bit platform")
    set(PLATFORM_64BIT TRUE)
else()
    message(STATUS "32-bit platform")
    set(PLATFORM_32BIT TRUE)
endif()

# Platform-specific settings
if(WIN32)
   message(STATUS "Building for Windows")
   set(WINDOWS TRUE)
elseif(APPLE)
   message(STATUS "Building for macOS")
   set(MACOS TRUE)
elseif(UNIX)
    message(STATUS "Building for Unix/Linux")
    set(LINUX TRUE)
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

# xcplib sources
set(xcplib_SOURCES src/xcpAppl.c src/xcpLite.c src/xcpEthServer.c src/xcpEthTl.c src/xcpQueue32.c src/xcpQueue64.c src/a2l.c src/persistence.c src/platform.c)   

set(PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR})
message(STATUS "PROJECT_ROOT is: ${PROJECT_ROOT}")

# Create xcplib library
set_source_files_properties(${xcplib_SOURCES} PROPERTIES LANGUAGE C)
add_library(xcplib ${xcplib_SOURCES})
target_include_directories(xcplib PUBLIC "${PROJECT_ROOT}/inc" "${PROJECT_ROOT}/src")

# Apply compiler-specific flags
if(COMMON_WARNING_FLAGS)
    target_compile_options(xcplib PRIVATE ${COMMON_WARNING_FLAGS})
endif()

# Platform-specific compile definitions
if(WIN32)
    target_compile_definitions(xcplib PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Link threads library to xcplib (needed for pthread usage in platform.h)
target_link_libraries(xcplib PUBLIC Threads::Threads)

# Check for atomic library support (needed on some ARM platforms with clang)
include(CheckLibraryExists)
check_library_exists(atomic __atomic_store_4 "" HAVE_LIBATOMIC)
if(HAVE_LIBATOMIC)
    message(STATUS "Linking with atomic library for C11 atomics support")
    target_link_libraries(xcplib PUBLIC atomic)
endif()


# Example hello_xcp
set(hello_xcp_SOURCES examples/hello_xcp/src/main.c)   
set_source_files_properties(${hello_xcp_SOURCES} PROPERTIES LANGUAGE C)
add_executable(hello_xcp ${hello_xcp_SOURCES})
target_include_directories(hello_xcp PUBLIC "${PROJECT_ROOT}/inc")
target_link_libraries(hello_xcp PRIVATE xcplib)

# Example no_a2l_demo
set(no_a2l_demo_SOURCES examples/no_a2l_demo/src/main.c)   
set_source_files_properties(${no_a2l_demo_SOURCES} PROPERTIES LANGUAGE C)
add_executable(no_a2l_demo ${no_a2l_demo_SOURCES})
target_include_directories(no_a2l_demo PUBLIC "${PROJECT_ROOT}/inc")
target_link_libraries(no_a2l_demo PRIVATE xcplib)

# Example hello_xcp_cpp
add_executable(hello_xcp_cpp examples/hello_xcp_cpp/src/main.cpp )
target_include_directories(hello_xcp_cpp PUBLIC "${PROJECT_ROOT}/inc")
target_link_libraries(hello_xcp_cpp PRIVATE xcplib)

# Example point_cloud_demo
add_executable(point_cloud_demo examples/point_cloud_demo/src/main.cpp )
target_include_directories(point_cloud_demo PUBLIC "${PROJECT_ROOT}/inc")
target_link_libraries(point_cloud_demo PRIVATE xcplib)


# Example c_demo
set(c_demo_SOURCES examples/c_demo/src/main.c)
set_source_files_properties(${c_demo_SOURCES} PROPERTIES LANGUAGE C)
add_executable(c_demo ${c_demo_SOURCES})
target_include_directories(c_demo PUBLIC "${PROJECT_ROOT}/inc")
target_link_libraries(c_demo PRIVATE xcplib)

# Example cpp_demo
add_executable(cpp_demo examples/cpp_demo/src/main.cpp examples/cpp_demo/src/sig_gen.cpp examples/cpp_demo/src/lookup.cpp)
target_include_directories(cpp_demo PUBLIC "${PROJECT_ROOT}/inc")
target_link_libraries(cpp_demo PRIVATE xcplib)

# Example struct_demo
set(struct_demo_SOURCES examples/struct_demo/src/main.c)
set_source_files_properties(${struct_demo_SOURCES} PROPERTIES LANGUAGE C)
add_executable(struct_demo ${struct_demo_SOURCES})
target_include_directories(struct_demo PUBLIC "${PROJECT_ROOT}/inc")
target_link_libraries(struct_demo PRIVATE xcplib)

# Example multi_thread_demo
set(multi_thread_demo_SOURCES examples/multi_thread_demo/src/main.c)
set_source_files_properties(${multi_thread_demo_SOURCES} PROPERTIES LANGUAGE C)
add_executable(multi_thread_demo ${multi_thread_demo_SOURCES})
target_include_directories(multi_thread_demo PUBLIC "${PROJECT_ROOT}/inc")
target_link_libraries(multi_thread_demo PRIVATE xcplib)

# Example bpf_demo
set(bpf_demo_SOURCES examples/bpf_demo/src/main.c)   
set_source_files_properties(${bpf_demo_SOURCES} PROPERTIES LANGUAGE C)
add_executable(bpf_demo ${bpf_demo_SOURCES})
target_include_directories(bpf_demo PUBLIC "${PROJECT_ROOT}/inc")
target_link_libraries(bpf_demo PRIVATE xcplib)

# Add libbpf dependency for Linux builds
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_library(LIBBPF_LIBRARY bpf)
    if(LIBBPF_LIBRARY)
        target_link_libraries(bpf_demo PRIVATE ${LIBBPF_LIBRARY})
        message(STATUS "Found libbpf: ${LIBBPF_LIBRARY}")
    else()
        message(WARNING "libbpf not found. BPF functionality will be disabled.")
    endif()
endif()

# Tests
if(BUILD_TEST)
    # Type detection test (C version)
    add_executable(type_detection_test_c test/type_detection_test/src/main.c test/type_detection_test/src/c_version_test.c)
    target_include_directories(type_detection_test_c PUBLIC "${PROJECT_ROOT}/inc")
    # target_link_libraries(type_detection_test_c PRIVATE xcplib)

    # Type detection test (C++ version)
    add_executable(type_detection_test_cpp test/type_detection_test/src/main.cpp test/type_detection_test/src/cpp_version_test.cpp)
    target_include_directories(type_detection_test_cpp PUBLIC "${PROJECT_ROOT}/inc")
    # target_link_libraries(type_detection_test_cpp PRIVATE xcplib)

    # A2L generation test (C version)
    add_executable(a2l_test test/a2l_test/src/main.c)
    target_include_directories(a2l_test PUBLIC "${PROJECT_ROOT}/inc")
    target_link_libraries(a2l_test PRIVATE xcplib)

    # Calibration segment multi-threading test (C++ version)
    add_executable(cal_test test/cal_test/src/main.cpp)
    target_include_directories(cal_test PUBLIC "${PROJECT_ROOT}/inc")
    target_link_libraries(cal_test PRIVATE xcplib)

    # Data acquisition multi-threading test (C version)
    add_executable(daq_test test/daq_test/src/main.c)
    target_include_directories(daq_test PUBLIC "${PROJECT_ROOT}/inc")
    target_link_libraries(daq_test PRIVATE xcplib)
endif()


# Platform-specific settings
if(WIN32)
    # Windows-specific settings (if any needed in the future)
else()
    # Unix/Linux/macOS specific settings
    
    # Link math library 
    target_link_libraries(multi_thread_demo PRIVATE m)
    target_link_libraries(hello_xcp_cpp PRIVATE m)
    target_link_libraries(point_cloud_demo PRIVATE m)

    # Set .out suffix for executables on non-Windows platforms
    set_target_properties(hello_xcp PROPERTIES SUFFIX ".out")
    set_target_properties(no_a2l_demo PROPERTIES SUFFIX ".out")
    set_target_properties(hello_xcp_cpp PROPERTIES SUFFIX ".out")
    set_target_properties(point_cloud_demo PROPERTIES SUFFIX ".out")
    set_target_properties(c_demo PROPERTIES SUFFIX ".out")
    set_target_properties(cpp_demo PROPERTIES SUFFIX ".out")
    set_target_properties(struct_demo PROPERTIES SUFFIX ".out")
    set_target_properties(multi_thread_demo PROPERTIES SUFFIX ".out")
    set_target_properties(bpf_demo PROPERTIES SUFFIX ".out")
    
    if(BUILD_TEST)
        set_target_properties(type_detection_test_c PROPERTIES SUFFIX ".out")
        set_target_properties(type_detection_test_cpp PROPERTIES SUFFIX ".out")
        set_target_properties(a2l_test PROPERTIES SUFFIX ".out")
        set_target_properties(cal_test PROPERTIES SUFFIX ".out")
        set_target_properties(daq_test PROPERTIES SUFFIX ".out")
    endif()
endif()

