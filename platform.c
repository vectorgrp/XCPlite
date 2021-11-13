/*----------------------------------------------------------------------------
| File:
|   platform.c
|
| Description:
|   Platform (Linux/Windows) abstraction layer
|
|   Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "platform.h"


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
// Mutex
/**************************************************************************/

#ifdef _LINUX

void mutexInit(MUTEX* m, int recursive, uint32_t spinCount) {

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
    // Window critical sections are always recursive
    InitializeCriticalSectionAndSpinCount(m,spinCount);
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

void socketShutdown() {

}

int socketOpen(SOCKET* sp, int nonBlocking, int reuseaddr) {

    // Create a socket
    *sp = socket(AF_INET, SOCK_DGRAM, 0);
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

int socketStartup() {

    int err;

    WORD wsaVersionRequested;
    WSADATA wsaData;

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

    return 1;
}


void socketShutdown(void) {

    WSACleanup();
}


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


int socketBind(SOCKET sock, uint8_t *addr, uint16_t port) {

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


#include <iphlpapi.h>
#pragma comment(lib, "IPHLPAPI.lib")
#define _WINSOCK_DEPRECATED_NO_WARNINGS

int socketGetLocalAddr(uint8_t* mac, uint8_t* addr) {

    static uint8_t addr1[4] = { 0,0,0,0 };
    static uint8_t mac1[6] = { 0,0,0,0,0,0 };
    uint32_t index1 = 0;
    PIP_ADAPTER_INFO pAdapter1 = NULL;
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
                        printf("  Ethernet adapter %d:", pAdapter->Index);
                        //printf(" %s", pAdapter->AdapterName);
                        printf(" %s", pAdapter->Description);
                        printf(" %02X-%02X-%02X-%02X-%02X-%02X", pAdapter->Address[0], pAdapter->Address[1], pAdapter->Address[2], pAdapter->Address[3], pAdapter->Address[4], pAdapter->Address[5]);
                        printf(" %s", pAdapter->IpAddressList.IpAddress.String);
                        //printf(" %s", pAdapter->IpAddressList.IpMask.String);
                        //printf(" Gateway: %s", pAdapter->GatewayList.IpAddress.String);
                        //if (pAdapter->DhcpEnabled) printf(" DHCP");
                        printf("\n");
                        if (addr1[0] == 0 ) { 
                            index1 = pAdapter->Index;
                            memcpy(addr1, (uint8_t*)&a, 4);
                            memcpy(mac1, pAdapter->Address, 6);
                            pAdapter1 = pAdapter;
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
        return 1;
    }
    return 0;
}

#endif // _WIN


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



int16_t socketRecvFrom(SOCKET sock, uint8_t* buffer, uint16_t bufferSize, uint8_t *addr, uint16_t *port) {

    SOCKADDR_IN src;
    socklen_t srclen = sizeof(src);
    int16_t n = (int16_t)recvfrom(sock, buffer, bufferSize, 0, (SOCKADDR*)&src, &srclen);
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

int16_t socketRecv(SOCKET sock, uint8_t* buffer, uint16_t bufferSize) {

    return socketRecvFrom(sock, buffer, bufferSize, NULL, NULL);
}



int socketSendTo(SOCKET sock, const uint8_t* buffer, uint16_t bufferSize, const uint8_t* addr, uint16_t port) {

    SOCKADDR_IN sa;
    sa.sin_family = AF_INET;
#ifdef _WIN
    memcpy(&sa.sin_addr.S_un.S_addr, addr, 4);
#else
    memcpy(&sa.sin_addr.s_addr, addr, 4);
#endif
    sa.sin_port = htons(port);
    return (int)sendto(sock, buffer, bufferSize, 0, (SOCKADDR*)&sa, (uint16_t)sizeof(sa));
}



