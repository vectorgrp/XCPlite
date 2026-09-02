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

// XCPlite uniquely uses _WIN, _LINUX, _MACOS, _QNX or _FREE_RTOS for platform specific code paths

// 64 Bit or 32 Bit platform
#if defined(_ix64_) || defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_WIN64)
#define PLATFORM_64BIT
#else
#define PLATFORM_32BIT
#endif

// FreeRTOS
#if defined(__FreeRTOS__) || defined(FREERTOS) || defined(_FREERTOS) || defined(__FREERTOS) || defined(_FREE_RTOS) || defined(FREE_RTOS)

#ifndef _FREE_RTOS // may already be defined as 1 via -D_FREE_RTOS on the compiler command line
#define _FREE_RTOS
#endif

// Windows
#elif defined(_WIN32) || defined(_WIN64)

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

#error "Platform not supported"

#endif

#endif

#if !defined(_WIN) && !defined(_LINUX) && !defined(_MACOS) && !defined(_QNX) && !defined(_FREE_RTOS)
#error "Please define platform _WIN, _MACOS, _LINUX, _QNX or _FREE_RTOS"
#endif

//-------------------------------------------------------------------------------------------------
// Compilation options

/*
OPTION_ATOMIC_EMULATION
OPTION_ENABLE_KEYBOARD
OPTION_ENABLE_TCP and/or OPTION_ENABLE_UDP
OPTION_SOCKET_HW_TIMESTAMPS (for Linux PTP tooling only)
OPTION_ENABLE_GET_LOCAL_ADDR
OPTION_CLOCK_TICKS_1NS or OPTION_CLOCK_TICKS_1US
OPTION_CLOCK_EPOCH_ARB or OPTION_CLOCK_EPOCH_PTP
*/
#include "xcplib_cfg.h" // for OPTION_xxx in xcplib context

//-------------------------------------------------------------------------------------------------

#include <inttypes.h> // for PRIx32, PRIu64
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t, uint_fastxx_t

#if !defined(__cplusplus) && !defined(static_assert)
#define static_assert _Static_assert
#endif

//-------------------------------------------------------------------------------------------------
// Platform specific functions

#if defined(_WIN)

#include <time.h>
#include <windows.h>

#elif defined(_FREE_RTOS)

// FreeRTOS kernel headers – required for task, semaphore, and timer APIs.
// On the POSIX simulator these are backed by pthreads; on the target they use the port-specific implementation.
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

// Note on C11 atomics for FreeRTOS targets:
// This code avoids 64-bit atomics on 32-bit embedded targets.
// ESP32-S3 / Xtensa LX7:
//   32-bit RMW atomics such as fetch_add use a CAS loop; contention can add retries.
//   atomic_uint_fast8_t is 32 bits with this toolchain, so ATOMIC_BOOL is word-sized.
// STM32:
//   On Cortex-M3/M4/M7/M33, 32-bit RMW atomics are typically implemented with
//   LDREX/STREX. 64-bit atomics are not native
#ifndef __cplusplus
#include <stdatomic.h>
#define ATOMIC_BOOL_TYPE uint_fast8_t
#define ATOMIC_BOOL atomic_uint_fast8_t
#else
#include <atomic>
#define ATOMIC_BOOL_TYPE uint_fast8_t
#define ATOMIC_BOOL std::atomic<uint_fast8_t>
using std::atomic_uint_fast16_t;
using std::atomic_uint_fast32_t;
using std::atomic_uint_fast64_t;
using std::atomic_uint_fast8_t;
using std::atomic_uint_least16_t;
using std::atomic_uint_least32_t;
using std::atomic_uint_least64_t;
using std::atomic_uint_least8_t;
#endif

// When testing FreeRTOS code paths on macOS/Linux, we use OS-specific sockets and clock code in platform.c
#if defined(FREE_RTOS_POSIX_SIM)

#if defined(__APPLE__)
#define _MACOS
#elif defined(__linux__)
#define _LINUX
#else
#error "Unsupported host OS for FreeRTOS POSIX simulator"
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <net/if.h>
#include <pthread.h>
#include <unistd.h>

#endif // FREE_RTOS_POSIX_SIM

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

#include <net/if.h>  // for IFNAMSIZ
#include <pthread.h> // for pthread_mutex

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
// Bring C11 <stdatomic.h> named types into the global namespace so C headers that use atomic_uint_fast*_t compile cleanly in C++ mode.
using std::atomic_uint_fast16_t;
using std::atomic_uint_fast32_t;
using std::atomic_uint_fast64_t;
using std::atomic_uint_fast8_t;
using std::atomic_uint_least16_t;
using std::atomic_uint_least32_t;
using std::atomic_uint_least64_t;
using std::atomic_uint_least8_t;
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

#include <stdio.h>  // for sprintf, snprintf
#include <string.h> // for strnlen

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
#define STRNLEN(s, n) strnlen_s(s, n)

#else

#define SPRINTF(dest, format, ...) snprintf((char *)dest, sizeof(dest), format, __VA_ARGS__)
#define SNPRINTF(dest, len, format, ...) snprintf((char *)dest, len, format, __VA_ARGS__)
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

//-------------------------------------------------------------------------------
// Shared memory

#if !defined(_WIN) && !defined(_FREE_RTOS)

// Open or create a named POSIX shared-memory region of `size` bytes.
// `name`      : SHM object name, e.g. "/data"
// `lock_path` : path for an flock-based serialization lock, e.g. "/tmp/data.lock"
// `size`      : size of the region in bytes
// `is_leader` : set to true when this process created the SHM (first caller)
// Leader receives a zero-initialised region; followers must wait for the leader to complete initialisation before using the shared data.
// Returns a writable pointer to the mapped region, or NULL on error.
void *platformShmOpen(const char *name, const char *lock_path, size_t size, bool *is_leader);

// Attach to an already-existing SHM region as a follower.
// Uses fstat to determine the actual mapped size (written into *size_out).
// Does NOT participate in leader election — use only when the caller is certain it is a follower.
// Much faster than platformShmOpen
void *platformShmOpenAttach(const char *name, size_t *size_out);

// Unmap a previously opened SHM region. If unlink is true, also calls shm_unlink().
void platformShmClose(const char *name, void *ptr, size_t size, bool unlink);

// Remove the SHM name without unmapping. Safe to call while the segment is still mapped.
// Prevents new processes from attaching; existing mappings remain valid.
void platformShmUnlink(const char *name);

#endif // !_WIN

//-------------------------------------------------------------------------------
// Mutex

#if defined(_WIN) // Windows

#define MUTEX CRITICAL_SECTION
#define MUTEX_INTIALIZER {0}
#define mutexLock EnterCriticalSection
#define mutexUnlock LeaveCriticalSection

#elif defined(_FREE_RTOS) // FreeRTOS

typedef struct {
    SemaphoreHandle_t handle;
#if configUSE_RECURSIVE_MUTEXES == 1
    bool recursive;
#endif
} MUTEX;

#if configUSE_RECURSIVE_MUTEXES == 1
#define MUTEX_INTIALIZER {NULL, false}
#else
#define MUTEX_INTIALIZER {NULL}
#endif

// Callers always pass &mutex (matching the POSIX pattern where MUTEX is a struct).
// Select the matching FreeRTOS API when recursive mutexes are enabled.
#if configUSE_RECURSIVE_MUTEXES == 1
#define mutexLock(m) ((m)->recursive ? xSemaphoreTakeRecursive((m)->handle, portMAX_DELAY) : xSemaphoreTake((m)->handle, portMAX_DELAY))
#define mutexUnlock(m) ((m)->recursive ? xSemaphoreGiveRecursive((m)->handle) : xSemaphoreGive((m)->handle))
#else
#define mutexLock(m) xSemaphoreTake((m)->handle, portMAX_DELAY)
#define mutexUnlock(m) xSemaphoreGive((m)->handle)
#endif

#else // Other

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

typedef HANDLE THREAD_HANDLE;
#define create_thread(thread_handle_ptr, attr, thread, args) *thread_handle_ptr = CreateThread(0, 0, thread, args, 0, NULL)
#define join_thread(h) WaitForSingleObject(h, INFINITE);
#define cancel_thread(h)                                                                                                                                                           \
    do {                                                                                                                                                                           \
        TerminateThread(h, 0);                                                                                                                                                     \
        WaitForSingleObject(h, 1000);                                                                                                                                              \
        CloseHandle(h);                                                                                                                                                            \
    } while (0)
#define get_thread_id() GetCurrentThreadId()

#elif defined(_FREE_RTOS) // FreeRTOS

// Stack depth (in bytes) and priority for internal XCP server tasks.
// Override in xcplib_rtos_cfg.h if the defaults do not fit.
// Note that on the POSIX simulator the FreeRTOS port needs significantly more stack size
#ifndef OPTION_FREERTOS_STACK_BYTES
#define OPTION_FREERTOS_STACK_BYTES (configMINIMAL_STACK_SIZE * sizeof(StackType_t))
#endif
#ifndef OPTION_FREERTOS_PRIORITY
#define OPTION_FREERTOS_PRIORITY (tskIDLE_PRIORITY + 2U)
#endif

typedef TaskHandle_t THREAD_HANDLE;
#if defined(ESP_PLATFORM)
#define FREERTOS_TASK_STACK_DEPTH(stack_bytes) (stack_bytes)
#else
#define FREERTOS_TASK_STACK_DEPTH(stack_bytes) ((stack_bytes) / sizeof(StackType_t))
#endif
#define create_thread(h, _attr, fn, args)                                                                                                                                          \
    do {                                                                                                                                                                           \
        BaseType_t res = xTaskCreate((TaskFunction_t)(fn), #fn, FREERTOS_TASK_STACK_DEPTH(OPTION_FREERTOS_STACK_BYTES), (args), OPTION_FREERTOS_PRIORITY, (h));                    \
        assert(res == pdPASS);                                                                                                                                                     \
    } while (0)
#define join_thread(h) /* No blocking join in FreeRTOS; synchronize via event flag or semaphore */
#define cancel_thread(h) vTaskDelete(h)
#define get_thread_id() ((uint32_t)(uintptr_t)xTaskGetCurrentTaskHandle())

#else // Other

typedef pthread_t THREAD_HANDLE;
#define create_thread(thread_handle_ptr, attr, thread, params) pthread_create(thread_handle_ptr, attr, thread, params)
#define join_thread(h) pthread_join(h, NULL)
#define cancel_thread(h)                                                                                                                                                           \
    do {                                                                                                                                                                           \
        pthread_detach(h);                                                                                                                                                         \
        pthread_cancel(h);                                                                                                                                                         \
    } while (0)
#define yield_thread(void) sched_yield(void)
#define get_thread_id() ((uint32_t)(uintptr_t)pthread_self())

#endif

//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------
// Thread function signature adapter
// Use THREAD_FUNC_RETURN as the return type and THREAD_FUNC_END as the exit statement
// of XCP server thread functions.  This makes a single function body compile correctly
// for all three ABIs: POSIX (void*), Windows (DWORD WINAPI), and FreeRTOS (void).

#if defined(_WIN)
#define THREAD_FUNC_RETURN DWORD WINAPI
#define THREAD_FUNC_END return 0
#elif defined(_FREE_RTOS)
#define THREAD_FUNC_RETURN void
#define THREAD_FUNC_END vTaskDelete(NULL) /* deletes the calling task; does not return */
#else                                     // POSIX
#define THREAD_FUNC_RETURN void *
#define THREAD_FUNC_END return NULL
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

// Note:
// SOCKET_HANDLE is an opaque type that may wrap the OS socket handle and additional info (e.g. for Linux hardware timestamping)
// INVALID_SOCKET_HANDLE is the invalid value for SOCKET_HANDLE
// SOCKET_FD(s) extracts the raw OS socket fd from a SOCKET_HANDLE (which may be a struct socket pointer on Linux with HW timestamps)

#if !defined(_WIN) // Non-Windows platform sockets

#if !defined(_WIN) && !defined(_FREE_RTOS)
#include "queue.h" // for tQueueBuffer
#endif

#define SOCKET int
#define INVALID_SOCKET (-1)

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
// For Linux hardware timestamping support, SOCKET_HANDLE is a pointer to struct socket which contains the socket fd and interface info for timestamp retrieval
struct socket {
    SOCKET sock;
    uint32_t addr;        // Bind address (network byte order) maybe INADDR_ANY
    uint16_t port;        // Port
    unsigned int ifindex; // Interface index
    char ifname[16];      // Interface name
    uint32_t ifaddr;      // Interface address
    uint8_t ifmac[6];     // Interface MAC address
};
typedef struct socket *SOCKET_HANDLE;
#define INVALID_SOCKET_HANDLE NULL
#define SOCKET_FD(s) ((s)->sock) // Extract the OS socket fd from a SOCKET_HANDLE
#else
// Linux (without HW timestamps), FreeRTOS, macOS, QNX: SOCKET_HANDLE is the raw OS fd
typedef SOCKET SOCKET_HANDLE;
#define INVALID_SOCKET_HANDLE INVALID_SOCKET
#define SOCKET_FD(s) (s) // Extract the OS socket fd from a SOCKET_HANDLE
#endif

#define SOCKADDR_IN struct sockaddr_in
#define SOCKADDR struct sockaddr

#undef htonll
#define htonll(val) ((((uint64_t)htonl((uint32_t)(val))) << 32) + htonl((uint32_t)((val) >> 32)))

#include <errno.h> // for errno and error codes from socketGetLastError

#define SOCKET_ERROR_ABORT ECONNABORTED // 53
#define SOCKET_ERROR_RESET ECONNRESET   // 54
#define SOCKET_ERROR_INTR EINTR         // 4
#define SOCKET_ERROR_TIMEDOUT ETIMEDOUT // 60
#define SOCKET_ERROR_WBLOCK EAGAIN      // 35 EWOULDBLOCK is the same as EAGAIN on Linux, but may be different on other platforms
#define SOCKET_ERROR_PIPE EPIPE         // 32
#define SOCKET_ERROR_BADF EBADF         // 9
#define SOCKET_ERROR_NOTCONN ENOTCONN   // 107 (57 macOS) Socket is not connected

#define socketGetLastError(void) errno
#define socketIsClosed(err) ((err) == ENOTCONN || (err) == ECONNABORTED || (err) == EBADF || (err) == ECONNRESET)
#define socketWouldBlock(err) ((err) == EAGAIN || (err) == EWOULDBLOCK)
#define socketTimeout(err) ((err) == ETIMEDOUT || (err) == EAGAIN || (err) == EWOULDBLOCK || (err) == EINTR)

#else // Windows sockets

#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET SOCKET_HANDLE;
#define INVALID_SOCKET_HANDLE INVALID_SOCKET
#define SOCKET_FD(s) (s)

#define SOCKADDR_IN struct sockaddr_in
#define SOCKADDR struct sockaddr

#include <errno.h>                         // for errno and error codes from socketGetLastError
int32_t socketGetLastError(void);
#define SOCKET_ERROR_ABORT WSAECONNABORTED // 10053
#define SOCKET_ERROR_RESET WSAECONNRESET   // 10054
#define SOCKET_ERROR_INTR WSAEINTR         // 10004
#define SOCKET_ERROR_TIMEDOUT WSAETIMEDOUT // 10060
#define SOCKET_ERROR_WBLOCK WSAEWOULDBLOCK // 10035
#define SOCKET_ERROR_PIPE WSAESHUTDOWN     // 10058
#define SOCKET_ERROR_BADF WSAEBADF         // 10009
#define SOCKET_ERROR_NOTCONN WSAENOTCONN   // 10057
#define socketIsClosed(err) ((err) == WSAECONNABORTED || (err) == WSAEBADF || (err) == WSAECONNRESET || (err) == WSAEINTR)
#define socketWouldBlock(err) ((err) == WSAEWOULDBLOCK)
#define socketTimeout(err) ((err) == WSAETIMEDOUT)

#define ssize_t int

#endif

// Socket mode flags
#define SOCKET_MODE_TCP (1 << 0)       // TCP socket
#define SOCKET_MODE_REUSEADDR (1 << 2) // Allow reuse of local address
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
#define SOCKET_MODE_GET_IF_INFO (1 << 6)     // Enable IP_PKTINFO to identify the receiving interface (Linux only)
#define SOCKET_MODE_HW_TIMESTAMPING (1 << 4) // Enable hardware timestamping (Linux only, requires root)
#define SOCKET_MODE_SW_TIMESTAMPING (1 << 5) // Enable kernel software timestamping (Linux only, requires root)
#endif

// Socket functions

// Initialize the socket subsystem (Windows: WSAStartup; no-op on POSIX)
// Must be called once before any other socket function
// Returns true on success
bool socketStartup(void);

// Clean up the socket subsystem (Windows: WSACleanup; no-op on POSIX)
void socketCleanup(void);

// Return a static human-readable string for a SOCKET_ERROR_* error code
// Returns "unknown socket error" for unrecognized codes
const char *socketGetErrorString(int32_t err);

// Create a TCP or UDP socket with the given SOCKET_MODE_xxx flags
// Sockets are always created in blocking mode, a timeout may be set with socketSetTimeout()
// SOCKET_MODE_TCP: TCP stream socket (default: UDP datagram)
// SOCKET_MODE_REUSEADDR: set SO_REUSEADDR to allow rapid port reuse after restart
// SOCKET_MODE_HW_TIMESTAMPING / SOCKET_MODE_SW_TIMESTAMPING: enable timestamps (Linux with hardware timestamps only)
// SOCKET_MODE_GET_IF_INFO: enable IP_PKTINFO to identify the receiving interface (Linux with hardware timestamps only)
// Returns true on success
bool socketOpen(SOCKET_HANDLE *socketp, uint16_t flags);

// Bind socket to a local address and port
// addr: network-byte-order IPv4 address; NULL or 0.0.0.0 binds to INADDR_ANY
// Returns true on success
bool socketBind(SOCKET_HANDLE socket, const uint8_t *addr, uint16_t port);

// Bind socket to a specific network interface by name (Linux only, requires root)
// Useful for multicast reception on a specific interface when bound to INADDR_ANY
// ifname: interface name, e.g. "eth0"; NULL or empty string is a no-op
// Returns true on success (returns true with a warning on non-Linux platforms)
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
bool socketBindToDevice(SOCKET_HANDLE socket, const char *ifname);
#endif

// Configure the NIC driver to generate hardware timestamps (Linux only, requires root)
// Must be called after socketBind; uses the interface name stored by socketBind/socketBindToDevice
// ptpOnly: true = timestamp PTP event packets only; false = timestamp all packets
// Falls back gracefully if the NIC does not support hardware timestamps
// Returns true on success
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
bool socketEnableTimestamps(SOCKET_HANDLE socket, bool ptpOnly);
#endif

// Join an IPv4 multicast group on a UDP socket
// maddr: multicast group address (network byte order)
// Interface selection priority: ifname > ifaddr > INADDR_ANY (kernel routing)
// Returns true on success
bool socketJoin(SOCKET_HANDLE socket, const uint8_t *maddr, const uint8_t *ifaddr, const char *ifname);

// Start listening for incoming TCP connections
// Returns true on success
bool socketListen(SOCKET_HANDLE socket);

// Accept an incoming TCP connection (blocking)
// addr: filled with the remote IPv4 address (network byte order) if non-NULL
// Returns a new connected SOCKET_HANDLE; the caller is responsible for closing it
SOCKET_HANDLE socketAccept(SOCKET_HANDLE socket, uint8_t *addr);

// Receive from a TCP socket (blocking)
// waitAll: true = MSG_WAITALL, block until bufferSize bytes arrive
// Return values:  > 0  bytes received
//                == 0  timeout (set with socketSetRecvTimeout) — no data yet, do background work and loop
//                 < 0  socket closed (graceful or reset) or error — check with socketIsClosed(socketGetLastError()) and exit the receive loop
int16_t socketRecv(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t bufferSize, bool waitAll);

// Receive a UDP datagram (blocking)
// srcAddr / srcPort: filled with sender's address/port if non-NULL
// time: optional receive timestamp (NULL to skip); hardware or software depending on socket flags
// Return values:  > 0  bytes received
//                == 0  timeout (set with socketSetRecvTimeout) — no data yet, do background work and loop
//                 < 0  socket closed or error — check with socketIsClosed(socketGetLastError()) and exit the receive loop
int16_t socketRecvFrom(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t bufferSize, uint8_t *srcAddr, uint16_t *srcPort, uint64_t *time);

// Send a UDP datagram to addr:port
// time: optional send timestamp (NULL to skip)
//       on Linux with HW timestamps: *time is set to 0; call socketGetSendTime() afterwards to retrieve it
//       on other platforms: *time is set to the system clock at send time
// Returns: bytes sent, 0 on closed socket, -1 on error
int16_t socketSendTo(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t bufferSize, const uint8_t *addr, uint16_t port, uint64_t *time);

// Send data on a TCP socket (blocking; loops internally on partial sends)
// Returns: bytes sent, 0 on closed socket, -1 on error
int16_t socketSend(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t bufferSize);

#if !defined(_WIN) && !defined(_FREE_RTOS)
// Send multiple buffers as a single UDP datagram (scatter-gather I/O via sendmsg, POSIX only)
// Returns: total bytes sent, 0 on closed socket, -1 on error (partial UDP sends treated as error)
int16_t socketSendToV(SOCKET_HANDLE socket, tQueueBuffer buffers[], uint16_t count, const uint8_t *addr, uint16_t port);

// Send multiple buffers on a TCP socket (scatter-gather I/O via sendmsg, POSIX only)
// Loops internally until all data is accepted by the kernel
// Returns: total bytes sent, 0 on closed socket, -1 on error
int16_t socketSendV(SOCKET_HANDLE socket, tQueueBuffer buffers[], uint16_t count);
#endif

// Retrieve TX hardware and/or software timestamp after socketSendTo (Linux only)
// Must be called shortly after socketSendTo returned *time==0
// Requires OPTION_SOCKET_HW_TIMESTAMPS and socketEnableTimestamps() to have been called
// txHwTime / txSwTime: set to 0 if the respective timestamp is not available; NULL to skip
// Returns true if at least one requested timestamp was successfully retrieved
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
bool socketGetSendTime(SOCKET_HANDLE socket, uint64_t *txHwTime, uint64_t *txSwTime);
#endif

// Set receive timeout on a blocking socket
// timeoutMs: timeout in milliseconds; 0 = restore infinite blocking
// With a timeout set, socketRecv/socketRecvFrom return 0 on expiry instead of blocking indefinitely,
// allowing the receive thread to perform background work before looping back
// Works for both TCP and UDP; use socketShutdown() to signal a receive thread to exit
// Returns true on success
bool socketSetTimeout(SOCKET_HANDLE socket, uint32_t timeoutMs);

// Shut down both directions of the socket (SHUT_RDWR / SD_BOTH)
// Unblocks a thread currently blocked in socketRecv or socketRecvFrom, causing it to return -1
bool socketShutdown(SOCKET_HANDLE socket);

// Close the OS socket, free the SOCKET_HANDLE, and set *socketp to NULL
// Returns true on success
bool socketClose(SOCKET_HANDLE *socketp);

// Get the MAC address of a network interface by name (e.g. "eth0")
// mac: output buffer, must point to at least 6 bytes
// Returns true on success
bool socketGetMAC(char *ifname, uint8_t *mac);

#ifdef OPTION_ENABLE_GET_LOCAL_ADDR
// Get the IPv4 address and MAC of the first non-loopback Ethernet interface
// mac / addr: output buffers (6 / 4 bytes respectively); either may be NULL
// Result is cached after the first successful call
// Returns true on success
bool socketGetLocalAddr(uint8_t *mac, uint8_t *addr);
#endif

#endif

//-------------------------------------------------------------------------------
// High resolution clock

#if defined(OPTION_CLOCK_TICKS_1NS)

#define CLOCK_TICKS_PER_S 1000000000UL

#elif defined(OPTION_CLOCK_TICKS_1US)

#define CLOCK_TICKS_PER_S 1000000UL

#else

#if !defined(CLOCK_TICKS_PER_S)
#error "Please define CLOCK_TICKS_PER_S and use ApplXcpRegisterGetClockCallback if the clock is not in 1ns or 1us resolution"
#endif

#endif

// Clock (epoch and resolution as configured in CLOCK_TICKS_PER_S
// Used as XCP DAQ clock
bool clockInit(void);
uint64_t clockGet(void);     // Clock value in ticks, epoch and resolution depend on configuration
uint64_t clockGetLast(void); // Last known clock value, updated by all clockGet calls, used to save syscall overhead when the last known clock value is sufficient
char *clockGetString(char *s, uint32_t l, uint64_t c);

// Monotonic system clock in ns or us resolution, with fast query functions for last seen value
// Used for socket timestamping and timeouts
uint64_t clockGetMonotonicNs(void);
uint64_t clockGetMonotonicUs(void);
uint64_t clockGetMonotonicNsLast(void);
uint64_t clockGetMonotonicUsLast(void);

// Realtime system clock in ns or us resolution, with fast query functions for last seen value
uint64_t clockGetRealtimeNs(void);
uint64_t clockGetRealtimeUs(void);
uint64_t clockGetRealtimeNsLast(void);
uint64_t clockGetRealtimeUsLast(void);

#ifdef TEST_CLOCK_GET_STATISTIC
void clockGetPrintStatistic(void);
#endif

//-------------------------------------------------------------------------------
// File system utilities

// Check if a file exists
// Returns true if the file exists and is accessible, false otherwise
bool fexists(const char *filename);

//-------------------------------------------------------------------------------
// Atomic operations emulation for Windows

// Lock-free atomic emulation for Windows using MSVC Interlocked intrinsics.
// Windows only - queue64f and queue64v are excluded on Windows, queue32 uses no atomics.
// Only load, store, CAS and exchange are needed (for ATOMIC_BOOL in xcplite.c and A2L_ONCE_ATOMIC_TYPE in a2l.c).
// Interlocked intrinsics provide full memory barriers for RMW operations.
#ifdef OPTION_ATOMIC_EMULATION

#define memory_order_acq_rel 0
#define memory_order_relaxed 0
#define memory_order_acquire 0
#define memory_order_release 0

#ifdef _WIN

#define atomic_uintptr_t uintptr_t
#define atomic_uint_fast8_t uint8_t
#define atomic_uint_fast16_t uint16_t
#define atomic_uint_least8_t uint8_t
#define atomic_uint_least16_t uint16_t
#define atomic_uint_fast32_t uint32_t
#define atomic_uint_least32_t uint32_t
#define atomic_uint_fast64_t uint64_t

#define ATOMIC_BOOL_TYPE uint8_t
#define ATOMIC_BOOL uint8_t

#include <intrin.h>
#pragma intrinsic(_InterlockedCompareExchange8)
#pragma intrinsic(_InterlockedCompareExchange16)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedCompareExchange64)
#pragma intrinsic(_InterlockedExchange8)
#pragma intrinsic(_InterlockedExchange16)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedExchange64)
#pragma intrinsic(_InterlockedExchangeAdd8)
#pragma intrinsic(_InterlockedExchangeAdd16)
#pragma intrinsic(_InterlockedExchangeAdd)
#pragma intrinsic(_InterlockedExchangeAdd64)

static __inline uint8_t atomic_load_explicit_u8(volatile uint8_t *a) { return (uint8_t)_InterlockedCompareExchange8((volatile char *)a, 0, 0); }
static __inline uint16_t atomic_load_explicit_u16(volatile uint16_t *a) { return (uint16_t)_InterlockedCompareExchange16((volatile short *)a, 0, 0); }
static __inline uint32_t atomic_load_explicit_u32(volatile uint32_t *a) { return (uint32_t)_InterlockedCompareExchange((volatile long *)a, 0, 0); }
static __inline uint64_t atomic_load_explicit_u64(volatile uint64_t *a) { return (uint64_t)_InterlockedCompareExchange64((volatile LONGLONG *)a, 0, 0); }

static __inline void atomic_store_explicit_u8(volatile uint8_t *a, uint8_t b) { (void)_InterlockedExchange8((volatile char *)a, (char)b); }
static __inline void atomic_store_explicit_u16(volatile uint16_t *a, uint16_t b) { (void)_InterlockedExchange16((volatile short *)a, (short)b); }
static __inline void atomic_store_explicit_u32(volatile uint32_t *a, uint32_t b) { (void)_InterlockedExchange((volatile long *)a, (long)b); }
static __inline void atomic_store_explicit_u64(volatile uint64_t *a, uint64_t b) { (void)_InterlockedExchange64((volatile LONGLONG *)a, (LONGLONG)b); }

static __inline uint8_t atomic_exchange_explicit_u8(volatile uint8_t *a, uint8_t b) { return (uint8_t)_InterlockedExchange8((volatile char *)a, (char)b); }
static __inline uint16_t atomic_exchange_explicit_u16(volatile uint16_t *a, uint16_t b) { return (uint16_t)_InterlockedExchange16((volatile short *)a, (short)b); }
static __inline uint32_t atomic_exchange_explicit_u32(volatile uint32_t *a, uint32_t b) { return (uint32_t)_InterlockedExchange((volatile long *)a, (long)b); }
static __inline uint64_t atomic_exchange_explicit_u64(volatile uint64_t *a, uint64_t b) { return (uint64_t)_InterlockedExchange64((volatile LONGLONG *)a, (LONGLONG)b); }

static __inline uint8_t atomic_fetch_add_explicit_u8(volatile uint8_t *a, uint8_t b) { return (uint8_t)_InterlockedExchangeAdd8((volatile char *)a, (char)b); }
static __inline uint16_t atomic_fetch_add_explicit_u16(volatile uint16_t *a, uint16_t b) { return (uint16_t)_InterlockedExchangeAdd16((volatile short *)a, (short)b); }
static __inline uint32_t atomic_fetch_add_explicit_u32(volatile uint32_t *a, uint32_t b) { return (uint32_t)_InterlockedExchangeAdd((volatile long *)a, (long)b); }
static __inline uint64_t atomic_fetch_add_explicit_u64(volatile uint64_t *a, uint64_t b) { return (uint64_t)_InterlockedExchangeAdd64((volatile LONGLONG *)a, (LONGLONG)b); }

static __inline uint8_t atomic_fetch_sub_explicit_u8(volatile uint8_t *a, uint8_t b) { return (uint8_t)_InterlockedExchangeAdd8((volatile char *)a, (char)(-((int8_t)b))); }
static __inline uint16_t atomic_fetch_sub_explicit_u16(volatile uint16_t *a, uint16_t b) {
    return (uint16_t)_InterlockedExchangeAdd16((volatile short *)a, (short)(-((int16_t)b)));
}
static __inline uint32_t atomic_fetch_sub_explicit_u32(volatile uint32_t *a, uint32_t b) { return (uint32_t)_InterlockedExchangeAdd((volatile long *)a, (long)(-(int32_t)b)); }
static __inline uint64_t atomic_fetch_sub_explicit_u64(volatile uint64_t *a, uint64_t b) {
    return (uint64_t)_InterlockedExchangeAdd64((volatile LONGLONG *)a, (LONGLONG)(-(int64_t)b));
}

static __inline bool atomic_compare_exchange_strong_explicit_u8(volatile uint8_t *a, uint8_t *b, uint8_t c) {
    char old = _InterlockedCompareExchange8((volatile char *)a, (char)c, (char)*b);
    if (old == (char)*b)
        return true;
    *b = (uint8_t)old;
    return false;
}
static __inline bool atomic_compare_exchange_strong_explicit_u16(volatile uint16_t *a, uint16_t *b, uint16_t c) {
    short old = _InterlockedCompareExchange16((volatile short *)a, (short)c, (short)*b);
    if (old == (short)*b)
        return true;
    *b = (uint16_t)old;
    return false;
}
static __inline bool atomic_compare_exchange_strong_explicit_u32(volatile uint32_t *a, uint32_t *b, uint32_t c) {
    long old = _InterlockedCompareExchange((volatile long *)a, (long)c, (long)*b);
    if (old == (long)*b)
        return true;
    *b = (uint32_t)old;
    return false;
}
static __inline bool atomic_compare_exchange_strong_explicit_u64(volatile uint64_t *a, uint64_t *b, uint64_t c) {
    LONGLONG old = _InterlockedCompareExchange64((volatile LONGLONG *)a, (LONGLONG)c, (LONGLONG)*b);
    if (old == (LONGLONG)*b)
        return true;
    *b = (uint64_t)old;
    return false;
}

#define atomic_load_explicit(a, b)                                                                                                                                                 \
    (sizeof(*(a)) == 1   ? (uint64_t)atomic_load_explicit_u8((volatile uint8_t *)(a))                                                                                              \
     : sizeof(*(a)) == 2 ? (uint64_t)atomic_load_explicit_u16((volatile uint16_t *)(a))                                                                                            \
     : sizeof(*(a)) == 4 ? (uint64_t)atomic_load_explicit_u32((volatile uint32_t *)(a))                                                                                            \
                         : (uint64_t)atomic_load_explicit_u64((volatile uint64_t *)(a)))

#define atomic_store_explicit(a, b, c)                                                                                                                                             \
    do {                                                                                                                                                                           \
        if (sizeof(*(a)) == 1) {                                                                                                                                                   \
            atomic_store_explicit_u8((volatile uint8_t *)(a), (uint8_t)(b));                                                                                                       \
        } else if (sizeof(*(a)) == 2) {                                                                                                                                            \
            atomic_store_explicit_u16((volatile uint16_t *)(a), (uint16_t)(b));                                                                                                    \
        } else if (sizeof(*(a)) == 4) {                                                                                                                                            \
            atomic_store_explicit_u32((volatile uint32_t *)(a), (uint32_t)(b));                                                                                                    \
        } else {                                                                                                                                                                   \
            atomic_store_explicit_u64((volatile uint64_t *)(a), (uint64_t)(b));                                                                                                    \
        }                                                                                                                                                                          \
    } while (0)

#define atomic_exchange_explicit(a, b, c)                                                                                                                                          \
    (sizeof(*(a)) == 1   ? (uint64_t)atomic_exchange_explicit_u8((volatile uint8_t *)(a), (uint8_t)(b))                                                                            \
     : sizeof(*(a)) == 2 ? (uint64_t)atomic_exchange_explicit_u16((volatile uint16_t *)(a), (uint16_t)(b))                                                                         \
     : sizeof(*(a)) == 4 ? (uint64_t)atomic_exchange_explicit_u32((volatile uint32_t *)(a), (uint32_t)(b))                                                                         \
                         : (uint64_t)atomic_exchange_explicit_u64((volatile uint64_t *)(a), (uint64_t)(b)))

#define atomic_fetch_add_explicit(a, b, c)                                                                                                                                         \
    (sizeof(*(a)) == 1   ? (uint64_t)atomic_fetch_add_explicit_u8((volatile uint8_t *)(a), (uint8_t)(b))                                                                           \
     : sizeof(*(a)) == 2 ? (uint64_t)atomic_fetch_add_explicit_u16((volatile uint16_t *)(a), (uint16_t)(b))                                                                        \
     : sizeof(*(a)) == 4 ? (uint64_t)atomic_fetch_add_explicit_u32((volatile uint32_t *)(a), (uint32_t)(b))                                                                        \
                         : (uint64_t)atomic_fetch_add_explicit_u64((volatile uint64_t *)(a), (uint64_t)(b)))

#define atomic_fetch_sub_explicit(a, b, c)                                                                                                                                         \
    (sizeof(*(a)) == 1   ? (uint64_t)atomic_fetch_sub_explicit_u8((volatile uint8_t *)(a), (uint8_t)(b))                                                                           \
     : sizeof(*(a)) == 2 ? (uint64_t)atomic_fetch_sub_explicit_u16((volatile uint16_t *)(a), (uint16_t)(b))                                                                        \
     : sizeof(*(a)) == 4 ? (uint64_t)atomic_fetch_sub_explicit_u32((volatile uint32_t *)(a), (uint32_t)(b))                                                                        \
                         : (uint64_t)atomic_fetch_sub_explicit_u64((volatile uint64_t *)(a), (uint64_t)(b)))

#define atomic_compare_exchange_strong_explicit(a, b, c, d, e)                                                                                                                     \
    (sizeof(*(a)) == 1   ? atomic_compare_exchange_strong_explicit_u8((volatile uint8_t *)(a), (uint8_t *)(b), (uint8_t)(c))                                                       \
     : sizeof(*(a)) == 2 ? atomic_compare_exchange_strong_explicit_u16((volatile uint16_t *)(a), (uint16_t *)(b), (uint16_t)(c))                                                   \
     : sizeof(*(a)) == 4 ? atomic_compare_exchange_strong_explicit_u32((volatile uint32_t *)(a), (uint32_t *)(b), (uint32_t)(c))                                                   \
                         : atomic_compare_exchange_strong_explicit_u64((volatile uint64_t *)(a), (uint64_t *)(b), (uint64_t)(c)))

#define atomic_compare_exchange_weak_explicit(a, b, c, d, e) atomic_compare_exchange_strong_explicit(a, b, c, d, e)

#else

#if !defined(PLATFORM_64BIT)
#error "Atomic emulation implementation requires a 64-bit Windows"
#endif

#endif

#endif // OPTION_ATOMIC_EMULATION

#ifdef __cplusplus
} // extern "C"
#endif
