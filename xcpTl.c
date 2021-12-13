/*----------------------------------------------------------------------------
| File:
|   xcpTl.c
|
| Description:
|   XCP on UDP transport layer
|   Linux (Raspberry Pi) and Windows version
|   Supports Winsock, Linux Sockets and Vector XL-API V3
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "platform.h"
#include "main_cfg.h"
#include "clock.h"
#ifdef APP_ENABLE_A2L_GEN
#include "A2L.h"
#endif
#include "xcpTl.h"
#include "xcpLite.h"    // Protocol layer interface



static struct {

    SOCKET Sock;

    unsigned int SlaveMTU;
    uint8_t SlaveAddr[4];
    uint16_t SlavePort;

    uint8_t MasterAddr[4];
    uint16_t MasterPort;
    int MasterAddrValid;

    // Transmit queue
    tXcpDtoBuffer dto_queue[XCPTL_DTO_QUEUE_SIZE];
    unsigned int dto_queue_rp; // rp = read index
    unsigned int dto_queue_len; // rp+len = write index (the next free entry), len=0 ist empty, len=XCPTL_DTO_QUEUE_SIZE is full
    tXcpDtoBuffer* dto_buffer_ptr; // current incomplete or not fully commited entry
    uint64_t dto_bytes_written;   // data bytes writen

    // CTO command transfer object counters (CRM,CRO)
    uint16_t LastCroCtr; // Last CRO command receive object message packet counter received
    uint16_t CrmCtr; // next CRM command response message packet counter

    // DTO data transfer object counter (DAQ,STIM)
    uint16_t DtoCtr; // next DAQ DTO data transmit message packet counter

    // Multicast
#ifdef XCPTL_ENABLE_MULTICAST
    tXcpThread MulticastThreadHandle;
    SOCKET MulticastSock;
#endif

    MUTEX Mutex_Queue;
    MUTEX Mutex_Send;

} gXcpTl;


uint64_t XcpTlGetBytesWritten() {
    return gXcpTl.dto_bytes_written;
}


#ifdef XCPTL_ENABLE_MULTICAST
static int handleXcpMulticast(int n, tXcpCtoMessage* p);
#endif



// Transmit a UDP datagramm (contains multiple XCP DTO messages or a single CRM message)
// Must be thread safe, because it is called from CMD and from DAQ thread
// Returns -1 on would block, 1 if ok, 0 on error
static int sendDatagram(const uint8_t *data, uint16_t size ) {

    int r;

#ifdef XCP_ENABLE_TESTMODE
    if (ApplXcpGetDebugLevel() >= 3) {
        printf("TX: size=%u ",size);
        for (unsigned int i = 0; i < size; i++) printf("%0X ", data[i]);
        printf("\n");
    }
#endif

    // Respond to active master
    if (!gXcpTl.MasterAddrValid) {
        printf("ERROR: invalid master address!\n");
        return 0;
    }

    mutexLock(&gXcpTl.Mutex_Send);
    r = socketSendTo(gXcpTl.Sock, data, size, gXcpTl.MasterAddr, gXcpTl.MasterPort);
    mutexUnlock(&gXcpTl.Mutex_Send);
    if (r != size) {
        if (socketGetLastError()==SOCKET_ERROR_WBLOCK) {
            return -1; // Would block
        }
        else {
            printf("ERROR: sento failed (result=%d, errno=%d)!\n", r, socketGetLastError());
        }
        return 0; // Error
    }

    return 1; // Ok
}


//------------------------------------------------------------------------------
// XCP (UDP) transport layer packet queue (DTO buffers)

// Not thread save!
static void getDtoBuffer() {

    tXcpDtoBuffer* b;

    /* Check if there is space in the queue */
    if (gXcpTl.dto_queue_len >= XCPTL_DTO_QUEUE_SIZE) {
        /* Queue overflow */
        gXcpTl.dto_buffer_ptr = NULL;
    }
    else {
        unsigned int i = gXcpTl.dto_queue_rp + gXcpTl.dto_queue_len;
        if (i >= XCPTL_DTO_QUEUE_SIZE) i -= XCPTL_DTO_QUEUE_SIZE;
        b = &gXcpTl.dto_queue[i];
        b->xcp_size = 0;
        b->xcp_uncommited = 0;
        gXcpTl.dto_buffer_ptr = b;
        gXcpTl.dto_queue_len++;
    }
}

// Clear and init transmit queue
void XcpTlInitTransmitQueue() {

    mutexLock(&gXcpTl.Mutex_Queue);
    gXcpTl.dto_queue_rp = 0;
    gXcpTl.dto_queue_len = 0;
    gXcpTl.dto_buffer_ptr = NULL;
    gXcpTl.dto_bytes_written = 0;
    getDtoBuffer();
    mutexUnlock(&gXcpTl.Mutex_Queue);
    assert(gXcpTl.dto_buffer_ptr);
}

// Transmit all completed and fully commited UDP frames
// Returns -1 would block, 1 ok, 0 error
int XcpTlHandleTransmitQueue( void ) {

    tXcpDtoBuffer* b;

    for (;;) {

        // Check
        mutexLock(&gXcpTl.Mutex_Queue);
        if (gXcpTl.dto_queue_len > 1) {
            b = &gXcpTl.dto_queue[gXcpTl.dto_queue_rp];
            if (b->xcp_uncommited > 0) b = NULL;
        }
        else {
            b = NULL;
        }
        mutexUnlock(&gXcpTl.Mutex_Queue);
        if (b == NULL) break;

        // Send this frame
        int r = sendDatagram(&b->xcp[0], b->xcp_size);
        if (r != 1) return r; // return on errors or if would block
        gXcpTl.dto_bytes_written += b->xcp_size;

        // Free this buffer when succesfully sent
        mutexLock(&gXcpTl.Mutex_Queue);
        gXcpTl.dto_queue_rp++;
        if (gXcpTl.dto_queue_rp >= XCPTL_DTO_QUEUE_SIZE) gXcpTl.dto_queue_rp -= XCPTL_DTO_QUEUE_SIZE;
        gXcpTl.dto_queue_len--;
        mutexUnlock(&gXcpTl.Mutex_Queue);

    } // for (;;)

    return 1; // Ok, queue empty now
}

// Transmit all committed DTOs
void XcpTlFlushTransmitQueue() {

#ifdef XCP_ENABLE_TESTMODE
    if (ApplXcpGetDebugLevel() >= 3) {
        printf("FlushTransmitQueue()\n");
    }
#endif

    // Complete the current buffer if non empty
    mutexLock(&gXcpTl.Mutex_Queue);
    if (gXcpTl.dto_buffer_ptr!=NULL && gXcpTl.dto_buffer_ptr->xcp_size>0) getDtoBuffer();
    mutexUnlock(&gXcpTl.Mutex_Queue);

    XcpTlHandleTransmitQueue();
}

// Reserve space for a DTO packet in a DTO buffer and return a pointer to data and a pointer to the buffer for commit reference
// Flush the transmit buffer, if no space left
uint8_t *XcpTlGetDtoBuffer(void **par, uint16_t size) {

    tXcpDtoMessage* p;

 #ifdef XCP_ENABLE_TESTMODE
    if (ApplXcpGetDebugLevel() >= 4) {
        printf("GetPacketBuffer(%u)\n", size);
        if (gXcpTl.dto_buffer_ptr) {
            printf("  current dto_buffer_ptr size=%u, c=%u\n", gXcpTl.dto_buffer_ptr->xcp_size, gXcpTl.dto_buffer_ptr->xcp_uncommited);
        }
        else {
            printf("  dto_buffer_ptr = NULL\n");
        }
    }
#endif

    mutexLock(&gXcpTl.Mutex_Queue);

    // Get another message buffer from queue, when active buffer ist full, overrun or after time condition
    if (gXcpTl.dto_buffer_ptr==NULL || (uint16_t)(gXcpTl.dto_buffer_ptr->xcp_size + size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE) > gXcpTl.SlaveMTU /*XCPTL_SOCKET_MTU_SIZE*/) {
        getDtoBuffer();
    }

    if (gXcpTl.dto_buffer_ptr != NULL) {

        // Build XCP message header (ctr+dlc) and store in DTO buffer
        p = (tXcpDtoMessage*)&gXcpTl.dto_buffer_ptr->xcp[gXcpTl.dto_buffer_ptr->xcp_size];
        p->ctr = gXcpTl.DtoCtr++;
        p->dlc = (uint16_t)size;
        gXcpTl.dto_buffer_ptr->xcp_size = (uint16_t)(gXcpTl.dto_buffer_ptr->xcp_size + size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE);
        *((tXcpDtoBuffer**)par) = gXcpTl.dto_buffer_ptr;
        gXcpTl.dto_buffer_ptr->xcp_uncommited++;

    }
    else {
        p = NULL; // Overflow
    }

    mutexUnlock(&gXcpTl.Mutex_Queue);

    if (p == NULL) return NULL; // Overflow
    return &p->data[0]; // return pointer to XCP message DTO data
}

void XcpTlCommitDtoBuffer(void *par) {

    tXcpDtoBuffer* p = (tXcpDtoBuffer*)par;

    if (par != NULL) {

#ifdef XCP_ENABLE_TESTMODE
        if (ApplXcpGetDebugLevel() >= 4) {
            printf("CommitPacketBuffer() c=%u,s=%u\n", p->xcp_uncommited, p->xcp_size);
        }
#endif

        mutexLock(&gXcpTl.Mutex_Queue);
        p->xcp_uncommited--;
        mutexUnlock(&gXcpTl.Mutex_Queue);

    }
}


//------------------------------------------------------------------------------

// Transmit XCP response or event packet
// Returns 0 error, 1 ok, -1 would block
int XcpTlSendCrm(const uint8_t *packet, uint16_t size) {

    assert(packet != NULL);
    assert(size>0);

    // Build XCP CTO message (ctr+dlc+packet)
    tXcpCtoMessage p;
    p.ctr = gXcpTl.LastCroCtr++;
    p.dlc = (uint16_t)size;
    memcpy(p.data, packet, size);
    return sendDatagram((unsigned char*)&p, (uint16_t)(size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE));
}


static int handleXcpCommands(uint16_t n, tXcpCtoMessage * p, uint8_t *srcAddr, uint16_t srcPort) {

    int connected;

    if (n >= XCPTL_TRANSPORT_LAYER_HEADER_SIZE+1) { // Valid socket data received, at least transport layer header and 1 byte

        // gXcpTl.LastCrmCtr = p->ctr;
        connected = XcpIsConnected();

#ifdef XCP_ENABLE_TESTMODE
        if (ApplXcpGetDebugLevel() >= 4 || (!connected && ApplXcpGetDebugLevel() >= 1)) {
            printf("RX: CTR %04X LEN %04X DATA = ", p->ctr,p->dlc);
            for (int i = 0; i < p->dlc; i++) printf("%0X ", p->data[i]);
            printf("\n");
        }
#endif

        /* Connected */
        if (connected) {

            // Check src addr
            assert(gXcpTl.MasterAddrValid);

            // Check unicast ip address, not allowed to change
            if (memcmp(&gXcpTl.MasterAddr, srcAddr, sizeof(gXcpTl.MasterAddr)) != 0) { // Message from different master received
                printf("WARNING: message from unknown new master %u.%u.%u.%u, disconnecting!\n", srcAddr[0], srcAddr[1], srcAddr[2], srcAddr[3]);
                XcpDisconnect();
                gXcpTl.MasterAddrValid = 0;
                return 1; // Disconnect
            }

            // Check unicast master udp port, not allowed to change
            if (gXcpTl.MasterPort != srcPort) {
                printf("WARNING: master port changed from %u to %u, disconnecting!\n", gXcpTl.MasterPort, srcPort);
                XcpDisconnect();
                gXcpTl.MasterAddrValid = 0;
                return 1; // Disconnect
            }

            XcpCommand((const uint32_t*)&p->data[0]); // Handle command
        }

        /* Not connected yet */
        else {
            /* Check for CONNECT command ? */
            const tXcpCto* pCmd = (const tXcpCto*)&p->data[0];
            if (p->dlc == 2 && CRO_CMD == CC_CONNECT) {
                memcpy(gXcpTl.MasterAddr,srcAddr,sizeof(gXcpTl.MasterAddr)); // Save master address, so XcpCommand can send the CONNECT response
                gXcpTl.MasterPort = srcPort;
                gXcpTl.MasterAddrValid = 1;
                XcpCommand((const uint32_t*)&p->data[0]); // Handle CONNECT command
            }
#ifdef XCP_ENABLE_TESTMODE
            else if (ApplXcpGetDebugLevel() >= 1) {
                printf("WARNING: no valid CONNECT command\n");
            }
#endif
        }

        // Actions after successfull connect
        if (!connected) {
            if (XcpIsConnected()) { // Is in connected state
#ifdef XCP_ENABLE_TESTMODE
                printf("XCP master connected: addr=%u.%u.%u.%u, port=%u\n", gXcpTl.MasterAddr[0], gXcpTl.MasterAddr[1], gXcpTl.MasterAddr[2], gXcpTl.MasterAddr[3], gXcpTl.MasterPort);
#endif

                // Inititialize the DAQ message queue
                XcpTlInitTransmitQueue();

            }
            else { // Is not in connected state
                gXcpTl.MasterAddrValid = 0; // Any client can connect
            }
        } // !connected before

    }
    else if (n>0) {
        printf("WARNING: invalid transport layer header received!\n");
        return 0; // Error
    }
    return 1; // Ok
}


// Handle incoming XCP commands
// returns 0 on error
int XcpTlHandleCommands() {

    uint8_t buffer[XCPTL_TRANSPORT_LAYER_HEADER_SIZE + XCPTL_CTO_SIZE];
    uint16_t srcPort;
    uint8_t srcAddr[4];
    int16_t n;

    // Receive a UDP datagramm
    // No no partial messages assumed
    n = socketRecvFrom(gXcpTl.Sock, buffer, (uint16_t)sizeof(buffer), srcAddr, &srcPort); // recv blocking
    if (n <= 0) {
        if (n == 0) return 1; // Ok, no command pending
        if (socketGetLastError() == SOCKET_ERROR_WBLOCK) {
            return 1; // Ok, no command pending
        }
        printf("ERROR %u: recvfrom failed (result=%d)!\n", socketGetLastError(), n);
        return 0; // Error
    }
    else {
        return handleXcpCommands((uint16_t)n, (tXcpCtoMessage*)buffer, srcAddr, srcPort);
    }
}


//-------------------------------------------------------------------------------------------------------
// XCP Multicast

#ifdef XCPTL_ENABLE_MULTICAST

static int handleXcpMulticast(int n, tXcpCtoMessage* p) {

    // Valid socket data received, at least transport layer header and 1 byte
    if (gXcpTl.MasterAddrValid && XcpIsConnected() && n >= XCPTL_TRANSPORT_LAYER_HEADER_SIZE + 1) {
        XcpCommand((const uint32_t*)&p->data[0]); // Handle command
    }
    return 1; // Ok
}

#ifdef _WIN
DWORD WINAPI XcpTlMulticastThread(LPVOID lpParameter)
#else
extern void* XcpTlMulticastThread(void* par)
#endif
{
    uint8_t buffer[256];
    int16_t n;
    for (;;) {
        n = socketRecv(gXcpTl.MulticastSock, (uint8_t*)&buffer, (uint16_t)sizeof(buffer));
        if (n < 0) break; // Terminate on error (socket close is used to terminate thread)
        handleXcpMulticast(n, (tXcpCtoMessage*)buffer);
    }
    printf("Terminate XCP multicast thread\n");
    socketClose(&gXcpTl.MulticastSock);
    return 0;
}

#endif

void XcpTlSetClusterId(uint16_t clusterId) {

}

int XcpTlInit(uint8_t* slaveAddr, uint16_t slavePort, uint16_t slaveMTU) {

    printf("\nInit XCP on UDP transport layer\n  (MTU=%u, DTO_QUEUE_SIZE=%u)\n", slaveMTU, XCPTL_DTO_QUEUE_SIZE);
    gXcpTl.SlaveMTU = slaveMTU;
    if (gXcpTl.SlaveMTU > XCPTL_SOCKET_JUMBO_MTU_SIZE) gXcpTl.SlaveMTU = XCPTL_SOCKET_JUMBO_MTU_SIZE;
    if (slaveAddr != 0 && slaveAddr[0] != 255 ) {  // Bind to given addr or ANY (0.0.0.0)
        memcpy(gXcpTl.SlaveAddr, slaveAddr, 4);
    }
    else { // Bind to the first adapter addr found
        socketGetLocalAddr(NULL, gXcpTl.SlaveAddr);
    }
    gXcpTl.SlavePort = slavePort;
    gXcpTl.LastCroCtr = 0;
    gXcpTl.DtoCtr = 0;
    gXcpTl.CrmCtr = 0;
    gXcpTl.MasterAddrValid = 0;

    mutexInit(&gXcpTl.Mutex_Send, 0, 0);
    mutexInit(&gXcpTl.Mutex_Queue, 0, 1000);

    // Unicast commands
    if (!socketOpen(&gXcpTl.Sock, 0 /*nonblocking*/, 0 /*reusable*/)) return 0;
    printf("  Bind XCP socket to %u.%u.%u.%u\n", gXcpTl.SlaveAddr[0], gXcpTl.SlaveAddr[1], gXcpTl.SlaveAddr[2], gXcpTl.SlaveAddr[3]);
    if (!socketBind(gXcpTl.Sock, gXcpTl.SlaveAddr, gXcpTl.SlavePort)) return 0; // Bind to ANY, when slaveAddr=255.255.255.255
    printf("  Listening for XCP commands on UDP port %u\n\n", gXcpTl.SlavePort);

    // Multicast commands
#ifdef XCPTL_ENABLE_MULTICAST

    uint16_t cid = XcpGetClusterId();
    uint8_t cip[4] = { 239,255,(uint8_t)(cid >> 8),(uint8_t)(cid) };

    printf("Start XCP multicast thread\n");
    if (!socketOpen(&gXcpTl.MulticastSock, 0 /*nonblocking*/, 1 /*reusable*/)) return 0;
    printf("  Bind XCP multicast socket to %u.%u.%u.%u\n", gXcpTl.SlaveAddr[0], gXcpTl.SlaveAddr[1], gXcpTl.SlaveAddr[2], gXcpTl.SlaveAddr[3]);
    if (!socketBind(gXcpTl.MulticastSock, gXcpTl.SlaveAddr, XCPTL_MULTICAST_PORT)) return 0; // Bind to ANY, when slaveAddr=255.255.255.255
    if (!socketJoin(gXcpTl.MulticastSock, cip)) return 0;
    printf("  Listening for XCP multicast from %u.%u.%u.%u:%u\n\n", cip[0], cip[1], cip[2], cip[3], XCPTL_MULTICAST_PORT);


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
    mutexDestroy(&gXcpTl.Mutex_Send);
    mutexDestroy(&gXcpTl.Mutex_Queue);
    socketClose(&gXcpTl.Sock);
}



// Wait for outgoing data or timeout after timeout_us
void XcpTlWaitForTransmitData(unsigned int timeout_us) {

    if (gXcpTl.dto_queue_len <= 1) {
        sleepNs(timeout_us*1000);
    }
    return;
}



//-------------------------------------------------------------------------------------------------------


// Info used to generate A2L
const char* XcpTlGetSlaveAddrString() {

    static char tmp[32];
    uint8_t addr[4],*a;
    if (gXcpTl.SlaveAddr[0] != 0) {
        a = gXcpTl.SlaveAddr;
    }
    else if (socketGetLocalAddr(NULL, addr)) {
        a = addr;
    }
    else {
        return "127.0.0.1";
    }
    sprintf(tmp, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    return tmp;
}

// Info used to generate A2L
uint16_t XcpTlGetSlavePort() {

    return gXcpTl.SlavePort;
}

// Info used to generate clock UUID
int XcpTlGetSlaveMAC(uint8_t *mac) {

    return socketGetLocalAddr(mac,NULL);
}
