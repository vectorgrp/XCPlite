/* xcpAppl.h */

/* Externals dependencies for xcpLite.c */


#ifndef __XCPAPPL_H_ 
#define __XCPAPPL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
	
/*----------------------------------------------------------------------------*/
// Debug prints

#ifdef XCP_ENABLE_TESTMODE

#define ApplXcpDebugLevel gDebugLevel
#define ApplXcpPrint printf

#endif
	

/*----------------------------------------------------------------------------*/
/* Info for GET_ID "slave name" */

#define ApplXcpSlaveIdLen XCPSIM_SLAVE_ID_LEN
#define ApplXcpSlaveId XCPSIM_SLAVE_ID
	   

/*----------------------------------------------------------------------------*/
// Address conversions XCP/A2L (32 bit unsigned int <-> pointer)

#ifdef _WIN64
	/* functions in xcpAppl.c */
#else
#define ApplXcpGetBaseAddr() ((vuint8*)0)
#define ApplXcpGetAddr(p) ((vuint32)((p)))
#define ApplXcpGetPointer(e,a) ((vuint8*)(a))
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

#define ApplXcpGetClock getClock32
#define ApplXcpGetClock64 getClock64
	
	   
/*----------------------------------------------------------------------------*/
// XCP Driver Transport Layer Callbacks as macros 

// Prepare start of DAQ not used
#define ApplXcpPrepareDaqStart()

// Set cluster id for multi cast reception not used yet
#define ApplXcpSetClusterId(id) 

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
