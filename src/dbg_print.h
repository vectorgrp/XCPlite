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

#include <stdint.h> // for uintxx_t

#include "main_cfg.h" // for OPTION_ENABLE_DBG_PRINTS, OPTION_DEFAULT_DBG_LEVEL, OPTION_MAX_DBG_LEVEL, OPTION_FIXED_DBG_LEVEL

#ifdef OPTION_ENABLE_DBG_PRINTS

/* Log levels:
1 - Error
2 - Warn
3 - Info
4 - Trace
5 - Debug
*/

// Log level may be runtime adjustable or fixed at compile time
#if defined(OPTION_ENABLE_DBG_PRINTS) && !defined(OPTION_FIXED_DBG_LEVEL) && !defined(OPTION_DEFAULT_DBG_LEVEL)
#error "Please define OPTION_DEFAULT_DBG_LEVEL or OPTION_FIXED_DBG_LEVEL"
#endif

// Prefixes
// #define DBG_PRINT_PREFIX "[XCP  ] "
#define DBG_PRINT_PREFIX

// ANSI color codes
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_RESET "\x1b[0m"

#ifdef OPTION_FIXED_DBG_LEVEL

// Not runtime adjustable, fixed log level
#define DBG_LEVEL OPTION_FIXED_DBG_LEVEL
#undef OPTION_MAX_DBG_LEVEL
#define OPTION_MAX_DBG_LEVEL OPTION_FIXED_DBG_LEVEL

#else // !OPTION_FIXED_DBG_LEVEL

// Runtime adjustable log level, limited by OPTION_MAX_DBG_LEVEL
#if defined(OPTION_MAX_DBG_LEVEL) && OPTION_MAX_DBG_LEVEL < OPTION_DEFAULT_DBG_LEVEL
#error "OPTION_MAX_DBG_LEVEL should be >= OPTION_DEFAULT_DBG_LEVEL"
#endif

// Use log level for XCP if not defined DBG_LEVEL
#ifndef DBG_LEVEL
#ifdef __cplusplus
extern "C" {
#endif
extern uint8_t gXcpLogLevel;
#ifdef __cplusplus
}
#endif
#define DBG_LEVEL gXcpLogLevel
#endif

#endif // OPTION_FIXED_DBG_LEVEL

// #define DBG_PRINT_ERROR_LOCATION // Uncomment to include file and line number in error messages
#ifdef DBG_PRINT_ERROR_LOCATION
#define DBG_STRINGIFY(x) #x
#define DBG_TOSTRING(x) DBG_STRINGIFY(x)
#define DBG_PRINT_ERROR_STR DBG_PRINT_PREFIX ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET " in " __FILE__ "-" DBG_TOSTRING(__LINE__) ": "
#else
#define DBG_PRINT_ERROR_STR DBG_PRINT_PREFIX ANSI_COLOR_RED "ERROR: " ANSI_COLOR_RESET
#endif
#define DBG_PRINT_WARNING_STR DBG_PRINT_PREFIX ANSI_COLOR_YELLOW "WARNING: " ANSI_COLOR_RESET

#define DBG_PRINTF(format, ...) printf(format, __VA_ARGS__)

#ifdef OPTION_ENABLE_DBG_STDERR

#define DBG_PRINTF_ERROR(format, ...)                                                                                                                                              \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 1)                                                                                                                                                        \
            fprintf(stderr, DBG_PRINT_ERROR_STR format, __VA_ARGS__);                                                                                                              \
    } while (0)
#define DBG_PRINT_ERROR(format)                                                                                                                                                    \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 1)                                                                                                                                                        \
            fprintf(stderr, DBG_PRINT_ERROR_STR format);                                                                                                                           \
    } while (0)

#define DBG_PRINTF_WARNING(format, ...)                                                                                                                                            \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 2)                                                                                                                                                        \
            fprintf(stderr, DBG_PRINT_WARNING_STR format, __VA_ARGS__);                                                                                                            \
    } while (0)
#define DBG_PRINT_WARNING(format)                                                                                                                                                  \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 2)                                                                                                                                                        \
            fprintf(stderr, DBG_PRINT_WARNING_STR format);                                                                                                                         \
    } while (0)

#else // OPTION_ENABLE_DBG_STDERR

#define DBG_PRINTF_ERROR(format, ...)                                                                                                                                              \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 1)                                                                                                                                                        \
            printf(ANSI_COLOR_RED "[XCP  ] ERROR: " ANSI_COLOR_RESET format, __VA_ARGS__);                                                                                         \
    } while (0)
#define DBG_PRINT_ERROR(format)                                                                                                                                                    \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 1)                                                                                                                                                        \
            printf(ANSI_COLOR_RED "[XCP  ] ERROR: " ANSI_COLOR_RESET format);                                                                                                      \
    } while (0)

#define DBG_PRINTF_WARNING(format, ...)                                                                                                                                            \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 2)                                                                                                                                                        \
            printf(ANSI_COLOR_YELLOW "[XCP  ] WARNING: " ANSI_COLOR_RESET format, __VA_ARGS__);                                                                                    \
    } while (0)
#define DBG_PRINT_WARNING(format)                                                                                                                                                  \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 2)                                                                                                                                                        \
            printf(ANSI_COLOR_YELLOW "[XCP  ] WARNING: " ANSI_COLOR_RESET format);                                                                                                 \
    } while (0)

#endif // !OPTION_ENABLE_DBG_STDERR

#define DBG_PRINTF3(format, ...)                                                                                                                                                   \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 3)                                                                                                                                                        \
            printf(DBG_PRINT_PREFIX format, __VA_ARGS__);                                                                                                                          \
    } while (0)
#define DBG_PRINT3(format)                                                                                                                                                         \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 3)                                                                                                                                                        \
            printf(DBG_PRINT_PREFIX format);                                                                                                                                       \
    } while (0)

#define DBG_PRINTF4(format, ...)                                                                                                                                                   \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 4)                                                                                                                                                        \
            printf(DBG_PRINT_PREFIX format, __VA_ARGS__);                                                                                                                          \
    } while (0)
#define DBG_PRINT4(format)                                                                                                                                                         \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 4)                                                                                                                                                        \
            printf(DBG_PRINT_PREFIX format);                                                                                                                                       \
    } while (0)

#define DBG_PRINTF5(format, ...)                                                                                                                                                   \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 5)                                                                                                                                                        \
            printf(DBG_PRINT_PREFIX format, __VA_ARGS__);                                                                                                                          \
    } while (0)
#define DBG_PRINT5(format)                                                                                                                                                         \
    do {                                                                                                                                                                           \
        if (DBG_LEVEL >= 5)                                                                                                                                                        \
            printf(DBG_PRINT_PREFIX format);                                                                                                                                       \
    } while (0)

// Convenience macros for format-only strings (no additional arguments)
#define DBG_PRINT(format) printf(format)

#if OPTION_MAX_DBG_LEVEL < 1
#undef DBG_PRINTF_ERROR
#define DBG_PRINTF_ERROR(s, ...) // printf(s,__VA_ARGS__)
#undef DBG_PRINT_ERROR
#define DBG_PRINT_ERROR(s) // printf(s,__VA_ARGS__)
#endif

#if OPTION_MAX_DBG_LEVEL < 2
#undef DBG_PRINTF_WARNING
#define DBG_PRINTF_WARNING(s, ...) // printf(s,__VA_ARGS__)
#undef DBG_PRINT_WARNING
#define DBG_PRINT_WARNING(s) // printf(s,__VA_ARGS__)
#endif

#if OPTION_MAX_DBG_LEVEL < 3
#undef DBG_PRINTF3
#define DBG_PRINTF3(s, ...)
#undef DBG_PRINT3
#define DBG_PRINT3(s)
#endif

#if OPTION_MAX_DBG_LEVEL < 4
#undef DBG_PRINTF4
#define DBG_PRINTF4(s, ...)
#undef DBG_PRINT4
#define DBG_PRINT4(s)
#endif

#if OPTION_MAX_DBG_LEVEL < 5
#undef DBG_PRINTF5
#define DBG_PRINTF5(s, ...)
#undef DBG_PRINT5
#define DBG_PRINT5(s)
#endif

#else // OPTION_ENABLE_DBG_PRINTS

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

#endif // !OPTION_ENABLE_DBG_PRINTS
