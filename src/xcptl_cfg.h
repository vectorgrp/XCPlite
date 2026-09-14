#pragma once
#define __XCPTL_CFG_H__

/*----------------------------------------------------------------------------
| File:
|   xcptl_cfg.h
|
| Description:
|   Parameter configuration for XCP transport layer
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "xcplib_cfg.h" // for OPTION_xxx

#if defined(OPTION_ENABLE_UDP) || defined(OPTION_ENABLE_UDP_RAW)
#define XCPTL_ENABLE_UDP
#endif
#if defined(OPTION_ENABLE_TCP)
#define XCPTL_ENABLE_TCP
#endif

// Raw Ethernet transport (OPTION_ENABLE_UDP_RAW) restrictions - see docs/SOCKET_RAW.md
#if defined(OPTION_ENABLE_UDP_RAW) && (defined(OPTION_ENABLE_UDP) || defined(OPTION_ENABLE_TCP))
#error "OPTION_ENABLE_UDP_RAW is mutually exclusive with OPTION_ENABLE_UDP / OPTION_ENABLE_TCP"
#endif
#if defined(OPTION_ENABLE_UDP_RAW) && !defined(OPTION_QUEUE_32)
#error "OPTION_ENABLE_UDP_RAW requires OPTION_QUEUE_32: the 64 bit queues use the vectored send path (socketSendToV), which the raw transport does not implement"
#endif
#if defined(OPTION_ENABLE_UDP_RAW) && defined(OPTION_SHM_MODE)
#error "OPTION_ENABLE_UDP_RAW is not supported in SHM mode (queueInitFromMemory is implemented for the 64 bit queues only)"
#endif

// Transport layer version
#define XCP_TRANSPORT_LAYER_VERSION 0x0104

// CTO size
// Maximum size of a XCP command packet (CRO,CRM)
// Must be %8, must be smaller or equal than XCPTL_MAX_DTO_SIZE
#define XCPTL_MAX_CTO_SIZE (248)

// DTO size
// Maximum size of a XCP data packet (DAQ,STIM)
#ifdef OPTION_QUEUE_64_FIX_SIZE
// Must be %8, must result in a queue entry size (including header) that is a multiple of the cache line size for optimal performance
#define XCPTL_MAX_DTO_SIZE (248) // CACHE_LINE_LIZE - QUEUE_HEADER_SIZE - XCPTL_TRANSPORT_LAYER_HEADER_SIZE = 248 for optimal fixed size tl queue entry size
#else
#define XCPTL_MAX_DTO_SIZE (1024) // Must be %8, must be smaller or equal than XCPTL_MAX_SEGMENT_SIZE
#endif

// Segment size is the maximum data buffer size given to socket send/sendTo; for UDP, it is the maximum UDP payload size.
// Subtract the minimum IPv4 and UDP headers from the IP MTU, then align down as required by the transport layer.
// Jumbo frames are supported, but it might be more efficient to use a smaller segment size.
#ifdef OPTION_MTU
#define XCPTL_MAX_SEGMENT_SIZE ((OPTION_MTU - 20 - 8) & ~0x07U)
#else
#error "Please define XCPTL_MAX_SEGMENT_SIZE"
#define XCPTL_MAX_SEGMENT_SIZE (1500 - 20 - 8)
#endif

// Note on OPTION_MTU:
// OPTION_MTU is the link MTU, the Ethernet header is NOT part of it.
// XCPTL_MAX_SEGMENT_SIZE = (OPTION_MTU - 28) & ~7 reserves 28 bytes for the IPv4 and UDP headers
// and then aligns down as the transport layer requires, so the resulting IP packet is at most
// OPTION_MTU bytes, and exactly OPTION_MTU when OPTION_MTU - 28 is already a multiple of 8:
// 1500 -> segment 1472 -> IP packet 1500.
// The invariant is therefore: OPTION_MTU <= link MTU.
// Before V2.1.11 OPTION_MTU was the link MTU rounded UP to a multiple of 8 (1504 for a 1500 byte
// link) and the invariant was OPTION_MTU <= link MTU + 4.
// An OPTION_MTU too large for the link is NOT caught at compile time - the link MTU is a runtime
// property that only the target knows. It is reported at runtime instead:
//   - socket transport: socketOpen sets DF on Linux, macOS/BSD, QNX and Windows, so sendto fails
//                       with EMSGSIZE and socketSendTo names the segment size and the OPTION_MTU
//   - raw transport:    eth_hal_send reports ETH_HAL_ERROR_SIZE
// Neither of those two fragments IPv4.
//
// lwIP is the exception: its socketOpen (the separate FreeRTOS implementation in sockets.c) sets
// no DF option, because lwIP has no IP_DONTFRAG, so an oversized datagram is not refused - lwIP
// fragments or drops it according to its own IP_FRAG build setting. socketSendTo therefore compares
// the segment against netif_default->mtu itself and warns once, but it still hands the datagram to
// lwIP: the check is a diagnostic, not a guard. On lwIP, OPTION_MTU has to be right.

// Receive timeout in milliseconds (rate of periodic checks for shutdown and background tasks in the receive thread)
#define XCPTL_RECV_TIMEOUT_MS 100

// Size granularity of the protocol layer packet inside a transport layer message
// A message is: WORD len + WORD ctr + protocol layer packet + fill.
// The packet size is rounded up to this alignment (that is the "fill"), so that the messages
// concatenated into a segment all start aligned - the message header (len+ctr) is word accessed.
// Also used as QUEUE_PAYLOAD_SIZE_ALIGNMENT by all queue variants, see queue.h.
// Only 4 is supported and queue.h enforces that with an #error - do not change this value.
#define XCPTL_PACKET_ALIGNMENT 4

// Transmit headroom: space reserved in front of a complete transmit segment, so the transport can
// prepend its link headers in place instead of copying the payload into a separate frame buffer.
// Used by the raw Ethernet transport for the 42 byte Ethernet + IPv4 + UDP header.
// 48 instead of 42 keeps the segment payload 8 byte aligned; the header is written right justified
// at (segment - 42), which also lands the IPv4 header on a 4 byte boundary.
#if defined(OPTION_ENABLE_UDP_RAW) && defined(OPTION_UDP_RAW_ZERO_COPY)
#define XCPTL_TX_HEADROOM 48
#else
#define XCPTL_TX_HEADROOM 0
#endif

// Only the segment accumulating queues (queue32.c, queue32m.c) reserve segment headroom.
// This cannot be violated today because OPTION_ENABLE_UDP_RAW already requires OPTION_QUEUE_32,
// but check it explicitly so a future transport cannot silently lose the reservation.
#if (XCPTL_TX_HEADROOM > 0) && !defined(OPTION_QUEUE_32)
#error "XCPTL_TX_HEADROOM requires OPTION_QUEUE_32: only the segment accumulating queues reserve segment headroom"
#endif

// Transport layer message header size
// This is fixed, no other options supported yet
#define XCPTL_TRANSPORT_LAYER_HEADER_SIZE 4

// Use the transmit queue for CRM messages instead of sending them directly from the command processing thread
// This may improve overall performance, but adds some latency for CRM messages (problem with GET_DAQ_CLOCK, no problem in PTP mode)
// Default is the low latency concept
// #define XCPTL_CRM_VIA_TRANSMIT_QUEUE

// Use separate transport layer counter for DAQ and CRM messages
// This avoids the need to maintain the transport layer counter consistency between DAQ and CRM messages in the XCP command processing and transmit queue handling threads
// But it is not supported by all XCP tools
// In CANape there is an option COUNTER_HANDLING with the mode 'exclude command response' and 'include command response'
// 'include command response' is default !
// #define XCPTL_EXCLUDE_CRM_FROM_CTR

// Multicast (GET_DAQ_CLOCK_MULTICAST)
// Not recommended setting
// #define XCPTL_ENABLE_MULTICAST
/*
Use multicast time synchronisation to improve synchronisation of multiple XCP slaves
This option is available since XCP V1.3, but using it, needs to create an additional thread and socket for multicast reception
There is no benefit if PTP time synchronized is used or if there is only one XCP device
Older CANape versions expect this option is on by default -> turn it off in device/protocol/event/TIME_CORRELATION_GETDAQCLOCK by changing from "multicast" to "extendedresponse"
*/
#if defined(XCPTL_ENABLE_UDP) || defined(XCPTL_ENABLE_TCP)
#ifdef XCPTL_ENABLE_MULTICAST
// #define XCLTL_RESTRICT_MULTICAST
#define XCPTL_MULTICAST_PORT 5557
#endif
#endif
