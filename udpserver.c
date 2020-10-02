/*----------------------------------------------------------------------------
| File:
|   udpserver.c
|   V1.0 23.9.2020
|
| Project:
|   XCP on UDP transport layer
|
 ----------------------------------------------------------------------------*/


#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/time.h>
#include <assert.h>

#include "udpserver.h"
#include "xcpLite.h"


#define DTO_SEND_QUEUE



 //------------------------------------------------------------------------------

static unsigned short gLastCmdCtr = 0;
static unsigned short gLastResCtr = 0;
static int gSock = 0;
static struct sockaddr_in gServerAddr, gClientAddr;
static socklen_t gClientAddrLen = 0;
static pthread_mutex_t gMutex = PTHREAD_MUTEX_INITIALIZER;



// Transmit a UDP datagramm (contains multiple XCP messages)
static int udpServerSendDatagram(unsigned int n, const unsigned char* data) {

    int r;

    if (gSock == 0 || gClientAddrLen == 0) {
        printf("Error: sendto failed !\n");
        return 0;
    }

#if defined ( XCP_ENABLE_TESTMODE )
    if (gDebugLevel >= 3) {
        printf("TX: ");
        for (int i = 0; i < n; i++) printf("%00X ", data[i]);
        printf("\n");
    }
#endif

    // Respond to last client received from, same port
    // gRemoteAddr.sin_port = htons(9001);
    r = sendto(gSock, data, n, 0, (struct sockaddr*)&gClientAddr, gClientAddrLen);
    if (r != n) {
        printf("Error: sendto failed (result=%d, errno=%d)\n", r, errno);
        return 0;
    }

    return 1;
}



//------------------------------------------------------------------------------
// XCP (UDP) transport layer packet queue (DTO buffers)

#define DTO_BUFFER_LEN XCP_UDP_MTU


#ifdef DTO_SEND_QUEUE

#define DTO_BUFFER_LEN XCP_UDP_MTU
#define DTO_QUEUE_SIZE 32

typedef struct dto_buffer {
    unsigned int size;
    unsigned int uncommited;
    unsigned char data[DTO_BUFFER_LEN];
} DTO_BUFFER;

static DTO_BUFFER dto_queue[DTO_QUEUE_SIZE];
static unsigned int dto_queue_rp; // rp = read index
static unsigned int dto_queue_len; // rp+len = write index (the next free entry), len=0 ist empty, len=DTO_QUEUE_SIZE is full
static DTO_BUFFER* dto_buffer; // current incomplete or not fully commited entry

// Not thread save
static void getDtoBuffer(void) {

    DTO_BUFFER* b;

    /* Check if there is space in the queue */
    if (dto_queue_len >= DTO_QUEUE_SIZE) {
        /* Queue overflow */
        dto_buffer = NULL;
    }
    else {
        unsigned int i = dto_queue_rp + dto_queue_len;
        if (i >= DTO_QUEUE_SIZE) i -= DTO_QUEUE_SIZE;
        b = &dto_queue[i];
        b->size = 0;
        b->uncommited = 0;
        dto_buffer = b;
        dto_queue_len++;
    }
}

// Not thread save
static void initDtoBufferQueue(void) {

    dto_queue_rp = 0;
    dto_queue_len = 0;
    getDtoBuffer();
    assert(dto_buffer);
}


//------------------------------------------------------------------------------

void udpServerHandleTransmitQueue(void) {

    DTO_BUFFER* b;

#if defined ( XCP_ENABLE_TESTMODE )
    if (gDebugLevel >= 3 && dto_queue_len>1) {
        printf("* rp = %u, len = %u\n", dto_queue_rp, dto_queue_len);
    }
#endif

    while (dto_queue_len > 1) {
     
        b = &dto_queue[dto_queue_rp];
        assert(b->size > 0);
        
        assert(dto_buffer != b); 
        assert(dto_queue_len <= DTO_QUEUE_SIZE);
        assert(dto_queue_rp <= DTO_QUEUE_SIZE);

        if (b->uncommited > 0) break; // Wait until fully commited

        // Send this frame
        udpServerSendDatagram(b->size, &b->data[0]);
        
        // Free this buffer
        pthread_mutex_lock(&gMutex);
        dto_queue_rp++;
        if (dto_queue_rp >= DTO_QUEUE_SIZE) dto_queue_rp -= DTO_QUEUE_SIZE;
        dto_queue_len--;
        pthread_mutex_unlock(&gMutex);

    }
}

//------------------------------------------------------------------------------



// Reserve space for a DTO packet in a DTO buffer and return a pointer to data and a pointer to the buffer for commit reference
// Flush the transmit buffer, if no space left
unsigned char *udpServerGetPacketBuffer(unsigned int size, void **par) {

    XCP_DTO_MESSAGE* p;

 #if defined ( XCP_ENABLE_TESTMODE )
    if (gDebugLevel >= 3) {
        printf("GetPacketBuffer(%u)\n", size);
        if (dto_buffer) {
            printf("  dto_buffer s=%u, c=%u\n", dto_buffer->size, dto_buffer->uncommited);
        }
        else {
            printf("  dto_buffer = NULL\n");
        }
    }
#endif

    pthread_mutex_lock(&gMutex);

    // Get another message buffer from queue, when active buffer ist full, overrun or after time condition
    if (dto_buffer==NULL || dto_buffer->size + size + XCP_PACKET_HEADER_SIZE > XCP_UDP_MTU ) {
        getDtoBuffer();
    }

    if (dto_buffer != NULL) {

        // Build XCP message (ctr+dlc+packet) and store in DTO buffer
        p = (XCP_DTO_MESSAGE*)&dto_buffer->data[dto_buffer->size];
        p->ctr = gLastResCtr++;
        p->dlc = (short unsigned int)size;
        dto_buffer->size += size + 4;

        *((DTO_BUFFER**)par) = dto_buffer;
        dto_buffer->uncommited++;
    }

    pthread_mutex_unlock(&gMutex);

    if (dto_buffer == NULL) return NULL;
    return &p->data[0];
}

void udpServerCommitPacketBuffer(void *par) {

    DTO_BUFFER* p = (DTO_BUFFER*)par;

    if (par != NULL) {

#if defined ( XCP_ENABLE_TESTMODE )
        if (gDebugLevel >= 3) {
            printf("CommitPacketBuffer() c=%u,s=%u\n", p->uncommited, p->size);
        }
#endif   

        pthread_mutex_lock(&gMutex);
        p->uncommited--;
        pthread_mutex_unlock(&gMutex);
    }
}

#else

unsigned int dto_buffer_size = 0;
unsigned char dto_buffer_data[DTO_BUFFER_LEN];


unsigned char* udpServerGetPacketBuffer(unsigned int size, void** par) {

    pthread_mutex_lock(&gMutex);

    if (dto_buffer_size + size + XCP_PACKET_HEADER_SIZE > XCP_UDP_MTU) {
        udpServerSendDatagram(dto_buffer_size, dto_buffer_data);
        dto_buffer_size = 0;
    }

    XCP_DTO_MESSAGE* p = (XCP_DTO_MESSAGE*)&dto_buffer_data[dto_buffer_size];
    p->ctr = gLastResCtr++;
    p->dlc = (short unsigned int)size;
    dto_buffer_size += size + 4;

    pthread_mutex_unlock(&gMutex);
    *par = p;
    return p->data;
}


void udpServerCommitPacketBuffer(void* par) {

#if defined ( XCP_ENABLE_TESTMODE )
    if (gDebugLevel >= 4) {
        XCP_DTO_MESSAGE* p = par;
        static vuint32 lastTime = 0, time = 0;
        printf("Commit CTR = %u, LEN = %u\n", p->ctr, p->dlc);
        for (int i = 0; i < p->dlc; i++) printf("%00X ", p->data[i]);
        printf("\n");
        printf(" ODT = %u,", p->data[0]);
        printf(" DAQ = %u,", p->data[1]);
        time = *(vuint32*)&p->data[2];
        printf(" TIME = %u <- %u dt=%d", time,lastTime,time-lastTime);
        lastTime = time;
        printf("\n");
    }
#endif

}




void udpServerHandleTransmitQueue(void) {}


#endif


//------------------------------------------------------------------------------

// Transmit XCP packet, copy to XCP message buffer
int udpServerSendCrmPacket(unsigned int size, const unsigned char* packet) {

    // Build XCP CTO message (ctr+dlc+packet)
    XCP_CTO_MESSAGE p;
    p.ctr = ++gLastCmdCtr;
    p.dlc = (short unsigned int)size;
    memcpy(p.data, packet, size);

#if defined ( XCP_ENABLE_TESTMODE )
    if (gDebugLevel >= 3) {
        printf("SendPacket() CTR = %04X,", p.ctr);
        printf(" LEN = %04X,", p.dlc);
        printf(" DATA = ");
        for (int i = 0; i < p.dlc; i++) printf("%00X ", p.data[i]);
        printf("\n");
    }
#endif

    return udpServerSendDatagram(size + 4, (unsigned char*)&p);
}

int udpServerHandleXCPCommands(void) {

    int n, i;
    XCP_CTO_MESSAGE buffer;

    // Receive UDP datagramm
    // Blocking with timeout 50ms
    gClientAddrLen = sizeof(gClientAddr);
    n = recvfrom(gSock, &buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr*)&gClientAddr, &gClientAddrLen);
    if (n < 0) {
        if (errno != EAGAIN) { // Socket error
#if defined ( XCP_ENABLE_TESTMODE )
            printf("recvfrom failed (result=%d,errno=%d)\n", n, errno);
            perror("error");
#endif
            return SOCK_READ_ERR;
        }
        else { // Socket timeout

        }
    }
    else if (n == 0) { // UDP datagramm with zero bytes received
#if defined ( XCP_ENABLE_TESTMODE )
        if (gDebugLevel >= 1) {
            printf("RX: 0 bytes\n");
        }
#endif
    }
    else if (n > 0) { // Socket data received
#if defined ( XCP_ENABLE_TESTMODE )
        if (gDebugLevel >= 3) {
            printf("RX: CTR %04X", buffer.ctr);
            printf(" LEN %04X", buffer.dlc);
            printf(" DATA = ");
            for (i = 0; i < buffer.dlc; i++) printf("%00X ", buffer.data[i]);
            printf("\n");
        }
#endif
        gLastCmdCtr = buffer.ctr;
        XcpCommand((const vuint32*)&buffer.data[0]);
    }

    return 0;
}


int udpServerInit(unsigned short serverPort, unsigned int socketTimeout)
{
    gLastCmdCtr = 0;
    gLastResCtr = 0;

    // Init queue
#ifdef DTO_SEND_QUEUE
    initDtoBufferQueue();
#endif

    //Create a socket
    gSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (gSock < 0) {
        perror("Cannot open socket");
        return SOCK_OPEN_ERR;
    }

    // Set socket timeout
    if (socketTimeout) {
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = (long int)socketTimeout;
        setsockopt(gSock, SOL_SOCKET, SO_RCVTIMEO, (void*)&timeout, sizeof(timeout));
    }

    // Set socket transmit buffer size
    int buffer = 2000000;
    setsockopt(gSock, SOL_SOCKET, SO_SNDBUF, (void*)&buffer, sizeof(buffer));

    // Bind the socket
    bzero((char*)&gServerAddr, sizeof(gServerAddr));
    gServerAddr.sin_family = AF_INET;
    gServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    gServerAddr.sin_port = htons(serverPort);
    memset(gServerAddr.sin_zero, '\0', sizeof(gServerAddr.sin_zero));
    if (bind(gSock, (struct sockaddr*)&gServerAddr, sizeof(gServerAddr)) < 0) {
        perror("Cannot bind on UDP port");
        shutdown(gSock, SHUT_RDWR);
        return SOCK_BIND_ERR;
    }

#if defined ( XCP_ENABLE_TESTMODE )
    if (gDebugLevel >= 1) {
        fprintf(stderr, "Bind on UDP port %d.\n", serverPort);
        fprintf(stderr, "UDP MTU = %d.\n", XCP_UDP_MTU);
    }
#endif

    // Create a mutex for packet transmission
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutex_init(&gMutex, &a);
     
    return 0;
}



int udpServerShutdown( void ) {

    shutdown(gSock, SHUT_RDWR);

    return 0;
}



