/*----------------------------------------------------------------------------
| File:
|   xcpEthServer.c
|
| Description:
|   XCP on UDP/TCP Server
|   Creates threads for cmd handling and data transmission
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
-----------------------------------------------------------------------------*/

#include "xcpEthServer.h"

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t
#include <stdio.h>    // for printf

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "platform.h"  // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex
#include "queue.h"
#include "xcp.h"        // for CRC_XXX
#include "xcpEthTl.h"   // for tXcpCtoMessage, xcpTlXxxx, xcpEthTlxxx
#include "xcpLite.h"    // for tXcpDaqLists, XcpXxx, ApplXcpXxx, ...
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcptl_cfg.h"  // for XCPTL_xxx

#ifdef OPTION_SHM_MODE
#include <unistd.h> // for getpid()
#endif

#if !defined(_WIN) && !defined(_LINUX) && !defined(_MACOS) && !defined(_QNX)
#error "Please define platform _WIN, _MACOS or _LINUX or _QNX"
#endif

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

// SHM queue region header for /xcpqueue
// Placed at the start of the shared queue memory; the actual queue data starts immediately after.
// Fixed-size types used to avoid uint_fast32_t platform differences (e.g. 8 bytes on arm64).
#ifdef OPTION_SHM_MODE
typedef struct {
    _Atomic uint32_t is_initialized; // set to 1 by the leader after queueInitFromMemory() completes
    _Atomic int32_t leader_pid;      // PID of the leader process; 0 until ready
    uint32_t queue_size;             // usable data area size (bytes) passed to queueInitFromMemory
    uint32_t pad[13];                // pad struct to exactly 64 bytes (one cache line)
} tShmQueueHeader;
static_assert(sizeof(tShmQueueHeader) == 64, "tShmQueueHeader must be 64 bytes");
#endif // OPTION_SHM_MODE

static struct {

    bool isInit;

    // Threads
    THREAD TransmitThreadHandle;
    volatile bool TransmitThreadRunning;
    THREAD ReceiveThreadHandle;
    volatile bool ReceiveThreadRunning;

    // Queue
    tQueueHandle TransmitQueue;

#ifdef OPTION_SHM_MODE
    // SHM queue tracking (XCP_MODE_SHM only; zeroed otherwise)
    bool shm_is_leader;          // true = this process created /xcpqueue
    void *shm_queue_ptr;         // mmap base of the /xcpqueue region, NULL when not SHM mode
    size_t shm_queue_total_size; // total mmap size: sizeof(tShmQueueHeader) + queue_size
#endif // OPTION_SHM_MODE

} gXcpServer;

// XCP server status
bool XcpEthServerStatus(void) {
    if (!XcpIsActivated())
        return true;
    return gXcpServer.isInit && gXcpServer.TransmitThreadRunning && gXcpServer.ReceiveThreadRunning;
}

// XCP server information
void XcpEthServerGetInfo(bool *isTcp, uint8_t *mac, uint8_t *addr, uint16_t *port) { XcpEthTlGetInfo(isTcp, mac, addr, port); }

// XCP server init
bool XcpEthServerInit(const uint8_t *addr, uint16_t port, bool useTCP, uint32_t queueSize) {

    // Check and ignore, if the XCP singleton has not been initialized and activated
    if (!XcpIsActivated()) {
        DBG_PRINT5("XcpEthServerInit: XCP is deactivated!\n");
        return true;
    }

    // Check if already initialized and running
    if (gXcpServer.isInit) {
        DBG_PRINT_WARNING("XCP server already running!\n");
        return false;
    }

    DBG_PRINT3("Start XCP server\n");
    DBG_PRINTF3("  Queue size = %u\n", queueSize);
#ifdef OPTION_ATOMIC_EMULATION
    DBG_PRINT3("  Using atomic emulation !!\n");
#endif

    gXcpServer.TransmitThreadRunning = false;
    gXcpServer.ReceiveThreadRunning = false;

    // Init network sockets
    if (!socketStartup())
        return false;

    // Create queue
    assert(queueSize > 0);
    if (queueSize < XCPTL_MAX_SEGMENT_SIZE * 4) {
        DBG_PRINT_WARNING("Queue size is smaller than XCPTL_MAX_SEGMENT_SIZE*4, may cause performance issues!\n");
    }
#ifdef OPTION_SHM_MODE
    if (XcpGetInitMode() == XCP_MODE_SHM) {
        // SHM queue: create (leader) or attach to (follower) the /xcpqueue shared memory region
        bool is_leader = false;
        size_t shm_total = sizeof(tShmQueueHeader) + queueSize;
        void *shm_ptr = platformShmOpen("/xcpqueue", "/tmp/xcpqueue.lock", shm_total, &is_leader);
        if (shm_ptr == NULL) {
            DBG_PRINT_ERROR("XcpEthServerInit: failed to open /xcpqueue SHM\n");
            return false;
        }
        gXcpServer.shm_is_leader = is_leader;
        gXcpServer.shm_queue_ptr = shm_ptr;
        gXcpServer.shm_queue_total_size = shm_total;
        tShmQueueHeader *hdr = (tShmQueueHeader *)shm_ptr;
        void *queue_mem = (uint8_t *)shm_ptr + sizeof(tShmQueueHeader);

        if (is_leader) {
            // Leader: initialise queue in SHM and signal readiness
            hdr->queue_size = queueSize;
            gXcpServer.TransmitQueue = queueInitFromMemory(queue_mem, queueSize, true, NULL);
            if (gXcpServer.TransmitQueue == NULL) {
                DBG_PRINT_ERROR("XcpEthServerInit: queueInitFromMemory failed\n");
                platformShmClose("/xcpqueue", shm_ptr, shm_total, true);
                return false;
            }
            atomic_store(&hdr->leader_pid, (int32_t)getpid());
            atomic_store(&hdr->is_initialized, 1U);
            DBG_PRINTF3("XcpEthServerInit: SHM queue leader, queue_size=%u\n", queueSize);
            // Leader falls through to normal socket + thread creation below
        } else {
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
            gXcpServer.TransmitQueue = queueInitFromMemory(queue_mem, hdr->queue_size, false, NULL);
            if (gXcpServer.TransmitQueue == NULL) {
                DBG_PRINT_ERROR("XcpEthServerInit: follower queueInitFromMemory failed\n");
                platformShmClose("/xcpqueue", shm_ptr, shm_total, false);
                return false;
            }
            DBG_PRINTF3("XcpEthServerInit: SHM queue follower attached, leader_pid=%d\n", (int)atomic_load(&hdr->leader_pid));
            // Follower: bind the queue to the XCP layer but skip socket and thread creation
            XcpStart(gXcpServer.TransmitQueue, false);
            gXcpServer.isInit = true;
            return true;
        }
    } else
#endif // OPTION_SHM_MODE
    {
        gXcpServer.TransmitQueue = queueInit(queueSize);
        if (gXcpServer.TransmitQueue == NULL)
            return false;
    }

    // Initialize XCP transport layer
    if (!XcpEthTlInit(addr, port, useTCP, gXcpServer.TransmitQueue))
        return false;

    // Create the receive thread
    create_thread(&gXcpServer.ReceiveThreadHandle, XcpServerReceiveThread);

    // Wait until receive thread is running to avoid races
    // The receive thread will acquire ownership of the XCP singleton by calling XcpStart(gXcpServer.TransmitQueue, false)
    while (!gXcpServer.ReceiveThreadRunning) {
        sleepUs(100);
    }

    // Create the transmit thread
    create_thread(&gXcpServer.TransmitThreadHandle, XcpServerTransmitThread);

    gXcpServer.isInit = true;
    return true;
}

// XCP server shutdown
bool XcpEthServerShutdown(void) {

    if (!XcpIsActivated()) {
        DBG_PRINT5("XcpEthServerInit: XCP is deactivated!\n");
        return false;
    }

#ifdef OPTION_SHM_MODE
    // SHM follower: no socket or threads were created — just clean up the queue and SHM region
    if (XcpGetInitMode() == XCP_MODE_SHM && gXcpServer.isInit && !gXcpServer.shm_is_leader) {
        gXcpServer.isInit = false;
        queueDeinit(gXcpServer.TransmitQueue);
        platformShmClose("/xcpqueue", gXcpServer.shm_queue_ptr, gXcpServer.shm_queue_total_size, false);
        gXcpServer.shm_queue_ptr = NULL;
        return true;
    }
#endif // OPTION_SHM_MODE

#ifdef OPTION_SERVER_FORCEFULL_TERMINATION
    // Forcefull termination
    if (gXcpServer.isInit) {
        DBG_PRINT3("Disconnect, cancel threads and shutdown XCP!\n");
        XcpDisconnect();
        cancel_thread(gXcpServer.ReceiveThreadHandle);
        cancel_thread(gXcpServer.TransmitThreadHandle);
        XcpEthTlShutdown();
        gXcpServer.isInit = false;
        socketCleanup();
        XcpReset();
    }
#else
    // Gracefull termination
    if (gXcpServer.isInit) {
        XcpDisconnect();
        gXcpServer.ReceiveThreadRunning = false;
        gXcpServer.TransmitThreadRunning = false;
        XcpEthTlShutdown();
        join_thread(gXcpServer.ReceiveThreadHandle);
        join_thread(gXcpServer.TransmitThreadHandle);
        gXcpServer.isInit = false;
        socketCleanup();
        XcpReset();
    }
#endif

    queueDeinit(gXcpServer.TransmitQueue);

#if !defined(_WIN)
    // SHM leader: release the /xcpqueue shared memory region and unlink the SHM object
    if (gXcpServer.shm_queue_ptr != NULL) {
        platformShmClose("/xcpqueue", gXcpServer.shm_queue_ptr, gXcpServer.shm_queue_total_size, gXcpServer.shm_is_leader);
        gXcpServer.shm_queue_ptr = NULL;
    }
#endif

    return true;
}

// XCP server unicast command receive thread
#if defined(_WIN) // Windows
DWORD WINAPI XcpServerReceiveThread(LPVOID par)
#else
extern void *XcpServerReceiveThread(void *par)
#endif
{
    (void)par;
    DBG_PRINT3("Start XCP receive thread\n");

    // Start the XCP protocol layer and acquire ownership of the XCP singleton
    XcpStart(gXcpServer.TransmitQueue, false);

    // Receive XCP unicast commands loop
    gXcpServer.ReceiveThreadRunning = true;
    while (gXcpServer.ReceiveThreadRunning) {
        if (!XcpEthTlHandleCommands()) { // Blocking
            DBG_PRINT_ERROR("XcpEthTlHandleCommands failed!\n");
            break; // error -> terminate thread
        }
        XcpBackgroundTasks(); // Handle background tasks, e.g. pending calibration updates
    }
    gXcpServer.ReceiveThreadRunning = false;

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
    gXcpServer.TransmitThreadRunning = true;
    while (gXcpServer.TransmitThreadRunning) {

        // Transmit all committed messages from the transmit queue
        int32_t n = XcpTlHandleTransmitQueue();
        if (n < 0) {
            DBG_PRINT_ERROR("XcpTlHandleTransmitQueue failed!\n");
            break; // error - terminate thread
        }
    }
    gXcpServer.TransmitThreadRunning = false;

    DBG_PRINT3("XCP transmit thread terminated!\n");
    return 0;
}
