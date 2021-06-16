/*----------------------------------------------------------------------------
| File:
|   udpserver.c
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

#include "main.h"
#include "udpserver.h"


// XCP on UDP Transport Layer data
tXcpTlData gXcpTl;


#ifndef _WIN // Linux sockets
pthread_mutex_t gXcpTlMutex = PTHREAD_MUTEX_INITIALIZER;
#endif


// Transmit a UDP datagramm (contains multiple XCP DTO messages or a single CRM message)
// Must be thread safe, because it is called from CMD and from DAQ thread
// Returns -1 on would block, 1 if ok, 0 on error
static int sendDatagram(const unsigned char* data, unsigned int size ) {

    int r;
        
#if defined ( XCP_ENABLE_TESTMODE )
    if (gDebugLevel >= 3) {
        printf("TX: size=%u ",size);
        for (unsigned int i = 0; i < size; i++) printf("%00X ", data[i]);
        printf("\n");
    }
#endif

    // Respond to active master
    if (!gXcpTl.MasterAddrValid) {
        printf("ERROR: invalid master address!\n");
        return 0;
    }
#ifdef XCPSIM_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        r = udpSendTo(gXcpTl.Sock.sockXl, data, size, 0, &gXcpTl.MasterAddr.addrXl, sizeof(gXcpTl.MasterAddr.addrXl));
    }
    else 
#endif    
    {
        //LOCK();
        r = sendto(gXcpTl.Sock.sock, data, size, SENDTO_FLAGS, (SOCKADDR*)&gXcpTl.MasterAddr.addr, sizeof(gXcpTl.MasterAddr.addr));
        //UNLOCK();
    }
    if (r != size) {
        if (udpSendtoWouldBlock(r)) {
            return -1; // Would block
        }
        else {
            printf("ERROR: sento failed (result=%d, errno=%d)!\n", r, udpGetLastError());
        }
        return 0; // Error
    }

    return 1; // Ok
}


//------------------------------------------------------------------------------
// XCP (UDP) transport layer packet queue (DTO buffers)

// Not thread save, called with LOCK()
static void getDtoBuffer(void) {

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
void udpTlInitTransmitQueue(void) {

    LOCK();
    gXcpTl.dto_queue_rp = 0;
    gXcpTl.dto_queue_len = 0;
    gXcpTl.dto_buffer_ptr = NULL;
    getDtoBuffer();
    UNLOCK();
    assert(gXcpTl.dto_buffer_ptr);
}

// Transmit all completed and fully commited UDP frames
// Returns -1 would block, 1 ok, 0 error
int udpTlHandleTransmitQueue( void ) {

    tXcpDtoBuffer* b;
    int result;

#ifdef XCPSIM_TEST_INSTRUMENTATION
    gXcpTl.dto_queue_cycle = gClock32- gXcpTl.dto_queue_timestamp;
    gXcpTl.dto_queue_timestamp = gClock32;
#endif

    for (;;) {

        // Check
        LOCK();
        if (gXcpTl.dto_queue_len > 1) {
            b = &gXcpTl.dto_queue[gXcpTl.dto_queue_rp];
            if (b->xcp_uncommited > 0) b = NULL; 
        }
        else {
            b = NULL;
        }
        UNLOCK();
        if (b == NULL) break;

        // Send this frame
        result = sendDatagram(&b->xcp[0], b->xcp_size);
        if (result != 1) return result; // return on errors or if would block

        // Free this buffer when succesfully sent
        LOCK();
        gXcpTl.dto_queue_rp++;
        if (gXcpTl.dto_queue_rp >= XCPTL_DTO_QUEUE_SIZE) gXcpTl.dto_queue_rp -= XCPTL_DTO_QUEUE_SIZE;
        gXcpTl.dto_queue_len--;
        UNLOCK();

    } // for (;;)

    return 1; // Ok, queue empty now
}

// Transmit all committed DTOs
void udpTlFlushTransmitQueue(void) {

#if defined ( XCP_ENABLE_TESTMODE )
    if (gDebugLevel >= 3) {
        printf("FlushTransmitQueue()\n");
    }
#endif

    // Complete the current buffer if non empty
    LOCK();
    if (gXcpTl.dto_buffer_ptr!=NULL && gXcpTl.dto_buffer_ptr->xcp_size>0) getDtoBuffer();
    UNLOCK();

    udpTlHandleTransmitQueue();
}

// Reserve space for a DTO packet in a DTO buffer and return a pointer to data and a pointer to the buffer for commit reference
// Flush the transmit buffer, if no space left
unsigned char *udpTlGetPacketBuffer(void **par, unsigned int size) {

    tXcpDtoMessage* p;

 #if defined ( XCP_ENABLE_TESTMODE )
    if (gDebugLevel >= 4) {
        printf("GetPacketBuffer(%u)\n", size);
        if (gXcpTl.dto_buffer_ptr) {
            printf("  current dto_buffer_ptr size=%u, c=%u\n", gXcpTl.dto_buffer_ptr->xcp_size, gXcpTl.dto_buffer_ptr->xcp_uncommited);
        }
        else {
            printf("  dto_buffer_ptr = NULL\n");
        }
    }
#endif

    LOCK();

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

    UNLOCK();
        
    if (p == NULL) return NULL; // Overflow
    return &p->data[0]; // return pointer to XCP message DTO data
}

void udpTlCommitPacketBuffer(void *par) {

    tXcpDtoBuffer* p = (tXcpDtoBuffer*)par;

    if (par != NULL) {

#if defined ( XCP_ENABLE_TESTMODE )
        if (gDebugLevel >= 4) {
            printf("CommitPacketBuffer() c=%u,s=%u\n", p->xcp_uncommited, p->xcp_size);
        }
#endif   

        LOCK();
        p->xcp_uncommited--;
        UNLOCK();
    }
}


//------------------------------------------------------------------------------

// Transmit XCP response or event packet
// Returns 0 error, 1 ok, -1 would block
int udpTlSendCrmPacket(const unsigned char* packet, unsigned int size) {

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


// Handle incoming XCO commands
// returns 0 on error
int udpTlHandleXCPCommands(void) {

    int n,connected;
    tXcpCtoMessage buffer;
    tUdpSockAddr src;
    int srclen;

    // Receive a UDP datagramm
    // No no partial messages assumed

#ifdef XCPSIM_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        srclen = sizeof(src.addrXl);
        n = udpRecvFrom(gXcpTl.Sock.sockXl, (char*)&buffer, sizeof(buffer), &src.addrXl);
        if (n <= 0) {
            if (n == 0) return 1; // Ok, no command pending
            if (udpRecvWouldBlock()) return 1; // Ok, no command pending
            printf("ERROR: recvfrom failed (result=%d)!\n", n);
           return 0; // Error
        }
    }
    else 
#endif
    {
        srclen = sizeof(src.addr);
        n = recvfrom(gXcpTl.Sock.sock, (char*)&buffer, sizeof(buffer), RECV_FLAGS, (SOCKADDR*)&src.addr, &srclen);
        if (n <= 0) {
            if (n == 0) return 1; // Ok, no command pending
            if (udpRecvWouldBlock()) {
                return 1; // Ok, no command pending
            }
                printf("ERROR: recvfrom failed (result=%d,error=%d)!\n", n, udpGetLastError());
            return 0; // Error           
        }
    }

    if (n >= XCPTL_TRANSPORT_LAYER_HEADER_SIZE+1) { // Valid socket data received, at least transport layer header and 1 byte

        // gXcpTl.LastCrmCtr = buffer.ctr;
        connected = XcpIsConnected();

#ifdef XCP_ENABLE_TESTMODE
        if (gDebugLevel >= 4 || (!connected && gDebugLevel >= 1)) {
            printf("RX: CTR %04X LEN %04X DATA = ", buffer.ctr,buffer.dlc);
            for (int i = 0; i < buffer.dlc; i++) printf("%00X ", buffer.data[i]);
            printf("\n");
        }
#endif

        /* Connected */
        if (connected) {
            // Check master ip address, ignore source port
            assert(gXcpTl.MasterAddrValid);
            if (memcmp(&gXcpTl.MasterAddr.addr.sin_addr, &src.addr.sin_addr, sizeof(src.addr.sin_addr)) != 0) {
                printf("WARNING: message from different master ignored\n");
                return 1; // Ignore
            }
            if (gXcpTl.MasterAddr.addr.sin_port != src.addr.sin_port) {
                printf("WARNING: master port changed from %u to %u, disconnecting!\n", htons(gXcpTl.MasterAddr.addr.sin_port), htons(src.addr.sin_port));
                XcpDisconnect();
                gXcpTl.MasterAddrValid = 0;
                return 1; // Disconnect
            }
            XcpCommand((const vuint32*)&buffer.data[0]); // Handle command
        }

        /* Not connected yet */
        else {
            /* Check for CONNECT command ? */
            const tXcpCto* pCmd = (const tXcpCto*)&buffer.data[0];
            if (buffer.dlc == 2 && CRO_CMD == CC_CONNECT) { 
                gXcpTl.MasterAddr = src; // Save master address, so XcpCommand can send the CONNECT response
                gXcpTl.MasterAddrValid = 1;
                XcpCommand((const vuint32*)&buffer.data[0]); // Handle CONNECT command
            }
#ifdef XCP_ENABLE_TESTMODE
            else if (gDebugLevel >= 1) {
                printf("WARNING: no valid CONNECT command\n");
            }
#endif
        }
       
        // Actions after successfull connect
        if (!connected) {
            if (XcpIsConnected()) { // Is in connected state

#ifdef XCP_ENABLE_TESTMODE
                {
                    unsigned char tmp[32];
                    inet_ntop(AF_INET, &gXcpTl.MasterAddr.addr.sin_addr, tmp, sizeof(tmp));
                    printf("XCP master connected: addr=%s, port=%u\n", tmp, htons(gXcpTl.MasterAddr.addr.sin_port));
                }
#endif

                // Inititialize the DAQ message queue
                udpTlInitTransmitQueue(); 

            } // Success 
            else { // Is not in connected state
                gXcpTl.MasterAddrValid = 0; // Any client can connect
            } 
        } // !connected before

        return 1; // Ok
    }
    else {
        printf("WARNING: invalid transport layer header received!\n");
        return 0; // Error
    }
}



//-------------------------------------------------------------------------------------------------------

#ifdef _LINUX // Linux

int udpTlInit(unsigned char slaveMac[6], unsigned char slaveAddr[4], uint16_t slavePort, unsigned int MTU)
{
    gXcpTl.SlaveMTU = MTU;
    gXcpTl.LastCroCtr = 0;
    gXcpTl.DtoCtr = 0;
    gXcpTl.CrmCtr = 0;
    gXcpTl.MasterAddrValid = 0;

    // Create a socket
    gXcpTl.Sock.sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (gXcpTl.Sock.sock < 0) {
        printf("error: cannot open socket!\n");
        return 0;
    }

    // Set socket transmit buffer size
    // No need for large buffer if DTO queue is enabled
    unsigned int txBufferSize = XCPTL_SOCKET_BUFFER_SIZE;
    setsockopt(gXcpTl.Sock.sock, SOL_SOCKET, SO_SNDBUF, (void*)&txBufferSize, sizeof(txBufferSize));

    // Avoid "Address already in use" error message
    int yes = 1;
    setsockopt(gXcpTl.Sock.sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // Bind the socket to any address and the specified port
    gXcpTl.SlaveAddr.addr.sin_family = AF_INET;
    gXcpTl.SlaveAddr.addr.sin_addr.s_addr = htonl(INADDR_ANY); 
    gXcpTl.SlaveAddr.addr.sin_port = htons(slavePort);
    memset(gXcpTl.SlaveAddr.addr.sin_zero, '\0', sizeof(gXcpTl.SlaveAddr.addr.sin_zero));
    if (bind(gXcpTl.Sock.sock, (SOCKADDR*)&gXcpTl.SlaveAddr.addr, sizeof(gXcpTl.SlaveAddr.addr)) < 0) {
        printf("error: Cannot bind on UDP port!\n");
        udpTlShutdown();
        return 0;
    }
    printf("  Listening on port %u\n", slavePort);

#if defined ( XCP_ENABLE_TESTMODE )
    if (gDebugLevel >= 1) {
        char tmp[32];
        inet_ntop(AF_INET, &gXcpTl.SlaveAddr.addr.sin_addr, tmp, sizeof(tmp));
        printf("  Bind sin_family=%u, addr=%s, port=%u\n", gXcpTl.SlaveAddr.addr.sin_family, tmp, ntohs(gXcpTl.SlaveAddr.addr.sin_port));
    }
#endif

    // Create a mutex needed for multithreaded DAQ event data packet transmissions
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&gXcpTlMutex, NULL);

    printf("\n");
    return 1;
}

// Wait for incoming io or timeout after <timeout> us
#ifdef XCPSIM_SINGLE_THREAD_SLAVE

#error "No reactor implemented in Linux version"
void udpTlWaitForReceiveEvent(unsigned int timeout_us) {
    return; 
}

#else

// Wait for outgoing data  or timeout after <timeout> us
void udpTlWaitForTransmitData(unsigned int timeout_us) {

    if (gXcpTl.dto_queue_len <= 1) {
        sleepNs(timeout_us * 1000);
    }
    return; 
}

#endif

void udpTlShutdown(void) {

    DESTROY();
    shutdown(gXcpTl.Sock.sock, SHUT_RDWR);
}


#endif
#ifdef _WIN 

#include "udp.h"

static HANDLE gEvent = 0;

int udpTlInit(unsigned char *slaveMac, unsigned char *slaveAddr, uint16_t slavePort, unsigned int slaveMTU) {

    printf("Init XCP on UDP transport layer\n  (MTU=%u, DTO_QUEUE_SIZE=%u)\n", slaveMTU, XCPTL_DTO_QUEUE_SIZE);

    gXcpTl.SlaveMTU = slaveMTU;
    if (gXcpTl.SlaveMTU > XCPTL_SOCKET_JUMBO_MTU_SIZE) gXcpTl.SlaveMTU = XCPTL_SOCKET_JUMBO_MTU_SIZE;

    gXcpTl.LastCroCtr = 0;
    gXcpTl.DtoCtr = 0;
    gXcpTl.CrmCtr = 0;
    gXcpTl.MasterAddrValid = 0;

#ifdef XCPSIM_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {

        printf("  (IP=%u.%u.%u.%u port=%u)\n", slaveAddr[0], slaveAddr[1], slaveAddr[2], slaveAddr[3], slavePort);

        // Create a critical section needed for multithreaded event data packet transmissions
        InitializeCriticalSection(&gXcpTl.cs);

        // Create a UDP socket on MAC-addr, IP-addr and port
        memset((void*)&gXcpTl.SlaveAddr.addrXl, 0, sizeof(gXcpTl.SlaveAddr.addrXl));
        memcpy(gXcpTl.SlaveAddr.addrXl.sin_mac, slaveMac, 6);
        memcpy(gXcpTl.SlaveAddr.addrXl.sin_addr, slaveAddr, 4);
        gXcpTl.SlaveAddr.addrXl.sin_port = HTONS(slavePort);
        memset((void*)&gXcpTl.SlaveMulticastAddr.addrXl, 0, sizeof(gXcpTl.SlaveMulticastAddr.addrXl));
        unsigned char multicastAddr[4] = { 239,255,0,1 };
        memcpy(gXcpTl.SlaveMulticastAddr.addrXl.sin_addr, multicastAddr, 4);
        gXcpTl.SlaveMulticastAddr.addrXl.sin_port = HTONS(5557);

        return udpInit(&gXcpTl.Sock.sockXl, &gEvent, &gXcpTl.SlaveAddr.addrXl, &gXcpTl.SlaveMulticastAddr.addrXl);

    }
    else 
#endif
    {

        WORD wsaVersionRequested;
        WSADATA wsaData;
        int err;

        // Create a critical section needed for multithreaded event data packet transmissions
        InitializeCriticalSection(&gXcpTl.cs);

        // Init Winsock2
        wsaVersionRequested = MAKEWORD(2, 2);
        err = WSAStartup(wsaVersionRequested, &wsaData);
        if (err != 0) {
            printf("ERROR: WSAStartup failed with ERROR: %d!\n", err);
            return 0;
        }
        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) { // Confirm that the WinSock DLL supports 2.2
            printf("ERROR: could not find a usable version of Winsock.dll!\n");
            WSACleanup();
            return 0;
        }

        // Create a socket
        gXcpTl.Sock.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (gXcpTl.Sock.sock == INVALID_SOCKET) {
            printf("ERROR: could not create socket!\n");
            WSACleanup();
            return 0;
        }

        // Avoid send to UDP nowhere problem (server has no open socket on master port) (stack-overlow 34242622)
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
        BOOL bNewBehavior = FALSE;
        DWORD dwBytesReturned = 0;
        WSAIoctl(gXcpTl.Sock.sock, SIO_UDP_CONNRESET, &bNewBehavior, sizeof bNewBehavior, NULL, 0, &dwBytesReturned, NULL, NULL);

#ifdef XCPSIM_SINGLE_THREAD_SLAVE
        // Set socket to non blocking receive, uses Event triggering
        uint32_t nonBlocking = 1;
        printf("  Single thread, non blocking mode\n");
#else
        // Set socket to blocking receive 
        uint32_t nonBlocking = 0;
        printf("  Multi thread, blocking mode\n");
#endif
        if (NO_ERROR != ioctlsocket(gXcpTl.Sock.sock, FIONBIO, &nonBlocking)) {
            printf("ERROR: could not set non socket mode mode!\n");
            WSACleanup();
            return 0;
        }

        // Bind the socket to any address and the specified port
        SOCKADDR_IN a;
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY); // inet_addr(XCP_SLAVE_IP); 
        a.sin_port = htons(slavePort);
        if (bind(gXcpTl.Sock.sock, (SOCKADDR*)&a, sizeof(a)) < 0) {
            printf("ERROR: cannot bind on UDP port!\n");
            udpTlShutdown();
            return 0;
        }
        printf("  Listening on port %u\n\n", slavePort);

        // Create an event triggered by receive and send activities
#ifdef XCPSIM_SINGLE_THREAD_SLAVE
        gEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        WSAEventSelect(gXcpTl.Sock.sock, gEvent, FD_READ | FD_WRITE);
#else
        gEvent = INVALID_HANDLE_VALUE;
#endif

        return 1;

    }
}

// Wait for io or timeout after <timeout> us
void udpTlWaitForReceiveEvent(unsigned int timeout_us) {

    unsigned int timeout = timeout_us / 1000; /* ms */
    if (timeout == 0) timeout = 1;
    HANDLE event_array[1];
    event_array[0] = gEvent;
    if (WaitForMultipleObjects(1, event_array, FALSE, timeout) == WAIT_TIMEOUT) { 
    }
}

void udpTlWaitForTransmitData(unsigned int timeout_us) {

    if (gXcpTl.dto_queue_len <= 1) {
        assert(timeout_us >= 1000);
        Sleep(timeout_us/1000);
    }
    return;

}


void udpTlShutdown(void) {

#ifdef XCPSIM_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        udpShutdown(gXcpTl.Sock.sockXl);
    }
    else 
#endif
    {
        closesocket(gXcpTl.Sock.sock);
        WSACleanup();
    }
}

#endif