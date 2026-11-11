# Standalone Linux emulator using the real FreeRTOS kernel and FreeRTOS-Plus-TCP stack.

cmake_minimum_required(VERSION 3.24)
project(xcplite_freertos_plus_tcp_emu_demo LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This reference build uses the FreeRTOS GCC/POSIX port on Linux")
endif()

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------

option(DEMO_STATIC_ALLOCATION "Use static XCPlite tasks and mutexes" ON)
option(DEMO_BUILD_TESTS "Build real-stack and fault-injection tests" ON)
set(DEMO_TICK_RATE 1000 CACHE STRING "FreeRTOS tick rate for the reference build")

# POSIX connects through libpcap; LOOPBACK keeps traffic inside the stack.
set(DEMO_NETWORK_INTERFACE 1 CACHE STRING "One-based libpcap interface index printed at startup")
set(FREERTOS_PLUS_TCP_NETWORK_IF POSIX CACHE STRING "Demo network interface: POSIX or LOOPBACK")
set_property(CACHE FREERTOS_PLUS_TCP_NETWORK_IF PROPERTY STRINGS POSIX LOOPBACK)

if(NOT DEMO_NETWORK_INTERFACE MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "DEMO_NETWORK_INTERFACE must be a positive libpcap interface index")
endif()

if(NOT FREERTOS_PLUS_TCP_NETWORK_IF MATCHES "^(POSIX|LOOPBACK)$")
    message(FATAL_ERROR "This demo supports FREERTOS_PLUS_TCP_NETWORK_IF=POSIX or LOOPBACK")
endif()

# ---------------------------------------------------------------------------
# FreeRTOS kernel and configuration
# ---------------------------------------------------------------------------

include(FetchContent)

# Share FreeRTOSConfig.h and compile definitions with the kernel and network stack.
add_library(freertos_config INTERFACE)

target_include_directories(freertos_config SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
)

target_compile_definitions(freertos_config INTERFACE
    _GNU_SOURCE
    projCOVERAGE_TEST=0
    DEMO_STATIC_ALLOCATION=$<BOOL:${DEMO_STATIC_ALLOCATION}>
    DEMO_TICK_RATE=${DEMO_TICK_RATE}
    DEMO_NETWORK_INTERFACE=${DEMO_NETWORK_INTERFACE}
    DEMO_NETWORK_LOOPBACK=$<STREQUAL:${FREERTOS_PLUS_TCP_NETWORK_IF},LOOPBACK>
)

set(FREERTOS_PORT GCC_POSIX CACHE STRING "" FORCE)
set(FREERTOS_HEAP 3 CACHE STRING "" FORCE) # heap_3 delegates to malloc/free.

FetchContent_Declare(
    freertos_kernel
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
    GIT_TAG        9b777ae5c5b8e9e456065a00294d1e5f5f9facf5 # V11.3.0, 202604 LTS
)
FetchContent_MakeAvailable(freertos_kernel)

# ---------------------------------------------------------------------------
# FreeRTOS-Plus-TCP network stack
# ---------------------------------------------------------------------------

FetchContent_Declare(
    freertos_plus_tcp
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Plus-TCP.git
    GIT_TAG        c12361095aca68aeed858f45d14395fbffa92c0d # V4.4.1, 202604 LTS
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(freertos_plus_tcp)

if(FREERTOS_PLUS_TCP_NETWORK_IF STREQUAL "POSIX")
    target_include_directories(freertos_plus_tcp_network_if SYSTEM PRIVATE
        "${PCAP_INCLUDE_DIR}"
    )
endif()

if(DEMO_BUILD_TESTS AND FREERTOS_PLUS_TCP_NETWORK_IF STREQUAL "POSIX")
    # Tests use a separate loopback endpoint and never open the capture interface.
    target_sources(freertos_plus_tcp_network_if PRIVATE
        "${freertos_plus_tcp_SOURCE_DIR}/source/portable/NetworkInterface/loopback/loopbackNetworkInterface.c"
    )
endif()

# ---------------------------------------------------------------------------
# XCPlite library
# ---------------------------------------------------------------------------

set(XCPLITE_CONFIGURATION rtos CACHE STRING "" FORCE)
set(XCPLITE_FREERTOS_PLUS_TCP ON CACHE BOOL "" FORCE)

# This standalone project supplies its own demo and tests.
set(XCPLITE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(XCPLITE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(XCPLITE_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    "${CMAKE_BINARY_DIR}/xcplite"
)

# ---------------------------------------------------------------------------
# Demo application
# ---------------------------------------------------------------------------

add_executable(freertos_plus_tcp_emu_demo
    src/main.c
    ../xcp_demo.c
)

target_include_directories(freertos_plus_tcp_emu_demo PRIVATE
    ..
)

target_link_libraries(freertos_plus_tcp_emu_demo PRIVATE
    xcplite
    m # sinf() in the shared demo application.
)

# Keep static data within the native RTOS configuration's 32-bit absolute address range.
target_link_options(freertos_plus_tcp_emu_demo PRIVATE -no-pie)

# ---------------------------------------------------------------------------
# Automated tests
# ---------------------------------------------------------------------------

if(DEMO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../test/freerto_plus_tcp_test"
        "${CMAKE_BINARY_DIR}/tests"
    )
endif()
