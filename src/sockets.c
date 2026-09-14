/*----------------------------------------------------------------------------
| File:
|   sockets.c
|
| Description:
|   Platform socket abstraction layer (Linux/Windows/macOS/QNX/FreeRTOS)
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "sockets.h"

#include <stdlib.h> // for malloc, free
#include <string.h> // for memset, memcpy, strerror, strncpy
#if !defined(_WIN)
#include <unistd.h> // for close
#endif

#include "assert.h"
#include "dbg_print.h"
#include "xcptl_cfg.h" // for OPTION_MTU and XCPTL_MAX_SEGMENT_SIZE in the EMSGSIZE diagnostic

#if (defined(OPTION_ENABLE_TCP) || defined(OPTION_ENABLE_UDP)) && !defined(OPTION_ENABLE_UDP_RAW)

const char *socketGetErrorString(int32_t err) {
#if !defined(_WIN)
    return strerror(err);
#else
    switch (err) {
    case SOCKET_ERROR_ABORT:
        return "connection aborted";
    case SOCKET_ERROR_RESET:
        return "connection reset";
    case SOCKET_ERROR_INTR:
        return "interrupted";
    case SOCKET_ERROR_TIMEDOUT:
        return "timed out";
    case SOCKET_ERROR_WBLOCK:
        return "would block";
    case SOCKET_ERROR_PIPE:
        return "broken pipe";
    case SOCKET_ERROR_BADF:
        return "bad file descriptor";
    case SOCKET_ERROR_NOTCONN:
        return "not connected";
    default:
        return "unknown socket error";
    }
#endif
}

//--------------------------------------------------------------------------
// FreeRTOS platforms

#if defined(_FREE_RTOS) && !defined(FREE_RTOS_POSIX_SIM) // FreeRTOS sockets

#ifdef OPTION_ENABLE_TCP
#error "FreeRTOS TCP socket functions not implemented yet"
#endif

#if defined(OPTION_FREERTOS_LWIP)
#include "lwip/errno.h"   // lwIP errno values mapped to POSIX codes
#include "lwip/netif.h"   // netif_default, struct netif::mtu, for the segment size check in socketSendTo
#include "lwip/sockets.h" // lwip_socket, lwip_bind, lwip_sendto, lwip_recvfrom, lwip_close, lwip_shutdown, lwip_setsockopt
#endif

// socketStartup: lwIP networking is initialised by the application (e.g. tcpip_init) — no-op here
bool socketStartup(void) {
#if defined(OPTION_FREERTOS_LWIP)
    return true;
#else
    DBG_PRINT_ERROR("FREE_RTOS:socketStartup not implemented\n");
    return true;
#endif
}

// socketCleanup: no teardown required for lwIP
void socketCleanup(void) {
#if !defined(OPTION_FREERTOS_LWIP)
    DBG_PRINT_ERROR("FREE_RTOS:socketCleanup not implemented\n");
#endif
}

// Create a UDP socket (TCP not supported: OPTION_ENABLE_TCP must not be defined)
bool socketOpen(SOCKET_HANDLE *socketp, uint16_t flags) {
#if defined(OPTION_FREERTOS_LWIP)
    assert(socketp != NULL);
    assert(!(flags & SOCKET_MODE_TCP)); // TCP not supported on FreeRTOS/lwIP

    int sock = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        DBG_PRINTF_ERROR("socketOpen: lwip_socket failed (errno=%d,%s)\n", errno, socketGetErrorString(errno));
        return false;
    }
    if (flags & SOCKET_MODE_REUSEADDR) {
        int yes = 1;
        if (lwip_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
            DBG_PRINTF_WARNING("socketOpen: SO_REUSEADDR failed (errno=%d,%s)\n", errno, socketGetErrorString(errno));
        }
    }
    *socketp = sock;
    DBG_PRINTF5("socketOpen: lwIP UDP socket %d opened\n", sock);
    return true;
#else
    DBG_PRINT_ERROR("FREE_RTOS:socketOpen not implemented\n");
    return false;
#endif
}

// Bind socket to a local address and port
// addr: network-byte-order IPv4 address; NULL or 0.x.x.x binds to INADDR_ANY
bool socketBind(SOCKET_HANDLE socket, const uint8_t *addr, uint16_t port) {
#if defined(OPTION_FREERTOS_LWIP)
    assert(socket != INVALID_SOCKET_HANDLE);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (addr != NULL && addr[0] != 0) {
        a.sin_addr.s_addr = *(uint32_t *)addr;
    } else {
        a.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    if (lwip_bind(socket, (struct sockaddr *)&a, sizeof(a)) < 0) {
        DBG_PRINTF_ERROR("socketBind: lwip_bind failed (errno=%d,%s) on port %u\n", errno, socketGetErrorString(errno), port);
        return false;
    }
    DBG_PRINTF5("socketBind: bound to port %u\n", port);
    return true;
#else
    DBG_PRINT_ERROR("FREE_RTOS:socketBind not implemented\n");
    return false;
#endif
}

// Shutdown socket — unblocks a thread blocked in socketRecvFrom
bool socketShutdown(SOCKET_HANDLE socket) {
#if defined(OPTION_FREERTOS_LWIP)
    if (socket != INVALID_SOCKET_HANDLE) {
        lwip_shutdown(socket, SHUT_RDWR);
    }
    return true;
#else
    DBG_PRINT_ERROR("FREE_RTOS:socketShutdown not implemented\n");
    return true;
#endif
}

// Close socket and free the handle
bool socketClose(SOCKET_HANDLE *socketp) {
#if defined(OPTION_FREERTOS_LWIP)
    assert(socketp != NULL);
    if (*socketp != INVALID_SOCKET_HANDLE) {
        lwip_close(*socketp);
        *socketp = INVALID_SOCKET_HANDLE;
    }
    return true;
#else
    DBG_PRINT_ERROR("FREE_RTOS:socketClose not implemented\n");
    return true;
#endif
}

// Receive a UDP datagram (blocking)
// Returns: > 0 bytes received, 0 on timeout/EAGAIN, -1 on error or socket closed
int16_t socketRecvFrom(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t bufferSize, uint8_t *srcAddr, uint16_t *srcPort, uint64_t *time) {
#if defined(OPTION_FREERTOS_LWIP)
    assert(socket != INVALID_SOCKET_HANDLE);
    struct sockaddr_in src;
    socklen_t srclen = sizeof(src);
    memset(&src, 0, sizeof(src));
    int16_t n = (int16_t)lwip_recvfrom(socket, buffer, bufferSize, 0, (struct sockaddr *)&src, &srclen);
    if (n == 0) {
        return 0; // Zero-length datagram or graceful close
    }
    if (n < 0) {
        int32_t err = errno;
        if (socketTimeout(err)) {
            return 0; // Timeout — caller loops and does background work
        }
        DBG_PRINTF_ERROR("socketRecvFrom: lwip_recvfrom failed (errno=%d,%s)\n", err, socketGetErrorString(err));
        return -1;
    }
    if (srcAddr != NULL) {
        memcpy(srcAddr, &src.sin_addr.s_addr, 4);
    }
    if (srcPort != NULL) {
        *srcPort = ntohs(src.sin_port);
    }
    if (time != NULL) {
        *time = clockGet(); // No hardware timestamps on lwIP; use XCP clock
    }
    return n;
#else
    DBG_PRINT_ERROR("FREE_RTOS:socketRecvFrom not implemented\n");
    return -1;
#endif
}

// Send a UDP datagram to addr:port
// Returns: bytes sent, 0 on closed socket, -1 on error
int16_t socketSendTo(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t bufferSize, const uint8_t *addr, uint16_t port, uint64_t *time) {
#if defined(OPTION_FREERTOS_LWIP)
    assert(socket != INVALID_SOCKET_HANDLE);
    assert(addr != NULL);
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    dst.sin_addr.s_addr = *(uint32_t *)addr;
    if (time != NULL) {
        *time = clockGet(); // No hardware timestamps on lwIP; use XCP clock at send time
    }

    // lwIP sets no DF option - it has no IP_DONTFRAG - so unlike Linux, macOS/BSD, QNX and Windows
    // it does not refuse an oversized datagram: it fragments or drops it according to its own
    // IP_FRAG build setting, silently either way. That makes lwIP the one transport where an
    // OPTION_MTU larger than the link MTU degrades measurement without any diagnostic, so check it
    // here. netif->mtu is the IP MTU, so the 20 byte IPv4 and 8 byte UDP headers are added.
    //
    // Reported once, not per datagram: this is the DAQ transmit path. Best effort - the default
    // netif is not necessarily the one routing to dst on a multi homed target, so a false report
    // is possible there, and it costs one log line and nothing else.
    if (netif_default != NULL && (uint32_t)bufferSize + 20u + 8u > (uint32_t)netif_default->mtu) {
        static bool mtu_reported = false;
        if (!mtu_reported) {
            mtu_reported = true;
            DBG_PRINTF_WARNING("socketSendTo: segment of %u bytes does not fit the link MTU of %u and lwIP will\n"
                               "  fragment or drop it. Reduce OPTION_MTU (currently %u, giving XCPTL_MAX_SEGMENT_SIZE=%u).\n",
                               (unsigned)bufferSize, (unsigned)netif_default->mtu, (unsigned)OPTION_MTU, (unsigned)XCPTL_MAX_SEGMENT_SIZE);
        }
    }

    int16_t n = (int16_t)lwip_sendto(socket, buffer, bufferSize, 0, (struct sockaddr *)&dst, sizeof(dst));
    if (n < 0) {
        int32_t err = errno;
        if (socketIsClosed(err)) {
            return 0; // Socket closed
        }
        DBG_PRINTF_ERROR("socketSendTo: lwip_sendto failed (errno=%d,%s)\n", err, socketGetErrorString(err));
        return -1;
    }
    return n;
#else
    DBG_PRINT_ERROR("FREE_RTOS:socketSendTo not implemented\n");
    return -1;
#endif
}

// Set receive timeout on a blocking socket
// timeoutMs == 0 restores infinite blocking
bool socketSetTimeout(SOCKET_HANDLE socket, uint32_t timeoutMs) {
#if defined(OPTION_FREERTOS_LWIP)
    assert(socket != INVALID_SOCKET_HANDLE);
    struct timeval tv;
    tv.tv_sec = (long)(timeoutMs / 1000U);
    tv.tv_usec = (long)(timeoutMs % 1000U) * 1000L;
    if (lwip_setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        DBG_PRINTF_WARNING("socketSetTimeout: lwip_setsockopt SO_RCVTIMEO failed (errno=%d,%s)\n", errno, socketGetErrorString(errno));
        return false;
    }
    DBG_PRINTF5("socketSetTimeout: set to %u ms\n", timeoutMs);
    return true;
#else
    DBG_PRINT_ERROR("FREE_RTOS:socketSetTimeout not implemented\n");
    return true;
#endif
}

#else

//--------------------------------------------------------------------------
// Non-Windows platforms
#if !defined(_WIN)

#include <ifaddrs.h> // for getifaddrs, struct ifaddrs

#include <arpa/inet.h>  // for htons, htonl
#include <netinet/in.h> // for sockaddr_in
#include <sys/socket.h> // for socket functions

#if defined(_LINUX) // Linux platform

#include <net/if.h>           // for if_nametoindex, struct ifreq, IFNAMSIZ
#include <netpacket/packet.h> // for struct sockaddr_ll (AF_PACKET, used by socketGetMAC)

#if defined(OPTION_SOCKET_HW_TIMESTAMPS) // Linux platform hardware time stamping support
#include <linux/errqueue.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h> // for SIOCSHWTSTAMP
#include <sys/ioctl.h>
#endif // defined(OPTION_SOCKET_HW_TIMESTAMPS)

#endif // Linux

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
    bool reuseaddr = flags & SOCKET_MODE_REUSEADDR;

    // Create a socket
    sock = socket(AF_INET, useTCP ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (sock < 0) {
        DBG_PRINT_ERROR("cannot open socket!\n");
        return 0;
    }

    if (reuseaddr) {
        int yes = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
            DBG_PRINTF_WARNING("Failed to enable SO_REUSEADDR on socket (errno=%d,%s)\n", errno, socketGetErrorString(errno));
        } else {
            DBG_PRINT5("SO_REUSEADDR enabled on socket\n");
        }
    }

    // Never fragment outgoing datagrams.
    // IPv4 fragmentation is actively harmful for DAQ: losing one fragment loses the whole
    // datagram, reassembly adds jitter and the reassembly buffers can overflow at DAQ rates.
    // Without this, an OPTION_MTU larger than the path MTU degrades measurement quality
    // silently and indefinitely. With it, socketSendTo fails with EMSGSIZE on the first
    // oversized segment, which is the diagnostic the user actually needs.
    // This also makes the socket transport behave like the raw Ethernet transport, which
    // cannot fragment at all (see docs/SOCKET_RAW.md).
    if (!useTCP) { // TCP does its own path MTU handling
#if defined(_LINUX)
        int pmtu = IP_PMTUDISC_DO; // always set DF, honour the discovered path MTU
        if (setsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER, &pmtu, sizeof(pmtu)) < 0) {
            DBG_PRINTF_WARNING("Failed to enable IP_MTU_DISCOVER on socket (errno=%d,%s), datagrams may be fragmented\n", errno, socketGetErrorString(errno));
        } else {
            DBG_PRINT5("IP_MTU_DISCOVER=IP_PMTUDISC_DO enabled, datagrams will not be fragmented\n");
        }
#elif defined(IP_DONTFRAG) // macOS, QNX and other BSD derived platforms
        int yes = 1;
        if (setsockopt(sock, IPPROTO_IP, IP_DONTFRAG, &yes, sizeof(yes)) < 0) {
            DBG_PRINTF_WARNING("Failed to enable IP_DONTFRAG on socket (errno=%d,%s), datagrams may be fragmented\n", errno, socketGetErrorString(errno));
        } else {
            DBG_PRINT5("IP_DONTFRAG enabled, datagrams will not be fragmented\n");
        }
#else
        DBG_PRINT5("Don't fragment not supported on this platform, datagrams may be fragmented\n");
#endif
    }

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
    if (flags & SOCKET_MODE_GET_IF_INFO) {
        int yes = 1;
        if (setsockopt(sock, IPPROTO_IP, IP_PKTINFO, &yes, sizeof(yes)) < 0) {
            DBG_PRINTF_WARNING("Failed to enable IP_PKTINFO on socket (errno=%d,%s)\n", errno, socketGetErrorString(errno));
        } else {
            DBG_PRINT5("IP_PKTINFO enabled\n");
        }
    }
#endif

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
            DBG_PRINTF_ERROR("Failed to enable socket hardware timestamps (SO_TIMESTAMPING, errno=%d,%s)\n", errno, socketGetErrorString(errno));
        } else {
            DBG_PRINTF5("Hardware timestamping enabled on socket (SO_TIMESTAMPING flags=0x%X)\n", flags);
        }
    }

    if (sw_timestamps) {

        // Enable software timestamps, if required
        int yes = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPNS, &yes, sizeof(yes)) < 0) {
            DBG_PRINTF_ERROR("Failed to enable socket software timestamps (SO_TIMESTAMPNS, errno=%d,%s)\n", errno, socketGetErrorString(errno));
        } else {
            DBG_PRINT5("Software timestamps enabled on socket (SO_TIMESTAMPNS)\n");
        }
    }
#endif

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
    SOCKET_HANDLE socket = (struct socket *)malloc(sizeof(struct socket));
    memset(socket, 0, sizeof(struct socket));
    socket->sock = sock;
    *socketp = socket;
#else
    *socketp = sock;
#endif
    return true;
}

bool socketBind(SOCKET_HANDLE socket, const uint8_t *addr, uint16_t port) {

    assert(socket != INVALID_SOCKET_HANDLE);
    assert(addr != NULL);

    SOCKET sock = SOCKET_FD(socket);

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
        DBG_PRINTF_ERROR("socketBind failed (errno=%d,%s) - cannot bind on %u.%u.%u.%u port %u!\n", socketGetLastError(), socketGetErrorString(socketGetLastError()),
                         addr ? addr[0] : 0, addr ? addr[1] : 0, addr ? addr[2] : 0, addr ? addr[3] : 0, port);
        if (port < 1024) {
            DBG_PRINT_ERROR("Binding to ports <1024 may require root privileges on Linux!\n");
        }
        return 0;
    }
    return true;
}

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)

// Bind socket to a specific network interface by name (Linux only)
// This is useful for multicast reception on a specific interface while binding to INADDR_ANY
// Requires root privileges on Linux
bool socketBindToDevice(SOCKET_HANDLE socket, const char *ifname) {

    assert(socket != INVALID_SOCKET_HANDLE);

    int sock = SOCKET_FD(socket);
    if (ifname != NULL && ifname[0] != '\0') {
        if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, ifname, strlen(ifname)) < 0) {
            DBG_PRINTF_ERROR("socketBindToDevice failed (errno=%d,%s) - cannot bind to device %s !\n", socketGetLastError(), socketGetErrorString(socketGetLastError()), ifname);
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
}

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

    DBG_PRINTF5("socketEnableTimestamps: Enabling timestamps on interface %s\n", ifr.ifr_name);

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

    DBG_PRINTF5("Hardware timestamping enabled on %s (tx_type=%d, rx_filter=%d)\n", ifr.ifr_name, hwconfig.tx_type, hwconfig.rx_filter);
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

#endif // Linux with OPTION_SOCKET_HW_TIMESTAMPS

// Shutdown socket
// Block rx and tx direction
bool socketShutdown(SOCKET_HANDLE socket) {
    if (socket != INVALID_SOCKET_HANDLE) {
        shutdown(SOCKET_FD(socket), SHUT_RDWR);
    }
    return true;
}

// Close socket
// Make addr reusable
bool socketClose(SOCKET_HANDLE *socketp) {
    assert(socketp != NULL);
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
    if (*socketp != NULL) {
        close((*socketp)->sock);
        free(*socketp);
        *socketp = NULL;
    }
#else
    if (*socketp != INVALID_SOCKET_HANDLE) {
        close(*socketp);
        *socketp = INVALID_SOCKET_HANDLE;
    }
#endif
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
                    DBG_PRINTF5("  %s: MAC = %02X-%02X-%02X-%02X-%02X-%02X\n", ifa->ifa_name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                }
#else
                if (ifa->ifa_addr->sa_family == AF_PACKET) {
                    struct sockaddr_ll *s = (struct sockaddr_ll *)ifa->ifa_addr;
                    memcpy(mac, s->sll_addr, 6);
                    DBG_PRINTF5("  %s: MAC = %02X-%02X-%02X-%02X-%02X-%02X\n", ifa->ifa_name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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
    char strbuf[64]; // @@@@ STACK buffer for IP addr string
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
#else  // Windows platform

// Winsock
#pragma comment(lib, "ws2_32.lib")

int32_t socketGetLastError(void) { return WSAGetLastError(); }

bool socketStartup(void) {

    int err;
    WORD wsaVersionRequested;
    WSADATA wsaData;

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
    bool reuseaddr = flags & SOCKET_MODE_REUSEADDR;

    // Create a socket
    if (!useTCP) {
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

// Avoid send to UDP nowhere problem (ignore ICMP host unreachable - server has no open socket on client port)
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
        DBG_PRINTF_ERROR("socketOpen failed (errno=%d,%s) - could not create socket!\n", socketGetLastError(), socketGetErrorString(socketGetLastError()));
        return false;
    }

    // Make addr reusable
    if (reuseaddr) {
        uint32_t one = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one)) < 0) {
            DBG_PRINTF_WARNING("socketOpen failed (errno=%d,%s) - could not enable SO_REUSEADDR on socket\n", socketGetLastError(), socketGetErrorString(socketGetLastError()));
        }
    }

    // Never fragment outgoing datagrams - see the comment in the POSIX socketOpen above
    if (!useTCP) {
        DWORD one = 1;
        if (setsockopt(sock, IPPROTO_IP, IP_DONTFRAGMENT, (const char *)&one, sizeof(one)) < 0) {
            DBG_PRINTF_WARNING("socketOpen failed (errno=%d,%s) - could not enable IP_DONTFRAGMENT, datagrams may be fragmented\n", socketGetLastError(),
                               socketGetErrorString(socketGetLastError()));
        }
    }

    *socketp = sock;
    return true;
}

bool socketBind(SOCKET_HANDLE socket, const uint8_t *addr, uint16_t port) {

    assert(socket != INVALID_SOCKET_HANDLE);
    SOCKET sock = socket;

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
            DBG_PRINTF_ERROR("socketBind failed (errno=%d,%s) - cannot bind on %u.%u.%u.%u port %u!\n", socketGetLastError(), socketGetErrorString(socketGetLastError()),
                             addr ? addr[0] : 0, addr ? addr[1] : 0, addr ? addr[2] : 0, addr ? addr[3] : 0, port);
        }
        return false;
    }
    return true;
}

// Shutdown socket
// Block rx and tx direction
bool socketShutdown(SOCKET_HANDLE socket) {

    assert(socket != INVALID_SOCKET_HANDLE);
    SOCKET sock = socket;

    if (sock != INVALID_SOCKET) {
        shutdown(sock, SD_BOTH);
    }
    return true;
}

// Close socket
// Make addr reusable
bool socketClose(SOCKET_HANDLE *socketp) {

    assert(socketp != NULL);
    if (*socketp != INVALID_SOCKET_HANDLE) {
        closesocket(*socketp);
        *socketp = INVALID_SOCKET_HANDLE;
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

// Set receive timeout on a socket
// timeoutMs: timeout in milliseconds, 0 = infinite blocking (restore default)
bool socketSetTimeout(SOCKET_HANDLE socket, uint32_t timeoutMs) {
    assert(socket != INVALID_SOCKET_HANDLE);
#if defined(_WIN)
    DWORD tv = (DWORD)timeoutMs;
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv)) < 0) {
        DBG_PRINTF_WARNING("socketSetTimeout: setsockopt SO_RCVTIMEO failed (errno=%d,%s)\n", socketGetLastError(), socketGetErrorString(socketGetLastError()));
        return false;
    }
#else
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (int32_t)(timeoutMs % 1000) * 1000;
    if (setsockopt(SOCKET_FD(socket), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        DBG_PRINTF_WARNING("socketSetTimeout: setsockopt SO_RCVTIMEO failed (errno=%d,%s)\n", errno, socketGetErrorString(errno));
        return false;
    }
#endif
    DBG_PRINTF5("socketSetTimeout: set to %u ms\n", timeoutMs);
    return true;
}

#if defined(OPTION_ENABLE_TCP)

// Listen on a TCP socket
bool socketListen(SOCKET_HANDLE socket) {
    assert(socket != INVALID_SOCKET_HANDLE);
    if (listen(SOCKET_FD(socket), 5)) {
        DBG_PRINTF_ERROR("socketListen failed (errno=%d,%s)!\n", socketGetLastError(), socketGetErrorString(socketGetLastError()));
        return 0;
    }
    return 1;
}

// Accept a connection on a listening TCP socket
// Returns the remote address if addr != NULL
SOCKET_HANDLE socketAccept(SOCKET_HANDLE listenSocket, uint8_t *addr) {
    assert(listenSocket != INVALID_SOCKET_HANDLE);
    struct sockaddr_in sa;
    socklen_t sa_size = sizeof(sa);
    SOCKET sock = accept(SOCKET_FD(listenSocket), (struct sockaddr *)&sa, &sa_size);
    if (addr)
        *(uint32_t *)addr = sa.sin_addr.s_addr;
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
    SOCKET_HANDLE socket = (struct socket *)malloc(sizeof(struct socket));
    memset(socket, 0, sizeof(struct socket));
    socket->sock = sock;
    socket->ifindex = listenSocket->ifindex;
    memcpy(socket->ifname, listenSocket->ifname, sizeof(socket->ifname));
    return socket;
#else
    return sock;
#endif
}

#endif // OPTION_ENABLE_TCP

#if !defined(_FREE_RTOS) || defined(FREE_RTOS_POSIX_SIM)

// Join a multicast group on a UDP socket
// maddr: Multicast group address (network byte order)
bool socketJoin(SOCKET_HANDLE socket, const uint8_t *maddr, const uint8_t *ifaddr, const char *ifname) {

    assert(socket != INVALID_SOCKET_HANDLE);
    SOCKET sock = SOCKET_FD(socket);

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
#if defined(OPTION_SOCKET_HW_TIMESTAMPS)
        socket->ifindex = group.imr_ifindex;
        strncpy(socket->ifname, ifname, sizeof(socket->ifname) - 1);
        socket->ifname[sizeof(socket->ifname) - 1] = '\0';
#endif
        DBG_PRINTF5("Joining multicast group on interface %s (index %d)\n", ifname, group.imr_ifindex);

#if defined(OPTION_SOCKET_HW_TIMESTAMPS)
        // Get MAC address for the interface and save it in the socket structure
        if (!socketGetMAC(socket->ifname, socket->ifmac)) {
            DBG_PRINTF_WARNING("socketJoin: Failed to get MAC address for interface %s!\n", ifname);
        }
#endif

    } else if (ifaddr != NULL && !(ifaddr[0] == 0 && ifaddr[1] == 0 && ifaddr[2] == 0 && ifaddr[3] == 0)) {
        // Use interface address
        group.imr_address.s_addr = *(uint32_t *)ifaddr;
#if defined(OPTION_SOCKET_HW_TIMESTAMPS)
        socket->ifaddr = *(uint32_t *)ifaddr;
#endif

        DBG_PRINTF5("Joining multicast group on interface address %u.%u.%u.%u\n", ifaddr[0], ifaddr[1], ifaddr[2], ifaddr[3]);

    } else {
        // Use INADDR_ANY (kernel picks interface based on routing)
        group.imr_address.s_addr = htonl(INADDR_ANY);

        DBG_PRINT5("Joining multicast group on INADDR_ANY\n");
    }

    if (0 > setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&group, sizeof(group))) {
        DBG_PRINTF_ERROR("socketJoin failed (errno=%d,%s) - can't set multicast socket option IP_ADD_MEMBERSHIP!\n", socketGetLastError(),
                         socketGetErrorString(socketGetLastError()));
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
        DBG_PRINTF_ERROR("socketJoin failed (errno=%d,%s) - can't set multicast socket option IP_ADD_MEMBERSHIP!\n", socketGetLastError(),
                         socketGetErrorString(socketGetLastError()));
        return 0;
    }
    (void)ifname; // Unused on non-Linux platforms
#endif
    return 1;
}

// Receive from UDP socket
// Blocking mode only, with optional timeout set with socketSetTimeout()
// Returns optional receive timestamps if (time != NULL)
// Support hardware timestamps if enabled on the socket and with OPTION_SOCKET_HW_TIMESTAMPS defined, otherwise system time is used
// Return values:
//   n > 0  : number of bytes received
//   n == 0 : timeout (set with socketTimeout) expired or would-block — no data yet, caller should loop and do background work
//   n < 0  : socket closed (graceful or reset) or unrecoverable error — caller should exit the receive loop
int16_t socketRecvFrom(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t bufferSize, uint8_t *addr, uint16_t *port, uint64_t *time) {

    assert(socket != INVALID_SOCKET_HANDLE);
    SOCKET sock = SOCKET_FD(socket);
    assert(sock != INVALID_SOCKET);

    SOCKADDR_IN src;
    src.sin_port = 0;
    src.sin_addr.s_addr = 0;

    int16_t n = 0;

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
    // Always use recvmsg() on Linux with HW_TIMESTAMPS: needed for IP_PKTINFO and optional timestamps.
    // Removing the if(time!=NULL) gate here is critical — without it the else clause
    // would dangle onto the port-extraction statement after #endif, causing no receive when time==NULL.
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

        // n = 0, zero-length UDP datagram, not a socket close, caller loops
        if (n == 0) {
            return 0; // Timeout — caller loops and does background work
        }

        // n < 0, error or timeout
        else if (n < 0) {
            int32_t err = socketGetLastError();
            if (socketTimeout(err)) {
                return 0; // Timeout — caller loops and does background work
            }
            DBG_PRINTF_ERROR("socketRecvFrom: recvmsg failed (errno=%d,%s, result=%d)!\n", err, socketGetErrorString(err), n);
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

        // n = 0, zero-length UDP datagram, not a socket close, caller loops
        if (n == 0) {
            return 0; // Timeout — caller loops and does background work
        } else if (n < 0) {
            int32_t err = socketGetLastError();
            // DBG_PRINTF6("socketRecvFrom: recvfrom returned n<0 (errno=%d,%s)\n", err, socketGetErrorString(err));

            if (socketTimeout(err)) {
                // DBG_PRINTF6("socketRecvFrom: recvfrom returned n<0, (errno=%d,%s), socket timeout, return 0\n", err, socketGetErrorString(err));
                return 0; // Timeout
            }

            DBG_PRINTF_ERROR("socketRecvFrom: failed n=%d (errno=%u,%s) , return -1\n", n, err, socketGetErrorString(err));
            return -1;
        }

        if (time != NULL) {
            assert(false && "Hardware timestamp are not enabled, would return system time");
            *time = clockGet();
        }
    }
#endif

    if (port)
        *port = htons(src.sin_port);
    if (addr)
        memcpy(addr, &src.sin_addr.s_addr, 4);

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
    DBG_PRINTF6("socketRecvFrom: sock=%d, ifindex=%d returned n=%u, time=%" PRIu64 "\n", sock, socket->ifindex, n, time ? *time : 0);
#else
    DBG_PRINTF6("socketRecvFrom: sock=%d returned n=%u, time=%" PRIu64 "\n", sock, n, time ? *time : 0);
#endif

    return n;
}

// Receive from TCP socket
// Blocking mode only, with optional timeout set with socketSetTimeout()
// For UDP use socketRecvFrom() instead, which also returns the source address and supports timestamps
// Return values:
//   n > 0  : number of bytes received
//   n == 0 : timeout (set with socketTimeout) expired or would-block — no data yet, caller should loop and do background work
//   n < 0  : socket closed (graceful or reset) or unrecoverable error — caller should exit the receive loop
#if defined(OPTION_ENABLE_TCP)
int16_t socketRecv(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t buffer_size, bool waitAll) {

    assert(socket != INVALID_SOCKET_HANDLE);
    // assert(socket->flags & SOCKET_MODE_TCP); // Use socketRecvFrom() for UDP sockets
    assert(buffer_size > 0);
    SOCKET sock = SOCKET_FD(socket);
    assert(sock != INVALID_SOCKET);

    if (!waitAll) {
        int16_t n = (int16_t)recv(sock, (char *)buffer, buffer_size, 0);

        // n = 0, socket close
        if (n == 0) {
            DBG_PRINT6("socketRecv: recv returned n=0, socket closed, return -1\n");
            return -1; // Socket closed
        }

        // n < 0, error or timeout
        else if (n < 0) {
            int32_t err = socketGetLastError();
            if (socketTimeout(err)) {
                DBG_PRINTF_ERROR("socketRecv: recv returned n<0, socket timeout (errno=%d,%s), return 0\n", err, socketGetErrorString(err));
                return 0; // Timeout, no data yet
            }
            DBG_PRINTF_ERROR("socketRecv: recv returned n<0, socket error (errno=%d,%s), return -1\n", err, socketGetErrorString(err));
            return -1; // Error
        }
        return n;
    }

    // waitAll: loop until exactly `size` bytes have been received.
    // MSG_WAITALL alone is not sufficient when SO_RCVTIMEO is set
    // Linux may return a partial size if the timeout fires mid-read.
    // We therefore implement a loop on top and return the timeout to the caller only when there is no data yet
    uint16_t received = 0;
    uint32_t timeout_counter = 0;
    for (;;) {
        int16_t n = (int16_t)recv(sock, (char *)buffer + received, (uint16_t)(buffer_size - received), MSG_WAITALL);

        // n = 0, socket close
        if (n == 0) {
            DBG_PRINT6("socketRecv: recv waitall returned n=0, socket closed, return -1\n");
            return -1; // Socket closed
        }

        // n < 0, error or timeout
        else if (n < 0) {
            int32_t err = socketGetLastError();
            if (socketTimeout(err)) {
                DBG_PRINTF6("socketRecv: recv waitall returned n<0, socket timeout (errno=%d,%s), return 0\n", err, socketGetErrorString(err));
                if (received == 0) {
                    return 0; // Timeout only before any data ok
                }
                DBG_PRINT_ERROR("socketRecv: recv waitall returned n<0, timeout mid-frame, return -1\n");
                return -1; // Partial frame received — TCP stream is desynchronised
            }
            DBG_PRINTF_ERROR("socketRecv: recv waitall returned n<0, socket error (errno=%d,%s), return -1\n", err, socketGetErrorString(err));
            return -1; // Error
        }

        received = (uint16_t)(received + (uint16_t)n);
        if (received >= buffer_size) {
            break; // done
        }

        if (++timeout_counter >= 4) {
            DBG_PRINT_ERROR("socketRecv: recv waitall timeout mid-frame, giving up after 4 attempts\n");
            break; // loop protection: should never happen
        }

        DBG_PRINTF_WARNING("socketRecv waitall: received %u bytes, waiting for %u more\n", received, buffer_size - received);
    }

    assert(received == buffer_size);
    return (int16_t)received;
}
#endif // OPTION_ENABLE_TCP

// Send datagram on UDP socket
// Returns number of bytes sent or -1 on error
// Requests and may returns optional send time if (time != NULL)
// Support hardware timestamps if enabled on the socket and with OPTION_SOCKET_HW_TIMESTAMPS defined, otherwise system time is used
// If *time = 0 on return, no timestamp is available yet, but can be obtained with socketGetSendTime()
// On non-Linux platforms, *time is set to system time at send
// Returns total number of bytes sent, 0 on socket closed or -1 on error
int16_t socketSendTo(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t size, const uint8_t *addr, uint16_t port, uint64_t *time) {

    assert(socket != INVALID_SOCKET_HANDLE);
    SOCKET sock = SOCKET_FD(socket);
    assert(sock != INVALID_SOCKET);

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
    DBG_PRINTF6("socketSendTo: sock=%d, ifindex=%d\n", sock, socket->ifindex);
#else
    DBG_PRINTF6("socketSendTo: sock=%d\n", sock);
#endif

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
            if (socketWouldBlock(err)) {
                DBG_PRINT_ERROR("socketSendTo: unexpected WBLOCK\n");
                return -1; // Should never happen on a blocking socket
            }
            if (socketIsClosed(err)) {
                DBG_PRINTF6("socketSendTo: socket closed (errno=%d,%s)\n", err, socketGetErrorString(err));
                return 0; // Transmit socket closed
            }
            DBG_PRINTF_ERROR("socketSendTo: sendmsg failed with errno=%d,%s!\n", err, socketGetErrorString(err));
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
        if (socketWouldBlock(err)) {
            DBG_PRINT_ERROR("socketSendTo: unexpected WBLOCK\n");
            return -1; // Should never happen on a blocking socket
        }
        if (socketIsClosed(err)) {
            DBG_PRINTF6("socketSendTo: socket closed (errno=%d,%s)\n", err, socketGetErrorString(err));
            return 0; // Transmit socket closed
        }
        // EMSGSIZE means the datagram exceeds the path MTU and the DF bit set in socketOpen
        // forbids fragmenting it. That is a configuration problem, not a transient error.
        if (err == SOCKET_ERROR_MSGSIZE) {
            DBG_PRINTF_ERROR("socketSendTo: segment of %u bytes exceeds the path MTU and must not be fragmented.\n"
                             "  Reduce OPTION_MTU (currently %u, giving XCPTL_MAX_SEGMENT_SIZE=%u) to fit the link.\n",
                             (unsigned)size, (unsigned)OPTION_MTU, (unsigned)XCPTL_MAX_SEGMENT_SIZE);
            return -1;
        }
        DBG_PRINTF_ERROR("socketSendTo: sendto failed with errno=%d,%s!\n", err, socketGetErrorString(err));
        return -1;
    }
    return (int16_t)n;
}

// Send buffer on a TCP socket
// Thread safe
// Returns total number of bytes sent, 0 on socket closed or -1 on error
#if defined(OPTION_ENABLE_TCP)
int16_t socketSend(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t size) {

    assert(socket != INVALID_SOCKET_HANDLE);
    SOCKET sock = SOCKET_FD(socket);
    assert(sock != INVALID_SOCKET);

    ssize_t n = send(sock, (const char *)buffer, size, 0);
    if (n < 0) {
        int32_t err = socketGetLastError();
        if (socketWouldBlock(err)) {
            DBG_PRINT_ERROR("socketSend: unexpected WBLOCK\n");
            return -1; // Should never happen on a blocking socket
        }
        if (socketIsClosed(err)) {
            DBG_PRINTF6("socketSend: socket closed (errno=%d,%s)\n", err, socketGetErrorString(err));
            return 0; // Transmit socket closed
        }
        DBG_PRINTF_ERROR("socketSend: send failed with errno=%d,%s!\n", err, socketGetErrorString(err));
        return -1;
    }
    return (int16_t)n;
}
#endif // OPTION_ENABLE_TCP

#endif // !defined(_FREE_RTOS) || defined(FREE_RTOS_POSIX_SIM)

// Vectored IO send and receive functions using sendmsg/recvmsg with iovec for efficient scatter-gather I/O
#if !defined(_WIN) && !defined(_FREE_RTOS)

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

    assert(socket != INVALID_SOCKET_HANDLE);
    SOCKET sock = SOCKET_FD(socket);
    assert(sock != INVALID_SOCKET);

    SOCKADDR_IN sa;
    sa.sin_family = AF_INET;
    memcpy(&sa.sin_addr.s_addr, addr, 4);
    sa.sin_port = htons(port);

    // Build iovec array on the stack - VLAs are acceptable here as count is usually small
    struct iovec iov[count];
    uint32_t total = 0;
    for (uint16_t i = 0; i < count; i++) {
        iov[i].iov_base = (void *)buffers[i].buffer;
        iov[i].iov_len = buffers[i].size;
        total += buffers[i].size;
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
        if (socketWouldBlock(err)) {
            DBG_PRINT_ERROR("socketSendToV: unexpected WBLOCK\n");
            return -1; // Should never happen on a blocking socket
        }
        if (socketIsClosed(err)) {
            DBG_PRINTF6("socketSendToV: socket closed (errno=%d,%s)\n", err, socketGetErrorString(err));
            return 0; // Transmit socket closed
        }
        if (err == SOCKET_ERROR_MSGSIZE) {
            DBG_PRINTF_ERROR("socketSendToV: segment of %" PRIu32 " bytes exceeds the path MTU and must not be fragmented.\n"
                             "  Reduce OPTION_MTU (currently %u, giving XCPTL_MAX_SEGMENT_SIZE=%u) to fit the link.\n",
                             total, (unsigned)OPTION_MTU, (unsigned)XCPTL_MAX_SEGMENT_SIZE);
            return -1;
        }
        DBG_PRINTF_ERROR("socketSendToV: sendmsg failed with errno=%d,%s!\n", err, socketGetErrorString(err));
        return -1;
    }
    if (total != n) {
        DBG_PRINTF_WARNING("socketSendToV: partial send, sent %" PRIu32 " of %" PRIu32 " bytes\n", (uint32_t)n, total);
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

    assert(socket != INVALID_SOCKET_HANDLE);
    SOCKET sock = SOCKET_FD(socket);
    assert(sock != INVALID_SOCKET);

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
    int32_t total = 0;
    for (;;) {
        ssize_t n = sendmsg(sock, &msg, 0);
        if (n < 0) {
            int32_t err = socketGetLastError();
            if (socketWouldBlock(err)) {
                DBG_PRINT_ERROR("socketSendV: unexpected WBLOCK\n");
                return -1; // Should never happen on a blocking socket
            }
            if (socketIsClosed(err)) {
                DBG_PRINTF6("socketSendV: socket closed (errno=%d,%s)\n", err, socketGetErrorString(err));
                return 0; // Transmit socket closed
            }
            DBG_PRINTF_ERROR("socketSendV: sendmsg failed with errno=%d,%s!\n", err, socketGetErrorString(err));
            return -1;
        }
        total += (int32_t)n;

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

    return (int16_t)total;
}

#endif // !defined(_WIN) && !defined(_FREE_RTOS)

// Get send time of last sent packet
// Retrieves TX hardware timestamp and kernel software timestamp from socket error queue
// Returns false if no timestamp available or on error
// On non-Linux platforms, this function always returns false
// On Linux, requires OPTION_SOCKET_HW_TIMESTAMPS defined and hardware timestamping enabled on the socket
// hw_time and sw_time are optional, set to NULL if not needed
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
bool socketGetSendTime(SOCKET_HANDLE socket, uint64_t *hw_time, uint64_t *sw_time) {

    assert(socket != NULL);
    SOCKET sock = socket->sock;
    assert(sock != INVALID_SOCKET);

    if (hw_time)
        *hw_time = 0;
    if (sw_time)
        *sw_time = 0;

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
}
#endif // defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)

#endif // !_WIN (closes the #else of #if _FREE_RTOS && !FREE_RTOS_POSIX_SIM)

#endif // OPTION_ENABLE_TCP || OPTION_ENABLE_UDP && !OPTION_ENABLE_UDP_RAW
