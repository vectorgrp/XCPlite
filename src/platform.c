/*----------------------------------------------------------------------------
| File:
|   platform.c
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
|   Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "platform.h" // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex, spinlock

#include "xcplib_cfg.h" // for OPTION_xxx ...

#include "assert.h"    // for assert
#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT, ...

#if defined(_LINUX) && defined(_MACOS)
#error "inconsistent platform defines: both _LINUX and _MACOS defined"
#endif

/**************************************************************************/
// Keyboard
/**************************************************************************/

#if !defined(_WIN) // Non-Windows platforms

#ifdef PLATFORM_ENABLE_KEYBOARD

int _getch(void) {
    struct termios oldt, newt;
    int ch;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ICANON;
    // newt.c_lflag &= ECHO; // echo
    newt.c_lflag &= ~ECHO; // no echo
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

int _kbhit(void) {
    struct termios oldt, newt;
    int ch;
    int oldf;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    if (ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
}

#endif
#endif

/**************************************************************************/
// Sleep
/**************************************************************************/

#if defined(_FREE_RTOS) // FreeRTOS sleep

// Minimum granularity is one tick (1 ms at configTICK_RATE_HZ = 1000).
// Delays are rounded up to the next tick.
void sleepUs(uint32_t us) {
    TickType_t ticks = (TickType_t)((((uint64_t)us * configTICK_RATE_HZ) + 999999U) / 1000000U);
    vTaskDelay(ticks == 0U ? 1U : ticks);
}

void sleepMs(uint32_t ms) {
    TickType_t ticks = (TickType_t)((((uint64_t)ms * configTICK_RATE_HZ) + 999U) / 1000U);
    vTaskDelay(ticks == 0U ? 1U : ticks);
}

#elif defined(_WIN) // Windows

void sleepUs(uint32_t us) {

    uint64_t t1, t2;

    // Sleep
    if (us > 1000) {
        Sleep(us / 1000);
    }

    // Busy wait <= 1ms, -> CPU load !!!
    else if (us > 0) {

        t1 = t2 = clockGet();
        uint64_t te = t1 + us * (uint64_t)(CLOCK_TICKS_PER_S / 1000000);
        for (;;) {
            t2 = clockGet();
            if (t2 >= te)
                break;
            Sleep(0);
        }
    } else {
        Sleep(0);
    }
}

void sleepMs(uint32_t ms) {
    if (ms > 0 && ms < 10) {
        // DBG_PRINT_WARNING("cannot precisely sleep less than 10ms!\n");
    }
    Sleep(ms);
}
#else               // Other

#include <time.h>   // for timespec, nanosleep, CLOCK_MONOTONIC_RAW
#include <unistd.h> // for sleep

void sleepUs(uint32_t us) {
    // DBG_PRINTF3(ANSI_COLOR_RED "Sleep for %u us\n" ANSI_COLOR_RESET, us);
    if (us == 0) {
        sleep(0);
    } else {
        struct timespec timeout, timerem;
        assert(us < 1000000UL);
        timeout.tv_sec = 0;
        timeout.tv_nsec = (long)us * 1000;
        nanosleep(&timeout, &timerem);
    }
}

void sleepMs(uint32_t ms) {
    // DBG_PRINTF3(ANSI_COLOR_RED "Sleep for %u ms\n" ANSI_COLOR_RESET, ms);
    if (ms == 0) {
        sleep(0);
    } else {
        struct timespec timeout, timerem;
        timeout.tv_sec = (long)ms / 1000;
        timeout.tv_nsec = (long)(ms % 1000) * 1000000;
        nanosleep(&timeout, &timerem);
    }
}

#endif // Other

/**************************************************************************/
// Memory mapping
/**************************************************************************/

#if defined(OPTION_SHM_MODE)

#if !defined(_WIN)
#include <sys/mman.h> // for mmap, munmap
#endif

void *platformMemAlloc(size_t size) {
#if defined(_WIN)
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
        return NULL;
    return mem;
#endif
}

void platformMemFree(void *ptr, size_t size) {
    if (ptr == NULL)
        return;
#if defined(_WIN)
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}

/**************************************************************************/
// POSIX shared memory
/**************************************************************************/

#if !defined(_WIN)

#include <errno.h>    // for errno, EEXIST, strerror
#include <fcntl.h>    // file I/O
#include <stdlib.h>   // for malloc, free
#include <sys/file.h> // for flock, LOCK_EX, LOCK_UN
#include <sys/stat.h> // for S_IRUSR, S_IWUSR

void *platformShmOpen(const char *name, const char *lock_path, size_t size, bool *is_leader) {

    *is_leader = false;

    // Acquire an exclusive flock on the lock file to serialise the leader-election window
    int lock_fd = open(lock_path, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
    if (lock_fd < 0) {
        DBG_PRINTF_ERROR("platformShmOpen: cannot open lock file '%s': %s\n", lock_path, strerror(errno));
        return NULL;
    }
    if (flock(lock_fd, LOCK_EX) != 0) {
        DBG_PRINTF_ERROR("platformShmOpen: flock failed: %s\n", strerror(errno));
        close(lock_fd);
        return NULL;
    }

    // Try to create exclusively — only the very first process succeeds and becomes leader
    int shm_fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (shm_fd >= 0) {
        // ---- LEADER ----
        *is_leader = true;
        if (ftruncate(shm_fd, (off_t)size) < 0) {
            DBG_PRINTF_ERROR("platformShmOpen: ftruncate failed: %s\n", strerror(errno));
            close(shm_fd);
            shm_unlink(name);
            flock(lock_fd, LOCK_UN);
            close(lock_fd);
            return NULL;
        }
    } else if (errno == EEXIST) {
        // ---- FOLLOWER: SHM already exists ----
        shm_fd = shm_open(name, O_RDWR, 0);
        if (shm_fd < 0) {
            DBG_PRINTF_ERROR("platformShmOpen: shm_open (follower) failed: %s\n", strerror(errno));
            flock(lock_fd, LOCK_UN);
            close(lock_fd);
            return NULL;
        }
        // Verify the existing SHM has the expected size.
        // If size == 0 the leader crashed between shm_open and ftruncate — safe to reclaim.
        // Any other size mismatch means a different binary version or a live leader; do NOT
        // auto-reclaim, as that would steal a running process's SHM or silently corrupt state.
        // Fail with a clear message so the user can run tools/shm_cleanup.sh.
        struct stat st;
        if (fstat(shm_fd, &st) < 0) {
            DBG_PRINTF_ERROR("platformShmOpen: fstat('%s') failed: %s\n", name, strerror(errno));
            close(shm_fd);
            flock(lock_fd, LOCK_UN);
            close(lock_fd);
            return NULL;
        }
        if (st.st_size == 0) {
            // Zero-size: leader crashed before ftruncate — safe to reclaim
            DBG_PRINTF5("platformShmOpen: zero-size SHM '%s' found — reclaiming as leader\n", name);
            close(shm_fd);
            shm_unlink(name);
            shm_fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
            if (shm_fd < 0) {
                DBG_PRINTF_ERROR("platformShmOpen: shm_open (reclaim) failed: %s\n", strerror(errno));
                flock(lock_fd, LOCK_UN);
                close(lock_fd);
                return NULL;
            }
            *is_leader = true;
            if (ftruncate(shm_fd, (off_t)size) < 0) {
                DBG_PRINTF_ERROR("platformShmOpen: ftruncate (reclaim) failed: %s\n", strerror(errno));
                close(shm_fd);
                shm_unlink(name);
                flock(lock_fd, LOCK_UN);
                close(lock_fd);
                return NULL;
            }
        } else if ((size_t)st.st_size < size) {
            // SHM is smaller than sizeof(tXcpData): definitely from a different, older binary.
            // Do not reclaim — require manual cleanup.
            // Note: st.st_size may be larger than size due to OS page-size rounding
            // (e.g. macOS ARM64 rounds ftruncate() up to 16 KiB pages); that is fine —
            // we map only 'size' bytes and validate the content by magic after mapping.
            DBG_PRINTF_ERROR("platformShmOpen: SHM '%s' is too small (found=%lld, expected=%zu).\n"
                             "  Stale object from an older build.\n"
                             "  Run:  ./tools/shm_cleanup.sh\n",
                             name, (long long)st.st_size, size);
            close(shm_fd);
            flock(lock_fd, LOCK_UN);
            close(lock_fd);
            return NULL;
        }
    } else {
        DBG_PRINTF_ERROR("platformShmOpen: shm_open failed: %s\n", strerror(errno));
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        return NULL;
    }

    // Map the region
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);

    if (ptr == MAP_FAILED) {
        DBG_PRINTF_ERROR("platformShmOpen: mmap failed: %s\n", strerror(errno));
        if (*is_leader)
            shm_unlink(name);
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        return NULL;
    }

    // Leader zero-initialises the region while still holding the lock, so followers
    // always see a clean state — never partially-written data from a previous run.
    if (*is_leader) {
        memset(ptr, 0, size);
    }

    flock(lock_fd, LOCK_UN);
    close(lock_fd);

    DBG_PRINTF5("platformShmOpen: %s '%s' (%zu bytes)\n", *is_leader ? "created" : "attached to", name, size);
    return ptr;
}

void *platformShmOpenAttach(const char *name, size_t *size_out) {
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) {
        DBG_PRINTF_ERROR("platformShmOpenAttach: shm_open('%s') failed: %s\n", name, strerror(errno));
        return NULL;
    }
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size == 0) {
        DBG_PRINTF_ERROR("platformShmOpenAttach: fstat('%s') failed or zero size\n", name);
        close(fd);
        return NULL;
    }
    *size_out = (size_t)st.st_size;
    void *ptr = mmap(NULL, *size_out, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) {
        DBG_PRINTF_ERROR("platformShmOpenAttach: mmap('%s', %zu) failed: %s\n", name, *size_out, strerror(errno));
        return NULL;
    }
    DBG_PRINTF5("platformShmOpenAttach: attached to '%s' (%zu bytes)\n", name, *size_out);
    return ptr;
}

void platformShmClose(const char *name, void *ptr, size_t size, bool unlink) {
    if (ptr != NULL) {
        munmap(ptr, size);
        DBG_PRINTF5("platformShmClose: unmapped '%s'\n", name);
    }
    if (unlink && name != NULL) {
        shm_unlink(name);
        DBG_PRINTF5("platformShmClose: unlinked '%s'\n", name);
    }
}

void platformShmUnlink(const char *name) {
    if (name != NULL) {
        shm_unlink(name);
        DBG_PRINTF5("platformShmUnlink: unlinked '%s'\n", name);
    }
}

#endif // !_WIN

#endif

/**************************************************************************/
// Mutex
/**************************************************************************/

#if defined(_FREE_RTOS) // FreeRTOS mutexes

void mutexInit(MUTEX *m, bool recursive, uint32_t spinCount) {
    (void)spinCount;
#if configUSE_RECURSIVE_MUTEXES == 1
    m->recursive = recursive;
#if configSUPPORT_STATIC_ALLOCATION == 1
    m->handle = recursive ? xSemaphoreCreateRecursiveMutexStatic(&m->buffer) : xSemaphoreCreateMutexStatic(&m->buffer);
#else
    m->handle = recursive ? xSemaphoreCreateRecursiveMutex() : xSemaphoreCreateMutex();
#endif
#else
    if (recursive) {
        m->handle = NULL;
        assert(!recursive); // Recursive FreeRTOS mutexes are disabled in this configuration
        return;
    }
#if configSUPPORT_STATIC_ALLOCATION == 1
    m->handle = xSemaphoreCreateMutexStatic(&m->buffer);
#else
    m->handle = xSemaphoreCreateMutex();
#endif
#endif
    assert(m->handle != NULL); // Check mutex creation
}

void mutexDestroy(MUTEX *m) {
    if (m != NULL && m->handle != NULL) {
        vSemaphoreDelete(m->handle);
        m->handle = NULL;
#if configUSE_RECURSIVE_MUTEXES == 1
        m->recursive = false;
#endif
    }
}

#elif defined(_WIN) // Windows

void mutexInit(MUTEX *m, bool recursive, uint32_t spinCount) {
    (void)recursive;
    // Window critical sections are always recursive
    (void)InitializeCriticalSectionAndSpinCount(m, spinCount);
}

void mutexDestroy(MUTEX *m) { DeleteCriticalSection(m); }

#else // Other

void mutexInit(MUTEX *m, bool recursive, uint32_t spinCount) {
    (void)spinCount;
    if (recursive) {
        pthread_mutexattr_t ma;
        pthread_mutexattr_init(&ma);
        pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(m, &ma);
    } else {
        pthread_mutex_init(m, NULL);
    }
}

void mutexDestroy(MUTEX *m) { pthread_mutex_destroy(m); }

#endif

/**************************************************************************/
// Clock
/**************************************************************************/

/*
    Clock options
    OPTION_CLOCK_EPOCH_ARB      arbitrary epoch, clock is monotonic, no corrections by NTP, PTP, ...
    OPTION_CLOCK_EPOCH_PTP      real time clock in ns or us since 1.1.1970
    CLOCK_TICKS_PER_S           number of clock ticks per second (e.g. 1000000 for 1us resolution, 1000000000 for 1ns resolution)
*/

#ifdef TEST_CLOCK_GET_STATISTIC
static atomic_uint_fast32_t gClockGetCtr = 0;
static atomic_uint_fast32_t gClockGetLastCtr = 0;
void clockGetPrintStatistic(void) {
    uint32_t getCtr = atomic_load_explicit(&gClockGetCtr, memory_order_relaxed);
    uint32_t getLastCtr = atomic_load_explicit(&gClockGetLastCtr, memory_order_relaxed);
    DBG_PRINTF3("clockGet calls: %" PRIu32 ", clockGetLast calls: %" PRIu32 "\n", getCtr, getLastCtr);
}
#endif

// ---------------------------------------------------------------------------
// FreeRTOS clock

// Uses xTaskGetTickCount() with granularity = 1/configTICK_RATE_HZ as XCP clock
// It is recommended to register a higher precision clock for CLOCK_TICKS_PER_S with ApplXcpRegisterGetClockCallback() !

#if defined(_FREE_RTOS) // FreeRTOS clock

static volatile uint64_t gClockLast_ = 0;

// Convert a FreeRTOS tick count to the configured clock resolution CLOCK_TICKS_PER_S
static inline uint64_t tickToClockUnit_(TickType_t ticks) { return (uint64_t)ticks * (CLOCK_TICKS_PER_S / configTICK_RATE_HZ); }

bool clockInit(void) {
    DBG_PRINT3("Init FreeRtos clock\n");
    clockGet(); // Initialize gClockLast_
    return true;
}

uint64_t clockGet(void) {

#ifdef TEST_CLOCK_GET_STATISTIC
    atomic_fetch_add_explicit(&gClockGetCtr, 1, memory_order_relaxed);
#endif

    uint64_t t = tickToClockUnit_(xTaskGetTickCount());
    gClockLast_ = t;
    return t;
}

uint64_t clockGetLast(void) {

#ifdef TEST_CLOCK_GET_STATISTIC
    atomic_fetch_add_explicit(&gClockGetLastCtr, 1, memory_order_relaxed);
#endif

    return gClockLast_;
}

char *clockGetString(char *s, uint32_t l, uint64_t c) {
    SNPRINTF(s, l, "%" PRIu64, c);
    return s;
}

uint64_t clockGetMonotonicNs(void) { return clockGet() * (1000000000ULL / CLOCK_TICKS_PER_S); }
uint64_t clockGetMonotonicNsLast(void) { return clockGetLast() * (1000000000ULL / CLOCK_TICKS_PER_S); }

// ---------------------------------------------------------------------------
// POSIX clock

#elif !defined(_WIN) // Non-Windows platform clock

#if !defined(OPTION_CLOCK_EPOCH_PTP) && !defined(OPTION_CLOCK_EPOCH_ARB)
#error "Please define OPTION_CLOCK_EPOCH_ARB or OPTION_CLOCK_EPOCH_PTP"
#endif

/*
Clock types
    CLOCK_REALTIME
        This clock may be affected by incremental adjustments performed by NTP.
        Epoch ns since 1.1.1970.
        Works on all platforms.
        1us granularity on MacOS.

    CLOCK_TAI
        This clock does not experience discontinuities and backwards jumps caused by NTP or inserting leap seconds as CLOCK_REALTIME does.
        Epoch ns since 1.1.1970 Not available on Linux and MacOS.

    CLOCK_MONOTONIC_RAW
        Provides a monotonic clock without time drift adjustments by NTP, giving higher stability and resolution Epoch ns since OS or process start.
        Works on all platforms except QNX, <1us granularity on MACOS.

    CLOCK_MONOTONIC
        Provides a monotonic clock that might be adjusted in frequency by NTP to compensate drifts (on Linux and MACOS).
        On QNX, this clock cannot be adjusted and is ensured to increase at a constant rate.
        Epoch ns since OS or process start.
        Available on all platforms.
        <1us granularity on MACOS.
*/

#ifdef OPTION_CLOCK_EPOCH_ARB
#ifdef _QNX
#define CLOCK_TYPE CLOCK_MONOTONIC // Same behaviour as CLOCK_MONOTONIC_RAW on the other os
#else
#define CLOCK_TYPE CLOCK_MONOTONIC_RAW
#endif
#else
#define CLOCK_TYPE CLOCK_REALTIME
#endif
#ifdef _QNX
#define CLOCK_MONOTONIC_TYPE CLOCK_MONOTONIC
#else
#define CLOCK_MONOTONIC_TYPE CLOCK_MONOTONIC_RAW
#endif
#define CLOCK_REALTIME_TYPE CLOCK_REALTIME

// Global clock variable, updated by all clockGet calls, used by clockGetLast() to save syscall overhead when the last known clock value is sufficient
static struct timespec __gClock;
static struct timespec __gClockMonotonic;
static struct timespec __gClockRealtime;

char *clockGetString(char *s, uint32_t l, uint64_t c) {

    if (c < 1000000000000000000ULL) { // Don't print time and date, if too old
        SNPRINTF(s, l, "%gs", (double)c / CLOCK_TICKS_PER_S);
    } else {
        time_t t = (time_t)(c / CLOCK_TICKS_PER_S); // s since 1.1.1970
        struct tm tm;
        gmtime_r(&t, &tm);
        uint64_t fns = c % CLOCK_TICKS_PER_S;
#if CLOCK_TICKS_PER_S == 1000000UL // us
        fns *= 1000;
#endif
        SNPRINTF(s, l, "%u.%u.%u %02u:%02u:%02u +%" PRIu64 "ns", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour % 24, tm.tm_min, tm.tm_sec, fns);
    }
    return s;
}

bool clockInit(void) {
    DBG_PRINT3("Init POSIX clock\n");
#ifdef OPTION_CLOCK_EPOCH_PTP
    DBG_PRINT3("  OPTION_CLOCK_EPOCH_PTP\n");
#endif
#ifdef OPTION_CLOCK_EPOCH_ARB
    DBG_PRINT3("  OPTION_CLOCK_EPOCH_ARB\n");
#endif
    DBG_PRINTF3("  CLOCK_TICKS_PER_S = %" PRIu32 "\n", (uint32_t)(CLOCK_TICKS_PER_S));

    clockGetRealtimeNs();        // Initialize __gClockRealtime
    clockGetMonotonicNs();       // Initialize __gClockMonotonic
    uint64_t clock = clockGet(); // Initialize gClock and ClockGetLast()
    (void)clock;
#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 3) { // Test
        struct timespec gtr;
        clock_getres(CLOCK_TYPE, &gtr);
        DBG_PRINTF3("  resolution = %ldns!\n", gtr.tv_nsec);
        char ts[64]; // @@@@ STACK buffer for clock value
        clockGetString(ts, sizeof(ts), clock);
        DBG_PRINTF3("  initial clock = %" PRIu64 " %s\n", clock, ts);
    }
#endif
    return true;
}

// Get 64 bit clock
uint64_t clockGet(void) {

#ifdef TEST_CLOCK_GET_STATISTIC
    atomic_fetch_add_explicit(&gClockGetCtr, 1, memory_order_relaxed);
#endif
    clock_gettime(CLOCK_TYPE, &__gClock);
#if CLOCK_TICKS_PER_S == 1000000000UL // ns
    return (((uint64_t)(__gClock.tv_sec) * 1000000000ULL) + (uint64_t)(__gClock.tv_nsec));
#elif CLOCK_TICKS_PER_S == 1000000UL  // us
    return (((uint64_t)(__gClock.tv_sec) * 1000000ULL) + (uint64_t)(__gClock.tv_nsec / 1000)); // us
#else
#error "Please define clock resolution CLOCK_TICKS_PER_S to 1ns or 1us"
#endif
}

// Get the last known clock value
uint64_t clockGetLast(void) {
#if CLOCK_TICKS_PER_S == 1000000000UL // ns
    return (((uint64_t)(__gClock.tv_sec) * 1000000000ULL) + (uint64_t)(__gClock.tv_nsec));
#elif CLOCK_TICKS_PER_S == 1000000UL  // us
    return (((uint64_t)(__gClock.tv_sec) * 1000000ULL) + (uint64_t)(__gClock.tv_nsec / 1000)); // us
#else
#error "Please define clock resolution CLOCK_TICKS_PER_S to 1ns or 1us"
#endif
}

uint64_t clockGetMonotonicNs(void) {
    clock_gettime(CLOCK_MONOTONIC_TYPE, &__gClockMonotonic);
    return (((uint64_t)(__gClockMonotonic.tv_sec) * 1000000000ULL) + (uint64_t)(__gClockMonotonic.tv_nsec));
}

uint64_t clockGetMonotonicUs(void) {
    clock_gettime(CLOCK_MONOTONIC_TYPE, &__gClockMonotonic);
    return (((uint64_t)(__gClockMonotonic.tv_sec) * 1000000ULL) + (uint64_t)(__gClockMonotonic.tv_nsec / 1000));
}

uint64_t clockGetRealtimeNs(void) {
    clock_gettime(CLOCK_REALTIME_TYPE, &__gClockRealtime);
    return (((uint64_t)(__gClockRealtime.tv_sec) * 1000000000ULL) + (uint64_t)(__gClockRealtime.tv_nsec));
}

uint64_t clockGetRealtimeUs(void) {
    clock_gettime(CLOCK_REALTIME_TYPE, &__gClockRealtime);
    return (((uint64_t)(__gClockRealtime.tv_sec) * 1000000ULL) + (uint64_t)(__gClockRealtime.tv_nsec / 1000));
}

// Get the last known clock value
// Save CPU load, clockGet may take reasonable run time, depending on platform
// For slow timeouts and timers, it is sufficient to rely on the relatively high call frequency of clockGet() by other
// parts of the application
uint64_t clockGetMonotonicNsLast(void) { return (((uint64_t)(__gClockMonotonic.tv_sec) * 1000000000ULL) + (uint64_t)(__gClockMonotonic.tv_nsec)); }
uint64_t clockGetMonotonicUsLast(void) { return (((uint64_t)(__gClockMonotonic.tv_sec) * 1000000ULL) + (uint64_t)(__gClockMonotonic.tv_nsec / 1000)); }
uint64_t clockGetRealtimeNsLast(void) { return (((uint64_t)(__gClockRealtime.tv_sec) * 1000000000ULL) + (uint64_t)(__gClockRealtime.tv_nsec)); }
uint64_t clockGetRealtimeUsLast(void) { return (((uint64_t)(__gClockRealtime.tv_sec) * 1000000ULL) + (uint64_t)(__gClockRealtime.tv_nsec / 1000)); }

// ---------------------------------------------------------------------------
// Windows clock

#else // Windows platform clock

static uint64_t __gClock = 0;

// Get the last known clock value
uint64_t clockGetLast(void) { return __gClock; }

// Performance counter to clock conversion
static uint64_t sFactor = 0; // ticks per us
static uint8_t sDivide = 0;  // divide or multiply
static uint64_t sOffset = 0; // offset

char *clockGetString(char *str, uint32_t l, uint64_t c) {

    if (c < 1000000000000000000ULL) { // Don't print time and date, if too old
        SNPRINTF(str, l, "%gs", (double)c / CLOCK_TICKS_PER_S);
    } else {
        uint64_t s = c / CLOCK_TICKS_PER_S;
        uint64_t ns = c % CLOCK_TICKS_PER_S;
        if (s < 3600 * 24 * 365 * 30) { // ARB epoch
            SNPRINTF(str, l, "%" PRIu64 "d%" PRIu64 "h%" PRIu64 "m%" PRIu64 "s+%" PRIu64 "ns", s / (3600 * 24), (s % (3600 * 24)) / 3600, ((s % (3600 * 24)) % 3600) / 60,
                     ((s % (3600 * 24)) % 3600) % 60, ns);
        } else { // UNIX epoch
            struct tm tm;
            time_t t = s;
            gmtime_s(&tm, &t);
            SNPRINTF(str, l, "%u.%u.%u %02u:%02u:%02u +%" PRIu64 "ns", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour % 24, tm.tm_min, tm.tm_sec, ns);
        }
    }
    return str;
}

#include <sys/timeb.h>

bool clockInit(void) {

    DBG_PRINT4("Init WINDOWS clock\n  ");
#ifdef OPTION_CLOCK_EPOCH_PTP
    DBG_PRINT4("OPTION_CLOCK_EPOCH_PTP,");
#endif
#ifdef OPTION_CLOCK_EPOCH_ARB
    DBG_PRINT4("OPTION_CLOCK_EPOCH_ARB,");
#endif
    DBG_PRINTF4("  CLOCK_TICKS_PER_S = %" PRIu32 "\n\n", (uint32_t)(CLOCK_TICKS_PER_S));

    __gClock = 0;

    // Get current performance counter frequency
    // Determine conversion to CLOCK_TICKS_PER_S -> sDivide/sFactor
    LARGE_INTEGER tF, tC;
    uint64_t tp;
    if (!QueryPerformanceFrequency(&tF)) {
        DBG_PRINT_ERROR("Performance counter not available on this system!\n");
        return false;
    }
    if (tF.u.HighPart) {
        DBG_PRINT_ERROR("Unexpected performance counter frequency!\n");
        return false;
    }

    if (CLOCK_TICKS_PER_S > tF.u.LowPart) {
        sFactor = (uint64_t)CLOCK_TICKS_PER_S / tF.u.LowPart;
        sDivide = 0;
    } else {
        sFactor = tF.u.LowPart / CLOCK_TICKS_PER_S;
        sDivide = 1;
    }

    // Get current performance counter to absolute time relation
#ifndef OPTION_CLOCK_EPOCH_ARB

    // Set time zone from TZ environment variable. If TZ is not set, the operating system is queried
    _tzset();

    // Get current UTC time in ms since 1.1.1970
    struct _timeb tstruct;
    uint64_t time_s;
    uint32_t time_ms;
    _ftime(&tstruct);
    time_ms = tstruct.millitm;
    time_s = tstruct.time;
    //_time64(&t); // s since 1.1.1970
#endif

    // Calculate factor and offset
    QueryPerformanceCounter(&tC);
    tp = (((int64_t)tC.u.HighPart) << 32) | (int64_t)tC.u.LowPart;
#ifndef OPTION_CLOCK_EPOCH_ARB
    // set offset from local clock UTC value t
    // this is inaccurate up to 1 s, but irrelevant because system clock UTC offset is also not accurate
    sOffset = time_s * CLOCK_TICKS_PER_S + (uint64_t)time_ms * (CLOCK_TICKS_PER_S / 1000) - tp * sFactor;
#else
    // Reset clock now
    sOffset = tp;
#endif

    clockGet();

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 5) {
#ifndef OPTION_CLOCK_EPOCH_ARB
        if (DBG_LEVEL >= 6) {
            struct tm tm;
            _gmtime64_s(&tm, (const __time64_t *)&time_s);
            printf("    Current time = %I64uus + %ums\n", time_s, time_ms);
            printf("    Zone difference in minutes from UTC: %d\n", tstruct.timezone);
            printf("    Time zone: %s\n", _tzname[0]);
            printf("    Daylight saving: %s\n", tstruct.dstflag ? "YES" : "NO");
            printf("    UTC time = %" PRIu64 "s since 1.1.1970 ", time_s);
            printf("    %u.%u.%u %u:%u:%u\n", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour % 24, tm.tm_min, tm.tm_sec);
        }
        printf("  System clock resolution = %" PRIu32 "Hz, UTC ns conversion = %c%" PRIu64 "+%" PRIu64 "\n", (uint32_t)tF.u.LowPart, sDivide ? '/' : '*', sFactor, sOffset);
#else
        printf("  System clock resolution = %" PRIu32 "Hz, ARB us conversion = -%" PRIu64 "/%" PRIu64 "\n", (uint32_t)tF.u.LowPart, sOffset, sFactor);
#endif
        uint64_t t;
        char ts[64]; // @@@@ STACK buffer for clock value
        t = clockGet();
        clockGetString(ts, sizeof(ts), t);
        printf("  Now = %" PRIu64 " (%" PRIu64 " per us) %s\n", t, (uint64_t)(CLOCK_TICKS_PER_S / 1000000), ts);
    }
#endif

    return true;
}

// Get 64 bit clock
uint64_t clockGet(void) {

    LARGE_INTEGER tp;
    uint64_t t;

    QueryPerformanceCounter(&tp);
    t = (((uint64_t)tp.u.HighPart) << 32) | (uint64_t)tp.u.LowPart;
    if (sDivide) {
        t = t / sFactor + sOffset;
    } else {
        t = t * sFactor + sOffset;
    }
    __gClock = t;
#ifdef TEST_CLOCK_GET_STATISTIC
    atomic_fetch_add_explicit(&gClockGetCtr, 1, memory_order_relaxed);
#endif
    return t;
}

#if (CLOCK_TICKS_PER_S == 1000000000UL)
uint64_t clockGetMonotonicNs() { return clockGet(); }
uint64_t clockGetMonotonicNsLast() { return clockGetLast(); }
uint64_t clockGetMonotonicUs() { return clockGet() / 1000; }
uint64_t clockGetMonotonicUsLast() { return clockGetLast() / 1000; }
#endif
#if (CLOCK_TICKS_PER_S == 1000000UL)
uint64_t clockGetMonotonicNs() { return clockGet() * 1000; }
uint64_t clockGetMonotonicNsLast() { return clockGetLast() * 1000; }
uint64_t clockGetMonotonicUs() { return clockGet(); }
uint64_t clockGetMonotonicUsLast() { return clockGetLast(); }
#endif

#endif // Windows

/**************************************************************************/
// File system utilities
/**************************************************************************/

#if !defined(_FREE_RTOS)

#if defined(_WIN)
#include <io.h> // for _access
#elif !defined(_FREE_RTOS)
#include <unistd.h> // for access (POSIX; not available on bare-metal FreeRTOS)
#endif

// Check if a file exists
// Returns true if the file exists and is accessible, false otherwise
bool fexists(const char *filename) {
    if (filename == NULL) {
        return false;
    }
#if defined(_WIN)
    // Windows: use _access from io.h
    return (_access(filename, 0) == 0);
#else
    // Linux/macOS/QNX: use access from unistd.h
    return (access(filename, F_OK) == 0);
#endif
}

#endif // !defined(_FREE_RTOS)
