#pragma once
/* xcpLite.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#include "xcptl_cfg.h"  // Transport layer configuration
#include "xcp_cfg.h"    // Protocol layer configuration
#include "xcp.h"        // XCP protocol defines
#include "xcpTl.h"      // Transport layer interface


/****************************************************************************/
/* DAQ event information                                                    */
/****************************************************************************/

#ifdef XCP_ENABLE_DAQ_EVENT_LIST

typedef struct {
    const char* name;
    uint32_t size; // ext event size
    uint8_t timeUnit; // timeCycle unit, 1ns=0, 10ns=1, 100ns=2, 1us=3, ..., 1ms=6, ...
    uint8_t timeCycle; // cycletime in units, 0 = sporadic or unknown
    uint16_t sampleCount; // packed event sample count
    uint16_t daqList; // associated DAQ list
    uint8_t priority; // priority 0 = queued, 1 = pushing, 2 = realtime
#ifdef XCP_ENABLE_MULTITHREAD_EVENTS
    MUTEX mutex;
#endif
#ifdef XCP_ENABLE_TEST_CHECKS
    uint64_t time; // last event time stamp
#endif
} tXcpEvent;

#endif


/****************************************************************************/
/* Protocol layer interface                                                 */
/****************************************************************************/

/* Initialization for the XCP Protocol Layer */
extern void XcpInit(void);
extern void XcpStart(void);
extern void XcpDisconnect();

/* Trigger a XCP data acquisition or stimulation event */
extern void XcpEvent(uint16_t event);
extern void XcpEventExt(uint16_t event, uint8_t* base);
extern void XcpEventAt(uint16_t event, uint64_t clock);

/* XCP command processor */
extern void XcpCommand( const uint32_t* pCommand, uint16_t len );

/* Send an XCP event message */
extern void XcpSendEvent(uint8_t evc, const uint8_t* d, uint8_t l);

/* Check status */
extern BOOL XcpIsStarted();
extern BOOL XcpIsConnected();
extern BOOL XcpIsDaqRunning();
extern BOOL XcpIsDaqEventRunning(uint16_t event);
extern uint64_t XcpGetDaqStartTime();
extern uint32_t XcpGetDaqOverflowCount();

/* Time synchronisation */
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
extern uint16_t XcpGetClusterId();
#endif

// Event list
#ifdef XCP_ENABLE_DAQ_EVENT_LIST

#define XCP_INVALID_EVENT 0xFFFF

// Clear event list
extern void XcpClearEventList();
// Add a measurement event to event list, return event number (0..MAX_EVENT-1)
extern uint16_t XcpCreateEvent(const char* name, uint32_t cycleTimeNs /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/, uint16_t sampleCount, uint32_t size);
// Get event list
extern tXcpEvent* XcpGetEventList(uint16_t* eventCount);
// Lookup event
extern tXcpEvent* XcpGetEvent(uint16_t event);

#endif


/****************************************************************************/
/* Protocol layer external dependencies                                     */
/****************************************************************************/

// All callback functions supplied by the application
// Must be thread save

/* Callbacks on connect, measurement prepare, start and stop */
extern BOOL ApplXcpConnect();
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
extern BOOL ApplXcpPrepareDaq();
#endif
extern BOOL ApplXcpStartDaq();
extern void ApplXcpStopDaq();

/* Address conversions from A2L address to pointer and vice versa */
/* Note that xcpAddrExt 0x01 and 0xFF are reserved for special use cases (0x01 if XCP_ENABLE_DYN_ADDRESSING, 0xFF if XCP_ENABLE_IDT_A2L_UPLOAD) */
extern uint8_t* ApplXcpGetPointer(uint8_t xcpAddrExt, uint32_t xcpAddr); /* Create a pointer (uint8_t*) from xcpAddrExt and xcpAddr, returns NULL if no access */
extern uint32_t ApplXcpGetAddr(uint8_t* p); // Calculate the xcpAddr address from a pointer
extern uint8_t *ApplXcpGetBaseAddr(); // Get the base address for DAQ data access */
/*
 Note 1:
   For DAQ performance and memory optimization:
   XCPlite DAQ tables do not store address extensions and do not use ApplXcpGetPointer(), addr is stored as 32 Bit value and access is hardcoded by *(baseAddr+xcpAddr)
   All accesible DAQ data is within a 4GByte range starting at ApplXcpGetBaseAddr()
   Address extensions to increase the addressable range are not supported yet
   Attempting to setup an ODT entry with address extension != 0 gives a CRC_ACCESS_DENIED error message

 Note 2:
   ApplXcpGetPointer may do address transformations according to active calibration page
   When measuring calibration variables inswitch
*/


/* Switch calibration pages */
#ifdef XCP_ENABLE_CAL_PAGE
extern uint8_t ApplXcpGetCalPage(uint8_t segment, uint8_t mode);
extern uint8_t ApplXcpSetCalPage(uint8_t segment, uint8_t page, uint8_t mode);
#endif

/* DAQ clock */
extern uint64_t ApplXcpGetClock64();

#define CLOCK_STATE_SYNCH_IN_PROGRESS                  (0)
#define CLOCK_STATE_SYNCH                              (1)
#define CLOCK_STATE_FREE_RUNNING                       (7)
#define CLOCK_STATE_GRANDMASTER_STATE_SYNC             (1 << 3)
extern uint8_t ApplXcpGetClockState();

#ifdef XCP_ENABLE_PTP
#define CLOCK_STRATUM_LEVEL_UNKNOWN   255
#define CLOCK_STRATUM_LEVEL_ARB       16   // unsychronized
#define CLOCK_STRATUM_LEVEL_UTC       0    // Atomic reference clock
#define CLOCK_EPOCH_TAI 0 // Atomic monotonic time since 1.1.1970 (TAI)
#define CLOCK_EPOCH_UTC 1 // Universal Coordinated Time (with leap seconds) since 1.1.1970 (UTC)
#define CLOCK_EPOCH_ARB 2 // Arbitrary (epoch unknown)
extern BOOL ApplXcpGetClockInfoGrandmaster(uint8_t* uuid, uint8_t* epoch, uint8_t* stratum);
#endif

/* Get info for GET_ID command (pointer to and length of data) */
/* Supports IDT_ASCII, IDT_ASAM_NAME, IDT_ASAM_PATH, IDT_ASAM_URL, IDT_ASAM_EPK and IDT_ASAM_UPLOAD */
/* Returns 0 if not available */
extern uint32_t ApplXcpGetId(uint8_t id, uint8_t* buf, uint32_t bufLen);

/* Read a chunk (offset,size) of the A2L file for upload */
/* Return FALSE if out of bounds */
#ifdef XCP_ENABLE_IDT_A2L_UPLOAD // Enable A2L content upload to host (IDT_ASAM_UPLOAD)
extern BOOL ApplXcpReadA2L(uint8_t size, uint32_t offset, uint8_t* data);
#endif

