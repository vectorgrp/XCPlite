/*----------------------------------------------------------------------------
| File:
|   platform.c
|
| Description:
|   Platform OS (Linux/Windows/MACOS/QNX) abstraction layer
|     Atomics
|     Sleep
|     Threads
|     Mutex
|     Sockets
|     Clock
|     Virtual memory
|     Keyboard
|
|   Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "platform.h"

#include <stdlib.h> // for malloc, free

#include "dbg_print.h"  // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "xcplib_cfg.h" // for OPTION_xxx ...

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
        timeout.tv_nsec = (long)us * 1000;
        nanosleep(&timeout, &timerem);
    }
}

void sleepMs(uint32_t ms) {
    if (ms == 0) {
        sleep(0);
    } else {
        struct timespec timeout, timerem;
        timeout.tv_sec = (long)ms / 1000;
        timeout.tv_nsec = (long)(ms % 1000) * 1000000;
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
// Memory mapping
/**************************************************************************/

#if !defined(_WIN)
#include <sys/mman.h>
#endif

void *platformMemAlloc(size_t size) {
#if defined(_WIN)
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
        return NULL;
    return mem;
#endif
}

void platformMemFree(void *ptr, size_t size) {
    if (ptr == NULL)
        return;
#if defined(_WIN)
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}

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

//--------------------------------------------------------------------------
#if !defined(_WIN) // Non-Windows platforms

#include <ifaddrs.h>

#if defined(_LINUX) // Linux platform
#include <net/if.h> // for if_nametoindex, struct ifreq, IFNAMSIZ
#if defined(OPTION_SOCKET_HW_TIMESTAMPS)
#include <linux/errqueue.h>
#include <linux/if_packet.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h> // for SIOCSHWTSTAMP
#include <sys/ioctl.h>     // for ioctl
#endif                     // OPTION_SOCKET_HW_TIMESTAMPS
#endif                     // Linux

#if defined(_MACOS) || defined(_QNX) // MacOS or QNX platforms
#include <net/if_dl.h>
#endif // MacOS or QNX platforms

bool socketStartup(void) { return true; }

void socketCleanup(void) {}

// Create a socket, TCP or UDP
// flag SOCKET_MODE_HW_TIMESTAMPING: Enable hardware timestamping (Linux only, requires root)
// flag SOCKET_MODE_SW_TIMESTAMPING: Enable software timestamping (Linux only)
bool socketOpen(SOCKET_HANDLE *socketp, uint16_t flags) {

    assert(socketp != NULL);
    SOCKET sock = INVALID_SOCKET;

    bool useTCP = flags & SOCKET_MODE_TCP;
    bool nonBlocking = !(flags & SOCKET_MODE_BLOCKING);
    bool reuseaddr = flags & SOCKET_MODE_REUSEADDR;
    bool getifinfo = flags & SOCKET_MODE_GET_IF_INFO;

    assert(nonBlocking == false); // Non-blocking sockets not implemented yet
    (void)nonBlocking;

    // Create a socket
    sock = socket(AF_INET, useTCP ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (sock < 0) {
        DBG_PRINT_ERROR("cannot open socket!\n");
        return 0;
    }

    if (reuseaddr) {
        int yes = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
            DBG_PRINTF_WARNING("Failed to enable SO_REUSEADDR on socket (errno=%d)\n", errno);
        } else {
            DBG_PRINT3("SO_REUSEADDR enabled on socket\n");
        }
    }

    if (getifinfo) {
        int yes = 1;
        if (setsockopt(sock, IPPROTO_IP, IP_PKTINFO, &yes, sizeof(yes)) < 0) {
            DBG_PRINTF_WARNING("Failed to enable IP_PKTINFO on socket (errno=%d)\n", errno);
        } else {
            DBG_PRINT3("IP_PKTINFO enabled\n");
        }
    }

// Enable timestamps if requested
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)

    bool hw_timestamps = flags & SOCKET_MODE_HW_TIMESTAMPING;
    bool sw_timestamps = flags & SOCKET_MODE_SW_TIMESTAMPING;
    if (hw_timestamps) {
        // Enable SO_TIMESTAMPING for full hardware and software timestamping support
        // This is required for PTP SYNC message timestamping
        // SO_TIMESTAMPING supersedes SO_TIMESTAMPNS and provides:
        //   - Hardware RX/TX timestamps (if NIC/driver supports it)
        //   - Software RX/TX timestamps (always available as fallback)
        //   - Raw hardware clock access
        //
        // The timestamp array returned in control messages:
        //   [0] = Software timestamp
        //   [1] = Deprecated (legacy)
        //   [2] = Hardware timestamp (from NIC PHY)
        uint32_t flags = SOF_TIMESTAMPING_TX_SOFTWARE |  // Software TX timestamp (always available)
                         SOF_TIMESTAMPING_RX_SOFTWARE |  // Software RX timestamp (always available)
                         SOF_TIMESTAMPING_SOFTWARE |     // Enable software timestamp generation
                         SOF_TIMESTAMPING_TX_HARDWARE |  // Hardware TX timestamp (if available)
                         SOF_TIMESTAMPING_RX_HARDWARE |  // Hardware RX timestamp (if available)
                         SOF_TIMESTAMPING_RAW_HARDWARE | // Use raw hardware clock (required for HW timestamps)
                         SOF_TIMESTAMPING_OPT_TSONLY |   // Return only timestamp, not packet data
                         // SOF_TIMESTAMPING_OPT_TX_SWHW |  // Generate both SW and HW TX timestamps
                         0;
        if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &flags, sizeof(flags)) < 0) {
            DBG_PRINTF_ERROR("Failed to enable socket hardware timestamps (SO_TIMESTAMPING, errno=%d)\n", errno);
        } else {
            DBG_PRINTF3("Hardware timestamping enabled on socket (SO_TIMESTAMPING flags=0x%X)\n", flags);
        }
    }

    if (sw_timestamps) {

        // Enable software timestamps, if required
        int yes = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPNS, &yes, sizeof(yes)) < 0) {
            DBG_PRINTF_ERROR("Failed to enable socket software timestamps (SO_TIMESTAMPNS, errno=%d)\n", errno);
        } else {
            DBG_PRINT3("Software timestamps enabled on socket (SO_TIMESTAMPNS)\n");
        }
    }
#endif

    SOCKET_HANDLE socket = (struct socket *)malloc(sizeof(struct socket));
    memset(socket, 0, sizeof(struct socket));
    socket->sock = sock;
    socket->flags = flags;
    *socketp = socket;
    return true;
}

bool socketBind(SOCKET_HANDLE socket, const uint8_t *addr, uint16_t port) {

    assert(socket != NULL);
    assert(addr != NULL);

    int sock = socket->sock;

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
        DBG_PRINTF_ERROR("socketBind failed (err=%d) - cannot bind on %u.%u.%u.%u port %u!\n", socketGetLastError(), addr ? addr[0] : 0, addr ? addr[1] : 0, addr ? addr[2] : 0,
                         addr ? addr[3] : 0, port);
        return 0;
    }

    socket->port = port;
    socket->addr = *(uint32_t *)addr;
    return true;
}

// Bind socket to a specific network interface by name (Linux only)
// This is useful for multicast reception on a specific interface while binding to INADDR_ANY
// Requires root privileges on Linux
bool socketBindToDevice(SOCKET_HANDLE socket, const char *ifname) {

    assert(socket != NULL);

#if defined(_LINUX)
    int sock = socket->sock;
    if (ifname != NULL && ifname[0] != '\0') {
        if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, ifname, strlen(ifname)) < 0) {
            DBG_PRINTF_ERROR("socketBindToDevice failed (err=%d) - cannot bind to device %s !\n", socketGetLastError(), ifname);
            return false;
        }
        DBG_PRINTF3("Socket bound to device %s\n", ifname);

        // Store interface name
        strncpy(socket->ifname, ifname, sizeof(socket->ifname) - 1);
        socket->ifname[sizeof(socket->ifname) - 1] = '\0';

        // Store interface index
        unsigned int ifindex = if_nametoindex(ifname);
        socket->ifindex = ifindex;
    }
    return true;
#else
    (void)socket;
    (void)ifname;
    DBG_PRINTF_WARNING("socketBindToDevice(%s): SO_BINDTODEVICE not supported on this platform, request ignored!\n", ifname ? ifname : "(null)");
    return true;
#endif
}

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)

// Enable hardware timestamping and/or software on a network interface
// This configures the NIC driver to generate timestamps for PTP packets
// Must be called after socket is created and bound
// ifname: Network interface name (e.g., "eth0"). If NULL, uses first non-loopback interface.
// Returns true on success, false on failure (falls back to software timestamps)
bool socketEnableTimestamps(SOCKET_HANDLE socket, bool ptpOnly) {

    assert(socket != NULL);
    int sock = socket->sock;

    struct ifreq ifr;
    struct hwtstamp_config hwconfig;

    // Use socket's ifname
    const char *ifname = socket->ifname[0] != '\0' ? socket->ifname : NULL;

    memset(&ifr, 0, sizeof(ifr));
    memset(&hwconfig, 0, sizeof(hwconfig));

    // If no interface specified, try to find the first non-loopback interface
    if (ifname == NULL) {
        DBG_PRINT_WARNING("socketEnableTimestamps: No ifname specified, searching for first non-loopback interface\n");
        struct ifaddrs *ifaddrs, *ifa;
        if (getifaddrs(&ifaddrs) == 0) {
            for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr != NULL && ifa->ifa_addr->sa_family == AF_INET) {
                    struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
                    if (sa->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
                        strncpy(ifr.ifr_name, ifa->ifa_name, IFNAMSIZ - 1);
                        break;
                    }
                }
            }
            freeifaddrs(ifaddrs);
        }
        if (ifr.ifr_name[0] == '\0') {
            DBG_PRINT_ERROR("socketEnableTimestamps: No suitable interface found\n");
            return false;
        }
    } else {
        strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    }

    DBG_PRINTF3("socketEnableTimestamps: Enabling timestamps on interface %s\n", ifr.ifr_name);

    // Configure hardware timestamping:
    // tx_type: HWTSTAMP_TX_ON enables TX timestamps for all packets
    // rx_filter: HWTSTAMP_FILTER_ALL or HWTSTAMP_FILTER_PTP_V2_EVENT for PTP packets
    hwconfig.flags = 0;
    hwconfig.tx_type = HWTSTAMP_TX_ON;                                                 // Enable TX hardware timestamps
    hwconfig.rx_filter = ptpOnly ? HWTSTAMP_FILTER_PTP_V2_EVENT : HWTSTAMP_FILTER_ALL; // Timestamp all incoming packets (or use HWTSTAMP_FILTER_PTP_V2_EVENT for PTP only)

    ifr.ifr_data = (char *)&hwconfig;

    if (ioctl(sock, SIOCSHWTSTAMP, &ifr) < 0) {

        // SIOCSHWTSTAMP requires CAP_NET_ADMIN or root privileges
        // Some NICs may not support it, or the filter mode may not be supported
        DBG_PRINTF_WARNING("socketEnableTimestamps: ioctl SIOCSHWTSTAMP failed for %s (errno=%d: %s)\n", ifr.ifr_name, errno, strerror(errno));
        DBG_PRINT_WARNING("Hardware timestamping may require root privileges or may not be supported by this NIC\n");

        // Try with a less restrictive filter
        hwconfig.rx_filter = HWTSTAMP_FILTER_NONE; // No RX filter, just enable TX
        hwconfig.tx_type = HWTSTAMP_TX_ON;
        if (ioctl(sock, SIOCSHWTSTAMP, &ifr) < 0) {
            DBG_PRINTF_WARNING("socketEnableTimestamps: Fallback also failed (errno=%d: %s)\n", errno, strerror(errno));
            return false;
        }
        DBG_PRINTF_WARNING("socketEnableTimestamps: Enabled TX-only hardware timestamps on %s\n", ifr.ifr_name);
        return true;
    }

    DBG_PRINTF3("Hardware timestamping enabled on %s (tx_type=%d, rx_filter=%d)\n", ifr.ifr_name, hwconfig.tx_type, hwconfig.rx_filter);
    return true;
}

#else

// Hardware timestamping not supported on this platform
// Stub for non-Linux platforms
bool socketEnableTimestamps(SOCKET_HANDLE socket, bool ptpOnly) {
    (void)socket;
    (void)ptpOnly;
    DBG_PRINT_ERROR("socketEnableTimestamps: Socket hardware timestamping not supported on this platform!\n");
    return false;
}

#endif

// Shutdown socket
// Block rx and tx direction
bool socketShutdown(SOCKET_HANDLE socket) {
    if (socket != NULL) {
        if (socket->sock > 0)
            shutdown(socket->sock, SHUT_RDWR);
    }
    return true;
}

// Close socket
// Make addr reusable
bool socketClose(SOCKET_HANDLE *socketp) {
    assert(socketp != NULL);
    if (*socketp != NULL) {
        close((*socketp)->sock);
        free(*socketp);
        *socketp = NULL;
    }
    return true;
}

// Get MAC address of a network interface by name
bool socketGetMAC(char *ifname, uint8_t *mac) {

    assert(ifname != NULL);
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

#ifdef OPTION_ENABLE_GET_LOCAL_ADDR

// Get local IP address and MAC address of the first non-loopback interface
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
                socketGetMAC(ifa1->ifa_name, __mac1);
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

//--------------------------------------------------------------------------
#else // Windows platform

// Winsock
#pragma comment(lib, "ws2_32.lib")

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
bool socketOpen(SOCKET_HANDLE *socketp, uint16_t flags) {

    assert(socketp != NULL);
    SOCKET sock = -1;

    bool useTCP = flags & SOCKET_MODE_TCP;
    bool nonBlocking = !(flags & SOCKET_MODE_BLOCKING);
    bool reuseaddr = flags & SOCKET_MODE_REUSEADDR;
    bool getifinfo = flags & SOCKET_MODE_GET_IF_INFO;
    bool hw_timestamps = flags & SOCKET_MODE_HW_TIMESTAMPING;
    bool sw_timestamps = flags & SOCKET_MODE_SW_TIMESTAMPING;

    assert(!hw_timestamps);       // Hardware timestamps not supported on Windows
    assert(!sw_timestamps);       // Software timestamps not supported on Windows
    assert(!getifinfo);           // IP_PKTINFO not supported on Windows
    assert(nonBlocking == false); // Non-blocking sockets not implemented yet
    (void)nonBlocking;

    // Create a socket
    if (!useTCP) {
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

// Avoid send to UDP nowhere problem (ignore ICMP host unreachable - server has no open socket on master port)
// (stack-overflow 34242622)
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
        bool bNewBehavior = false;
        DWORD dwBytesReturned = 0;
        if (sock != INVALID_SOCKET) {
            WSAIoctl(sock, SIO_UDP_CONNRESET, &bNewBehavior, sizeof bNewBehavior, NULL, 0, &dwBytesReturned, NULL, NULL);
        }
    } else {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    }
    if (sock == INVALID_SOCKET) {
        DBG_PRINTF_ERROR("socketOpen failed (err=%d) - could not create socket!\n", socketGetLastError());
        return false;
    }

    // Set nonblocking mode
    // u_long b = nonBlocking ? 1 : 0;
    // if (NO_ERROR != ioctlsocket(sock, FIONBIO, &b)) {
    //     DBG_PRINTF_ERROR("socketOpen failed (err=%d) - could not set non blocking mode!\n", socketGetLastError());
    //     return false;
    // }

    // Make addr reusable
    if (reuseaddr) {
        uint32_t one = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one)) < 0) {
            DBG_PRINTF_WARNING("socketOpen failed (err=%d) - could not enable SO_REUSEADDR on socket\n", socketGetLastError());
        }
    }

    SOCKET_HANDLE socket = (struct socket *)malloc(sizeof(struct socket));
    assert(socket != NULL);
    memset(socket, 0, sizeof(struct socket));
    socket->sock = sock;
    socket->flags = flags;
    *socketp = socket;
    return true;
}

bool socketBind(SOCKET_HANDLE socket, const uint8_t *addr, uint16_t port) {

    assert(socket != NULL);
    SOCKET sock = socket->sock;

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
            DBG_PRINTF_ERROR("socketBind failed (err=%d) - cannot bind on %u.%u.%u.%u port %u!\n", socketGetLastError(), addr ? addr[0] : 0, addr ? addr[1] : 0, addr ? addr[2] : 0,
                             addr ? addr[3] : 0, port);
        }
        return false;
    }
    return true;
}

// Shutdown socket
// Block rx and tx direction
bool socketShutdown(SOCKET_HANDLE socket) {

    assert(socket != NULL);
    SOCKET sock = socket->sock;

    if (sock != -1) {
        shutdown(sock, SD_BOTH);
    }
    return true;
}

// Close socket
// Make addr reusable
bool socketClose(SOCKET_HANDLE *socketp) {

    assert(socketp != NULL);
    if (*socketp != NULL) {
        closesocket((*socketp)->sock);
        free(*socketp);
        *socketp = NULL;
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

//--------------------------------------------------------------------------
// All platforms

// Listen on a TCP socket
bool socketListen(SOCKET_HANDLE socket) {
    assert(socket != NULL);
    if (listen(socket->sock, 5)) {
        DBG_PRINTF_ERROR("socketListen failed (err=%d)!\n", socketGetLastError());
        return 0;
    }
    return 1;
}

// Accept a connection on a listening TCP socket
// Returns the remote address if addr != NULL
SOCKET_HANDLE socketAccept(SOCKET_HANDLE listenSocket, uint8_t *addr) {
    assert(listenSocket != NULL);
    struct sockaddr_in sa;
    socklen_t sa_size = sizeof(sa);
    SOCKET sock = accept(listenSocket->sock, (struct sockaddr *)&sa, &sa_size);
    if (addr)
        *(uint32_t *)addr = sa.sin_addr.s_addr;

    SOCKET_HANDLE socket = (struct socket *)malloc(sizeof(struct socket));
    memset(socket, 0, sizeof(struct socket));
    socket->sock = sock;
#ifdef _LINUX
    socket->ifindex = listenSocket->ifindex;
    strncpy(socket->ifname, listenSocket->ifname, sizeof(socket->ifname) - 1);
    socket->ifname[sizeof(socket->ifname) - 1] = '\0';
#endif
    socket->flags = listenSocket->flags;
    return socket;
}

// Join a multicast group on a UDP socket
// maddr: Multicast group address (network byte order)
bool socketJoin(SOCKET_HANDLE socket, const uint8_t *maddr, const uint8_t *ifaddr, const char *ifname) {

    assert(socket != NULL);
    SOCKET sock = socket->sock;

#if defined(_LINUX)
    // On Linux, use ip_mreqn which allows specifying interface by name or index
    struct ip_mreqn group;
    memset(&group, 0, sizeof(group));
    group.imr_multiaddr.s_addr = *(uint32_t *)maddr;

    // Priority: interface name > interface address > INADDR_ANY
    if (ifname != NULL && ifname[0] != '\0') {
        // Use interface name (most reliable for multicast on Linux)
        group.imr_ifindex = if_nametoindex(ifname);
        if (group.imr_ifindex == 0) {
            DBG_PRINTF_ERROR("socketJoin: Interface %s not found!\n", ifname);
            return 0;
        }
        socket->ifindex = group.imr_ifindex;
        strncpy(socket->ifname, ifname, sizeof(socket->ifname) - 1);
        socket->ifname[sizeof(socket->ifname) - 1] = '\0';
        DBG_PRINTF4("Joining multicast group on interface %s (index %d)\n", ifname, group.imr_ifindex);

        // Get MAC address for the interface and save it in the socket structure
        if (!socketGetMAC(socket->ifname, socket->ifmac)) {
            DBG_PRINTF_WARNING("socketJoin: Failed to get MAC address for interface %s!\n", ifname);
        }

    } else if (ifaddr != NULL && !(ifaddr[0] == 0 && ifaddr[1] == 0 && ifaddr[2] == 0 && ifaddr[3] == 0)) {
        // Use interface address
        group.imr_address.s_addr = *(uint32_t *)ifaddr;
        socket->ifaddr = *(uint32_t *)ifaddr;

        DBG_PRINTF4("Joining multicast group on interface address %u.%u.%u.%u\n", ifaddr[0], ifaddr[1], ifaddr[2], ifaddr[3]);

    } else {
        // Use INADDR_ANY (kernel picks interface based on routing)
        group.imr_address.s_addr = htonl(INADDR_ANY);

        DBG_PRINT4("Joining multicast group on INADDR_ANY\n");
    }

    if (0 > setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&group, sizeof(group))) {
        DBG_PRINTF_ERROR("socketJoin failed (err=%d) - can't set multicast socket option IP_ADD_MEMBERSHIP!\n", socketGetLastError());
        return 0;
    }
#else
    // Non-Linux platforms: use standard struct ip_mreq (address-based only)
    struct ip_mreq group;
    group.imr_multiaddr.s_addr = *(uint32_t *)maddr;
    // Use the specified interface address, or INADDR_ANY if NULL or 0.0.0.0
    if (ifaddr == NULL || (ifaddr[0] == 0 && ifaddr[1] == 0 && ifaddr[2] == 0 && ifaddr[3] == 0)) {
        group.imr_interface.s_addr = htonl(INADDR_ANY);
    } else {
        group.imr_interface.s_addr = *(uint32_t *)ifaddr;
    }
    if (0 > setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&group, sizeof(group))) {
        DBG_PRINTF_ERROR("socketJoin failed (err=%d) - can't set multicast socket option IP_ADD_MEMBERSHIP!\n", socketGetLastError());
        return 0;
    }
    (void)ifname; // Unused on non-Linux platforms
#endif
    return 1;
}

// Receive from UDP socket
// Returns optional receive timestamps if (time != NULL)
// Support hardware timestamps if enabled on the socket and with OPTION_SOCKET_HW_TIMESTAMPS defined, otherwise system time is used
// Return number of bytes received, 0 when socket closed, would block or empty UDP packet received, or -1 on error
int16_t socketRecvFrom(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t bufferSize, uint8_t *addr, uint16_t *port, uint64_t *time) {

    assert(socket != NULL);
    SOCKET sock = socket->sock;

    SOCKADDR_IN src;
    int16_t n = 0;

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
    // Use recvmsg() to retrieve control messages (timestamps and/or IP_PKTINFO) if available
    // This path is taken when timestamps are requested OR when IP_PKTINFO might be enabled
    {
        struct iovec iov;
        struct msghdr msg;
        char control[CMSG_SPACE(sizeof(struct timespec) * 3) + CMSG_SPACE(sizeof(struct in_pktinfo))];
        iov.iov_base = buffer;
        iov.iov_len = bufferSize;
        memset(&msg, 0, sizeof(msg));
        msg.msg_name = &src;
        msg.msg_namelen = sizeof(src);
        msg.msg_flags = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
        n = (int16_t)recvmsg(sock, &msg, 0);
        if (n == 0) {
            return 0;
        } else if (n < 0) {
            int32_t err = socketGetLastError();
            if (err == SOCKET_ERROR_WBLOCK) {
                return 0; // Would block, should never happen on a blocking socket
            }
            if (err == SOCKET_ERROR_ABORT || err == SOCKET_ERROR_BADF || err == SOCKET_ERROR_RESET || err == SOCKET_ERROR_INTR) {
                DBG_PRINTF5("socketSendV: socket closed (err=%d)\n", err);
                return 0; // Socket closed
            }
            DBG_PRINTF_ERROR("%u - recvmsg failed (result=%d)!\n", err, n);
            return -1;
        }

        // Extract timestamp and interface info from control messages if available
        if (time != NULL)
            *time = 0;
        struct timespec *hw = NULL;
        struct timespec *sw = NULL;
        struct cmsghdr *cmsg;
        uint16_t n = 0;
        for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            n++;
            int level = cmsg->cmsg_level;
            int type = cmsg->cmsg_type;

            DBG_PRINTF6("socketRecvFrom: cmsg level=%d type=%d (%s)\n", level, type, //
                        (level == SOL_SOCKET && type == SO_TIMESTAMPING)  ? "SO_TIMESTAMPING"
                        : (level == SOL_SOCKET && type == SO_TIMESTAMPNS) ? "SO_TIMESTAMPNS"
                        : (level == IPPROTO_IP && type == IP_PKTINFO)     ? "IP_PKTINFO"
                                                                          : "UNKNOWN");

            if (SOL_SOCKET == level && SO_TIMESTAMPING == type) {
                if (cmsg->cmsg_len < sizeof(struct timespec) * 3) {
                    DBG_PRINT_WARNING("short SO_TIMESTAMPING message");
                    break;
                }
                assert(hw == NULL);
                hw = (struct timespec *)CMSG_DATA(cmsg);
            } else if (SOL_SOCKET == level && SO_TIMESTAMPNS == type) {
                if (cmsg->cmsg_len < sizeof(struct timespec)) {
                    DBG_PRINT_WARNING("short SO_TIMESTAMPNS message");
                    break;
                }
                sw = (struct timespec *)CMSG_DATA(cmsg);
            } else if (IPPROTO_IP == level && IP_PKTINFO == type) {
                struct in_pktinfo *pktinfo = (struct in_pktinfo *)CMSG_DATA(cmsg);
                // Always print IP_PKTINFO for debugging (use printf, not DBG_PRINTF)
                DBG_PRINTF6("socketRecvFrom: IP_PKTINFO - ipi_ifindex=%d, ipi_addr=%08x, ipi_spec_dst=%08x, socket->ifindex=%d\n", pktinfo->ipi_ifindex,
                            ntohl(pktinfo->ipi_addr.s_addr), ntohl(pktinfo->ipi_spec_dst.s_addr), socket->ifindex);
                assert(socket->ifindex == 0 || socket->ifindex == pktinfo->ipi_ifindex);
                // Note: Just to be sure, we always get timestamps from expected if. Currently no mechanism to return this info to caller
            }
        }
        if (n == 0) {
            DBG_PRINT6("socketRecvFrom: No control messages received\n");
        }

        // Process timestamps if requested
        if (time != NULL) {
            uint64_t t = 0;
            if (hw != NULL) {
                struct timespec *ts;
                ts = &hw[2];
                t = (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
                if (t != 0) {
                    DBG_PRINT6("socketRecvFrom: timestamp taken from control messages SO_TIMESTAMPING [2]\n");
                } else {
                    ts = &hw[0];
                    t = (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
                    if (t != 0) {
                        DBG_PRINT6("socketRecvFrom: timestamp taken from control messages SO_TIMESTAMPING [0]\n");
                    }
                }

                // {
                //     uint64_t t_hw = hw[2].tv_sec * 1000000000ULL + hw[2].tv_nsec;
                //     uint64_t t_sw = hw[0].tv_sec * 1000000000ULL + hw[0].tv_nsec;
                //     printf("socketRecvFrom: HW timestamp = %" PRIu64 " ns, SW timestamp = %" PRIu64 " ns, diff = %" PRIi64 " ns\n", t_hw, t_sw, (int64_t)(t_hw - t_sw));
                // }
            }
            if (t == 0 && sw != NULL) {
                struct timespec *ts = sw;
                t = (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
                DBG_PRINT5("socketRecvFrom: timestamp taken from control messages SO_TIMESTAMPNS\n");
            }
            if (t == 0) {
                DBG_PRINT_WARNING("socketRecvFrom: No timestamp found in control messages\n");
            }
            *time = t;
        }
    }
#else
    {
        socklen_t srclen = sizeof(src);
        n = (int16_t)recvfrom(sock, (char *)buffer, bufferSize, 0, (SOCKADDR *)&src, &srclen);

        if (n == 0) {
            return 0;
        } else if (n < 0) {
            int32_t err = socketGetLastError();
            if (err == SOCKET_ERROR_WBLOCK)
                return 0; // Would block, should never happen on a blocking socket
            if (err == SOCKET_ERROR_ABORT || err == SOCKET_ERROR_BADF || err == SOCKET_ERROR_RESET || err == SOCKET_ERROR_INTR) {
                DBG_PRINTF5("socketRecvFrom: socket closed (err=%d)\n", err);
                return 0; // Socket closed
            }
            DBG_PRINTF_ERROR("socketRecvFrom: failed (err=%u - result=%d)!\n", err, n);
            return -1;
        }

        if (time != NULL) {
            *time = clockGet();
        }
    }
#endif

    if (port)
        *port = htons(src.sin_port);
    if (addr)
        memcpy(addr, &src.sin_addr.s_addr, 4);

    DBG_PRINTF6("socketRecvFrom: sock=%d, ifindex=%d returned n=%u, time=%" PRIu64 "\n", sock, socket->ifindex, n, time ? *time : 0);

    return n;
}

// Receive from TCP or UDP socket, blocking or non-blocking
// Return number of bytes received, 0 when socket closed, would block or empty UDP packet received, or -1 on error
int16_t socketRecv(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t size, bool waitAll) {

    assert(socket != NULL);
    int16_t n = (int16_t)recv(socket->sock, (char *)buffer, size, waitAll ? MSG_WAITALL : 0);
    if (n == 0) {
        return 0;
    } else if (n < 0) {
        int32_t err = socketGetLastError();
        if (err == SOCKET_ERROR_WBLOCK) {
            return 0; // Would block, should never happen on a blocking socket
        }
        if (err == SOCKET_ERROR_ABORT || err == SOCKET_ERROR_BADF || err == SOCKET_ERROR_RESET || err == SOCKET_ERROR_INTR) {
            DBG_PRINT5("socketRecv: socket closed\n");
            return 0; // Socket closed
        }
        DBG_PRINTF_ERROR("%u - recv failed (result=%d)!\n", err, n);
        return -1; // Error
    }
    return n;
}

// Send datagram on UDP socket
// Returns number of bytes sent or -1 on error
// Requests and may returns optional send time if (time != NULL)
// Support hardware timestamps if enabled on the socket and with OPTION_SOCKET_HW_TIMESTAMPS defined, otherwise system time is used
// If *time = 0 on return, no timestamp is available yet, but can be obtained with socketGetSendTime()
// On non-Linux platforms, *time is set to system time at send
// Returns total number of bytes sent, 0 on socket closed or -1 on error
int16_t socketSendTo(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t size, const uint8_t *addr, uint16_t port, uint64_t *time) {

    assert(socket != NULL);
    SOCKET sock = socket->sock;

    DBG_PRINTF6("socketSendTo: sock=%d, ifindex=%d\n", sock, socket->ifindex);

    SOCKADDR_IN sa;
    sa.sin_family = AF_INET;
#if defined(_WIN) // Windows
    memcpy(&sa.sin_addr.S_un.S_addr, addr, 4);
#else
    memcpy(&sa.sin_addr.s_addr, addr, 4);
#endif
    sa.sin_port = htons(port);

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
    if (time != NULL) {
        // On Linux, we need to use sendmsg() with SO_TIMESTAMPING control message
        // to request TX timestamp generation for this specific packet
        struct iovec iov;
        struct msghdr msg;
        char control[CMSG_SPACE(sizeof(uint32_t))];
        struct cmsghdr *cmsg;

        iov.iov_base = (void *)buffer;
        iov.iov_len = size;

        memset(&msg, 0, sizeof(msg));
        msg.msg_name = &sa;
        msg.msg_namelen = sizeof(sa);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);

        // Add control message to request timestamp generation
        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SO_TIMESTAMPING;
        cmsg->cmsg_len = CMSG_LEN(sizeof(uint32_t));

        // Request both hardware and software timestamps
        // Hardware timestamp will be used if available, otherwise fall back to software
        uint32_t ts_flags = SOF_TIMESTAMPING_TX_SOFTWARE | SOF_TIMESTAMPING_TX_HARDWARE;
        memcpy(CMSG_DATA(cmsg), &ts_flags, sizeof(ts_flags));
        *time = 0; // Clear time, to indicate that it may be obtained later with socketGetSendTime()
        ssize_t n = sendmsg(sock, &msg, 0);
        if (n < 0) {
            int32_t err = socketGetLastError();
            if (err == SOCKET_ERROR_WBLOCK) {
                DBG_PRINT_ERROR("socketSendTo: unexpected WBLOCK\n");
                return -1; // Should never happen on a blocking socket
            }
            if (err == SOCKET_ERROR_ABORT || err == SOCKET_ERROR_BADF || err == SOCKET_ERROR_RESET || err == SOCKET_ERROR_INTR || err == SOCKET_ERROR_PIPE) {
                DBG_PRINTF5("socketSendTo: socket closed (err=%d)\n", err);
                return 0; // Socket closed
            }
            DBG_PRINTF_ERROR("socketSendTo: sendmsg failed with err=%d!\n", err);
            return -1;
        }
        return (int16_t)n;
    }
#else

    if (time != NULL)
        *time = clockGet(); // Return system time as send time on non-Linux platforms

#endif
    ssize_t n = sendto(sock, (const char *)buffer, size, 0, (SOCKADDR *)&sa, (uint16_t)sizeof(sa));
    if (n < 0) {
        int32_t err = socketGetLastError();
        if (err == SOCKET_ERROR_WBLOCK) {
            DBG_PRINT_ERROR("socketSendTo: unexpected WBLOCK\n");
            return -1; // Should never happen on a blocking socket
        }
        if (err == SOCKET_ERROR_ABORT || err == SOCKET_ERROR_BADF || err == SOCKET_ERROR_RESET || err == SOCKET_ERROR_INTR || err == SOCKET_ERROR_PIPE) {
            DBG_PRINTF5("socketSendTo: socket closed (err=%d)\n", err);
            return 0; // Socket closed
        }
        DBG_PRINTF_ERROR("socketSendTo: sendto failed with err=%d!\n", err);
        return -1;
    }
    return (int16_t)n;
}

// Send buffer on a TCP socket
// Thread safe
// Returns total number of bytes sent, 0 on socket closed or -1 on error
int16_t socketSend(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t size) {

    assert(socket != NULL);
    SOCKET sock = socket->sock;

    ssize_t n = send(sock, (const char *)buffer, size, 0);
    if (n < 0) {
        int32_t err = socketGetLastError();
        if (err == SOCKET_ERROR_WBLOCK) {
            DBG_PRINT_ERROR("socketSend: unexpected WBLOCK\n");
            return -1; // Should never happen on a blocking socket
        }
        if (err == SOCKET_ERROR_ABORT || err == SOCKET_ERROR_BADF || err == SOCKET_ERROR_RESET || err == SOCKET_ERROR_INTR || err == SOCKET_ERROR_PIPE) {
            DBG_PRINTF5("socketSend: socket closed (err=%d)\n", err);
            return 0; // Socket closed
        }
        DBG_PRINTF_ERROR("socketSend: send failed with err=%d!\n", err);
        return -1;
    }
    return (int16_t)n;
}

#if !defined(_WIN) && !defined(OPTION_DISABLE_VECTORED_IO)

// Send multiple datagrams on a UDP socket
// Returns number of bytes sent or -1 on error
// Send multiple buffers as a UDP datagram to a specific address/port
// Using iovec for efficient scatter-gather I/O (POSIX: Linux, macOS, QNX)
// Thread safe
// buffers: array of pointers to data buffers
// sizes:   array of buffer sizes, one per buffer
// count:   number of buffers
// Returns total number of bytes sent, 0 on socket closed or -1 on error
int16_t socketSendToV(SOCKET_HANDLE socket, tQueueBuffer buffers[], uint16_t count, const uint8_t *addr, uint16_t port) {

    assert(socket != NULL);
    assert(buffers != NULL);
    assert(count > 0);
    assert(addr != NULL);

    SOCKET sock = socket->sock;

    SOCKADDR_IN sa;
    sa.sin_family = AF_INET;
    memcpy(&sa.sin_addr.s_addr, addr, 4);
    sa.sin_port = htons(port);

    // Build iovec array on the stack - VLAs are acceptable here as count is usually small
    struct iovec iov[count];
    int16_t total = 0;
    for (uint16_t i = 0; i < count; i++) {
        iov[i].iov_base = (void *)buffers[i].buffer;
        iov[i].iov_len = buffers[i].size;
        total += (int16_t)buffers[i].size;
    }

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &sa;
    msg.msg_namelen = sizeof(sa);
    msg.msg_iov = iov;
    msg.msg_iovlen = count;

    ssize_t n = sendmsg(sock, &msg, 0);
    if (n < 0) {
        int32_t err = socketGetLastError();
        if (err == SOCKET_ERROR_WBLOCK) {
            DBG_PRINT_ERROR("socketSendToV: unexpected WBLOCK\n");
            return -1; // Should never happen on a blocking socket
        }
        if (err == SOCKET_ERROR_ABORT || err == SOCKET_ERROR_BADF || err == SOCKET_ERROR_RESET || err == SOCKET_ERROR_INTR || err == SOCKET_ERROR_PIPE) {
            DBG_PRINTF5("socketSendToV: socket closed (err=%d)\n", err);
            return 0; // Socket closed
        }
        DBG_PRINTF_ERROR("socketSendToV: sendmsg failed with err=%d!\n", err);
        return -1;
    }
    if (total != n) {
        DBG_PRINTF_WARNING("socketSendToV: partial send, sent %d of %d bytes\n", (int)n, (int)total);
        return -1; // Treat partial sends as an error on UDP sockets, as the caller cannot recover
    }
    return (int16_t)n;
}

// Send multiple buffers on a TCP socket
// Using iovec for efficient scatter-gather I/O (POSIX: Linux, macOS, QNX)
// Thread safe
// buffers: array of pointers to data buffers
// sizes:   array of buffer sizes, one per buffer
// count:   number of buffers
// Returns total number of bytes sent, 0 on socket closed or -1 on error
int16_t socketSendV(SOCKET_HANDLE socket, tQueueBuffer buffers[], uint16_t count) {

    assert(socket != NULL);
    assert(buffers != NULL);
    assert(count > 0);

    SOCKET sock = socket->sock;

    // Build iovec array on the stack - VLAs are acceptable here as count is usually small
    struct iovec iov[count];
    for (uint16_t i = 0; i < count; i++) {
        iov[i].iov_base = (void *)buffers[i].buffer;
        iov[i].iov_len = buffers[i].size;
    }

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = count;

    // TCP streams may deliver partial sends: loop until all data is accepted by the kernel
    // Advance iovec entries as bytes are consumed to avoid re-sending already sent data
    // Note: all sockets in this codebase are blocking (see socketOpen), so WBLOCK must not
    // occur. If it does mid-loop, the iovec state is partially consumed and the caller cannot
    // recover, so it is treated as an unrecoverable error rather than returning a partial count.
    int16_t total = 0;
    for (;;) {
        ssize_t n = sendmsg(sock, &msg, 0);
        if (n < 0) {
            int32_t err = socketGetLastError();
            if (err == SOCKET_ERROR_WBLOCK) {
                DBG_PRINT_ERROR("socketSendV: unexpected WBLOCK\n");
                return -1; // Should never happen on a blocking socket
            }
            if (err == SOCKET_ERROR_ABORT || err == SOCKET_ERROR_BADF || err == SOCKET_ERROR_RESET || err == SOCKET_ERROR_INTR || err == SOCKET_ERROR_PIPE) {
                DBG_PRINTF5("socketSendV: socket closed (err=%d)\n", err);
                return 0; // Socket closed
            }
            DBG_PRINTF_ERROR("socketSendV: sendmsg failed with err=%d!\n", err);
            return -1;
        }
        total += (int16_t)n;

        // Advance the iovec past the bytes already sent
        size_t remaining = (size_t)n;
        while (msg.msg_iovlen > 0 && remaining >= msg.msg_iov[0].iov_len) {
            remaining -= msg.msg_iov[0].iov_len;
            msg.msg_iov++;
            msg.msg_iovlen--;
        }
        if (msg.msg_iovlen == 0)
            break; // All data sent
        // Adjust the first remaining iovec for the partial send
        msg.msg_iov[0].iov_base = (uint8_t *)msg.msg_iov[0].iov_base + remaining;
        msg.msg_iov[0].iov_len -= remaining;
    }

    return total;
}

#endif // !_WIN

// Get send time of last sent packet
// Retrieves TX hardware timestamp and kernel software timestamp from socket error queue
// Returns false if no timestamp available or on error
// On non-Linux platforms, this function always returns false
// On Linux, requires OPTION_SOCKET_HW_TIMESTAMPS defined and hardware timestamping enabled on the socket
// hw_time and sw_time are optional, set to NULL if not needed
bool socketGetSendTime(SOCKET_HANDLE socket, uint64_t *hw_time, uint64_t *sw_time) {

    assert(socket != NULL);
    SOCKET sock = socket->sock;

    if (hw_time)
        *hw_time = 0;
    if (sw_time)
        *sw_time = 0;

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
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

    DBG_PRINT5("socketGetSendTime: Reading from error queue...\n");

    // Read from error queue with retries (timeout 10ms)
    ssize_t ret = -1;
    for (uint32_t attempt = 0; attempt < 10; attempt++) {
        ret = recvmsg(sock, &msg, MSG_ERRQUEUE | MSG_DONTWAIT);
        if (ret >= 0) {
            DBG_PRINTF5("socketGetSendTime: Got message from error queue after %u attempts, ret=%ld\n", attempt, ret);
            break;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            DBG_PRINTF_ERROR("socketGetSendTime: recvmsg error queue failed with errno=%d (%s)\n", errno, strerror(errno));
            return false;
        }
        // Wait a bit and retry
        sleepUs(1000); // 1ms
    }
    if (ret < 0) {
        DBG_PRINT_WARNING("socketGetSendTime: Timeout, no TX timestamp available after retries\n");
        return false;
    }

    // Look for timestamps in control messages
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        DBG_PRINTF5("socketGetSendTime: Found cmsg level=%d type=%d (SOL_SOCKET=%d SO_TIMESTAMPING=%d)\n", cmsg->cmsg_level, cmsg->cmsg_type, SOL_SOCKET, SO_TIMESTAMPING);
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPING) {
            // SO_TIMESTAMPING returns 3 timespec structures: software, deprecated, hardware
            struct timespec *ts_array = (struct timespec *)CMSG_DATA(cmsg);

            DBG_PRINTF5("socketGetSendTime: ts[0]=%ld.%09ld ts[1]=%ld.%09ld ts[2]=%ld.%09ld\n", ts_array[0].tv_sec, ts_array[0].tv_nsec, ts_array[1].tv_sec, ts_array[1].tv_nsec,
                        ts_array[2].tv_sec, ts_array[2].tv_nsec);

            // hardware timestamp (index 2)
            ts = &ts_array[2];
            if (ts->tv_sec != 0 || ts->tv_nsec != 0) {
                if (hw_time)
                    *hw_time = (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
                DBG_PRINTF5("socketGetSendTime: Using HW TX timestamp: %ld.%09ld\n", ts->tv_sec, ts->tv_nsec);
            }

            // software timestamp (index 0)
            ts = &ts_array[0];
            if (ts->tv_sec != 0 || ts->tv_nsec != 0) {
                if (sw_time)
                    *sw_time = (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
                DBG_PRINTF5("socketGetSendTime: Using SW TX timestamp: %ld.%09ld\n", ts->tv_sec, ts->tv_nsec);
            }

            if ((hw_time == NULL || *hw_time != 0) && (sw_time == NULL || *sw_time != 0)) {
                break; // Got what we needed
            }
        }
    }

    if ((hw_time == NULL || *hw_time != 0) && (sw_time == NULL || *sw_time != 0)) {
        DBG_PRINTF5("socketGetSendTime: hw=%" PRIu64 ", sw=%" PRIu64 ", sys= %" PRIu64 "\n", hw_time ? *hw_time : 0, sw_time ? *sw_time : 0, clockGet());
        return true; // Got all requested timestamps
    }
    if (hw_time != NULL && *hw_time == 0)
        DBG_PRINT_WARNING("socketGetSendTime: No hardware TX timestamp found\n");
    if (sw_time != NULL && *sw_time == 0)
        DBG_PRINT_WARNING("socketGetSendTime: No software TX timestamp found\n");

    return false;

#else
    (void)sock;
#endif
    return false;
}

#endif

/**************************************************************************/
// Clock
/**************************************************************************/

static uint64_t __gClock = 0;

#ifdef TEST_CLOCK_GET_STATISTIC
static atomic_uint_fast64_t gClockGetCtr = 0;
static atomic_uint_fast64_t gClockGetLastCtr = 0;
void clockGetPrintStatistic(void) {
    uint64_t getCtr = atomic_load_explicit(&gClockGetCtr, memory_order_relaxed);
    uint64_t getLastCtr = atomic_load_explicit(&gClockGetLastCtr, memory_order_relaxed);
    DBG_PRINTF3("clockGet calls: %" PRIu64 ", clockGetLast calls: %" PRIu64 "\n", getCtr, getLastCtr);
}
#endif

// Get the last known clock value
// Save CPU load, clockGet may take reasonable run time, depending on platform
// For slow timeouts and timers, it is sufficient to rely on the relatively high call frequency of clockGet() by other
// parts of the application
uint64_t clockGetLast(void) {
#ifdef TEST_CLOCK_GET_STATISTIC
    atomic_fetch_add_explicit(&gClockGetLastCtr, 1, memory_order_relaxed);
#endif
    return __gClock;
}

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

    OPTION_CLOCK_EPOCH_ARB      arbitrary epoch, clock is monotonic, no corrections by NTP, PTP, ...
    OPTION_CLOCK_EPOCH_PTP      real time clock in ns or us since 1.1.1970
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
#define CLOCK_TYPE CLOCK_REALTIME
#endif
#ifdef _QNX
#define CLOCK_MONOTONIC_TYPE CLOCK_MONOTONIC
#else
#define CLOCK_MONOTONIC_TYPE CLOCK_MONOTONIC_RAW
#endif
#define CLOCK_REALTIME_TYPE CLOCK_REALTIME

char *clockGetString(char *s, uint32_t l, uint64_t c) {

    if (c < 1000000000000000000ULL) { // Don't print time and date, if too old
        SNPRINTF(s, l, "%gs", (double)c / CLOCK_TICKS_PER_S);
    } else {
        time_t t = (time_t)(c / CLOCK_TICKS_PER_S); // s since 1.1.1970
        struct tm tm;
        gmtime_r(&t, &tm);
        uint64_t fns = c % CLOCK_TICKS_PER_S;
#ifdef OPTION_CLOCK_TICKS_1US
        fns *= 1000;
#endif
        SNPRINTF(s, l, "%u.%u.%u %02u:%02u:%02u +%" PRIu64 "ns", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour % 24, tm.tm_min, tm.tm_sec, fns);
    }
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
    uint64_t clock = clockGet(); // Initialize ClockGetLast()

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 3) { // Test
        struct timespec gtr;
        clock_getres(CLOCK_TYPE, &gtr);
        DBG_PRINTF3("  resolution = %ldns!\n", gtr.tv_nsec);
        char ts[64];
        clockGetString(ts, sizeof(ts), clock);
        DBG_PRINTF3("  initial clock = %" PRIu64 " %s\n", clock, ts);
    }
#endif
    return true;
}

// Get 64 bit clock
uint64_t clockGet(void) {
    struct timespec ts;
    clock_gettime(CLOCK_TYPE, &ts);
#ifdef TEST_CLOCK_GET_STATISTIC
    atomic_fetch_add_explicit(&gClockGetCtr, 1, memory_order_relaxed);
#endif
#ifdef OPTION_CLOCK_TICKS_1NS // ns
    return __gClock = (((uint64_t)(ts.tv_sec) * 1000000000ULL) + (uint64_t)(ts.tv_nsec));
#else // us
    return __gClock = (((uint64_t)(ts.tv_sec) * 1000000ULL) + (uint64_t)(ts.tv_nsec / 1000)); // us
    // return __gClock = (((uint64_t)(ts.tv_sec - gts0.tv_sec) * 1000000ULL) + (uint64_t)(ts.tv_nsec / 1000));
#endif
}

uint64_t clockGetMonotonicNs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_TYPE, &ts);
    return (((uint64_t)(ts.tv_sec) * 1000000000ULL) + (uint64_t)(ts.tv_nsec));
}

uint64_t clockGetMonotonicUs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_TYPE, &ts);
    return (((uint64_t)(ts.tv_sec) * 1000000ULL) + (uint64_t)(ts.tv_nsec / 1000));
}

uint64_t clockGetRealtimeNs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME_TYPE, &ts);
    return (((uint64_t)(ts.tv_sec) * 1000000000ULL) + (uint64_t)(ts.tv_nsec));
}

#else // Windows

// Performance counter to clock conversion
static uint64_t sFactor = 0; // ticks per us
static uint8_t sDivide = 0;  // divide or multiply
static uint64_t sOffset = 0; // offset

char *clockGetString(char *str, uint32_t l, uint64_t c) {

    if (c < 1000000000000000000ULL) { // Don't print time and date, if too old
        SNPRINTF(str, l, "%gs", (double)c / CLOCK_TICKS_PER_S);
    } else {
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
    }
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
    DBG_PRINTF4("  CLOCK_TICKS_PER_S = %I64u\n\n", CLOCK_TICKS_PER_S);

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
        printf("  Now = %I64u (%I64u per us) %s\n", t, CLOCK_TICKS_PER_US, ts);
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
#ifdef TEST_CLOCK_GET_STATISTIC
    atomic_fetch_add_explicit(&gClockGetCtr, 1, memory_order_relaxed);
#endif
    return t;
}

#endif // Windows

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
