/*----------------------------------------------------------------------------
| File:
|   udpserver.c
|
| Description:
|   XCP on UDP transport layer
|   Linux (Raspberry Pi) Version
 ----------------------------------------------------------------------------*/


#include "udpserver.h"
#include "udpraw.h"
#include "xcpLite.h"

// XCP on UDP Transport Layer data
tXcpTlData gXcpTl;

pthread_mutex_t gXcpTlMutex = PTHREAD_MUTEX_INITIALIZER;


#ifdef DTO_SEND_QUEUE
static DTO_BUFFER dto_queue[DTO_QUEUE_SIZE];
static unsigned int dto_queue_rp; // rp = read index
static unsigned int dto_queue_len; // rp+len = write index (the next free entry), len=0 ist empty, len=DTO_QUEUE_SIZE is full
static DTO_BUFFER* dto_buffer_ptr; // current incomplete or not fully commited entry
#endif


// Transmit a UDP datagramm (contains multiple XCP messages)
// Must be thread safe
static int udpServerSendDatagram(const unsigned char* data, unsigned int size ) {

    int r;
        
#if defined ( XCP_ENABLE_TESTMODE )
    if (gXcpDebugLevel >= 3) {
        printf("TX: ");
        for (int i = 0; i < size; i++) printf("%00X ", data[i]);
        printf("\n");
    }
#endif

    // Respond to active client, same port    // option gRemoteAddr.sin_port = htons(9001);
    r = sendto(gXcpTl.Sock, data, size, 0, (struct sockaddr*)&gXcpTl.ClientAddr, sizeof(struct sockaddr));
    if (r != size) {
        perror("udpServerSendDatagram() sendto failed");
        fprintf(stderr,"(result=%d, errno=%d)\n", r, errno);
        return 0;
    }

    return 1;
}



//------------------------------------------------------------------------------
// XCP (UDP) transport layer packet queue (DTO buffers)




#ifdef DTO_SEND_QUEUE

// Not thread save
static void getDtoBuffer(void) {

    DTO_BUFFER* b;

    /* Check if there is space in the queue */
    if (dto_queue_len >= DTO_QUEUE_SIZE) {
        /* Queue overflow */
        dto_buffer_ptr = NULL;
    }
    else {
        unsigned int i = dto_queue_rp + dto_queue_len;
        if (i >= DTO_QUEUE_SIZE) i -= DTO_QUEUE_SIZE;
        b = &dto_queue[i];
        b->xcp_size = 0;
        b->xcp_uncommited = 0;
        dto_buffer_ptr = b;
        dto_queue_len++;
    }
}

// Not thread save
static void initDtoBufferQueue(void) {

    dto_queue_rp = 0;
    dto_queue_len = 0;
    dto_buffer_ptr = NULL;
    memset(dto_queue, 0, sizeof(dto_queue));
#ifdef DTO_SEND_RAW
    for (int i = 0; i < DTO_QUEUE_SIZE; i++) {
        udpRawInitIpHeader(&dto_queue[i].ip, &gXcpTl.ServerAddr, &gXcpTl.ClientAddr);
        udpRawInitUdpHeader(&dto_queue[i].udp, &gXcpTl.ServerAddr, &gXcpTl.ClientAddr);
    }
#endif
    getDtoBuffer();
    assert(dto_buffer_ptr);
}


//------------------------------------------------------------------------------

// Transmit all completed and fully commited UDP frames
void udpServerHandleTransmitQueue( void ) {

    DTO_BUFFER* b;

    for (;;) {

        // Check
        pthread_mutex_lock(&gXcpTlMutex);
        if (dto_queue_len > 1) {
            b = &dto_queue[dto_queue_rp];
            if (b->xcp_uncommited > 0) b = NULL; 
        }
        else {
            b = NULL;
        }
        pthread_mutex_unlock(&gXcpTlMutex);
        if (b == NULL) break;

        // Send this frame
#ifdef DTO_SEND_RAW
        udpRawSend(b, &gXcpTl.ClientAddr);
#else
        udpServerSendDatagram(&b->xcp[0], b->xcp_size);
#endif

        // Free this buffer
        pthread_mutex_lock(&gXcpTlMutex);
        dto_queue_rp++;
        if (dto_queue_rp >= DTO_QUEUE_SIZE) dto_queue_rp -= DTO_QUEUE_SIZE;
        dto_queue_len--;
        pthread_mutex_unlock(&gXcpTlMutex);

    } // for (;;)
}


// Transmit all committed DTOs
void udpServerFlushTransmitQueue(void) {
    
    // Complete the current buffer if non empty
    pthread_mutex_lock(&gXcpTlMutex);
    if (dto_buffer_ptr!=NULL && dto_buffer_ptr->xcp_size>0) getDtoBuffer();
    pthread_mutex_unlock(&gXcpTlMutex);

    udpServerHandleTransmitQueue();
}


//------------------------------------------------------------------------------



// Reserve space for a DTO packet in a DTO buffer and return a pointer to data and a pointer to the buffer for commit reference
// Flush the transmit buffer, if no space left
unsigned char *udpServerGetPacketBuffer(void **par, unsigned int size) {

    XCP_DTO_MESSAGE* p;

 #if defined ( XCP_ENABLE_TESTMODE )
    if (gXcpDebugLevel >= 3) {
        printf("GetPacketBuffer(%u)\n", size);
        if (dto_buffer_ptr) {
            printf("  dto_buffer_ptr s=%u, c=%u\n", dto_buffer_ptr->xcp_size, dto_buffer_ptr->xcp_uncommited);
        }
        else {
            printf("  dto_buffer_ptr = NULL\n");
        }
    }
#endif

    pthread_mutex_lock(&gXcpTlMutex);

    // Get another message buffer from queue, when active buffer ist full, overrun or after time condition
    if (dto_buffer_ptr==NULL || dto_buffer_ptr->xcp_size + size + XCP_MESSAGE_HEADER_SIZE > XCP_UDP_MTU ) {
        getDtoBuffer();
    }

    if (dto_buffer_ptr != NULL) {

        // Build XCP message header (ctr+dlc) and store in DTO buffer
        p = (XCP_DTO_MESSAGE*)&dto_buffer_ptr->xcp[dto_buffer_ptr->xcp_size];
        p->ctr = gXcpTl.LastResCtr++;
        p->dlc = (short unsigned int)size;
        dto_buffer_ptr->xcp_size += size + XCP_MESSAGE_HEADER_SIZE;

        *((DTO_BUFFER**)par) = dto_buffer_ptr;
        dto_buffer_ptr->xcp_uncommited++;
    }
    else {
        p = NULL; // Overflow
    }

    pthread_mutex_unlock(&gXcpTlMutex);
        
    return p!=NULL ? &p->data[0] : NULL; // return pointer to XCP message DTO data
}

void udpServerCommitPacketBuffer(void *par) {

    DTO_BUFFER* p = (DTO_BUFFER*)par;

    if (par != NULL) {

#if defined ( XCP_ENABLE_TESTMODE )
        if (gXcpDebugLevel >= 3) {
            printf("CommitPacketBuffer() c=%u,s=%u\n", p->xcp_uncommited, p->xcp_size);
        }
#endif   

        pthread_mutex_lock(&gXcpTlMutex);
        p->xcp_uncommited--;
        pthread_mutex_unlock(&gXcpTlMutex);
    }
}

#else

unsigned int dto_buffer_size = 0;
unsigned char dto_buffer_data[DTO_BUFFER_LEN];

unsigned char* udpServerGetPacketBuffer(void** par, unsigned int size) {

    pthread_mutex_lock(&gXcpTl.Mutex);

    if (dto_buffer_size + size + XCP_PACKET_HEADER_SIZE > XCP_UDP_MTU) {
        udpServerSendDatagram(dto_buffer_data, dto_buffer_size);
        dto_buffer_size = 0;
    }

    XCP_DTO_MESSAGE* p = (XCP_DTO_MESSAGE*)&dto_buffer_data[dto_buffer_size];
    p->ctr = gXcpTl.LastResCtr++;
    p->dlc = (short unsigned int)size;
    dto_buffer_size += size + XCP_MESSAGE_HEADER_SIZE;

    *par = p;
    return p->data;
}


void udpServerCommitPacketBuffer(void* par) {

    pthread_mutex_unlock(&gXcpTl.Mutex);
}

void udpServerFlushPacketBuffer(void) {

    pthread_mutex_lock(&gXcpTl.Mutex);

    if (dto_buffer_size>0) {
        udpServerSendDatagram(dto_buffer_data, dto_buffer_size);
        dto_buffer_size = 0;
    }

    pthread_mutex_unlock(&gXcpTl.Mutex);

}

#endif


//------------------------------------------------------------------------------

// Transmit XCP packet, copy to XCP message buffer
int udpServerSendCrmPacket(const unsigned char* packet, unsigned int size) {

    int r; 

    assert(packet != NULL);
    assert(size>0);

    // ToDo: Eliminate this lock, this has impact on xcpEvent runtime !!!!
    pthread_mutex_lock(&gXcpTlMutex);

    // Build XCP CTO message (ctr+dlc+packet)
    XCP_CTO_MESSAGE p;
    p.ctr = ++gXcpTl.LastCmdCtr;
    p.dlc = (short unsigned int)size;
    memcpy(p.data, packet, size);
    r = udpServerSendDatagram((unsigned char*)&p, size + XCP_MESSAGE_HEADER_SIZE);

    pthread_mutex_unlock(&gXcpTlMutex);

    return r;
}

int udpServerHandleXCPCommands(void) {

    int n, i;
    int connected;
    XCP_CTO_MESSAGE buffer;

    // Receive UDP datagramm
    // No Blocking
    struct sockaddr_in src;
    socklen_t srclen = sizeof(src);
    n = recvfrom(gXcpTl.Sock, &buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr*)&src, &srclen);
    if (n < 0) {
        if (errno != EAGAIN) { // Socket error
            fprintf(stderr, "recvfrom failed (result=%d,errno=%d)\n", n, errno);
            perror("error");
            return 0;
        }
        else { // Socket timeout

        }
    }
    else if (n == 0) { // UDP datagramm with zero bytes received
#if defined ( XCP_ENABLE_TESTMODE )
        if (gXcpDebugLevel >= 1) {
            printf("RX: 0 bytes\n");
        }
#endif
    }
    else if (n > 0) { // Socket data received

#ifdef XCP_ENABLE_TESTMODE
        if (gXcpDebugLevel >= 3) {
            printf("RX: CTR %04X", buffer.ctr);
            printf(" LEN %04X", buffer.dlc);
            printf(" DATA = ");
            for (i = 0; i < buffer.dlc; i++) printf("%00X ", buffer.data[i]);
            printf("\n");
        }
#endif
        
        gXcpTl.LastCmdCtr = buffer.ctr;
        connected = (gXcp.SessionStatus & SS_CONNECTED);
        if (connected) {
            XcpCommand((const vuint32*)&buffer.data[0]);
        }
        else {
            if (buffer.dlc == 2) { // Accept dlc=2 for CONNECT command only
                gXcpTl.ClientAddr = src;
                gXcpTl.ClientAddrValid = 1;
                XcpCommand((const vuint32*)&buffer.data[0]);
            }
        }
       

        // Connect successfull
        if (!connected && gXcp.SessionStatus & SS_CONNECTED) {

#ifdef XCP_ENABLE_TESTMODE
            if (gXcpDebugLevel >= 1) {
                unsigned char tmp[32];
                printf("XCP client connected:\n");
                inet_ntop(AF_INET, &gXcpTl.ClientAddr.sin_addr, tmp, sizeof(tmp));
                printf("  Client addr=%s, port=%u\n", tmp, ntohs(gXcpTl.ClientAddr.sin_port));
                inet_ntop(AF_INET, &gXcpTl.ServerAddr.sin_addr, tmp, sizeof(tmp));
                printf("  Server addr=%s, port=%u\n", tmp, ntohs(gXcpTl.ServerAddr.sin_port));
            }
#endif

#ifdef DTO_SEND_QUEUE
#ifdef DTO_SEND_RAW
            if (!udpRawInit(&gXcpTl.ServerAddr, &gXcpTl.ClientAddr)) {
                printf("Cannot initialize raw socket\n");
                shutdown(gRawSock, SHUT_RDWR);
                return 0;
            }
#endif
            initDtoBufferQueue(); // Build all UDP and IP headers according to server and client address 
#endif
        } // connected
    }

    return 0;
}


int udpServerInit(unsigned short serverPort, unsigned int socketTimeout)
{
    gXcpTl.LastCmdCtr = 0;
    gXcpTl.LastResCtr = 0;
    gXcpTl.ClientAddrValid = 0;
    
    //Create a socket
    gXcpTl.Sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (gXcpTl.Sock < 0) {
        perror("Cannot open socket");
        return -1;
    }

    // Set socket timeout
    if (socketTimeout) {
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = (long int)socketTimeout;
        setsockopt(gXcpTl.Sock, SOL_SOCKET, SO_RCVTIMEO, (void*)&timeout, sizeof(timeout));
    }

    // Set socket transmit buffer size
    int buffer = 2000000;
    setsockopt(gXcpTl.Sock, SOL_SOCKET, SO_SNDBUF, (void*)&buffer, sizeof(buffer));

    // Avoid "Address already in use" error message
    int yes = 1;
    setsockopt(gXcpTl.Sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    
    // Bind the socket
    gXcpTl.ServerAddr.sin_family = AF_INET;
    gXcpTl.ServerAddr.sin_addr.s_addr = htonl(INADDR_ANY); // inet_addr("172.31.31.195");
    gXcpTl.ServerAddr.sin_port = htons(serverPort);
    memset(gXcpTl.ServerAddr.sin_zero, '\0', sizeof(gXcpTl.ServerAddr.sin_zero));

    if (bind(gXcpTl.Sock, (struct sockaddr*)&gXcpTl.ServerAddr, sizeof(gXcpTl.ServerAddr)) < 0) {
        perror("Cannot bind on UDP port");
        shutdown(gXcpTl.Sock, SHUT_RDWR);
        return 0;
    }

#if defined ( XCP_ENABLE_TESTMODE )
    if (gXcpDebugLevel >= 1) {
        char tmp[32];
        inet_ntop(AF_INET, &gXcpTl.ServerAddr.sin_addr, tmp, sizeof(tmp));
        printf("Bind sin_family=%u, addr=%s, port=%u\n", gXcpTl.ServerAddr.sin_family, tmp, ntohs(gXcpTl.ServerAddr.sin_port));
        fprintf(stderr, "UDP MTU = %d.\n", XCP_UDP_MTU);
    }
#endif

    // Create a mutex needed for multithreaded event data packet transmissions
    //pthread_mutexattr_t a;
    //pthread_mutexattr_init(&a);
    //pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    //pthread_mutex_init(&gXcpTl.Mutex, &a);
    //pthread_mutex_init(&gXcpTlMutex, NULL);

    return 1;
}



int udpServerShutdown( void ) {

    pthread_mutex_destroy(&gXcpTlMutex);
    shutdown(gXcpTl.Sock, SHUT_RDWR);
    return 1;
}


#if defined ( XCP_ENABLE_TESTMODE )
void udpServerPrintPacket( XCP_DTO_MESSAGE* p ) {
   
    printf("CTR = %u, LEN = %u\n", p->ctr, p->dlc);
    for (int i = 0; i < p->dlc; i++) printf("%00X ", p->data[i]);
    printf("\n");
    printf(" ODT = %u,", p->data[0]);
    printf(" DAQ = %u,", p->data[1]);
    printf("\n");
}

#endif
