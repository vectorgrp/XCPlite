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

#include "main.h"
#include "xcpTl.h"


// XCP on UDP Transport Layer data
tXcpTlData gXcpTl;



// Transmit a UDP datagramm (contains multiple XCP DTO messages or a single CRM message)
// Must be thread safe, because it is called from CMD and from DAQ thread
// Returns -1 on would block, 1 if ok, 0 on error
static int sendDatagram(const unsigned char* data, unsigned int size ) {

    int r;
        
#if defined ( XCP_ENABLE_TESTMODE )
    if (gDebugLevel >= 3) {
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
#ifdef XCPSIM_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        r = (int)udpSendTo(gXcpTl.Sock.sockXl, data, size, 0, &gXcpTl.MasterAddr.addrXl, (socklen_t)sizeof(gXcpTl.MasterAddr.addrXl));
    }
    else 
#endif    
    {
        //LOCK();
        r = (int)sendto(gXcpTl.Sock.sock, data, size, SENDTO_FLAGS, (SOCKADDR*)&gXcpTl.MasterAddr.addr, (uint16_t)sizeof(gXcpTl.MasterAddr.addr));
        //UNLOCK();
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

// Not thread save, called with LOCK()
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
void udpTlInitTransmitQueue() {

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
    gXcpTl.dto_queue_cycle = gLocalClock32- gXcpTl.dto_queue_timestamp;
    gXcpTl.dto_queue_timestamp = gLocalClock32;
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
#ifdef XCPSIM_TEST_INSTRUMENTATION
        gXcpTl.dto_bytes_written += b->xcp_size;
        gXcpTl.dto_latency = gLocalClock32 - b->xcp_timestamp;
#endif
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
void udpTlFlushTransmitQueue() {

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
#ifdef XCPSIM_TEST_INSTRUMENTATION
        gXcpTl.dto_buffer_ptr->xcp_timestamp = gLocalClock32; // for testing: measure transmit latency
#endif
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



int udpTlHandleXcpCommands(int n, tXcpCtoMessage* p, tUdpSockAddr* src) {

    int connected;

    if (n >= XCPTL_TRANSPORT_LAYER_HEADER_SIZE+1) { // Valid socket data received, at least transport layer header and 1 byte

        // gXcpTl.LastCrmCtr = p->ctr;
        connected = XcpIsConnected();

#ifdef XCP_ENABLE_TESTMODE
        if (gDebugLevel >= 4 || (!connected && gDebugLevel >= 1)) {
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

            XcpCommand((const vuint32*)&p->data[0]); // Handle command
        }

        /* Not connected yet */
        else {
            /* Check for CONNECT command ? */
            const tXcpCto* pCmd = (const tXcpCto*)&p->data[0];
            if (p->dlc == 2 && CRO_CMD == CC_CONNECT) { 
                gXcpTl.MasterAddr = *src; // Save master address, so XcpCommand can send the CONNECT response
                gXcpTl.MasterAddrValid = 1;
                XcpCommand((const vuint32*)&p->data[0]); // Handle CONNECT command
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
                    char tmp[32];
                    inet_ntop(AF_INET, &gXcpTl.MasterAddr.addr.sin_addr, tmp, sizeof(tmp));
                    printf("XCP master connected: addr=%s, port=%u\n", tmp, htons(gXcpTl.MasterAddr.addr.sin_port));
                }
#endif

                // Inititialize the DAQ message queue
                udpTlInitTransmitQueue(); 

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
int udpTlHandleCommands() {

    uint8_t buffer[XCPTL_TRANSPORT_LAYER_HEADER_SIZE + XCPTL_CTO_SIZE];
    tUdpSockAddr src;
    socklen_t srclen;
    int n;

    // Receive a UDP datagramm
    // No no partial messages assumed
#ifdef XCPSIM_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        unsigned int flags;
        srclen = sizeof(src.addrXl);
        n = udpRecvFrom(gXcpTl.Sock.sockXl, (char*)&buffer, sizeof(buffer), &src.addrXl, &flags);
        if (n <= 0) {
            if (n == 0) return 1; // Ok, no command pending
            if (socketGetLastError() == SOCKET_ERROR_WBLOCK) return 1; // Ok, no command pending
            printf("ERROR %u: recvfrom failed (result=%d)!\n", socketGetLastError(), n);
           return 0; // Error
        }
    }
    else 
#endif
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

    return udpTlHandleXcpCommands(n, (tXcpCtoMessage*)buffer, &src);
}


//-------------------------------------------------------------------------------------------------------
// XCP Multicast

#ifdef XCPSIM_ENABLE_MULTICAST
#ifdef _WIN
DWORD WINAPI udpTlMulticastThread(LPVOID lpParameter)
#else
extern void* udpTlMulticastThread(void* par)
#endif
{
    uint8_t buffer[256];
    int n;
    char tmp[32];
    uint16_t cid = XcpGetClusterId();
    uint8_t cip[4] = { 239,255,(uint8_t)(cid >> 8),(uint8_t)(cid) };

    printf("Start XCP multicast thread\n");
    if (!socketOpen(&gXcpTl.MulticastSock, FALSE /*nonblocking*/, TRUE /*reusable*/)) return 0;
    if (!socketBind(gXcpTl.MulticastSock, 5557)) return 0;
    if (!socketJoin(gXcpTl.MulticastSock, cip)) return 0;
    inet_ntop(AF_INET, cip, tmp, sizeof(tmp));
    printf("  Listening on %s port=%u\n\n", tmp, 5557);
    for (;;) {
        n = socketRecv(gXcpTl.MulticastSock, (char*)&buffer, (uint16_t)sizeof(buffer));
        if (n < 0) break; // Terminate on error (socket close is used to terminate thread)
        udpTlHandleXcpCommands(n, (tXcpCtoMessage*)buffer, NULL);
    }
    printf("Terminate XCP multicast thread\n");
    socketClose(&gXcpTl.MulticastSock);
    return 0;
}

#endif




//-------------------------------------------------------------------------------------------------------

#ifdef _LINUX // Linux

uint32_t GetLocalIPAddr()
{
    struct ifaddrs* ifaddr;
    char str[100];
    uint32_t addr1 = 0;
    if (-1 != getifaddrs(&ifaddr)) {
        for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if ((NULL != ifa->ifa_addr) && (AF_INET == ifa->ifa_addr->sa_family)) {
                struct sockaddr_in* sa = (struct sockaddr_in*)(ifa->ifa_addr);
                if (0x100007f != sa->sin_addr.s_addr) /* not loop back adapter (127.0.0.1) */
                {
                    inet_ntop(AF_INET, &sa->sin_addr.s_addr, str, 100);
                    printf("  Network interface %s has IP address %s\n", ifa->ifa_name, str);
                    if (addr1==0) addr1 = sa->sin_addr.s_addr;
                }
            }
        }
        freeifaddrs(ifaddr);
    }
    return addr1;
}


int networkInit(uint8_t* slaveAddr, uint16_t slavePort) {

    uint32_t localAddr;
    char tmp[32];

    printf("Init Network\n");

    // MAC, Unicast IP-addr and port
    memset((void*)&gXcpTl.SlaveAddr.addr, 0, sizeof(gXcpTl.SlaveAddr.addr));
    localAddr = GetLocalIPAddr();
    if (localAddr == 0 || *(uint32_t*)slaveAddr != 0x0100007F) {
        memcpy(&gXcpTl.SlaveAddr.addr.sin_addr, slaveAddr, 4);
    }
    else {
        memcpy(&gXcpTl.SlaveAddr.addr.sin_addr, &localAddr, 4);
    }
    gXcpTl.SlaveAddr.addr.sin_port = htons(slavePort);

    inet_ntop(AF_INET, &gXcpTl.SlaveAddr.addr.sin_addr, tmp, sizeof(tmp));
    printf("  Unicast ip=%s port=%u\n\n", tmp, slavePort);
    return 1;
}


extern void networkShutdown() {

}


int udpTlInit(uint8_t notUsedSlaveAddr1[4], uint16_t slavePort, uint16_t slaveMTU)
{
    printf("\nInit XCP on UDP transport layer\n  (MTU=%u, DTO_QUEUE_SIZE=%u)\n", slaveMTU, XCPTL_DTO_QUEUE_SIZE);
    gXcpTl.SlaveMTU = slaveMTU;
    if (gXcpTl.SlaveMTU > XCPTL_SOCKET_JUMBO_MTU_SIZE) gXcpTl.SlaveMTU = XCPTL_SOCKET_JUMBO_MTU_SIZE;
    gXcpTl.LastCroCtr = 0;
    gXcpTl.DtoCtr = 0;
    gXcpTl.CrmCtr = 0;
    gXcpTl.MasterAddrValid = 0;

    if (!socketOpen(&gXcpTl.Sock.sock, FALSE, FALSE)) return 0;
    if (!socketBind(gXcpTl.Sock.sock, slavePort)) return 0;
    printf("  Listening on UDP port %u\n\n", slavePort);

    // Create a mutex needed for multithreaded DAQ event data packet transmissions
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&gXcpTl.Mutex, NULL);

    // Create multicast thread
#ifdef XCPSIM_ENABLE_MULTICAST
    create_thread(&gXcpTl.MulticastThreadHandle, udpTlMulticastThread);
    sleepMs(50);
#endif

    printf("\n");
    return 1;
}

// Wait for incoming io or timeout after <timeout> us
#ifdef XCPSIM_SINGLE_THREAD_SLAVE
#error "udpTlWaitForReceiveEvent() not implemented in Linux version"
#else

// Wait for outgoing data or timeout after timeout_us
void udpTlWaitForTransmitData(unsigned int timeout_us) {

    if (gXcpTl.dto_queue_len <= 1) {
        sleepNs(timeout_us * 1000);
    }
    return; 
}

#endif

void udpTlShutdown() {

#ifdef XCPSIM_ENABLE_MULTICAST
    socketClose(&gXcpTl.MulticastSock);
    sleepMs(500);
    cancel_thread(gXcpTl.MulticastThreadHandle);
#endif
    DESTROY();
    socketClose(&gXcpTl.Sock.sock);
}


#endif

#ifdef _WIN 


#include <iphlpapi.h>
#pragma comment(lib, "IPHLPAPI.lib")
#define _WINSOCK_DEPRECATED_NO_WARNINGS

uint32_t GetLocalIPAddr() {

    uint32_t addr;
    uint32_t addr1 = 0;
    uint32_t index1 = 0;
    PIP_ADAPTER_INFO pAdapterInfo;
    PIP_ADAPTER_INFO pAdapter = NULL;
    PIP_ADAPTER_INFO pAdapter1 = NULL;
    DWORD dwRetVal = 0;

    ULONG ulOutBufLen = sizeof(IP_ADAPTER_INFO);
    pAdapterInfo = (IP_ADAPTER_INFO*)malloc(sizeof(IP_ADAPTER_INFO));
    if (pAdapterInfo == NULL) return 0;

    if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
        free(pAdapterInfo);
        pAdapterInfo = (IP_ADAPTER_INFO*)malloc(ulOutBufLen);
        if (pAdapterInfo == NULL) return 0;
    }
    if ((dwRetVal = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen)) == NO_ERROR) {
        pAdapter = pAdapterInfo;
        while (pAdapter) {
            if (pAdapter->Type == MIB_IF_TYPE_ETHERNET) {
                inet_pton(AF_INET, pAdapter->IpAddressList.IpAddress.String, &addr);
                if (addr) {
                    printf("  Ethernet adapter %d:", pAdapter->Index);
                    //printf(" %s", pAdapter->AdapterName);
                    printf(" %s", pAdapter->Description);
                    //printf(" %02X-%02X-%02X-%02X-%02X-%02X", pAdapter->Address[0], pAdapter->Address[1], pAdapter->Address[2], pAdapter->Address[3], pAdapter->Address[4], pAdapter->Address[5]);
                    printf(" %s", pAdapter->IpAddressList.IpAddress.String);
                    //printf(" %s", pAdapter->IpAddressList.IpMask.String);
                    //printf(" Gateway: %s", pAdapter->GatewayList.IpAddress.String);
                    //if (pAdapter->DhcpEnabled) printf(" DHCP");
                    printf("\n");
                    if (addr1 == 0 || ((uint8_t*)&addr)[0]==172) { // prefer 172.x.x.x
                        addr1 = addr; index1 = pAdapter->Index; pAdapter1 = pAdapter;
                    }
                }
            }
            pAdapter = pAdapter->Next;
        }
    }
    else {
        return 0;
    }
    printf("  Using adapter %d ip %s for A2L\n", index1, pAdapter1->IpAddressList.IpAddress.String);
    if (pAdapterInfo) free(pAdapterInfo);
    return addr1;
}

int networkInit(unsigned char* slaveAddr, uint16_t slavePort) {

    WORD wsaVersionRequested;
    WSADATA wsaData;
    int err;
    uint32_t localAddr;
    char tmp[32];

    printf("Init Network\n");

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

#ifdef XCPSIM_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {

        // MAC, Unicast IP-addr and port
        memset((void*)&gXcpTl.SlaveAddr.addrXl, 0, sizeof(gXcpTl.SlaveAddr.addrXl));
        uint8_t mac[6] = XCPSIM_SLAVE_XL_MAC;
        memcpy(gXcpTl.SlaveAddr.addrXl.sin_mac, mac, 6);
        memcpy(gXcpTl.SlaveAddr.addrXl.sin_addr, slaveAddr, 4);
        gXcpTl.SlaveAddr.addrXl.sin_port = HTONS(slavePort);
    }
    else
#endif
    {
        // MAC, Unicast IP-addr and port
        memset((void*)&gXcpTl.SlaveAddr.addr, 0, sizeof(gXcpTl.SlaveAddr.addr));
        localAddr = GetLocalIPAddr();
        if (localAddr == 0 || *(uint32_t*)slaveAddr != 0x0100007F) {
            memcpy(&gXcpTl.SlaveAddr.addr.sin_addr, slaveAddr, 4);
        }
        else {
            memcpy(&gXcpTl.SlaveAddr.addr.sin_addr, &localAddr, 4);
        }
        gXcpTl.SlaveAddr.addr.sin_port = htons(slavePort);
    }
    inet_ntop(AF_INET, &gXcpTl.SlaveAddr.addr.sin_addr, tmp, sizeof(tmp));
    printf("  Unicast ip=%s port=%u\n\n", tmp, slavePort);
    return 1;
}


extern void networkShutdown() {

    WSACleanup();
}


int udpTlInit(uint8_t *slaveAddr, uint16_t slavePort, uint16_t slaveMTU) {

    printf("\nInit XCP on UDP transport layer\n  (MTU=%u, DTO_QUEUE_SIZE=%u)\n", slaveMTU, XCPTL_DTO_QUEUE_SIZE);

    gXcpTl.SlaveMTU = slaveMTU;
    if (gXcpTl.SlaveMTU > XCPTL_SOCKET_JUMBO_MTU_SIZE) gXcpTl.SlaveMTU = XCPTL_SOCKET_JUMBO_MTU_SIZE;
    gXcpTl.LastCroCtr = 0;
    gXcpTl.DtoCtr = 0;
    gXcpTl.CrmCtr = 0;
    gXcpTl.MasterAddrValid = 0;

    // Create a critical section needed for multithreaded event data packet transmissions
    InitializeCriticalSection(&gXcpTl.CriticalSection);

#ifdef XCPSIM_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {     

        // XCP multicast IP-addr and port
        uint16_t cid = XcpGetClusterId();
        uint8_t cip[4] = { 239,255,(uint8_t)(cid >> 8),(uint8_t)(cid) };
        memset((void*)&gXcpTl.MulticastAddrXl.addrXl, 0, sizeof(gXcpTl.MulticastAddrXl.addrXl));
        memcpy(gXcpTl.MulticastAddrXl.addrXl.sin_addr, cip, 4);
        gXcpTl.MulticastAddrXl.addrXl.sin_port = HTONS(5557);

        // Create a UDP socket
        return udpInit(&gXcpTl.Sock.sockXl, &gXcpTl.Event, &gXcpTl.SlaveAddr.addrXl, &gXcpTl.MulticastAddrXl.addrXl);
    }
    else 
#endif
    {        
        
#ifdef XCPSIM_SINGLE_THREAD_SLAVE
        // Set socket to non blocking receive, uses Event triggering
        if (!socketOpen(&gXcpTl.Sock.sock, TRUE)) return 0;
        printf("  Single thread, non blocking mode\n");
#else
        // Set socket to blocking receive 
        if (!socketOpen(&gXcpTl.Sock.sock, FALSE, FALSE)) return 0;
        printf("  Multi thread, blocking mode\n");
#endif
        if (!socketBind(gXcpTl.Sock.sock,slavePort)) return 0;
        printf("  Listening on UDP port %u\n\n", slavePort);

        // Create an event triggered by receive and send activities
#ifdef XCPSIM_SINGLE_THREAD_SLAVE
        gEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        WSAEventSelect(gXcpTl.Sock.sock, gEvent, FD_READ | FD_WRITE);
#else
        gXcpTl.Event = INVALID_HANDLE_VALUE;
#endif

#ifdef XCPSIM_ENABLE_MULTICAST
#ifdef XCPSIM_ENABLE_XLAPI_V3
        if (!gOptionUseXLAPI) {
            create_thread(&gXcpTl.MulticastThreadHandle, udpTlMulticastThread);
        }
#endif
#endif
        return 1;
    }
}

// Wait for io or timeout after <timeout> us
void udpTlWaitForReceiveEvent(unsigned int timeout_us) {

    unsigned int timeout = timeout_us / 1000; /* ms */
    if (timeout == 0) timeout = 1;
    HANDLE event_array[1];
    event_array[0] = gXcpTl.Event;
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


void udpTlShutdown() {

#ifdef XCPSIM_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        udpShutdown(gXcpTl.Sock.sockXl);
    }
    else 
#endif
    {
#ifdef XCPSIM_ENABLE_MULTICAST
        socketClose(&gXcpTl.MulticastSock);
        sleepMs(500);
        cancel_thread(gXcpTl.MulticastThreadHandle);
#endif
        socketClose(&gXcpTl.Sock.sock);
    }
}

#endif


