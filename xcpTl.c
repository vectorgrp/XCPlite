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
#include "util.h"
#include "clock.h"
#ifdef APP_ENABLE_A2L_GEN
#include "A2L.h"
#endif
#include "xcpTl.h"
#include "xcpLite.h"    // Protocol layer interface



static struct {

    tUdpSock Sock;

    unsigned int SlaveMTU;
    uint16_t SlavePort;

    tUdpSockAddr MasterAddr;
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
    // XL-API
#ifdef APP_ENABLE_XLAPI_V3
    tUdpSockAddr MulticastAddrXl;
#endif
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
static int sendDatagram(const unsigned char* data, unsigned int size ) {

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
    {
        mutexLock(&gXcpTl.Mutex_Send);
        r = (int)sendto(gXcpTl.Sock.sock, data, size, SENDTO_FLAGS, (SOCKADDR*)&gXcpTl.MasterAddr.addr, (uint16_t)sizeof(gXcpTl.MasterAddr.addr));
        mutexUnlock(&gXcpTl.Mutex_Send);
    }
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
    getDtoBuffer();
    mutexUnlock(&gXcpTl.Mutex_Queue);
    assert(gXcpTl.dto_buffer_ptr);
}

// Transmit all completed and fully commited UDP frames
// Returns -1 would block, 1 ok, 0 error
int XcpTlHandleTransmitQueue( void ) {

    tXcpDtoBuffer* b;
    int result;

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
        result = sendDatagram(&b->xcp[0], b->xcp_size);
        if (result != 1) return result; // return on errors or if would block

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
unsigned char *XcpTlGetDtoBuffer(void **par, unsigned int size) {

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
    if (gXcpTl.dto_buffer_ptr==NULL || gXcpTl.dto_buffer_ptr->xcp_size + size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE > gXcpTl.SlaveMTU /*XCPTL_SOCKET_MTU_SIZE*/) {
        getDtoBuffer();
    }

    if (gXcpTl.dto_buffer_ptr != NULL) {

        // Build XCP message header (ctr+dlc) and store in DTO buffer
        p = (tXcpDtoMessage*)&gXcpTl.dto_buffer_ptr->xcp[gXcpTl.dto_buffer_ptr->xcp_size];
        p->ctr = gXcpTl.DtoCtr++;
        p->dlc = (uint16_t)size;
        gXcpTl.dto_buffer_ptr->xcp_size += size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE;

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
int XcpTlSendCrm(const unsigned char* packet, unsigned int size) {

    int result;
    unsigned int retries = SEND_RETRIES;
    assert(packet != NULL);
    assert(size>0);

    // Build XCP CTO message (ctr+dlc+packet)
    tXcpCtoMessage p;
    p.ctr = gXcpTl.LastCroCtr++;
    p.dlc = (uint16_t)size;
    memcpy(p.data, packet, size);
    do {
        result = sendDatagram((unsigned char*)&p, size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE);
        if (result != -1) break; // break on success or error, retry on would block (-1)
        sleepNs(1000);
    } while (--retries > 0);
    return result;
}

    
static int handleXcpCommands(int n, tXcpCtoMessage * p, tUdpSockAddr * src) {

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
            if (src != NULL) {

                // Check unicast ip address, not allowed to change 
                if (memcmp(&gXcpTl.MasterAddr.addr.sin_addr, &src->addr.sin_addr, sizeof(src->addr.sin_addr)) != 0) { // Message from different master received
                    char tmp[32];
                    inet_ntop(AF_INET, &src->addr.sin_addr, tmp, sizeof(tmp));
                    printf("WARNING: message from unknown new master %s, disconnecting!\n", tmp);
                    XcpDisconnect();
                    gXcpTl.MasterAddrValid = 0;
                    return 1; // Disconnect
                }

                // Check unicast master udp port, not allowed to change 
                if (gXcpTl.MasterAddr.addr.sin_port != src->addr.sin_port) {
                    printf("WARNING: master port changed from %u to %u, disconnecting!\n", htons(gXcpTl.MasterAddr.addr.sin_port), htons(src->addr.sin_port));
                    XcpDisconnect();
                    gXcpTl.MasterAddrValid = 0;
                    return 1; // Disconnect
                }
            }

            XcpCommand((const uint32_t*)&p->data[0]); // Handle command
        }

        /* Not connected yet */
        else {
            /* Check for CONNECT command ? */
            const tXcpCto* pCmd = (const tXcpCto*)&p->data[0];
            if (p->dlc == 2 && CRO_CMD == CC_CONNECT) { 
                gXcpTl.MasterAddr = *src; // Save master address, so XcpCommand can send the CONNECT response
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
                {
                    char tmp[32];
                    inet_ntop(AF_INET, &gXcpTl.MasterAddr.addr.sin_addr, tmp, sizeof(tmp));
                    printf("XCP master connected: addr=%s, port=%u\n", tmp, htons(gXcpTl.MasterAddr.addr.sin_port));
                }
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
    tUdpSockAddr src;
    socklen_t srclen;
    int n;

    // Receive a UDP datagramm
    // No no partial messages assumed

    {
        srclen = sizeof(src.addr);
        n = (int)recvfrom(gXcpTl.Sock.sock, (char*)&buffer, (uint16_t)sizeof(buffer), 0, (SOCKADDR*)&src.addr, &srclen); // recv blocking
        if (n <= 0) {
            if (n == 0) return 1; // Ok, no command pending
            if (socketGetLastError() == SOCKET_ERROR_WBLOCK) {
                return 1; // Ok, no command pending
            }
                printf("ERROR %u: recvfrom failed (result=%d)!\n", socketGetLastError(), n);
            return 0; // Error           
        }
    }

    return handleXcpCommands(n, (tXcpCtoMessage*)buffer, &src);
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
    int n;
    char tmp[32];
    uint16_t cid = XcpGetClusterId();
    uint8_t cip[4] = { 239,255,(uint8_t)(cid >> 8),(uint8_t)(cid) };

    printf("Start XCP multicast thread\n");
    if (!socketOpen(&gXcpTl.MulticastSock, 0 /*nonblocking*/, 1 /*reusable*/)) return 0;
    if (!socketBind(gXcpTl.MulticastSock, XCPTL_MULTICAST_PORT)) return 0;
    if (!socketJoin(gXcpTl.MulticastSock, cip)) return 0;
    inet_ntop(AF_INET, cip, tmp, sizeof(tmp));
    printf("  Listening on %s port=%u\n\n", tmp, XCPTL_MULTICAST_PORT);
    for (;;) {
        n = socketRecv(gXcpTl.MulticastSock, (char*)&buffer, (uint16_t)sizeof(buffer));
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


//-------------------------------------------------------------------------------------------------------

#ifdef _LINUX // Linux

int XcpTlInit(uint16_t slavePort, uint16_t slaveMTU)
{
    printf("\nInit XCP on UDP transport layer\n  (MTU=%u, DTO_QUEUE_SIZE=%u)\n", slaveMTU, XCPTL_DTO_QUEUE_SIZE);
    gXcpTl.SlaveMTU = slaveMTU;
    gXcpTl.SlavePort = slavePort;
    if (gXcpTl.SlaveMTU > XCPTL_SOCKET_JUMBO_MTU_SIZE) gXcpTl.SlaveMTU = XCPTL_SOCKET_JUMBO_MTU_SIZE;
    gXcpTl.LastCroCtr = 0;
    gXcpTl.DtoCtr = 0;
    gXcpTl.CrmCtr = 0;
    gXcpTl.MasterAddrValid = 0;

    if (!socketOpen(&gXcpTl.Sock.sock, 0, 0)) return 0;
    if (!socketBind(gXcpTl.Sock.sock, slavePort)) return 0;
    printf("  Listening on UDP port %u\n\n", slavePort);

    mutexInit(&gXcpTl.Mutex_Send,0,0);
    mutexInit(&gXcpTl.Mutex_Queue,0,1000);

    // Create multicast thread
#ifdef XCPTL_ENABLE_MULTICAST
    create_thread(&gXcpTl.MulticastThreadHandle, XcpTlMulticastThread);
    sleepMs(50);
#endif

    printf("\n");
    return 1;
}

// Wait for outgoing data or timeout after timeout_us
void XcpTlWaitForTransmitData(unsigned int timeout_us) {

    if (gXcpTl.dto_queue_len <= 1) {
        sleepNs(timeout_us * 1000);
    }
    return; 
}

void XcpTlShutdown() {

#ifdef XCPTL_ENABLE_MULTICAST
    socketClose(&gXcpTl.MulticastSock);
    sleepMs(500);
    cancel_thread(gXcpTl.MulticastThreadHandle);
#endif
    mutexDestroy(&gXcpTl.Mutex_Send);
    mutexDestroy(&gXcpTl.Mutex_Queue);
    socketClose(&gXcpTl.Sock.sock);
}


#endif

#ifdef _WIN 

int XcpTlInit(uint16_t slavePort, uint16_t slaveMTU) {

    printf("\nInit XCP on UDP transport layer\n  (MTU=%u, DTO_QUEUE_SIZE=%u)\n", slaveMTU, XCPTL_DTO_QUEUE_SIZE);

    gXcpTl.SlaveMTU = slaveMTU;
    if (gXcpTl.SlaveMTU > XCPTL_SOCKET_JUMBO_MTU_SIZE) gXcpTl.SlaveMTU = XCPTL_SOCKET_JUMBO_MTU_SIZE;
    gXcpTl.SlavePort = slavePort;
    gXcpTl.LastCroCtr = 0;
    gXcpTl.DtoCtr = 0;
    gXcpTl.CrmCtr = 0;
    gXcpTl.MasterAddrValid = 0;

    mutexInit(&gXcpTl.Mutex_Send,0,0);
    mutexInit(&gXcpTl.Mutex_Queue,0,1000);

    {
        if (!socketOpen(&gXcpTl.Sock.sock, 0, 0)) return 0;
        if (!socketBind(gXcpTl.Sock.sock,slavePort)) return 0;
        printf("  Listening on UDP port %u\n\n", slavePort);
#ifdef XCPTL_ENABLE_MULTICAST
        create_thread(&gXcpTl.MulticastThreadHandle, XcpTlMulticastThread);
#endif
        return 1;
    }
}


void XcpTlWaitForTransmitData(unsigned int timeout_us) {

    if (gXcpTl.dto_queue_len <= 1) {
        assert(timeout_us >= 1000);
        Sleep(timeout_us/1000);
    }
    return;

}


void XcpTlShutdown() {

    {
#ifdef XCPTL_ENABLE_MULTICAST
        socketClose(&gXcpTl.MulticastSock);
        sleepMs(500);
        cancel_thread(gXcpTl.MulticastThreadHandle);
#endif
        socketClose(&gXcpTl.Sock.sock);
    }
}

#endif

//-------------------------------------------------------------------------------------------------------


const char* XcpTlGetSlaveIP() {

    uint32_t a;
    uint8_t m[6];
    static char tmp[32];
    a = socketGetLocalAddr(m);
    if (a == 0) {
        return "127.0.0.1";
    }
    else {
        inet_ntop(AF_INET, &a, tmp, sizeof(tmp));
    }
    return tmp;
}

uint16_t XcpTlGetSlavePort() {

    return gXcpTl.SlavePort;
}

int XcpTlGetSlaveMAC(uint8_t *mac) {

    uint32_t a;
    a = socketGetLocalAddr(mac);
    return (a != 0);
}




