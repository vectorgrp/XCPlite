/*----------------------------------------------------------------------------
| File:
|   xcpethserver.c
|
| Description:
|   XCP on UDP/TCP Server
|   Creates threads for cmd handling and data transmission
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
-----------------------------------------------------------------------------*/

#include "xcpethserver.h"

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t
#include <stdio.h>    // for printf

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "platform.h"  // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex
#include "queue.h"
#include "xcp.h"        // for CRC_XXX
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcplite.h"    // for tXcpDaqLists, XcpXxx, ApplXcpXxx, ...

#ifdef OPTION_SHM_MODE
#ifdef OPTION_ENABLE_A2L_GENERATOR
#include "a2l.h" // for A2lFinalize() called from the follower background thread
#endif
#include <unistd.h> // for getpid()
#endif              // OPTION_SHM_MODE

#if !defined(_WIN) && !defined(_LINUX) && !defined(_MACOS) && !defined(_QNX)
#error "Please define platform _WIN, _MACOS or _LINUX or _QNX"
#endif

#include "xcpethtl.h" // for XcpEthTlxxx
#include "xcptl.h"    // for XcpTlHandleTransmitQueue

#if defined(_WIN) // Windows
static DWORD WINAPI XcpServerReceiveThread(LPVOID lpParameter);
#else
static void *XcpServerReceiveThread(void *par);
#endif

#if defined(_WIN) // Windows
static DWORD WINAPI XcpServerTransmitThread(LPVOID lpParameter);
#else
static void *XcpServerTransmitThread(void *par);
#endif

#if !defined(OPTION_ENABLE_TCP) && !defined(OPTION_ENABLE_UDP)
#error "Please define OPTION_ENABLE_TCP or OPTION_ENABLE_UDP"
#endif

//-------------------------------------------------------------------------------------------------------

#ifdef OPTION_SHM_MODE

// In SHM mode, in an application which is not the XCP server
// There is a single follower background thread for polling A2L finalize requests and alive_counter
#if defined(_WIN) // Windows
static DWORD WINAPI XcpShmThread(LPVOID lpParameter);
#else
static void *XcpShmThread(void *par);
#endif

// In SHM mode, the queue is in shared memory and has an additional shared memory header
// SHM queue region header for /xcpqueue
// Placed at the start of the shared queue memory, the actual queue data starts immediately after.
typedef struct {
    atomic_uint_least32_t is_initialized; // set to 1 by the leader after queueInitFromMemory() completes
    atomic_uint_least32_t leader_pid;     // PID of the leader process; 0 until ready
    uint32_t queue_size;                  // usable data area size (bytes) passed to queueInitFromMemory
    uint32_t pad[13];                     // pad struct to exactly 64 bytes (one cache line)
} tShmQueueHeader;
static_assert(sizeof(tShmQueueHeader) == 64, "tShmQueueHeader must be 64 bytes");

#endif // OPTION_SHM_MODE

//-------------------------------------------------------------------------------------------------------

static struct {

    bool is_init;

    // Threads
    THREAD transmit_thread_handle;
    volatile bool transmit_thread_running;
    THREAD receive_thread_handle;
    volatile bool receive_thread_running;

    // Queue
    tQueueHandle transmit_queue;

#ifdef OPTION_SHM_MODE
    // In SHM mode, gXcpServer has additional state
    void *shm_queue_ptr;         // mmap base of the /xcpqueue region, NULL when not SHM mode
    size_t shm_queue_total_size; // total mmap size: sizeof(tShmQueueHeader) + queue_size
    THREAD shm_thread_handle;    // Shm follower background thread (polling A2L finalize request and alive_counter)
    volatile bool shm_thread_running;
#endif // OPTION_SHM_MODE

} gXcpServer;

//-------------------------------------------------------------------------------------------------------

#ifdef OPTION_SHM_MODE
bool XcpShmServerStatus(void) { return gXcpServer.is_init && gXcpServer.shm_thread_running; }
#endif // OPTION_SHM_MODE

// XCP server status
bool XcpEthServerStatus(void) {
    if (!XcpIsActivated())
        return true;

#ifdef OPTION_SHM_MODE
    if (XcpShmIsActive() && !XcpShmIsServer()) {
        return XcpShmServerStatus();
    }
#endif // OPTION_SHM_MODE

    return gXcpServer.is_init && gXcpServer.transmit_thread_running && gXcpServer.receive_thread_running;
}

#ifdef OPTION_SHM_MODE

// Initialize shared memory queue and start the background thread for non-server processes in SHM mode
static bool ShmInit_(uint32_t queue_size) {

    gXcpServer.shm_thread_running = false;
    gXcpServer.shm_queue_ptr = NULL;

    // SHM queue
    // Followers must attach at the leader's queue size (from fstat), never reclaim the region.
    size_t shm_total;
    void *shm_ptr;
    if (XcpShmIsLeader()) {
        shm_total = sizeof(tShmQueueHeader) + queue_size;
        bool elected = false;
        shm_ptr = platformShmOpen("/xcpqueue", "/tmp/xcpqueue.lock", shm_total, &elected);
        if (shm_ptr == NULL) {
            DBG_PRINT_ERROR("XcpEthServerInit: failed to create /xcpqueue SHM\n");
            return false;
        }
        // A second leader process (e.g. after rebuild) won /xcpdata but /xcpqueue may
        // already exist. platformShmOpen does NOT reclaim it when sizes differ — we own it.
        if (elected) {
            DBG_PRINTF3("XcpEthServerInit: SHM leader created /xcpqueue with size %zu bytes\n", shm_total);
        } else {
            DBG_PRINT_WARNING("XcpEthServerInit: Is SHM leader, but /xcpqueue already exists, not reinitializing\n");
        }
    } else {
        // Follower: attach to the existing queue at whatever size the leader allocated
        shm_ptr = platformShmOpenAttach("/xcpqueue", &shm_total);
        if (shm_ptr == NULL) {
            DBG_PRINT_ERROR("XcpEthServerInit: failed to attach /xcpqueue SHM\n");
            return false;
        }
        DBG_PRINTF3("XcpEthServerInit: SHM follower attached to /xcpqueue with size %zu bytes\n", shm_total);
    }

    gXcpServer.shm_queue_ptr = shm_ptr;
    gXcpServer.shm_queue_total_size = shm_total;
    tShmQueueHeader *hdr = (tShmQueueHeader *)shm_ptr;
    void *queue_mem = (uint8_t *)shm_ptr + sizeof(tShmQueueHeader);

    // Leader: initialise transmit queue in SHM and signal readiness
    if (XcpShmIsLeader()) {
        hdr->queue_size = queue_size;
        gXcpServer.transmit_queue = queueInitFromMemory(queue_mem, queue_size, true, NULL);
        if (gXcpServer.transmit_queue == NULL) {
            DBG_PRINT_ERROR("XcpEthServerInit: queueInitFromMemory failed\n");
            platformShmClose("/xcpqueue", shm_ptr, shm_total, true);
            return false;
        }
        atomic_store(&hdr->leader_pid, (int32_t)getpid());
        atomic_store(&hdr->is_initialized, 1U);
        DBG_PRINTF3("XcpEthServerInit: SHM leader, queue created, queue_size=%u\n", queue_size);

    }

    // Follower: Takeover existing queue
    else {
        // Follower: wait for the leader to complete queue initialisation
        DBG_PRINT3("XcpEthServerInit: SHM queue follower, waiting for leader...\n");
        for (int i = 0; i < 5000 && !atomic_load(&hdr->is_initialized); i++) {
            sleepUs(1000);
        }
        if (!atomic_load(&hdr->is_initialized)) {
            DBG_PRINT_ERROR("XcpEthServerInit: timeout waiting for SHM queue leader\n");
            platformShmClose("/xcpqueue", shm_ptr, shm_total, false);
            return false;
        }
        gXcpServer.transmit_queue = queueInitFromMemory(queue_mem, hdr->queue_size, false, NULL);
        if (gXcpServer.transmit_queue == NULL) {
            DBG_PRINT_ERROR("XcpEthServerInit: follower queueInitFromMemory failed\n");
            platformShmClose("/xcpqueue", shm_ptr, shm_total, false);
            return false;
        }
        DBG_PRINT3("XcpEthServerInit: SHM follower, queue attached\n");
    }

    if (!XcpShmIsServer()) {

        DBG_PRINT3("Start without XCP on ethernet server in SHM mode\n");

        // Start the background polling thread for SHM mode non server
        create_thread(&gXcpServer.shm_thread_handle, XcpShmThread);
        // Wait until the follower thread has started
        while (!gXcpServer.shm_thread_running) {
            sleepUs(100);
        }
    }

    return true;
}

// XCP on SHM server init
bool XcpShmServerInit(uint32_t queue_size) {

    // Check and ignore, if the XCP singleton has not been initialized and activated
    if (!XcpIsActivated()) {
        DBG_PRINT5("XcpEthServerInit: XCP is deactivated!\n");
        return true;
    }

    // Check if already initialized and running
    if (gXcpServer.is_init) {
        DBG_PRINT_WARNING("XCP server already running!\n");
        return false;
    }

    gXcpServer.transmit_thread_running = false;
    gXcpServer.receive_thread_running = false;
    gXcpServer.transmit_queue = NULL;

    if (!ShmInit_(queue_size)) {
        return false;
    }

    DBG_PRINT3("XCP SHM server initialized\n");
    gXcpServer.is_init = true;
    return true;
}

#endif // OPTION_SHM_MODE

// XCP on ethernet server init
bool XcpEthServerInit(const uint8_t *addr, uint16_t port, bool useTCP, uint32_t queue_size) {

    // Check and ignore, if the XCP singleton has not been initialized and activated
    if (!XcpIsActivated()) {
        DBG_PRINT5("XcpEthServerInit: XCP is deactivated!\n");
        return true;
    }

    // Check if already initialized and running
    if (gXcpServer.is_init) {
        DBG_PRINT_WARNING("XCP server already running!\n");
        return false;
    }

    gXcpServer.transmit_thread_running = false;
    gXcpServer.receive_thread_running = false;
    gXcpServer.transmit_queue = NULL;

#ifdef OPTION_SHM_MODE
    // In SHM mode, init SHM and create the transmit queue in shared memory
    if (XcpShmIsActive()) {
        if (!ShmInit_(queue_size)) {
            return false;
        }
    } else
#endif // OPTION_SHM_MODE else

    // Create the transmit queue on heap
    {
        assert(queue_size > 0);
        gXcpServer.transmit_queue = queueInit(queue_size);
        if (gXcpServer.transmit_queue == NULL)
            return false;
    }

#ifdef OPTION_SHM_MODE
    // In SHM mode, start the XCP on ethernet server only in the SHM server
    if (XcpShmIsServer())
#endif // OPTION_SHM_MODE else

    // Start the XCP on ethernet server with the created transmit queue
    {
        DBG_PRINT3("Start XCP on Ethernet server\n");

        // Init network sockets
        if (!socketStartup())
            return false;

        // Initialize XCP transport layer
        if (!XcpEthTlInit(addr, port, useTCP, gXcpServer.transmit_queue))
            return false;

        // Create the receive thread which starts the XCP protocol layer and handles incoming XCP unicast commands
        create_thread(&gXcpServer.receive_thread_handle, XcpServerReceiveThread);

        // Wait until receive thread is running to avoid races
        while (!gXcpServer.receive_thread_running) {
            sleepUs(100);
        }

        // Create the transmit thread
        // @@@@ TODO: Check, why start the transmit thread after the receive thread, should it better be before, once we implement first cycle data acquisition ?
        create_thread(&gXcpServer.transmit_thread_handle, XcpServerTransmitThread);
    }

    gXcpServer.is_init = true;
    return true;
}

// XCP on ethernet server shutdown
bool XcpEthServerShutdown(void) {

    if (!XcpIsActivated()) {
        DBG_PRINT5("XcpEthServerInit: XCP is deactivated!\n");
        return false;
    }

    // Check if already initialized and running
    if (!gXcpServer.is_init) {
        DBG_PRINT_WARNING("XCP server not running!\n");
        return false;
    }

#ifdef OPTION_SHM_MODE
    // In SHM mode, shutdown
    // Not SHM server: no socket or server threads — stop the background thread, then clean up, unmap but do not unlink the SHM region for the queue
    if (XcpShmIsActive() && !XcpShmIsServer()) {
        gXcpServer.shm_thread_running = false;
        join_thread(gXcpServer.shm_thread_handle);
        platformShmClose("/xcpqueue", gXcpServer.shm_queue_ptr, gXcpServer.shm_queue_total_size, false);
        gXcpServer.shm_queue_ptr = NULL;
        gXcpServer.is_init = false;
        return true;
    }
#endif // OPTION_SHM_MODE

    DBG_PRINT3("Disconnect, cancel threads and shutdown XCP!\n");
    XcpDisconnect();
#ifdef OPTION_SERVER_FORCEFULL_TERMINATION
    // Forcefull termination
    // Threads are cancelled immediately without waiting for clean termination
    cancel_thread(gXcpServer.receive_thread_handle);
    cancel_thread(gXcpServer.transmit_thread_handle);
    sleepMs(10); // Give threads some time to terminate after cancellation before cleaning up sockets and other resources
    XcpEthTlShutdown();
#else
    // Gracefull termination
    // @@@@ TODO: Does not terminate socketAccept
    gXcpServer.receive_thread_running = false;
    gXcpServer.transmit_thread_running = false;
    join_thread(gXcpServer.receive_thread_handle);
    join_thread(gXcpServer.transmit_thread_handle);
    XcpEthTlShutdown();
#endif
    socketCleanup();

    gXcpServer.is_init = false;

    // Free the transmit queue, if it was created on heap
    queueDeinit(gXcpServer.transmit_queue);

#ifdef OPTION_SHM_MODE
    // In SHM mode, if the server goes down, unmap and unlink the SHM region for the queue
    // No other application can use the SHM queue anymore, because the server is down
    if (XcpShmIsActive() && gXcpServer.shm_queue_ptr != NULL) {
        platformShmClose("/xcpqueue", gXcpServer.shm_queue_ptr, gXcpServer.shm_queue_total_size, false);
        gXcpServer.shm_queue_ptr = NULL;
    }
#endif

    return true;
}

// XCP on SHM server shutdown
bool XcpShmServerShutdown(void) { return XcpEthServerShutdown(); }

//-------------------------------------------------------------------------------------------------------

// XCP server unicast command receive thread
#if defined(_WIN) // Windows
DWORD WINAPI XcpServerReceiveThread(LPVOID par)
#else
extern void *XcpServerReceiveThread(void *par)
#endif
{
    (void)par;
    DBG_PRINT3("Start XCP receive thread\n");

    // Start the XCP protocol layer and event handling
    XcpStart(gXcpServer.transmit_queue, false);

    // Receive XCP unicast commands loop
    gXcpServer.receive_thread_running = true;
    while (gXcpServer.receive_thread_running) {

        // Blocking, with timeout to allow handling background tasks in this thread as well
        if (!XcpEthTlHandleCommands()) {
            DBG_PRINT_ERROR("XcpEthTlHandleCommands failed!\n");
            break; // error -> terminate thread
        }

        // Handle background tasks, e.g. pending calibration updates
        XcpBackgroundTasks();

#ifdef TEST_ENABLE_DBG_CHECKS
        static uint64_t ctr = 0;
        ctr++;
        static uint64_t last_time = 0;
        static uint64_t last_ctr = 0;
        uint64_t now = clockGetMonotonicUs();
        if (now - last_time >= 1000000) { // every 1s
            uint32_t loops = ctr - last_ctr;
            if (XcpIsConnected() && loops <= 5) {
                DBG_PRINTF_WARNING("XCP receive thread: only %u loops per second, slow background processing, check if the thread is blocked\n", loops);
            }
            if (loops > 1000) {
                DBG_PRINT_WARNING("XCP receive thread: more than 1000 loops per second, check if the thread is busy waiting\n");
            }
            DBG_PRINTF6("XCP receive thread: %llu loop per second\n", loops);
            last_time = now;
            last_ctr = ctr;
        }
#endif
    }
    gXcpServer.receive_thread_running = false;

    DBG_PRINT3("XCP receive thread terminated!\n");
    return 0;
}

// XCP server transmit thread
#if defined(_WIN) // Windows
DWORD WINAPI XcpServerTransmitThread(LPVOID par)
#else
extern void *XcpServerTransmitThread(void *par)
#endif
{
    (void)par;

    DBG_PRINT3("Start XCP transmit thread\n");

    // Transmit loop
    gXcpServer.transmit_thread_running = true;
    while (gXcpServer.transmit_thread_running) {

        // Transmit all committed messages from the transmit queue
        int32_t n = XcpTlHandleTransmitQueue();
        if (n < 0) {
            DBG_PRINT_ERROR("XcpTlHandleTransmitQueue failed!\n");
            break; // error - terminate thread
        }

#ifdef TEST_ENABLE_DBG_CHECKS
        static uint64_t ctr = 0;
        ctr++;
        static uint64_t last_time = 0;
        static uint64_t last_ctr = 0;
        uint64_t now = clockGetMonotonicUs();
        if (now - last_time >= 1000000) { // every 1s
            uint32_t loops = ctr - last_ctr;
            if (XcpIsConnected() && loops <= 5) {
                DBG_PRINTF_WARNING("XCP transmit thread: only %u loops per second, slow background processing, check if the thread is blocked\n", loops);
            }
            if (loops > 2000) {
                DBG_PRINT_WARNING("XCP transmit thread: more than 2000 loops per second, check if the thread is busy waiting\n");
            }
            DBG_PRINTF6("XCP transmit thread: %llu loops per second\n", loops);
            last_time = now;
            last_ctr = ctr;
        }
#endif
    }
    gXcpServer.transmit_thread_running = false;

    DBG_PRINT3("XCP transmit thread terminated!\n");
    return 0;
}

//-------------------------------------------------------------------------------------------------------
// SHM server thread

#ifdef OPTION_SHM_MODE
// In SHM mode, there is a background thread in applications which are not the XCP server
// Polls the A2L finalize request flag so followers can write their A2L file as soon
// as the leader gets an XCP client CONNECT.  Also increments the alive_counter so
// the leader can detect stale follower processes.
#if defined(_WIN) // Windows
DWORD WINAPI XcpShmThread(LPVOID par)
#else
extern void *XcpShmThread(void *par)
#endif
{
    (void)par;
    DBG_PRINT3("Start SHM thread\n");

    // Start the XCP protocol layer
    // Without starting the protocoll layer, event handling is not enabled
    XcpStart(gXcpServer.transmit_queue, false);

    gXcpServer.shm_thread_running = true;
    bool a2l_finalized = false; // tracks whether this follower has already finalized its A2L
    while (gXcpServer.shm_thread_running) {
        sleepUs(50000); // Poll every 50 ms

        // Handle background tasks, e.g. pending calibration updates
        XcpBackgroundTasks();

        // Prove this follower is still alive
        XcpShmIncrementAliveCounter();

#ifdef OPTION_ENABLE_A2L_GENERATOR
        if (!a2l_finalized && XcpShmIsA2lFinalizeRequested()) {
            sleepMs(500);
            // @@@@ TODO workaround by some delay because the finalize request could already set, when attaching to the SHM after the leader has requested A2L
            // finalization. Remove this workaround by a better synchronization between the leader and followers.
            if (A2lFinalize()) {
                DBG_PRINT3("A2L finalized by SHM request\n");
                a2l_finalized = true;
            }
        }
#endif
    }

    gXcpServer.shm_thread_running = false;
    DBG_PRINT3("SHM background thread terminated!\n");
    return 0;
}

#endif // OPTION_SHM_MODE
