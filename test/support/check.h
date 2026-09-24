// test/support/check.h - shared CHECK macro for the xcplite test suite.
//
//   CHECK(cond)                 - if cond is false, print "<file>:<line>: <cond> failed" and exit(EXIT_FAILURE)
//   CHECK(cond, fmt, ...)       - same, plus a printf-style message: "<file>:<line>: <cond> failed: <fmt>"
//
// Unlike assert(), CHECK always evaluates its condition (also in Release / NDEBUG builds) and
// reports the failing condition, file and line. The message is optional, so call sites that pass
// only a condition do not need to change. The optional message must be a printf format string
// literal (it is concatenated with "" in the macro, which also enables -Wformat checking).

#pragma once

#include <stdarg.h> // for va_list
#include <stdio.h>  // for fprintf, vfprintf, fputs, fputc
#include <stdlib.h> // for exit, EXIT_FAILURE

#ifdef __cplusplus
extern "C" {
#endif

// Prints ": <formatted message>" when a message was given; prints nothing for the empty ("") message.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2), unused))
#endif
static inline void xcp_check_print_msg_(const char *fmt, ...) {
    if (fmt[0] != '\0') {
        va_list ap;
        va_start(ap, fmt);
        fputs(": ", stderr);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
}

#ifdef __cplusplus
} // extern "C"
#endif

#define CHECK(cond, ...)                                                                                                                                                           \
    do {                                                                                                                                                                           \
        if (!(cond)) {                                                                                                                                                             \
            fprintf(stderr, "%s:%d: %s failed", __FILE__, __LINE__, #cond);                                                                                                        \
            xcp_check_print_msg_("" __VA_ARGS__);                                                                                                                                  \
            fputc('\n', stderr);                                                                                                                                                   \
            exit(EXIT_FAILURE);                                                                                                                                                    \
        }                                                                                                                                                                          \
    } while (0)
