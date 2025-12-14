/*----------------------------------------------------------------------------
| File:
|   platform.c
|
| Description:
|   Platform (Linux/Windows/MACOS/QNX) abstraction layer
|     Atomics
|     Sleep
|     Threads
|     Mutex
|     Sockets
|     Clock
|     Keyboard
|
|   Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "platform.h"

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "main_cfg.h"  // for OPTION_xxx ...
#include <inttypes.h>  // for PRIu64

/**************************************************************************/
// Winsock
/**************************************************************************/

#if defined(_WIN) // Windows // Windows needs to link with Ws2_32.lib

#pragma comment(lib, "ws2_32.lib")

#endif

/**************************************************************************/
// Keyboard
/**************************************************************************/

#if !defined(_WIN) // Non-Windows platforms

#ifdef PLATFORM_ENABLE_KEYBOARD

#include <fcntl.h>

int _getch(void) {
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

int _kbhit(void) {
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

#if !defined(_WIN)

#include <time.h>   // for timespec, nanosleep, CLOCK_MONOTONIC_RAW
#include <unistd.h> // for sleep

void sleepUs(uint32_t us) {
    if (us == 0) {
        sleep(0);
    } else {
        struct timespec timeout, timerem;
        assert(us < 1000000UL);
        timeout.tv_sec = 0;
        timeout.tv_nsec = (int32_t)us * 1000;
        nanosleep(&timeout, &timerem);
    }
}

void sleepMs(uint32_t ms) {
    if (ms == 0) {
        sleep(0);
    } else {
        struct timespec timeout, timerem;
        timeout.tv_sec = (int32_t)ms / 1000;
        timeout.tv_nsec = (int32_t)(ms % 1000) * 1000000;
        nanosleep(&timeout, &timerem);
    }
}

#else // Windows

void sleepUs(uint32_t us) {

    uint64_t t1, t2;

    // Sleep
    if (us > 1000) {
        Sleep(us / 1000);
    }

    // Busy wait <= 1ms, -> CPU load !!!
    else if (us > 0) {

        t1 = t2 = clockGet();
        uint64_t te = t1 + us * (uint64_t)CLOCK_TICKS_PER_US;
        for (;;) {
            t2 = clockGet();
            if (t2 >= te)
                break;
            Sleep(0);
        }
    } else {
        Sleep(0);
    }
}

void sleepMs(uint32_t ms) {
    if (ms > 0 && ms < 10) {
        // DBG_PRINT_WARNING("cannot precisely sleep less than 10ms!\n");
    }
    Sleep(ms);
}

#endif // Windows

/**************************************************************************/
// Atomics
/**************************************************************************/

// stdatomic emulation for Windows
#ifdef OPTION_ATOMIC_EMULATION

MUTEX gWinMutex;

uint64_t atomic_exchange_explicit(uint64_t *a, uint64_t b, int c) {
    (void)c;
    mutexLock(&gWinMutex);
    uint64_t old_value = *a;
    *a = b;
    mutexUnlock(&gWinMutex);
    return old_value;
}

uint64_t atomic_fetch_add_explicit(uint64_t *a, uint64_t b, int c) {
    (void)c;
    mutexLock(&gWinMutex);
    uint64_t old_value = *a;
    *a += b;
    mutexUnlock(&gWinMutex);
    return old_value;
}

uint64_t atomic_fetch_sub_explicit(uint64_t *a, uint64_t b, int c) {
    (void)c;
    mutexLock(&gWinMutex);
    uint64_t old_value = *a;
    *a -= b;
    mutexUnlock(&gWinMutex);
    return old_value;
}

bool atomic_compare_exchange_weak_explicit(uint64_t *a, uint64_t *b, uint64_t c, int d, int e) {
    (void)d;
    (void)e;
    bool res;
    mutexLock(&gWinMutex);
    uint64_t old_value = *a;
    if (old_value == *b) {
        *a = c;
        res = true;
    } else {
        *b = old_value;
        res = false;
    }
    mutexUnlock(&gWinMutex);
    return res;
}

bool atomic_compare_exchange_strong_explicit(uint64_t *a, uint64_t *b, uint64_t c, int d, int e) {
    (void)d;
    (void)e;
    bool res;
    mutexLock(&gWinMutex);
    uint64_t old_value = *a;
    if (old_value == *b) {
        *a = c;
        res = true;
    } else {
        *b = old_value;
        res = false;
    }
    mutexUnlock(&gWinMutex);
    return res;
}

#endif // OPTION_ATOMIC_EMULATION

/**************************************************************************/
// Mutex
/**************************************************************************/

#if !defined(_WIN) // Non-Windows platforms

void mutexInit(MUTEX *m, bool recursive, uint32_t spinCount) {
    (void)spinCount;
    if (recursive) {
        pthread_mutexattr_t ma;
        pthread_mutexattr_init(&ma);
        pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(m, &ma);
    } else {
        pthread_mutex_init(m, NULL);
    }
}

void mutexDestroy(MUTEX *m) { pthread_mutex_destroy(m); }

#else // Windows

void mutexInit(MUTEX *m, bool recursive, uint32_t spinCount) {
    (void)recursive;
    // Window critical sections are always recursive
    (void)InitializeCriticalSectionAndSpinCount(m, spinCount);
}

void mutexDestroy(MUTEX *m) { DeleteCriticalSection(m); }

#endif

/**************************************************************************/
// Sockets
/**************************************************************************/

#if defined(OPTION_ENABLE_TCP) || defined(OPTION_ENABLE_UDP)

#if !defined(_WIN) // Non-Windows platforms

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
#include <linux/errqueue.h>
#include <linux/net_tstamp.h>
#endif

bool socketStartup(void) { return true; }

void socketCleanup(void) {}

bool socketOpen(SOCKET *sp, bool useTCP, bool nonBlocking, bool reuseaddr, bool timestamps) {
    (void)nonBlocking;

    // Create a socket
    *sp = socket(AF_INET, useTCP ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (*sp < 0) {
        DBG_PRINT_ERROR("cannot open socket!\n");
        return 0;
    }

    if (reuseaddr) {
        int yes = 1;
        setsockopt(*sp, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    }

    // Enable timestamps if requested
    if (timestamps) {
#if defined(OPTION_SOCKET_HW_TIMESTAMPS)
#if defined(_LINUX)
        int yes = 1;
        // Enable RX timestamping - Linux uses SO_TIMESTAMPNS which provides struct timespec (nanosecond precision)
        if (setsockopt(*sp, SOL_SOCKET, SO_TIMESTAMPNS, &yes, sizeof(yes)) < 0) {
            DBG_PRINTF_ERROR("WARNING: Failed to enable SO_TIMESTAMPNS (errno=%d), using system time instead\n", errno);
        } else {
            DBG_PRINT3("SO_TIMESTAMPNS enabled successfully\n");
        }

        // Enable TX timestamping - timestamps will be available via error queue after send
        uint32_t flags = SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_TX_SOFTWARE | SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE |
                         SOF_TIMESTAMPING_RAW_HARDWARE;
        if (setsockopt(*sp, SOL_SOCKET, SO_TIMESTAMPING, &flags, sizeof(flags)) < 0) {
            DBG_PRINTF_ERROR("WARNING: Failed to enable SO_TIMESTAMPING (errno=%d), TX timestamps not available\n", errno);
        } else {
            DBG_PRINT3("SO_TIMESTAMPING enabled successfully for TX timestamps\n");
        }
#else
        DBG_PRINT_ERROR("Hardware timestamps not supported on this platform, using system time instead\n");
#endif
#endif
    }

    return true;
}

bool socketBind(SOCKET sock, uint8_t *addr, uint16_t port) {

    // Bind the socket to any address and the specified port
    SOCKADDR_IN a;
    a.sin_family = AF_INET;
    if (addr != NULL && addr[0] != 0) {
        a.sin_addr.s_addr = *(uint32_t *)addr; // Bind to the specific addr given
    } else {
        a.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to any addr
    }
    a.sin_port = htons(port);
    if (bind(sock, (SOCKADDR *)&a, sizeof(a)) < 0) {
        DBG_PRINTF_ERROR("%d - cannot bind on %u.%u.%u.%u port %u!\n", socketGetLastError(), addr ? addr[0] : 0, addr ? addr[1] : 0, addr ? addr[2] : 0, addr ? addr[3] : 0, port);
        return 0;
    }

    return true;
}

// Shutdown socket
// Block rx and tx direction
bool socketShutdown(SOCKET sock) {
    if (sock != INVALID_SOCKET) {
        shutdown(sock, SHUT_RDWR);
    }
    return true;
}

// Close socket
// Make addr reusable
bool socketClose(SOCKET *sp) {
    if (*sp != INVALID_SOCKET) {
        close(*sp);
        *sp = INVALID_SOCKET;
    }
    return true;
}

#ifdef OPTION_ENABLE_GET_LOCAL_ADDR

#if !defined(_MACOS) && !defined(_QNX)
#include <linux/if_packet.h>
#else
#include <ifaddrs.h>
#include <net/if_dl.h>
#endif
#if !defined(_WIN) // Non-Windows platforms
#include <ifaddrs.h>
#endif

static bool GetMAC(char *ifname, uint8_t *mac) {
    struct ifaddrs *ifaddrs, *ifa;
    if (getifaddrs(&ifaddrs) == 0) {
        for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
            if (!strcmp(ifa->ifa_name, ifname)) {
#if defined(_MACOS) || defined(_QNX)
                if (ifa->ifa_addr->sa_family == AF_LINK) {
                    memcpy(mac, (uint8_t *)LLADDR((struct sockaddr_dl *)ifa->ifa_addr), 6);
                    DBG_PRINTF4("  %s: MAC = %02X-%02X-%02X-%02X-%02X-%02X\n", ifa->ifa_name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                }
#else
                if (ifa->ifa_addr->sa_family == AF_PACKET) {
                    struct sockaddr_ll *s = (struct sockaddr_ll *)ifa->ifa_addr;
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
    return false;
}

bool socketGetLocalAddr(uint8_t *mac, uint8_t *addr) {
    static uint32_t __addr1 = 0;
    static uint8_t __mac1[6] = {0, 0, 0, 0, 0, 0};
#ifdef DBG_LEVEL
    char strbuf[64];
#endif
    if (__addr1 == 0) {
        struct ifaddrs *ifaddrs, *ifa;
        struct ifaddrs *ifa1 = NULL;
        if (-1 != getifaddrs(&ifaddrs)) {
            for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
                if ((NULL != ifa->ifa_addr) && (AF_INET == ifa->ifa_addr->sa_family)) { // IPV4
                    struct sockaddr_in *sa = (struct sockaddr_in *)(ifa->ifa_addr);
                    if (0x100007f != sa->sin_addr.s_addr) { /* not 127.0.0.1 */
                        if (__addr1 == 0) {
                            __addr1 = sa->sin_addr.s_addr;
                            ifa1 = ifa;
                            break;
                        }
                    }
                }
            }
            if (__addr1 != 0 && ifa1 != NULL) {
                GetMAC(ifa1->ifa_name, __mac1);
#ifdef DBG_LEVEL
                if (DBG_LEVEL >= 5) {
                    inet_ntop(AF_INET, &__addr1, strbuf, sizeof(strbuf));
                    printf("  Use IPV4 adapter %s with IP=%s, MAC=%02X-%02X-%02X-%02X-%02X-%02X for A2L info and clock "
                           "UUID\n",
                           ifa1->ifa_name, strbuf, __mac1[0], __mac1[1], __mac1[2], __mac1[3], __mac1[4], __mac1[5]);
                }
#endif
            }
            freeifaddrs(ifaddrs);
        }
    }
    if (__addr1 != 0) {
        if (mac)
            memcpy(mac, __mac1, 6);
        if (addr)
            memcpy(addr, &__addr1, 4);
        return true;
    } else {
        return false;
    }
}

#endif // OPTION_ENABLE_GET_LOCAL_ADDR

#else

int32_t socketGetLastError(void) { return WSAGetLastError(); }

bool socketStartup(void) {

    int err;
    WORD wsaVersionRequested;
    WSADATA wsaData;

    // @@@@ TODO: Workaround for Windows
    mutexInit(&gWinMutex, false, 1000);

    // Init Winsock2
    wsaVersionRequested = MAKEWORD(2, 2);
    err = WSAStartup(wsaVersionRequested, &wsaData);
    if (err != 0) {
        DBG_PRINTF_ERROR("WSAStartup failed with error %d!\n", err);
        return false;
    }
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) { // Confirm that the WinSock DLL supports 2.2
        DBG_PRINT_ERROR("Could not find a usable version of Winsock.dll!\n");
        WSACleanup();
        return false;
    }

    return true;
}

void socketCleanup(void) { WSACleanup(); }

// Create a socket, TCP or UDP
// Note: Enabling HW timestamps may have impact on throughput
bool socketOpen(SOCKET *sp, bool useTCP, bool nonBlocking, bool reuseaddr, bool timestamps) {

    (void)timestamps;

    // Create a socket
    if (!useTCP) {
        *sp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

// Avoid send to UDP nowhere problem (ignore ICMP host unreachable - server has no open socket on master port)
// (stack-overflow 34242622)
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
        bool bNewBehavior = false;
        DWORD dwBytesReturned = 0;
        if (*sp != INVALID_SOCKET) {
            WSAIoctl(*sp, SIO_UDP_CONNRESET, &bNewBehavior, sizeof bNewBehavior, NULL, 0, &dwBytesReturned, NULL, NULL);
        }
    } else {
        *sp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    }
    if (*sp == INVALID_SOCKET) {
        DBG_PRINTF_ERROR("%d - could not create socket!\n", socketGetLastError());
        return false;
    }

    // Set nonblocking mode
    u_long b = nonBlocking ? 1 : 0;
    if (NO_ERROR != ioctlsocket(*sp, FIONBIO, &b)) {
        DBG_PRINTF_ERROR("%d - could not set non blocking mode!\n", socketGetLastError());
        return false;
    }

    // Make addr reusable
    if (reuseaddr) {
        uint32_t one = 1;
        setsockopt(*sp, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    }

    return true;
}

bool socketBind(SOCKET sock, uint8_t *addr, uint16_t port) {

    // Bind the socket to any address and the specified port
    SOCKADDR_IN a;
    a.sin_family = AF_INET;
    if (addr != NULL && *(uint32_t *)addr != 0) {
        a.sin_addr.s_addr = *(uint32_t *)addr; // Bind to the specific addr given
    } else {                                   // NULL or 0.x.x.x
        a.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to any addr
    }
    a.sin_port = htons(port);
    if (bind(sock, (SOCKADDR *)&a, sizeof(a)) < 0) {
        if (socketGetLastError() == WSAEADDRINUSE) {
            DBG_PRINT_ERROR("Port is already in use!\n");
        } else {
            DBG_PRINTF_ERROR("%d - cannot bind on %u.%u.%u.%u port %u!\n", socketGetLastError(), addr ? addr[0] : 0, addr ? addr[1] : 0, addr ? addr[2] : 0, addr ? addr[3] : 0,
                             port);
        }
        return false;
    }
    return true;
}

// Shutdown socket
// Block rx and tx direction
bool socketShutdown(SOCKET sock) {

    if (sock != INVALID_SOCKET) {
        shutdown(sock, SD_BOTH);
    }
    return true;
}

// Close socket
// Make addr reusable
bool socketClose(SOCKET *sockp) {

    if (*sockp != INVALID_SOCKET) {
        closesocket(*sockp);
        *sockp = INVALID_SOCKET;
    }
    return true;
}

#ifdef OPTION_ENABLE_GET_LOCAL_ADDR

#include <iphlpapi.h>
#pragma comment(lib, "IPHLPAPI.lib")
#define _WINSOCK_DEPRECATED_NO_WARNINGS

bool socketGetLocalAddr(uint8_t *mac, uint8_t *addr) {

    static uint8_t __addr1[4] = {0, 0, 0, 0};
    static uint8_t __mac1[6] = {0, 0, 0, 0, 0, 0};
    uint32_t a;
    PIP_ADAPTER_INFO pAdapterInfo;
    PIP_ADAPTER_INFO pAdapter = NULL;
    DWORD dwRetVal = 0;

    if (__addr1[0] == 0) {

        ULONG ulOutBufLen = sizeof(IP_ADAPTER_INFO);
        pAdapterInfo = (IP_ADAPTER_INFO *)malloc(sizeof(IP_ADAPTER_INFO));
        if (pAdapterInfo == NULL)
            return 0;

        if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
            free(pAdapterInfo);
            pAdapterInfo = (IP_ADAPTER_INFO *)malloc(ulOutBufLen);
            if (pAdapterInfo == NULL)
                return 0;
        }
        if ((dwRetVal = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen)) == NO_ERROR) {
            pAdapter = pAdapterInfo;
            while (pAdapter) {
                if (pAdapter->Type == MIB_IF_TYPE_ETHERNET) {
                    inet_pton(AF_INET, pAdapter->IpAddressList.IpAddress.String, &a);
                    if (a != 0) {
#ifdef DBG_LEVEL
                        DBG_PRINTF5("  Ethernet adapter %" PRIu32 ":", (uint32_t)pAdapter->Index);
                        // DBG_PRINTF5(" %s", pAdapter->AdapterName);
                        DBG_PRINTF5(" %s", pAdapter->Description);
                        DBG_PRINTF5(" %02X-%02X-%02X-%02X-%02X-%02X", pAdapter->Address[0], pAdapter->Address[1], pAdapter->Address[2], pAdapter->Address[3], pAdapter->Address[4],
                                    pAdapter->Address[5]);
                        DBG_PRINTF5(" %s", pAdapter->IpAddressList.IpAddress.String);
                        // DBG_PRINTF5(" %s", pAdapter->IpAddressList.IpMask.String);
                        // DBG_PRINTF5(" Gateway: %s", pAdapter->GatewayList.IpAddress.String);
                        // if (pAdapter->DhcpEnabled) DBG_PRINTF5(" DHCP");
                        DBG_PRINT5("\n");
#endif
                        if (__addr1[0] == 0) {
                            memcpy(__addr1, (uint8_t *)&a, 4);
                            memcpy(__mac1, pAdapter->Address, 6);
                        }
                    }
                }
                pAdapter = pAdapter->Next;
            }
        }
        if (pAdapterInfo)
            free(pAdapterInfo);
    }

    if (__addr1[0] != 0) {
        if (mac)
            memcpy(mac, __mac1, 6);
        if (addr)
            memcpy(addr, __addr1, 4);
        return true;
    }
    return false;
}

#endif // OPTION_ENABLE_GET_LOCAL_ADDR

#endif // _WIN

bool socketListen(SOCKET sock) {

    if (listen(sock, 5)) {
        DBG_PRINTF_ERROR("%d - listen failed!\n", socketGetLastError());
        return 0;
    }
    return 1;
}

SOCKET socketAccept(SOCKET sock, uint8_t *addr) {

    struct sockaddr_in sa;
    socklen_t sa_size = sizeof(sa);
    SOCKET s = accept(sock, (struct sockaddr *)&sa, &sa_size);
    if (addr)
        *(uint32_t *)addr = sa.sin_addr.s_addr;
    return s;
}

bool socketJoin(SOCKET sock, uint8_t *maddr) {

    struct ip_mreq group;
    group.imr_multiaddr.s_addr = *(uint32_t *)maddr;
    group.imr_interface.s_addr = htonl(INADDR_ANY);
    if (0 > setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&group, sizeof(group))) {
        DBG_PRINTF_ERROR("%d - failed to set multicast socket option IP_ADD_MEMBERSHIP!\n", socketGetLastError());
        return 0;
    }
    return 1;
}

// Receive from socket
// Return number of bytes received, 0 when socket closed, would block or empty UDP packet received, -1 on error
int16_t socketRecvFrom(SOCKET sock, uint8_t *buffer, uint16_t bufferSize, uint8_t *addr, uint16_t *port, uint64_t *time) {

    SOCKADDR_IN src;

#if defined(OPTION_SOCKET_HW_TIMESTAMPS) && defined(_LINUX)
    struct iovec iov;
    struct msghdr msg;
    char control[256];
    iov.iov_base = buffer;
    iov.iov_len = bufferSize;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &src;
    msg.msg_namelen = sizeof(src);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    int16_t n = (int16_t)recvmsg(sock, &msg, 0);
#else
    socklen_t srclen = sizeof(src);
    int16_t n = (int16_t)recvfrom(sock, (char *)buffer, bufferSize, 0, (SOCKADDR *)&src, &srclen);

#endif

    if (n == 0) {
        return 0;
    } else if (n < 0) {
        int32_t err = socketGetLastError();
        if (err == SOCKET_ERROR_WBLOCK)
            return 0;
        if (err == SOCKET_ERROR_ABORT || err == SOCKET_ERROR_RESET || err == SOCKET_ERROR_INTR) {
            return 0; // Socket closed
        }
        DBG_PRINTF_ERROR("%u - recvmsg failed (result=%d)!\n", err, n);
        return -1;
    }

    // Extract timestamp from control messages if available
    if (time != NULL) {
#if defined(OPTION_SOCKET_HW_TIMESTAMPS) && defined(_LINUX)
        *time = 0;
        struct cmsghdr *cmsg;
        for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            // DBG_PRINTF5("socketRecvFrom: cmsg_level=%d cmsg_type=%d (looking for SOL_SOCKET=%d SCM_TIMESTAMPNS=%d)\n", cmsg->cmsg_level, cmsg->cmsg_type,
            // SOL_SOCKET,SCM_TIMESTAMPNS);
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_TIMESTAMPNS) {
                struct timespec *ts = (struct timespec *)CMSG_DATA(cmsg);
                *time = (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
                // DBG_PRINTF5("socketRecvFrom: HW timestamp found: %llu ns (sec=%ld, nsec=%ld)\n", (unsigned long long)*time, ts->tv_sec, ts->tv_nsec);
                break;
            }
        }
        if (*time == 0) {
            DBG_PRINT_WARNING("socketRecvFrom: No HW timestamp found in control messages\n");
        }
#ifdef OPTION_ENABLE_DBG_METRICS
        uint64_t system_time = clockGet();
        uint64_t dt = system_time - *time;
        DBG_PRINTF3("socketRecvFrom: received %u bytes from %u.%u.%u.%u:%u at %llu (dt = %llu)\n", n, ((uint8_t *)&src.sin_addr.s_addr)[0], ((uint8_t *)&src.sin_addr.s_addr)[1],
                    ((uint8_t *)&src.sin_addr.s_addr)[2], ((uint8_t *)&src.sin_addr.s_addr)[3], ntohs(src.sin_port), (time ? (unsigned long long)*time : 0),
                    (unsigned long long)dt);
#endif
#else
        *time = clockGet();
#endif
    }

    if (port)
        *port = htons(src.sin_port);
    if (addr)
        memcpy(addr, &src.sin_addr.s_addr, 4);

    return n;
}

// Receive from socket
// Return number of bytes received, 0 when socket closed, would block or empty UDP packet received, -1 on error
int16_t socketRecv(SOCKET sock, uint8_t *buffer, uint16_t size, bool waitAll) {

    int16_t n = (int16_t)recv(sock, (char *)buffer, size, waitAll ? MSG_WAITALL : 0);
    if (n == 0) {
        return 0;
    } else if (n < 0) {
        int32_t err = socketGetLastError();
        if (err == SOCKET_ERROR_WBLOCK)
            return 0; // Would block
        if (err == SOCKET_ERROR_ABORT || err == SOCKET_ERROR_RESET || err == SOCKET_ERROR_INTR) {
            return 0; // Socket closed
        }
        DBG_PRINTF_ERROR("%u - recvfrom failed (result=%d)!\n", err, n);
        return -1; // Error
    }
    return n;
}

// Send datagram on socket
// Must be thread save
int16_t socketSendTo(SOCKET sock, const uint8_t *buffer, uint16_t size, const uint8_t *addr, uint16_t port, uint64_t *time) {

    SOCKADDR_IN sa;
    sa.sin_family = AF_INET;
#if defined(_WIN) // Windows
    memcpy(&sa.sin_addr.S_un.S_addr, addr, 4);
#else
    memcpy(&sa.sin_addr.s_addr, addr, 4);
#endif
    sa.sin_port = htons(port);
    if (time != NULL)
        *time = clockGet();
    return (int16_t)sendto(sock, (const char *)buffer, size, 0, (SOCKADDR *)&sa, (uint16_t)sizeof(sa));
}

// Send datagram on socket
// Must be thread save
int16_t socketSend(SOCKET sock, const uint8_t *buffer, uint16_t size) { return (int16_t)send(sock, (const char *)buffer, size, 0); }

// Get send time of last sent packet
// Retrieves TX hardware timestamp from socket error queue
// Returns 0 if no timestamp available or on error
uint64_t socketGetSendTime(SOCKET sock) {
#if defined(OPTION_SOCKET_HW_TIMESTAMPS) && defined(_LINUX)
    char control[512];
    char data[1];
    struct iovec iov;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    struct timespec *ts = NULL;

    iov.iov_base = data;
    iov.iov_len = sizeof(data);

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    // Read from error queue (non-blocking)
    int ret = recvmsg(sock, &msg, MSG_ERRQUEUE | MSG_DONTWAIT);
    if (ret < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            DBG_PRINTF4("socketGetSendTime: recvmsg error queue failed (errno=%d)\n", errno);
        }
        return 0;
    }

    // Look for timestamp in control messages
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPING) {
            // SO_TIMESTAMPING returns 3 timespec structures: software, deprecated, hardware
            struct timespec *ts_array = (struct timespec *)CMSG_DATA(cmsg);

            // Try hardware timestamp first (index 2)
            if (ts_array[2].tv_sec != 0 || ts_array[2].tv_nsec != 0) {
                ts = &ts_array[2];
                DBG_PRINTF4("socketGetSendTime: HW TX timestamp: %ld.%09ld\n", ts->tv_sec, ts->tv_nsec);
            }
            // Fall back to software timestamp (index 0)
            else if (ts_array[0].tv_sec != 0 || ts_array[0].tv_nsec != 0) {
                ts = &ts_array[0];
                DBG_PRINTF4("socketGetSendTime: SW TX timestamp: %ld.%09ld\n", ts->tv_sec, ts->tv_nsec);
            }

            if (ts != NULL) {
                uint64_t timestamp = (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
                return timestamp;
            }
        }
    }

    DBG_PRINT4("socketGetSendTime: No TX timestamp found in error queue\n");
#else
    (void)sock;
#endif
    return 0;
}

#endif

/**************************************************************************/
// Clock
/**************************************************************************/

static uint64_t __gClock = 0;

// Get the last known clock value
// Save CPU load, clockGet may take resonable run time, depending on platform
// For slow timeouts and timers, it is sufficient to rely on the relatively high call frequency of clockGet() by other
// parts of the application
uint64_t clockGetLast(void) { return __gClock; }

// Not used, might be faster on macOS
// #ifdef _MACOS
// #include <mach/mach_time.h>
// uint64_t getMachineTime(void) {
//     uint64_t tm = mach_absolute_time();
//     mach_timebase_info_data_t timebase;
//     mach_timebase_info(&timebase);
//     return tm * timebase.numer / timebase.denom;
// }
// #endif

#if !defined(_WIN) // Non-Windows platforms

#if !defined(OPTION_CLOCK_EPOCH_PTP) && !defined(OPTION_CLOCK_EPOCH_ARB)
#error "Please define OPTION_CLOCK_EPOCH_ARB or OPTION_CLOCK_EPOCH_PTP"
#endif

/*
Clock options

    OPTION_CLOCK_EPOCH_ARB      arbitrary epoch
    OPTION_CLOCK_EPOCH_PTP      since 1.1.1970
    OPTION_CLOCK_TICKS_1NS      resolution 1ns or 1us, granularity depends on platform
    OPTION_CLOCK_TICKS_1US

Clock types
    CLOCK_REALTIME
        This clock may be affected by incremental adjustments performed by NTP.
        Epoch ns since 1.1.1970.
        Works on all platforms.
        1us granularity on MacOS.

    CLOCK_TAI
        This clock does not experience discontinuities and backwards jumps caused by NTP or inserting leap seconds as CLOCK_REALTIME does.
        Epoch ns since 1.1.1970 Not available on Linux and MacOS.

    CLOCK_MONOTONIC_RAW
        Provides a monotonic clock without time drift adjustments by NTP, giving higher stability and resolution Epoch ns since OS or process start.
        Works on all platforms except QNX, <1us granularity on MACOS.

    CLOCK_MONOTONIC
        Provides a monotonic clock that might be adjusted in frequency by NTP to compensate drifts (on Linux and MACOS).
        On QNX, this clock cannot be adjusted and is ensured to increase at a constant rate.
        Epoch ns since OS or process start.
        Available on all platforms.
        <1us granularity on MACOS.
*/

#ifdef OPTION_CLOCK_EPOCH_ARB
#ifdef _QNX
#define CLOCK_TYPE CLOCK_MONOTONIC // Same behaviour as CLOCK_MONOTONIC_RAW on the other os
#else
#define CLOCK_TYPE CLOCK_MONOTONIC_RAW
#endif
#else
#ifdef _WIN
#define CLOCK_TYPE CLOCK_TAI
#else
#define CLOCK_TYPE CLOCK_REALTIME
#endif
#endif

char *clockGetString(char *s, uint32_t l, uint64_t c) {

#ifdef OPTION_CLOCK_EPOCH_ARB
    SNPRINTF(s, l, "%gs", (double)c / CLOCK_TICKS_PER_S);
#else
    time_t t = (time_t)(c / CLOCK_TICKS_PER_S); // s since 1.1.1970
    struct tm tm;
    gmtime_r(&t, &tm);
    uint64_t fns = c % CLOCK_TICKS_PER_S;
#ifdef OPTION_CLOCK_TICKS_1US
    fns *= 1000;
#endif
    SNPRINTF(s, l, "%u.%u.%u %02u:%02u:%02u +%" PRIu64 "ns", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour % 24, tm.tm_min, tm.tm_sec, fns);
#endif
    return s;
}

bool clockInit(void) {
    DBG_PRINT3("Init clock\n");
#ifdef OPTION_CLOCK_EPOCH_PTP
    DBG_PRINT3("  epoch = OPTION_CLOCK_EPOCH_PTP\n");
#endif
#ifdef OPTION_CLOCK_EPOCH_ARB
    DBG_PRINT3("  epoch = OPTION_CLOCK_EPOCH_ARB\n");
#endif
#ifdef OPTION_CLOCK_TICKS_1US
    DBG_PRINT3("  ticks = OPTION_CLOCK_TICKS_1US\n");
#endif
#ifdef OPTION_CLOCK_TICKS_1NS
    DBG_PRINT3("  ticks = OPTION_CLOCK_TICKS_1NS\n");
#endif

    __gClock = 0;

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 3) { // Test
        struct timespec gtr;
        clock_getres(CLOCK_TYPE, &gtr);
        DBG_PRINTF3("  resolution = %ldns!\n", gtr.tv_nsec);
    }
#endif

    clockGet(); // Initialize ClockGetLast()
    return true;
}

// Get 64 bit clock
uint64_t clockGet(void) {
    struct timespec ts;
    clock_gettime(CLOCK_TYPE, &ts);
#ifdef OPTION_CLOCK_TICKS_1NS // ns
    return __gClock = (((uint64_t)(ts.tv_sec) * 1000000000ULL) + (uint64_t)(ts.tv_nsec));
#else // us
    return __gClock = (((uint64_t)(ts.tv_sec) * 1000000ULL) + (uint64_t)(ts.tv_nsec / 1000)); // us
    // return __gClock = (((uint64_t)(ts.tv_sec - gts0.tv_sec) * 1000000ULL) + (uint64_t)(ts.tv_nsec / 1000));
#endif
}

#else // Windows

// Performance counter to clock conversion
static uint64_t sFactor = 0; // ticks per us
static uint8_t sDivide = 0;  // divide or multiply
static uint64_t sOffset = 0; // offset

char *clockGetString(char *str, uint32_t l, uint64_t c) {

#ifdef OPTION_CLOCK_EPOCH_ARB
    SNPRINTF(str, l, "%gs", (double)c / CLOCK_TICKS_PER_S);
#else
    uint64_t s = c / CLOCK_TICKS_PER_S;
    uint64_t ns = c % CLOCK_TICKS_PER_S;
    if (s < 3600 * 24 * 365 * 30) { // ARB epoch
        SNPRINTF(str, l, "%" PRIu64 "d%" PRIu64 "h%" PRIu64 "m%" PRIu64 "s+%" PRIu64 "ns", s / (3600 * 24), (s % (3600 * 24)) / 3600, ((s % (3600 * 24)) % 3600) / 60,
                 ((s % (3600 * 24)) % 3600) % 60, ns);
    } else { // UNIX epoch
        struct tm tm;
        time_t t = s;
        gmtime_s(&tm, &t);
        SNPRINTF(str, l, "%u.%u.%u %02u:%02u:%02u +%" PRIu64 "ns", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour % 24, tm.tm_min, tm.tm_sec, ns);
    }
#endif
    return str;
}

#include <sys/timeb.h>

bool clockInit(void) {

    DBG_PRINT4("Init clock\n  ");
#ifdef OPTION_CLOCK_EPOCH_PTP
    DBG_PRINT4("OPTION_CLOCK_EPOCH_PTP,");
#endif
#ifdef OPTION_CLOCK_EPOCH_ARB
    DBG_PRINT4("OPTION_CLOCK_EPOCH_ARB,");
#endif
#ifdef OPTION_CLOCK_TICKS_1US
    DBG_PRINT4("OPTION_CLOCK_TICKS_1US\n");
#endif
#ifdef OPTION_CLOCK_TICKS_1NS
    DBG_PRINT4("OPTION_CLOCK_TICKS_1NS\n");
#endif
    DBG_PRINTF4("  CLOCK_TICKS_PER_S = %u\n\n", CLOCK_TICKS_PER_S);

    __gClock = 0;

    // Get current performance counter frequency
    // Determine conversion to CLOCK_TICKS_PER_S -> sDivide/sFactor
    LARGE_INTEGER tF, tC;
    uint64_t tp;
    if (!QueryPerformanceFrequency(&tF)) {
        DBG_PRINT_ERROR("Performance counter not available on this system!\n");
        return false;
    }
    if (tF.u.HighPart) {
        DBG_PRINT_ERROR("Unexpected performance counter frequency!\n");
        return false;
    }

    if (CLOCK_TICKS_PER_S > tF.u.LowPart) {
        sFactor = (uint64_t)CLOCK_TICKS_PER_S / tF.u.LowPart;
        sDivide = 0;
    } else {
        sFactor = tF.u.LowPart / CLOCK_TICKS_PER_S;
        sDivide = 1;
    }

    // Get current performance counter to absolute time relation
#ifndef OPTION_CLOCK_EPOCH_ARB

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
#ifndef OPTION_CLOCK_EPOCH_ARB
    // set offset from local clock UTC value t
    // this is inaccurate up to 1 s, but irrelevant because system clock UTC offset is also not accurate
    sOffset = time_s * CLOCK_TICKS_PER_S + (uint64_t)time_ms * CLOCK_TICKS_PER_MS - tp * sFactor;
#else
    // Reset clock now
    sOffset = tp;
#endif

    clockGet();

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 5) {
#ifndef OPTION_CLOCK_EPOCH_ARB
        if (DBG_LEVEL >= 6) {
            struct tm tm;
            _gmtime64_s(&tm, (const __time64_t *)&time_s);
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

    return true;
}

// Get 64 bit clock
uint64_t clockGet(void) {

    LARGE_INTEGER tp;
    uint64_t t;

    QueryPerformanceCounter(&tp);
    t = (((uint64_t)tp.u.HighPart) << 32) | (uint64_t)tp.u.LowPart;
    if (sDivide) {
        t = t / sFactor + sOffset;
    } else {
        t = t * sFactor + sOffset;
    }
    __gClock = t;
    return t;
}

#endif // Windows

uint64_t clockGetUs(void) { return clockGet() / CLOCK_TICKS_PER_US; }
uint64_t clockGetNs(void) { return clockGet(); }

char *clockGetTimeString(char *str, uint32_t l, int64_t t) {

#ifdef OPTION_CLOCK_EPOCH_ARB
    SNPRINTF(str, l, "%gs", (double)t / CLOCK_TICKS_PER_S);
#else
    char sign = '+';
    if (t < 0) {
        sign = '-';
        t = -t;
    }
    uint64_t s = t / CLOCK_TICKS_PER_S;
    uint64_t ns = t % CLOCK_TICKS_PER_S;
    SNPRINTF(str, l, "%c%" PRIu64 "d%" PRIu64 "h%" PRIu64 "m%" PRIu64 "s+%" PRIu64 "ns", sign, s / (3600 * 24), (s % (3600 * 24)) / 3600, ((s % (3600 * 24)) % 3600) / 60,
             ((s % (3600 * 24)) % 3600) % 60, ns);
#endif
    return str;
}

/**************************************************************************/
// File system utilities
/**************************************************************************/

#ifdef _WIN
#include <io.h> // for _access
#else
#include <unistd.h> // for access
#endif

// Check if a file exists
// Returns true if the file exists and is accessible, false otherwise
bool fexists(const char *filename) {
    if (filename == NULL) {
        return false;
    }

#ifdef _WIN
    // Windows: use _access from io.h
    return (_access(filename, 0) == 0);
#else
    // Linux/macOS: use access from unistd.h
    return (access(filename, F_OK) == 0);
#endif
}
