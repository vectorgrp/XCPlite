#pragma once
#define __PLATFORM_H__

/*
| Code released into public domain, no attribution required
*/

//-------------------------------------------------------------------------------------------------
// Platform defines

// 64 Bit or 32 Bit platform
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_WIN64)
#define PLATFORM_64BIT
#else
#define PLATFORM_32BIT
#endif

// Windows or Linux/macOS ?
#if defined(_WIN32) || defined(_WIN64)

#define _WIN

#if defined(_WIN32) && defined(_WIN64)
// #error "defined(_WIN32) && defined(_WIN64)"
#undef _WIN32 // @@@@ TODO: Remove this line
#endif
#if defined(_LINUX) || defined(_LINUX64) || defined(_LINUX32)
#error "defined(_LINUX) || defined(_LINUX64) || defined(_LINUX32)"
#endif

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
#define _CRT_SECURE_NO_WARNINGS
#else
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif

#if !defined(_WIN) && !defined(_LINUX) && !defined(_MACOS)
#error "Please define platform _WIN or _MACOS or _LINUX"
#endif

//-------------------------------------------------------------------------------------------------
// Platform specific functions

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#if defined(_WIN)

#define _CRT_SECURE_NO_WARNINGS
#include <time.h>
#include <windows.h>

#elif defined(_LINUX) || defined(_MACOS) // Linux

#include <pthread.h>

#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>

#else

#error "Please define platform _WIN or _MACOS or _LINUX"

#endif

#include "main_cfg.h" // for OPTION_xxx

#if !defined(_WIN) && !defined(_LINUX) && !defined(_MACOS)
#error "Please define platform _WIN, _MACOS or _LINUX"
#endif

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

#if defined(_WIN) // Windows

#define SPRINTF(dest, format, ...) sprintf_s((char *)dest, sizeof(dest), format, __VA_ARGS__)
#define SNPRINTF(dest, len, format, ...) sprintf_s((char *)dest, len, format, __VA_ARGS__)
#define STRNCPY(dest, src, n) strncpy(dest, src, n)
#define STRNLEN(s, n) strnlen_s(s, n)

#elif defined(_LINUX) // Linux

#define SPRINTF(dest, format, ...) snprintf((char *)dest, sizeof(dest), format, __VA_ARGS__)
#define SNPRINTF(dest, len, format, ...) snprintf((char *)dest, len, format, __VA_ARGS__)
#define STRNCPY strncpy
#define STRNLEN strnlen

#endif

//-------------------------------------------------------------------------------
// Delay

// Delay based on clock
void sleepNs(uint32_t ns);

// Delay - Less precise and less CPU load, not based on clock, time domain different
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
// Platform independent socket functions

#if defined(OPTION_ENABLE_TCP) || defined(OPTION_ENABLE_UDP)

#ifdef _LINUX // Linux sockets

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
char *clockGetString(char *s, uint32_t l, uint64_t c);
char *clockGetTimeString(char *s, uint32_t l, int64_t c);

//-------------------------------------------------------------------------------
// Atomic operations

#ifndef _WIN
#include <stdatomic.h>
#else
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

#define atomic_bool uint8_t
#define atomic_uint_fast64_t uint64_t
#define atomic_uint_fast32_t uint32_t
#define atomic_uintptr_t uintptr_t
#define atomic_uint_fast8_t uint8_t
#define atomic_uint_fast8_t uint8_t

#define atomic_store_explicit(a, b, c) (*(a)) = (b)
#define atomic_load_explicit(a, b) (*(a))

extern MUTEX gWinMutex;

#define atomic_fetch_add_explicit(a, b, c)                                                                                                                                         \
    {                                                                                                                                                                              \
        mutexLock(&gWinMutex);                                                                                                                                                     \
        (*(a)) += (b);                                                                                                                                                             \
        mutexUnlock(&gWinMutex);                                                                                                                                                   \
    }

#define atomic_fetch_sub_explicit(a, b, c)                                                                                                                                         \
    {                                                                                                                                                                              \
        mutexLock(&gWinMutex);                                                                                                                                                     \
        (*(a)) -= (b);                                                                                                                                                             \
        mutexUnlock(&gWinMutex);                                                                                                                                                   \
    }

bool atomic_compare_exchange_strong_explicit(uint8_t *a, uint8_t *b, uint8_t c, int d, int e);
bool atomic_compare_exchange_weak_explicit(uint8_t *a, uint8_t *b, uint8_t c, int d, int e);

#endif
