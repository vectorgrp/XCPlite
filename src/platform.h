#pragma once
#define __PLATFORM_H__

/*----------------------------------------------------------------------------
| File:
|   platform.h
|
| Description:
|   XCPlite internal header file for platform.c
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

//-------------------------------------------------------------------------------------------------
// Platform defines

// 64 Bit or 32 Bit platform
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_WIN64)
#define PLATFORM_64BIT
#else
#define PLATFORM_32BIT
#endif

// Windows
#if defined(_WIN32) || defined(_WIN64)

#define _WIN

// For Windows compatibility, we must use an emulation for atomic operations and fallback to the 32 bit transmit queue
// MSVC does not support C11 stdatomic.h
#define OPTION_ATOMIC_EMULATION

#if defined(_WIN32) && defined(_WIN64)
// #error "defined(_WIN32) && defined(_WIN64)"
#undef _WIN32 // @@@@ TODO: Remove this line
#endif
#if defined(_LINUX) || defined(_LINUX64) || defined(_LINUX32)
#error "defined(_LINUX) || defined(_LINUX64) || defined(_LINUX32)"
#endif

// Linux or macOS
#else

#define _LINUX

#if defined(_ix64_) || defined(__x86_64__) || defined(__aarch64__)
#define _LINUX64
#ifdef __APPLE__
#define _MACOS
#endif
#else
#error "32 Bit OS not supported"
#define _LINUX32
#ifdef __APPLE__
#define _MACOS32
#endif
#endif

#if defined(_WIN) || defined(_WIN64) || defined(_WIN32)
#error "defined(_WIN) || defined(_WIN64) || defined(_WIN32)"
#endif

#endif

#ifdef _WIN
#define WIN32_LEAN_AND_MEAN

#endif

#if !defined(_WIN) && !defined(_LINUX) && !defined(_MACOS)
#error "Please define platform _WIN or _MACOS or _LINUX"
#endif

//-------------------------------------------------------------------------------------------------
// Compilation options

#include "main_cfg.h" // for OPTION_xxx

//-------------------------------------------------------------------------------------------------
// Platform specific functions

#if defined(_WIN)

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIx32, PRIu64
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t, uint_fastxx_t
#include <stdio.h>
#include <stdio.h> // for printf
#include <time.h>
#include <windows.h>

#if defined(_WIN)
#include <stdlib.h> // for malloc, free
#endif

#elif defined(_LINUX) || defined(_MACOS) // Linux

// Define feature test macros before any includes to ensure POSIX functions are available
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
// #ifndef _GNU_SOURCE
// #define _GNU_SOURCE
// #endif
// #ifndef _POSIX_C_SOURCE
// #define _POSIX_C_SOURCE 200809L
// #endif

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIx32, PRIu64
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t, uint_fastxx_t
#include <stdio.h>    // for printf

#include <pthread.h>

#ifndef OPTION_ATOMIC_EMULATION
#ifndef __cplusplus
#include <stdatomic.h>
#define ATOMIC_BOOL_TYPE uint_fast8_t
#define ATOMIC_BOOL atomic_uint_fast8_t
#else
// For C++, use <atomic> instead of <stdatomic.h>
#include <atomic>
#define ATOMIC_BOOL_TYPE uint_fast8_t
#define ATOMIC_BOOL std::atomic<uint_fast8_t>
#endif
#endif

#else

#error "Please define platform _WIN or _MACOS or _LINUX"

#endif

//-------------------------------------------------------------------------------
// Compile options

#include "main_cfg.h" // for OPTION_xxx

//-------------------------------------------------------------------------------
// Keyboard

#ifdef OPTION_ENABLE_KEYBOARD

#ifdef _LINUX
#include <termios.h>
int _getch(void);
int _kbhit(void);
#endif

#ifdef _WIN
#include <conio.h>
#endif

#endif // PLATFORM_ENABLE_KEYBOARD

//-------------------------------------------------------------------------------
// Safe sprintf, strncpy, ...

#include <stdio.h>
#include <string.h>

// Portable implementation of strnlen for systems that don't have it
static inline size_t safe_strnlen(const char *s, size_t maxlen) {
    size_t len = 0;
    while (len < maxlen && s[len] != '\0') {
        len++;
    }
    return len;
}

#if defined(_WIN) // Windows

#define SPRINTF(dest, format, ...) sprintf_s((char *)dest, sizeof(dest), format, __VA_ARGS__)
#define SNPRINTF(dest, len, format, ...) sprintf_s((char *)dest, len, format, __VA_ARGS__)
#define STRNCPY(dest, src, n) strncpy(dest, src, n)
#define STRNLEN(s, n) strnlen_s(s, n)

#elif defined(_LINUX) // Linux

#define SPRINTF(dest, format, ...) snprintf((char *)dest, sizeof(dest), format, __VA_ARGS__)
#define SNPRINTF(dest, len, format, ...) snprintf((char *)dest, len, format, __VA_ARGS__)
#define STRNCPY strncpy
#define STRNLEN safe_strnlen

#endif

#ifdef __cplusplus
extern "C" {
#endif

//-------------------------------------------------------------------------------
// Delay

// Delay based on XCP clock
// Busy waits on low durations
void sleepUs(uint32_t us);

// Delay - Less precise and less CPU load, not based on XCP clock, time domain different
void sleepMs(uint32_t ms);

//-------------------------------------------------------------------------------
// Mutex

#if defined(_WIN) // Windows

#define MUTEX CRITICAL_SECTION
#define mutexLock EnterCriticalSection
#define mutexUnlock LeaveCriticalSection

#elif defined(_LINUX) // Linux

#define MUTEX pthread_mutex_t
#define MUTEX_INTIALIZER PTHREAD_MUTEX_INITIALIZER
#define mutexLock pthread_mutex_lock
#define mutexUnlock pthread_mutex_unlock

#endif

void mutexInit(MUTEX *m, bool recursive, uint32_t spinCount);
void mutexDestroy(MUTEX *m);

//-------------------------------------------------------------------------------
// Threads

#if defined(_WIN) // Windows

typedef HANDLE THREAD;
#define create_thread(h, t) *h = CreateThread(0, 0, t, NULL, 0, NULL)
#define join_thread(h) WaitForSingleObject(h, INFINITE);
#define cancel_thread(h)                                                                                                                                                           \
    {                                                                                                                                                                              \
        TerminateThread(h, 0);                                                                                                                                                     \
        WaitForSingleObject(h, 1000);                                                                                                                                              \
        CloseHandle(h);                                                                                                                                                            \
    }

#elif defined(_LINUX) // Linux

typedef pthread_t THREAD;
#define create_thread(h, t) pthread_create(h, NULL, t, NULL)
#define join_thread(h) pthread_join(h, NULL)
#define cancel_thread(h)                                                                                                                                                           \
    {                                                                                                                                                                              \
        pthread_detach(h);                                                                                                                                                         \
        pthread_cancel(h);                                                                                                                                                         \
    }
#define yield_thread(void) sched_yield(void)

#endif

//-------------------------------------------------------------------------------
// Thread local storage

#ifdef __cplusplus
#define THREAD_LOCAL thread_local
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define THREAD_LOCAL _Thread_local
#elif defined(__GNUC__)
#define THREAD_LOCAL __thread
#elif defined(_MSC_VER)
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL static // Fallback to static (not thread-safe)
#error "Thread-local storage not supported"
#endif

//-------------------------------------------------------------------------------
// Platform independent socket functions

#if defined(OPTION_ENABLE_TCP) || defined(OPTION_ENABLE_UDP)

#ifdef _LINUX // Linux or macOS sockets

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define SOCKET int
#define INVALID_SOCKET (-1)

#define SOCKADDR_IN struct sockaddr_in
#define SOCKADDR struct sockaddr

#define SOCKET_ERROR_ABORT EBADF
#define SOCKET_ERROR_RESET EBADF
#define SOCKET_ERROR_INTR EBADF
#define SOCKET_ERROR_WBLOCK EAGAIN

#undef htonll
#define htonll(val) ((((uint64_t)htonl((uint32_t)val)) << 32) + htonl((uint32_t)(val >> 32)))

#define socketGetLastError(void) errno

#endif

#if defined(_WIN) // Windows // Windows sockets or XL-API

#include <winsock2.h>
#include <ws2tcpip.h>

#define SOCKET_ERROR_OTHER 1
#define SOCKET_ERROR_WBLOCK WSAEWOULDBLOCK
#define SOCKET_ERROR_ABORT WSAECONNABORTED
#define SOCKET_ERROR_RESET WSAECONNRESET
#define SOCKET_ERROR_INTR WSAEINTR

int32_t socketGetLastError(void);

#endif

// Timestamp mode
#define SOCKET_TIMESTAMP_NONE 0    // No timestamps
#define SOCKET_TIMESTAMP_HW 1      // Hardware clock
#define SOCKET_TIMESTAMP_HW_SYNT 2 // Hardware clock syntonized to PC clock
#define SOCKET_TIMESTAMP_PC 3      // PC clock

// Clock mode
#define SOCKET_TIMESTAMP_FREE_RUNNING 0
#define SOCKET_TIMESTAMP_SOFTWARE_SYNC 1

bool socketStartup(void);
void socketCleanup(void);
bool socketOpen(SOCKET *sp, bool useTCP, bool nonBlocking, bool reuseaddr, bool timestamps);
bool socketBind(SOCKET sock, uint8_t *addr, uint16_t port);
bool socketJoin(SOCKET sock, uint8_t *maddr);
bool socketListen(SOCKET sock);
SOCKET socketAccept(SOCKET sock, uint8_t *addr);
int16_t socketRecv(SOCKET sock, uint8_t *buffer, uint16_t bufferSize, bool waitAll);
int16_t socketRecvFrom(SOCKET sock, uint8_t *buffer, uint16_t bufferSize, uint8_t *srcAddr, uint16_t *srcPort, uint64_t *time);
int16_t socketSend(SOCKET sock, const uint8_t *buffer, uint16_t bufferSize);
int16_t socketSendTo(SOCKET sock, const uint8_t *buffer, uint16_t bufferSize, const uint8_t *addr, uint16_t port, uint64_t *time);
bool socketShutdown(SOCKET sock);
bool socketClose(SOCKET *sp);
#ifdef OPTION_ENABLE_GET_LOCAL_ADDR
bool socketGetLocalAddr(uint8_t *mac, uint8_t *addr);
#endif

#endif

//-------------------------------------------------------------------------------
// High resolution clock

#ifdef OPTION_CLOCK_TICKS_1NS

#define CLOCK_TICKS_PER_M (1000000000ULL * 60)
#define CLOCK_TICKS_PER_S 1000000000
#define CLOCK_TICKS_PER_MS 1000000
#define CLOCK_TICKS_PER_US 1000
#define CLOCK_TICKS_PER_NS 1

#else

#ifndef OPTION_CLOCK_TICKS_1US
#error "Please define OPTION_CLOCK_TICKS_1NS or OPTION_CLOCK_TICKS_1US"
#endif

#define CLOCK_TICKS_PER_S 1000000
#define CLOCK_TICKS_PER_MS 1000
#define CLOCK_TICKS_PER_US 1

#endif

// Clock
bool clockInit(void);
uint64_t clockGet(void);
uint64_t clockGetLast(void);
uint64_t clockGetUs(void);
uint64_t clockGetNs(void);
char *clockGetString(char *s, uint32_t l, uint64_t c);
char *clockGetTimeString(char *s, uint32_t l, int64_t c);

//-------------------------------------------------------------------------------
// Atomic operations

// Atomic operations emulation for Windows
#ifdef OPTION_ATOMIC_EMULATION

#ifdef _WIN32_
#error "Windows32 not implemented yet"
#endif

// On Windows 64 we rely on the x86-64 strong memory model and assume atomic 64 bit load/store
// Use a mutex for thread safe atomic_fetch_add/sub and atomic_compare_exchange
// The windows version is for demonstration and test purposes, not optimized for minimal locking overhead
#define memory_order_acq_rel 0
#define memory_order_relaxed 0
#define memory_order_acquire 0
#define memory_order_release 0

#define atomic_uint_fast64_t uint64_t
#define atomic_uintptr_t uintptr_t
#define atomic_uint_fast8_t uint64_t

#define ATOMIC_BOOL_TYPE uint64_t
#define ATOMIC_BOOL uint64_t

#define atomic_store_explicit(a, b, c) (*(a)) = (b)
#define atomic_load_explicit(a, b) (*(a))

extern MUTEX gWinMutex;

uint64_t atomic_exchange_explicit(uint64_t *a, uint64_t b, int c);
uint64_t atomic_fetch_add_explicit(uint64_t *a, uint64_t b, int c);
uint64_t atomic_fetch_sub_explicit(uint64_t *a, uint64_t b, int c);
bool atomic_compare_exchange_strong_explicit(uint64_t *a, uint64_t *b, uint64_t c, int d, int e);
bool atomic_compare_exchange_weak_explicit(uint64_t *a, uint64_t *b, uint64_t c, int d, int e);
bool atomic_compare_exchange_strong_explicit(uint64_t *a, uint64_t *b, uint64_t c, int d, int e);
bool atomic_compare_exchange_weak_explicit(uint64_t *a, uint64_t *b, uint64_t c, int d, int e);

#endif // OPTION_ATOMIC_EMULATION

#ifdef __cplusplus
} // extern "C"
#endif
