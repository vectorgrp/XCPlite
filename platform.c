/*----------------------------------------------------------------------------
| File:
|   platform.c
|
| Description:
|   Platform (Linux/Windows) abstraction layer
|
|   Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "clock.h"
#include "platform.h"

#ifdef _WIN // Windows needs to link with Ws2_32.lib
#pragma comment(lib, "ws2_32.lib")
#endif


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
// Sleep
/**************************************************************************/

#ifdef _LINUX // Linux

void sleepNs(uint32_t ns) {
    struct timespec timeout, timerem;
    assert(ns < 1000000000UL);
    timeout.tv_sec = 0;
    timeout.tv_nsec = (int32_t)ns;
    nanosleep(&timeout, &timerem);
}

void sleepMs(uint32_t ms) {
    struct timespec timeout, timerem;
    timeout.tv_sec = (int32_t)ms / 1000;
    timeout.tv_nsec = (int32_t)(ms % 1000) * 1000000;
    nanosleep(&timeout, &timerem);
}

#else


void sleepNs(uint32_t ns) {

    uint64_t t1, t2;
    uint32_t us = ns / 1000;
    uint32_t ms = us / 1000;

    // Start sleeping at 1800us, shorter sleeps are more precise but need significant CPU time
    if (us >= 2000) {
        Sleep(ms - 1);
    }
    // Busy wait
    else {
        t1 = t2 = clockGet64();
        uint64_t te = t1 + us * (uint64_t)CLOCK_TICKS_PER_US;
        for (;;) {
            t2 = clockGet64();
            if (t2 >= te) break;
            if (te - t2 > 0) Sleep(0);
        }
    }
}

void sleepMs(uint32_t ms) {

    Sleep(ms);
}


#endif // Windows


/**************************************************************************/
// Mutex
/**************************************************************************/

#ifdef _LINUX

void mutexInit(MUTEX* m, int recursive, uint32_t spinCount) {
    (void)spinCount;
    if (recursive) {
        pthread_mutexattr_t ma;
        pthread_mutexattr_init(&ma);
        pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(m, &ma);
    }
    else {
        pthread_mutex_init(m, NULL);
    }
}

void mutexDestroy(MUTEX* m) {

    pthread_mutex_destroy(m);
}

#else

void mutexInit(MUTEX* m, int recursive, uint32_t spinCount) {
    (void) recursive;
    // Window critical sections are always recursive
    (void)InitializeCriticalSectionAndSpinCount(m,spinCount);
}

void mutexDestroy(MUTEX* m) {

    DeleteCriticalSection(m);
}

#endif


/**************************************************************************/
// Sockets
/**************************************************************************/

#ifdef _LINUX

int socketStartup() {

    return 1;
}

void socketCleanup() {

}

int socketOpen(SOCKET* sp, BOOL useTCP, BOOL nonBlocking, BOOL reuseaddr) {
    (void)nonBlocking;
    // Create a socket
    *sp = socket(AF_INET, useTCP ?SOCK_STREAM:SOCK_DGRAM , 0);
    if (*sp < 0) {
        printf("ERROR: cannot open socket!\n");
        return 0;
    }

    if (reuseaddr) {
        int yes = 1;
        setsockopt(*sp, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    }

    return 1;
}

int socketBind(SOCKET sock, uint8_t* addr, uint16_t port) {

    // Bind the socket to any address and the specified port
    SOCKADDR_IN a;
    a.sin_family = AF_INET;
    if (addr != NULL && addr[0] != 0) {
        a.sin_addr.s_addr = *(uint32_t*)addr; // Bind to the specific addr given
    }
    else {
        a.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to any addr
    }
    a.sin_port = htons(port);
    if (bind(sock, (SOCKADDR*)&a, sizeof(a)) < 0) {
        printf("ERROR %u: cannot bind on %u.%u.%u.%u port %u!\n", socketGetLastError(), addr[0], addr[1], addr[2], addr[3], port);
        return 0;
    }

    return 1;
}


int socketShutdown(SOCKET sock) {
    if (sock != INVALID_SOCKET) {
        shutdown(sock, SHUT_RDWR);
    }
    return 1;
}

int socketClose(SOCKET *sp) {
    if (*sp != INVALID_SOCKET) {
        close(*sp);
        *sp = INVALID_SOCKET;
    }
    return 1;
}


#include <linux/if_packet.h>

static int GetMAC(char* ifname, uint8_t* mac) {
    struct ifaddrs* ifap, * ifaptr;

    if (getifaddrs(&ifap) == 0) {
        for (ifaptr = ifap; ifaptr != NULL; ifaptr = ifaptr->ifa_next) {
            if (!strcmp(ifaptr->ifa_name, ifname) && ifaptr->ifa_addr->sa_family == AF_PACKET) {
                struct sockaddr_ll* s = (struct sockaddr_ll*)ifaptr->ifa_addr;
                memcpy(mac, s->sll_addr, 6);
                break;
            }
        }
        freeifaddrs(ifap);
        return ifaptr != NULL;
    }
    return 0;
}

int socketGetLocalAddr(uint8_t* mac, uint8_t* addr) {

    static uint32_t addr1 = 0;
    static uint8_t mac1[6] = { 0,0,0,0,0,0 };

    if (addr1 != 0) {
        if (addr) memcpy(addr, &addr1, 4);
        if (mac) memcpy(mac, mac1, 6);
        return 1;
    }

    struct ifaddrs* ifaddr;
    char strbuf[100];
    struct ifaddrs* ifa1 = NULL;

    if (-1 != getifaddrs(&ifaddr)) {
        for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if ((NULL != ifa->ifa_addr) && (AF_INET == ifa->ifa_addr->sa_family)) {
                struct sockaddr_in* sa = (struct sockaddr_in*)(ifa->ifa_addr);
                if (0x100007f != sa->sin_addr.s_addr) { /* not loop back adapter (127.0.0.1) */
                    inet_ntop(AF_INET, &sa->sin_addr.s_addr, strbuf, sizeof(strbuf));
                    printf("  Network interface %s: ip=%s\n", ifa->ifa_name, strbuf);
                    if (addr1 == 0) {
                        addr1 = sa->sin_addr.s_addr;
                        ifa1 = ifa;
                    }
                }
            }
        }
        freeifaddrs(ifaddr);
    }
    if (addr1 != 0) {
        GetMAC(ifa1->ifa_name, mac1);
        if (mac) memcpy(mac, mac1, 6);
        if (addr) memcpy(addr, &addr1, 4);
        inet_ntop(AF_INET, &addr1, strbuf, sizeof(strbuf));
        printf("  Use adapter %s with ip=%s, mac=%02X-%02X-%02X-%02X-%02X-%02X for A2L info and clock UUID\n", ifa1->ifa_name, strbuf, mac1[0], mac1[1], mac1[2], mac1[3], mac1[4], mac1[5]);
        return 1;
    }
    return 0;
}




#endif // _LINUX


#ifdef _WIN

BOOL socketStartup() {

    int err;

    WORD wsaVersionRequested;
    WSADATA wsaData;

    // Init Winsock2
    wsaVersionRequested = MAKEWORD(2, 2);
    err = WSAStartup(wsaVersionRequested, &wsaData);
    if (err != 0) {
        printf("ERROR: WSAStartup failed with ERROR: %d!\n", err);
        return FALSE;
    }
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) { // Confirm that the WinSock DLL supports 2.2
        printf("ERROR: could not find a usable version of Winsock.dll!\n");
        WSACleanup();
        return FALSE;
    }

    return TRUE;
}


void socketCleanup(void) {

    WSACleanup();
}


BOOL socketOpen(SOCKET* sp, int useTCP, int nonBlocking, int reuseaddr) {

    // Create a socket
    if (!useTCP) {
        *sp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        // Avoid send to UDP nowhere problem (ignore ICMP host unreachable - server has no open socket on master port) (stack-overflow 34242622)
        #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
        BOOL bNewBehavior = FALSE;
        DWORD dwBytesReturned = 0;
        WSAIoctl(*sp, SIO_UDP_CONNRESET, &bNewBehavior, sizeof bNewBehavior, NULL, 0, &dwBytesReturned, NULL, NULL);
    }
    else {
        *sp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    }
    if (*sp == INVALID_SOCKET) {
        printf("ERROR %u: could not create socket!\n", socketGetLastError());
        return FALSE;
    }

    // Set nonblocking mode
    u_long b = nonBlocking ? 1:0;
    if (NO_ERROR != ioctlsocket(*sp, FIONBIO, &b)) {
        printf("ERROR %u: could not set non blocking mode!\n", socketGetLastError());
        return FALSE;
    }

    // Make addr reusable
    if (reuseaddr) {
        uint32_t one = 1;
        setsockopt(*sp, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    }

    return TRUE;
}


BOOL socketBind(SOCKET sock, uint8_t *addr, uint16_t port) {

    // Bind the socket to any address and the specified port
    SOCKADDR_IN a;
    a.sin_family = AF_INET;
    if (addr != NULL && addr[0] !=0) {
        a.sin_addr.s_addr = *(uint32_t*)addr; // Bind to the specific addr given
    }
    else {
        a.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to any addr
    }
    a.sin_port = htons(port);
    if (bind(sock, (SOCKADDR*)&a, sizeof(a)) < 0) {
        if (socketGetLastError() == WSAEADDRINUSE) {
            printf("ERROR: Port is already in use!\n");
        }
        else {
            printf("ERROR %u: cannot bind on port %u!\n", socketGetLastError(), port);
        }
        return FALSE;
    }
    return TRUE;
}


BOOL socketShutdown(SOCKET sock) {

    if (sock != INVALID_SOCKET) {
        shutdown(sock,SD_BOTH);
    }
    return TRUE;
}

BOOL socketClose(SOCKET* sockp) {

    if (*sockp != INVALID_SOCKET) {
        closesocket(*sockp);
        *sockp = INVALID_SOCKET;
    }
    return TRUE;
}


#include <iphlpapi.h>
#pragma comment(lib, "IPHLPAPI.lib")
#define _WINSOCK_DEPRECATED_NO_WARNINGS

BOOL socketGetLocalAddr(uint8_t* mac, uint8_t* addr) {

    static uint8_t addr1[4] = { 0,0,0,0 };
    static uint8_t mac1[6] = { 0,0,0,0,0,0 };
    uint32_t a;
    PIP_ADAPTER_INFO pAdapterInfo;
    PIP_ADAPTER_INFO pAdapter = NULL;
    DWORD dwRetVal = 0;

    if (addr1[0] == 0) {

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
                    inet_pton(AF_INET, pAdapter->IpAddressList.IpAddress.String, &a);
                    if (a!=0) {
                        printf("  Ethernet adapter %" PRIu32 ":", (uint32_t) pAdapter->Index);
                        //printf(" %s", pAdapter->AdapterName);
                        printf(" %s", pAdapter->Description);
                        printf(" %02X-%02X-%02X-%02X-%02X-%02X", pAdapter->Address[0], pAdapter->Address[1], pAdapter->Address[2], pAdapter->Address[3], pAdapter->Address[4], pAdapter->Address[5]);
                        printf(" %s", pAdapter->IpAddressList.IpAddress.String);
                        //printf(" %s", pAdapter->IpAddressList.IpMask.String);
                        //printf(" Gateway: %s", pAdapter->GatewayList.IpAddress.String);
                        //if (pAdapter->DhcpEnabled) printf(" DHCP");
                        printf("\n");
                        if (addr1[0] == 0 ) {
                            memcpy(addr1, (uint8_t*)&a, 4);
                            memcpy(mac1, pAdapter->Address, 6);
                        }
                    }
                }
                pAdapter = pAdapter->Next;
            }
        }
        if (pAdapterInfo) free(pAdapterInfo);
    }

    if (addr1[0] != 0) {
        if (mac) memcpy(mac, mac1, 6);
        if (addr) memcpy(addr, addr1, 4);
        return TRUE;
    }
    return FALSE;
}

#endif // _WIN


BOOL socketListen(SOCKET sock) {

    if (listen(sock, 5)) {
        printf("ERROR %u: listen failed!\n", socketGetLastError());
        return 0;
    }
    return 1;
}

SOCKET socketAccept(SOCKET sock, uint8_t addr[]) {

    struct sockaddr_in sa;
    socklen_t sa_size = sizeof(sa);
    SOCKET s = accept(sock, (struct sockaddr*) & sa, &sa_size);
    *(uint32_t*)addr = sa.sin_addr.s_addr;
    return s;
}


BOOL socketJoin(SOCKET sock, uint8_t* maddr) {

    struct ip_mreq group;
    group.imr_multiaddr.s_addr = *(uint32_t*)maddr;
    group.imr_interface.s_addr = htonl(INADDR_ANY);
    if (0 > setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&group, sizeof(group))) {
        printf("ERROR %u: Failed to set multicast socket option IP_ADD_MEMBERSHIP!\n", socketGetLastError());
        return 0;
    }
    return 1;
}


// Receive from a UDP socket
// Return number of bytes received, 0 when TCP socket closed or empty UDP packet received, -1 on error
int16_t socketRecvFrom(SOCKET sock, uint8_t* buffer, uint16_t bufferSize, uint8_t *addr, uint16_t *port) {

    SOCKADDR_IN src;
    socklen_t srclen = sizeof(src);
    int16_t n = (int16_t)recvfrom(sock, (char*)buffer, bufferSize, 0, (SOCKADDR*)&src, &srclen);
    if (n == 0) {
        return 0;
    }
    else if (n < 0) {
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

// Receive from TCP socket
int16_t socketRecv(SOCKET sock, uint8_t* buffer, uint16_t size) {

    int16_t n = (int16_t)recv(sock, (char*)buffer, size, MSG_WAITALL);
    if (n == 0) {
        return 0;
    }
    else if (n < 0) {
        if (socketGetLastError() == SOCKET_ERROR_WBLOCK) return 0;
        if (socketGetLastError() == SOCKET_ERROR_CLOSED) {
            printf("Socket closed\n");
            return -1; // Socket closed
        }
        printf("ERROR %u: recvfrom failed (result=%d)!\n", socketGetLastError(), n);
    }
    return n;
}


// Send datagram on socket
// Must be thread save
int16_t socketSendTo(SOCKET sock, const uint8_t* buffer, uint16_t size, const uint8_t* addr, uint16_t port) {

    SOCKADDR_IN sa;
    sa.sin_family = AF_INET;
#ifdef _WIN
    memcpy(&sa.sin_addr.S_un.S_addr, addr, 4);
#else
    memcpy(&sa.sin_addr.s_addr, addr, 4);
#endif
    sa.sin_port = htons(port);
    return (int16_t)sendto(sock, (const char*)buffer, size, 0, (SOCKADDR*)&sa, (uint16_t)sizeof(sa));
}

// Send datagram on socket
// Must be thread save
int16_t socketSend(SOCKET sock, const uint8_t* buffer, uint16_t size) {

    return (int16_t)send(sock, (const char *)buffer, size, 0);
}



