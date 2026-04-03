/*----------------------------------------------------------------------------
| File:
|   xcpethtl.c
|
| Description:
|   XCP on UDP/TCP transport layer
|   Linux, MACOS and Windows version
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
 ----------------------------------------------------------------------------*/

#include "xcpethtl.h"

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t
#include <stdio.h>    // for printf
#include <string.h>   // for memcpy, strcmp

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT, ...
#include "platform.h"  // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex
#include "queue.h"
#include "xcp.h"        // for CRC_XXX
#include "xcp_cfg.h"    // for XCP_xxx
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcplite.h"    // for tXcpDaqLists, XcpXxx, ApplXcpXxx, ...
#include "xcptl_cfg.h"  // for XCPTL_xxx

// Parameter checks
#if XCPTL_TRANSPORT_LAYER_HEADER_SIZE != 4
#error "Transportlayer supports only 4 byte headers!"
#endif
#if ((XCPTL_MAX_CTO_SIZE & 0x07) != 0)
#error "XCPTL_MAX_CTO_SIZE should be aligned to 8!"
#endif
#if ((XCPTL_MAX_DTO_SIZE & 0x07) != 0)
#error "XCPTL_MAX_DTO_SIZE should be aligned to 8!"
#endif
#if ((XCPTL_MAX_SEGMENT_SIZE & 0x07) != 0)
#error "XCPTL_MAX_SEGMENT_SIZE should be aligned to 8!"
#endif
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
#ifndef XCPTL_ENABLE_MULTICAST
#error "XCPTL_ENABLE_MULTICAST must be defined for GET_DAQ_CLOCK_MULTICAST"
#endif
#endif

#pragma pack(push, 1)
typedef struct {
    uint16_t dlc;
    uint16_t ctr;
    uint8_t packet[XCPTL_MAX_CTO_SIZE];
} tXcpCtoMessage;
#pragma pack(pop)

static struct {

    tQueueHandle queue; // Transmit queue handle, used to transmit XCP DTO and EVENT messages

    MUTEX ctr_mutex; // Transmit queue handler mutex, used to keep the consistency of the message counter
    uint16_t ctr;    // Next CRM response message packet counter

#if defined(_WIN) // Windows
    HANDLE queue_event;
    uint64_t queue_event_time;
#endif

    SOCKET_HANDLE socket;
#ifdef XCPTL_ENABLE_TCP
    SOCKET_HANDLE listen_socket;
#endif

    uint8_t server_mac[6];
    uint8_t server_addr[4];
    uint16_t server_port;
    bool server_use_tcp;
    uint8_t master_addr[4];
    uint16_t master_port;
    bool master_addr_valid;

    // Multicast
#ifdef XCPTL_ENABLE_MULTICAST
    THREAD multicast_thread_handle;
    SOCKET_HANDLE multicast_sock;
#endif

#if defined(OPTION_QUEUE_64_FIX_SIZE) || defined(OPTION_QUEUE_64_VAR_SIZE)
    uint64_t last_transmit_time; // Last transmit time in ns from clockGetMonotonicNs()
#endif

} gXcpTl;

#if defined(XCPTL_ENABLE_TCP) && defined(XCPTL_ENABLE_UDP)
#define isTCP() (gXcpTl.listen_socket != INVALID_SOCKET_HANDLE)
#else
#ifdef XCPTL_ENABLE_TCP
#define isTCP() true
#else
#define isTCP() false
#endif
#endif

#ifdef XCPTL_ENABLE_MULTICAST
static int handleXcpMulticastCommand(int n, tXcpCtoMessage *p, uint8_t *dstAddr, uint16_t dstPort);
#endif

//-------------------------------------------------------------------------------------------------------
// Ethernet transport layer socket functions

// Transmit a UDP datagramm or TCP segment (contains multiple XCP DTO messages or a single CRM message (len+ctr+packet+fill))
// Must be thread safe, because it is called from CMD and from DAQ thread
// Returns false on error
#if !defined(OPTION_QUEUE_64_FIX_SIZE) && !defined(OPTION_QUEUE_64_VAR_SIZE)
static bool XcpEthTlSend(const uint8_t *data, uint16_t size, const uint8_t *addr, uint16_t port) {

    int r;

    assert(size > 0 && size <= XCPTL_MAX_SEGMENT_SIZE);
    assert(data != NULL);
    DBG_PRINTF6("XcpEthTlSend: msg_len = %u\n", size);

#ifdef TEST_ENABLE_DBG_METRICS
    gXcpTxPacketCount++;
#endif

#ifdef XCPTL_ENABLE_TCP
    if (isTCP()) {
        r = socketSend(gXcpTl.socket, data, size);
    } else
#endif
#ifdef XCPTL_ENABLE_UDP
    {
        if (addr != NULL) { // Respond to given addr and port (used for multicast only)
            r = socketSendTo(gXcpTl.socket, data, size, addr, port, NULL);
        } else { // Respond to active master
            if (!gXcpTl.master_addr_valid) {
                DBG_PRINT_ERROR("XcpEthTlSend: invalid master address!\n");
                return false;
            }
            r = socketSendTo(gXcpTl.socket, data, size, gXcpTl.master_addr, gXcpTl.master_port, NULL);
        }
    }
#endif // UDP

    if (r != size) {
        DBG_PRINTF_ERROR("XcpEthTlSend: send failed (result=%d, size=%u)!\n", r, size);
        return false;
    }
    return true;
}
#endif

#ifdef TEST_ENABLE_BUFFERCOUNT_HISTOGRAM
static uint32_t gBufferCountHistogram[256] = {0xFFFFFFFF}; // For debugging, count the size of each iovec buffer sent
#endif

// Transmit a XCP segment with XCPTL_MAX_SEGMENT_SIZE (UDP or TCP) with multiple XCP DTO messages
// Using vectored io
// Returns false on error
#if defined(OPTION_QUEUE_64_FIX_SIZE) || defined(OPTION_QUEUE_64_VAR_SIZE)
static bool XcpEthTlSendV(tQueueBuffer buffers[], uint16_t count) {

    assert(buffers != NULL);
    assert(count > 0);

    int r;

    DBG_PRINTF6("XcpEthTlSendV: buffers count = %u\n", count);

#ifdef TEST_ENABLE_BUFFERCOUNT_HISTOGRAM
    assert(count <= 256);
    if (gBufferCountHistogram[0] == 0xFFFFFFFF) {
        memset(gBufferCountHistogram, 0, sizeof(gBufferCountHistogram));
    }
    gBufferCountHistogram[count - 1]++;
#endif

#ifdef TEST_ENABLE_DBG_METRICS
    gXcpTxMessageCount++;
    gXcpTxIoVectorCount += count;
#endif

#ifdef XCPTL_ENABLE_TCP
    if (isTCP()) {
        r = socketSendV(gXcpTl.socket, buffers, count);
    } else
#endif
#ifdef XCPTL_ENABLE_UDP
    {

        // Respond to active master
        if (!gXcpTl.master_addr_valid) {
            DBG_PRINT_ERROR("XcpEthTlSendV: invalid master address!\n");
            return false;
        }
        r = socketSendToV(gXcpTl.socket, buffers, count, gXcpTl.master_addr, gXcpTl.master_port);
    }
#endif // UDP

    if (r <= 0) {
        DBG_PRINTF_ERROR("XcpEthTlSend: vectored send failed (result=%d)!\n", r);
        return false;
    }
    return true;
}
#endif // OPTION_QUEUE_64_FIX_SIZE

//-------------------------------------------------------------------------------------------------------

// Transmit a packet (the packet contains a single XCP CRM command response message)
#ifndef XCPTL_CRM_VIA_TRANSMIT_QUEUE
void XcpTlSendCrm(const uint8_t *data, uint8_t size) {
    assert(size <= XCPTL_MAX_CTO_SIZE); // Check for buffer overflow

    DBG_PRINTF6("XcpEthTlSendCrm: msg_len = %u\n", size);

    mutexLock(&gXcpTl.ctr_mutex);

    // Build XCP CTO message (ctr+dlc+packet)
    tXcpCtoMessage p;
    p.dlc = size;
    p.ctr = gXcpTl.ctr++; // Get next response packet counter
    memcpy(p.packet, data, size);

    // Send the packet using the same sendmsg path as DAQ to avoid UDP datagram reordering
    // At the NIC/kernel sendto vs sendmsg can be treated differently
    // No error handling, loosing a CRM message will lead to a timeout in the XCP client
#if defined(OPTION_QUEUE_64_FIX_SIZE) || defined(OPTION_QUEUE_64_VAR_SIZE)
    tQueueBuffer buf = {.buffer = (uint8_t *)&p, .size = (uint16_t)(size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE)};
    XcpEthTlSendV(&buf, 1);
#else
    XcpEthTlSend((const uint8_t *)&p, (uint16_t)(size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE), NULL, 0);
#endif

    mutexUnlock(&gXcpTl.ctr_mutex);
}
#endif

// Transmit XCP multicast response
#ifdef XCPTL_ENABLE_MULTICAST
void XcpEthTlSendMulticastCrm(const uint8_t *packet, uint16_t packet_size, const uint8_t *addr, uint16_t port) {

    int r;

    // Build XCP CTO message (ctr+dlc+packet)
    tXcpCtoMessage p;
    p.dlc = (uint16_t)packet_size;
    p.ctr = 0;
    memcpy(p.packet, packet, packet_size);

    // No error handling, loosing a CRM message will lead to a timeout in the XCP client
    XcpEthTlSend((uint8_t *)&p, (uint16_t)(packet_size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE), addr, port);
}
#endif

//-------------------------------------------------------------------------------------------------------

static bool handleXcpCommand(tXcpCtoMessage *p, uint8_t *srcAddr, uint16_t srcPort) {

    assert(p != NULL);

    bool connected = XcpIsConnected();

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 5) {
        DBG_PRINTF6("RX: CTR %04X LEN %04X DATA = ", p->ctr, p->dlc);
        for (int i = 0; i < p->dlc; i++)
            printf("%0X ", p->packet[i]);
        printf("\n");
    }
#endif

    /* Connected */
    if (connected) {

#ifdef XCPTL_ENABLE_UDP
        if (!isTCP() && gXcpTl.master_addr_valid) {

            // Check unicast ip address, not allowed to change
            assert(srcAddr != NULL);
            if (memcmp(&gXcpTl.master_addr, srcAddr, sizeof(gXcpTl.master_addr)) != 0) { // Message from different master received
                DBG_PRINTF_WARNING("message from unknown new master %u.%u.%u.%u, disconnecting!\n", srcAddr[0], srcAddr[1], srcAddr[2], srcAddr[3]);
                XcpDisconnect();
                gXcpTl.master_addr_valid = false;
                goto not_connected; // Disconnected, now check for new CONNECT command
            }

            // Check unicast master udp port, not allowed to change
            if (gXcpTl.master_port != srcPort) {
                DBG_PRINTF_WARNING("client port changed from %u to %u, disconnecting!\n", gXcpTl.master_port, srcPort);
                XcpDisconnect();
                gXcpTl.master_addr_valid = false;
                goto not_connected; // Disconnected, now check for new CONNECT command
            }
        }
#endif // UDP
        if (p->dlc > XCPTL_MAX_CTO_SIZE) {
            DBG_PRINTF_ERROR("Received command with invalid length %u!\n", p->dlc);
            return false;
        }
        XcpCommand((const uint32_t *)&p->packet[0], (uint8_t)p->dlc); // Handle command
    }

    /* Not connected yet */
    else {

    not_connected:

        /* Check for CONNECT command ? */
        if (p->dlc == 2 && p->packet[0] == CC_CONNECT) {

#ifdef XCPTL_ENABLE_UDP
            if (!isTCP()) {
                assert(srcAddr != NULL);
                memcpy(gXcpTl.master_addr, srcAddr,
                       sizeof(gXcpTl.master_addr)); // Save master address, so XcpCommand can send the CONNECT response
                gXcpTl.master_port = srcPort;
                gXcpTl.master_addr_valid = true;
                DBG_PRINTF3("CONNECT from UDP master %u.%u.%u.%u, port %u\n", srcAddr[0], srcAddr[1], srcAddr[2], srcAddr[3], srcPort);
            }
#endif // UDP

            queueClear(gXcpTl.queue);                                     // Clear the transmit queue, just to be sure, should be already empty
            XcpCommand((const uint32_t *)&p->packet[0], (uint8_t)p->dlc); // Handle CONNECT command
        } else {
            DBG_PRINT_WARNING("handleXcpCommand: no valid CONNECT command\n");
        }
    }

#ifdef XCPTL_ENABLE_UDP
    if (!isTCP() && !connected) { // not connected before
        if (XcpIsConnected()) {
            DBG_PRINTF3("XCP client connected on UDP addr=%u.%u.%u.%u, port=%u\n", gXcpTl.master_addr[0], gXcpTl.master_addr[1], gXcpTl.master_addr[2], gXcpTl.master_addr[3],
                        gXcpTl.master_port);
        } else {                              // Is not in connected state
            gXcpTl.master_addr_valid = false; // Any client can connect
        }
    } // not connected before
#endif // UDP

    return true;
}

// Handle incoming XCP commands
// Returns false on error
// @@@@ TODO: Check error handling
bool XcpEthTlHandleCommands(void) {

    tXcpCtoMessage msgBuf;
    int16_t n;

#ifdef XCPTL_ENABLE_TCP
    if (isTCP()) {

        // Listen mode
        // Listen to incoming TCP connection
        // Accept a new connection if no receive socket exists, otherwise receive data on the existing connection
        if (gXcpTl.socket == INVALID_SOCKET_HANDLE) {
            DBG_PRINT5("Waiting for TCP connection ...\n");
            // Wait here for incoming TCP connection
            // Blocking
            if (gXcpTl.listen_socket == INVALID_SOCKET_HANDLE)
                return false; // Listen socket closed
            gXcpTl.socket = socketAccept(gXcpTl.listen_socket, gXcpTl.master_addr);
            if (gXcpTl.socket == INVALID_SOCKET_HANDLE) {
                DBG_PRINTF_WARNING("XcpEthTlHandleCommands: socketAccept failed (errno=%d, %s)!\n", socketGetLastError(), socketGetErrorString(socketGetLastError()));
                return true; // Ignore error from accept, retry in next loop iteration
            } else {
                // Set receive timeout to allow periodic checks for shutdown and background tasks
                socketSetTimeout(gXcpTl.socket, XCPTL_RECV_TIMEOUT_MS);
                DBG_PRINTF3("XCP master %u.%u.%u.%u accepted!\n", gXcpTl.master_addr[0], gXcpTl.master_addr[1], gXcpTl.master_addr[2], gXcpTl.master_addr[3]);
                DBG_PRINT3("Listening for XCP commands on TCP socket\n");
            }
        }

        // Receive TCP transport layer message, blocking with timeout
        // Receive header
        n = socketRecv(gXcpTl.socket, (uint8_t *)&msgBuf.dlc, (uint16_t)XCPTL_TRANSPORT_LAYER_HEADER_SIZE, true);

        // n = 0, no data or timeout from socketSetTimeout()
        if (n == 0) {
            DBG_PRINT6("XcpEthTlHandleCommands: socketRecv (header) returned 0, socket_timeout, return true\n");
            return true; // Ok, timeout from socketSetTimeout()
        }

        // n < 0 Error - Socket closed or other error
        else if (n < 0) {
            int32_t err = socketGetLastError();
            DBG_PRINTF6("XcpEthTlHandleCommands: socketRecv (header) returned <0, err = (%d,%s)\n", err, socketGetErrorString(err));
            if (err == 0 || socketIsClosed(err) || socketTimeout(err)) { // Check for socket closed
            socket_closed:
                DBG_PRINT6("XcpEthTlHandleCommands: socket_closed, disconnect XCP, goto accept mode again, return true\n");
                DBG_PRINT3("XCP Master closed TCP connection! XCP disconnected.\n");
                XcpDisconnect();               // Set XCP into disconnected state
                sleepMs(100);                  // @@@@ TODO: Check if this sleep is really needed
                socketShutdown(gXcpTl.socket); // Shutdown the socket and close it to free resources and to be able to accept a new connection
                socketClose(&gXcpTl.socket);
                return true; // Ok, TCP socket closed, go to listen mode again
            } else {
            socket_error:
                DBG_PRINT_ERROR("XcpEthTlHandleCommands: socket_error, return false\n");
                return false; // Error, socket error
            }
        } // n < 0

        // n > 0 ok, data received, should be the header
        else {

            // @@@@ Check to be sure that the header is received in full, otherwise the TCP stream is desynchronised and we should close the connection to recover
            assert(n == XCPTL_TRANSPORT_LAYER_HEADER_SIZE);
            if (n != XCPTL_TRANSPORT_LAYER_HEADER_SIZE) {
                DBG_PRINTF_ERROR("XcpEthTlHandleCommands: expected %u bytes, received %u bytes, closing connection\n", msgBuf.dlc, n);
                goto socket_error; // Should not happen with waitall=true
            }

            // Receive packet
            n = socketRecv(gXcpTl.socket, (uint8_t *)&msgBuf.packet, msgBuf.dlc, true); // packet

            // n = 0, no data, timeout from socketSetTimeout()
            if (n == 0) {
                DBG_PRINT_ERROR("XcpEthTlHandleCommands: timeout on receiving packet data, closing connection\n");
                return false; // Should not happen with waitall=true
            }

            // Error n < 0 - Socket closed or other error
            else if (n < 0) {
                int32_t err = socketGetLastError();
                DBG_PRINTF6("XcpEthTlHandleCommands: socketRecv packet returned <0, n = %d, err = (%d,%s)\n", n, err, socketGetErrorString(err));
                if (err == 0 || socketIsClosed(err) || socketTimeout(err)) {
                    DBG_PRINT_ERROR("XcpEthTlHandleCommands: Closed TCP connection during packet receive!\n");
                    goto socket_closed;
                }
                DBG_PRINTF_ERROR("XcpEthTlHandleCommands: socketRecv for packet failed n=%d (errno=%d, %s)!\n", n, err, socketGetErrorString(err));
                goto socket_error; // Treat always as error
            } // n < 0

            // n > 0 ok, data received, should be the header
            else {
                if (n != msgBuf.dlc) {
                    DBG_PRINTF_ERROR("XcpEthTlHandleCommands: expected %u bytes, received %u bytes, closing connection\n", msgBuf.dlc, n);
                    goto socket_error; // Should not happen with waitall=true
                }
#ifdef TEST_ENABLE_DBG_METRICS
                gXcpRxPacketCount++;
#endif
                return handleXcpCommand(&msgBuf, NULL, 0);
            } // packet
        } // header
    } // isTCP
#endif // TCP

#ifdef XCPTL_ENABLE_UDP
    if (!isTCP()) {
        uint16_t srcPort;
        uint8_t srcAddr[4];
        n = socketRecvFrom(gXcpTl.socket, (uint8_t *)&msgBuf, (uint16_t)sizeof(msgBuf), srcAddr, &srcPort, NULL);

        // n = 0, no data, timeout from socketSetTimeout()
        if (n == 0) {
            return true; // Timeout — no data pending
        }

        // n < 0 Error - Socket closed or other error
        else if (n < 0) {
            int32_t err = socketGetLastError();
            DBG_PRINTF_ERROR("XcpEthTlHandleCommands: socketRecvFrom failed n=%d (errno=%d, %s)!\n", n, err, socketGetErrorString(err));
            return false; // Socket error
        }

        // n > 0, Ok, data received
        else {
#ifdef TEST_ENABLE_DBG_METRICS
            gXcpRxPacketCount++;
#endif
            if (msgBuf.dlc != n - XCPTL_TRANSPORT_LAYER_HEADER_SIZE) {
                DBG_PRINT_ERROR("XcpEthTlHandleCommands: Corrupt message received!\n");
                return false; // Error
            }
            return handleXcpCommand(&msgBuf, srcAddr, srcPort);
        }
    }
#endif // UDP

    assert(false); // Should not happen, either TCP or UDP must be enabled
    return false;
}

//-------------------------------------------------------------------------------------------------------
// XCP Multicast

#ifdef XCPTL_ENABLE_MULTICAST

static int handleXcpMulticastCommand(int n, tXcpCtoMessage *p, uint8_t *dstAddr, uint16_t dstPort) {

    (void)dstAddr;
    (void)dstPort;

    // @@@@ TODO: Check multicast addr and cluster id and port
    // printf("MULTICAST: %u.%u.%u.%u:%u len=%u\n", dstAddr[0], dstAddr[1], dstAddr[2], dstAddr[3], dstPort, n);

    // Valid socket data received, at least transport layer header and 1 byte
    if (n >= XCPTL_TRANSPORT_LAYER_HEADER_SIZE + 1 && p->dlc <= n - XCPTL_TRANSPORT_LAYER_HEADER_SIZE) {
        if (p->dlc >= XCPTL_MAX_CTO_SIZE)
            return 0;                                                 // Error
        XcpCommand((const uint32_t *)&p->packet[0], (uint8_t)p->dlc); // Handle command
    } else {
        printf("MULTICAST ignored\n");
    }
    return 1; // Ok
}

void XcpEthTlSetClusterId(uint16_t clusterId) {
    (void)clusterId;
    // Not implemented
}

#if !defined(_WIN) && !defined(_LINUX) && !defined(_MACOS) && !defined(_QNX)
#error "Please define platform _WIN, _MACOS or _LINUX or _QNX"
#endif

#if defined(_WIN) // Windows
DWORD WINAPI XcpTlMulticastThread(LPVOID par)
#else
extern void *XcpTlMulticastThread(void *par)
#endif
{
    uint8_t buffer[256];
    int16_t n;
    uint16_t srcPort;
    uint8_t srcAddr[4];

    (void)par;

    for (;;) {
        n = socketRecvFrom(gXcpTl.multicast_sock, buffer, (uint16_t)sizeof(buffer), srcAddr, &srcPort, NULL);
        if (n <= 0)
            break; // Terminate on error or socket close
#ifdef XCLTL_RESTRICT_MULTICAST
        // Accept multicast from active master only
        if (gXcpTl.master_addr_valid && memcmp(gXcpTl.master_addr, srcAddr, 4) == 0) {
            handleXcpMulticastCommand(n, (tXcpCtoMessage *)buffer, srcAddr, srcPort);
        } else {
            DBG_PRINTF_WARNING("Ignored Multicast from %u.%u.%u.%u:%u\n", srcAddr[0], srcAddr[1], srcAddr[2], srcAddr[3], srcPort);
        }
#else
        handleXcpMulticastCommand(n, (tXcpCtoMessage *)buffer, srcAddr, srcPort);
#endif
    }
    DBG_PRINT3("XCP multicast thread terminated\n");
    socketClose(&gXcpTl.multicast_sock);
    return 0;
}

#endif // XCPTL_ENABLE_MULTICAST

//-------------------------------------------------------------------------------------------------------

// Initialize transport layer
// Queue can be provided externally in Queue, if *Queue==NULL queue is returned
bool XcpEthTlInit(const uint8_t *addr, uint16_t port, bool useTCP, tQueueHandle Queue) {

    DBG_PRINT3("Init XCP transport layer\n");
    DBG_PRINTF3("  MAX_CTO_SIZE=%u\n", XCPTL_MAX_CTO_SIZE);
#ifdef XCPTL_ENABLE_MULTICAST
    DBG_PRINT3("        Option ENABLE_MULTICAST (not recommended)\n");
#endif

    assert(Queue != NULL);
    gXcpTl.queue = Queue;
    mutexInit(&gXcpTl.ctr_mutex, false, 0);
    gXcpTl.ctr = 0; // Reset packet counter
#if defined(OPTION_QUEUE_64_FIX_SIZE) || defined(OPTION_QUEUE_64_VAR_SIZE)
    gXcpTl.last_transmit_time = 0; // Reset last transmit time
#endif

    // Initialize transport layer event
#if defined(_WIN) // Windows
    gXcpTl.queue_event = CreateEvent(NULL, true, false, NULL);
    assert(gXcpTl.queue_event != NULL);
    gXcpTl.queue_event_time = 0;
#endif

    uint8_t bind_addr[4] = {0, 0, 0, 0}; // Bind to ANY(0.0.0.0)
    if (addr != NULL) {                  // Bind to given addr
        memcpy(bind_addr, addr, 4);
    }

    gXcpTl.server_port = port;
    gXcpTl.server_use_tcp = useTCP;
    gXcpTl.master_addr_valid = false;
    gXcpTl.socket = INVALID_SOCKET_HANDLE;

    // Unicast UDP or TCP commands
#ifdef XCPTL_ENABLE_TCP
    gXcpTl.listen_socket = INVALID_SOCKET_HANDLE;
    if (useTCP) { // TCP
        if (!socketOpen(&gXcpTl.listen_socket, SOCKET_MODE_TCP | SOCKET_MODE_REUSEADDR))
            return false;
        if (!socketBind(gXcpTl.listen_socket, bind_addr, gXcpTl.server_port))
            return false;
        if (!socketListen(gXcpTl.listen_socket))
            return false; // Put socket in listen mode
        DBG_PRINTF3("  Listening for TCP connections on %u.%u.%u.%u port %u\n", bind_addr[0], bind_addr[1], bind_addr[2], bind_addr[3], port);
    } else
#else
    if (useTCP) { // TCP
        DBG_PRINT_ERROR("Must #define XCPTL_ENABLE_TCP for TCP support\n");
        return false;
    } else
#endif
    { // UDP
        if (!socketOpen(&gXcpTl.socket, SOCKET_MODE_REUSEADDR))
            return false;
        if (!socketBind(gXcpTl.socket, bind_addr, port))
            return false; // Bind on ANY, when serverAddr=255.255.255.255

        // Set receive timeout to allow periodic checks for shutdown and background tasks
        socketSetTimeout(gXcpTl.socket, XCPTL_RECV_TIMEOUT_MS);

        DBG_PRINTF3("  Listening for XCP commands on UDP %u.%u.%u.%u port %u\n", bind_addr[0], bind_addr[1], bind_addr[2], bind_addr[3], port);
    }

#ifdef OPTION_ENABLE_GET_LOCAL_ADDR
    {
        uint8_t addr1[4] = {0, 0, 0, 0};
        uint8_t mac1[6] = {0, 0, 0, 0, 0, 0};
        socketGetLocalAddr(mac1, addr1); // Store actual MAC and IP addr for later use
        DBG_PRINTF3("  MAC=%02X.%02X.%02X.%02X.%02X.%02X IP=%u.%u.%u.%u\n", mac1[0], mac1[1], mac1[2], mac1[3], mac1[4], mac1[5], addr1[0], addr1[1], addr1[2], addr1[3]);
        if (bind_addr[0] == 0) {
            memcpy(gXcpTl.server_addr, addr1, 4); // Store IP address for XcpEthTlGetInfo
        } else {
            memcpy(gXcpTl.server_addr, bind_addr, 4);
        }
        memcpy(gXcpTl.server_mac, mac1, 6); // Store MAC address for XcpEthTlGetInfo
    }
#endif

    // Multicast UDP commands
#ifdef XCPTL_ENABLE_MULTICAST

    // Open a socket for GET_DAQ_CLOCK_MULTICAST and join its multicast group
    if (!socketOpen(&gXcpTl.multicast_sock, SOCKET_MODE_REUSEADDR))
        return false;
    DBG_PRINTF3("  Bind XCP multicast socket to %u.%u.%u.%u:%u\n", bind_addr[0], bind_addr[1], bind_addr[2], bind_addr[3], XCPTL_MULTICAST_PORT);
    if (!socketBind(gXcpTl.multicast_sock, bind_addr, XCPTL_MULTICAST_PORT))
        return false; // Bind to ANY, when serverAddr=255.255.255.255
    uint16_t cid = XcpGetClusterId();
    uint8_t maddr[4] = {239, 255, 0, 0}; // XCPTL_MULTICAST_ADDR = 0xEFFFiiii;
    maddr[2] = (uint8_t)(cid >> 8);
    maddr[3] = (uint8_t)(cid);
    if (!socketJoin(gXcpTl.multicast_sock, maddr, addr, NULL))
        return false;
    DBG_PRINTF3("  Listening for XCP GET_DAQ_CLOCK multicast on %u.%u.%u.%u\n", maddr[0], maddr[1], maddr[2], maddr[3]);

    DBG_PRINT3("  Start XCP multicast thread\n");
    create_thread(&gXcpTl.multicast_thread_handle, XcpTlMulticastThread);

#endif

    return true;
}

void XcpEthTlShutdown(void) {

    mutexDestroy(&gXcpTl.ctr_mutex);

    // Close all sockets to enable all threads to terminate
#ifdef XCPTL_ENABLE_MULTICAST
    socketClose(&gXcpTl.multicast_sock);
    join_thread(gXcpTl.multicast_thread_handle);
#endif
#ifdef XCPTL_ENABLE_TCP
    if (isTCP())
        socketClose(&gXcpTl.listen_socket);
#endif
    socketClose(&gXcpTl.socket);

#if defined(_WIN) // Windows
    CloseHandle(gXcpTl.queue_event);
#endif

#ifdef TEST_ENABLE_BUFFERCOUNT_HISTOGRAM
    printf("Buffer size histogram for vectored sends:\n");
    for (int i = 0; i < 256; i++) {
        if (gBufferCountHistogram[i] != 0xFFFFFFFF && gBufferCountHistogram[i] > 0) {
            printf(" %3u: %u\n", i, gBufferCountHistogram[i]);
        }
    }
#endif
}

//-------------------------------------------------------------------------------------------------------
void XcpEthTlGetInfo(bool *isTcp, uint8_t *mac, uint8_t *addr, uint16_t *port) {

    if (isTcp != NULL)
        *isTcp = gXcpTl.server_use_tcp;
#ifdef OPTION_ENABLE_GET_LOCAL_ADDR
    if (addr != NULL)
        memcpy(addr, gXcpTl.server_addr, 4);
    if (mac != NULL)
        memcpy(mac, gXcpTl.server_mac, 6);
#else
    if (addr != NULL) {
        addr[0] = 127;
        addr[1] = 0;
        addr[2] = 0;
        addr[3] = 1;
    }; // Use local host as default
    (void)mac;
#endif
    if (port != NULL)
        *port = gXcpTl.server_port;
}

//-------------------------------------------------------------------------------------------------------
// Generic transport layer functions

// Transmit completed and fully commited XCP DAQ and EVENT messages in the transmit queue as segments in UDP frames
// Uses vectored io to send multiple messages in one UDP frame, if possible
// Returns number of bytes sent or -1 on error
#if defined(OPTION_QUEUE_64_FIX_SIZE) || defined(OPTION_QUEUE_64_VAR_SIZE)

// MTU currently set to 8000 (jumbo frames) -> XCPTL_MAX_SEGMENT_SIZE = 7968
// Queue size currently typically at least 32KByte
#define MAX_BUFFERS 256          // Max number of buffers that can be accumulated into one segment
#define MIN_UPDATE_TIME_MS 50ULL // Update data at least every 50ms
#define MAX_QUEUE_LEVEL 50       // Transmit immediately, when the queue is more than 50% full and there is no more commited data
#define MAX_SLEEP_TIME_MS 1      // 1ms sleep time for retry, when there is was segment ready to send
#define MAX_RETRIES 100          // Return to the caller after MAX_RETRIES (100ms to allow background tasks and graceful shutdown)

// Collect queue buffers for one segment and transmit them
// Returns n = number of bytes sent or -1 on error
// Returns n = 0 after timeout, if there is nothing to send
// Returns after each segment sent
int32_t XcpTlHandleTransmitQueue(void) {

    uint32_t length = 0;                     // Number of bytes collected for transmission
    uint32_t index = 0;                      // Index for peeking into the queue
    uint32_t total_lost = 0;                 // Accumulated lost packet count across all peeks
    tQueueBuffer queue_buffers[MAX_BUFFERS]; // Buffer pointers for peeking into the queue, max segment size / min message size
    for (uint16_t retries = 0; retries < MAX_RETRIES;) {

        uint32_t lost = 0;
        bool flush = false;
        // DBG_PRINTF3("P %u\n", index);
        tQueueBuffer queue_buffer = queuePeek(gXcpTl.queue, index, &lost, &flush);
        total_lost += lost;
        uint16_t l = queue_buffer.size;

        // Queue does not have more committed data to peek or is empty
        if (l == 0) {

            // If there is commited data
            if (length > 0) {

                // If time since last transmit is longer than MIN_UPDATE_TIME_MS (50), break the loop and transmit any collected buffers
                // (to avoid too long delays when there is only little data in the queue)
                if ((clockGetMonotonicNs() - gXcpTl.last_transmit_time) > (MIN_UPDATE_TIME_MS * 1000000)) {
                    // DBG_PRINT3("T\n");
                    break; // Timeout
                }

                // If the queue is more than MAX_QUEUE_LEVEL full, break the loop and transmit collected buffers
                // (small queues may run full, without ever reaching the maximum segment size)
                // Don't call this too often, because it adds cache coherency traffic to sync the queue head between threads
                uint32_t max_level;
                uint32_t level = queueLevel(gXcpTl.queue, &max_level);
                if ((level * 100) / max_level > MAX_QUEUE_LEVEL) {
                    // DBG_PRINT3("L\n");
                    break; // Queue max level reached
                }
            }

            // Sleep some time and retry
            sleepMs(MAX_SLEEP_TIME_MS);
            retries++;

        } else {

            // Check if this buffer fits into the maximum XCP segment size, if not break the loop and transmit the collected buffers
            if (length + l > XCPTL_MAX_SEGMENT_SIZE) {
                // DBG_PRINT3("F\n");
                break; // Segment full, transmit collected buffers
            }

            // Store buffer for later vectored io transmission and release
            queue_buffers[index++] = queue_buffer;
            length += l;

            // Reached max number of buffers for one segment, break loop and transmit collected buffers
            if (index >= MAX_BUFFERS) {
                // DBG_PRINT3("B\n");
                break; // Buffers full, transmit collected buffers
            }
        }

        // Flush
        if (flush) {
            // DBG_PRINT3("F\n");
            break; // Flush requested by queue, transmit collected buffers
        }
    } // for(;retries<MAX_RETRIES;) peek loop

    // If there is nothing to send, return to the caller
    if (length == 0) {
        // DBG_PRINT3("XcpTlHandleTransmitQueue: Queue has no data, return\n");
        return 0; // Nothing to do, return to the caller, who can do other background tasks or shutdown the server gracefully
    }

    // Maintain consistency of the message counter, so all messages in this segment have increasing transport layer counter
    // Could be avoided by using the alternative transport layer counter mode of CANape, but this would required explicitly setting it in each new CANape project
    // The only tradeoff of this approach is increased latency of command responses, which is important for GET_DAQ_CLOCK responses
    mutexLock(&gXcpTl.ctr_mutex);

    // Account for any lost packets in the counter (must be inside the mutex to avoid race with XcpTlSendCrm)
    if (total_lost > 0) {
        gXcpTl.ctr += (uint16_t)total_lost;
        DBG_PRINTF_WARNING("Transmit queue overflow: lost %u packets, ctr=%u\n", total_lost, gXcpTl.ctr);
    }

    // Update the transport layer header (ctr+len) for all collected messages
    for (uint32_t i = 0; i < index; i++) {
        assert(queue_buffers[i].buffer != NULL);
        uint32_t l = queue_buffers[i].size - XCPTL_TRANSPORT_LAYER_HEADER_SIZE; // Get message payload size without transport layer header
        assert(l > 0);
        assert(l <= XCPTL_MAX_DTO_SIZE);
        assert(l % 4 == 0);
        uint8_t *b = queue_buffers[i].buffer;
#ifdef XCPTL_EXCLUDE_CRM_FROM_CTR // CANape option exclude command response
        uint16_t ctr = 0;
        if (b[4] == PID_ERR || b[4] == PID_RES) {
            ctr = 0;
        } else {
            assert(b[4] == PID_SERV || b[4] == PID_EV || b[5] == 0xAA);
            ctr = gXcpTl.ctr++;
        }
#else
        uint16_t ctr = gXcpTl.ctr++;
#endif
        *(uint32_t *)b = ((uint32_t)(ctr) << 16) | l; // Set transport layer counter for this segment
    }

    // Send the complete frame (blocking until sent)
    bool res = XcpEthTlSendV(queue_buffers, index);

    mutexUnlock(&gXcpTl.ctr_mutex);

    gXcpTl.last_transmit_time = clockGetMonotonicNs(); // Update last transmit time

    // Free all queue buffers
    for (uint32_t i = 0; i < index; i++) {
        queueRelease(gXcpTl.queue, &queue_buffers[i]);
    }

    if (res) {
        // DBG_PRINTF3("XcpTlHandleTransmitQueue: Segment transmitted, length=%u, ctr=(%u-%u)\n", length, ctr - index + 1, ctr);
        return (int32_t)length;
    } else {
        return -1; // error from send
    }
}

#else

int32_t XcpTlHandleTransmitQueue(void) {

    // Simply polling transmit queue
    // @@@@ TODO: Optimize efficiency, use a condvar or something like that to wakeup/sleep the transmit thread
    // @@@@ TODO: Eliminate the mutex
    // This is needed to assure XCP transport layer header counter consistency among response and DAQ packets
    // In fact this is a XCP design flaw, CANape supports independent DAQ and response packet counters, but other tools don't

    // Burst rate
    const uint32_t max_inner_loops = 1000; // Maximum number of ethernet packets to send in a burst without sleeping
#ifdef _WIN // Windows
    // Timeout to give the caller a chance to do other heath checking or shutdown the server gracefully
    const uint32_t max_outer_loops = 10; // Number of outer loops before return
    // Sleep time in ms after burst or queue empty
    const uint32_t outer_loop_sleep_ms = 10; // Sleep time in ms for each outer loop
#else       // Linux
    // Timeout to give the caller a chance to do other heath checking or shutdown the server gracefully
    const uint32_t max_outer_loops = 100; // Number of outer loops before return
    // Sleep time in ms after burst or queue empty
    const uint32_t outer_loop_sleep_ms = 1; // Sleep time in ms for each outer loop
#endif

// @@@@ TODO: This is too early, when the server is started before A2lInit !!!!!!!!!!!!!
#if defined(OPTION_DAQ_ASYNC_EVENT) && defined(XCP_ENABLE_DAQ_EVENT_LIST)
    static tXcpEventId gXcpAsyncEvent = XCP_UNDEFINED_EVENT_ID;
    if (gXcpAsyncEvent == XCP_UNDEFINED_EVENT_ID) {
        gXcpAsyncEvent = XcpCreateEvent("async", outer_loop_sleep_ms * CLOCK_TICKS_PER_MS, 0);
    }
#endif

    int32_t n = 0;      // Number of bytes sent
    bool flush = false; // Flush queue in regular intervals

    for (uint32_t j = 0; j < max_outer_loops; j++) {
        for (uint32_t i = 0; i < max_inner_loops; i++) {

            // Check if there is a segment with multiple messages in the transmit queue
            mutexLock(&gXcpTl.ctr_mutex);
            uint32_t lost = 0;
            tQueueBuffer queue_buffer = queuePop(gXcpTl.queue, true, flush, &lost);
            flush = false; // Reset flush flag
            if (lost > 0) {
                gXcpTl.ctr += (uint16_t)lost; // Increase packet counter by lost packets (must not be thread safe, used only to indicate error)
                DBG_PRINTF_WARNING("Transmit queue overflow: lost %u packets, ctr=%u\n", lost, gXcpTl.ctr);
            }
            uint16_t l = queue_buffer.size;
            const uint8_t *b = queue_buffer.buffer;
            if (l == 0) {
                mutexUnlock(&gXcpTl.ctr_mutex);
                break; // queue is empty, break inner loop and sleep a bit
            } else {
                // Send this frame (blocking)
                bool r = XcpEthTlSend(b, l, NULL, 0);
                mutexUnlock(&gXcpTl.ctr_mutex);

                // Free this buffer
                queueRelease(gXcpTl.queue, &queue_buffer);

                // Check result
                if (!r) { // error
                    return -1;
                }
                n += l;
            }

        } // for(i)

        // Flush queue every cycle
        if (n == 0 && j == max_outer_loops - 2) {
            flush = true;
        }

        sleepMs(outer_loop_sleep_ms);
#if defined(OPTION_DAQ_ASYNC_EVENT) && defined(XCP_ENABLE_DAQ_EVENT_LIST)
        XcpEvent(gXcpAsyncEvent);
#endif

    } // for(j)
    return n;
}

#endif

// Wait (sleep) until transmit queue is empty
// This function is thread safe, any thread can wait for transmit queue empty
// Timeout after timeout_ms milliseconds
#define TRANSMIT_QUEUE_EMPTY_SLEEP_MS 20 // Sleep time in ms for each loop while waiting for transmit queue empty
bool XcpTlWaitForTransmitQueueEmpty(uint16_t timeout_ms) {
    DBG_PRINTF5("XcpTlWaitForTransmitQueueEmpty: timeout=%u\n", timeout_ms);
    for (;;) {
        sleepMs(TRANSMIT_QUEUE_EMPTY_SLEEP_MS);
        if (timeout_ms < TRANSMIT_QUEUE_EMPTY_SLEEP_MS) { // Wait max timeout_ms until the transmit queue is empty
            DBG_PRINT5("XcpTlWaitForTransmitQueueEmpty: timeout reached\n");
            return false;
        };
        timeout_ms -= TRANSMIT_QUEUE_EMPTY_SLEEP_MS;
        uint32_t max_level;
        uint32_t level = queueLevel(gXcpTl.queue, &max_level);
        DBG_PRINTF6("XcpTlWaitForTransmitQueueEmpty: level=%u, max_level=%u\n", level, max_level);
        if (level == 0)
            break; // Transmit queue is empty
    }
    return true;
}

//-------------------------------------------------------------------------------------------------------

// Get the next transmit message counter
// For queue32.c
uint16_t XcpTlGetCtr(void) { return gXcpTl.ctr++; }
