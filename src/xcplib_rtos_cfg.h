#pragma once

/*----------------------------------------------------------------------------
| File:
|   xcplib_rtos_cfg.h
|
| Description:
|   XCPlite configuration OVERRIDES for FreeRTOS embedded targets
|   Applied AFTER the defaults in xcplib_cfg.h via:
|     cmake: target_compile_definitions(xcplite PRIVATE "XCPLIB_CFG_OVERRIDE=\"xcplib_rtos_cfg.h\"")
|
|   Key differences in overrides from the defaults in xcplib_cfg.h:
|     - Absolute memory addressing
|     - No jumbo frames, standard Ethernet MTU of 1500 bytes (1472 bytes UDP payload)
|     - No TCP support (not implemented yet for FreeRTOS)
|     - Reduced memory footprint
|     - 32 bit DAQ queue
|     - Clock resolution 1us
|     - No file system
|     - No on-target A2L generation
|     - No persistence, no A2L/ELF upload (no filesystem)
|     - No forceful thread termination (use vTaskDelete instead)
|     - Reduced queue size, and max event number and calibration segment counts to fit in embedded SRAM
|
|   Optional:
|     OPTION_ENABLE_TCP not implemented yet for FreeRTOS
|     OPTION_ENABLE_PTP not implemented yet for FreeRTOS
|   Addressing scheme:
|     Absolute memory addressing with A2L segments as absolute memory regions with static lifetime default page, no segment relative addressing
|   Platform requirements:
|    No filesystem required, 32 bit platform, currently only FreeRTOS, ThreadX planned to be supported in the future
|   Examples:
|    freertos_demo     - FreeRTOS POSIX simulator (Linux only), for testing FreeRTOS xcplite support on the host
|                        cmake: XCPLITE_CONFIGURATION=rtos, XCPLITE_BUILD_EXAMPLES=ON
|    esp32_freertos_demo - ESP32 target, uses the same xcplib_rtos_cfg.h override,
|                        built externally via PlatformIO (not a CMake project)
|   Tools:
|     xcpclient for A2L generation from ELF and testing
|   Tests:
|     -

 ----------------------------------------------------------------------------*/

// FreeRTOS rx and tx task stack depth (in bytes) and priority
// On the POSIX simulator the size must be considerably larger than usual
// Tune these values to the actual needs of the XCP server tasks on your target
#if defined(FREE_RTOS_POSIX_SIM)
#define OPTION_FREERTOS_STACK_BYTES (16U * 1024U)
#else
#define OPTION_FREERTOS_STACK_BYTES (4U * 1024U)
#endif
#define OPTION_FREERTOS_PRIORITY (tskIDLE_PRIORITY + 2U)

// FreeRTOS IP stack configuration
#define OPTION_FREERTOS_LWIP // Use the lwIP socket API for FreeRTOS

//-------------------------------------------------------------------------------
// Logging
// On embedded targets no stderr available
#undef OPTION_ENABLE_DBG_STDERR

//-------------------------------------------------------------------------------
// Clock

// FreeRTOS clock is assumed to have 1us ticks by default
// Adjust below if your clock has a different resolution, but be aware of the consequences regarding rounding errors and representation problems
#undef OPTION_CLOCK_TICKS_1NS
#define OPTION_CLOCK_TICKS_1US // Default for FreeRTOS

// Custom clock configuration for FreeRTOS targets, adjust CLOCK_TICKS_PER_S to the resolution of your DAQ clock, e.g. 1 MHz for microsecond resolution
#if !defined(OPTION_CLOCK_TICKS_1NS) && !defined(OPTION_CLOCK_TICKS_1US)
// #define CLOCK_TICKS_PER_S 480000000UL // 480 MHz clock tick rate for 2.08 ns resolution
#error "Please define CLOCK_TICKS_PER_S to the number of clock ticks per second for the DAQ clock"
#endif

#undef OPTION_CLOCK_EPOCH_PTP  // Clock has PTP epoch (0 = January 1st, 1970)
#define OPTION_CLOCK_EPOCH_ARB // Arbitrary clock zero

//-------------------------------------------------------------------------------
// XCP server
#undef OPTION_ENABLE_TCP // TCP support stubs not implemented yet for FreeRTOS
// OPTION_ENABLE_UDP stays enabled: FreeRTOS targets use the lwIP socket API (OPTION_FREERTOS_LWIP above),
// the POSIX simulator uses host sockets. For targets without any IP stack, use XCPLITE_CONFIGURATION=raw
// (OPTION_ENABLE_UDP_RAW, hand-crafted UDP/IP over a raw Ethernet HAL) - see docs/SOCKET_RAW.md

#undef OPTION_MTU
#define OPTION_MTU 1500                    // Standard Ethernet MTU: (1500 - 28) & ~7 = 1472 bytes max UDP payload
#undef OPTION_SERVER_FORCEFULL_TERMINATION // FreeRTOS uses vTaskDelete(NULL) to end tasks — no forceful termination

//-------------------------------------------------------------------------------
// Calibration

// Calibration segment management and RCU is enabled in the default configuration
// We use that for FreeRTOS, it does not support a fully section registered approach yet
// Calibration segments are detected in XcpInit by their static descriptors and allocated from the calibration memory bump allocator
// #undef OPTION_CAL_SEGMENTS

// Calibration segments max count and total memory size for the calibration memory bump allocator
// (each segment needs 3 copies of its data)
#undef OPTION_CAL_SEGMENT_COUNT
#define OPTION_CAL_SEGMENT_COUNT 8
#undef OPTION_CAL_MEM_SIZE
#define OPTION_CAL_MEM_SIZE (1024 * 4) // 4 KB

// No persistence (no filesystem on embedded)
#undef OPTION_ENABLE_PERSISTENCE

// Absolute addressing (compatible with most A2L tools and xcpclient)
// Address extension 0 is absolute addressing (linker map / elf address == 32 bit XCP address)
// Calibration segments have absolute addresses, segment relative addressing is still available on address extension 1
#define OPTION_CAL_SEGMENTS_ABS

//-------------------------------------------------------------------------------
// Data acquisition

// No dynamic event list management, use section registered events
#undef OPTION_DAQ_EVENT_LIST

// Adjust OPTION_DAQ_MEM_SIZE and OPTION_DAQ_EVENT_COUNT to your application
// In maximum fragmentation, each measurement value needs 6 bytes DAQ list memory
#undef OPTION_DAQ_MEM_SIZE
#define OPTION_DAQ_MEM_SIZE (512 * 6) // 4 KB for XCP DAQ tables
#undef OPTION_DAQ_EVENT_COUNT
#define OPTION_DAQ_EVENT_COUNT 16

// OPTION_QUEUE_32 is MANDATORY on 32-bit Cortex-M4:
// The 64-bit lockless queue variants use 64-bit atomics
#undef OPTION_QUEUE_64_VAR_SIZE
#undef OPTION_QUEUE_64_FIX_SIZE
#define OPTION_QUEUE_32
// Fixed 4 KB for the queue buffer, parameter of XcpEthServerInit ignored, must be a multiple of sizeof(tXcpSegmentBuffer)
#define OPTION_QUEUE_32_SIZE (16 * sizeof(tXcpSegmentBuffer))
// Use a crtical section instead of a mutex, locked sequences are only a few instructions
#define OPTION_QUEUE32_CRITICAL_SECTION
#undef OPTION_QUEUE32_MUTEX

//-------------------------------------------------------------------------------
// A2L / ELF — no filesystem on embedded; generate A2L externally via xcpclient or other tools
#undef OPTION_ENABLE_A2L_GENERATOR
#undef OPTION_ENABLE_A2L_UPLOAD
#undef OPTION_ENABLE_ELF_UPLOAD
