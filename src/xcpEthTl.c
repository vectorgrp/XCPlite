/*----------------------------------------------------------------------------
| File:
|   xcpEthTl.c
|
| Description:
|   XCP on UDP transport layer
|   Linux and Windows version
|   Supports Winsock and Linux Sockets
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "platform.h"
#include "dbg_print.h"
#include "xcpLite.h"   

// Parameter checks
#if XCPTL_TRANSPORT_LAYER_HEADER_SIZE != 4
#error "Transportlayer supports only 4 byte headers!"
#endif
#if ((XCPTL_MAX_CTO_SIZE&0x07) != 0)
#error "XCPTL_MAX_CTO_SIZE should be aligned to 8!"
#endif
#if ((XCPTL_MAX_DTO_SIZE&0x03) != 0)
#error "XCPTL_MAX_DTO_SIZE should be aligned to 4!"
#endif


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

/* Transport Layer:
segment = message 1 + message 2 ... + message n
message = len + ctr + (protocol layer packet) + fill
*/
typedef struct {
    uint16_t uncommited;        // Number of uncommited messages in this segment
    uint16_t size;              // Number of overall bytes in this segment
    uint8_t msg[XCPTL_MAX_SEGMENT_SIZE];  // Segment (UDP MTU) - concatenated transport layer messages
} tXcpMessageBuffer;


static struct {

    // Generic transport layer 
    int32_t lastError;
    uint16_t lastCroCtr; // Last CRO command receive object message message counter received
    uint16_t ctr; // next DAQ DTO data transmit message packet counter

    // Transmit segment queue
    tXcpMessageBuffer queue[XCPTL_QUEUE_SIZE];
    uint32_t queue_rp; // rp = read index
    uint32_t queue_len; // rp+len = write index (the next free entry), len=0 ist empty, len=XCPTL_QUEUE_SIZE is full
#if defined(_WIN) // Windows
    HANDLE queue_event;
    uint64_t queue_event_time;
#endif
    tXcpMessageBuffer* msg_ptr; // current incomplete or not fully commited segment
    MUTEX Mutex_Queue;
    
#if defined(XCPTL_ENABLE_UDP) || defined(XCPTL_ENABLE_TCP)

    // Ethernet
    SOCKET Sock;
    #ifdef XCPTL_ENABLE_TCP
        SOCKET ListenSock;
    #endif
    #ifdef PLATFORM_ENABLE_GET_LOCAL_ADDR
        uint8_t ServerMac[6];
    #endif
    uint8_t ServerAddr[4];
    uint16_t ServerPort;
    BOOL ServerUseTCP;
    BOOL blockingRx;
    uint8_t MasterAddr[4];
    uint16_t MasterPort;
    BOOL MasterAddrValid;

    // Multicast
    #ifdef XCPTL_ENABLE_MULTICAST
        tXcpThread MulticastThreadHandle;
        SOCKET MulticastSock;
    #endif

#endif

} gXcpTl;


#if defined(XCPTL_ENABLE_TCP) && defined(XCPTL_ENABLE_UDP)
#define isTCP() (gXcpTl.ListenSock != INVALID_SOCKET)
#else
#ifdef XCPTL_ENABLE_TCP
#define isTCP() TRUE
#else
#define isTCP() FALSE
#endif
#endif


#ifdef XCPTL_ENABLE_MULTICAST
static int handleXcpMulticastCommand(int n, tXcpCtoMessage* p, uint8_t* dstAddr, uint16_t dstPort);
#endif



//-------------------------------------------------------------------------------------------------------

static void XcpTlInitTransmitQueue();


//-------------------------------------------------------------------------------------------------------
// Generic transport layer functions

BOOL XcpTlInit() {

    gXcpTl.lastCroCtr = 0;
    gXcpTl.ctr = 0;
    mutexInit(&gXcpTl.Mutex_Queue, FALSE, 1000);
    XcpTlInitTransmitQueue();
#if defined(_WIN) // Windows
    gXcpTl.queue_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    assert(gXcpTl.queue_event!=NULL); 
    gXcpTl.queue_event_time = 0;
#endif
    
    DBG_PRINT3("\nInit XCP transport layer\n");
    DBG_PRINTF3("  SEGMENT_SIZE=%u, MAX_CTO_SIZE=%u, QUEUE_SIZE=%u, ALIGNMENT=%u, %uKiB memory used\n", XCPTL_MAX_SEGMENT_SIZE, XCPTL_MAX_CTO_SIZE, XCPTL_QUEUE_SIZE, XCPTL_PACKET_ALIGNMENT, (unsigned int)sizeof(gXcpTl) / 1024);
    DBG_PRINT3("  Note: These parameters in xcptl_cfg.h need to be configured for optimal memory consumption and performance!\n");
#ifdef XCPTL_ENABLE_MULTICAST
    DBG_PRINT3("        Option ENABLE_MULTICAST is not recommended\n");
#endif
#ifndef XCPTL_QUEUED_CRM
    DBG_PRINT3("        Option QUEUED_CRM is disabled, enabled is recommended\n");
#endif
   
    
    return TRUE;
}

void XcpTlShutdown() {

    mutexDestroy(&gXcpTl.Mutex_Queue);
#if defined(_WIN) // Windows
    CloseHandle(gXcpTl.queue_event);
#endif
}


#if defined(XCPTL_QUEUED_CRM) && !defined(XCPTL_QUEUED_CRM_OPT)

// Queue a response or event packet
// Must be thread save, if XcpPrint used, XcpCommand is never called from multiple threads
// If transmission fails, when queue is full, tool times out, retries or take appropriate action
// Note: CANape cancels measurement, when answer to GET_DAQ_CLOCK times out
void XcpTlSendCrm(const uint8_t* packet, uint16_t packet_size) {

    void* handle = NULL;
    uint8_t* p;

    // Queue the response packet
    if ((p = XcpTlGetTransmitBuffer(&handle, packet_size)) != NULL) {
        memcpy(p, packet, packet_size);
        XcpTlCommitTransmitBuffer(handle, TRUE /* flush */);
    }
    else { // Buffer overflow
        DBG_PRINT_WARNING("WARNING: queue overflow\n");
        // Ignore, handled by tool
    }
}

#endif

// Execute XCP command
// Returns XCP error code
uint8_t XcpTlCommand( uint16_t msgLen, const uint8_t* msgBuf) {

    BOOL connected = XcpIsConnected();
    tXcpCtoMessage* p = (tXcpCtoMessage*)msgBuf;
    assert(msgLen>=p->dlc+XCPTL_TRANSPORT_LAYER_HEADER_SIZE);

    /* Connected */
    if (connected) {
        return XcpCommand((const uint32_t*)&p->packet[0], p->dlc); // Handle command
    }

    /* Not connected yet */
    else {
        /* Check for CONNECT command ? */
        if (p->dlc == 2 && p->packet[0] == CC_CONNECT) {
            XcpTlInitTransmitQueue();
            return XcpCommand((const uint32_t*)&p->packet[0],p->dlc); // Handle CONNECT command
        }
        else {
            DBG_PRINTF_WARNING("WARNING: XcpTlCommand: no valid CONNECT command, dlc=%u, data=%02X\n", p->dlc, p->packet[0]);
            return CRC_CMD_SYNTAX;
        }

    }
}


//-------------------------------------------------------------------------------------------------------
// XCP (UDP or TCP) transport layer segment/message/packet queue (DTO buffers)

// Notify transmit queue handler thread
// Not thread save!
static BOOL notifyTransmitQueueHandler() {

    // Windows only, Linux version uses polling
#if defined(_WIN) // Windows
    // Notify when there is finalized buffer in the queue
    // Notify at most every XCPTL_QUEUE_TRANSMIT_CYCLE_TIME to save CPU load
    uint64_t clock = clockGetLast();
    if (clock== gXcpTl.queue_event_time) clock = clockGet();
    if (gXcpTl.queue_len == 2 || (gXcpTl.queue_len > 2 && clock >= gXcpTl.queue_event_time + XCPTL_QUEUE_TRANSMIT_CYCLE_TIME)) {
      gXcpTl.queue_event_time = clock;
      SetEvent(gXcpTl.queue_event);
      return TRUE;
    }
#endif
    return FALSE;
}

// Allocate a new transmit buffer (transmit queue entry)
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

    notifyTransmitQueueHandler();
}

// Clear and init transmit queue
static void XcpTlInitTransmitQueue() {

    mutexLock(&gXcpTl.Mutex_Queue);
    gXcpTl.queue_rp = 0;
    gXcpTl.queue_len = 0;
    gXcpTl.msg_ptr = NULL;
    getSegmentBuffer();
    mutexUnlock(&gXcpTl.Mutex_Queue);
    assert(gXcpTl.msg_ptr);
}

// Get transmit queue level
int32_t XcpTlGetTransmitQueueLevel() {
  return gXcpTl.queue_len; 
}



// Reserve space for a XCP packet in a transmit segment buffer and return a pointer to packet data and a handle for the segment buffer for commit reference
// Flush the transmit segment buffer, if no space left
uint8_t *XcpTlGetTransmitBuffer(void **handlep, uint16_t packet_size) {

    tXcpMessage* p;
    uint16_t msg_size;

 #if XCPTL_PACKET_ALIGNMENT==2
    packet_size = (uint16_t)((packet_size + 1) & 0xFFFE); // Add fill %2
#endif
#if XCPTL_PACKET_ALIGNMENT==4
    packet_size = (uint16_t)((packet_size + 3) & 0xFFFC); // Add fill %4
#endif
    msg_size = (uint16_t)(packet_size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE);
    if (msg_size > XCPTL_MAX_SEGMENT_SIZE) {
        return NULL; // Overflow, should never happen in correct DAQ setups
    }

    mutexLock(&gXcpTl.Mutex_Queue);

    // Get another message buffer from queue, when active buffer ist full
    if (gXcpTl.msg_ptr==NULL || (uint16_t)(gXcpTl.msg_ptr->size + msg_size) > XCPTL_MAX_SEGMENT_SIZE) {
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

void XcpTlCommitTransmitBuffer(void *handle, BOOL flush) {

    tXcpMessageBuffer* p = (tXcpMessageBuffer*)handle;
    if (handle != NULL) {
        mutexLock(&gXcpTl.Mutex_Queue);
        p->uncommited--;

        // Flush (high priority data commited)
        if (flush && gXcpTl.msg_ptr != NULL && gXcpTl.msg_ptr->size > 0) {
#if defined(_WIN) // Windows
            gXcpTl.queue_event_time = 0;
#endif
            getSegmentBuffer();
        }
        
        mutexUnlock(&gXcpTl.Mutex_Queue);
    }
}

// Flush the current transmit segment buffer, used on high prio event data
void XcpTlFlushTransmitBuffer() {

    // Complete the current buffer if non empty
    mutexLock(&gXcpTl.Mutex_Queue);
    if (gXcpTl.msg_ptr != NULL && gXcpTl.msg_ptr->size > 0) getSegmentBuffer();
    mutexUnlock(&gXcpTl.Mutex_Queue);
}

// Wait until transmit segment queue is empty, used when measurement is stopped  
void XcpTlWaitForTransmitQueueEmpty() {

    uint16_t timeout = 0;
    XcpTlFlushTransmitBuffer(); // Flush the current segment buffer
    do {
        sleepMs(20);
        timeout++;
    } while (gXcpTl.queue_len > 1 && timeout<=50); // Wait max 1s until the transmit queue is empty
}


// Check if there is a fully commited segment buffer in the transmit queue
const uint8_t * XcpTlTransmitQueuePeek( uint16_t* msg_len) {

    tXcpMessageBuffer* b = NULL;
    
    // Check there is a commited entry
    mutexLock(&gXcpTl.Mutex_Queue);
    if (gXcpTl.queue_len > 1) {
        b = &gXcpTl.queue[gXcpTl.queue_rp];
        if (b->uncommited > 0) b = NULL; // return when reaching a not fully commited segment buffer 
    }
    else {
        b = NULL;
    }
    mutexUnlock(&gXcpTl.Mutex_Queue);
    if (b == NULL) return NULL; // Qqueue is empty or not fully commited
    assert(b->size!=0); 
    *msg_len = b->size;
    return &b->msg[0]; 
}

// Remove the next transmit queue entry
void XcpTlTransmitQueueNext() {
  
    mutexLock(&gXcpTl.Mutex_Queue);
    if (gXcpTl.queue_len > 1 && gXcpTl.queue[gXcpTl.queue_rp].uncommited == 0) {
        if (++gXcpTl.queue_rp >= XCPTL_QUEUE_SIZE) gXcpTl.queue_rp = 0;
        gXcpTl.queue_len--;
    }
    mutexUnlock(&gXcpTl.Mutex_Queue);
}



//------------------------------------------------------------------------------
// Ethernet transport layer socket functions

#if defined(XCPTL_ENABLE_UDP) || defined(XCPTL_ENABLE_TCP)

// Transmit a UDP datagramm or TCP segment (contains multiple XCP DTO messages or a single CRM message (len+ctr+packet+fill))
// Must be thread safe, because it is called from CMD and from DAQ thread
// Returns -1 on would block, 1 if ok, 0 on error
static int sendEthDatagram(const uint8_t *data, uint16_t size, const uint8_t* addr, uint16_t port) {

    int r;

#ifdef XCPTL_ENABLE_TCP
    if (isTCP()) {
      r = socketSend(gXcpTl.Sock, data, size);
    }
    else
#endif

#ifdef XCPTL_ENABLE_UDP
    {
      if (addr != NULL) { // Respond to given addr and port (used for multicast)
        r = socketSendTo(gXcpTl.Sock, data, size, addr, port, NULL);
      }
      else { // Respond to active master
        if (!gXcpTl.MasterAddrValid) {
          DBG_PRINT_ERROR("ERROR: invalid master address!\n");
          gXcpTl.lastError = XCPTL_ERROR_INVALID_MASTER;
          return 0;
        }
        r = socketSendTo(gXcpTl.Sock, data, size, gXcpTl.MasterAddr, gXcpTl.MasterPort, NULL);
      }
    }
#endif // UDP

    if (r != size) {
        if (socketGetLastError()==SOCKET_ERROR_WBLOCK) {
            gXcpTl.lastError = XCPTL_ERROR_WOULD_BLOCK;
            return -1; // Would block
        }
        else {
            DBG_PRINTF_ERROR("ERROR: sendEthDatagram: send failed (result=%d, errno=%d)!\n", r, socketGetLastError());
            gXcpTl.lastError = XCPTL_ERROR_SEND_FAILED;
            return 0; // Error
        }
    }

    return 1; // Ok
}


// Transmit all completed and fully commited UDP frames
// Returns number of bytes sent or -1 on error
int32_t XcpTlHandleTransmitQueue() {

    const uint32_t max_loops = 20; // maximum number of packets to send without sleep(0) 

    tXcpMessageBuffer* b = NULL;
    int32_t n = 0;

    for (;;) {
      for (uint32_t i = 0; i < max_loops; i++) {

        // Check
        mutexLock(&gXcpTl.Mutex_Queue);
        if (gXcpTl.queue_len > 1) {
          b = &gXcpTl.queue[gXcpTl.queue_rp];
          if (b->uncommited > 0) b = NULL; // return when reaching a not fully commited segment buffer 
        }
        else {
          b = NULL;
        }
        mutexUnlock(&gXcpTl.Mutex_Queue);
        if (b == NULL) break; // Ok, queue is empty or not fully commited
        assert(b->size!=0); 

        // Send this frame
        int r = sendEthDatagram(&b->msg[0], b->size, NULL, 0);
        
        if (r == (-1)) { // would block
          b = NULL;
          break; 
        }
        if (r == 0) { // error
          return -1; 
        }
        n += b->size;

        // Free this buffer when succesfully sent
        mutexLock(&gXcpTl.Mutex_Queue);
        if (++gXcpTl.queue_rp >= XCPTL_QUEUE_SIZE) gXcpTl.queue_rp = 0;
        gXcpTl.queue_len--;
        mutexUnlock(&gXcpTl.Mutex_Queue);

      } // for (max_loops)

      if (b == NULL) break; // queue is empty
      sleepMs(0);

    } // for (ever)

    return n; // Ok, queue empty now
}


#if !defined(XCPTL_QUEUED_CRM) || defined(XCPTL_QUEUED_CRM_OPT)

// Transmit XCP response or event packet
// No error handling in protocol layer
// Must be thread save, if XcpPrint used, XcpCommand is never called from multiple threads
// If transmission fails, tool times out, retries or take appropriate action
// Note: CANape cancels measurement, when answer to GET_DAQ_CLOCK times out
void XcpTlSendCrm(const uint8_t* packet, uint16_t packet_size) {

#ifdef XCPTL_QUEUED_CRM

    void* handle = NULL;
    uint8_t* p;
    int r = 0;

#ifdef XCPTL_QUEUED_CRM_OPT
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
        msg.dlc = (uint16_t)msg_size;
        msg_size = (uint16_t)(msg_size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE);
        r = sendEthDatagram((uint8_t*)&msg.dlc, msg_size, NULL, 0);
    }
    mutexUnlock(&gXcpTl.Mutex_Queue);
    if (r == 1) return; // ok
#endif // XCPTL_QUEUED_CRM_OPT

    // Queue the response packet
    if ((p = XcpTlGetTransmitBuffer(&handle, packet_size)) != NULL) {
        memcpy(p, packet, packet_size);
        XcpTlCommitTransmitBuffer(handle, TRUE /* flush */);
    }
    else { // Buffer overflow
        // Ignore, handled by tool
    }

#else

    int r;

    // Build XCP CTO message (ctr+dlc+packet)
    tXcpCtoMessage p;
    p.dlc = (uint16_t)packet_size;
    p.ctr = gXcpTl.lastCroCtr++;
    memcpy(p.packet, packet, packet_size);
    r = sendDatagram((uint8_t*)&p, (uint16_t)(packet_size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE), NULL, 0);
    if (r==(-1)) { // Would block
        // @@@@ ToDo: Handle this case
    }

#endif
}

#endif // !defined(XCPTL_QUEUED_CRM) || defined(XCPTL_QUEUED_CRM_OPT)


//------------------------------------------------------------------------------


// Transmit XCP multicast response
void XcpEthTlSendMulticastCrm(const uint8_t* packet, uint16_t packet_size, const uint8_t* addr, uint16_t port) {

  int r;

  // Build XCP CTO message (ctr+dlc+packet)
  tXcpCtoMessage p;
  p.dlc = (uint16_t)packet_size;
  p.ctr = 0;
  memcpy(p.packet, packet, packet_size);
  r = sendEthDatagram((uint8_t*)&p, (uint16_t)(packet_size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE),addr,port);
  if (r == (-1)) { // Would block
      // @@@@ ToDo: Handle this case
  }
}


//------------------------------------------------------------------------------


static int handleXcpCommand(int n, tXcpCtoMessage *p, uint8_t *srcAddr, uint16_t srcPort) {

    int connected;
    (void)n;

    // gXcpTl.LastCrmCtr = p->ctr;
    connected = XcpIsConnected();

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 5) {
        DBG_PRINTF5("RX: CTR %04X LEN %04X DATA = ", p->ctr,p->dlc);
        for (int i = 0; i < p->dlc; i++) DBG_PRINTF5("%0X ", p->packet[i]);
        DBG_PRINT5("\n");
    }
#endif

    /* Connected */
    if (connected) {

#ifdef XCPTL_ENABLE_UDP
        if (!isTCP() && gXcpTl.MasterAddrValid) {

            // Check unicast ip address, not allowed to change
            if (memcmp(&gXcpTl.MasterAddr, srcAddr, sizeof(gXcpTl.MasterAddr)) != 0) { // Message from different master received
                DBG_PRINTF_WARNING("WARNING: message from unknown new master %u.%u.%u.%u, disconnecting!\n", srcAddr[0], srcAddr[1], srcAddr[2], srcAddr[3]);
                XcpDisconnect();
                gXcpTl.MasterAddrValid = FALSE;
                return 1; // Disconnect
            }

            // Check unicast master udp port, not allowed to change
            if (gXcpTl.MasterPort != srcPort) {
                DBG_PRINTF_WARNING("WARNING: master port changed from %u to %u, disconnecting!\n", gXcpTl.MasterPort, srcPort);
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
            if (!isTCP()) {
                memcpy(gXcpTl.MasterAddr, srcAddr, sizeof(gXcpTl.MasterAddr)); // Save master address, so XcpCommand can send the CONNECT response
                gXcpTl.MasterPort = srcPort;
                gXcpTl.MasterAddrValid = TRUE;
            }
#endif // UDP
            XcpTlInitTransmitQueue();
            XcpCommand((const uint32_t*)&p->packet[0],p->dlc); // Handle CONNECT command
        }
        else {
            DBG_PRINT_WARNING("WARNING: handleXcpCommand: no valid CONNECT command\n");
        }

    }

#ifdef XCPTL_ENABLE_UDP
    if (!isTCP() && !connected) { // not connected before
        if (XcpIsConnected()) {
            DBG_PRINTF3("XCP master connected on UDP addr=%u.%u.%u.%u, port=%u\n", gXcpTl.MasterAddr[0], gXcpTl.MasterAddr[1], gXcpTl.MasterAddr[2], gXcpTl.MasterAddr[3], gXcpTl.MasterPort);
        }
        else { // Is not in connected state
            gXcpTl.MasterAddrValid = FALSE; // Any client can connect
        }
    } // not connected before
#endif // UDP

    return 1; // Ok
}


// Handle incoming XCP commands
// Blocking for timeout_ms, currently XCPTL_TIMEOUT_INFINITE only (blocking)
// returns FALSE on error
BOOL XcpEthTlHandleCommands(uint32_t timeout_ms) {

    tXcpCtoMessage msgBuf;
    int16_t n;

    // Timeout not used
    // Behaviour depends on socket mode (blocking or non blocking)
    (void)timeout_ms;
    assert((!gXcpTl.blockingRx && timeout_ms==0) || (gXcpTl.blockingRx && timeout_ms==XCPTL_TIMEOUT_INFINITE));

#ifdef XCPTL_ENABLE_TCP
    if (isTCP()) {

        // Listen to incoming TCP connection if not connected
        if (gXcpTl.Sock == INVALID_SOCKET) {
            DBG_PRINT5("CDM thread waiting for TCP connection ...\n");
            gXcpTl.Sock = socketAccept(gXcpTl.ListenSock, gXcpTl.MasterAddr); // Wait here for incoming connection
            if (gXcpTl.Sock == INVALID_SOCKET) {
                DBG_PRINT_ERROR("ERROR: accept failed!\n");
                return TRUE; // Ignore error from accept, when in non blocking mode
            }
            else {
                DBG_PRINTF3("XCP master %u.%u.%u.%u accepted!\n", gXcpTl.MasterAddr[0], gXcpTl.MasterAddr[1], gXcpTl.MasterAddr[2], gXcpTl.MasterAddr[3]);
                DBG_PRINT3("Listening for XCP commands\n");
            }
        }

        // Receive TCP transport layer message
        n = socketRecv(gXcpTl.Sock, (uint8_t*)&msgBuf.dlc, (uint16_t)XCPTL_TRANSPORT_LAYER_HEADER_SIZE, TRUE); // header, recv blocking
        if (n == XCPTL_TRANSPORT_LAYER_HEADER_SIZE) {
            n = socketRecv(gXcpTl.Sock, (uint8_t*)&msgBuf.packet, msgBuf.dlc, TRUE); // packet, recv blocking
            if (n > 0) {
                if (n == msgBuf.dlc) {
                    return handleXcpCommand(n, &msgBuf, NULL, 0);
                }
                else {
                    socketShutdown(gXcpTl.Sock); // Let the receive thread terminate without error message
                    return FALSE;  // Should not happen
                }
            }
        }
        if (n==0) {  // Socket closed
            DBG_PRINT3("XCP Master closed TCP connection! XCP disconnected.\n");
            XcpDisconnect();
            sleepMs(100);
            socketShutdown(gXcpTl.Sock); // Let the receive thread terminate without error message
            socketClose(&gXcpTl.Sock);
            return TRUE; // Ok, TCP socket closed
        }
    }
#endif // TCP

#ifdef XCPTL_ENABLE_UDP
    if (!isTCP()) {
        uint16_t srcPort;
        uint8_t srcAddr[4];
        n = socketRecvFrom(gXcpTl.Sock, (uint8_t*)&msgBuf, (uint16_t)sizeof(msgBuf), srcAddr, &srcPort, NULL); 
        if (n == 0) return TRUE; // Socket closed, should not happen
        if (n < 0) {  // error
            if (socketGetLastError() == SOCKET_ERROR_WBLOCK) return 1; // Ok, timeout, no command pending
            DBG_PRINTF_ERROR("ERROR %u: recvfrom failed (result=%d)!\n", socketGetLastError(), n);
            return FALSE; // Error
        }
        else { // Ok
            if (msgBuf.dlc != n - XCPTL_TRANSPORT_LAYER_HEADER_SIZE) {
              DBG_PRINT_ERROR("ERROR: corrupt message received!\n");
              return FALSE; // Error
            }
            return handleXcpCommand(n, &msgBuf, srcAddr, srcPort);
        }
    }
#endif // UDP

    return FALSE;
}


//-------------------------------------------------------------------------------------------------------
// XCP Multicast

#ifdef XCPTL_ENABLE_MULTICAST

static int handleXcpMulticastCommand(int n, tXcpCtoMessage* p, uint8_t* dstAddr, uint16_t dstPort) {

    (void)dstAddr;
    (void)dstPort;

    // @@@@ ToDo: Check addr and cluster id and port
    //printf("MULTICAST: %u.%u.%u.%u:%u len=%u\n", dstAddr[0], dstAddr[1], dstAddr[2], dstAddr[3], dstPort, n);

    // Valid socket data received, at least transport layer header and 1 byte
    if (n >= XCPTL_TRANSPORT_LAYER_HEADER_SIZE + 1 && p->dlc <= n- XCPTL_TRANSPORT_LAYER_HEADER_SIZE) {
        XcpCommand((const uint32_t*)&p->packet[0],p->dlc); // Handle command
    }
    else {
      printf("MULTICAST ignored\n");

    }
    return 1; // Ok
}

void XcpEthTlSetClusterId(uint16_t clusterId) {
  (void)clusterId;
  // Not implemented
}


#if defined(_WIN) // Windows
DWORD WINAPI XcpTlMulticastThread(LPVOID par)
#elif defined(_LINUX) // Linux
extern void* XcpTlMulticastThread(void* par)
#endif
{
    uint8_t buffer[256];
    int16_t n;
    uint16_t srcPort;
    uint8_t srcAddr[4];

    (void)par;

    for (;;) {
        n = socketRecvFrom(gXcpTl.MulticastSock, buffer, (uint16_t)sizeof(buffer), srcAddr, &srcPort, NULL);
        if (n <= 0) break; // Terminate on error or socket close 
#ifdef XCLTL_RESTRICT_MULTICAST
        // Accept multicast from active master only
        if (gXcpTl.MasterAddrValid && memcmp(gXcpTl.MasterAddr, srcAddr, 4) == 0) {
            handleXcpMulticastCommand(n, (tXcpCtoMessage*)buffer, srcAddr, srcPort);
        }
        else {
            DBG_PRINTF_WARNING("WARNING: Ignored Multicast from %u.%u.%u.%u:%u\n", srcAddr[0], srcAddr[1], srcAddr[2], srcAddr[3], srcPort);
        }
#else
        handleXcpMulticastCommand(n, (tXcpCtoMessage*)buffer, srcAddr, srcPort);
#endif
    }
    DBG_PRINT3("XCP multicast thread terminated\n");
    socketClose(&gXcpTl.MulticastSock);
    return 0;
}

#endif // XCPTL_ENABLE_MULTICAST


//-------------------------------------------------------------------------------------------------------

BOOL XcpEthTlInit(const uint8_t* addr, uint16_t port, BOOL useTCP, BOOL blockingRx) {

    if (!XcpTlInit()) return FALSE;

    if (addr != 0)  { // Bind to given addr 
        memcpy(gXcpTl.ServerAddr, addr, 4);
    } else { // Bind to ANY(0.0.0.0)
        memset(gXcpTl.ServerAddr, 0, 4);
    }
    gXcpTl.ServerPort = port;
    gXcpTl.ServerUseTCP = useTCP;
    gXcpTl.blockingRx = blockingRx;
    gXcpTl.MasterAddrValid = FALSE;
    gXcpTl.Sock = INVALID_SOCKET;
    // Unicast UDP or TCP commands
#ifdef XCPTL_ENABLE_TCP
    gXcpTl.ListenSock = INVALID_SOCKET;
    if (useTCP) 
    { // TCP
        if (!socketOpen(&gXcpTl.ListenSock, TRUE /* useTCP */, !blockingRx, TRUE /*reuseAddr*/, FALSE /* timestamps*/)) return FALSE;
        if (!socketBind(gXcpTl.ListenSock, gXcpTl.ServerAddr, gXcpTl.ServerPort)) return FALSE; // Bind on ANY, when serverAddr=255.255.255.255
        if (!socketListen(gXcpTl.ListenSock)) return FALSE; // Put socket in listen mode
        DBG_PRINTF3("  Listening for TCP connections on %u.%u.%u.%u port %u\n", gXcpTl.ServerAddr[0], gXcpTl.ServerAddr[1], gXcpTl.ServerAddr[2], gXcpTl.ServerAddr[3], gXcpTl.ServerPort);
    }
    else
#else
    if (useTCP) 
    { // TCP
        DBG_PRINT_ERROR("ERROR: #define XCPTL_ENABLE_TCP for TCP support\n");
        return FALSE;
    }
    else
#endif
    { // UDP
        if (!socketOpen(&gXcpTl.Sock, FALSE /* useTCP */, !blockingRx, TRUE /*reuseAddr*/, FALSE /* timestamps*/)) return FALSE;
        if (!socketBind(gXcpTl.Sock, gXcpTl.ServerAddr, gXcpTl.ServerPort)) return FALSE; // Bind on ANY, when serverAddr=255.255.255.255
        DBG_PRINTF3("  Listening for XCP commands on UDP %u.%u.%u.%u port %u\n", gXcpTl.ServerAddr[0], gXcpTl.ServerAddr[1], gXcpTl.ServerAddr[2], gXcpTl.ServerAddr[3], gXcpTl.ServerPort);
    }

#ifdef PLATFORM_ENABLE_GET_LOCAL_ADDR
    socketGetLocalAddr(gXcpTl.ServerMac, NULL); // Store MAC for later use
#endif

    // Multicast UDP commands
#ifdef XCPTL_ENABLE_MULTICAST

      // Open a socket for GET_DAQ_CLOCK_MULTICAST and join its multicast group
      if (!socketOpen(&gXcpTl.MulticastSock, FALSE /*useTCP*/, FALSE /*nonblocking*/, TRUE /*reusable*/, FALSE /* timestamps*/)) return FALSE;
      DBG_PRINTF3("  Bind XCP multicast socket to %u.%u.%u.%u:%u\n", gXcpTl.ServerAddr[0], gXcpTl.ServerAddr[1], gXcpTl.ServerAddr[2], gXcpTl.ServerAddr[3], XCPTL_MULTICAST_PORT);
      if (!socketBind(gXcpTl.MulticastSock, gXcpTl.ServerAddr, XCPTL_MULTICAST_PORT)) return FALSE; // Bind to ANY, when serverAddr=255.255.255.255
      uint16_t cid = XcpGetClusterId();
      uint8_t maddr[4] = { 239,255,0,0 }; // XCPTL_MULTICAST_ADDR = 0xEFFFiiii; 
      maddr[2] = (uint8_t)(cid >> 8);
      maddr[3] = (uint8_t)(cid);
      if (!socketJoin(gXcpTl.MulticastSock, maddr)) return FALSE;
      DBG_PRINTF3("  Listening for XCP GET_DAQ_CLOCK multicast on %u.%u.%u.%u\n", maddr[0], maddr[1], maddr[2], maddr[3]);

      DBG_PRINT3("  Start XCP multicast thread\n");
      create_thread(&gXcpTl.MulticastThreadHandle, XcpTlMulticastThread);

#endif

    return TRUE;
}


void XcpEthTlShutdown() {

    // Close all sockets to enable all threads to terminate
#ifdef XCPTL_ENABLE_MULTICAST
    socketClose(&gXcpTl.MulticastSock);
    join_thread(gXcpTl.MulticastThreadHandle);
#endif
#ifdef XCPTL_ENABLE_TCP
    if (isTCP()) socketClose(&gXcpTl.ListenSock);
#endif
    socketClose(&gXcpTl.Sock);

    // Free other resources
    XcpTlShutdown();
}


//-------------------------------------------------------------------------------------------------------

// Wait for outgoing data or timeout after timeout_us
// Return FALSE in case of timeout
BOOL XcpTlWaitForTransmitData(uint32_t timeout_ms) {

  assert(timeout_ms >= 1);

#if defined(_WIN) // Windows 
    // Use event triggered for Windows
    if (WAIT_OBJECT_0 == WaitForSingleObject(gXcpTl.queue_event, timeout_ms)) {
      ResetEvent(gXcpTl.queue_event);
      return TRUE;
    }
    return FALSE;
#elif defined(_LINUX) // Linux
    // Use polling for Linux
    #define XCPTL_QUEUE_TRANSMIT_POLLING_TIME_MS 1
    uint32_t t = 0;
    while (gXcpTl.queue_len <= 1) {
        sleepMs(XCPTL_QUEUE_TRANSMIT_POLLING_TIME_MS); 
        t = t + XCPTL_QUEUE_TRANSMIT_POLLING_TIME_MS;
        if (t >= timeout_ms) return FALSE;
      }
      return TRUE;
#endif
}


//-------------------------------------------------------------------------------------------------------

int32_t XcpTlGetLastError() {
    return gXcpTl.lastError;
}


//-------------------------------------------------------------------------------------------------------

#ifdef PLATFORM_ENABLE_GET_LOCAL_ADDR
void XcpEthTlGetInfo(BOOL* isTcp, uint8_t* mac, uint8_t* addr, uint16_t *port) {
  
    *isTcp = gXcpTl.ServerUseTCP;
    memcpy(addr, gXcpTl.ServerAddr, 4);
    memcpy(mac, gXcpTl.ServerMac, 4);
    *port = gXcpTl.ServerPort;
}
#endif


#endif  // defined(XCPTL_ENABLE_UDP) || defined(XCPTL_ENABLE_TCP)



