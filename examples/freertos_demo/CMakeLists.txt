# examples/freertos_demo/CMakeLists.txt
#
# Builds the freertos_demo target.
# Downloads FreeRTOS-Kernel (POSIX simulator port) via CMake FetchContent.
#
# Requires: CMake >= 3.14, internet access on first configure, POSIX platform (macOS / Linux).
# Does NOT build on Windows.

cmake_minimum_required(VERSION 3.14)

if(WIN32)
    message(STATUS "freertos_demo: skipping – not supported on Windows")
    return()
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# FreeRTOS-Kernel (POSIX simulator port)
# ---------------------------------------------------------------------------

# Tell the FreeRTOS-Kernel CMake build which port and heap implementation to use
set(FREERTOS_PORT "GCC_POSIX" CACHE STRING "FreeRTOS port – POSIX simulator")
set(FREERTOS_HEAP "3"         CACHE STRING "FreeRTOS heap – delegates to system malloc/free")

FetchContent_Declare(
    freertos_kernel
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
    GIT_TAG        V11.1.0
    GIT_SHALLOW    TRUE
)

# Create an INTERFACE library that exposes our FreeRTOSConfig.h to the kernel.
# This is the recommended approach for FreeRTOS-Kernel V11+.
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"   # FreeRTOSConfig.h lives here
)
# _GNU_SOURCE is required so that the FreeRTOS GCC/POSIX port (port.c) can
# see POSIX symbols such as clockid_t, locale_t, CLOCK_MONOTONIC, SIG_BLOCK,
# struct sigaction, PTHREAD_STACK_MIN, sigwait(), etc.  Because freertos_config
# is an INTERFACE library consumed by the kernel build itself, any definition
# placed here is automatically applied to freertos_kernel_port.
target_compile_definitions(freertos_config INTERFACE projCOVERAGE_TEST=0 _GNU_SOURCE)

FetchContent_MakeAvailable(freertos_kernel)

# ---------------------------------------------------------------------------
# freertos_demo executable
# ---------------------------------------------------------------------------

add_executable(freertos_demo src/main.c)

target_link_libraries(freertos_demo PRIVATE
    xcplite          # XCPlite measurement/calibration library (POSIX build)
    freertos_kernel  # FreeRTOS kernel + POSIX simulator port
)

# macOS / Linux: ensure GNU extensions are available (needed by FreeRTOS POSIX port)
target_compile_definitions(freertos_demo PRIVATE _GNU_SOURCE)

# Suppress some warnings that the FreeRTOS port headers trigger with -pedantic
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(freertos_demo PRIVATE
        -Wno-pedantic
        -Wno-unused-parameter
    )
endif()


target_compile_definitions(xcplite PRIVATE
    _FREE_RTOS                                           # Force FreeRTOS code paths in platform.h/platform.c, xcpappl.c
    FREE_RTOS_POSIX_SIM                                  # Use Linux sockets and clock code in platform.c when _FREE_RTOS is defined
    "XCPLIB_CFG_OVERRIDE=\"xcplib_rtos_cfg.h\""
)

# xcplite needs to find FreeRTOS.h / task.h etc. when _FREE_RTOS is defined
target_include_directories(xcplite PRIVATE
    "${freertos_kernel_SOURCE_DIR}/include"
    "${freertos_kernel_SOURCE_DIR}/portable/ThirdParty/GCC/Posix"
    "${CMAKE_CURRENT_SOURCE_DIR}"   # FreeRTOSConfig.h
)


