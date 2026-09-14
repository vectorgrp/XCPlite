#pragma once

/*----------------------------------------------------------------------------
| File:
|   socket_raw_hal.h
|
| Description:
|   Raw Ethernet HAL for the OPTION_ENABLE_UDP_RAW transport (src/socket_raw.c)
|
|   socket_raw.c implements UDP/IPv4, ARP and ICMP on top of this interface.
|   A port has to provide send/receive of complete Ethernet frames and the local
|   MAC address - nothing else. Backends:
|     socket_raw_hal_linux.c  Linux AF_PACKET (development and test)
|     socket_raw_hal_xlapi.c  Vector XLAPI on Windows      (future)
|     socket_raw_hal_cmp.c    ASAM CMP capture modules     (future)
|
|   Frame contract:
|     Frames are complete Ethernet frames WITHOUT FCS: dst MAC, src MAC, EtherType,
|     payload. Frames as short as 50 bytes are passed to eth_hal_send(); if the MAC
|     of the port does not pad to the 60 byte Ethernet minimum, the port must do it.
|     The largest frame socket_raw.c will ever pass, and the max_len it offers to
|     eth_hal_recv(), is 42 + XCPTL_MAX_SEGMENT_SIZE, which is OPTION_MTU + 10
|     (1434 bytes with the OPTION_MTU of 1420 the raw configuration uses). Jumbo frames
|     are therefore supported by configuring OPTION_MTU accordingly. 802.1Q VLAN tags are not.
|     Whether the link can actually carry that frame is a RUNTIME property that only
|     the backend knows: if it cannot, eth_hal_send() returns ETH_HAL_ERROR_SIZE.
|     There is deliberately no compile time frame size limit, see docs/SOCKET_RAW.md.
|
|   Threading contract:
|     eth_hal_send()   always called with the transmit mutex of socket_raw.c held,
|                      therefore it does NOT need to be reentrant
|     eth_hal_recv()   called from the XCP receive thread only
|     eth_hal_wakeup() may be called from any thread
|
|   Backend specific configuration (interface name, XLAPI channel, CMP device and
|   stream id, ...) is passed as an opaque string to eth_hal_open() and parsed by
|   the backend. socket_raw.c never interprets it.
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include <stdbool.h> // for bool
#include <stdint.h>  // for uint8_t, uint16_t, int16_t

#include "platform.h" // for the platform defines (_LINUX, _WIN, ...) and OPTION_xxx

#ifdef OPTION_ENABLE_UDP_RAW

// Select the HAL backend
//
// OPTION_UDP_RAW_HAL_EXTERNAL lets an application provide its own backend out of tree: xcplib then
// selects none, and the eth_hal_* symbols stay undefined in libxcplite until the application links
// its own implementation against it. Use this for backends which do not belong in the library, for
// example ASAM CMP for testing XCP tools through capture modules, or a vendor specific interface.
// The application implements the functions declared below, compiles its own source file, and
// defines OPTION_UDP_RAW_HAL_EXTERNAL in its configuration override header.
#if defined(OPTION_UDP_RAW_HAL_EXTERNAL)
// The application provides the backend, nothing is selected here
#elif defined(_LINUX)
// socket_raw_hal_linux.c
#else
#error "OPTION_ENABLE_UDP_RAW has a HAL backend for Linux (AF_PACKET) only, or define OPTION_UDP_RAW_HAL_EXTERNAL to supply your own - see docs/SOCKET_RAW.md"
#endif

// Error returns of eth_hal_send() / eth_hal_recv()
// ETH_HAL_ERROR_SIZE is reported separately because it is a configuration problem, not a
// transient error: the frame is larger than the link can carry and there is no fragmentation.
#define ETH_HAL_ERROR (-1)
#define ETH_HAL_ERROR_SIZE (-2)

// Opaque per interface context of the HAL backend
typedef struct eth_hal_ctx tEthHalCtx;

// Open the Ethernet interface
// config: backend specific selector, may be NULL when the backend needs none
//         Linux: interface name, e.g. "eth0"
// Returns true on success, *ctx is then valid until eth_hal_close()
bool eth_hal_open(const char *config, tEthHalCtx **ctx);

// Close the Ethernet interface and release all resources
void eth_hal_close(tEthHalCtx *ctx);

// Get the MAC address of the interface
// mac: output buffer, must point to at least 6 bytes
// Returns true on success
bool eth_hal_get_mac(tEthHalCtx *ctx, uint8_t *mac);

// Send one complete Ethernet frame (without FCS)
// Returns: len on success
//          ETH_HAL_ERROR_SIZE if the frame exceeds what this interface can carry
//          ETH_HAL_ERROR on any other error
int16_t eth_hal_send(tEthHalCtx *ctx, const uint8_t *frame, uint16_t len);

// Receive one complete Ethernet frame (without FCS), blocking with timeout
// Frames sent by this application itself must not be returned
// timeout_ms: 0 = poll and return immediately
// Returns:  > 0  bytes received
//          == 0  timeout expired, no frame available
//           < 0  fatal error
int16_t eth_hal_recv(tEthHalCtx *ctx, uint8_t *frame, uint16_t max_len, uint32_t timeout_ms);

// Abort a blocked eth_hal_recv()
// Optional: a backend which can not do this may implement it as a no-op, socket_raw.c
// caps each eth_hal_recv() timeout slice so shutdown still works, just less promptly
void eth_hal_wakeup(tEthHalCtx *ctx);

#endif // OPTION_ENABLE_UDP_RAW
