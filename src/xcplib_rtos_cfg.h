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
|   Only settings that DIFFER from the POSIX defaults are listed here.
|   Key differences:
|     - Absolute memory addressing
|     - 32 bit DAQ queue
|     - Clock resolution
|     - No file system
|     - No on-target A2L generation
|     - No persistence, no A2L/ELF upload (no filesystem)
|     - No forceful thread termination (use vTaskDelete instead)
|     - Reduced queue size, and max event number and calibration segment counts to fit in embedded SRAM
|
 ----------------------------------------------------------------------------*/

// FreeRTOS rx and tx task stack depth (in bytes) and priority
// On the POSIX simulator the size must be considerably larger than usual
// Tune these values to the actual needs of the XCP server tasks on your target
#define OPTION_FREERTOS_STACK_BYTES (8U * 1024U)
#define OPTION_FREERTOS_PRIORITY (tskIDLE_PRIORITY + 2U)

// FreeRTOS IP stack configuration
#define OPTION_FREERTOS_LWIP // Use the lwIP stack for FreeRTOS; requires FreeRTOS+TCP

//-------------------------------------------------------------------------------
// Logging
// On embedded targets no stderr available
#undef OPTION_ENABLE_DBG_STDERR

//-------------------------------------------------------------------------------
// Clock
#undef OPTION_CLOCK_TICKS_1NS
#define OPTION_CLOCK_TICKS_1US // 1 us ticks
#undef OPTION_CLOCK_EPOCH_PTP
#define OPTION_CLOCK_EPOCH_ARB // Arbitrary clock zero

//-------------------------------------------------------------------------------
// XCP server
#undef OPTION_ENABLE_TCP // TCP support stubs not implemented yet for FreeRTOS
#undef OPTION_MTU
#define OPTION_MTU 1504                    // Standard Ethernet MTU: 1504 - 32 = 1472 bytes max UDP payload (%8 aligned)
#undef OPTION_SERVER_FORCEFULL_TERMINATION // FreeRTOS uses vTaskDelete(NULL) to end tasks — no forceful termination

//-------------------------------------------------------------------------------
// Calibration segments max count and total memory size (each segment needs 3 copies of its data
#undef OPTION_CAL_SEGMENT_COUNT
#define OPTION_CAL_SEGMENT_COUNT 8
#undef OPTION_CAL_MEM_SIZE
#define OPTION_CAL_MEM_SIZE (1024 * 4) // 4 KB

// No persistence (no filesystem on embedded)
#undef OPTION_ENABLE_PERSISTENCE

// Absolute addressing (compatible with most A2L tools and xcpclient
// Address extension 0 is absolute addressing (linker map / elf address == XCP address))
// Calibration segments have absolute addresses, segment relative addressing is still available on address extension 1
#define OPTION_CAL_SEGMENTS_ABS

//-------------------------------------------------------------------------------
// Data acquisition
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

//-------------------------------------------------------------------------------
// A2L / ELF — no filesystem on embedded; generate A2L externally via xcpclient or other tools
#undef OPTION_ENABLE_A2L_GENERATOR
#undef OPTION_ENABLE_A2L_UPLOAD
#undef OPTION_ENABLE_ELF_UPLOAD
