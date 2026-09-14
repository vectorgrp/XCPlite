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
OPTION_ENABLE_TCP and/or OPTION_ENABLE_UDP or OPTION_ENABLE_UDP_RAW
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

// When testing FreeRTOS code paths on macOS/Linux, we use OS-specific sockets and clock code in platform.c and sockets.c
#if defined(FREE_RTOS_POSIX_SIM)

#if defined(__APPLE__)
#define _MACOS
#elif defined(__linux__)
#define _LINUX
#else
#error "Unsupported host OS for FreeRTOS POSIX simulator"
#endif

// _GNU_SOURCE must be provided by the build system.
// The freertos_emu_demo CMakeLists.txt sets it via target_compile_definitions.
// Non-CMake builds: pass -D_GNU_SOURCE on the compiler command line.
#if defined(__linux__) && !defined(_GNU_SOURCE)
#error "_GNU_SOURCE is not defined. Pass -D_GNU_SOURCE on the compiler command line."
#endif

#include <net/if.h>
#include <pthread.h>
#include <unistd.h>

#endif // FREE_RTOS_POSIX_SIM

#else

// _GNU_SOURCE must be defined by the build system before any system headers are included.
// When using CMake: link against xcplite::xcplite — _GNU_SOURCE is a PUBLIC compile definition
// on Linux and propagates automatically to all consumers.
// Non-CMake builds: pass -D_GNU_SOURCE on the compiler command line.
#if defined(_LINUX) && !defined(_GNU_SOURCE)
#error "_GNU_SOURCE is not defined. Add -D_GNU_SOURCE to your compiler flags, or use the xcplite CMake target (xcplite::xcplite) which propagates this automatically on Linux."
#endif

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
#if configSUPPORT_STATIC_ALLOCATION == 1
    StaticSemaphore_t buffer; // Static storage for the FreeRTOS mutex
#endif
#if configUSE_RECURSIVE_MUTEXES == 1
    bool recursive;
#endif
} MUTEX;

#ifdef __cplusplus
#define MUTEX_INTIALIZER {}
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

// create_thread() result convention
//
//   POSIX     expression, 0 on success (pthread_create)
//   Windows   expression, 0 on success (adapted below, CreateThread itself returns a HANDLE)
//   FreeRTOS  STATEMENTS, both variants assert on failure - they have no value and cannot be tested
//
// Windows follows the POSIX convention so that a check reads the same way on both rather than
// meaning the opposite thing. A *portable* check is still not possible, because the FreeRTOS
// variants are statements: `if (create_thread(...))` does not compile there. That is deliberate -
// it fails at build time instead of silently. No caller in this repository tests the result.
//
// If you need to know that a thread is actually running, have the thread set a flag as its first
// action and wait for it. That is portable and proves more than a creation result: see
// cmpRestStart() in examples/cmp_demo/src/cmp_rest.c.

#if defined(_WIN) // Windows

typedef HANDLE THREAD_HANDLE;
#define create_thread(thread_handle_ptr, attr, thread, args) (((*(thread_handle_ptr) = CreateThread(0, 0, thread, args, 0, NULL)) == NULL) ? -1 : 0)
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

#if defined(ESP_PLATFORM)
#define FREERTOS_TASK_STACK_DEPTH(stack_bytes) (stack_bytes)
#else
#define FREERTOS_TASK_STACK_DEPTH(stack_bytes) ((stack_bytes) / sizeof(StackType_t))
#endif

typedef TaskHandle_t THREAD_HANDLE;
#if configSUPPORT_STATIC_ALLOCATION == 1
// Each call site owns a persistent task control block and stack.
#define create_thread(h, _attr, fn, args)                                                                                                                                          \
    do {                                                                                                                                                                           \
        static StaticTask_t task_buffer;                                                                                                                                           \
        static StackType_t stack_buffer[FREERTOS_TASK_STACK_DEPTH(OPTION_FREERTOS_STACK_BYTES)];                                                                                  \
        *(h) = xTaskCreateStatic((TaskFunction_t)(fn), #fn, FREERTOS_TASK_STACK_DEPTH(OPTION_FREERTOS_STACK_BYTES), (args), OPTION_FREERTOS_PRIORITY, stack_buffer, &task_buffer); \
        assert(*(h) != NULL);                                                                                                                                                      \
    } while (0)
#else
#define create_thread(h, _attr, fn, args)                                                                                                                                          \
    do {                                                                                                                                                                           \
        BaseType_t res = xTaskCreate((TaskFunction_t)(fn), #fn, FREERTOS_TASK_STACK_DEPTH(OPTION_FREERTOS_STACK_BYTES), (args), OPTION_FREERTOS_PRIORITY, (h));                    \
        assert(res == pdPASS);                                                                                                                                                     \
    } while (0)
#endif
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
