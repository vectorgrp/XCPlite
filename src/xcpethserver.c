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
#ifdef OPTION_SHM_MODE
#include <unistd.h> // for getpid()
#endif

#include "a2l.h"        // for A2lFinalize()
#include "dbg_print.h"  // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "platform.h"   // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex
#include "queue.h"      // for tQueueHandle, queueInitFromMemory, ...
#include "xcp.h"        // for CRC_XXX
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcplite.h"    // for tXcpDaqLists, XcpXxx, ApplXcpXxx, ...

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

#ifdef OPTION_SHM_MODE // SHM mode thread

#if defined(_WIN) // Windows
static DWORD WINAPI ShmThread_(LPVOID lpParameter);
#else
static void *ShmThread_(void *par);
#endif

// In SHM mode, the queue is in shared memory and has an additional shared memory header
// SHM queue region header for /xcpqueue
// Placed at the start of the shared queue memory, the actual queue data starts immediately after.
typedef struct {
    atomic_uint_least32_t is_initialized; // set to 1 after queueInitFromMemory(clear=true) completes
    uint32_t queue_size;                  // size passed to queueInitFromMemory
    uint32_t pad[14];                     // pad struct to exactly 64 bytes (one cache line)
} tShmQueueHeader;
static_assert(sizeof(tShmQueueHeader) == 64, "tShmQueueHeader must be 64 bytes");

#endif // SHM_MODE

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

#ifdef OPTION_SHM_MODE // transmit queue state in shared memory mode

    void *shm_queue_ptr;         // mmap base of the /xcpqueue region, NULL when SHM mode not activated
    size_t shm_queue_total_size; // total mmap size: sizeof(tShmQueueHeader) + queue_size
    THREAD shm_thread_handle;    // Background thread for non server processes
    volatile bool shm_thread_running;

#endif

} gXcpServer;

//-------------------------------------------------------------------------------------------------------
// SHM server

#ifdef OPTION_SHM_MODE // SHM server functions

// Initialize shared memory queue and start the background thread for non-server processes in SHM mode
static bool ShmServerInit_(uint32_t queue_size) {

    gXcpServer.shm_thread_running = false;
    gXcpServer.shm_queue_ptr = NULL;

    // Create or attach to the SHM queue
    size_t queue_total_size = sizeof(tShmQueueHeader) + queue_size;
    bool queue_leader = false;
    void *queue_ptr = platformShmOpen("/xcpqueue", "/tmp/xcpqueue.lock", queue_total_size, &queue_leader);
    // @@@@ TODO: Optimization, if we are sure it already exists, saves a few 100 us
    // void *queue_ptr = platformShmOpenAttach("/xcpqueue", &queue_total_size);
    if (queue_ptr == NULL) {
        DBG_PRINT_ERROR("XcpEthServerInit: failed to create '/xcpqueue'\n");
        return false;
    }
    tShmQueueHeader *hdr = (tShmQueueHeader *)queue_ptr;
    if (queue_leader) {
        hdr->queue_size = queue_size;
        DBG_PRINTF3(ANSI_COLOR_BLUE "Created '/xcpqueue' with %zu bytes (including header)\n" ANSI_COLOR_RESET, queue_total_size);
    } else {
        // @@@@ TODO: There might be a race condition until the leader has initialized the queue ? Wait !
        if (hdr->queue_size != queue_size) {
            DBG_PRINTF_WARNING("XcpEthServerInit: queue size mismatch for '/xcpqueue', expected %u, got %u\n", queue_size, hdr->queue_size);
        }
        assert(atomic_load(&hdr->is_initialized) != 0);
        DBG_PRINTF3(ANSI_COLOR_BLUE "Attached to '/xcpqueue', size %zu bytes (including header)\n" ANSI_COLOR_RESET, queue_total_size);
    }
    gXcpServer.shm_queue_ptr = queue_ptr;
    gXcpServer.shm_queue_total_size = queue_total_size;
    void *queue_mem = (uint8_t *)queue_ptr + sizeof(tShmQueueHeader);
    gXcpServer.transmit_queue = queueInitFromMemory(queue_mem, queue_size, queue_leader /* clear*/, NULL);
    if (gXcpServer.transmit_queue == NULL) {
        DBG_PRINT_ERROR("XcpEthServerInit: queueInitFromMemory failed\n");
        platformShmClose("/xcpqueue", queue_ptr, queue_total_size, queue_leader /* unlink */);
        return false;
    }
    if (queue_leader) {
        atomic_store(&hdr->is_initialized, 1U);
    }
    DBG_PRINTF3(ANSI_COLOR_BLUE "Queue init (clear=%u, queue_size=%u)\n" ANSI_COLOR_RESET, queue_leader, queue_size);

    // Start the background thread for non-server processes
    if (!XcpShmIsXcpServer()) {
        create_thread(&gXcpServer.shm_thread_handle, ShmThread_);
        DBG_PRINT(ANSI_COLOR_BLUE "SHM mode initialized without XCP on ethernet server, create SHM thread\n" ANSI_COLOR_RESET);
    }

    return true;
}

void ShmServerShutdown_(void) {
    DBG_PRINT3(ANSI_COLOR_BLUE "Terminate SHM thread\n" ANSI_COLOR_RESET);
    gXcpServer.shm_thread_running = false;
    join_thread(gXcpServer.shm_thread_handle);
    DBG_PRINT3(ANSI_COLOR_BLUE "Unmap '/xcpqueue'\n" ANSI_COLOR_RESET);
    platformShmClose("/xcpqueue", gXcpServer.shm_queue_ptr, gXcpServer.shm_queue_total_size, false);
    gXcpServer.shm_queue_ptr = NULL;
    gXcpServer.is_init = false;
}

// SHM server thread
// Background thread in all other applications which are not an XCP server in SHM mode
// Polls the A2L finalize request flag, so applications can write their A2L file on request by the server
// Also increments the alive_counter to detect stale processes.
#if defined(_WIN) // Windows
DWORD WINAPI ShmThread_(LPVOID par)
#else
extern void *ShmThread_(void *par)
#endif
{
    (void)par;
    DBG_PRINT3(ANSI_COLOR_BLUE "SHM thread started\n" ANSI_COLOR_RESET);

    // Start the XCP protocol layer
    // Without starting the protocoll layer, event handling is not enabled
    XcpStart(gXcpServer.transmit_queue, false);

    gXcpServer.shm_thread_running = true;
    while (gXcpServer.shm_thread_running) {

        uint64_t now = clockGetMonotonicNs(); // Drive the current last time with 50ms cycle in this loop
        (void)now;

        sleepUs(50000); // 50 ms

        // Handle background tasks, e.g. pending calibration updates
        XcpBackgroundTasks();

        // Prove this application is still alive
        XcpShmIncrementAliveCounter();

        // Poll the global A2L finalize request flag
        // If set, finalize the local A2L file if not already done
#ifdef OPTION_ENABLE_A2L_GENERATOR
        if (!XcpShmIsA2lFinalized(XcpShmGetAppId()) && XcpShmIsA2lFinalizeRequested()) {
            // @@@@ TODO: Check how to handle this
            sleepMs(100);
            if (A2lFinalize()) {
                DBG_PRINT3(ANSI_COLOR_BLUE "A2L finalized by SHM request\n" ANSI_COLOR_RESET);
            }
        }
#endif
    }

    gXcpServer.shm_thread_running = false;
    DBG_PRINT3(ANSI_COLOR_BLUE "SHM background thread terminated!\n" ANSI_COLOR_RESET);
    return 0;
}

//-------------------------------------------------------------------------------------------------------
// Public functions with same functionality as XcpEthServerXxx
// Not used yet

// SHM server init
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

    if (!ShmServerInit_(queue_size)) {
        return false;
    }

    DBG_PRINT3("XCP SHM server initialized\n");
    gXcpServer.is_init = true;
    return true;
}

// XCP on SHM server shutdown
bool XcpShmServerShutdown(void) { return XcpEthServerShutdown(); }

// XCP on SHM server status
bool XcpShmServerStatus(void) { return gXcpServer.is_init && gXcpServer.shm_thread_running; }

#endif // SHM_MODE

//-------------------------------------------------------------------------------------------------------
// Public function XCP on Ethernet server

// XCP server status
bool XcpEthServerStatus(void) {
    if (!XcpIsActivated())
        return true;

#ifdef OPTION_SHM_MODE // SHM server get status
    if (!XcpShmIsXcpServer()) {
        return XcpShmServerStatus();
    }
#endif

    return gXcpServer.is_init && gXcpServer.transmit_thread_running && gXcpServer.receive_thread_running;
}

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

#ifdef OPTION_SHM_MODE // call SHM server init
    // Init SHM and create the transmit queue in shared memory
    if (!ShmServerInit_(queue_size)) {
        return false;
    }
#else
    // Create the transmit queue on heap
    assert(queue_size > 0);
    gXcpServer.transmit_queue = queueInit(queue_size);
    if (gXcpServer.transmit_queue == NULL)
        return false;
#endif

#ifdef OPTION_SHM_MODE // XCP server initialisation
    // In SHM mode, start the XCP on ethernet server only in the SHM server
    if (XcpShmIsXcpServer())
#endif // SHM_MODE else

    // Start the XCP on ethernet server with the created transmit queue
    {
        DBG_PRINT3(ANSI_COLOR_GREEN "Start XCP on Ethernet server\n" ANSI_COLOR_RESET);

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
            sleepUs(20);
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
        DBG_PRINT5("XcpEthServerShutdown: XCP is deactivated!\n");
        return false;
    }

    // Check if already initialized and running
    if (!gXcpServer.is_init) {
        DBG_PRINT_WARNING("XcpEthServerShutdown:XCP server not running!\n");
        return false;
    }

    DBG_PRINT3("Disconnect and shutdown XCP!\n");
    XcpDisconnect();

    // Reset the XCP protocol layer
    // In SHM mode deregister application
    XcpDeinit();

#ifdef OPTION_SHM_MODE // SHM server shutdown
    if (!XcpShmIsXcpServer()) {
        // Not XCP server: stop the shm background thread, then clean up, unmap but do not unlink the queue
        ShmServerShutdown_();
        return true;
    }
#endif // SHM_MODE

    DBG_PRINT3("Terminate server threads\n");
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

#ifdef OPTION_SHM_MODE // SHM queue deinit
    // In SHM mode, if the server goes down, unmap and unlink the queue
    // No new application can attach to the existing SHM queue anymore, because the server is down,
    // Still exiting applications will detect queue overflow and take appropriate action
    DBG_PRINT3(ANSI_COLOR_BLUE "Unlink '/xcpqueue'\n" ANSI_COLOR_RESET);
    assert(gXcpServer.shm_queue_ptr != NULL);
    platformShmClose("/xcpqueue", gXcpServer.shm_queue_ptr, gXcpServer.shm_queue_total_size, true /* unlink */);
    gXcpServer.shm_queue_ptr = NULL;
#else
    // Free the transmit queue, if it was created on heap
    queueDeinit(gXcpServer.transmit_queue);
#endif

    return true;
}

//-------------------------------------------------------------------------------------------------------
// Server threads

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

        static uint64_t ctr = 0;
        ctr++;
        (void)ctr;

        uint64_t now = clockGetMonotonicNs(); // Drive the current last time with XCPTL_RECV_TIMEOUT_MS cycle in this loop

        // Blocking, with timeout to allow handling background tasks in this thread as well
        if (!XcpEthTlHandleCommands()) {
            DBG_PRINT_ERROR("XcpEthTlHandleCommands failed!\n");
            break; // error -> terminate thread
        }

        // Handle background tasks, e.g. pending calibration updates
        XcpBackgroundTasks();

#ifdef OPTION_SHM_MODE // increment alive counter
        // In SHM mode, prove this application is still alive
        XcpShmIncrementAliveCounter();
#endif

        // Every 1s
        static uint64_t last_time = 0;
        if (now - last_time >= 1000000000ULL) {
            last_time = now;

#ifdef OPTION_SHM_MODE // check alive counters of all applications
            // In SHM mode, Server checks alive counters of all applications and prints debug info
            XcpShmCheckAliveCounters();
#endif

#ifdef TEST_ENABLE_DBG_CHECKS
            {
                static uint64_t last_ctr = 0;
                uint64_t loops = ctr - last_ctr;
                if (XcpIsConnected() && loops <= 5) {
                    DBG_PRINTF_WARNING("XCP receive thread: only %llu loops per second, slow background processing\n", loops);
                }
                if (loops > 1000) {
                    DBG_PRINT_WARNING("XCP receive thread: more than 1000 loops per second\n");
                }
                DBG_PRINTF6("XCP receive thread: %llu loop per second\n", loops);
                last_ctr = ctr;
            }
#endif
        }
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
        uint64_t now = clockGetMonotonicNsLast();
        static uint64_t ctr = 0;
        ctr++;
        static uint64_t last_time = 0;
        static uint64_t last_ctr = 0;
        if (now - last_time >= 1000000000ULL) { // every 1s
            uint64_t loops = ctr - last_ctr;
            if (XcpIsConnected() && loops <= 5) {
                DBG_PRINTF_WARNING("XCP transmit thread: only %llu loops per second, slow background processing\n", loops);
            }
            if (loops > 2000) {
                DBG_PRINT_WARNING("XCP transmit thread: more than 2000 loops per second\n");
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
