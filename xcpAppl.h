/* xcpAppl.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */


#ifndef __XCPAPPL_H_ 
#define __XCPAPPL_H_

#include "main.h"


#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*/
// Debug prints

#ifdef XCP_ENABLE_TESTMODE

#define ApplXcpDebugLevel gDebugLevel
#define ApplXcpPrint printf

#endif
	

/*----------------------------------------------------------------------------*/
/* Info for GET_ID "slave name" */

#define ApplXcpSlaveIdLen APP_SLAVE_ID_LEN
#define ApplXcpSlaveId APP_SLAVE_ID
	   

/*----------------------------------------------------------------------------*/
// Address conversions XCP/A2L (32 bit unsigned int <-> pointer)

#ifdef _WIN
	/* functions in xcpAppl.c */
#else
#ifdef _LINUX32
#define ApplXcpGetBaseAddr()   ((vuint8*)0)
#define ApplXcpGetAddr(p)      ((vuint32)(p))
#define ApplXcpGetPointer(e,a) ((vuint8*)(a))
#else
	/* functions in xcpAppl.c */
#endif
#endif


/*----------------------------------------------------------------------------*/
// Event list for GET_EVENT_INFO command and XcpEvent optimization

/* Info for GET_EVENT_INFO*/
#ifdef XCP_ENABLE_DAQ_EVENT_LIST

/* Event list */
extern vuint16 ApplXcpEventCount;
extern tXcpEvent ApplXcpEventList[XCP_MAX_EVENT];

// Add a measurement event to event list, return event number (0..MAX_EVENT-1)
extern vuint16 XcpCreateEvent(const char* name, vuint16 timeCycle /*ms */, vuint16 sampleCount, vuint32 size);

#endif



/*----------------------------------------------------------------------------*/
// DAQ clock provided to xcpLite.c as macros

// Get slave clock
// XCP slave clock timestamp resolution defined in xcp_cfg.h
// Clock must be monotonic !!!

#define ApplXcpGetClock()     clockGet32()
#define ApplXcpGetClock64()   clockGet64()
#define ApplXcpPrepareDaq()   1 /* not used */  

	   
/*----------------------------------------------------------------------------*/
// XCP Driver Transport Layer Callbacks as macros 

// Prepare start of DAQ not used
#define ApplXcpPrepareDaqStart() /* not used */

// Set cluster id for multi cast reception not used yet
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
#define ApplXcpSetClusterId(id) 
#endif

// Get and commit buffer space for a DAQ DTO message
#define ApplXcpGetDtoBuffer udpTlGetPacketBuffer
#define ApplXcpCommitDtoBuffer udpTlCommitPacketBuffer

// Start stop DAQ
#define ApplXcpDaqStart udpTlInitTransmitQueue
#define ApplXcpDaqStop udpTlInitTransmitQueue

// Send a CRM message
#define ApplXcpSendCrm udpTlSendCrmPacket



#ifdef __cplusplus
}
#endif



#endif
