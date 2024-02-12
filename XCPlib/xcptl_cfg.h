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

// Transport layer
#define XCP_TRANSPORT_LAYER_TYPE XCP_TRANSPORT_LAYER_ETH
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

// Transport layer header size
// This is fixed, no other options supported
#define XCPTL_TRANSPORT_LAYER_HEADER_SIZE 4

// TL segment size and DTO size
// Segment size is the maximum data buffer size given to send/sendTo, for UDP it is the UDP MTU
#define XCPTL_MAX_SEGMENT_SIZE (OPTION_MTU-20-8) // UDP MTU (MTU - IP-header - UDP-header)
#define XCPTL_MAX_DTO_SIZE (XCPTL_MAX_SEGMENT_SIZE-XCPTL_TRANSPORT_LAYER_HEADER_SIZE) // Normal ETH frame MTU - IPhdr - UDPhdr- XCPhdr, DTO size must be mod 4 
#define XCPTL_PACKET_ALIGNMENT 4 // Packet alignment for multiple XCP transport layer packets in a XCP transport layer message

// DAQ transmit queue 
// Transmit queue size in segments, should at least be able to hold all data produced until the next call to HandleTransmitQueue
#define XCPTL_QUEUE_SIZE 16  // array[XCPTL_QUEUE_SIZE] of tXcpMessageBuffer (XCPTL_MAX_SEGMENT_SIZE+4) 
// Maximum queue trigger event rate
#define XCPTL_QUEUE_TRANSMIT_CYCLE_TIME (1*CLOCK_TICKS_PER_MS)
// Flush cycle
#define XCPTL_QUEUE_FLUSH_CYCLE_MS 50 // Send a DTO packet at least every x ms, XCPTL_TIMEOUT_INFINITE to turn off

// CTO size
// Maximum size of a XCP command
#define XCPTL_MAX_CTO_SIZE 252 // must be mod 4

  



