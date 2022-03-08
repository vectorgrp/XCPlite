#pragma once

/*----------------------------------------------------------------------------
| File:
|   xcptl_cfg.h
|
| Description:
|   User configuration file for XCP transport layer parameters
|
| Code released into public domain, no attribution required
|
 ----------------------------------------------------------------------------*/

// Transport layer version
#define XCP_TRANSPORT_LAYER_VERSION 0x0104

// TCP or/and UDP option enabled
#define XCPTL_ENABLE_TCP
#define XCPTL_ENABLE_UDP

// Transmit mode
#define XCPTL_QUEUED_CRM // Use transmit queue for command responces
/*
Benefits:
- Unique transport layers message counters for CRM and DTO (CANape default transport layer option is "include command response")
- Transmit queue empty before DAQ is stopped (end of measurement consistent for all event channels)
- socketSendTo needs not to be thread safe for a socket
Drawbacks:
- Increased latency for GET_DAQ_CLOCK response during DAQ running, which impacts time sync quality if XCP 1.3 trigger initiator "sampled on reception" is not supported
- Impact on DAQ performance because transport layer packet is flushed for command responses
- DAQ queue overflow can happen on command responses, CANape aborts when response to GET_DAQ_CLOCK is missing
*/

// TL segment size and DTO size (must all be even!)
// Segment size is the maximum data buffer size given to send/sendTo, for UDP it is the MTU
#define XCPTL_JUMBO_FRAMES
#ifdef XCPTL_JUMBO_FRAMES
  #define XCPTL_SEGMENT_SIZE (1024*7) // UDP MTU = 7168 - Use jumbo frames for UDP
  #define XCPTL_MAX_DTO_SIZE (1024-XCPTL_TRANSPORT_LAYER_HEADER_SIZE-4) // DTO size must be mod 4
  #define XCPTL_PACKET_ALIGNMENT 4 // Packet alignment for multiple XCP transport layer packets in a XCP transport layer message
#else
  #define XCPTL_SEGMENT_SIZE (256*5) // UDP MTU = 1280
  #define XCPTL_MAX_DTO_SIZE (256-XCPTL_TRANSPORT_LAYER_HEADER_SIZE) // DTO size must be mod 4
  #define XCPTL_PACKET_ALIGNMENT 1 // Packet alignment for multiple XCP transport layer packets in a XCP transport layer message
#endif

// CTO size
// Maximum size of a XCP command
#define XCPTL_MAX_CTO_SIZE 252 // must be mod 4

// DAQ transmit queue size
// Transmit queue size in segments, should at least be able to hold all data produced until the next call to HandleTransmitQueue
#define XCPTL_QUEUE_SIZE (20)

// Transport layer header size
// This is fixed, no other options supported
#define XCPTL_TRANSPORT_LAYER_HEADER_SIZE 4

// Multicast (GET_DAQ_CLOCK_MULTICAST)
// Use multicast time synchronisation (not recommended)
// See readme.md
//#define XCPTL_ENABLE_MULTICAST
#ifdef XCPTL_ENABLE_MULTICAST
    #define XCPTL_MULTICAST_PORT 5557
#endif

