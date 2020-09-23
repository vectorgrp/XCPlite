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

#include "udpserver.h"
#include "xcpLite.h"

    
#define UDP_MTU (1500-20-8)  // IPv4 1500 ETH - 28 IP - 8 UDP, RaspberryPi does not support Jumbo Frame
static unsigned char gDTOBuffer[UDP_MTU];
static unsigned int gDTOBufferSize = 0;

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

    // Respond to last client received from, same port
    // gRemoteAddr.sin_port = htons(9001);
     r = sendto(gSock, data, n, 0, (struct sockaddr*)&gClientAddr, gClientAddrLen);
     if (r != n) {
         printf("Error: sendto failed (result=%d, errno=%d)\n", r, errno);
         return 0;
    }

    return 1;
}

// Flush the XCP message buffer
int udpServerFlush(void) {

    unsigned int n;
    int r;
    
    pthread_mutex_lock(&gMutex);

    n = gDTOBufferSize;
    if (n > 0) {
        gDTOBufferSize = 0;
        r = udpServerSendDatagram(n, gDTOBuffer);
    }
    else {
        r = 1;
    }

    pthread_mutex_unlock(&gMutex);

    return r;
}

// Transmit DTO packet, copy to XCP message buffer
int udpServerSendPacket(unsigned int size, const unsigned char* packet) {

    int n,r;

    pthread_mutex_lock(&gMutex);

    // Flush message buffer when full 
    if (gDTOBufferSize + size + XCP_PACKET_HEADER_SIZE > UDP_MTU) {
        r = udpServerSendDatagram(gDTOBufferSize, gDTOBuffer);
        gDTOBufferSize = 0;
    }
    else {
        r = 1;
    }

    // Build XCP message (ctr+dlc+packet) and store in DTO buffer
    XCP_MESSAGE* p = (XCP_MESSAGE*)&gDTOBuffer[gDTOBufferSize];
    p->ctr = gLastResCtr++;
    p->dlc = (short unsigned int)size;
    memcpy(&(p->data), packet, size);
    gDTOBufferSize += size + 4;

    pthread_mutex_unlock(&gMutex);

    if (gDebugLevel >= 2) {
        printf("TX DTO: CTR %04X,", p->ctr);
        printf(" LEN %04X,", p->dlc);
        printf(" DATA = ");
        for (int i = 0; i < p->dlc; i++) printf("%00X,", p->data[i]);
        printf("\n");
    }

    return r;
}



int udpServerInit(unsigned short serverPort)
{
    struct timeval timeout;

    gDTOBufferSize = 0;
    gLastCmdCtr = 0;
    gLastResCtr = 0;

    //Create a socket
    gSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (gSock < 0) {
        perror("Cannot open socket");
        return SOCK_OPEN_ERR;
    }

    // Set socket timeout
    timeout.tv_sec = 0;
    timeout.tv_usec = 50000; // 50ms
    setsockopt(gSock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

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

    if (gDebugLevel >= 1) {
        fprintf(stderr, "Bind on UDP port %d.\n", serverPort);
        fprintf(stderr, "UDP MTU = %d.\n", UDP_MTU);
    }

    // Create a mutex for packet transmission
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutex_init(&gMutex, &a);

    return 0;
}


int udpServerHandleXCPCommands( void ) {

    int n, i;
    XCP_MESSAGE buffer;

    // Receive UDP datagramm, non blocking with timeout set with setsockopt for background processing in this thread
    gClientAddrLen = sizeof(gClientAddr);
    n = recvfrom(gSock, &buffer, sizeof(buffer), 0 /*MSG_DONTWAIT*/, (struct sockaddr *)&gClientAddr, &gClientAddrLen);
    if (n < 0) { 
        if (errno != EAGAIN) { // Socket error
            printf("recvfrom failed (result=%d,errno=%d)\n", n, errno);
            perror("error");
            return SOCK_READ_ERR;
        }
        else { // Socket timeout
            if (gDebugLevel >= 2) {
                printf(".\n");
            }
        }
    }
    else if (n == 0) { // UDP datagramm with zero bytes received
        if (gDebugLevel >= 1) {
            printf("RX: 0 bytes\n");
        }
    }
    else if (n > 0) { // Socket data received
        if (gDebugLevel >= 2) {
            printf("RX: CTR %02X", buffer.ctr);
            printf(" LEN %02X", buffer.dlc);
            printf(" DATA = ");
            for (i = 0; i < buffer.dlc; i++) printf("%00X,", buffer.data[i]);
            //printf("\n");
        }
        gLastCmdCtr = buffer.ctr;
        XcpCommand((const vuint32*)&buffer.data[0]);
    }

    return 0;
}


int udpServerShutdown( void ) {

    shutdown(gSock, SHUT_RDWR);

    return 0;
}



