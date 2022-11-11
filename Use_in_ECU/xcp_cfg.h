#pragma once

/*----------------------------------------------------------------------------
| File:
|   xcp_cfg.h
|
| Description:
|   User configuration file for XCP protocol layer parameters
|
| Code released into public domain, no attribution required
|
----------------------------------------------------------------------------*/


 /*----------------------------------------------------------------------------*/
 /* Platform specific type definitions for xcpLite.c */

// Enable debug print (printf) depending on ApplXcpGetDebugLevel()
#undef XCP_ENABLE_DEBUG_PRINTS

// Enable extended error checks, performance penalty !!!
#undef XCP_ENABLE_TEST_CHECKS

#if 0
XCP_CPUTYPE_BIGENDIAN
XCP_DAQ_CLOCK_64BIT
XCP_DAQ_MEM_SIZE
XCP_ENABLE_CAL_PAGE
XCP_ENABLE_CHECKSUM
XCP_ENABLE_DAQ_CLOCK_MULTICAST
XCP_ENABLE_DAQ_EVENT_INFO
XCP_ENABLE_DAQ_EVENT_LIST
XCP_ENABLE_DEBUG_PRINTS
XCP_ENABLE_DYN_ADDRESSING
XCP_ENABLE_IDT_A2L_HTTP_GET
XCP_ENABLE_IDT_A2L_UPLOAD
XCP_ENABLE_INTERLEAVED
XCP_ENABLE_MULTITHREAD_EVENTS
XCP_ENABLE_PACKED_MODE
XCP_ENABLE_PTP
XCP_ENABLE_TEST_CHECKS
XCP_MAX_DTO_ENTRY_SIZE
XCP_MAX_ODT_ENTRY_SIZE
XCP_PROTOCOL_LAYER_VERSION
XCPTL_MAX_CTO_SIZE
XCPTL_MAX_DTO_SIZE
XCPTL_QUEUED_CRM
#endif
 /*----------------------------------------------------------------------------*/
 /* Version */

// Driver version (GET_COMM_MODE_INFO)
#define XCP_DRIVER_VERSION 0x01

// Protocol layer version
#define XCP_PROTOCOL_LAYER_VERSION 0x0104  // PACKED_MODE, CC_START_STOP_SYNCH prepare

#define XCP_ENABLE_DAQ_EVENT_LIST // Enable event list
#define XCP_MAX_EVENT 16 // Maximum number of events, size of event table

#define XCP_DAQ_MEM_SIZE (5*200) // Amount of memory for DAQ tables, each ODT entry (e.g. measurement variable) needs 5 bytes

#define XCP_TIMESTAMP_TICKS 1  // ticks per unit
#define XCP_TIMESTAMP_UNIT DAQ_TIMESTAMP_UNIT_1US // unit DAQ_TIMESTAMP_UNIT_xxx
#define XCP_DAQ_CLOCK_UIID { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }
