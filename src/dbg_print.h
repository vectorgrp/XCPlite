#pragma once
#define __DBG_PRINT_H__

/*
| Code released into public domain, no attribution required
*/

#include "main_cfg.h" // for OPTION_xxx

//-------------------------------------------------------------------------------
// Debug print

#if defined(OPTION_ENABLE_DBG_PRINTS) && !defined(OPTION_DEFAULT_DBG_LEVEL)
#error "Please define OPTION_DEFAULT_DBG_LEVEL"
#endif

#ifdef OPTION_ENABLE_DBG_PRINTS

/*
1 - Error
2 - Warn
3 - Info
4 - Trace
5 - Debug
*/

extern uint8_t gDebugLevel;
#define DBG_LEVEL gDebugLevel

#define DBG_PRINTF(format, ...) printf(format, __VA_ARGS__)
#define DBG_PRINTF_ERROR(format, ...)                                                                                                                                              \
    if (DBG_LEVEL >= 1)                                                                                                                                                            \
    printf("[XCP  ] ERROR: " format, __VA_ARGS__)
#define DBG_PRINTF_WARNING(format, ...)                                                                                                                                            \
    if (DBG_LEVEL >= 2)                                                                                                                                                            \
    printf("[XCP  ] WARNING: " format, __VA_ARGS__)
#define DBG_PRINTF3(format, ...)                                                                                                                                                   \
    if (DBG_LEVEL >= 3)                                                                                                                                                            \
    printf("[XCP  ] " format, __VA_ARGS__)
#define DBG_PRINTF4(format, ...)                                                                                                                                                   \
    if (DBG_LEVEL >= 4)                                                                                                                                                            \
    printf("[XCP  ] " format, __VA_ARGS__)
#define DBG_PRINTF5(format, ...)                                                                                                                                                   \
    if (DBG_LEVEL >= 5)                                                                                                                                                            \
    printf("[XCP  ] " format, __VA_ARGS__)

#define DBG_PRINT(format) printf(format)
#define DBG_PRINT_ERROR(format)                                                                                                                                                    \
    if (DBG_LEVEL >= 1)                                                                                                                                                            \
    printf("[XCP  ] ERROR: " format)
#define DBG_PRINT_WARNING(format)                                                                                                                                                  \
    if (DBG_LEVEL >= 2)                                                                                                                                                            \
    printf("[XCP  ] WARNING: " format)
#define DBG_PRINT3(format)                                                                                                                                                         \
    if (DBG_LEVEL >= 3)                                                                                                                                                            \
    printf("[XCP  ] " format)
#define DBG_PRINT4(format)                                                                                                                                                         \
    if (DBG_LEVEL >= 4)                                                                                                                                                            \
    printf("[XCP  ] " format)
#define DBG_PRINT5(format)                                                                                                                                                         \
    if (DBG_LEVEL >= 5)                                                                                                                                                            \
    printf("[XCP  ] " format)

#else

#undef DBG_LEVEL

#define DBG_PRINTF(s, ...)
#define DBG_PRINTF_ERROR(s, ...) // printf(s,__VA_ARGS__)
#define DBG_PRINTF_WARNING(s, ...)
#define DBG_PRINTF3(s, ...)
#define DBG_PRINTF4(s, ...)
#define DBG_PRINTF5(s, ...)

#define DBG_PRINT(s, ...)
#define DBG_PRINT_ERROR(s, ...) // printf(s,__VA_ARGS__)
#define DBG_PRINT_WARNING(s, ...)
#define DBG_PRINT3(s, ...)
#define DBG_PRINT4(s, ...)
#define DBG_PRINT5(s, ...)

#endif
