#pragma once

/* xcpLite.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#include "xcp_cfg.h"    // Protocol layer configuration
#include "xcptl_cfg.h"  // Transport layer configuration
#include "xcp.h"        // XCP protocol defines
#include "xcpTl.h"      // Transport layer interface
#include "xcpAppl.h"    // Protocol layer external dependencies






/****************************************************************************/
/* DAQ event information                                                    */
/****************************************************************************/

#ifdef XCP_ENABLE_DAQ_EVENT_LIST

typedef struct {
    const char* name;
    uint32_t size; // ext event size
    uint8_t timeUnit; // timeCycle unit, 1us = 3, 10us = 4, 100us = 5, 1ms = 6, 10ms = 7, ...
    uint8_t timeCycle; // cycletime in units, 0 = sporadic or unknown
    uint16_t sampleCount; // packed event sample count
    uint16_t daqList; // associated DAQ list
    uint8_t priority; // priority 0 = queued, 1 = pushing, 2 = realtime
} tXcpEvent;

#endif



/****************************************************************************/
/* Protocol layer interface                                                 */
/****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif


/* Return values */
#define XCP_CMD_DENIED              0
#define XCP_CMD_OK                  1
#define XCP_CMD_PENDING             2
#define XCP_CMD_SYNTAX              3
#define XCP_CMD_BUSY                4
#define XCP_CMD_UNKNOWN             5
#define XCP_CMD_OUT_OF_RANGE        6
#define XCP_MODE_NOT_VALID          7
#define XCP_CMD_ERROR               0xFF


/* Initialization for the XCP Protocol Layer */
extern void XcpInit(void);
extern void XcpStart(void);
extern void XcpDisconnect();

/* Trigger a XCP data acquisition or stimulation event */
extern void XcpEvent(uint16_t event);
extern void XcpEventExt(uint16_t event, uint8_t* base);
extern void XcpEventAt(uint16_t event, uint64_t clock);
extern void XcpEventExtAt(uint16_t event, uint8_t* base, uint64_t clock);

/* XCP command processor */
extern void XcpCommand( const uint32_t* pCommand );

/* Send an XCP event message */
extern void XcpSendEvent(uint8_t evc, const uint8_t* d, uint8_t l);

/* Check status */
extern uint8_t XcpIsStarted();
extern uint8_t XcpIsConnected();
extern uint8_t XcpIsDaqRunning();
extern uint8_t XcpIsDaqPacked();
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
extern uint16_t XcpCreateEvent(const char* name, uint32_t cycleTime /* us */, uint8_t priority /* 0-normal, >=1 realtime*/, uint16_t sampleCount, uint32_t size);
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
extern BOOL ApplXcpStopDaq();

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

/* Info */
#ifdef XCP_ENABLE_IDT_A2L_NAME // Enable GET_ID: A2L filename without extension
extern const char* ApplXcpGetA2lName();
extern const char* ApplXcpGetA2lFileName();
#endif
#ifdef XCP_ENABLE_IDT_A2L_UPLOAD // Enable GET_ID: A2L content upload to host
extern uint8_t ApplXcpGetA2lUpload(uint8_t** p, uint32_t* n);
#endif
extern const char* ApplXcpGetName();

#ifdef __cplusplus
}
#endif
