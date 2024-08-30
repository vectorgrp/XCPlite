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
#include "platform.h"
#include "dbg_print.h"

#if defined(_WIN) // Windows // Windows needs to link with Ws2_32.lib

#pragma comment(lib, "ws2_32.lib")

#endif


/**************************************************************************/
// Keyboard
/**************************************************************************/

#ifdef _LINUX

#ifdef PLATFORM_ENABLE_KEYBOARD

#include <fcntl.h>

int _getch() {
  struct termios oldt, newt;
  int ch;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~ICANON;
  // newt.c_lflag &= ECHO; // echo
  newt.c_lflag &= ~ECHO; // no echo
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  ch = getchar();
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  return ch;
}

int _kbhit() {
  struct termios oldt, newt;
  int ch;
  int oldf;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
  ch = getchar();
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);
  if (ch != EOF) {
    ungetc(ch, stdin);
    return 1;
  }
  return 0;
}

#endif
#endif



/**************************************************************************/
// Sleep
/**************************************************************************/

#if defined(_LINUX) // Linux

void sleepNs(uint32_t ns) {
  if (ns == 0) {
    sleep(0);
  }
  else {
    struct timespec timeout, timerem;
    assert(ns < 1000000000UL);
    timeout.tv_sec = 0;
    timeout.tv_nsec = (int32_t)ns;
    nanosleep(&timeout, &timerem);
  }
}

void sleepMs(uint32_t ms) {
  if (ms == 0) {
    sleep(0);
  }
  else {
    struct timespec timeout, timerem;
    timeout.tv_sec = (int32_t)ms / 1000;
    timeout.tv_nsec = (int32_t)(ms % 1000) * 1000000;
    nanosleep(&timeout, &timerem);
  }
}

#elif defined(_WIN) // Windows

void sleepNs(uint32_t ns) {

    uint64_t t1, t2;
    uint32_t us = ns / 1000;

    // Sleep
    if (us > 1000) {
        Sleep(us/1000);
    }

    // Busy wait <= 1ms, -> CPU load !!!
    else if (us>0) {
      
        t1 = t2 = clockGet();
        uint64_t te = t1 + us * (uint64_t)CLOCK_TICKS_PER_US;
        for (;;) {
            t2 = clockGet();
            if (t2 >= te) break;
            Sleep(0);
        }
    }
    else {
      Sleep(0);
    }
}

void sleepMs(uint32_t ms) {
    if (ms > 0 && ms < 10) {  
      DBG_PRINT_WARNING("WARNING: cannot precisely sleep less than 10ms!\n");
    }
    Sleep(ms);
}



#endif // Windows


/**************************************************************************/
// Mutex
/**************************************************************************/

#if defined(_LINUX)

void mutexInit(MUTEX* m, BOOL recursive, uint32_t spinCount) {
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

#elif defined(_WIN)

void mutexInit(MUTEX* m, BOOL recursive, uint32_t spinCount) {
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

BOOL socketStartup() {
  return TRUE;
}

void socketCleanup() {
}

BOOL socketOpen(SOCKET* sp, BOOL useTCP, BOOL nonBlocking, BOOL reuseaddr, BOOL timestamps) {
    (void)nonBlocking;
    (void)timestamps;
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

    return TRUE;
}

BOOL socketBind(SOCKET sock, uint8_t* addr, uint16_t port) {

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

    return TRUE;
}


BOOL socketShutdown(SOCKET sock) {
    if (sock != INVALID_SOCKET) {
        shutdown(sock, SHUT_RDWR);
    }
    return TRUE;
}

BOOL socketClose(SOCKET *sp) {
    if (*sp != INVALID_SOCKET) {
        close(*sp);
        *sp = INVALID_SOCKET;
    }
    return TRUE;
}



#ifdef PLATFORM_ENABLE_GET_LOCAL_ADDR

#ifndef __APPLE__
#include <linux/if_packet.h>
#else
#include <ifaddrs.h>
#include <net/if_dl.h>
#endif

static BOOL GetMAC(char* ifname, uint8_t* mac) {
    struct ifaddrs *ifaddrs, *ifa;
    if (getifaddrs(&ifaddrs) == 0) {
        for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
            if (!strcmp(ifa->ifa_name, ifname)) {
#ifdef __APPLE__
                if (ifa->ifa_addr->sa_family == AF_LINK) {
                    memcpy(mac, (unsigned char *)LLADDR((struct sockaddr_dl *)ifa->ifa_addr), 6);
                    DBG_PRINTF4("  %s: MAC = %02X-%02X-%02X-%02X-%02X-%02X\n", ifa->ifa_name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                }
#else
                if (ifa->ifa_addr->sa_family == AF_PACKET) {
                    struct sockaddr_ll* s = (struct sockaddr_ll*)ifa->ifa_addr;
                    memcpy(mac, s->sll_addr, 6);
                    DBG_PRINTF4("  %s: MAC = %02X-%02X-%02X-%02X-%02X-%02X\n", ifa->ifa_name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                    break;
                }
#endif
            }
        }
        freeifaddrs(ifaddrs);
        return (ifa != NULL);
    }
    return FALSE;
}

BOOL socketGetLocalAddr(uint8_t* mac, uint8_t* addr) {
    static uint32_t addr1 = 0;
    static uint8_t mac1[6] = { 0,0,0,0,0,0 };
#ifdef DBG_LEVEL
    char strbuf[64];
#endif
    if (addr1 == 0) {
        struct ifaddrs *ifaddrs, *ifa;
        struct ifaddrs *ifa1 = NULL;
        if (-1 != getifaddrs(&ifaddrs)) {
            for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
                if ((NULL != ifa->ifa_addr) && (AF_INET == ifa->ifa_addr->sa_family)) { // IPV4
                    struct sockaddr_in* sa = (struct sockaddr_in*)(ifa->ifa_addr);
                    if (0x100007f != sa->sin_addr.s_addr) { /* not 127.0.0.1 */
                        if (addr1 == 0) {
                            addr1 = sa->sin_addr.s_addr;
                            ifa1 = ifa;
                            break;
                        }
                    }
                }
            }
            if (addr1 != 0 && ifa1!=NULL) {
                GetMAC(ifa1->ifa_name, mac1);
#ifdef DBG_LEVEL
                if (DBG_LEVEL >= 4) {
                    inet_ntop(AF_INET, &addr1, strbuf, sizeof(strbuf));
                    printf("  Use IPV4 adapter %s with IP=%s, MAC=%02X-%02X-%02X-%02X-%02X-%02X for A2L info and clock UUID\n", ifa1->ifa_name, strbuf, mac1[0], mac1[1], mac1[2], mac1[3], mac1[4], mac1[5]);
                }
#endif
            }
            freeifaddrs(ifaddrs);
        }
    }
    if (addr1 != 0) {
        if (mac) memcpy(mac, mac1, 6);
        if (addr) memcpy(addr, &addr1, 4);
        return TRUE;
    } 
    else {
        return FALSE;
    }
}



#endif // PLATFORM_ENABLE_GET_LOCAL_ADDR

#endif // _LINUX

#if defined(_WIN)

uint32_t socketGetTimestampMode(uint8_t *clockType) {

  if (clockType!=NULL) *clockType = SOCKET_TIMESTAMP_FREE_RUNNING;
  return SOCKET_TIMESTAMP_PC;
}

BOOL socketSetTimestampMode(uint8_t m) {

  if (m != SOCKET_TIMESTAMP_NONE && m != SOCKET_TIMESTAMP_PC) {
    DBG_PRINT_ERROR("ERROR: unsupported timestamp mode!\n");
    return FALSE;
  }
  return TRUE;
}

int32_t socketGetLastError() {

  return WSAGetLastError();
}


BOOL socketStartup() {

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

    WSACleanup();
}

// Create a socket, TCP or UDP
// Note: Enabling HW timestamps may have impact on throughput
BOOL socketOpen(SOCKET* sp, BOOL useTCP, BOOL nonBlocking, BOOL reuseaddr, BOOL timestamps) {

    (void)timestamps;

    // Create a socket
    if (!useTCP) {
        *sp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        // Avoid send to UDP nowhere problem (ignore ICMP host unreachable - server has no open socket on master port) (stack-overflow 34242622)
        #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
        BOOL bNewBehavior = FALSE;
        DWORD dwBytesReturned = 0;
        if (*sp != INVALID_SOCKET) {
          WSAIoctl(*sp, SIO_UDP_CONNRESET, &bNewBehavior, sizeof bNewBehavior, NULL, 0, &dwBytesReturned, NULL, NULL);
        }
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


BOOL socketBind(SOCKET sock, uint8_t *addr, uint16_t port) {

    // Bind the socket to any address and the specified port
    SOCKADDR_IN a;
    a.sin_family = AF_INET;
    if (addr != NULL && *(uint32_t*)addr !=0) { 
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

#ifdef PLATFORM_ENABLE_GET_LOCAL_ADDR

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
#ifdef DBG_LEVEL
                        DBG_PRINTF5("  Ethernet adapter %" PRIu32 ":", (uint32_t) pAdapter->Index);
                        //DBG_PRINTF5(" %s", pAdapter->AdapterName);
                        DBG_PRINTF5(" %s", pAdapter->Description);
                        DBG_PRINTF5(" %02X-%02X-%02X-%02X-%02X-%02X", pAdapter->Address[0], pAdapter->Address[1], pAdapter->Address[2], pAdapter->Address[3], pAdapter->Address[4], pAdapter->Address[5]);
                        DBG_PRINTF5(" %s", pAdapter->IpAddressList.IpAddress.String);
                        //DBG_PRINTF5(" %s", pAdapter->IpAddressList.IpMask.String);
                        //DBG_PRINTF5(" Gateway: %s", pAdapter->GatewayList.IpAddress.String);
                        //if (pAdapter->DhcpEnabled) DBG_PRINTF5(" DHCP");
                        DBG_PRINT5("\n");
#endif
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

#endif // PLATFORM_ENABLE_GET_LOCAL_ADDR


#endif // _WIN


BOOL socketListen(SOCKET sock) {

    if (listen(sock, 5)) {
        DBG_PRINTF_ERROR("ERROR %d: listen failed!\n", socketGetLastError());
        return 0;
    }
    return 1;
}

SOCKET socketAccept(SOCKET sock, uint8_t addr[]) {

    struct sockaddr_in sa;
    socklen_t sa_size = sizeof(sa);
    SOCKET s = accept(sock, (struct sockaddr*) & sa, &sa_size);
    if (addr) *(uint32_t*)addr = sa.sin_addr.s_addr;
    return s;
}


BOOL socketJoin(SOCKET sock, uint8_t* maddr) {

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
int16_t socketRecvFrom(SOCKET sock, uint8_t* buffer, uint16_t bufferSize, uint8_t *addr, uint16_t *port, uint64_t *time) {

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
    if (time!=NULL) *time = clockGet();
    if (port) *port = htons(src.sin_port);
    if (addr) memcpy(addr, &src.sin_addr.s_addr, 4);
    return n;
}

// Receive from socket
// Return number of bytes received, 0 when socket closed, would block or empty UDP packet received, -1 on error
int16_t socketRecv(SOCKET sock, uint8_t* buffer, uint16_t size, BOOL waitAll) {

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
int16_t socketSendTo(SOCKET sock, const uint8_t* buffer, uint16_t size, const uint8_t* addr, uint16_t port, uint64_t *time) {

    SOCKADDR_IN sa;
    sa.sin_family = AF_INET;
#if defined(_WIN) // Windows
    memcpy(&sa.sin_addr.S_un.S_addr, addr, 4);
#elif defined(_LINUX) // Linux
    memcpy(&sa.sin_addr.s_addr, addr, 4);
#else
#error
#endif
    sa.sin_port = htons(port);
    if (time!=NULL) *time = clockGet();
    return (int16_t)sendto(sock, (const char*)buffer, size, 0, (SOCKADDR*)&sa, (uint16_t)sizeof(sa));
}

// Send datagram on socket
// Must be thread save
int16_t socketSend(SOCKET sock, const uint8_t* buffer, uint16_t size) {

    return (int16_t)send(sock, (const char *)buffer, size, 0);
}


/**************************************************************************/
// Clock
/**************************************************************************/

static uint64_t sClock = 0;

// Get the last known clock value
// Save CPU load, clockGet may take up to 2us run time, depending on platform
// For slow timeouts and timers, it is sufficient to rely on the relatively high call frequency of clockGet() by other parts of the application
uint64_t clockGetLast() {
  return sClock;
}

#if defined(_LINUX) // Linux

/*
Linux clock type
  CLOCK_REALTIME  This clock is affected by incremental adjustments performed by NTP
  CLOCK_TAI       This clock does not experience discontinuities and backwards jumps caused by NTP inserting leap seconds as CLOCK_REALTIME does.
                  Not available on WSL
*/
#define CLOCK_TYPE CLOCK_REALTIME
// #define CLOCK_TYPE CLOCK_TAI

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
        tm.tm_hour % 24, tm.tm_min, tm.tm_sec, 
        fns);
#endif
    return s;
}


BOOL clockInit()
{
    DBG_PRINT3("\nInit clock\n  (");
#ifdef CLOCK_USE_UTC_TIME_NS
    DBG_PRINT3("CLOCK_USE_UTC_TIME_NS,");
#endif
#ifdef CLOCK_USE_APP_TIME_US
    DBG_PRINT3("CLOCK_USE_APP_TIME_US,");
#endif
#if CLOCK_TYPE == CLOCK_TAI
    DBG_PRINT3("CLOCK_TYPE_TAI,");
#endif
#if CLOCK_TYPE == CLOCK_REALTIME
    DBG_PRINT3("CLOCK_TYPE_REALTIME,");
#endif
    DBG_PRINT3(")\n");

    sClock = 0;

    clock_getres(CLOCK_TYPE, &gtr);
    DBG_PRINTF4("Clock resolution is %lds,%ldns!\n", gtr.tv_sec, gtr.tv_nsec);

#ifndef CLOCK_USE_UTC_TIME_NS
    clock_gettime(CLOCK_TYPE, &gts0);
#endif
    clockGet();

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 4) {
        uint64_t t1, t2;
        char s[128];
        struct timespec gts;
        struct timeval ptm;
        time_t now = time(NULL);
        gettimeofday(&ptm, NULL);
        clock_gettime(CLOCK_TYPE, &gts);
        DBG_PRINTF4("  CLOCK_REALTIME=%lus time=%lu timeofday=%lu\n", gts.tv_sec, now, ptm.tv_sec);
        t1 = clockGet(); sleepNs(100000); t2 = clockGet();
        DBG_PRINTF4("  +0us:   %s\n", clockGetString(s, sizeof(s), t1));
        DBG_PRINTF4("  +100us: %s (%u)\n", clockGetString(s, sizeof(s), t2), (uint32_t)(t2 - t1));
        DBG_PRINT4("\n");
    }
#endif

    return TRUE;
}


// Free running clock with 1us tick
uint64_t clockGet() {

    struct timespec ts;
    clock_gettime(CLOCK_TYPE, &ts);
#ifdef CLOCK_USE_UTC_TIME_NS // ns since 1.1.1970
    return sClock = (((uint64_t)(ts.tv_sec) * 1000000000ULL) + (uint64_t)(ts.tv_nsec)); // ns
#else // us since init
    return sClock = (((uint64_t)(ts.tv_sec - gts0.tv_sec) * 1000000ULL) + (uint64_t)(ts.tv_nsec / 1000)); // us
#endif
}

#elif defined(_WIN) // Windows

// Performance counter to clock conversion
static uint64_t sFactor = 0; // ticks per us
#ifdef CLOCK_USE_UTC_TIME_NS
static uint8_t sDivide = 0; // divide or multiply
#endif
static uint64_t sOffset = 0; // offset

char* clockGetString(char* str, uint32_t l, uint64_t c) {

#ifndef CLOCK_USE_UTC_TIME_NS
    SNPRINTF(str, l, "%gs", (double)c / CLOCK_TICKS_PER_S);
#else
    uint64_t s = c / CLOCK_TICKS_PER_S;
    uint64_t ns = c % CLOCK_TICKS_PER_S;
    if (s < 3600 * 24 * 365 * 30) { // ARB epoch
        SNPRINTF(str, l, "%" PRIu64 "d%" PRIu64 "h%" PRIu64 "m%" PRIu64 "s+%" PRIu64 "ns", s / (3600 * 24), (s % (3600 * 24)) / 3600, ((s % (3600 * 24)) % 3600) / 60, ((s % (3600 * 24)) % 3600) % 60, ns);
    }
    else { // UNIX epoch
        struct tm tm;
        time_t t = s;
        gmtime_s(&tm, &t);
        SNPRINTF(str, l, "%u.%u.%u %02u:%02u:%02u +%" PRIu64 "ns", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour % 24, tm.tm_min, tm.tm_sec, ns);
    }
#endif
    return str;
}

char* clockGetTimeString(char* str, uint32_t l, int64_t t) {

#ifndef CLOCK_USE_UTC_TIME_NS
    SNPRINTF(str, l, "%gs", (double)t/CLOCK_TICKS_PER_S);
#else
    char sign = '+';    if (t < 0) { sign = '-'; t = -t; }
    uint64_t s = t / CLOCK_TICKS_PER_S;
    uint64_t ns = t % CLOCK_TICKS_PER_S;
    SNPRINTF(str, l, "%c%" PRIu64 "d%" PRIu64 "h%" PRIu64 "m%" PRIu64 "s+%" PRIu64 "ns", sign, s / (3600 * 24), (s % (3600 * 24)) / 3600, ((s % (3600 * 24)) % 3600) / 60, ((s % (3600 * 24)) % 3600) % 60, ns);
#endif
    return str;
}

#include <sys/timeb.h>

BOOL clockInit() {

    DBG_PRINT3("\nInit clock\n");
#ifdef CLOCK_USE_UTC_TIME_NS
    DBG_PRINT3("  CLOCK_USE_UTC_TIME_NS\n");
#else
    DBG_PRINT3("  CLOCK_USE_APP_TIME_US\n");
#endif
    
    sClock = 0;

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
#ifdef CLOCK_USE_UTC_TIME_NS
    if (CLOCK_TICKS_PER_S > tF.u.LowPart) {
      sFactor = CLOCK_TICKS_PER_S / tF.u.LowPart;
      sDivide = 0;
}
    else {
      sFactor = tF.u.LowPart / CLOCK_TICKS_PER_S;
      sDivide = 1;
    }
#else
    sFactor = tF.u.LowPart / CLOCK_TICKS_PER_S;
#endif

    // Get current performance counter to absolute time relation
#ifdef CLOCK_USE_UTC_TIME_NS

    // Set time zone from TZ environment variable. If TZ is not set, the operating system is queried
    _tzset();

    // Get current UTC time in ms since 1.1.1970
    struct _timeb tstruct;
    uint64_t time_s;
    uint32_t time_ms;
    _ftime(&tstruct);
    time_ms = tstruct.millitm;
    time_s = tstruct.time;
    //_time64(&t); // s since 1.1.1970
#endif

    // Calculate factor and offset
    QueryPerformanceCounter(&tC);
    tp = (((int64_t)tC.u.HighPart) << 32) | (int64_t)tC.u.LowPart;
#ifdef CLOCK_USE_UTC_TIME_NS
    // set  offset from local clock UTC value t
    // this is inaccurate up to 1 s, but irrelevant because system clock UTC offset is also not accurate
    sOffset = time_s * CLOCK_TICKS_PER_S + (uint64_t)time_ms * CLOCK_TICKS_PER_MS - tp * sFactor;
#else
    // Reset clock now
    sOffset = tp;
#endif

    clockGet();

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 5) {
#ifdef CLOCK_USE_UTC_TIME_NS
        if (DBG_LEVEL >= 6) {
            struct tm tm;
            _gmtime64_s(&tm, (const __time64_t*)&time_s);
            printf("    Current time = %I64uus + %ums\n", time_s, time_ms);
            printf("    Zone difference in minutes from UTC: %d\n", tstruct.timezone);
            printf("    Time zone: %s\n", _tzname[0]);
            printf("    Daylight saving: %s\n", tstruct.dstflag ? "YES" : "NO");
            printf("    UTC time = %" PRIu64 "s since 1.1.1970 ", time_s);
            printf("    %u.%u.%u %u:%u:%u\n", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour % 24, tm.tm_min, tm.tm_sec);
        } 
        printf("  System clock resolution = %" PRIu32 "Hz, UTC ns conversion = %c%" PRIu64 "+%" PRIu64 "\n", (uint32_t)tF.u.LowPart, sDivide ? '/' : '*', sFactor, sOffset);
#else
      printf("  System clock resolution = %" PRIu32 "Hz, ARB us conversion = -%" PRIu64 "/%" PRIu64 "\n", (uint32_t)tF.u.LowPart, sOffset, sFactor);
#endif
        uint64_t t;
        char ts[64];
        t = clockGet();
        clockGetString(ts, sizeof(ts), t);
        printf("  Now = %I64u (%u per us) %s\n", t, CLOCK_TICKS_PER_US, ts);
    } 
#endif

    return TRUE;
}


// Clock 64 Bit (UTC or ARB) 
uint64_t clockGet() {

    LARGE_INTEGER tp;
    uint64_t t;

    QueryPerformanceCounter(&tp);
    t = (((uint64_t)tp.u.HighPart) << 32) | (uint64_t)tp.u.LowPart;
#ifdef CLOCK_USE_UTC_TIME_NS
    if (sDivide) {
        t = t / sFactor + sOffset;
    }
    else {
        t = t * sFactor + sOffset;
    }

#else
    t = (t - sOffset) / sFactor;
#endif
    sClock = t;
    return t;
}


#endif // Windows

