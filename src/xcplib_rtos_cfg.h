#pragma once

/*----------------------------------------------------------------------------
| File:
|   xcplib_rtos_cfg.h
|
| Description:
|   XCPlite configuration OVERRIDES for FreeRTOS / embedded targets (e.g. STM32 Cortex-M4).
|   Applied AFTER the defaults in xcplib_cfg.h via:
|     cmake: target_compile_definitions(xcplite PRIVATE "XCPLIB_CFG_OVERRIDE=\"xcplib_rtos_cfg.h\"")
|
|   Only settings that DIFFER from the POSIX defaults are listed here.
|   Key differences:
|     - OPTION_QUEUE_32          mandatory on 32-bit Cortex-M4 (no 64-bit atomics)
|     - OPTION_CLOCK_TICKS_1US   FreeRTOS tick-based clock (1 ms granularity)
|     - No persistence, no A2L/ELF upload (no filesystem)
|     - No forceful thread termination (use vTaskDelete instead)
|     - Reduced buffer/event sizes to fit in embedded SRAM
|
|   @@@@ TODO: Tune OPTION_CAL_MEM_SIZE, OPTION_DAQ_MEM_SIZE, OPTION_MTU and
|              stack sizes (xcplib_rtos_cfg.h) to your hardware memory map.
 ----------------------------------------------------------------------------*/

//-------------------------------------------------------------------------------
// Logging
// On embedded targets route debug output to UART / ITM-SWO; no stderr available.
#undef OPTION_ENABLE_DBG_STDERR

//-------------------------------------------------------------------------------
// Clock
// FreeRTOS clock uses xTaskGetTickCount() — granularity = 1/configTICK_RATE_HZ.
// @@@@ TODO: For higher resolution swap in a hardware free-running counter (DWT->CYCCNT etc.).
#undef OPTION_CLOCK_TICKS_1NS
#define OPTION_CLOCK_TICKS_1US // 1 us ticks (1 ms actual granularity at 1 kHz tick rate)

//-------------------------------------------------------------------------------
// XCP server
#undef OPTION_ENABLE_TCP
// Standard Ethernet MTU: 1504 - 32 = 1472 bytes max UDP payload (%8 aligned)
#undef OPTION_MTU
#define OPTION_MTU 1504
// FreeRTOS uses vTaskDelete(NULL) to end tasks — no forceful termination
#undef OPTION_SERVER_FORCEFULL_TERMINATION

//-------------------------------------------------------------------------------
// Calibration segments — tune to available SRAM
// @@@@ TODO: Adjust OPTION_CAL_SEGMENT_COUNT and OPTION_CAL_MEM_SIZE to your application
#undef OPTION_CAL_SEGMENT_COUNT
#define OPTION_CAL_SEGMENT_COUNT 8
#undef OPTION_CAL_MEM_SIZE
#define OPTION_CAL_MEM_SIZE (1024 * 4) // 4 KB

// No persistence (no filesystem on embedded)
#undef OPTION_ENABLE_PERSISTENCE

// Absolute addressing (compatible with most A2L tools and xcpclient)
#define OPTION_CAL_SEGMENTS_ABS

//-------------------------------------------------------------------------------
// DAQ — tune to available SRAM
// @@@@ TODO: Adjust OPTION_DAQ_MEM_SIZE and OPTION_DAQ_EVENT_COUNT to your application
#undef OPTION_DAQ_MEM_SIZE
#define OPTION_DAQ_MEM_SIZE (1024 * 4) // 4 KB for XCP DAQ tables
#undef OPTION_DAQ_EVENT_COUNT
#define OPTION_DAQ_EVENT_COUNT 16

// OPTION_QUEUE_32 is MANDATORY on 32-bit Cortex-M4:
// The 64-bit lockless queue variants use 64-bit atomics not available on ARMv7-M.
#undef OPTION_QUEUE_64_VAR_SIZE
#undef OPTION_QUEUE_64_FIX_SIZE
#define OPTION_QUEUE_32

//-------------------------------------------------------------------------------
// A2L / ELF — no filesystem on embedded; generate A2L externally via xcpclient
#undef OPTION_ENABLE_A2L_GENERATOR
#undef OPTION_ENABLE_A2L_UPLOAD
#undef OPTION_ENABLE_ELF_UPLOAD
