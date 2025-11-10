#pragma once
#define __DBG_PRINT_H__

/*----------------------------------------------------------------------------
| File:
|   dbg_print.h
|
| Description:
|   Debug logging
|
| Code released into public domain, no attribution required
|
 ----------------------------------------------------------------------------*/

#include "main_cfg.h" // for OPTION_ENABLE_DBG_PRINTS, OPTION_DEFAULT_DBG_LEVEL, OPTION_FIXED_DBG_LEVEL

#if defined(OPTION_ENABLE_DBG_PRINTS) && !defined(OPTION_FIXED_DBG_LEVEL) && !defined(OPTION_DEFAULT_DBG_LEVEL)
#error "Please define OPTION_DEFAULT_DBG_LEVEL or OPTION_FIXED_DBG_LEVEL"
#endif

#ifdef OPTION_ENABLE_DBG_METRICS

extern uint32_t gXcpWritePendingCount;
extern uint32_t gXcpCalSegPublishAllCount;
extern uint32_t gXcpDaqEventCount;
extern uint32_t gXcpTxPacketCount;
extern uint32_t gXcpRxPacketCount;

#endif

#ifdef OPTION_ENABLE_DBG_PRINTS

/*
1 - Error
2 - Warn
3 - Info
4 - Trace
5 - Debug
*/

// ANSI color codes
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_RESET "\x1b[0m"

#ifdef OPTION_FIXED_DBG_LEVEL
#define DBG_LEVEL OPTION_FIXED_DBG_LEVEL
#else
extern uint8_t gXcpDebugLevel;
#define DBG_LEVEL gXcpDebugLevel
#endif

#define DBG_PRINTF(format, ...) printf(format, __VA_ARGS__)

#ifdef OPTION_ENABLE_DBG_STDERR
#define DBG_PRINTF_ERROR(format, ...)                                                                                                                                              \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 1)                                                                                                                                                        \
            fprintf(stderr, ANSI_COLOR_RED "[XCP  ] ERROR: " ANSI_COLOR_RESET format, __VA_ARGS__);                                                                                \
    } while (0)
#define DBG_PRINTF_WARNING(format, ...)                                                                                                                                            \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 2)                                                                                                                                                        \
            fprintf(stderr, ANSI_COLOR_YELLOW "[XCP  ] WARNING: " ANSI_COLOR_RESET format, __VA_ARGS__);                                                                           \
    } while (0)
#else
#define DBG_PRINTF_ERROR(format, ...)                                                                                                                                              \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 1)                                                                                                                                                        \
            printf(ANSI_COLOR_RED "[XCP  ] ERROR: " ANSI_COLOR_RESET format, __VA_ARGS__);                                                                                         \
    } while (0)
#define DBG_PRINTF_WARNING(format, ...)                                                                                                                                            \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 2)                                                                                                                                                        \
            printf(ANSI_COLOR_YELLOW "[XCP  ] WARNING: " ANSI_COLOR_RESET format, __VA_ARGS__);                                                                                    \
    } while (0)
#endif
#define DBG_PRINTF3(format, ...)                                                                                                                                                   \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 3)                                                                                                                                                        \
            printf("[XCP  ] " format, __VA_ARGS__);                                                                                                                                \
    } while (0)
#define DBG_PRINTF4(format, ...)                                                                                                                                                   \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 4)                                                                                                                                                        \
            printf("[XCP  ] " format, __VA_ARGS__);                                                                                                                                \
    } while (0)
#define DBG_PRINTF5(format, ...)                                                                                                                                                   \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 5)                                                                                                                                                        \
            printf("[XCP  ] " format, __VA_ARGS__);                                                                                                                                \
    } while (0)

// Convenience macros for format-only strings (no additional arguments)
#define DBG_PRINT(format) printf(format)

#ifdef OPTION_ENABLE_DBG_STDERR
#define DBG_PRINT_ERROR(format)                                                                                                                                                    \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 1)                                                                                                                                                        \
            fprintf(stderr, ANSI_COLOR_RED "[XCP  ] ERROR: " ANSI_COLOR_RESET format);                                                                                             \
    } while (0)
#define DBG_PRINT_WARNING(format)                                                                                                                                                  \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 2)                                                                                                                                                        \
            fprintf(stderr, ANSI_COLOR_YELLOW "[XCP  ] WARNING: " ANSI_COLOR_RESET format);                                                                                        \
    } while (0)
#else
#define DBG_PRINT_ERROR(format)                                                                                                                                                    \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 1)                                                                                                                                                        \
            printf(ANSI_COLOR_RED "[XCP  ] ERROR: " ANSI_COLOR_RESET format);                                                                                                      \
    } while (0)
#define DBG_PRINT_WARNING(format)                                                                                                                                                  \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 2)                                                                                                                                                        \
            printf(ANSI_COLOR_YELLOW "[XCP  ] WARNING: " ANSI_COLOR_RESET format);                                                                                                 \
    } while (0)
#endif
#define DBG_PRINT3(format)                                                                                                                                                         \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 3)                                                                                                                                                        \
            printf("[XCP  ] " format);                                                                                                                                             \
    } while (0)
#define DBG_PRINT4(format)                                                                                                                                                         \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 4)                                                                                                                                                        \
            printf("[XCP  ] " format);                                                                                                                                             \
    } while (0)
#define DBG_PRINT5(format)                                                                                                                                                         \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 5)                                                                                                                                                        \
            printf("[XCP  ] " format);                                                                                                                                             \
    } while (0)

#else

#undef DBG_LEVEL

#define DBG_PRINTF(s, ...)
#define DBG_PRINTF_ERROR(s, ...) // printf(s,__VA_ARGS__)
#define DBG_PRINTF_WARNING(s, ...)
#define DBG_PRINTF3(s, ...)
#define DBG_PRINTF4(s, ...)
#define DBG_PRINTF5(s, ...)

#define DBG_PRINT(s)
#define DBG_PRINT_ERROR(s) // printf(s,__VA_ARGS__)
#define DBG_PRINT_WARNING(s)
#define DBG_PRINT3(s)
#define DBG_PRINT4(s)
#define DBG_PRINT5(s)

#endif
