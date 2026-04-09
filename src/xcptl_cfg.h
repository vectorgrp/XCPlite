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

#if defined(OPTION_ENABLE_UDP)
#define XCPTL_ENABLE_UDP
#endif
#if defined(OPTION_ENABLE_TCP)
#define XCPTL_ENABLE_TCP
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
#define XCPTL_MAX_DTO_SIZE (512) // Must be %8, must be smaller or equal than XCPTL_MAX_SEGMENT_SIZE
#endif

// Segment size is the maximum data buffer size given to sockets send/sendTo, for UDP it is the UDP MTU
// Jumbo frames are supported, but it might be more efficient to use a smaller segment sizes
#ifdef OPTION_MTU
#define XCPTL_MAX_SEGMENT_SIZE (OPTION_MTU - 32) // UDP MTU (- IP-header)
#else
#error "Please define XCPTL_MAX_SEGMENT_SIZE"
#define XCPTL_MAX_SEGMENT_SIZE (1500 - 20 - 8)
#endif

// Receive timeout in milliseconds (rate of periodic checks for shutdown and background tasks in the receive thread)
#define XCPTL_RECV_TIMEOUT_MS 100

// Alignment for packet concatenation
#define XCPTL_PACKET_ALIGNMENT 4 // Packet alignment for multiple XCP transport layer packets in a XCP transport layer message

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
