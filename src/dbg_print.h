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
#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t gXcpWritePendingCount;
extern uint32_t gXcpCalSegPublishAllCount;
extern uint32_t gXcpDaqEventCount;
extern uint32_t gXcpTxPacketCount;
extern uint32_t gXcpRxPacketCount;
#ifdef __cplusplus
}
#endif
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
#ifdef __cplusplus
extern "C" {
#endif
extern uint8_t gXcpDebugLevel;
#ifdef __cplusplus
}
#endif
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
#else // OPTION_ENABLE_DBG_STDERR
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
#endif // !OPTION_ENABLE_DBG_STDERR
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

// C++ specific debug print macros
#ifdef __cplusplus

#include <iostream>
#include <utility> // for std::forward

// Base case: no arguments
inline void DBG_PRINTV3() { std::cout << std::endl; }

// Recursive case: print first argument, then recurse with the rest
template <typename T, typename... Args> inline void DBG_PRINTV3(const T &first, const Args &...args) {
    std::cout << first;
    if constexpr (sizeof...(args) > 0) {
        std::cout << " ";
        DBG_PRINTV3(args...);
    } else {
        std::cout << std::endl;
    }
}

// Level 3 variadic template version
template <typename... Args> inline void DBG_PRINT3_VARIADIC(const Args &...args) {
    if (DBG_LEVEL >= 3) {
        std::cout << "[XCP  ] ";
        DBG_PRINTV3(args...);
    }
}
// Level 4 variadic template version
template <typename... Args> inline void DBG_PRINT4_VARIADIC(const Args &...args) {
    if (DBG_LEVEL >= 4) {
        std::cout << "[XCP  ] ";
        DBG_PRINTV3(args...);
    }
}
// Level 5 variadic template version
template <typename... Args> inline void DBG_PRINT5_VARIADIC(const Args &...args) {
    if (DBG_LEVEL >= 5) {
        std::cout << "[XCP  ] ";
        DBG_PRINTV3(args...);
    }
}

// Helper class for printing variables with their names
template <typename T> struct DbgVar {
    const char *name;
    const T &value;

    DbgVar(const char *n, const T &v) : name(n), value(v) {}

    friend std::ostream &operator<<(std::ostream &os, const DbgVar &var) {
        os << var.name << " = " << var.value;
        return os;
    }
};

// Helper function to create DbgVar instances
template <typename T> inline DbgVar<T> dbg_var(const char *name, const T &value) { return DbgVar<T>(name, value); }

// Base case: no variables
inline void DBG_PRINT_VARS_IMPL(int level) {}

// Recursive case: print variables one by one
template <typename T, typename... Args> inline void DBG_PRINT_VARS_IMPL(int level, const DbgVar<T> &first, const DbgVar<Args> &...rest) {
    if (DBG_LEVEL >= level) {
        std::cout << "[XCP  ] " << first << std::endl;
        DBG_PRINT_VARS_IMPL(level, rest...);
    }
}

// Convenience functions for each debug level
template <typename... Args> inline void DBG_PRINT3_VAR(const DbgVar<Args> &...vars) { DBG_PRINT_VARS_IMPL(3, vars...); }

template <typename... Args> inline void DBG_PRINT4_VAR(const DbgVar<Args> &...vars) { DBG_PRINT_VARS_IMPL(4, vars...); }

template <typename... Args> inline void DBG_PRINT5_VAR(const DbgVar<Args> &...vars) { DBG_PRINT_VARS_IMPL(5, vars...); }

// Simple macro to wrap variable name and value (still need one macro for stringification)
#define DBG_VAR(v) dbg_var(#v, v)

#endif // __cplusplus

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
#else // OPTION_ENABLE_DBG_STDERR
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
#endif // !OPTION_ENABLE_DBG_STDERR
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
