/*----------------------------------------------------------------------------
| File:
|   xcptl_cfg.h
|
| Description:
|   Konfiguration file for XCP transport layer parameters
 ----------------------------------------------------------------------------*/

#ifndef __XCPTL_CFG_H_
#define __XCPTL_CFG_H_

 // Transport layer version 
 #define XCP_TRANSPORT_LAYER_VERSION 0x0104 

 // UDP socket MTU
#define XCPTL_SOCKET_MTU_SIZE 1400
#define XCPTL_SOCKET_JUMBO_MTU_SIZE 7500

// UDP socket buffer size
#define XCPTL_SOCKET_BUFFER_SIZE (2*1024*1024)

// Transport layer header size
#define XCPTL_TRANSPORT_LAYER_HEADER_SIZE 4

// DTO size (does not need jumbo frames)
#define XCPTL_DTO_SIZE (XCPTL_SOCKET_MTU_SIZE-XCPTL_TRANSPORT_LAYER_HEADER_SIZE)

// CTO size
#define XCPTL_CTO_SIZE 250

 // DTO queue entry count 
#define XCPTL_DTO_QUEUE_SIZE (1*1024)   // Transmit queue size in DAQ UDP packets, should at least be able to hold all data produced by the largest event


#endif



