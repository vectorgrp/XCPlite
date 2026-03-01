#pragma once
#define __PLATFORM_H__

/*----------------------------------------------------------------------------
| File:
|   platform.h
|
| Description:
|   Platform OS (Linux/Windows/MACOS/QNX) abstraction layer
|     Atomics
|     Sleep
|     Threads
|     Mutex
|     Sockets
|     Clock
|     Virtual memory
|     Keyboard
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

//-------------------------------------------------------------------------------------------------
// Platform defines

// 64 Bit or 32 Bit platform
#if defined(_ix64_) || defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_WIN64)
#define PLATFORM_64BIT
#else
#define PLATFORM_32BIT
#endif

// Windows
#if defined(_WIN32) || defined(_WIN64)

#define _WIN

#define WIN32_LEAN_AND_MEAN

// Linux or macOS or QNX
#else

#if defined(PLATFORM_64BIT)

#if defined(__APPLE__)
#define _MACOS
#elif defined(__QNXNTO__) || defined(__QNX__)
#define _QNX
#else
#define _LINUX
#endif

#else

#error "32 Bit *X OS currently not supported"

#endif

#endif

#if !defined(_WIN) && !defined(_LINUX) && !defined(_MACOS) && !defined(_QNX)
#error "Please define platform _WIN or _MACOS or _LINUX or _QNX"
#endif

//-------------------------------------------------------------------------------------------------
// Compilation options

/*
OPTION_ATOMIC_EMULATION
OPTION_ENABLE_KEYBOARD
OPTION_ENABLE_TCP and/or OPTION_ENABLE_UDP
OPTION_SOCKET_HW_TIMESTAMPS
OPTION_ENABLE_GET_LOCAL_ADDR
OPTION_CLOCK_TICKS_1NS or OPTION_CLOCK_TICKS_1US
OPTION_CLOCK_EPOCH_ARB or OPTION_CLOCK_EPOCH_PTP
*/
#include "xcplib_cfg.h" // for OPTION_xxx

//-------------------------------------------------------------------------------------------------
// Platform specific functions

#if defined(_WIN)

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIx32, PRIu64
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t, uint_fastxx_t
#include <stdio.h>    // for printf
#include <time.h>
#include <windows.h>

#else

// Define feature test macros before any includes to ensure POSIX functions are available
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
// #ifndef _POSIX_C_SOURCE
// #define _POSIX_C_SOURCE 200809L
// #endif

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIx32, PRIu64
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t, uint_fastxx_t
#include <stdio.h>    // for printf

#include <net/if.h> // for IFNAMSIZ
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

#endif

//-------------------------------------------------------------------------------
// Keyboard

#ifdef OPTION_ENABLE_KEYBOARD

#if !defined(_WIN) // Non-Windows platforms

#include <termios.h>
int _getch(void);
int _kbhit(void);

#else

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
    if (s != NULL) {
        while (len < maxlen && s[len] != '\0') {
            len++;
        }
    }
    return len;
}

#if defined(_WIN) // Windows

#define SPRINTF(dest, format, ...) sprintf_s((char *)dest, sizeof(dest), format, __VA_ARGS__)
#define SNPRINTF(dest, len, format, ...) sprintf_s((char *)dest, len, format, __VA_ARGS__)
#define STRNCPY(dest, src, n) strncpy(dest, src, n)
#define STRNLEN(s, n) strnlen_s(s, n)

#else

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
// Memory mapping (platform abstraction)

void *platformMemAlloc(size_t size);
void platformMemFree(void *ptr, size_t size);

#if !defined(_WIN) && defined(OPTION_SHM_MODE) // POSIX shared memory — not available on Windows
// Open or create a named POSIX shared-memory region of `size` bytes.
// `name`      : SHM object name, e.g. "/xcpdata"
// `lock_path` : path for an flock-based serialisation lock, e.g. "/tmp/xcpdata.lock"
// `size`      : size of the region in bytes
// `is_leader` : set to true when this process created the SHM (first caller)
// Leader receives a zero-initialised region; followers must wait for the leader
// to complete initialisation before using the shared data.
// Returns a writable pointer to the mapped region, or NULL on error.
void *platformShmOpen(const char *name, const char *lock_path, size_t size, bool *is_leader);
// Attach to an already-existing SHM region as a follower.
// Uses fstat to determine the actual mapped size (written into *size_out).
// Does NOT participate in leader election — use only when the caller is certain it is a follower.
void *platformShmOpenAttach(const char *name, size_t *size_out);
// Unmap a previously opened SHM region. If is_leader, also calls shm_unlink().
void platformShmClose(const char *name, void *ptr, size_t size, bool is_leader);
#endif // !_WIN && OPTION_SHM_MODE

//-------------------------------------------------------------------------------
// Mutex

#if defined(_WIN) // Windows

#define MUTEX CRITICAL_SECTION
#define mutexLock EnterCriticalSection
#define mutexUnlock LeaveCriticalSection

#else

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
#define get_thread_id() GetCurrentThreadId()

#else

typedef pthread_t THREAD;
#define create_thread(h, t) pthread_create(h, NULL, t, NULL)
#define create_thread_arg(h, t, p) pthread_create(h, NULL, t, p)
#define join_thread(h) pthread_join(h, NULL)
#define cancel_thread(h)                                                                                                                                                           \
    {                                                                                                                                                                              \
        pthread_detach(h);                                                                                                                                                         \
        pthread_cancel(h);                                                                                                                                                         \
    }
#define yield_thread(void) sched_yield(void)
#define get_thread_id() ((uint32_t)(uintptr_t)pthread_self())

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

#if !defined(_WIN) // Non-Windows platforms

#ifndef OPTION_DISABLE_VECTORED_IO
#include "queue.h" // for tQueueBuffer and vectored io functions
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define SOCKET int
#define INVALID_SOCKET (-1)

struct socket {
    SOCKET sock;    // Socket handle
    uint32_t addr;  // Bind address (network byte order) maybe INADDR_ANY
    uint16_t port;  // Port
    uint16_t flags; // Socket mode flags

    // Linux only:
    unsigned int ifindex; // Interface index
    char ifname[16];      // Interface name
    uint32_t ifaddr;      // Interface address
    uint8_t ifmac[6];     // Interface MAC address
};

typedef struct socket *SOCKET_HANDLE;
#define INVALID_SOCKET_HANDLE NULL

#define SOCKADDR_IN struct sockaddr_in
#define SOCKADDR struct sockaddr

#define SOCKET_ERROR_ABORT ECONNABORTED
#define SOCKET_ERROR_RESET ECONNRESET
#define SOCKET_ERROR_INTR EINTR
#define SOCKET_ERROR_WBLOCK EAGAIN
#define SOCKET_ERROR_PIPE EPIPE
#define SOCKET_ERROR_BADF EBADF

#undef htonll
#define htonll(val) ((((uint64_t)htonl((uint32_t)(val))) << 32) + htonl((uint32_t)((val) >> 32)))

#define socketGetLastError(void) errno

#else // Windows sockets

#include <winsock2.h>
#include <ws2tcpip.h>

struct socket {
    SOCKET sock;    // Socket handle
    uint32_t addr;  // Bind address (network byte order) maybe INADDR_ANY
    uint16_t port;  // Port
    uint16_t flags; // Socket mode flags
};

typedef struct socket *SOCKET_HANDLE;
#define INVALID_SOCKET_HANDLE NULL

#define SOCKADDR_IN struct sockaddr_in
#define SOCKADDR struct sockaddr

#define SOCKET_ERROR_OTHER 1
#define SOCKET_ERROR_WBLOCK WSAEWOULDBLOCK
#define SOCKET_ERROR_ABORT WSAECONNABORTED
#define SOCKET_ERROR_RESET WSAECONNRESET
#define SOCKET_ERROR_INTR WSAEINTR
#define SOCKET_ERROR_PIPE WSAECONNRESET // No EPIPE on Windows; connection reset is the closest equivalent
#define SOCKET_ERROR_BADF WSAEBADF

int32_t socketGetLastError(void);

#endif

// Socket mode flags
#define SOCKET_MODE_TCP (1 << 0)       // TCP socket
#define SOCKET_MODE_BLOCKING (1 << 1)  // Blocking socket
#define SOCKET_MODE_REUSEADDR (1 << 2) // Allow reuse of local address

#define SOCKET_MODE_HW_TIMESTAMPING (1 << 4) // Enable hardware timestamping (Linux only, requires root)
#define SOCKET_MODE_SW_TIMESTAMPING (1 << 5) // Enable kernel software timestamping (Linux only, requires root)
#define SOCKET_MODE_GET_IF_INFO (1 << 6)     // Check interface info on recv

// Socket functions
bool socketStartup(void);
void socketCleanup(void);
bool socketOpen(SOCKET_HANDLE *socketp, uint16_t flags);
bool socketBind(SOCKET_HANDLE socket, const uint8_t *addr, uint16_t port);
bool socketBindToDevice(SOCKET_HANDLE socket, const char *ifname); // Bind socket to a specific network interface (Linux only, requires root for non-INADDR_ANY)
bool socketEnableTimestamps(SOCKET_HANDLE socket, bool ptpOnly);   // Enable timestamping (Linux only, requires root)
bool socketJoin(SOCKET_HANDLE socket, const uint8_t *maddr, const uint8_t *ifaddr, const char *ifname);
bool socketListen(SOCKET_HANDLE socket);
SOCKET_HANDLE socketAccept(SOCKET_HANDLE socket, uint8_t *addr);
int16_t socketRecv(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t bufferSize, bool waitAll);
int16_t socketRecvFrom(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t bufferSize, uint8_t *srcAddr, uint16_t *srcPort, uint64_t *time);
int16_t socketSendTo(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t bufferSize, const uint8_t *addr, uint16_t port, uint64_t *time);
int16_t socketSend(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t bufferSize);
#if !defined(_WIN) && !defined(OPTION_DISABLE_VECTORED_IO)
// Non-Windows: vectored send functions
int16_t socketSendToV(SOCKET_HANDLE socket, tQueueBuffer buffers[], uint16_t count, const uint8_t *addr, uint16_t port);
int16_t socketSendV(SOCKET_HANDLE socket, tQueueBuffer buffers[], uint16_t count);
#endif
bool socketGetSendTime(SOCKET_HANDLE socket, uint64_t *txHwTime, uint64_t *txSwTime);
bool socketShutdown(SOCKET_HANDLE socket);     // Shutdown socket for read and write
bool socketClose(SOCKET_HANDLE *socketp);      // Close socket
bool socketGetMAC(char *ifname, uint8_t *mac); // Helper to get MAC address of a network interface by name
#ifdef OPTION_ENABLE_GET_LOCAL_ADDR
bool socketGetLocalAddr(uint8_t *mac, uint8_t *addr); // Helper to get local IP address and MAC address of the first non-loopback interface
#endif

#endif

//-------------------------------------------------------------------------------
// High resolution clock

#ifdef OPTION_CLOCK_TICKS_1NS

#define CLOCK_TICKS_PER_M (1000000000ULL * 60)
#define CLOCK_TICKS_PER_S 1000000000ULL
#define CLOCK_TICKS_PER_MS 1000000ULL
#define CLOCK_TICKS_PER_US 1000ULL
#define CLOCK_TICKS_PER_NS 1ULL

#else

#ifndef OPTION_CLOCK_TICKS_1US
#error "Please define OPTION_CLOCK_TICKS_1NS or OPTION_CLOCK_TICKS_1US"
#endif

#define CLOCK_TICKS_PER_S 1000000
#define CLOCK_TICKS_PER_MS 1000
#define CLOCK_TICKS_PER_US 1

#endif

// Clock (as used by XCP, epoch and resolution configured in xcplib_cfg.h)
bool clockInit(void);
uint64_t clockGet(void);
uint64_t clockGetLast(void);
char *clockGetString(char *s, uint32_t l, uint64_t c);
char *clockGetTimeString(char *s, uint32_t l, int64_t c);

// Monotonic system clock in nanoseconds
uint64_t clockGetMonotonicNs(void);
// Monotonic system clock in microseconds
uint64_t clockGetMonotonicUs(void);
// Realtime system clock in nanoseconds
uint64_t clockGetRealtimeNs(void);

#ifdef TEST_CLOCK_GET_STATISTIC
void clockGetPrintStatistic(void);
#endif

//-------------------------------------------------------------------------------
// File system utilities

// Check if a file exists
// Returns true if the file exists and is accessible, false otherwise
bool fexists(const char *filename);

//-------------------------------------------------------------------------------
// Atomic operations

// Atomic operations emulation for Windows
#ifdef OPTION_ATOMIC_EMULATION

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
#define atomic_uint_fast16_t uint64_t

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
