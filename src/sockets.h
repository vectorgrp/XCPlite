#pragma once
#define __SOCKETS_H__

/*----------------------------------------------------------------------------
| File:
|   sockets.h
|
| Description:
|   Platform socket abstraction layer (Linux/Windows/macOS/QNX/FreeRTOS)
|
|   Requires OPTION_ENABLE_TCP and/or OPTION_ENABLE_UDP, or OPTION_ENABLE_UDP_RAW
|   (mutually exclusive) — the entire API is compiled away without one of them.
|
|   Build variants and supported functions:
|
|   _FREE_RTOS && !FREE_RTOS_POSIX_SIM (bare-metal FreeRTOS):
|     OPTION_FREERTOS_LWIP defined:
|       socketStartup, socketCleanup, socketOpen (UDP only, TCP not supported),
|       socketBind, socketShutdown, socketClose,
|       socketRecvFrom, socketSendTo, socketSetTimeout
|     OPTION_FREERTOS_LWIP not defined:
|       All functions are error stubs — the caller must provide the implementation.
|
|   POSIX: _LINUX / _MACOS / _QNX  (and FREE_RTOS_POSIX_SIM):
|     Base:
|       socketStartup (no-op), socketCleanup (no-op),
|       socketOpen, socketBind, socketShutdown, socketClose,
|       socketGetMAC, socketSetTimeout,
|       socketJoin, socketRecvFrom, socketSendTo
|     + OPTION_ENABLE_TCP:
|       socketListen, socketAccept, socketRecv, socketSend
|     + !_FREE_RTOS (scatter-gather via sendmsg):
|       socketSendToV, socketSendV
|     + OPTION_ENABLE_GET_LOCAL_ADDR:
|       socketGetLocalAddr
|     + _LINUX && OPTION_SOCKET_HW_TIMESTAMPS:
|       SOCKET_HANDLE becomes struct socket* (fd + interface metadata)
|       socketBindToDevice, socketEnableTimestamps, socketGetSendTime
|       socketRecvFrom uses recvmsg with SO_TIMESTAMPING / SO_TIMESTAMPNS
|       socketSendTo   uses sendmsg with per-packet timestamp request
|
|   _WIN (Windows / Winsock2):
|     Base:
|       socketStartup (WSAStartup), socketCleanup (WSACleanup),
|       socketOpen, socketBind, socketShutdown, socketClose,
|       socketSetTimeout, socketJoin, socketRecvFrom, socketSendTo
|     + OPTION_ENABLE_TCP:
|       socketListen, socketAccept, socketRecv, socketSend
|     + OPTION_ENABLE_GET_LOCAL_ADDR:
|       socketGetLocalAddr
|     No scatter-gather I/O (sendmsg not available on Windows).
|     No hardware timestamping.
|
|   OPTION_ENABLE_UDP_RAW (raw Ethernet, mutually exclusive with the above):
|     A hand-crafted UDP/IPv4 layer over a raw Ethernet HAL, for targets without
|     any TCP/IP stack. Implemented in socket_raw.c, HAL in socket_raw_hal*.c.
|     Requires OPTION_QUEUE_32 - the 64 bit queues use the vectored send path,
|     which this variant does not provide. Enforced in xcptl_cfg.h.
|     Supported subset - see docs/SOCKET_RAW.md for the full design:
|       socketStartup, socketCleanup, socketGetErrorString,
|       socketOpen, socketBind, socketShutdown, socketClose,
|       socketRecvFrom, socketSendTo, socketSetTimeout
|     Not provided: TCP (socketListen/Accept/Recv/Send), multicast (socketJoin),
|       vectored I/O (socketSendToV/socketSendV), hardware timestamps,
|       socketGetMAC, socketGetLocalAddr.
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "platform.h" // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex, spinlock

#ifdef __cplusplus
extern "C" {
#endif

// Platform independent socket functions

#if defined(OPTION_ENABLE_TCP) || defined(OPTION_ENABLE_UDP) || defined(OPTION_ENABLE_UDP_RAW)

// Note:
// SOCKET_HANDLE is an opaque type that may wrap the OS socket handle and additional info (e.g. for Linux hardware timestamping)
// INVALID_SOCKET_HANDLE is the invalid value for SOCKET_HANDLE
// SOCKET_FD(s) extracts the raw OS socket fd from a SOCKET_HANDLE (which may be a struct socket pointer on Linux with HW timestamps)

#if defined(OPTION_ENABLE_UDP_RAW) // Raw Ethernet transport (no OS socket API)

// SOCKET_HANDLE is an opaque context defined in socket_raw.c
// There is no OS socket fd, therefore SOCKET_FD() is deliberately not defined
#include "xcptl_cfg.h" // for XCPTL_TX_HEADROOM

struct socket_raw;
typedef struct socket_raw *SOCKET_HANDLE;
#define INVALID_SOCKET_HANDLE NULL

// Self contained error codes - no <errno.h> on bare metal targets
#define SOCKET_ERROR_NONE 0
#define SOCKET_ERROR_TIMEDOUT 1
#define SOCKET_ERROR_BADF 2
#define SOCKET_ERROR_NOTCONN 3
#define SOCKET_ERROR_HAL 4
#define SOCKET_ERROR_TOOBIG 5
#define SOCKET_ERROR_NOPEER 6
#define SOCKET_ERROR_MSGSIZE 7 // frame exceeds what the link can carry, no fragmentation

// Last error of the calling context, set by the raw socket functions
int32_t socketGetLastError(void);

#define socketIsClosed(err) ((err) == SOCKET_ERROR_BADF || (err) == SOCKET_ERROR_NOTCONN)
#define socketWouldBlock(err) ((err) == SOCKET_ERROR_TIMEDOUT)
#define socketTimeout(err) ((err) == SOCKET_ERROR_TIMEDOUT)

#elif !defined(_WIN) // Non-Windows platform sockets

#if !defined(_WIN) && !defined(_FREE_RTOS)
#include "queue.h" // for tQueueBuffer
#endif

#define SOCKET int
#define INVALID_SOCKET (-1)

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
// For Linux hardware timestamping support, SOCKET_HANDLE is a pointer to struct socket which contains the socket fd and interface info for timestamp retrieval
struct socket {
    SOCKET sock;
    uint32_t addr;        // Bind address (network byte order) maybe INADDR_ANY
    uint16_t port;        // Port
    unsigned int ifindex; // Interface index
    char ifname[16];      // Interface name
    uint32_t ifaddr;      // Interface address
    uint8_t ifmac[6];     // Interface MAC address
};
typedef struct socket *SOCKET_HANDLE;
#define INVALID_SOCKET_HANDLE NULL
#define SOCKET_FD(s) ((s)->sock) // Extract the OS socket fd from a SOCKET_HANDLE
#else
// Linux (without HW timestamps), FreeRTOS, macOS, QNX: SOCKET_HANDLE is the raw OS fd
typedef SOCKET SOCKET_HANDLE;
#define INVALID_SOCKET_HANDLE INVALID_SOCKET
#define SOCKET_FD(s) (s) // Extract the OS socket fd from a SOCKET_HANDLE
#endif

#define SOCKADDR_IN struct sockaddr_in
#define SOCKADDR struct sockaddr

#undef htonll
#define htonll(val) ((((uint64_t)htonl((uint32_t)(val))) << 32) + htonl((uint32_t)((val) >> 32)))

#include <errno.h> // for errno and error codes from socketGetLastError

#define SOCKET_ERROR_ABORT ECONNABORTED // 53
#define SOCKET_ERROR_RESET ECONNRESET   // 54
#define SOCKET_ERROR_INTR EINTR         // 4
#define SOCKET_ERROR_TIMEDOUT ETIMEDOUT // 60
#define SOCKET_ERROR_WBLOCK EAGAIN      // 35 EWOULDBLOCK is the same as EAGAIN on Linux, but may be different on other platforms
#define SOCKET_ERROR_PIPE EPIPE         // 32
#define SOCKET_ERROR_BADF EBADF         // 9
#define SOCKET_ERROR_NOTCONN ENOTCONN   // 107 (57 macOS) Socket is not connected
#define SOCKET_ERROR_MSGSIZE EMSGSIZE   // 90 (40 macOS) Datagram too large for the path MTU (DF is set, see socketOpen)

#define socketGetLastError(void) errno
#define socketIsClosed(err) ((err) == ENOTCONN || (err) == ECONNABORTED || (err) == EBADF || (err) == ECONNRESET)
#define socketWouldBlock(err) ((err) == EAGAIN || (err) == EWOULDBLOCK)
#define socketTimeout(err) ((err) == ETIMEDOUT || (err) == EAGAIN || (err) == EWOULDBLOCK || (err) == EINTR)

#else // Windows sockets

#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET SOCKET_HANDLE;
#define INVALID_SOCKET_HANDLE INVALID_SOCKET
#define SOCKET_FD(s) (s)

#define SOCKADDR_IN struct sockaddr_in
#define SOCKADDR struct sockaddr

#include <errno.h>                         // for errno and error codes from socketGetLastError
int32_t socketGetLastError(void);
#define SOCKET_ERROR_ABORT WSAECONNABORTED // 10053
#define SOCKET_ERROR_RESET WSAECONNRESET   // 10054
#define SOCKET_ERROR_INTR WSAEINTR         // 10004
#define SOCKET_ERROR_TIMEDOUT WSAETIMEDOUT // 10060
#define SOCKET_ERROR_WBLOCK WSAEWOULDBLOCK // 10035
#define SOCKET_ERROR_PIPE WSAESHUTDOWN     // 10058
#define SOCKET_ERROR_BADF WSAEBADF         // 10009
#define SOCKET_ERROR_NOTCONN WSAENOTCONN   // 10057
#define SOCKET_ERROR_MSGSIZE WSAEMSGSIZE   // 10040 Datagram too large for the path MTU (DF is set, see socketOpen)
#define socketIsClosed(err) ((err) == WSAECONNABORTED || (err) == WSAEBADF || (err) == WSAECONNRESET || (err) == WSAEINTR)
#define socketWouldBlock(err) ((err) == WSAEWOULDBLOCK)
#define socketTimeout(err) ((err) == WSAETIMEDOUT)

#define ssize_t int

#endif

// Socket mode flags
#define SOCKET_MODE_TCP (1 << 0)       // TCP socket
#define SOCKET_MODE_REUSEADDR (1 << 2) // Allow reuse of local address
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
#define SOCKET_MODE_GET_IF_INFO (1 << 6)     // Enable IP_PKTINFO to identify the receiving interface (Linux only)
#define SOCKET_MODE_HW_TIMESTAMPING (1 << 4) // Enable hardware timestamping (Linux only, requires root)
#define SOCKET_MODE_SW_TIMESTAMPING (1 << 5) // Enable kernel software timestamping (Linux only, requires root)
#endif

// Socket functions

// Initialize the socket subsystem (Windows: WSAStartup; no-op on POSIX)
// Must be called once before any other socket function
// Returns true on success
bool socketStartup(void);

// Clean up the socket subsystem (Windows: WSACleanup; no-op on POSIX)
void socketCleanup(void);

// Return a static human-readable string for a SOCKET_ERROR_* error code
// Returns "unknown socket error" for unrecognized codes
const char *socketGetErrorString(int32_t err);

// Create a TCP or UDP socket with the given SOCKET_MODE_xxx flags
// Sockets are always created in blocking mode, a timeout may be set with socketSetTimeout()
// SOCKET_MODE_TCP: TCP stream socket (default: UDP datagram)
// SOCKET_MODE_REUSEADDR: set SO_REUSEADDR to allow rapid port reuse after restart
// SOCKET_MODE_HW_TIMESTAMPING / SOCKET_MODE_SW_TIMESTAMPING: enable timestamps (Linux with hardware timestamps only)
// SOCKET_MODE_GET_IF_INFO: enable IP_PKTINFO to identify the receiving interface (Linux with hardware timestamps only)
// Returns true on success
bool socketOpen(SOCKET_HANDLE *socketp, uint16_t flags);

// Bind socket to a local address and port
// addr: network-byte-order IPv4 address; NULL or 0.0.0.0 binds to INADDR_ANY
// Returns true on success
bool socketBind(SOCKET_HANDLE socket, const uint8_t *addr, uint16_t port);

// Bind socket to a specific network interface by name (Linux only, requires root)
// Useful for multicast reception on a specific interface when bound to INADDR_ANY
// ifname: interface name, e.g. "eth0"; NULL or empty string is a no-op
// Returns true on success (returns true with a warning on non-Linux platforms)
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
bool socketBindToDevice(SOCKET_HANDLE socket, const char *ifname);
#endif

// Configure the NIC driver to generate hardware timestamps (Linux only, requires root)
// Must be called after socketBind; uses the interface name stored by socketBind/socketBindToDevice
// ptpOnly: true = timestamp PTP event packets only; false = timestamp all packets
// Falls back gracefully if the NIC does not support hardware timestamps
// Returns true on success
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
bool socketEnableTimestamps(SOCKET_HANDLE socket, bool ptpOnly);
#endif

#if !defined(OPTION_ENABLE_UDP_RAW) // not provided by the raw Ethernet transport

// Join an IPv4 multicast group on a UDP socket
// maddr: multicast group address (network byte order)
// Interface selection priority: ifname > ifaddr > INADDR_ANY (kernel routing)
// Returns true on success
bool socketJoin(SOCKET_HANDLE socket, const uint8_t *maddr, const uint8_t *ifaddr, const char *ifname);

// Start listening for incoming TCP connections
// Returns true on success
bool socketListen(SOCKET_HANDLE socket);

// Accept an incoming TCP connection (blocking)
// addr: filled with the remote IPv4 address (network byte order) if non-NULL
// Returns a new connected SOCKET_HANDLE; the caller is responsible for closing it
SOCKET_HANDLE socketAccept(SOCKET_HANDLE socket, uint8_t *addr);

// Receive from a TCP socket (blocking)
// waitAll: true = MSG_WAITALL, block until bufferSize bytes arrive
// Return values:  > 0  bytes received
//                == 0  timeout (set with socketSetRecvTimeout) — no data yet, do background work and loop
//                 < 0  socket closed (graceful or reset) or error — check with socketIsClosed(socketGetLastError()) and exit the receive loop
int16_t socketRecv(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t bufferSize, bool waitAll);

#endif // !OPTION_ENABLE_UDP_RAW

// Receive a UDP datagram (blocking)
// srcAddr / srcPort: filled with sender's address/port if non-NULL
// time: optional receive timestamp (NULL to skip); hardware or software depending on socket flags
// Return values:  > 0  bytes received
//                == 0  timeout (set with socketSetRecvTimeout) — no data yet, do background work and loop
//                 < 0  socket closed or error — check with socketIsClosed(socketGetLastError()) and exit the receive loop
int16_t socketRecvFrom(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t bufferSize, uint8_t *srcAddr, uint16_t *srcPort, uint64_t *time);

// Send a UDP datagram to addr:port
// time: optional send timestamp (NULL to skip)
//       on Linux with HW timestamps: *time is set to 0; call socketGetSendTime() afterwards to retrieve it
//       on other platforms: *time is set to the system clock at send time
// Returns: bytes sent, 0 on closed socket, -1 on error
int16_t socketSendTo(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t bufferSize, const uint8_t *addr, uint16_t port, uint64_t *time);

#if !defined(OPTION_ENABLE_UDP_RAW) // not provided by the raw Ethernet transport
// Send data on a TCP socket (blocking; loops internally on partial sends)
// Returns: bytes sent, 0 on closed socket, -1 on error
int16_t socketSend(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t bufferSize);
#endif

#if !defined(_WIN) && !defined(_FREE_RTOS) && !defined(OPTION_ENABLE_UDP_RAW)
// Send multiple buffers as a single UDP datagram (scatter-gather I/O via sendmsg, POSIX only)
// Returns: total bytes sent, 0 on closed socket, -1 on error (partial UDP sends treated as error)
int16_t socketSendToV(SOCKET_HANDLE socket, tQueueBuffer buffers[], uint16_t count, const uint8_t *addr, uint16_t port);

// Send multiple buffers on a TCP socket (scatter-gather I/O via sendmsg, POSIX only)
// Loops internally until all data is accepted by the kernel
// Returns: total bytes sent, 0 on closed socket, -1 on error
int16_t socketSendV(SOCKET_HANDLE socket, tQueueBuffer buffers[], uint16_t count);
#endif

// Retrieve TX hardware and/or software timestamp after socketSendTo (Linux only)
// Must be called shortly after socketSendTo returned *time==0
// Requires OPTION_SOCKET_HW_TIMESTAMPS and socketEnableTimestamps() to have been called
// txHwTime / txSwTime: set to 0 if the respective timestamp is not available; NULL to skip
// Returns true if at least one requested timestamp was successfully retrieved
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
bool socketGetSendTime(SOCKET_HANDLE socket, uint64_t *txHwTime, uint64_t *txSwTime);
#endif

#if defined(OPTION_ENABLE_UDP_RAW)
// Select the Ethernet interface used by the raw Ethernet transport
// config: backend specific and opaque, e.g. the interface name "eth0" for the Linux
//         AF_PACKET backend. NULL restores the OPTION_UDP_RAW_IFNAME default.
// Must be called before XcpEthServerInit(); the string is not copied and must stay valid.
void socketRawSetInterface(const char *config);

// Get the local MAC address of the raw Ethernet transport, as reported by its HAL
// mac: output buffer, must point to at least 6 bytes
// Returns true on success
bool socketRawGetLocalMac(SOCKET_HANDLE socket, uint8_t *mac);

#if XCPTL_TX_HEADROOM > 0
// Send a UDP datagram from a buffer which has writable headroom in front of it (zero copy).
// PRECONDITION: buffer must have XCPTL_TX_HEADROOM writable bytes immediately before it.
// The Ethernet/IPv4/UDP header is written there in place and the payload is not copied.
// Used for the DAQ transmit path, where the buffer is a transmit queue segment (see queue.h).
// Command responses are built on the stack, have no headroom, and use socketSendTo instead.
// Returns: bytes sent (the payload size), 0 on closed socket, -1 on error
int16_t socketSendToReserved(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t bufferSize, const uint8_t *addr, uint16_t port, uint64_t *time);
#endif
#endif

// Set receive timeout on a blocking socket
// timeoutMs: timeout in milliseconds; 0 = restore infinite blocking
// With a timeout set, socketRecv/socketRecvFrom return 0 on expiry instead of blocking indefinitely,
// allowing the receive thread to perform background work before looping back
// Works for both TCP and UDP; use socketShutdown() to signal a receive thread to exit
// Returns true on success
bool socketSetTimeout(SOCKET_HANDLE socket, uint32_t timeoutMs);

// Shut down both directions of the socket (SHUT_RDWR / SD_BOTH)
// Unblocks a thread currently blocked in socketRecv or socketRecvFrom, causing it to return -1
bool socketShutdown(SOCKET_HANDLE socket);

// Close the OS socket, free the SOCKET_HANDLE, and set *socketp to NULL
// Returns true on success
bool socketClose(SOCKET_HANDLE *socketp);

#if !defined(OPTION_ENABLE_UDP_RAW) // the raw transport reads the MAC from its Ethernet HAL
// Get the MAC address of a network interface by name (e.g. "eth0")
// mac: output buffer, must point to at least 6 bytes
// Returns true on success
bool socketGetMAC(char *ifname, uint8_t *mac);
#endif

#ifdef OPTION_ENABLE_GET_LOCAL_ADDR
// Get the IPv4 address and MAC of the first non-loopback Ethernet interface
// mac / addr: output buffers (6 / 4 bytes respectively); either may be NULL
// Result is cached after the first successful call
// Returns true on success
bool socketGetLocalAddr(uint8_t *mac, uint8_t *addr);
#endif

#endif

#ifdef __cplusplus
} // extern "C"
#endif
