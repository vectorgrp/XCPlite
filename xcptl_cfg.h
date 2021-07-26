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

 // DTO queue entry count 
#define XCPTL_DTO_QUEUE_SIZE 100   // DAQ transmit queue size in UDP packets, should at least be able to hold all data produced until the next call to udpTlHandleTransmitQueue


#endif



