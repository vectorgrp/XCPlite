/* xcpLite.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __XCPLITE_H_ 
#define __XCPLITE_H_

#include "xcp_cfg.h"    // Protocol layer configuration
#include "xcp.h"        // XCP protocol defines
#include "xcpTl.h"      // Transport layer interface
#include "xcpAppl.h"    // Protocol layer external dependencies


/****************************************************************************/
/* XCP Packet Type Definition                                               */
/****************************************************************************/

typedef union {
    /* There might be a loss of up to 3 bytes. */
    uint8_t  b[((XCPTL_CTO_SIZE + 3) & 0xFFC)];
    uint16_t w[((XCPTL_CTO_SIZE + 3) & 0xFFC) / 2];
    uint32_t dw[((XCPTL_CTO_SIZE + 3) & 0xFFC) / 4];
} tXcpCto;



/****************************************************************************/
/* XCP Commands                                                             */
/****************************************************************************/

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



/* Bitmasks for gXcp.SendStatus */
#define XCP_CRM_REQUEST             0x01u
#define XCP_DTO_REQUEST             0x02u
#define XCP_EVT_REQUEST             0x04u
#define XCP_CRM_PENDING             0x10u
#define XCP_DTO_PENDING             0x20u
#define XCP_EVT_PENDING             0x40u
#define XCP_SEND_PENDING            (XCP_DTO_PENDING|XCP_CRM_PENDING|XCP_EVT_PENDING)



/****************************************************************************/
/* DAQ event information                                                    */
/****************************************************************************/

#ifdef XCP_ENABLE_DAQ_EVENT_LIST

typedef struct {
    const char* name;
    uint8_t timeUnit;
    uint8_t timeCycle;
    uint16_t sampleCount; // packed event sample count
    uint32_t size; // ext event size
} tXcpEvent;

#endif

/****************************************************************************/
/* XCP clock information                                                    */
/****************************************************************************/

#define XCP_STRATUM_LEVEL_UNKNOWN   255
#define XCP_STRATUM_LEVEL_ARB       16   // unsychronized
#define XCP_STRATUM_LEVEL_UTC       0    // Atomic reference clock

#define XCP_EPOCH_TAI 0 // Atomic monotonic time since 1.1.1970 (TAI)
#define XCP_EPOCH_UTC 1 // Universal Coordinated Time (with leap seconds) since 1.1.1970 (UTC)
#define XCP_EPOCH_ARB 2 // Arbitrary (unknown)

typedef struct {
    uint8_t      UUID[8];
    uint16_t     timestampTicks;
    uint8_t      timestampUnit;
    uint8_t      stratumLevel;
    uint8_t      nativeTimestampSize;
    uint8_t      fill[3]; // for alignment (8 byte) of structure
    uint64_t     valueBeforeWrapAround;
} T_CLOCK_INFO_SLAVE;


#ifdef XCP_ENABLE_GRANDMASTER_CLOCK_INFO

typedef struct {
    uint8_t     UUID[8];
    uint16_t    timestampTicks;
    uint8_t     timestampUnit;
    uint8_t     stratumLevel;
    uint8_t     nativeTimestampSize;
    uint8_t     epochOfGrandmaster;
    uint8_t     fill[2]; // for alignment (8 byte) of structure
    uint64_t    valueBeforeWrapAround;
} T_CLOCK_INFO_GRANDMASTER;

typedef struct {
    uint64_t  timestampOrigin;
    uint64_t  timestampLocal;
} T_CLOCK_INFO_RELATION;

#endif


/****************************************************************************/
/* Protocol layer interface                                                 */
/****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif


/* Initialization for the XCP Protocol Layer */
extern void XcpInit(void);
extern void XcpStart(void);
extern void XcpDisconnect();

/* Trigger a XCP data acquisition or stimulation event */
extern void XcpEvent(uint16_t event); 
extern void XcpEventExt(uint16_t event, uint8_t* base);
extern void XcpEventAt(uint16_t event, uint64_t clock );

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
extern uint16_t XcpGetClusterId();
extern void XcpSetGrandmasterClockInfo(uint8_t* id, uint8_t epoch, uint8_t stratumLevel);

// Add a measurement event to event list, return event number (0..MAX_EVENT-1)
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
extern uint16_t XcpCreateEvent(const char* name, uint16_t timeCycle /*ms */, uint16_t sampleCount, uint32_t size);
extern tXcpEvent* XcpGetEventList(uint16_t* eventCount);
#endif

// Test instrumentation
#ifdef APP_ENABLE_A2L_GEN
void XcpCreateA2lDescription();
#endif


/****************************************************************************/
/* Protocol layer external dependencies                                     */
/****************************************************************************/

// All functions must be thread save


/* Generate a native pointer from XCP address extension and address */
extern uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr);
extern uint32_t ApplXcpGetAddr(uint8_t* p);
extern uint8_t *ApplXcpGetBaseAddr();

#ifdef XCP_ENABLE_CAL_PAGE
extern uint8_t ApplXcpGetCalPage(uint8_t segment, uint8_t mode);
extern uint8_t ApplXcpSetCalPage(uint8_t segment, uint8_t page, uint8_t mode);
#endif

#ifdef XCP_ENABLE_GRANDMASTER_CLOCK_INFO
extern uint8_t ApplXcpGetClockInfo(T_CLOCK_INFO_SLAVE* s,T_CLOCK_INFO_GRANDMASTER* g);
#endif

// DAQ clock provided by application
extern uint32_t ApplXcpGetClock();
extern uint64_t ApplXcpGetClock64();
extern int ApplXcpPrepareDaq();


/* Info for GET_ID IDT_ASCII - ECU identification */
extern uint8_t ApplXcpGetSlaveId(char** p, uint32_t* n);

/* Info for GET_ID IDT_ASAM_NAME - a2l file name without extension */
#if defined ( XCP_ENABLE_A2L_NAME )
extern uint8_t ApplXcpGetA2LFilename(char** p, uint32_t* n, int path);
#endif

/* Info for GET_ID IDT_ASAM_NAME - A2L upload */
#if defined ( XCP_ENABLE_FILE_UPLOAD )
extern uint8_t ApplXcpReadFile(uint8_t type, uint8_t** p, uint32_t* n);
#endif


#ifdef __cplusplus
}
#endif



#endif

