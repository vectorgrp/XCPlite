#pragma once

/*----------------------------------------------------------------------------
| File:
|   xcplib_raw_cfg.h
|
| Description:
|   XCPlite configuration OVERRIDES for the raw Ethernet transport
|   Applied AFTER the defaults in xcplib_cfg.h via:
|     cmake: target_compile_definitions(xcplite PRIVATE "XCPLIB_CFG_OVERRIDE=\"xcplib_raw_cfg.h\"")
|
|   XCP on UDP/IPv4 implemented inside xcplib (src/socket_raw.c) on top of a thin
|   raw Ethernet HAL (src/socket_raw_hal.h), for targets which have no TCP/IP stack.
|   This configuration is the Linux development and test vehicle for that transport,
|   and the template for embedded ports (FreeRTOS/bare metal) or Vector XLAPI, ASAM CMP.
|
|   Key differences in overrides from the defaults in xcplib_cfg.h:
|     - OPTION_ENABLE_UDP_RAW instead of OPTION_ENABLE_UDP / OPTION_ENABLE_TCP
|     - OPTION_QUEUE_32 is MANDATORY (the 64 bit queues use the vectored send path
|       socketSendToV, which the raw transport does not implement)
|     - Standard Ethernet MTU less encapsulation headroom, no jumbo frames
|       (the raw transport does not fragment IPv4), see OPTION_MTU below
|   Addressing scheme:
|     Default (segment relative addressing on address extension 0)
|   Platform requirements:
|     Linux with CAP_NET_RAW (AF_PACKET). See docs/SOCKET_RAW.md for the test setup.
|   Examples:
|     udp_raw_demo
|   Tests:
|     test/test_socket_raw.sh
 ----------------------------------------------------------------------------*/

//-------------------------------------------------------------------------------
// XCP server transport

#undef OPTION_ENABLE_TCP
#undef OPTION_ENABLE_UDP
#define OPTION_ENABLE_UDP_RAW

// This configuration uses the built in Linux AF_PACKET backend (src/socket_raw_hal_linux.c).
// A project which supplies its own Ethernet HAL out of tree - for example an ASAM CMP backend -
// defines OPTION_UDP_RAW_HAL_EXTERNAL in ITS OWN configuration override header instead, and links
// its implementation against libxcplite. See docs/SOCKET_RAW.md.
// #define OPTION_UDP_RAW_HAL_EXTERNAL

// (1420 - 28) & ~7 = 1392 bytes max UDP payload, so the largest frame on the wire is
// 42 + 1392 = 1434 bytes and its IP packet is 1420 bytes. The raw transport does not fragment IPv4, so one segment must fit
// into one frame and an oversized frame can only be refused, never split.
//
// This is deliberately below the 1500 of a standard Ethernet link. The headroom is for a HAL
// backend which ENCAPSULATES the frame before putting it on the wire: an out of tree backend
// (OPTION_UDP_RAW_HAL_EXTERNAL) may add a header of its own, and at the full link MTU a
// segment of 1472 bytes already fills a 1500 byte path, leaving it nothing. Raise this to
// 1500 to use the full standard Ethernet MTU when the backend adds nothing.
#undef OPTION_MTU
#define OPTION_MTU 1420

//-------------------------------------------------------------------------------
// Transmit queue

// MANDATORY for OPTION_ENABLE_UDP_RAW, enforced by a #error in xcptl_cfg.h.
// The default on 64 bit hosts would be OPTION_QUEUE_64_VAR_SIZE, whose transmit path
// uses socketSendToV (scatter-gather), which the raw transport does not provide.
#undef OPTION_QUEUE_64_VAR_SIZE
#undef OPTION_QUEUE_64_FIX_SIZE
#define OPTION_QUEUE_32

// Zero copy transmit: reserve XCPTL_TX_HEADROOM bytes in front of every transmit queue
// segment so the Ethernet/IPv4/UDP header can be written in place (see xcptl_cfg.h).
#define OPTION_UDP_RAW_ZERO_COPY

//-------------------------------------------------------------------------------
// Raw Ethernet transport parameters
// Note: apart from OPTION_UDP_RAW_IFNAME these configure the shared UDP/IPv4/ARP/ICMP layer in
// socket_raw.c, not the Ethernet backend, so they apply whichever backend is selected.

// Default network interface, used when the application does not select one.
// The udp_raw_demo overrides this with its --if command line option.
#define OPTION_UDP_RAW_IFNAME "eth0"

// Answer ICMP Echo Requests (ping).
// Very useful during bring-up: a successful ping proves the Ethernet HAL, the MAC
// filter, the ARP reply, the IPv4 header build and the header checksum all work,
// before any XCP tooling is involved.
#define OPTION_UDP_RAW_ENABLE_ICMP_ECHO

// UDP checksum on transmit - exactly one of the following:
//   _ZERO     write 0x0000. Legal for IPv4 (RFC 768) and costs nothing. Default.
//             Note tcpdump/Wireshark then cannot validate the UDP framing - switch to
//             _COMPUTE temporarily during bring-up if that check is wanted.
//   _COMPUTE  RFC 768 software checksum over pseudo header and payload
//   _HW       leave 0, the EMAC inserts it (STM32 ETH, ESP32 EMAC support this)
#define OPTION_UDP_RAW_UDP_CHECKSUM_ZERO
// #define OPTION_UDP_RAW_UDP_CHECKSUM_COMPUTE
// #define OPTION_UDP_RAW_UDP_CHECKSUM_HW

// Verify IPv4/UDP checksums of received frames.
// On a switched link the Ethernet FCS already covers the wire, so this mostly catches
// our own parser bugs - which is exactly what is wanted during bring-up.
#define OPTION_UDP_RAW_VERIFY_RX_CHECKSUM

// Send a gratuitous ARP announcement on bind, to prime switch MAC tables and the
// ARP cache of the XCP client. Not required: ARP Requests for our IP are always answered.
// #define OPTION_UDP_RAW_GRATUITOUS_ARP
