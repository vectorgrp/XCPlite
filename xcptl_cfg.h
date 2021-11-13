/*----------------------------------------------------------------------------
| File:
|   xcptl_cfg.h
|
| Description:
|   User configuration file for XCP transport layer parameters
 ----------------------------------------------------------------------------*/

#ifndef __XCPTL_CFG_H_
#define __XCPTL_CFG_H_

 // Transport layer version 
 #define XCP_TRANSPORT_LAYER_VERSION 0x0104 

 // UDP socket MTU
#define XCPTL_SOCKET_MTU_SIZE 1400
#define XCPTL_SOCKET_JUMBO_MTU_SIZE 7500

// DTO size (does not need jumbo frames)
#define XCPTL_DTO_SIZE (XCPTL_SOCKET_MTU_SIZE-XCPTL_TRANSPORT_LAYER_HEADER_SIZE)

// CTO size
#define XCPTL_CTO_SIZE 250

// DAQ transmit queue size 
#define XCPTL_DTO_QUEUE_SIZE (256)   // Transmit queue size in DAQ UDP packets, should at least be able to hold all data produced until the next call to HandleTransmitQueue

 // Transport layer header size
#define XCPTL_TRANSPORT_LAYER_HEADER_SIZE 4

// Multicast (GET_DAQ_CLOCK_MULTICAST)
// Use multicast time synchronisation to improve synchronisation of multiple XCP slaves
// This is standard in XCP V1.3, but it needs to create an additional thread and socket for multicast reception
// Adjust CANape setting in device/protocol/event/TIME_CORRELATION_GETDAQCLOCK from "multicast" to "extended response" if this is not desired 
#define XCPTL_ENABLE_MULTICAST 
#ifdef XCPTL_ENABLE_MULTICAST
    #define XCPTL_MULTICAST_PORT 5557
#endif


#endif



