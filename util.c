/*----------------------------------------------------------------------------
| File:
|   util.c
|
| Description:
|   Some helper functions
|
|   Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "util.h"


 



/**************************************************************************/
// Keyboard
/**************************************************************************/

#ifdef _LINUX

int _getch() {
    static int ch = -1, fd = 0;
    struct termios neu, alt;
    fd = fileno(stdin);
    tcgetattr(fd, &alt);
    neu = alt;
    neu.c_lflag = (unsigned int)neu.c_lflag & ~(unsigned int)(ICANON | ECHO);
    tcsetattr(fd, TCSANOW, &neu);
    ch = getchar();
    tcsetattr(fd, TCSANOW, &alt);
    return ch;
}

int _kbhit() {
    struct termios term, oterm;
    int fd = 0;
    int c = 0;
    tcgetattr(fd, &oterm);
    memcpy(&term, &oterm, sizeof(term));
    term.c_lflag = term.c_lflag & (!ICANON);
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 1;
    tcsetattr(fd, TCSANOW, &term);
    c = getchar();
    tcsetattr(fd, TCSANOW, &oterm);
    if (c != -1)
        ungetc(c, stdin);
    return ((c != -1) ? 1 : 0);
}

#endif



/**************************************************************************/
// Sockets
/**************************************************************************/

#ifdef _LINUX

int socketOpen(SOCKET* sp, int nonBlocking, int reuseaddr) {

    // Create a socket
    *sp = socket(AF_INET, SOCK_DGRAM, 0);
    if (*sp < 0) {
        printf("ERROR: cannot open socket!\n");
        return 0;
    }

    // Set socket transmit buffer size
    // No need for large buffer if DTO queue is enabled
    /*
    unsigned int txBufferSize = XCPTL_SOCKET_BUFFER_SIZE;
    setsockopt(gXcpTl.Sock.sock, SOL_SOCKET, SO_SNDBUF, (void*)&txBufferSize, sizeof(txBufferSize));
    */

    if (reuseaddr) {
        int yes = 1;
        setsockopt(*sp, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    }

    return 1;
}

int socketBind(SOCKET sock, uint16_t port) {

    // Bind the socket to any address and the specified port
    SOCKADDR_IN a;
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(sock, (SOCKADDR*)&a, sizeof(a)) < 0) {
        printf("ERROR %u: cannot bind on UDP port %u!\n", socketGetLastError(), port);
        return 0;
    }

    return 1;
}


int socketClose(SOCKET *sp) {
    if (*sp != INVALID_SOCKET) {
        shutdown(*sp, SHUT_RDWR);
        *sp = INVALID_SOCKET;
    }
    return 1;
}

#endif


#ifdef _WIN

int socketOpen(SOCKET* sp, int nonBlocking, int reuseaddr) {
    
    // Create a socket
    *sp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (*sp == INVALID_SOCKET) {
        printf("ERROR %u: could not create socket!\n", socketGetLastError());
        return 0;
    }
    
    // Avoid send to UDP nowhere problem (server has no open socket on master port) (stack-overlow 34242622)
    #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
    BOOL bNewBehavior = FALSE;
    DWORD dwBytesReturned = 0;
    WSAIoctl(*sp, SIO_UDP_CONNRESET, &bNewBehavior, sizeof bNewBehavior, NULL, 0, &dwBytesReturned, NULL, NULL);

    // Set nonblocking mode 
    uint32_t b = nonBlocking ? 1:0;
    if (NO_ERROR != ioctlsocket(*sp, FIONBIO, &b)) {
        printf("ERROR %u: could not set non blocking mode!\n", socketGetLastError());
        return 0;
    }

    // Make addr reusable
    if (reuseaddr) {
        uint32_t one = 1;
        setsockopt(*sp, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    }

    return 1;
}


int socketBind(SOCKET sock, uint16_t port) {

    // Bind the socket to any address and the specified port
    SOCKADDR_IN a;
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(sock, (SOCKADDR*)&a, sizeof(a)) < 0) {
        if (socketGetLastError() == WSAEADDRINUSE) {
            printf("ERROR: Port is already in use!\n");
        }
        else {
            printf("ERROR %u: cannot bind on UDP port %u!\n", socketGetLastError(), port);
        }
        return 0;
    }
    return 1;
}

int socketClose(SOCKET *sock) {

    if (*sock != INVALID_SOCKET) {
        closesocket(*sock);
        *sock = INVALID_SOCKET;
    }
    return 1;
}

#endif


int socketJoin(SOCKET sock, uint8_t* addr) {

    struct ip_mreq group;
    group.imr_multiaddr.s_addr = *(uint32_t*)addr;
    group.imr_interface.s_addr = htonl(INADDR_ANY);
    if (0 > setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&group, sizeof(group))) {
        printf("ERROR %u: Failed to set multicast socket option IP_ADD_MEMBERSHIP!\n", socketGetLastError());
        return 0;
    }
    return 1;
}



int socketRecvFrom(SOCKET sock, uint8_t* buffer, uint16_t bufferSize, uint8_t *addr, uint16_t *port) {

    SOCKADDR_IN src;
    socklen_t srclen = sizeof(src);
    int n = (int)recvfrom(sock, buffer, bufferSize, 0, (SOCKADDR*)&src, &srclen);
    if (n == 0) return 0;
    if (n < 0) {
        if (socketGetLastError() == SOCKET_ERROR_WBLOCK) return 0;
        if (socketGetLastError() == SOCKET_ERROR_CLOSED) {
            printf("Socket closed\n");
            return -1; // Socket closed
        }
        printf("ERROR %u: recvfrom failed (result=%d)!\n", socketGetLastError(), n);
    }
    if (port) *port = htons(src.sin_port);
    if (addr) memcpy(addr, &src.sin_addr.s_addr, 4);
    return n;
}

int socketRecv(SOCKET sock, uint8_t* buffer, uint16_t bufferSize) {

    return socketRecvFrom(sock, buffer, bufferSize, NULL, NULL);
}


