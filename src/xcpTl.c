/*----------------------------------------------------------------------------
| File:
|   xcpTl.c
|
| Description:
|   XCP on UDP transport layer
|   Linux (Raspberry Pi) and Windows version
|   Supports Winsock and Linux Sockets
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "main_cfg.h"
#include "platform.h"
#include "util.h"

#include "xcptl_cfg.h"  // Transport layer configuration
#include "xcpTl.h"      // Transport layer interface
#include "xcpLite.h"    // Protocol layer interface
#include "xcpAppl.h"    // Dependecies to application code


// Parameter checks
#if XCPTL_TRANSPORT_LAYER_HEADER_SIZE != 4
#error "Transportlayer supports only 4 byte headers!"
#endif
#if ((XCPTL_MAX_CTO_SIZE&0x03) != 0)
#error "XCPTL_MAX_CTO_SIZE should be aligned to 4!"
#endif
#if ((XCPTL_MAX_DTO_SIZE&0x03) != 0)
#error "XCPTL_MAX_DTO_SIZE should be aligned to 4!"
#endif



#define XCPTL_ERROR_OK             0
#define XCPTL_ERROR_WOULD_BLOCK    1
#define XCPTL_ERROR_SEND_FAILED    2
#define XCPTL_ERROR_INVALID_MASTER 3

// Message types

typedef struct {
    uint16_t dlc;    // lenght
    uint16_t ctr;    // message counter
    uint8_t packet[1];  // message data
} tXcpMessage;

typedef struct {
    uint16_t dlc;
    uint16_t ctr;
    uint8_t packet[XCPTL_MAX_CTO_SIZE];
} tXcpCtoMessage;

typedef struct {
    uint16_t dlc;
    uint16_t ctr;
    uint8_t packet[XCPTL_MAX_DTO_SIZE];
} tXcpDtoMessage;


/* Transport Layer:
segment = message 1 + message 2 ... + message n
message = len + ctr + (protocol layer packet) + fill
*/
typedef struct {
    uint16_t uncommited;        // Number of uncommited messages in this segment
    uint16_t size;              // Number of overall bytes in this segment
    uint8_t msg[XCPTL_SEGMENT_SIZE];  // Segment/MTU - concatenated transport layer messages
} tXcpMessageBuffer;


static struct {

    SOCKET Sock;
#ifdef XCPTL_ENABLE_TCP
    SOCKET ListenSock;
#endif

    uint8_t ServerAddr[4];
    uint16_t ServerPort;

    uint8_t MasterAddr[4];
    uint16_t MasterPort;
    BOOL MasterAddrValid;

    int32_t lastError;

    // Transmit segment queue
    tXcpMessageBuffer queue[XCPTL_QUEUE_SIZE];
    uint32_t queue_rp; // rp = read index
    uint32_t queue_len; // rp+len = write index (the next free entry), len=0 ist empty, len=XCPTL_QUEUE_SIZE is full
    tXcpMessageBuffer* msg_ptr; // current incomplete or not fully commited segment
    uint64_t bytes_written;   // data bytes writen

    // CTO command transfer object counter
    uint16_t lastCroCtr; // Last CRO command receive object message message counter received

    // CRM,DTO message counter
    uint16_t ctr; // next DAQ DTO data transmit message packet counter

    // Multicast
#ifdef XCPTL_ENABLE_MULTICAST
    tXcpThread MulticastThreadHandle;
    SOCKET MulticastSock;
#endif

    MUTEX Mutex_Queue;
    
} gXcpTl;

#if defined XCPTL_ENABLE_TCP && defined XCPTL_ENABLE_UDP
#define isTCP() (gXcpTl.ListenSock != INVALID_SOCKET)
#define isUDP() (gXcpTl.ListenSock == INVALID_SOCKET)
#else
#ifdef XCPTL_ENABLE_TCP
#define isTCP() TRUE
#else
#define isTCP() FALSE
#endif
#ifdef XCPTL_ENABLE_UDP
#define isUDP() TRUE
#else
#define isUDP() FALSE
#endif
#endif


uint64_t XcpTlGetBytesWritten() {
    return gXcpTl.bytes_written;
}


#ifdef XCPTL_ENABLE_MULTICAST
static int handleXcpMulticast(int n, tXcpCtoMessage* p);
#endif



// Transmit a UDP datagramm or TCP segment (contains multiple XCP DTO messages or a single CRM message (len+ctr+packet+fill))
// Must be thread safe, because it is called from CMD and from DAQ thread
// Returns -1 on would block, 1 if ok, 0 on error
static int sendDatagram(const uint8_t *data, uint16_t size ) {

    int r;

#ifdef XCP_ENABLE_DEBUG_PRINTS
    if (XCP_DBG_LEVEL >= 5) {
        const tXcpMessage* m = (tXcpMessage *)data;
        XCP_DBG_PRINTF1("TX: message_size=%u, packet_size=%u ",size, m->dlc);
        if (size<=16) for (unsigned int i = 0; i < size; i++) XCP_DBG_PRINTF1("%0X ", data[i]);
        XCP_DBG_PRINT1("\n");
    }
#endif

#ifdef XCPTL_ENABLE_TCP
    if (isTCP()) {
        r = socketSend(gXcpTl.Sock, data, size);
    }
    else
#endif

#ifdef XCPTL_ENABLE_UDP
    {
        // Respond to active master
        if (!gXcpTl.MasterAddrValid) {
            XCP_DBG_PRINT_ERROR("ERROR: invalid master address!\n");
            gXcpTl.lastError = XCPTL_ERROR_INVALID_MASTER;
            return 0;
        }

        r = socketSendTo(gXcpTl.Sock, data, size, gXcpTl.MasterAddr, gXcpTl.MasterPort);
    }
#endif // UDP

    if (r != size) {
        if (socketGetLastError()==SOCKET_ERROR_WBLOCK) {
            gXcpTl.lastError = XCPTL_ERROR_WOULD_BLOCK;
            return -1; // Would block
        }
        else {
            XCP_DBG_PRINTF_ERROR("ERROR: sento failed (result=%d, errno=%d)!\n", r, socketGetLastError());
            gXcpTl.lastError = XCPTL_ERROR_SEND_FAILED;
            return 0; // Error
        }
    }

    return 1; // Ok
}


//------------------------------------------------------------------------------
// XCP (UDP or TCP) transport layer segment/message/packet queue (DTO buffers)

// Not thread save!
static void getSegmentBuffer() {

    tXcpMessageBuffer* b;

    /* Check if there is space in the queue */
    if (gXcpTl.queue_len >= XCPTL_QUEUE_SIZE) {
        /* Queue overflow */
        gXcpTl.msg_ptr = NULL;
    }
    else {
        unsigned int i = gXcpTl.queue_rp + gXcpTl.queue_len;
        if (i >= XCPTL_QUEUE_SIZE) i -= XCPTL_QUEUE_SIZE;
        b = &gXcpTl.queue[i];
        b->size = 0;
        b->uncommited = 0;
        gXcpTl.msg_ptr = b;
        gXcpTl.queue_len++;
    }
}

// Clear and init transmit queue
void XcpTlInitTransmitQueue() {

    mutexLock(&gXcpTl.Mutex_Queue);
    gXcpTl.queue_rp = 0;
    gXcpTl.queue_len = 0;
    gXcpTl.msg_ptr = NULL;
    gXcpTl.bytes_written = 0;
    getSegmentBuffer();
    mutexUnlock(&gXcpTl.Mutex_Queue);
    assert(gXcpTl.msg_ptr);
}

// Transmit all completed and fully commited UDP frames
// Returns -1 would block, 1 ok, 0 error
int XcpTlHandleTransmitQueue( void ) {

    tXcpMessageBuffer* b;

    for (;;) {

        // Check
        mutexLock(&gXcpTl.Mutex_Queue);
        if (gXcpTl.queue_len > 1) {
            b = &gXcpTl.queue[gXcpTl.queue_rp];
            if (b->uncommited > 0) b = NULL;
        }
        else {
            b = NULL;
        }
        mutexUnlock(&gXcpTl.Mutex_Queue);
        if (b == NULL) break;
        if (b->size == 0) continue; // This should not happen @@@@

        // Send this frame
        int r = sendDatagram(&b->msg[0], b->size);
        if (r == (-1)) return 1; // Ok, would block (-1)
        if (r == 0) return 0; // Nok, error (0)
        gXcpTl.bytes_written += b->size;

        // Free this buffer when succesfully sent
        mutexLock(&gXcpTl.Mutex_Queue);
        if (++gXcpTl.queue_rp >= XCPTL_QUEUE_SIZE) gXcpTl.queue_rp = 0;
        gXcpTl.queue_len--;
        mutexUnlock(&gXcpTl.Mutex_Queue);

    } // for (;;)

    return 1; // Ok, queue empty now
}

// Transmit all committed buffers in queue
void XcpTlFlushTransmitQueue() {

    // Complete the current buffer if non empty
    mutexLock(&gXcpTl.Mutex_Queue);
    if (gXcpTl.msg_ptr!=NULL && gXcpTl.msg_ptr->size>0) getSegmentBuffer();
    mutexUnlock(&gXcpTl.Mutex_Queue);

    XcpTlHandleTransmitQueue();
}

// Reserve space for a XCP packet in a transmit buffer and return a pointer to packet data and a handle for the segment buffer for commit reference
// Flush the transmit segment buffer, if no space left
uint8_t *XcpTlGetTransmitBuffer(void **handlep, uint16_t packet_size) {

    tXcpMessage* p;
    uint16_t msg_size;

 #if XCPTL_PACKET_ALIGNMENT==2
    packet_size = (uint16_t)((packet_size + 1) & 0xFFFE); // Add fill
#endif
#if XCPTL_PACKET_ALIGNMENT==4
    packet_size = (uint16_t)((packet_size + 3) & 0xFFFC); // Add fill
#endif
    msg_size = (uint16_t)(packet_size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE);

    mutexLock(&gXcpTl.Mutex_Queue);

    // Get another message buffer from queue, when active buffer ist full
    if (gXcpTl.msg_ptr==NULL || (uint16_t)(gXcpTl.msg_ptr->size + msg_size) >= XCPTL_SEGMENT_SIZE) {
        getSegmentBuffer();
    }

    if (gXcpTl.msg_ptr != NULL) {

        // Build XCP message header (ctr+dlc) and store in DTO buffer
        p = (tXcpMessage*)&gXcpTl.msg_ptr->msg[gXcpTl.msg_ptr->size];
        p->ctr = gXcpTl.ctr++;
        p->dlc = (uint16_t)packet_size;
        gXcpTl.msg_ptr->size = (uint16_t)(gXcpTl.msg_ptr->size + msg_size);
        *((tXcpMessageBuffer**)handlep) = gXcpTl.msg_ptr;
        gXcpTl.msg_ptr->uncommited++;

    }
    else {
        p = NULL; // Overflow
    }

    mutexUnlock(&gXcpTl.Mutex_Queue);

    if (p == NULL) return NULL; // Overflow
    return &p->packet[0]; // return pointer to XCP message DTO data
}

void XcpTlCommitTransmitBuffer(void *handle) {

    tXcpMessageBuffer* p = (tXcpMessageBuffer*)handle;

    if (handle != NULL) {
        mutexLock(&gXcpTl.Mutex_Queue);
        p->uncommited--;
        mutexUnlock(&gXcpTl.Mutex_Queue);
    }
}

void XcpTlFlushTransmitBuffer() {
    mutexLock(&gXcpTl.Mutex_Queue);
    if (gXcpTl.msg_ptr != NULL && gXcpTl.msg_ptr->size > 0) {
        getSegmentBuffer();
    }
    mutexUnlock(&gXcpTl.Mutex_Queue);
}

void XcpTlWaitForTransmitQueue() {

    XcpTlFlushTransmitBuffer();
    do {
        sleepMs(2);
    } while (gXcpTl.queue_len > 1) ;

}

//------------------------------------------------------------------------------

// Transmit XCP response or event packet
// No error handling in protocol layer
// If transmission fails, tool times out, retries or take appropriate action
// Note: CANape cancels measurement, when answer to GET_DAQ_CLOCK times out
void XcpTlSendCrm(const uint8_t* packet, uint16_t packet_size) {

#ifdef XCPTL_QUEUED_CRM

    void* handle = NULL;
    uint8_t* p;
    int r = 0;

    // If transmit queue is empty, save the space and transmit instantly
    mutexLock(&gXcpTl.Mutex_Queue);
    if (gXcpTl.queue_len <= 1 && (gXcpTl.msg_ptr == NULL || gXcpTl.msg_ptr->size == 0)) {

        // Send the response
        // Build XCP CTO message (ctr+dlc+packet)
        tXcpCtoMessage msg;
        uint16_t msg_size;
        msg.ctr = gXcpTl.ctr++;
        memcpy(msg.packet, packet, packet_size);
        msg_size = packet_size;
#if (XCPTL_PACKET_ALIGNMENT==2)
        msg_size = (uint16_t)((msg_size + 1) & 0xFFFE); // Add fill
#endif
#if (XCPTL_PACKET_ALIGNMENT==4)
        msg_size = (uint16_t)((msg_size + 3) & 0xFFFC); // Add fill
#endif
        msg.dlc = (uint16_t)msg_size;
        msg_size = (uint16_t)(msg_size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE);
        r = sendDatagram((uint8_t*)&msg, msg_size);
    }
    mutexUnlock(&gXcpTl.Mutex_Queue);
    if (r == 1) return; // ok

    // Queue the response packet
    if ((p = XcpTlGetTransmitBuffer(&handle, packet_size)) != NULL) {
        memcpy(p, packet, packet_size);
        XcpTlCommitTransmitBuffer(handle);
        XcpTlFlushTransmitBuffer();
    }
    else { // Buffer overflow
        // @@@@ Todo
    }

#else

    int r;

    // Build XCP CTO message (ctr+dlc+packet)
    tXcpCtoMessage p;
    p.ctr = gXcpTl.lastCroCtr++;
    p.dlc = (uint16_t)packet_size;
    memcpy(p.packet, packet, packet_size);
    r = sendDatagram((unsigned char*)&p, (uint16_t)(packet_size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE));
    if (r==(-1)) { // Would block
        // @@@@ Todo
    }

#endif
}


static int handleXcpCommand(tXcpCtoMessage *p, uint8_t *srcAddr, uint16_t srcPort) {

    int connected;

    // gXcpTl.LastCrmCtr = p->ctr;
    connected = XcpIsConnected();

#ifdef XCP_ENABLE_DEBUG_PRINTS
    if (XCP_DBG_LEVEL >= 5 || (!connected && XCP_DBG_LEVEL >= 3)) {
        XCP_DBG_PRINTF1("RX: CTR %04X LEN %04X DATA = ", p->ctr,p->dlc);
        for (int i = 0; i < p->dlc; i++) XCP_DBG_PRINTF1("%0X ", p->packet[i]);
        XCP_DBG_PRINT1("\n");
    }
#endif

    /* Connected */
    if (connected) {

#ifdef XCPTL_ENABLE_UDP
        if (isUDP() && gXcpTl.MasterAddrValid) {

            // Check unicast ip address, not allowed to change
            if (memcmp(&gXcpTl.MasterAddr, srcAddr, sizeof(gXcpTl.MasterAddr)) != 0) { // Message from different master received
                XCP_DBG_PRINTF1("WARNING: message from unknown new master %u.%u.%u.%u, disconnecting!\n", srcAddr[0], srcAddr[1], srcAddr[2], srcAddr[3]);
                XcpDisconnect();
                gXcpTl.MasterAddrValid = FALSE;
                return 1; // Disconnect
            }

            // Check unicast master udp port, not allowed to change
            if (gXcpTl.MasterPort != srcPort) {
                XCP_DBG_PRINTF1("WARNING: master port changed from %u to %u, disconnecting!\n", gXcpTl.MasterPort, srcPort);
                XcpDisconnect();
                gXcpTl.MasterAddrValid = FALSE;
                return 1; // Disconnect
            }
        }
#endif // UDP

        XcpCommand((const uint32_t*)&p->packet[0], p->dlc); // Handle command
    }

    /* Not connected yet */
    else {
        /* Check for CONNECT command ? */
        if (p->dlc == 2 && p->packet[0] == CC_CONNECT) {
#ifdef XCPTL_ENABLE_UDP
            if (isUDP()) {
                memcpy(gXcpTl.MasterAddr, srcAddr, sizeof(gXcpTl.MasterAddr)); // Save master address, so XcpCommand can send the CONNECT response
                gXcpTl.MasterPort = srcPort;
                gXcpTl.MasterAddrValid = TRUE;
            }
#endif // UDP
            XcpTlInitTransmitQueue();
            XcpCommand((const uint32_t*)&p->packet[0],p->dlc); // Handle CONNECT command
        }
        else {
            XCP_DBG_PRINT1("WARNING: no valid CONNECT command\n");
        }

    }

#ifdef XCPTL_ENABLE_UDP
    if (isUDP() && !connected) { // not connected before
        if (XcpIsConnected()) {
            XCP_DBG_PRINTF1("XCP master connected on UDP addr=%u.%u.%u.%u, port=%u\n", gXcpTl.MasterAddr[0], gXcpTl.MasterAddr[1], gXcpTl.MasterAddr[2], gXcpTl.MasterAddr[3], gXcpTl.MasterPort);
        }
        else { // Is not in connected state
            gXcpTl.MasterAddrValid = FALSE; // Any client can connect
        }
    } // not connected before
#endif // UDP

    return 1; // Ok
}


// Handle incoming XCP commands
// returns 0 on error
BOOL XcpTlHandleCommands() {

    tXcpCtoMessage msgBuf;
    int16_t n;

#ifdef XCPTL_ENABLE_TCP
    if (isTCP()) {

        // Listen to incoming TCP connection if not connected
        if (gXcpTl.Sock == INVALID_SOCKET) {
            XCP_DBG_PRINT3("CDM thread waiting for TCP connection ...\n");
            gXcpTl.Sock = socketAccept(gXcpTl.ListenSock, gXcpTl.MasterAddr); // Wait here for incoming connection
            if (gXcpTl.Sock == INVALID_SOCKET) {
                XCP_DBG_PRINT_ERROR("ERROR: accept failed!\n");
                return TRUE; // Ignore error from accept
            }
            else {
                XCP_DBG_PRINTF1("Master %u.%u.%u.%u accepted!\n", gXcpTl.MasterAddr[0], gXcpTl.MasterAddr[1], gXcpTl.MasterAddr[2], gXcpTl.MasterAddr[3]);
                XCP_DBG_PRINT3("Listening for XCP commands\n");
            }
        }

        // Receive TCP transport layer message
        n = socketRecv(gXcpTl.Sock, (uint8_t*)&msgBuf, (uint16_t)XCPTL_TRANSPORT_LAYER_HEADER_SIZE, TRUE); // header, recv blocking
        if (n == XCPTL_TRANSPORT_LAYER_HEADER_SIZE) {
            n = socketRecv(gXcpTl.Sock, (uint8_t*)&msgBuf.packet, msgBuf.dlc, TRUE); // packet, recv blocking
            if (n > 0) {
                if (n == msgBuf.dlc) {
                    return handleXcpCommand(&msgBuf, NULL, 0);
                }
                else {
                    socketShutdown(gXcpTl.Sock);
                    return FALSE;  // Should not happen
                }
            }
        }
        if (n==0) {  // Socket closed
            XCP_DBG_PRINT1("Master closed TCP connection! XCP disconnected.\n");
            XcpDisconnect();
            sleepMs(100);
            socketShutdown(gXcpTl.Sock);
            socketClose(&gXcpTl.Sock);
            return TRUE; // Ok, TCP socket closed
        }
    }
#endif // TCP

#ifdef XCPTL_ENABLE_UDP
    if (isUDP()) {
        // Wait for a UDP datagramm
        uint16_t srcPort;
        uint8_t srcAddr[4];
        n = socketRecvFrom(gXcpTl.Sock, (uint8_t*)&msgBuf, (uint16_t)sizeof(msgBuf), srcAddr, &srcPort); // recv blocking
        if (n == 0) return TRUE; // Socket closed, should not happen
        if (n < 0) {  // error
            if (socketGetLastError() == SOCKET_ERROR_WBLOCK) return 1; // Ok, timeout, no command pending
            XCP_DBG_PRINTF_ERROR("ERROR %u: recvfrom failed (result=%d)!\n", socketGetLastError(), n);
            return FALSE; // Error
        }
        else { // Ok
            if (msgBuf.dlc != n - XCPTL_TRANSPORT_LAYER_HEADER_SIZE) {
              XCP_DBG_PRINT_ERROR("ERROR: corrupt message received!\n");
              return FALSE; // Error
            }
            return handleXcpCommand(&msgBuf, srcAddr, srcPort);
        }
    }
#endif // UDP

    return FALSE;
}


//-------------------------------------------------------------------------------------------------------
// XCP Multicast

#ifdef XCPTL_ENABLE_MULTICAST

static int handleXcpMulticast(int n, tXcpCtoMessage* p) {

    // Valid socket data received, at least transport layer header and 1 byte
    if (XcpIsConnected() && n >= XCPTL_TRANSPORT_LAYER_HEADER_SIZE + 1) {
        XcpCommand((const uint32_t*)&p->packet[0],p->dlc); // Handle command
    }
    return 1; // Ok
}

#ifdef _WIN
DWORD WINAPI XcpTlMulticastThread(LPVOID par)
#else
extern void* XcpTlMulticastThread(void* par)
#endif
{
    uint8_t buffer[256];
    int16_t n;
    (void)par;
    for (;;) {
        n = socketRecvFrom(gXcpTl.MulticastSock, buffer, (uint16_t)sizeof(buffer), NULL, NULL);
        if (n <= 0) break; // Terminate on error or socket close 
        handleXcpMulticast(n, (tXcpCtoMessage*)buffer);
    }
    XCP_DBG_PRINT1("Terminate XCP multicast thread\n");
    socketClose(&gXcpTl.MulticastSock);
    return 0;
}

#endif

void XcpTlSetClusterId(uint16_t clusterId) {
  (void)clusterId;
}

int XcpTlInit(const uint8_t* addr, uint16_t port, BOOL useTCP) {

    XCP_DBG_PRINTF2("\nInit XCP on %s transport layer\n", useTCP ? "TCP" : "UDP");
    XCP_DBG_PRINTF2("  MTU=%u, QUEUE_SIZE=%u, %uKiB memory used\n", XCPTL_SEGMENT_SIZE, XCPTL_QUEUE_SIZE, (unsigned int)sizeof(gXcpTl) / 1024);

    if (addr != 0)  { // Bind to given addr 
        memcpy(gXcpTl.ServerAddr, addr, 4);
    } else { // Bind to ANY(0.0.0.0)
        memset(gXcpTl.ServerAddr, 0, 4);
    }

    gXcpTl.ServerPort = port;
    gXcpTl.lastCroCtr = 0;
    gXcpTl.ctr = 0;
    gXcpTl.MasterAddrValid = FALSE;
    gXcpTl.Sock = INVALID_SOCKET;

    mutexInit(&gXcpTl.Mutex_Queue, 0, 1000);
    XcpTlInitTransmitQueue();

#ifdef XCPTL_ENABLE_TCP
    gXcpTl.ListenSock = INVALID_SOCKET;
    if (useTCP) { // TCP
        if (!socketOpen(&gXcpTl.ListenSock, 1 /* useTCP */, 0 /*nonblocking*/, 1 /*reuseAddr*/)) return 0;
        if (!socketBind(gXcpTl.ListenSock, gXcpTl.ServerAddr, gXcpTl.ServerPort)) return 0; // Bind on ANY, when serverAddr=255.255.255.255
        if (!socketListen(gXcpTl.ListenSock)) return 0; // Put socket in listen mode
        XCP_DBG_PRINTF1("  Listening for TCP connections on %u.%u.%u.%u port %u\n", gXcpTl.ServerAddr[0], gXcpTl.ServerAddr[1], gXcpTl.ServerAddr[2], gXcpTl.ServerAddr[3], gXcpTl.ServerPort);
    }
    else
#else
    if (useTCP) { // TCP
        XCP_DBG_PRINT_ERROR("ERROR: #define XCPTL_ENABLE_TCP for support\n");
        return FALSE;
    }
    else
#endif
    { // UDP
        if (!socketOpen(&gXcpTl.Sock, 0 /* useTCP */, 0 /*nonblocking*/, 1 /*reuseAddr*/)) return 0;
        if (!socketBind(gXcpTl.Sock, gXcpTl.ServerAddr, gXcpTl.ServerPort)) return 0; // Bind on ANY, when serverAddr=255.255.255.255
        XCP_DBG_PRINTF1("  Listening for XCP commands on UDP %u.%u.%u.%u port %u\n", gXcpTl.ServerAddr[0], gXcpTl.ServerAddr[1], gXcpTl.ServerAddr[2], gXcpTl.ServerAddr[3], gXcpTl.ServerPort);
    }

    // Multicast UDP commands
#ifdef XCPTL_ENABLE_MULTICAST

    if (!socketOpen(&gXcpTl.MulticastSock, 0 /*useTCP*/, 0 /*nonblocking*/, 1 /*reusable*/)) return 0;
    XCP_DBG_PRINTF2("  Bind XCP multicast socket to %u.%u.%u.%u\n", gXcpTl.ServerAddr[0], gXcpTl.ServerAddr[1], gXcpTl.ServerAddr[2], gXcpTl.ServerAddr[3]);
    if (!socketBind(gXcpTl.MulticastSock, gXcpTl.ServerAddr, XCPTL_MULTICAST_PORT)) return 0; // Bind to ANY, when serverAddr=255.255.255.255

    uint16_t cid = XcpGetClusterId();
    uint8_t maddr[4] = { 239,255,0,0 }; // 0xEFFFiiii
    maddr[2] = (uint8_t)(cid >> 8);
    maddr[3] = (uint8_t)(cid);
    if (!socketJoin(gXcpTl.MulticastSock, maddr)) return 0;
    XCP_DBG_PRINTF3("  Listening for XCP multicast from %u.%u.%u.%u:%u\n", maddr[0], maddr[1], maddr[2], maddr[3], XCPTL_MULTICAST_PORT);

    XCP_DBG_PRINT3("  Start XCP multicast thread\n");
    create_thread(&gXcpTl.MulticastThreadHandle, XcpTlMulticastThread);
#endif
    return 1;
}


void XcpTlShutdown() {

#ifdef XCPTL_ENABLE_MULTICAST
    socketClose(&gXcpTl.MulticastSock);
    sleepMs(500);
    cancel_thread(gXcpTl.MulticastThreadHandle);
#endif
    mutexDestroy(&gXcpTl.Mutex_Queue);
#ifdef XCPTL_ENABLE_TCP
    if (isTCP()) socketClose(&gXcpTl.ListenSock);
#endif
    socketClose(&gXcpTl.Sock);
}



// Wait for outgoing data or timeout after timeout_us
void XcpTlWaitForTransmitData(uint32_t timeout_ms) {

    if (gXcpTl.queue_len <= 1) {
        sleepMs(timeout_ms);
    }
    return;
}



//-------------------------------------------------------------------------------------------------------

int XcpTlGetLastError() {
    return gXcpTl.lastError;
}


// Info used to generate A2L
const char* XcpTlGetServerAddrString() {

    static char tmp[32];
    uint8_t addr[4],*a;
    if (gXcpTl.ServerAddr[0] != 0) {
        a = gXcpTl.ServerAddr;
    }
    else if (socketGetLocalAddr(NULL, addr)) { // If server bound to ANY, determine addr of the ethernet adapter
        a = addr;
    }
    else {
        return "127.0.0.1";
    }
    SPRINTF(tmp, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    return tmp;
}

// Info used to generate A2L
uint16_t XcpTlGetServerPort() {

    return gXcpTl.ServerPort;
}

// Info used to generate clock UUID
int XcpTlGetServerMAC(uint8_t *mac) {

    return socketGetLocalAddr(mac,NULL);
}

