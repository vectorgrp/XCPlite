#pragma once

#include "main.h"
#include "xcp_cfg.h"    // Protocol layer configuration
#include "xcptl_cfg.h"  // Transport layer configuration
#include "xcp.h"        // XCP protocol defines
#include "xcpTl.h"      // Transport layer interface

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


/* Trigger a XCP data acquisition or stimulation event */
extern void XcpEvent(uint16_t event);

/* Send an XCP event message */
extern void XcpSendEvent(uint8_t evc, const uint8_t* d, uint8_t l);

// Event list
#ifdef XCP_ENABLE_DAQ_EVENT_LIST

// Add a measurement event to event list, return event number (0..MAX_EVENT-1)
extern uint16_t XcpCreateEvent(const char* name, uint32_t cycleTimeNs /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/, uint16_t sampleCount, uint32_t size);
// Get event list
extern tXcpEvent* XcpGetEventList(uint16_t* eventCount);

#endif


/* Initialization for the XCP Protocol Layer */
extern void XcpInit(void);
extern void XcpStart(void);
extern void XcpDisconnect();

/* XCP command processor */
extern void XcpCommand( const uint32_t* pCommand, uint16_t len );

/* Check status */
extern BOOL XcpIsConnected();

// Event list
#ifdef XCP_ENABLE_DAQ_EVENT_LIST

// Lookup event
extern tXcpEvent* XcpGetEvent(uint16_t event);

#endif


/* Callbacks */
extern BOOL ApplXcpConnect();
extern BOOL ApplXcpPrepareDaq();
extern BOOL ApplXcpStartDaq();
extern void ApplXcpStopDaq();

/* Generate a native pointer from XCP address extension and address */
extern uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr);
extern uint32_t ApplXcpGetAddr(uint8_t* p);
extern uint8_t *ApplXcpGetBaseAddr();

/* DAQ clock */
extern uint64_t ApplXcpGetClock64();
extern uint8_t ApplXcpGetClockState();



#if 0
#pragma once
/* xcpLite.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#include "xcp_cfg.h"    // Protocol layer configuration
#include "xcptl_cfg.h"  // Transport layer configuration
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
extern BOOL XcpIsDaqPacked();
extern uint64_t XcpGetDaqStartTime();
extern uint32_t XcpGetDaqOverflowCount();

/* Time synchronisation */
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
extern uint16_t XcpGetClusterId();
#endif
#ifdef XCP_ENABLE_PTP
extern void XcpSetGrandmasterClockInfo(uint8_t* id, uint8_t epoch, uint8_t stratumLevel);
#endif

// Event list
#ifdef XCP_ENABLE_DAQ_EVENT_LIST

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

// All functions must be thread save

/* Callbacks */
extern BOOL ApplXcpConnect();
extern BOOL ApplXcpPrepareDaq();
extern BOOL ApplXcpStartDaq();
extern void ApplXcpStopDaq();

/* Generate a native pointer from XCP address extension and address */
extern uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr);
extern uint32_t ApplXcpGetAddr(uint8_t* p);
extern uint8_t *ApplXcpGetBaseAddr();

/* Switch calibration page */
#ifdef XCP_ENABLE_CAL_PAGE
extern uint8_t ApplXcpGetCalPage(uint8_t segment, uint8_t mode);
extern uint8_t ApplXcpSetCalPage(uint8_t segment, uint8_t page, uint8_t mode);
#endif

/* DAQ clock */
extern uint64_t ApplXcpGetClock64();
extern uint8_t ApplXcpGetClockState();
#ifdef XCP_ENABLE_PTP
extern BOOL ApplXcpGetClockInfoGrandmaster(uint8_t* uuid, uint8_t* epoch, uint8_t* stratum);
#endif

/* Info (for GET_ID) */
extern uint32_t ApplXcpGetId(uint8_t id, uint8_t* buf, uint32_t bufLen);
#ifdef XCP_ENABLE_IDT_A2L_UPLOAD // Enable GET_ID: A2L content upload to host
extern BOOL ApplXcpReadA2L(uint8_t size, uint32_t addr, uint8_t* data);
#endif

#endif
