/*----------------------------------------------------------------------------
| File:
|   platform.c
|
| Description:
|   Platform (Linux/Windows) abstraction layer
|     Keyboard
|     Sleep
|     Threads
|     Mutex
|     Sockets
|     Clock
|
|   Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "main_cfg.h"
#include "platform.h"
#include "util.h"


#ifdef _WIN // Windows needs to link with Ws2_32.lib

#if OPTION_ENABLE_XLAPI_V3
#include "xl_udp.h"
#endif

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


void sleepNs(uint32_t sleep_ns) {

    uint64_t t1, t2;

    t1 = clockGet64();
    uint64_t te = t1 + (uint64_t)sleep_ns*CLOCK_TICKS_PER_NS;
    for (;;) {
        t2 = clockGet64();
        if (t2 >= te) break;
        uint32_t ms = (uint32_t)((te - t2) / CLOCK_TICKS_PER_MS);
        Sleep(0);
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
        DBG_PRINT_ERROR("ERROR: cannot open socket!\n");
        return 0;
    }

    if (reuseaddr) {
        int yes = 1;
        setsockopt(*sp, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    }

    return 1;
}

int socketBind(SOCKET sock, const uint8_t* addr, uint16_t port) {

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
        DBG_PRINTF_ERROR("ERROR %d: cannot bind on %u.%u.%u.%u port %u!\n", socketGetLastError(), addr?addr[0]:0, addr?addr[1]:0, addr? addr[2]:0, addr?addr[3]: 0, port);
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
                    DBG_PRINTF1("  Network interface %s: ip=%s\n", ifa->ifa_name, strbuf);
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
        DBG_PRINTF1("  Use adapter %s with ip=%s, mac=%02X-%02X-%02X-%02X-%02X-%02X for A2L info and clock UUID\n", ifa1->ifa_name, strbuf, mac1[0], mac1[1], mac1[2], mac1[3], mac1[4], mac1[5]);
        return 1;
    }
    return 0;
}




#endif // _LINUX


#ifdef _WIN

BOOL socketStartup() {


#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        return xlUdpSocketStartup((char*)APP_NAME);
    }
#endif


    int err;

    WORD wsaVersionRequested;
    WSADATA wsaData;

    // Init Winsock2
    wsaVersionRequested = MAKEWORD(2, 2);
    err = WSAStartup(wsaVersionRequested, &wsaData);
    if (err != 0) {
        DBG_PRINTF_ERROR("ERROR: WSAStartup failed with ERROR: %d!\n", err);
        return FALSE;
    }
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) { // Confirm that the WinSock DLL supports 2.2
        DBG_PRINT_ERROR("ERROR: could not find a usable version of Winsock.dll!\n");
        WSACleanup();
        return FALSE;
    }

    return TRUE;
}


void socketCleanup(void) {

#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        xlUdpSocketCleanup();
    }
#endif

    WSACleanup();
}


BOOL socketOpen(SOCKET* sp, int useTCP, int nonBlocking, int reuseaddr) {

#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        return xlUdpSocketOpen((XL_SOCKET*)sp, useTCP, nonBlocking, reuseaddr);
    }
#endif

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
        DBG_PRINTF_ERROR("ERROR %d: could not create socket!\n", socketGetLastError());
        return FALSE;
    }

    // Set nonblocking mode
    u_long b = nonBlocking ? 1:0;
    if (NO_ERROR != ioctlsocket(*sp, FIONBIO, &b)) {
        DBG_PRINTF_ERROR("ERROR %d: could not set non blocking mode!\n", socketGetLastError());
        return FALSE;
    }

    // Make addr reusable
    if (reuseaddr) {
        uint32_t one = 1;
        setsockopt(*sp, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    }

    return TRUE;
}


BOOL socketBind(SOCKET sock, const uint8_t *addr, uint16_t port) {

#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        return xlUdpSocketBind((XL_SOCKET)sock, gOptionXlServerNet, gOptionXlServerSeg, gOptionXlServerMac, addr, port);
    }
#endif

    // Bind the socket to any address and the specified port
    SOCKADDR_IN a;
    a.sin_family = AF_INET;
    if (addr != NULL && addr[0] !=0) { 
        a.sin_addr.s_addr = *(uint32_t*)addr; // Bind to the specific addr given
    }
    else { // NULL or 0.x.x.x
        a.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to any addr
    }
    a.sin_port = htons(port);
    if (bind(sock, (SOCKADDR*)&a, sizeof(a)) < 0) {
        if (socketGetLastError() == WSAEADDRINUSE) {
            DBG_PRINTF_ERROR("ERROR: Port is already in use!\n");
        }
        else {
            DBG_PRINTF_ERROR("ERROR %d: cannot bind on %u.%u.%u.%u port %u!\n", socketGetLastError(), addr?addr[0]:0, addr?addr[1]:0, addr?addr[2]:0, addr?addr[3]:0, port);
        }
        return FALSE;
    }
    return TRUE;
}


BOOL socketShutdown(SOCKET sock) {

#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        return xlUdpSocketShutdown(sock);
    }
#endif

    if (sock != INVALID_SOCKET) {
        shutdown(sock,SD_BOTH);
    }
    return TRUE;
}

BOOL socketClose(SOCKET* sockp) {

#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        return xlUdpSocketClose((XL_SOCKET*)sockp);
    }
#endif

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

#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        if (addr) memcpy(addr, gOptionXlServerAddr, 4);
        if (mac) memcpy(mac, gOptionXlServerMac, 6);
        return 1;
    }
#endif

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
                        DBG_PRINTF1("  Ethernet adapter %" PRIu32 ":", (uint32_t) pAdapter->Index);
                        //DBG_PRINTF1(" %s", pAdapter->AdapterName);
                        DBG_PRINTF1(" %s", pAdapter->Description);
                        DBG_PRINTF1(" %02X-%02X-%02X-%02X-%02X-%02X", pAdapter->Address[0], pAdapter->Address[1], pAdapter->Address[2], pAdapter->Address[3], pAdapter->Address[4], pAdapter->Address[5]);
                        DBG_PRINTF1(" %s", pAdapter->IpAddressList.IpAddress.String);
                        //DBG_PRINTF1(" %s", pAdapter->IpAddressList.IpMask.String);
                        //DBG_PRINTF1(" Gateway: %s", pAdapter->GatewayList.IpAddress.String);
                        //if (pAdapter->DhcpEnabled) printf(" DHCP");
                        DBG_PRINT1("\n");
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

#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        return xlUdpSocketListen(sock);
    }
#endif

    if (listen(sock, 5)) {
        DBG_PRINTF_ERROR("ERROR %d: listen failed!\n", socketGetLastError());
        return 0;
    }
    return 1;
}

SOCKET socketAccept(SOCKET sock, uint8_t addr[]) {

#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        return xlUdpSocketAccept(sock, addr);
    }
#endif

    struct sockaddr_in sa;
    socklen_t sa_size = sizeof(sa);
    SOCKET s = accept(sock, (struct sockaddr*) & sa, &sa_size);
    if (addr) *(uint32_t*)addr = sa.sin_addr.s_addr;
    return s;
}


BOOL socketJoin(SOCKET sock, uint8_t* maddr) {

#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        return xlUdpSocketJoin(sock, maddr);
    }
#endif

    struct ip_mreq group;
    group.imr_multiaddr.s_addr = *(uint32_t*)maddr;
    group.imr_interface.s_addr = htonl(INADDR_ANY);
    if (0 > setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&group, sizeof(group))) {
        DBG_PRINTF_ERROR("ERROR %d: Failed to set multicast socket option IP_ADD_MEMBERSHIP!\n", socketGetLastError());
        return 0;
    }
    return 1;
}


// Receive from socket
// Return number of bytes received, 0 when socket closed, would block or empty UDP packet received, -1 on error
int16_t socketRecvFrom(SOCKET sock, uint8_t* buffer, uint16_t bufferSize, uint8_t *addr, uint16_t *port) {

#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        return xlUdpSocketRecvFrom(sock, buffer, bufferSize, addr, port);
    }
#endif

    SOCKADDR_IN src;
    socklen_t srclen = sizeof(src);
    int16_t n = (int16_t)recvfrom(sock, (char*)buffer, bufferSize, 0, (SOCKADDR*)&src, &srclen);
    if (n == 0) {
        return 0;
    }
    else if (n < 0) {
        int32_t err = socketGetLastError();
        if (err == SOCKET_ERROR_WBLOCK) return 0;
        if (err == SOCKET_ERROR_ABORT || err == SOCKET_ERROR_RESET || err == SOCKET_ERROR_INTR) {
            return 0; // Socket closed
        }
        DBG_PRINTF_ERROR("ERROR %u: recvfrom failed (result=%d)!\n", err, n);
        return -1;
    }
    if (port) *port = htons(src.sin_port);
    if (addr) memcpy(addr, &src.sin_addr.s_addr, 4);
    return n;
}

// Receive from socket
// Return number of bytes received, 0 when socket closed, would block or empty UDP packet received, -1 on error
int16_t socketRecv(SOCKET sock, uint8_t* buffer, uint16_t size, BOOL waitAll) {

#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        assert(!waitAll);
        return xlUdpSocketRecv(sock, buffer, size);
    }
#endif

    int16_t n = (int16_t)recv(sock, (char*)buffer, size, waitAll ? MSG_WAITALL:0);
    if (n == 0) {
        return 0;
    }
    else if (n < 0) {
        int32_t err = socketGetLastError();
        if (err == SOCKET_ERROR_WBLOCK) return 0;  // Would block
        if (err == SOCKET_ERROR_ABORT || err == SOCKET_ERROR_RESET || err == SOCKET_ERROR_INTR) {
            return 0; // Socket closed
        }
        DBG_PRINTF_ERROR("ERROR %u: recvfrom failed (result=%d)!\n", err, n);
        return -1; // Error
    }
    return n;
}


// Send datagram on socket
// Must be thread save
int16_t socketSendTo(SOCKET sock, const uint8_t* buffer, uint16_t size, const uint8_t* addr, uint16_t port) {

#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        return xlUdpSocketSendTo(sock, buffer, size, addr, port);
    }
#endif

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

#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        return xlUdpSocketSend(sock, buffer, size);
    }
#endif

    return (int16_t)send(sock, (const char *)buffer, size, 0);
}



/**************************************************************************/
// Clock
/**************************************************************************/

#ifdef _LINUX // Linux

/*
Linux clock type
  CLOCK_REALTIME  This clock is affected by incremental adjustments performed by NTP
  CLOCK_TAI       This clock does not experience discontinuities and backwards jumps caused by NTP inserting leap seconds as CLOCK_REALTIME does.
                  Not available on WSL
*/
#define CLOCK_TYPE CLOCK_REALTIME
/// #define CLOCK_TYPE CLOCK_TAI

static struct timespec gtr;
#ifndef CLOCK_USE_UTC_TIME_NS
static struct timespec gts0;
#endif

char* clockGetString(char* s, uint32_t l, uint64_t c) {

#ifndef CLOCK_USE_UTC_TIME_NS
    SNPRINTF(s,l, "%gs", (double)c / CLOCK_TICKS_PER_S);
#else
    time_t t = (time_t)(c / CLOCK_TICKS_PER_S); // s since 1.1.1970
    struct tm tm;
    gmtime_r(&t, &tm);
    uint64_t fns = c % CLOCK_TICKS_PER_S;
    SNPRINTF(s,l, "%u.%u.%u %02u:%02u:%02u +%" PRIu64 "ns", 
        tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, 
        (tm.tm_hour + 2) % 24, tm.tm_min, tm.tm_sec, 
        fns);
#endif
    return s;
}


BOOL clockInit()
{
    DBG_PRINT2("\nInit clock\n  (");
#ifdef CLOCK_USE_UTC_TIME_NS
    DBG_PRINT2("CLOCK_USE_UTC_TIME_NS,");
#endif
#ifdef CLOCK_USE_APP_TIME_US
    DBG_PRINT2("CLOCK_USE_APP_TIME_US,");
#endif
#if CLOCK_TYPE == CLOCK_TAI
    DBG_PRINT2("CLOCK_TYPE_TAI,");
#endif
#if CLOCK_TYPE == CLOCK_REALTIME
    DBG_PRINT2("CLOCK_TYPE_REALTIME,");
#endif
    DBG_PRINT2(")\n");

    clock_getres(CLOCK_TYPE, &gtr);
    DBG_PRINTF2("Clock resolution is %lds,%ldns!\n", gtr.tv_sec, gtr.tv_nsec);

#ifndef CLOCK_USE_UTC_TIME_NS
    clock_gettime(CLOCK_TYPE, &gts0);
#endif
    clockGet64();

#ifdef XCP_ENABLE_DGB_PRINT
    if (DBG_LEVEL >= 2) {
        uint64_t t1, t2;
        char s[128];
        struct timespec gts_TAI;
        struct timespec gts_REALTIME;
        struct timeval ptm;
        // Print different clocks
        time_t now = time(NULL);
        gettimeofday(&ptm, NULL);
        clock_gettime(CLOCK_TAI, &gts_TAI);
        clock_gettime(CLOCK_REALTIME, &gts_REALTIME);
        DBG_PRINTF2("  CLOCK_TAI=%lus CLOCK_REALTIME=%lus time=%lu timeofday=%lu\n", gts_TAI.tv_sec, gts_REALTIME.tv_sec, now, ptm.tv_sec);
        // Check
        t1 = clockGet64();
        sleepNs(100000);
        t2 = clockGet64();
        DBG_PRINTF2("  +0us:   %s\n", clockGetString(s, sizeof(s), t1));
        DBG_PRINTF2("  +100us: %s (%u)\n", clockGetString(s, sizeof(s), t2), (uint32_t)(t2 - t1));
        DBG_PRINT2("\n");
    }
#endif

    return TRUE;
}


// Free running clock with 1us tick
uint64_t clockGet64() {

    struct timespec ts;
    clock_gettime(CLOCK_TYPE, &ts);
#ifdef CLOCK_USE_UTC_TIME_NS // ns since 1.1.1970
    return (((uint64_t)(ts.tv_sec) * 1000000000ULL) + (uint64_t)(ts.tv_nsec)); // ns
#else // us since init
    return (((uint64_t)(ts.tv_sec - gts0.tv_sec) * 1000000ULL) + (uint64_t)(ts.tv_nsec / 1000)); // us
#endif
}

#else // Windows

// Performance counter to clock conversion
static uint64_t sFactor = 0; // ticks per us
static uint8_t sDivide = 0; // divide or multiply
static uint64_t sOffset = 0; // offset

#ifdef ENABLE_DEBUG_PRINTS
char* clockGetString(char* s, uint32_t l, uint64_t c) {

#ifndef CLOCK_USE_UTC_TIME_NS
    SNPRINTF(s, l, "%gs", (double)c / CLOCK_TICKS_PER_S);
#else
    time_t t = (time_t)(c / CLOCK_TICKS_PER_S); // s
    struct tm tm;
    gmtime_s(&tm, &t);
    uint64_t fns = c % CLOCK_TICKS_PER_S;
    SNPRINTF(s, l, "%u.%u.%u %02u:%02u:%02u +%" PRIu64 "s", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, (tm.tm_hour + 2) % 24, tm.tm_min, tm.tm_sec, fns);
#endif
    return s;
}
#endif

#include <sys/timeb.h>

BOOL clockInit() {

    DBG_PRINT2("\nInit clock\n");
#ifdef CLOCK_USE_UTC_TIME_NS
    DBG_PRINT2("  CLOCK_USE_UTC_TIME_NS\n");
#endif

    // Get current performance counter frequency
    // Determine conversion to CLOCK_TICKS_PER_S -> sDivide/sFactor
    LARGE_INTEGER tF, tC;
    uint64_t tp;
    if (!QueryPerformanceFrequency(&tF)) {
        DBG_PRINT_ERROR("ERROR: Performance counter not available on this system!\n");
        return FALSE;
    }
    if (tF.u.HighPart) {
        DBG_PRINT_ERROR("ERROR: Unexpected performance counter frequency!\n");
        return FALSE;
    }
#ifndef CLOCK_USE_UTC_TIME_NS
    sFactor = tF.u.LowPart / CLOCK_TICKS_PER_S;
    sDivide = 1;
#else
    if (CLOCK_TICKS_PER_S > tF.u.LowPart) {
        sFactor = CLOCK_TICKS_PER_S / tF.u.LowPart;
        sDivide = 0;
    }
    else {
        sFactor = tF.u.LowPart / CLOCK_TICKS_PER_S;
        sDivide = 1;
    }
#endif

    // Get current performance counter to absolute time relation
#ifdef CLOCK_USE_UTC_TIME_NS

    // Set time zone from TZ environment variable. If TZ is not set, the operating system is queried
    _tzset();

    // Get current UTC time in ms since 1.1.1970
    struct _timeb tstruct;
    uint64_t t;
    uint32_t t_ms;
    _ftime(&tstruct);
    t_ms = tstruct.millitm;
    t = tstruct.time;
    //_time64(&t); // s since 1.1.1970
#endif

    // Calculate factor and offset for clockGet64/32
    QueryPerformanceCounter(&tC);
    tp = (((int64_t)tC.u.HighPart) << 32) | (int64_t)tC.u.LowPart;
#ifndef CLOCK_USE_UTC_TIME_NS
    // Reset clock now
    sOffset = tp;
#else
    // set  offset from local clock UTC value t
    // this is inaccurate up to 1 s, but irrelevant because system clock UTC offset is also not accurate
    sOffset = t * CLOCK_TICKS_PER_S + (uint64_t)t_ms * CLOCK_TICKS_PER_MS - tp * sFactor;
#endif

    clockGet64();

#ifdef ENABLE_DEBUG_PRINTS
    if (DBG_LEVEL >= 3) {
#ifdef CLOCK_USE_UTC_TIME_NS
        if (DBG_LEVEL >= 4) {
            struct tm tm;
            _gmtime64_s(&tm, (const __time64_t*)&t);
            DBG_PRINTF2("  Current time = %I64uus + %ums\n", t, t_ms);
            DBG_PRINTF2("  Zone difference in minutes from UTC: %d\n", tstruct.timezone);
            DBG_PRINTF2("  Time zone: %s\n", _tzname[0]);
            DBG_PRINTF2("  Daylight saving: %s\n", tstruct.dstflag ? "YES" : "NO");
            DBG_PRINTF2("  UTC time = %" PRIu64 "s since 1.1.1970 ", t);
            DBG_PRINTF2("  %u.%u.%u %u:%u:%u\n", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, (tm.tm_hour + 2) % 24, tm.tm_min, tm.tm_sec);
        }
#endif
        uint64_t t1, t2;
        char s[64];
        t1 = clockGet64();
        sleepNs(100000);
        t2 = clockGet64();

        DBG_PRINTF3("  Resolution = %u Hz, system resolution = %" PRIu32 " Hz, conversion = %c%" PRIu64 "+%" PRIu64 "\n", CLOCK_TICKS_PER_S, (uint32_t)tF.u.LowPart, sDivide ? '/' : '*', sFactor, sOffset);
        DBG_PRINTF4("  +0us:   %I64u  %s\n", t1, clockGetString(s, 64, t1));
        DBG_PRINTF4("  +100us: %I64u  %s\n", t2, clockGetString(s, 64, t2));
    } // Test
#endif

    return TRUE;
}


// Clock 64 Bit (UTC or ARB) 
uint64_t clockGet64() {

    LARGE_INTEGER tp;
    uint64_t t;

    QueryPerformanceCounter(&tp);
    t = (((uint64_t)tp.u.HighPart) << 32) | (uint64_t)tp.u.LowPart;
    if (sDivide) {
        t = t / sFactor + sOffset;
    }
    else {
        t = t * sFactor + sOffset;
    }

    return t;
}


#endif // Windows

