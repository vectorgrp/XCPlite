

/*****************************************************************************
| File:
|   xcpLite.c
|
|  Description:
|    Implementation of the ASAM XCP Protocol Layer V1.4
|    Lite Version (see feature list and limitations)
|
|
|  Supported commands:
|   GET_COMM_MODE_INFO GET_ID GET_VERSION
|   SET_MTA UPLOAD SHORT_UPLOAD DOWNLOAD SHORT_DOWNLOAD
|   GET_CAL_PAGE SET_CAL_PAGE BUILD_CHECKSUM
|   GET_DAQ_RESOLUTION_INFO GET_DAQ_PROCESSOR_INFO GET_DAQ_EVENT_INFO
|   FREE_DAQ ALLOC_DAQ ALLOC_ODT ALLOC_ODT_ENTRY SET_DAQ_PTR WRITE_DAQ WRITE_DAQ_MULTIPLE
|   GET_DAQ_LIST_MODE SET_DAQ_LIST_MODE START_STOP_SYNCH START_STOP_DAQ_LIST
|   GET_DAQ_CLOCK GET_DAQ_CLOCK_MULTICAST TIME_CORRELATION_PROPERTIES
|
|  Limitations:
|     - Testet on 32 bit or 64 bit Linux and Windows platforms
|     - 8 bit and 16 bit CPUs are not supported
|     - No Motorola byte sex
|     - No misra compliance
|     - Overall number of ODTs limited to 64K
|     - Overall number of ODT entries is limited to 64K
|     - Fixed DAQ+ODT 2 byte DTO header
|     - Fixed 32 bit time stamp
|     - Only dynamic DAQ list allocation supported
|     - Resume is not supported
|     - Overload indication by event is not supported
|     - DAQ does not support prescaler
|     - ODT optimization not supported
|     - Seed & key is not supported
|     - Flash programming is not supported

|
|  More features, more transport layer (CAN, FlexRay) and platform support, misra compliance
|  by the free XCP basic version available from Vector Informatik GmbH at www.vector.com
|
|  Limitations of the XCP basic version:
|     - Stimulation (Bypassing) is not available|
|     - Bit stimulation is not available
|     - SHORT_DOWNLOAD is not implemented
|     - MODIFY_BITS is not available|
|     - FLASH and EEPROM Programming is not available|
|     - Block mode for UPLOAD, DOWNLOAD and PROGRAM is not available
|     - Resume mode is not available|
|     - Memory write and read protection is not supported
|     - Checksum calculation with AUTOSAR CRC module is not supported
|
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
|  No limitations and full compliance are available with the commercial version
|  from Vector Informatik GmbH, please contact Vector
|***************************************************************************/

#include "main.h"
#include "platform.h"
#include "dbg_print.h"
#include "xcpLite.h"    // Protocol layer interface


/****************************************************************************/
/* Defaults and checks                                                      */
/****************************************************************************/

/* Check limits of the XCP imnplementation */
#if defined( XCPTL_MAX_CTO_SIZE )
#if ( XCPTL_MAX_CTO_SIZE > 255 )
#error "XCPTL_MAX_CTO_SIZE must be <= 255"
#endif
#if ( XCPTL_MAX_CTO_SIZE < 8 )
#error "XCPTL_MAX_CTO_SIZE must be >= 8"
#endif
#else
#error "Please define XCPTL_CTO_SIZE"
#endif

#if defined( XCPTL_MAX_DTO_SIZE )
#if ( XCPTL_MAX_DTO_SIZE > (XCPTL_MAX_SEGMENT_SIZE-4) )
#error "XCPTL_MAX_DTO_SIZE too large"
#endif
#if ( XCPTL_MAX_DTO_SIZE < 8 )
#error "XCPTL_MAX_DTO_SIZE must be >= 8"
#endif
#else
#error "Please define XCPTL_DTO_SIZE"
#endif

/* Max. size of an object referenced by an ODT entry XCP_MAX_ODT_ENTRY_SIZE may be limited  */
#if defined ( XCP_MAX_ODT_ENTRY_SIZE )
#if ( XCP_MAX_DTO_ENTRY_SIZE > 255 )
#error "XCP_MAX_ODT_ENTRY_SIZE too large"
#endif
#else
#define XCP_MAX_ODT_ENTRY_SIZE 248 // mod 4 = 0 to optimize DAQ copy granularity
#endif

/* Check XCP_DAQ_MEM_SIZE */
#if defined ( XCP_DAQ_MEM_SIZE )
#if ( XCP_DAQ_MEM_SIZE > 0xFFFFFFFF )
#error "XCP_DAQ_MEM_SIZE must be <= 0xFFFFFFFF"
#endif
#else
#error "Please define XCP_DAQ_MEM_SIZE"
#endif

// Dynamic addressing (ext=1, addr=(event<<16)|offset requires transport layer mode XCPTL_QUEUED_CRM
#if defined(XCP_ENABLE_DYN_ADDRESSING) && !defined(XCPTL_QUEUED_CRM)
#error "Dynamic address format (ext=1) requires XCPTL_QUEUED_CRM!"
#endif


/****************************************************************************/
/* DAQ Type Definition                                                      */
/****************************************************************************/


/* ODT */
/* Size must be even !!! */
typedef struct {
    uint16_t firstOdtEntry;       /* Absolute odt entry number */
    uint16_t lastOdtEntry;        /* Absolute odt entry number */
    uint16_t size;                /* Number of bytes */
} tXcpOdt;


/* DAQ list */
typedef struct {
    uint16_t lastOdt;             /* Absolute odt number */
    uint16_t firstOdt;            /* Absolute odt number */
    uint16_t eventChannel;
#ifdef XCP_ENABLE_PACKED_MODE
    uint16_t sampleCount;         /* Packed mode */
#endif
    uint8_t mode;
    uint8_t state;
    uint8_t priority;
} tXcpDaqList;


/* Dynamic DAQ list structures */
typedef struct {
    uint16_t         DaqCount;
    uint16_t         OdtCount;       /* Absolute */
    uint16_t         OdtEntryCount;  /* Absolute */
    union {
        uint8_t        b[XCP_DAQ_MEM_SIZE];
        tXcpDaqList   DaqList[XCP_DAQ_MEM_SIZE / sizeof(tXcpDaqList)];
    } u;
} tXcpDaq;





/* Shortcuts */

/* j is absolute odt number */
#define DaqListOdtEntryCount(j) ((gXcp.pOdt[j].lastOdtEntry-gXcp.pOdt[j].firstOdtEntry)+1)
#define DaqListOdtLastEntry(j)  (gXcp.pOdt[j].lastOdtEntry)
#define DaqListOdtFirstEntry(j) (gXcp.pOdt[j].firstOdtEntry)
#define DaqListOdtSize(j)       (gXcp.pOdt[j].size)

/* n is absolute odtEntry number */
#define OdtEntrySize(n)         (gXcp.pOdtEntrySize[n])
#define OdtEntryAddr(n)         (gXcp.pOdtEntryAddr[n])

/* i is daq number */
#define DaqListOdtCount(i)      ((gXcp.Daq.u.DaqList[i].lastOdt-gXcp.Daq.u.DaqList[i].firstOdt)+1)
#define DaqListLastOdt(i)       gXcp.Daq.u.DaqList[i].lastOdt
#define DaqListFirstOdt(i)      gXcp.Daq.u.DaqList[i].firstOdt
#define DaqListMode(i)          gXcp.Daq.u.DaqList[i].mode
#define DaqListState(i)         gXcp.Daq.u.DaqList[i].state
#define DaqListEventChannel(i)  gXcp.Daq.u.DaqList[i].eventChannel
#define DaqListPriority(i)      gXcp.Daq.u.DaqList[i].priority
#ifdef XCP_ENABLE_PACKED_MODE
#define DaqListSampleCount(i)   gXcp.Daq.u.DaqList[i].sampleCount
#endif


/****************************************************************************/
/* XCP Packet                                                */
/****************************************************************************/

typedef union {
    /* There might be a loss of up to 3 bytes. */
    uint8_t  b[((XCPTL_MAX_CTO_SIZE + 3) & 0xFFC)];
    uint16_t w[((XCPTL_MAX_CTO_SIZE + 3) & 0xFFC) / 2];
    uint32_t dw[((XCPTL_MAX_CTO_SIZE + 3) & 0xFFC) / 4];
} tXcpCto;


/****************************************************************************/
/* Protocol layer data                                                      */
/****************************************************************************/

typedef struct {

    uint16_t SessionStatus;

    uint8_t CrmLen;                        /* RES,ERR message length */
    uint8_t CroLen;                        /* CMD message length */
    tXcpCto Crm;                           /* RES,ERR message buffer */
    tXcpCto Cro;                           /* CMD message buffer */

    uint8_t* MtaPtr;                          /* Memory Transfer Address as pointer (ApplXcpGetPointer) */
    uint32_t MtaAddr;
    uint8_t MtaExt;

    /* Dynamic DAQ list structures, This structure should be stored in resume mode */
    tXcpDaq Daq;
    tXcpOdt* pOdt;
    uint32_t* pOdtEntryAddr;
    uint8_t* pOdtEntrySize;

    uint64_t DaqStartClock64;
    uint32_t DaqOverflowCount;

    /* State info from SET_DAQ_PTR for WRITE_DAQ and WRITE_DAQ_MULTIPLE */
    uint16_t WriteDaqOdtEntry;
    uint16_t WriteDaqOdt;
    uint16_t WriteDaqDaq;

    /* Optional event list */
#ifdef XCP_ENABLE_DAQ_EVENT_LIST

    uint16_t EventCount;
    tXcpEvent EventList[XCP_MAX_EVENT];

#endif


#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103

#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
    uint16_t ClusterId;
#endif

#if XCP_TRANSPORT_LAYER_TYPE==XCP_TRANSPORT_LAYER_ETH
    #pragma pack(push, 1)
    struct {
        T_CLOCK_INFO server;
#ifdef XCP_ENABLE_PTP
        T_CLOCK_INFO_GRANDMASTER grandmaster;
        T_CLOCK_INFO_RELATION relation;
#endif
    } ClockInfo;
    #pragma pack(pop)
#endif
#endif

} tXcpData;


static tXcpData gXcp = { 0,0,0 };

#define CRM                       (gXcp.Crm)
#define CRM_LEN                   (gXcp.CrmLen)
#define CRM_BYTE(x)               (gXcp.Crm.b[x])
#define CRM_WORD(x)               (gXcp.Crm.w[x])
#define CRM_DWORD(x)              (gXcp.Crm.dw[x])

#define CRO                       (gXcp.Cro)
#define CRO_LEN                   (gXcp.CroLen)
#define CRO_BYTE(x)               (gXcp.Cro.b[x])
#define CRO_WORD(x)               (gXcp.Cro.w[x])
#define CRO_DWORD(x)              (gXcp.Cro.dw[x])


/****************************************************************************/
/* Macros                                                                   */
/****************************************************************************/

#define error(e) { err=(e); goto negative_response; }
#define check_error(e) { err=(e); if (err!=0) goto negative_response;  }

#ifdef XCP_ENABLE_DYN_ADDRESSING
  #define check_result(e) { err=(e); if (err!=0) { if (err==CRC_CMD_PENDING) { XcpPushCommand(); goto no_response;} else goto negative_response; } }
#else
  #define check_result(e) { err=(e); if (err!=0) goto negative_response; }
#endif

#define isInitialized() (gXcp.SessionStatus & SS_INITIALIZED)
#define isStarted() (gXcp.SessionStatus & SS_STARTED)
#define isConnected() (gXcp.SessionStatus & SS_CONNECTED)
#define isDaqRunning() (gXcp.SessionStatus & SS_DAQ)
#define isCmdPending() (gXcp.SessionStatus & SS_CMD_PENDING)
#define isLegacyMode() (gXcp.SessionStatus & SS_LEGACY_MODE)


/****************************************************************************/
// Test instrumentation
/****************************************************************************/

#ifdef XCP_ENABLE_TEST_CHECKS
  #define check_len(n) if (cmdLen<(n)) { err = CRC_CMD_SYNTAX; goto negative_response; }
#else
  #define check_len(n)
#endif


#ifdef DBG_LEVEL
static void XcpPrintCmd();
static void XcpPrintRes();
static void XcpPrintDaqList(uint16_t daq);
#endif


/****************************************************************************/
/* Status                                                                   */
/****************************************************************************/

BOOL XcpIsStarted() {
  return isStarted();
}

BOOL XcpIsConnected() {
    return isConnected();
}

BOOL XcpIsDaqRunning() {
    return isDaqRunning();
}

BOOL XcpIsDaqEventRunning(uint16_t event) {

  if (!isDaqRunning()) return FALSE; // DAQ not running

  for (uint16_t daq = 0; daq < gXcp.Daq.DaqCount; daq++) {
    if ((DaqListState(daq) & DAQ_STATE_RUNNING) == 0) continue; // DAQ list not active
    if (DaqListEventChannel(daq) == event) return TRUE; // Event is associated to this DAQ list
  }

  return FALSE;
}

#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
uint16_t XcpGetClusterId() {
    return gXcp.ClusterId;
}
#endif

uint64_t XcpGetDaqStartTime() {
    return gXcp.DaqStartClock64;
}

uint32_t XcpGetDaqOverflowCount() {
    return gXcp.DaqOverflowCount;
}


/****************************************************************************/
/* Calibration                                                              */
/****************************************************************************/

// Write n bytes. Copying of size bytes from data to gXcp.MtaPtr
static uint8_t XcpWriteMta( uint8_t size, const uint8_t* data )
{
    // Ext=0x01 Relativ addressing
#ifdef XCP_ENABLE_DYN_ADDRESSING
    if (gXcp.MtaExt == 0x01) {
        return CRC_CMD_PENDING; // Async command
    }
#endif

    // Ext=0x00 Standard memory access
    if (gXcp.MtaExt == 0x00) {
        if (gXcp.MtaPtr == NULL) return CRC_ACCESS_DENIED;
        memcpy(gXcp.MtaPtr, data, size);
        return 0; // Ok
    }

    return CRC_ACCESS_DENIED; // Access violation
}

// Read n bytes. Copying of size bytes from data to gXcp.MtaPtr
static uint8_t XcpReadMta( uint8_t size, uint8_t* data )
{
    // Ext=0x01 Relativ addressing
#ifdef XCP_ENABLE_DYN_ADDRESSING
    if (gXcp.MtaExt == 0x01) {
        return CRC_CMD_PENDING; // Async command
    }
#endif

    // Ext=0xFF File (A2L) upload
#ifdef XCP_ENABLE_IDT_A2L_UPLOAD
    if (gXcp.MtaExt == 0xFF) {
        if (!ApplXcpReadA2L(size, gXcp.MtaAddr, data)) return CRC_ACCESS_DENIED; // Access violation
        gXcp.MtaAddr += size;
        return 0; // Ok
    }
#endif

    // Ext=0x00 Standard memory access
    if (gXcp.MtaExt == 0x00) {
        if (gXcp.MtaPtr == NULL) return CRC_ACCESS_DENIED;
        memcpy(data, gXcp.MtaPtr, size);
        return 0; // Ok
    }

    return CRC_ACCESS_DENIED; // Access violation
}



/****************************************************************************/
/* Data Aquisition Setup                                                    */
/****************************************************************************/

// Free all dynamic DAQ lists
static void  XcpFreeDaq( void )
{
  gXcp.SessionStatus &= (uint16_t)(~SS_DAQ);

  gXcp.Daq.DaqCount = 0;
  gXcp.Daq.OdtCount = 0;
  gXcp.Daq.OdtEntryCount = 0;

  gXcp.pOdt = (tXcpOdt*)0;
  gXcp.pOdtEntryAddr = 0;
  gXcp.pOdtEntrySize = 0;

  memset((uint8_t*)&gXcp.Daq.u.b[0], 0, XCP_DAQ_MEM_SIZE);
}

// Allocate Memory for daq,odt,odtEntries and Queue according to DaqCount, OdtCount and OdtEntryCount
static uint8_t  XcpAllocMemory( void )
{
  uint32_t s;

  /* Check memory overflow */
  s = ( gXcp.Daq.DaqCount * (uint32_t)sizeof(tXcpDaqList ) +
      ( gXcp.Daq.OdtCount * (uint32_t)sizeof(tXcpOdt) ) +
      ( gXcp.Daq.OdtEntryCount * ((uint32_t)sizeof(uint8_t*) + (uint32_t)sizeof(uint8_t) ) ) );

  if (s>=XCP_DAQ_MEM_SIZE) return CRC_MEMORY_OVERFLOW;

  gXcp.pOdt = (tXcpOdt*)&gXcp.Daq.u.DaqList[gXcp.Daq.DaqCount];
  gXcp.pOdtEntryAddr = (uint32_t*)&gXcp.pOdt[gXcp.Daq.OdtCount];
  gXcp.pOdtEntrySize = (uint8_t*)&gXcp.pOdtEntryAddr[gXcp.Daq.OdtEntryCount];
  
  DBG_PRINTF4("[XcpAllocMemory] %u of %u Bytes used\n",s,XCP_DAQ_MEM_SIZE );
  return 0;
}

// Allocate daqCount DAQ lists
static uint8_t  XcpAllocDaq( uint16_t daqCount )
{
  if ( (gXcp.Daq.OdtCount!=0) || (gXcp.Daq.OdtEntryCount!=0) )  {
    return CRC_SEQUENCE;
  }
  if( daqCount == 0 || daqCount>255)  {
    return CRC_OUT_OF_RANGE;
  }

  gXcp.Daq.DaqCount = (uint8_t)daqCount;
  return XcpAllocMemory();
}

// Allocate odtCount ODTs in a DAQ list
static uint8_t XcpAllocOdt( uint16_t daq, uint8_t odtCount )
{
  uint32_t n;

  if ( (gXcp.Daq.DaqCount==0) || (gXcp.Daq.OdtEntryCount!=0) )  {
    return CRC_SEQUENCE;
  }
  if( odtCount == 0 ) {
    return CRC_OUT_OF_RANGE;
  }

  n = (uint32_t)gXcp.Daq.OdtCount + (uint32_t)odtCount;
  if (n > 0xFFFF) return CRC_OUT_OF_RANGE; // Overall number of ODTs limited to 64K

  gXcp.Daq.u.DaqList[daq].firstOdt = gXcp.Daq.OdtCount;
  gXcp.Daq.OdtCount = (uint16_t)n;
  gXcp.Daq.u.DaqList[daq].lastOdt = (uint16_t)(gXcp.Daq.OdtCount-1);
  gXcp.Daq.u.DaqList[daq].eventChannel = 0xFFFF; // Undefined

  return XcpAllocMemory();
}

// Adjust ODT size by size
static BOOL  XcpAdjustOdtSize(uint16_t daq, uint16_t odt, uint8_t size) {
#ifdef XCP_ENABLE_PACKED_MODE
    uint16_t sc = DaqListSampleCount(daq);
    if (sc == 0) sc = 1;
    DaqListOdtSize(odt) = (uint16_t)(DaqListOdtSize(odt) + size*sc);
#else
    (void) daq;
    DaqListOdtSize(odt) = (uint16_t)(DaqListOdtSize(odt) + size);
#endif
#ifdef XCP_ENABLE_TEST_CHECKS
    if (DaqListOdtSize(odt) > XCPTL_MAX_DTO_SIZE) {
        DBG_PRINTF_ERROR("ERROR: ODT size %u exceed XCPTL_MAX_DTO_SIZE %u!\n", DaqListOdtSize(odt), XCPTL_MAX_DTO_SIZE);
        return FALSE;
    }
#endif
    return TRUE;
}

// Allocate all ODT entries, Parameter odt is relative odt number
static uint8_t XcpAllocOdtEntry( uint16_t daq, uint8_t odt, uint8_t odtEntryCount )
{
  int xcpFirstOdt;
  uint32_t n;

  if ( (gXcp.Daq.DaqCount==0) || (gXcp.Daq.OdtCount==0) )  {
    return (uint8_t)CRC_SEQUENCE;
  }
  if (odtEntryCount==0)  {
    return (uint8_t)CRC_OUT_OF_RANGE;
  }

  /* Absolute ODT entry count is limited to 64K */
  n = (uint32_t)gXcp.Daq.OdtEntryCount + (uint32_t)odtEntryCount;
  if (n>0xFFFF) return CRC_MEMORY_OVERFLOW;

  xcpFirstOdt = gXcp.Daq.u.DaqList[daq].firstOdt;
  gXcp.pOdt[xcpFirstOdt+odt].firstOdtEntry = gXcp.Daq.OdtEntryCount;
  gXcp.Daq.OdtEntryCount = (uint16_t)n;
  gXcp.pOdt[xcpFirstOdt + odt].lastOdtEntry = (uint16_t)(gXcp.Daq.OdtEntryCount - 1);
  gXcp.pOdt[xcpFirstOdt + odt].size = 0;

  return XcpAllocMemory();
}

// Set ODT entry pointer
static uint8_t  XcpSetDaqPtr(uint16_t daq, uint8_t odt, uint8_t idx) {

    uint16_t odt0 = (uint16_t)(DaqListFirstOdt(daq) + odt); // Absolute odt index
    if ((daq >= gXcp.Daq.DaqCount) || (odt >= DaqListOdtCount(daq)) || (idx >= DaqListOdtEntryCount(odt0))) return CRC_OUT_OF_RANGE;
    // Save info for XcpAddOdtEntry from WRITE_DAQ and WRITE_DAQ_MULTIPLE
    gXcp.WriteDaqOdtEntry = (uint16_t)(DaqListOdtFirstEntry(odt0) + idx); // Absolute odt entry index
    gXcp.WriteDaqOdt = odt0; // Absolute odt index
    gXcp.WriteDaqDaq = daq;
    return 0;
}

// Add an ODT entry to current DAQ/ODT
static uint8_t XcpAddOdtEntry(uint32_t addr, uint8_t ext, uint8_t size) {

    if ((size == 0) || size > XCP_MAX_ODT_ENTRY_SIZE) return CRC_OUT_OF_RANGE;
    if (0 == gXcp.Daq.DaqCount || 0 == gXcp.Daq.OdtCount || 0 == gXcp.Daq.OdtEntryCount) return CRC_DAQ_CONFIG;

#ifndef XCP_ENABLE_DYN_ADDRESSING
    if (ext > 0) return CRC_ACCESS_DENIED; // Not supported
#else
    if (ext > 1) return CRC_ACCESS_DENIED; // Not supported
    if (ext == 1) {
        uint16_t e0 = DaqListEventChannel(gXcp.WriteDaqDaq);
        uint16_t e1 = (uint16_t)(addr >> 16);
        addr &= 0x0000FFFF;
        if (e0 != 0xFFFF && e0 != e1) return CRC_OUT_OF_RANGE; // Error event channel redefinition
        DaqListEventChannel(gXcp.WriteDaqDaq) = e1;
     }
    else
#endif
    {
        uint8_t* p;
        uint64_t a;
        p = ApplXcpGetPointer(ext, addr);
        if (p == NULL) return CRC_ACCESS_DENIED; // Access denied
        a = p - ApplXcpGetBaseAddr();
        if (a>0xFFFFFFFF) return CRC_ACCESS_DENIED; // Access out of range
        addr = (uint32_t)a;
    }

    OdtEntrySize(gXcp.WriteDaqOdtEntry) = size;
    OdtEntryAddr(gXcp.WriteDaqOdtEntry) = addr; // Holds A2L/XCP address
    if (!XcpAdjustOdtSize(gXcp.WriteDaqDaq, gXcp.WriteDaqOdt, size)) return CRC_DAQ_CONFIG;
    gXcp.WriteDaqOdtEntry++; // Autoincrement to next ODT entry, no autoincrementing over ODTs
    return 0;
}

// Set DAQ list mode
static uint8_t XcpSetDaqListMode(uint16_t daq, uint16_t event, uint8_t mode, uint8_t prio ) {

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    tXcpEvent* e = XcpGetEvent(event); // Check if event exists
    if (e == NULL) return CRC_OUT_OF_RANGE;
#endif
#ifdef XCP_ENABLE_DYN_ADDRESSING 
    uint16_t e0 = DaqListEventChannel(daq);
    if (e0 != 0xFFFF && event != e0) return CRC_OUT_OF_RANGE; // Error event channel redefinition
#endif
    DaqListEventChannel(daq) = event;
    DaqListMode(daq) = mode;
    DaqListPriority(daq) = prio;
  return 0;
}

// Start DAQ list
// Start event processing
static void XcpStartDaq( uint16_t daq )
{
  DaqListState(daq) |= DAQ_STATE_RUNNING;
  gXcp.SessionStatus |= SS_DAQ;
}

// Start all selected DAQs
// Start event processing
static void XcpStartAllSelectedDaq()
{
  uint16_t daq;
  
  gXcp.DaqStartClock64 = ApplXcpGetClock64();
  gXcp.DaqOverflowCount = 0;

  // Reset event time stamps
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
#ifdef XCP_ENABLE_TEST_CHECKS
  for (uint16_t e = 0; e < gXcp.EventCount; e++) {
      gXcp.EventList[e].time = 0;
  }
#endif
#endif

  // Start all selected DAQs
  for (daq=0;daq<gXcp.Daq.DaqCount;daq++)  {
    if ( (DaqListState(daq) & DAQ_STATE_SELECTED) != 0 ) {
      DaqListState(daq) |= DAQ_STATE_RUNNING;
      DaqListState(daq) &= (uint8_t)~DAQ_STATE_SELECTED;
#ifdef DBG_LEVEL
      if (DBG_LEVEL >= 3) {
          XcpPrintDaqList(daq);
      }
#endif
    }
  }
#ifdef DBG_LEVEL
  if (DBG_LEVEL >= 2) {
    char ts[64];
    clockGetString(ts, sizeof(ts), gXcp.DaqStartClock64);
    printf("DAQ start at t=%s\n", ts);
  }
#endif
  gXcp.SessionStatus |= SS_DAQ;
}

// Stop DAQ list
// Returns TRUE if all DAQ lists are stopped and event procession has stopped
static uint8_t XcpStopDaq( uint16_t daq )
{
  DaqListState(daq) &= (uint8_t)(~(DAQ_STATE_OVERRUN|DAQ_STATE_RUNNING));

  /* Check if all DAQ lists are stopped */
  for (daq=0; daq<gXcp.Daq.DaqCount; daq++)  {
    if ( (DaqListState(daq) & DAQ_STATE_RUNNING) != 0 )  {
      return 0;
    }
  }
  gXcp.SessionStatus &= (uint16_t)(~SS_DAQ); // Stop processing DAQ events
  return 1;
}

// Stop all selected DAQs
// Does not stop event processing
static void XcpStopAllSelectedDaq()
{
  uint16_t daq;

  for (daq=0;daq<gXcp.Daq.DaqCount;daq++) {
    if ( (DaqListState(daq) & DAQ_STATE_SELECTED) != 0 ) {
      XcpStopDaq(daq);
      DaqListState(daq) = DAQ_STATE_STOPPED_UNSELECTED;
    }
  }
}

// Stop all DAQs
static void XcpStopAllDaq( void )
{
  for (uint8_t daq=0; daq<gXcp.Daq.DaqCount; daq++) {
    DaqListState(daq) = DAQ_STATE_STOPPED_UNSELECTED;
  }
  gXcp.SessionStatus &= (uint16_t)(~SS_DAQ); // Stop processing DAQ events
}


/****************************************************************************/
/* Data Aquisition Processor                                                */
/****************************************************************************/

// Measurement data acquisition, sample and transmit measurement date associated to event

static void XcpEvent_(uint16_t event, uint8_t* base, uint64_t clock)
{
  uint8_t* d;
  uint8_t* d0;
  uint32_t e, el, odt, hs, n;
  uint16_t daq;
#ifdef XCP_ENABLE_PACKED_MODE
  uint32_t sc;
#endif
  void* handle = NULL;

  if (!isDaqRunning()) return; // DAQ not running

  // Event checks
  // Disable for maximal measurement performance
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
#if defined(XCP_ENABLE_TEST_CHECKS) || defined(XCP_ENABLE_MULTITHREAD_EVENTS)
  tXcpEvent* ev = XcpGetEvent(event);
  if (ev == NULL) {
      DBG_PRINTF_ERROR("ERROR: Unknown event %u!\n", event);
      return; // Unknown event
  }
#endif
#endif

  // Loop over all active DAQ lists associated to the current event
  for (daq=0; daq<gXcp.Daq.DaqCount; daq++) {
      if ((DaqListState(daq) & DAQ_STATE_RUNNING) == 0) continue; // DAQ list not active
      if (DaqListEventChannel(daq) != event) continue; // DAQ list not associated with this event
#ifdef XCP_ENABLE_PACKED_MODE
      sc = DaqListSampleCount(daq); // Packed mode sample count, 0 if not packed
#endif

      // Loop over all ODTs of the current DAQ list
      for (hs=2+4,odt=DaqListFirstOdt(daq);odt<=DaqListLastOdt(daq);hs=2,odt++)  {

          // Mutex to ensure transmit buffers with time stamp in ascending order
#ifdef XCP_ENABLE_MULTITHREAD_EVENTS
          mutexLock(&ev->mutex);
#endif
          // Get clock, if not given as parameter
          if (clock==0) clock = ApplXcpGetClock64();

          // Get DTO buffer
          d0 = XcpTlGetTransmitBuffer(&handle, (uint16_t)(DaqListOdtSize(odt) + hs));

#ifdef XCP_ENABLE_MULTITHREAD_EVENTS
          mutexUnlock(&ev->mutex);
#endif

          // Check declining time stamps
          // Disable for maximal measurement performance
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
#ifdef XCP_ENABLE_TEST_CHECKS
          if (ev->time > clock) { // declining time stamps
              DBG_PRINTF_ERROR("ERROR: Declining timestamp! event=%u, diff=%" PRIu64 "\n", event, ev->time-clock);
          }
          if (ev->time == clock) { // duplicate time stamps
              DBG_PRINTF3("WARNING: Duplicate timestamp! event=%u\n", event);
          }
#endif
#endif

         // Buffer overrun
         if (d0 == NULL) {
            gXcp.DaqOverflowCount++;
            DaqListState(daq) |= DAQ_STATE_OVERRUN;
            return; // Skip rest of this event on queue overrun
        }

        // ODT,DAQ header
        d0[0] = (uint8_t)(odt-DaqListFirstOdt(daq)); /* Relative odt number */
        d0[1] = (uint8_t)daq;

        // Use BIT7 of PID or ODT to indicate overruns
        if ( (DaqListState(daq) & DAQ_STATE_OVERRUN) != 0 ) {
          d0[0] |= 0x80;
          DaqListState(daq) &= (uint8_t)(~DAQ_STATE_OVERRUN);
        }

        // Timestamp
        if (hs == 2+4) { // First ODT (data starts at offset 6) always has a 32 bit timestamp
            *((uint32_t*)&d0[2]) = (uint32_t)clock;
        }

        // Copy data 
        /* This is the inner loop, optimize here */
        e = DaqListOdtFirstEntry(odt);
        if (OdtEntrySize(e) != 0) {
            el = DaqListOdtLastEntry(odt);
            d = &d0[hs];
            while (e <= el) { // inner DAQ loop
                n = OdtEntrySize(e);
                if (n == 0) break;
#ifdef XCP_ENABLE_PACKED_MODE
                if (sc>1) n *= sc; // packed mode
#endif
                memcpy((uint8_t*)d, &base[OdtEntryAddr(e)], n);
                d += n;
                e++;
            } // ODT entry
        }

        XcpTlCommitTransmitBuffer(handle, DaqListPriority(daq)!=0 && odt==DaqListLastOdt(daq));

      } /* odt */

  } /* daq */

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
#ifdef XCP_ENABLE_TEST_CHECKS
  ev->time = clock;
#endif
#endif
}

void XcpEventAt(uint16_t event, uint64_t clock) {
    if (!isDaqRunning()) return; // DAQ not running
    XcpEvent_(event, ApplXcpGetBaseAddr(), clock);
}

void XcpEventExt(uint16_t event, uint8_t* base) {

#ifdef XCP_ENABLE_DYN_ADDRESSING
    if (!isStarted()) return;
    if (isCmdPending()) { // Pending command, check if it can be executed in this context
        if (gXcp.MtaExt == 1 && (uint16_t)(gXcp.MtaAddr >> 16) == event) {
            // Convert MtaPtr to context
            gXcp.MtaPtr = base + (gXcp.MtaAddr & 0xFFFF);
            gXcp.MtaExt = 0;
            XcpCommand((const uint32_t*)&gXcp.Cro, gXcp.CroLen);
            gXcp.SessionStatus &= (uint16_t)~SS_CMD_PENDING;
        }
    }
#endif

    if (!isDaqRunning()) return; // DAQ not running
    XcpEvent_(event, base, 0);
}

void XcpEvent(uint16_t event) {
    if (!isDaqRunning()) return; // DAQ not running
    XcpEvent_(event, ApplXcpGetBaseAddr(), 0);
}


/****************************************************************************/
/* Command Processor                                                        */
/****************************************************************************/


// Stops DAQ and goes to disconnected state
void XcpDisconnect( void )
{
  if (!isStarted()) return;

  if (isDaqRunning()) {
    ApplXcpStopDaq();
    XcpStopAllDaq();
    XcpTlWaitForTransmitQueueEmpty(); // Wait until transmit queue empty
  }

  gXcp.SessionStatus &= (uint16_t)(~SS_CONNECTED);
}

// Transmit command response
static void XcpSendResponse() {

  XcpTlSendCrm((const uint8_t*)&gXcp.Crm, gXcp.CrmLen);
#ifdef DBG_LEVEL
  if (DBG_LEVEL >= 2) XcpPrintRes();
#endif
}

// Transmit multicast command response
#if XCP_TRANSPORT_LAYER_TYPE==XCP_TRANSPORT_LAYER_ETH
static void XcpSendMulticastResponse( uint8_t *addr, uint16_t port) {

  XcpEthTlSendMulticastCrm((const uint8_t*)&gXcp.Crm, gXcp.CrmLen, addr, port);
#ifdef DBG_LEVEL
  if (DBG_LEVEL >= 2) XcpPrintRes();
#endif
}
#endif

//  Push XCP command which can not be executes in this context for later async execution
#ifdef XCP_ENABLE_DYN_ADDRESSING
static void XcpPushCommand() {
    gXcp.SessionStatus |= (uint16_t)SS_CMD_PENDING;
}
#endif

//  Handles incoming XCP commands
void XcpCommand( const uint32_t* cmdData, uint16_t cmdLen )
{

  uint8_t err = 0;

  if (!isStarted()) return;
  if (cmdLen > sizeof(gXcp.Cro)) return;
  
  gXcp.CroLen = (uint8_t)cmdLen;
  memcpy(&gXcp.Cro, cmdData, cmdLen);

  // Prepare the default response
  CRM_CMD = PID_RES; /* Response, no error */
  gXcp.CrmLen = 1; /* Length = 1 */

  // CONNECT ?
#if XCP_TRANSPORT_LAYER_TYPE == XCP_TRANSPORT_LAYER_ETH
  if (cmdLen==CRO_CONNECT_LEN && CRO_CMD==CC_CONNECT)
#else
  if (cmdLen>=CRO_CONNECT_LEN && CRO_CMD==CC_CONNECT)
#endif
  {
#ifdef DBG_LEVEL
      DBG_PRINTF2("CONNECT mode=%u\n", CRO_CONNECT_MODE);
      if (gXcp.SessionStatus & SS_CONNECTED) DBG_PRINT1("  Already connected! DAQ setup cleared! Legacy mode activated!\n");
#endif

      // Check application is ready for XCP connect 
      if (!ApplXcpConnect()) error(CRC_ACCESS_DENIED);

      // Initialize Session Status
      gXcp.SessionStatus = (uint16_t)(SS_INITIALIZED | SS_STARTED | SS_CONNECTED | SS_LEGACY_MODE);

      /* Reset DAQ */
      XcpFreeDaq();

      // Response
      gXcp.CrmLen = CRM_CONNECT_LEN;
      CRM_CONNECT_TRANSPORT_VERSION = (uint8_t)( (uint16_t)XCP_TRANSPORT_LAYER_VERSION >> 8 ); /* Major versions of the XCP Protocol Layer and Transport Layer Specifications. */
      CRM_CONNECT_PROTOCOL_VERSION =  (uint8_t)( (uint16_t)XCP_PROTOCOL_LAYER_VERSION >> 8 );
      CRM_CONNECT_MAX_CTO_SIZE = XCPTL_MAX_CTO_SIZE;
      CRM_CONNECT_MAX_DTO_SIZE = XCPTL_MAX_DTO_SIZE;
      CRM_CONNECT_RESOURCE = RM_DAQ;       /* Data Acquisition supported */
      CRM_CONNECT_COMM_BASIC = CMB_OPTIONAL;
#if defined ( XCP_CPUTYPE_BIGENDIAN )
      CRM_CONNECT_COMM_BASIC |= (uint8_t)PI_MOTOROLA;
#endif
  }

  // Handle other all other commands
  else {

#ifdef DBG_LEVEL
      if (DBG_LEVEL >= 2) XcpPrintCmd();
#endif
      if (!isConnected() && CRO_CMD!= CC_TRANSPORT_LAYER_CMD) { // Must be connected, exception are the transport layer commands
          DBG_PRINT1("Command ignored because not in connected state, no response sent!\n");
          return;
      }

      if (cmdLen<1 || cmdLen>XCPTL_MAX_CTO_SIZE) error(CRC_CMD_SYNTAX);
      switch (CRO_CMD)
      {
        case CC_SYNCH:
          {
            /* Always return a negative response with the error code ERR_CMD_SYNCH. */
            gXcp.CrmLen = CRM_SYNCH_LEN;
            CRM_CMD = PID_ERR;
            CRM_ERR = CRC_CMD_SYNCH;
           }
          break;

        case CC_GET_COMM_MODE_INFO:
          {
            gXcp.CrmLen = CRM_GET_COMM_MODE_INFO_LEN;
            CRM_GET_COMM_MODE_INFO_DRIVER_VERSION = XCP_DRIVER_VERSION;
#ifdef XCP_ENABLE_INTERLEAVED
            CRM_GET_COMM_MODE_INFO_COMM_OPTIONAL = 0; // CMO_INTERLEAVED_MODE;
            CRM_GET_COMM_MODE_INFO_QUEUE_SIZE = XCP_INTERLEAVED_QUEUE_SIZE;
#else
            CRM_GET_COMM_MODE_INFO_COMM_OPTIONAL = 0;
            CRM_GET_COMM_MODE_INFO_QUEUE_SIZE = 0;
#endif
            CRM_GET_COMM_MODE_INFO_MAX_BS = 0;
            CRM_GET_COMM_MODE_INFO_MIN_ST = 0;
          }
          break;

        case CC_DISCONNECT:
          {
            XcpDisconnect();
          }
          break;

        case CC_GET_ID:
          {
              check_len(CRO_GET_ID_LEN);
              CRM_LEN = CRM_GET_ID_LEN;
              CRM_GET_ID_MODE = 0; // Transfer mode
              CRM_GET_ID_LENGTH = 0;
              switch (CRO_GET_ID_TYPE) {
                case IDT_ASCII: 
                case IDT_ASAM_NAME:
                case IDT_ASAM_PATH:
                case IDT_ASAM_URL:
                case IDT_ASAM_EPK:
                  CRM_GET_ID_LENGTH = ApplXcpGetId(CRO_GET_ID_TYPE, CRM_GET_ID_DATA, CRM_GET_ID_DATA_MAX_LEN);
                  CRM_LEN = (uint8_t)(CRM_GET_ID_LEN+CRM_GET_ID_LENGTH);
                  CRM_GET_ID_MODE = 0x01; // Uncompressed data in response
                  break;
#ifdef XCP_ENABLE_IDT_A2L_UPLOAD
                case IDT_ASAM_UPLOAD:
                    gXcp.MtaAddr = 0;
                    gXcp.MtaExt = 0xFF;
                    CRM_GET_ID_LENGTH = ApplXcpGetId(CRO_GET_ID_TYPE, NULL, 0);
                    CRM_GET_ID_MODE = 0x00; // Uncompressed data upload 
                    break;
#endif
                default:
                  error(CRC_OUT_OF_RANGE);
              }
          }
          break;

        case CC_GET_STATUS:
          {
            gXcp.CrmLen = CRM_GET_STATUS_LEN;
            CRM_GET_STATUS_STATUS = (uint8_t)(gXcp.SessionStatus&0xFF);
            CRM_GET_STATUS_PROTECTION = 0;
            CRM_GET_STATUS_CONFIG_ID = 0; /* Session configuration ID not available. */
          }
          break;

        case CC_SET_MTA:
          {            
            check_len(CRO_SET_MTA_LEN);
            gXcp.MtaExt = CRO_SET_MTA_EXT;
            gXcp.MtaAddr = CRO_SET_MTA_ADDR;
            gXcp.MtaPtr = NULL;
            if (gXcp.MtaExt > 1) error(CRC_OUT_OF_RANGE);
            if (gXcp.MtaExt == 0) {
                gXcp.MtaPtr = ApplXcpGetPointer(gXcp.MtaExt, gXcp.MtaAddr);
            }
          }
          break;

        case CC_DOWNLOAD:
          {
            check_len(CRO_DOWNLOAD_LEN);
            uint8_t size = CRO_DOWNLOAD_SIZE; // Variable cmdLen
            if (size > CRO_DOWNLOAD_MAX_SIZE || size > cmdLen-CRO_DOWNLOAD_LEN) error(CRC_CMD_SYNTAX)
            check_result(XcpWriteMta(size, CRO_DOWNLOAD_DATA));
          }
          break;

        case CC_SHORT_DOWNLOAD:
          {
            check_len(CRO_SHORT_DOWNLOAD_LEN);
            uint8_t size = CRO_SHORT_DOWNLOAD_SIZE; // Variable cmdLen
            if (size > CRO_SHORT_DOWNLOAD_MAX_SIZE || size > cmdLen - CRO_SHORT_DOWNLOAD_LEN) error(CRC_CMD_SYNTAX)
            if (!isCmdPending()) {
                gXcp.MtaExt = CRO_SHORT_DOWNLOAD_EXT;
                gXcp.MtaAddr = CRO_SHORT_DOWNLOAD_ADDR;
                gXcp.MtaPtr = ApplXcpGetPointer(gXcp.MtaExt, gXcp.MtaAddr);
            }
            check_result(XcpWriteMta(size, CRO_SHORT_DOWNLOAD_DATA));
          }
          break;

        case CC_UPLOAD:
          {
            check_len(CRO_UPLOAD_LEN);
            uint8_t size = CRO_UPLOAD_SIZE;
            if (size > CRM_UPLOAD_MAX_SIZE) error(CRC_OUT_OF_RANGE);
            check_result(XcpReadMta(size,CRM_UPLOAD_DATA));
            gXcp.CrmLen = (uint8_t)(CRM_UPLOAD_LEN+size);
          }
          break;

        case CC_SHORT_UPLOAD:
          {
            check_len(CRO_SHORT_UPLOAD_LEN);
            uint8_t size = CRO_SHORT_UPLOAD_SIZE;
            if (size > CRM_SHORT_UPLOAD_MAX_SIZE) error(CRC_OUT_OF_RANGE);
            if (!isCmdPending()) {
                gXcp.MtaExt = CRO_SHORT_UPLOAD_EXT;
                gXcp.MtaAddr = CRO_SHORT_UPLOAD_ADDR;
                gXcp.MtaPtr = ApplXcpGetPointer(gXcp.MtaExt, gXcp.MtaAddr);
            }
            check_result(XcpReadMta(size,CRM_SHORT_UPLOAD_DATA));
            gXcp.CrmLen = (uint8_t)(CRM_SHORT_UPLOAD_LEN+size);
          }
          break;

#ifdef XCP_ENABLE_CAL_PAGE
        case CC_SET_CAL_PAGE:
          {
            check_len(CRO_SET_CAL_PAGE_LEN);
            check_error(ApplXcpSetCalPage(CRO_SET_CAL_PAGE_SEGMENT, CRO_SET_CAL_PAGE_PAGE, CRO_SET_CAL_PAGE_MODE));
          }
          break;

        case CC_GET_CAL_PAGE:
          {
            check_len(CRO_GET_CAL_PAGE_LEN);
            gXcp.CrmLen = CRM_GET_CAL_PAGE_LEN;
            CRM_GET_CAL_PAGE_PAGE = ApplXcpGetCalPage(CRO_GET_CAL_PAGE_SEGMENT, CRO_GET_CAL_PAGE_MODE);
          }
          break;
#endif // XCP_ENABLE_CAL_PAGE

#ifdef XCP_ENABLE_CHECKSUM
        case CC_BUILD_CHECKSUM:
          {
            check_len(CRO_BUILD_CHECKSUM_LEN);
            uint32_t n = CRO_BUILD_CHECKSUM_SIZE;
            uint32_t s = 0;
            uint32_t d,i;
            //if (n % 4 != 0) error(CRC_OUT_OF_RANGE)
            //n = n / 4;
            n = (n + 3) / 4;
            for (i = 0; i < n; i++) { 
              check_error(XcpReadMta(4, (uint8_t*)&d)); 
              s += d; 
            }
            CRM_BUILD_CHECKSUM_RESULT = s;
            CRM_BUILD_CHECKSUM_TYPE = XCP_CHECKSUM_TYPE_ADD44;
            gXcp.CrmLen = CRM_BUILD_CHECKSUM_LEN;
          }
          break;
#endif // XCP_ENABLE_CHECKSUM

        case CC_GET_DAQ_PROCESSOR_INFO:
          {
            gXcp.CrmLen = CRM_GET_DAQ_PROCESSOR_INFO_LEN;
            CRM_GET_DAQ_PROCESSOR_INFO_MIN_DAQ = 0;
            CRM_GET_DAQ_PROCESSOR_INFO_MAX_DAQ = (gXcp.Daq.DaqCount); /* dynamic */
#if defined ( XCP_ENABLE_DAQ_EVENT_INFO )
            CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT = gXcp.EventCount;
#else
            CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT = 0; /* Unknown */
#endif
            CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE = (uint8_t)DAQ_HDR_ODT_DAQB; /* DTO identification field type: Relative ODT number, absolute list number (BYTE) */
            CRM_GET_DAQ_PROCESSOR_INFO_PROPERTIES = (uint8_t)( DAQ_PROPERTY_CONFIG_TYPE | DAQ_PROPERTY_TIMESTAMP | DAQ_OVERLOAD_INDICATION_PID );
          }
          break;

        case CC_GET_DAQ_RESOLUTION_INFO:
          {
            gXcp.CrmLen = CRM_GET_DAQ_RESOLUTION_INFO_LEN;
            CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_DAQ = 1;
            CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_STIM = 1;
            CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_DAQ  = (uint8_t)XCP_MAX_ODT_ENTRY_SIZE;
            CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_STIM = (uint8_t)XCP_MAX_ODT_ENTRY_SIZE;
            CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE = XCP_TIMESTAMP_UNIT | DAQ_TIMESTAMP_FIXED | DAQ_TIMESTAMP_DWORD;
            CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS = XCP_TIMESTAMP_TICKS;
          }
          break;

#ifdef XCP_ENABLE_DAQ_EVENT_INFO
        case CC_GET_DAQ_EVENT_INFO:
          {
            check_len(CRO_GET_DAQ_EVENT_INFO_LEN);
            uint16_t eventNumber = CRO_GET_DAQ_EVENT_INFO_EVENT;
            tXcpEvent* event = XcpGetEvent(eventNumber);
            if (event==NULL) error(CRC_OUT_OF_RANGE);
            gXcp.CrmLen = CRM_GET_DAQ_EVENT_INFO_LEN;
            CRM_GET_DAQ_EVENT_INFO_PROPERTIES = DAQ_EVENT_PROPERTIES_DAQ | DAQ_EVENT_PROPERTIES_EVENT_CONSISTENCY;
#ifdef XCP_ENABLE_PACKED_MODE
            if (ApplXcpEventList[event].sampleCount) CRM_GET_DAQ_EVENT_INFO_PROPERTIES |= DAQ_EVENT_PROPERTIES_PACKED;
#endif
            // if (event->size) CRM_GET_DAQ_EVENT_INFO_PROPERTIES |= DAQ_EVENT_PROPERTIES_EXT; @@@@ V1.6
            CRM_GET_DAQ_EVENT_INFO_MAX_DAQ_LIST = 0xFF;
            CRM_GET_DAQ_EVENT_INFO_NAME_LENGTH = (uint8_t)strlen(event->name);
            CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE = event->timeCycle;
            CRM_GET_DAQ_EVENT_INFO_TIME_UNIT = event->timeUnit;
            CRM_GET_DAQ_EVENT_INFO_PRIORITY = event->priority;
            gXcp.MtaPtr = (uint8_t*)event->name;
            gXcp.MtaExt = 0;
          }
          break;
#endif // XCP_ENABLE_DAQ_EVENT_INFO

        case CC_FREE_DAQ:
          {
              XcpFreeDaq();
          }
          break;

        case CC_ALLOC_DAQ:
          {
            check_len(CRO_ALLOC_DAQ_LEN);
            uint16_t count = CRO_ALLOC_DAQ_COUNT;
            check_error(XcpAllocDaq(count));
          }
          break;

        case CC_ALLOC_ODT:
          {
            check_len(CRO_ALLOC_ODT_LEN);
            uint16_t daq = CRO_ALLOC_ODT_DAQ;
            uint8_t count = CRO_ALLOC_ODT_COUNT;
            if (daq >= gXcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
            check_error( XcpAllocOdt(daq, count) )
          }
          break;

        case CC_ALLOC_ODT_ENTRY:
          {
            check_len(CRO_ALLOC_ODT_ENTRY_LEN);
            uint16_t daq = CRO_ALLOC_ODT_ENTRY_DAQ;
            uint8_t odt = CRO_ALLOC_ODT_ENTRY_ODT;
            uint8_t count = CRO_ALLOC_ODT_ENTRY_COUNT;
            if ((daq >= gXcp.Daq.DaqCount) || (odt >= DaqListOdtCount(daq))) error(CRC_OUT_OF_RANGE);
            check_error( XcpAllocOdtEntry(daq, odt, count) )
          }
          break;

        case CC_GET_DAQ_LIST_MODE:
          {
            check_len(CRO_GET_DAQ_LIST_MODE_LEN);
            uint16_t daq = CRO_GET_DAQ_LIST_MODE_DAQ;
            if (daq >= gXcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
            gXcp.CrmLen = CRM_GET_DAQ_LIST_MODE_LEN;
            CRM_GET_DAQ_LIST_MODE_MODE = DaqListMode(daq);
            CRM_GET_DAQ_LIST_MODE_PRESCALER = 1;
            CRM_GET_DAQ_LIST_MODE_EVENTCHANNEL = DaqListEventChannel(daq);
            CRM_GET_DAQ_LIST_MODE_PRIORITY = DaqListPriority(daq);
          }
          break;

        case CC_SET_DAQ_LIST_MODE:
          {
            check_len(CRO_SET_DAQ_LIST_MODE_LEN);
            uint16_t daq = CRO_SET_DAQ_LIST_MODE_DAQ;
            uint16_t event = CRO_SET_DAQ_LIST_MODE_EVENTCHANNEL;
            uint8_t mode = CRO_SET_DAQ_LIST_MODE_MODE;
            uint8_t prio = CRO_SET_DAQ_LIST_MODE_PRIORITY;
            if (daq >= gXcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
            if (mode & (DAQ_MODE_ALTERNATING | DAQ_MODE_DIRECTION | DAQ_MODE_DTO_CTR | DAQ_MODE_PID_OFF)) error(CRC_OUT_OF_RANGE);  // none of these modes implemented
            if (0==(mode & (DAQ_MODE_TIMESTAMP))) error(CRC_CMD_SYNTAX);  // timestamp is fixed on
            if (CRO_SET_DAQ_LIST_MODE_PRESCALER > 1) error(CRC_OUT_OF_RANGE); // prescaler is not implemented
            check_error(XcpSetDaqListMode(daq, event, mode, prio));
            break;
          }

        case CC_SET_DAQ_PTR:
          {
            check_len(CRO_SET_DAQ_PTR_LEN);
            uint16_t daq = CRO_SET_DAQ_PTR_DAQ;
            uint8_t odt = CRO_SET_DAQ_PTR_ODT;
            uint8_t idx = CRO_SET_DAQ_PTR_IDX;
            check_error(XcpSetDaqPtr(daq,odt,idx));
          }
          break;

        case CC_WRITE_DAQ:
          {
            check_len(CRO_WRITE_DAQ_LEN);
            check_error(XcpAddOdtEntry(CRO_WRITE_DAQ_ADDR, CRO_WRITE_DAQ_EXT, CRO_WRITE_DAQ_SIZE));
          }
          break;

        case CC_WRITE_DAQ_MULTIPLE: 
          {
            check_len(CRO_WRITE_DAQ_MULTIPLE_LEN(1));
            uint8_t n = CRO_WRITE_DAQ_MULTIPLE_NODAQ;
            check_len(CRO_WRITE_DAQ_MULTIPLE_LEN(n));
            for (int i = 0; i < n; i++) {
                  check_error(XcpAddOdtEntry(CRO_WRITE_DAQ_MULTIPLE_ADDR(i), CRO_WRITE_DAQ_MULTIPLE_EXT(i), CRO_WRITE_DAQ_MULTIPLE_SIZE(i)));
              }
          }
          break;

        case CC_START_STOP_DAQ_LIST:
          {
            check_len(CRO_START_STOP_DAQ_LIST_LEN);
            uint16_t daq = CRO_START_STOP_DAQ_LIST_DAQ;
            if (daq >= gXcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
            if ( (CRO_START_STOP_DAQ_LIST_MODE==1 ) || (CRO_START_STOP_DAQ_LIST_MODE==2) )  { // start or select
              DaqListState(daq) |= DAQ_STATE_SELECTED;
              if (CRO_START_STOP_DAQ_LIST_MODE == 1) { // start individual daq list
                  XcpStartDaq(daq);
              }
              gXcp.CrmLen = CRM_START_STOP_DAQ_LIST_LEN;
              CRM_START_STOP_DAQ_LIST_FIRST_PID = 0; // Absolute DAQ, Relative ODT - DaqListFirstPid(daq);
            }
            else {
              if (XcpStopDaq(daq)) {
                  XcpTlWaitForTransmitQueueEmpty(); // Event processing stopped - wait until transmit queue empty before sending command response
              }
            }

          }
          break;

        case CC_START_STOP_SYNCH:
          {
            if ((0 == gXcp.Daq.DaqCount) || (0 == gXcp.Daq.OdtCount) || (0 == gXcp.Daq.OdtEntryCount)) error(CRC_DAQ_CONFIG);
            check_len(CRO_START_STOP_SYNCH_LEN);
            switch (CRO_START_STOP_SYNCH_MODE) {
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
            case 3: /* prepare for start selected */
                if (!ApplXcpPrepareDaq()) error(CRC_RESOURCE_TEMPORARY_NOT_ACCESSIBLE);
                break;
#endif
            case 2: /* stop selected */
                XcpStopAllSelectedDaq();
                break;
            case 1: /* start selected */
                if (!ApplXcpStartDaq()) error(CRC_RESOURCE_TEMPORARY_NOT_ACCESSIBLE);
                XcpSendResponse(); // Transmit response and then start DAQ
                XcpStartAllSelectedDaq();
                return;
            case 0: /* stop all */
                ApplXcpStopDaq();
                XcpStopAllDaq();
                XcpTlWaitForTransmitQueueEmpty(); // Wait until transmit queue empty before sending command response
                break;
            default:
                error(CRC_OUT_OF_RANGE);
            }
              
          }
          break;

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103 && XCP_TRANSPORT_LAYER_TYPE==XCP_TRANSPORT_LAYER_ETH
        case CC_TIME_CORRELATION_PROPERTIES:
          {
            check_len(CRO_TIME_SYNCH_PROPERTIES_LEN);
            gXcp.CrmLen = CRM_TIME_SYNCH_PROPERTIES_LEN;
            if ((CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES & TIME_SYNCH_SET_PROPERTIES_RESPONSE_FMT) >= 1) { // set extended format
              DBG_PRINTF2("  Timesync extended mode activated (RESPONSE_FMT=%u)\n", CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES & TIME_SYNCH_SET_PROPERTIES_RESPONSE_FMT);
              gXcp.SessionStatus &= (uint16_t)~SS_LEGACY_MODE;
            }
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
            if (CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES & TIME_SYNCH_SET_PROPERTIES_CLUSTER_ID) { // set cluster id
              DBG_PRINTF2("  Cluster id set to %u\n", CRO_TIME_SYNCH_PROPERTIES_CLUSTER_ID);
              gXcp.ClusterId = CRO_TIME_SYNCH_PROPERTIES_CLUSTER_ID; // Set cluster id
              XcpEthTlSetClusterId(gXcp.ClusterId);
            }
            CRM_TIME_SYNCH_PROPERTIES_CLUSTER_ID = gXcp.ClusterId;
#else
            if (CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES & TIME_SYNCH_SET_PROPERTIES_CLUSTER_ID) { // set cluster id
                //error(CRC_OUT_OF_RANGE); // CANape insists on setting a cluster id, even if Multicast is not enabled
                DBG_PRINTF2("  Cluster id = %u setting ignored\n", CRO_TIME_SYNCH_PROPERTIES_CLUSTER_ID);
            }
            CRM_TIME_SYNCH_PROPERTIES_CLUSTER_ID = 0;
#endif
            if (CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES & TIME_SYNCH_SET_PROPERTIES_TIME_SYNCH_BRIDGE) error(CRC_OUT_OF_RANGE); // set time sync bride is not supported -> error
            CRM_TIME_SYNCH_PROPERTIES_SERVER_CONFIG = SERVER_CONFIG_RESPONSE_FMT_ADVANCED | SERVER_CONFIG_DAQ_TS_SERVER | SERVER_CONFIG_TIME_SYNCH_BRIDGE_NONE;  // SERVER_CONFIG_RESPONSE_FMT_LEGACY
            CRM_TIME_SYNCH_PROPERTIES_RESERVED = 0x0;
#ifndef XCP_ENABLE_PTP
            CRM_TIME_SYNCH_PROPERTIES_OBSERVABLE_CLOCKS = LOCAL_CLOCK_FREE_RUNNING | GRANDM_CLOCK_NONE | ECU_CLOCK_NONE;
            CRM_TIME_SYNCH_PROPERTIES_SYNCH_STATE = LOCAL_CLOCK_STATE_FREE_RUNNING;
            CRM_TIME_SYNCH_PROPERTIES_CLOCK_INFO = CLOCK_INFO_SERVER;
#else // XCP_ENABLE_PTP
            if (ApplXcpGetClockInfoGrandmaster(gXcp.ClockInfo.grandmaster.UUID, &gXcp.ClockInfo.grandmaster.epochOfGrandmaster, &gXcp.ClockInfo.grandmaster.stratumLevel)) { // Update UUID and clock details
                CRM_TIME_SYNCH_PROPERTIES_OBSERVABLE_CLOCKS = LOCAL_CLOCK_SYNCHED | GRANDM_CLOCK_READABLE | ECU_CLOCK_NONE;
                DBG_PRINTF1("  GrandmasterClock: UUID=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X stratumLevel=%u, epoch=%u\n", gXcp.ClockInfo.grandmaster.UUID[0], gXcp.ClockInfo.grandmaster.UUID[1], gXcp.ClockInfo.grandmaster.UUID[2], gXcp.ClockInfo.grandmaster.UUID[3], gXcp.ClockInfo.grandmaster.UUID[4], gXcp.ClockInfo.grandmaster.UUID[5], gXcp.ClockInfo.grandmaster.UUID[6], gXcp.ClockInfo.grandmaster.UUID[7], gXcp.ClockInfo.grandmaster.stratumLevel, gXcp.ClockInfo.grandmaster.epochOfGrandmaster);
                CRM_TIME_SYNCH_PROPERTIES_SYNCH_STATE = ApplXcpGetClockState();
                DBG_PRINTF1("  SyncState: %u\n", CRM_TIME_SYNCH_PROPERTIES_SYNCH_STATE);
                CRM_TIME_SYNCH_PROPERTIES_CLOCK_INFO = CLOCK_INFO_SERVER | CLOCK_INFO_GRANDM | CLOCK_INFO_RELATION;
            }
            else {
                CRM_TIME_SYNCH_PROPERTIES_OBSERVABLE_CLOCKS = LOCAL_CLOCK_FREE_RUNNING | GRANDM_CLOCK_NONE | ECU_CLOCK_NONE;
                CRM_TIME_SYNCH_PROPERTIES_SYNCH_STATE = LOCAL_CLOCK_STATE_FREE_RUNNING;
                CRM_TIME_SYNCH_PROPERTIES_CLOCK_INFO = CLOCK_INFO_SERVER;
            }
#endif // XCP_ENABLE_PTP
            if (CRO_TIME_SYNCH_PROPERTIES_GET_PROPERTIES_REQUEST & TIME_SYNCH_GET_PROPERTIES_GET_CLK_INFO) { // check whether MTA based upload is requested
                gXcp.MtaPtr = (uint8_t*)&gXcp.ClockInfo.server;
                gXcp.MtaExt = 0;
            }
          }
          break;
#endif // >= 0x0103

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
        case CC_TRANSPORT_LAYER_CMD:
          switch (CRO_TL_SUBCOMMAND) {

              #ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
              case CC_TL_GET_DAQ_CLOCK_MULTICAST:
              {
                  check_len(CRO_GET_DAQ_CLOC_MCAST_LEN);
                  uint16_t clusterId = CRO_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER;
                  if (gXcp.ClusterId != clusterId) error(CRC_OUT_OF_RANGE);
                  CRM_CMD = PID_EV;
                  CRM_EVENTCODE = EVC_TIME_SYNCH;
                  CRM_GET_DAQ_CLOCK_MCAST_TRIGGER_INFO = 0x18 + 0x02; // TIME_OF_SAMPLING (Bitmask 0x18, 3 - Sampled on reception) + TRIGGER_INITIATOR ( Bitmask 0x07, 2 - GET_DAQ_CLOCK_MULTICAST)
                  if (!isLegacyMode()) { // Extended format
                      #ifdef XCP_DAQ_CLOCK_64BIT
                          gXcp.CrmLen = CRM_GET_DAQ_CLOCK_MCAST_LEN + 8;
                          CRM_GET_DAQ_CLOCK_MCAST_PAYLOAD_FMT = DAQ_CLOCK_PAYLOAD_FMT_ID| DAQ_CLOCK_PAYLOAD_FMT_SLV_64; // size of timestamp is DLONG + CLUSTER_ID
                          CRM_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER64 = CRO_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER;
                          CRM_GET_DAQ_CLOCK_MCAST_COUNTER64 = CRO_GET_DAQ_CLOCK_MCAST_COUNTER;
                          uint64_t clock = ApplXcpGetClock64();
                          CRM_GET_DAQ_CLOCK_MCAST_TIME64_LOW = (uint32_t)(clock);
                          CRM_GET_DAQ_CLOCK_MCAST_TIME64_HIGH = (uint32_t)(clock >> 32);
                          CRM_GET_DAQ_CLOCK_MCAST_SYNCH_STATE64 = ApplXcpGetClockState();
                      #else
                          gXcp.CrmLen = CRM_GET_DAQ_CLOCK_MCAST_LEN + 4;
                          CRM_GET_DAQ_CLOCK_MCAST_PAYLOAD_FMT = DAQ_CLOCK_PAYLOAD_FMT_ID | DAQ_CLOCK_PAYLOAD_FMT_SLV_32; // size of timestamp is DWORD + CLUSTER_ID
                          CRM_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER = CRO_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER;
                          CRM_GET_DAQ_CLOCK_MCAST_COUNTER = CRO_GET_DAQ_CLOCK_MCAST_COUNTER;
                          CRM_GET_DAQ_CLOCK_MCAST_TIME = (uint32_t)ApplXcpGetClock64();
                          CRM_GET_DAQ_CLOCK_MCAST_SYNCH_STATE = ApplXcpGetClockState();
                      #endif
                      if (gXcp.CrmLen> XCPTL_MAX_CTO_SIZE) error(CRC_CMD_UNKNOWN); // Extended mode needs enough CTO size 
                  }
                  else
                  { // Legacy format
                      gXcp.CrmLen = CRM_GET_DAQ_CLOCK_MCAST_LEN;
                      CRM_GET_DAQ_CLOCK_MCAST_PAYLOAD_FMT = DAQ_CLOCK_PAYLOAD_FMT_SLV_32; // size of timestamp is DWORD
                      CRM_GET_DAQ_CLOCK_MCAST_TIME = (uint32_t)ApplXcpGetClock64();
                  }
              }
              break;
              #endif // XCP_ENABLE_DAQ_CLOCK_MULTICAST

                  #if XCP_TRANSPORT_LAYER_TYPE==XCP_TRANSPORT_LAYER_ETH
                  case CC_TL_GET_SERVER_ID:
                    goto no_response; // Not supported, no response, response has atypical layout

                  case CC_TL_GET_SERVER_ID_EXTENDED:
                    check_len(CRO_TL_GET_SERVER_ID_LEN);
                    BOOL server_isTCP;
                    uint16_t server_port;
                    uint8_t server_addr[4];
                    uint8_t server_mac[6];
                    uint16_t client_port;
                    uint8_t client_addr[4];
                    client_port = CRO_TL_GET_SERVER_ID_PORT;
                    memcpy(client_addr, &CRO_TL_GET_SERVER_ID_ADDR(0), 4);
                    XcpEthTlGetInfo(&server_isTCP, server_mac, server_addr, &server_port);
                    memcpy(&CRM_TL_GET_SERVER_ID_ADDR(0),server_addr,4);
                    CRM_TL_GET_SERVER_ID_PORT = server_port;
                    CRM_TL_GET_SERVER_ID_STATUS = 
                      (server_isTCP ? GET_SERVER_ID_STATUS_PROTOCOL_TCP : GET_SERVER_ID_STATUS_PROTOCOL_UDP) | // protocol type
                      (isConnected() ? GET_SERVER_ID_STATUS_SLV_AVAILABILITY_BUSY : 0) | // In use
                      0; // TL_SLV_DETECT_STATUS_SLV_ID_EXT_SUPPORTED; // GET_SERVER_ID_EXTENDET supported
                    CRM_TL_GET_SERVER_ID_RESOURCE  = RM_DAQ;                 
                    CRM_TL_GET_SERVER_ID_ID_LEN = (uint8_t)ApplXcpGetId(IDT_ASCII, &CRM_TL_GET_SERVER_ID_ID, CRM_TL_GET_SERVER_ID_MAX_LEN);
                    memcpy((uint8_t*)&CRM_TL_GET_SERVER_ID_MAC(CRM_TL_GET_SERVER_ID_ID_LEN), server_mac, 6);
                    gXcp.CrmLen = (uint8_t)CRM_TL_GET_SERVER_ID_LEN(CRM_TL_GET_SERVER_ID_ID_LEN);
                    XcpSendMulticastResponse(client_addr,client_port); // Transmit multicast command response
                    goto no_response;
                    #endif

                  case 0:
                  default: /* unknown transport layer command */
                    error(CRC_CMD_UNKNOWN);

              }
          break;
#endif // >= 0x0103

          case CC_GET_DAQ_CLOCK:
          {
              #if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
              CRM_GET_DAQ_CLOCK_RES1 = 0x00; // Placeholder for event code
              CRM_GET_DAQ_CLOCK_TRIGGER_INFO = 0x18; // TIME_OF_SAMPLING (Bitmask 0x18, 3 - Sampled on reception)
              if (!isLegacyMode()) { // Extended format
                #ifdef XCP_DAQ_CLOCK_64BIT
                   gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN + 5;
                   CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = DAQ_CLOCK_PAYLOAD_FMT_SLV_64;// FMT_XCP_SLV = size of timestamp is DLONG
                   uint64_t clock = ApplXcpGetClock64();
                   CRM_GET_DAQ_CLOCK_TIME64_LOW =  (uint32_t)(clock);
                   CRM_GET_DAQ_CLOCK_TIME64_HIGH = (uint32_t)(clock >> 32);
                   CRM_GET_DAQ_CLOCK_SYNCH_STATE64 = ApplXcpGetClockState();
                #else
                   gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN + 1;
                   CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = DAQ_CLOCK_PAYLOAD_FMT_SLV_32; // FMT_XCP_SLV = size of timestamp is DWORD
                   CRM_GET_DAQ_CLOCK_TIME = (uint32_t)ApplXcpGetClock64();
                   CRM_GET_DAQ_CLOCK_SYNCH_STATE = ApplXcpGetClockState();
                #endif
                if (gXcp.CrmLen > XCPTL_MAX_CTO_SIZE) error(CRC_CMD_UNKNOWN); // Extended mode needs enough CTO size               
              }
              else
              #endif // >= 0x0103
              { // Legacy format
                  CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = DAQ_CLOCK_PAYLOAD_FMT_SLV_32; // FMT_XCP_SLV = size of timestamp is DWORD
                  gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN;
                  CRM_GET_DAQ_CLOCK_TIME = (uint32_t)ApplXcpGetClock64();
              }
          }
          break;

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
          case CC_LEVEL_1_COMMAND:
            switch (CRO_LEVEL_1_COMMAND_CODE) {

              /* Major and minor versions */
              case CC_GET_VERSION:
                gXcp.CrmLen = CRM_GET_VERSION_LEN;
                CRM_GET_VERSION_RESERVED = 0;
                CRM_GET_VERSION_PROTOCOL_VERSION_MAJOR = (uint8_t)((uint16_t)XCP_PROTOCOL_LAYER_VERSION >> 8);
                CRM_GET_VERSION_PROTOCOL_VERSION_MINOR = (uint8_t)(XCP_PROTOCOL_LAYER_VERSION & 0xFF);
                CRM_GET_VERSION_TRANSPORT_VERSION_MAJOR = (uint8_t)((uint16_t)XCP_TRANSPORT_LAYER_VERSION >> 8);
                CRM_GET_VERSION_TRANSPORT_VERSION_MINOR = (uint8_t)(XCP_TRANSPORT_LAYER_VERSION & 0xFF);
                break;

  #ifdef XCP_ENABLE_PACKED_MODE
              case CC_SET_DAQ_LIST_PACKED_MODE:
              {
                  uint16_t daq = CRO_SET_DAQ_LIST_PACKED_MODE_DAQ;
                  if (daq >= gXcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
                  if (CRO_SET_DAQ_LIST_PACKED_MODE_MODE!=0x01) error(CRC_DAQ_CONFIG); // only element grouped implemented
                  if (CRO_SET_DAQ_LIST_PACKED_MODE_TIMEMODE != DPM_TIMESTAMP_MODE_LAST) error(CRC_DAQ_CONFIG); // only late timestamp supported
                  DaqListSampleCount(daq) = CRO_SET_DAQ_LIST_PACKED_MODE_SAMPLECOUNT;
              }
              break;
  #endif

              default: /* unknown command */
                  error(CRC_CMD_UNKNOWN);
              }
            break;
#endif // >= 0x0104

          default: /* unknown command */
              {
                  error(CRC_CMD_UNKNOWN)
              }

      } // switch()
  }

  // Transmit command response
  XcpSendResponse();
  return;

  // Transmit error response
 negative_response:
  gXcp.CrmLen = 2;
  CRM_CMD = PID_ERR;
  CRM_ERR = err;
  XcpSendResponse();
  return;

  // Return with no responce in case of async commands
#if XCP_TRANSPORT_LAYER_TYPE==XCP_TRANSPORT_LAYER_ETH
  no_response:
    return;
#endif
}


/*****************************************************************************
| Event
******************************************************************************/
void XcpSendEvent(uint8_t evc, const uint8_t* d, uint8_t l)
{
    if (!isConnected()) return;

    uint8_t i;
    if (isConnected()) {
        CRM_BYTE(0) = PID_EV; /* Event*/
        CRM_BYTE(1) = evc;  /* Event Code*/
        gXcp.CrmLen = 2;
        for (i = 0; i < l; i++) CRM_BYTE(gXcp.CrmLen++) = d[i++];
        XcpSendResponse();
    }
}


/*****************************************************************************
| Initialization of the XCP Protocol Layer
******************************************************************************/

void XcpInit( void )
{
  if (gXcp.SessionStatus != SS_INITIALIZED) {

    assert(gXcp.SessionStatus == 0);

    /* Initialize all XCP variables to zero */
    memset((uint8_t*)&gXcp, 0, sizeof(gXcp));

#if XCP_TRANSPORT_LAYER_TYPE==XCP_TRANSPORT_LAYER_ETH
  #if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
    #ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
      gXcp.ClusterId = XCP_MULTICAST_CLUSTER_ID;  // XCP default cluster id (multicast addr 239,255,0,1, group 127,0,1 (mac 01-00-5E-7F-00-01)
      XcpEthTlSetClusterId(gXcp.ClusterId);
    #endif
  #endif
#endif

    /* Initialize the session status */
    gXcp.SessionStatus = 0;

#ifdef DBG_LEVEL
    DBG_PRINT1("\nInit XCP protocol layer\n");
    #ifndef XCP_MAX_EVENT
      #define XCP_MAX_EVENT 0
    #endif
    DBG_PRINTF1("  Version=%u.%u, MAXEV=%u, MAXCTO=%u, MAXDTO=%u, DAQMEM=%u, MAXDAQ=%u, MAXENTRY=%u, MAXENTRYSIZE=%u\n", XCP_PROTOCOL_LAYER_VERSION >> 8, XCP_PROTOCOL_LAYER_VERSION & 0xFF, XCP_MAX_EVENT, XCPTL_MAX_CTO_SIZE, XCPTL_MAX_DTO_SIZE, XCP_DAQ_MEM_SIZE, (1 << sizeof(uint16_t) * 8) - 1, (1 << sizeof(uint16_t) * 8) - 1, (1 << (sizeof(uint8_t) * 8)) - 1);
    DBG_PRINTF1("  %u KiB memory used\n", (unsigned int)sizeof(gXcp) / 1024);
    DBG_PRINT1("  Options=(");

    // Print activated XCP protocol options
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST // Enable GET_DAQ_CLOCK_MULTICAST
    DBG_PRINT1("DAQ_CLK_MULTICAST,");
#endif
#ifdef XCP_DAQ_CLOCK_64BIT  // Use 64 Bit time stamps
    DBG_PRINT1("DAQ_CLK_64BIT,");
#endif
#ifdef XCP_ENABLE_PTP // Enable server clock synchronized to PTP grandmaster clock
    DBG_PRINT1("GM_CLK_INFO,");
#endif
#ifdef XCP_ENABLE_PACKED_MODE  // Enable packed DAQ events
    DBG_PRINT1("PACKED_MODE,");
#endif
#ifdef XCP_ENABLE_IDT_A2L_UPLOAD // Enable A2L upload to host
    DBG_PRINT1("A2L_UPLOAD,");
#endif
#ifdef XCP_ENABLE_IDT_A2L_HTTP_GET // Enable A2L upload to host
    DBG_PRINT1("A2L_URL,");
#endif
#ifdef XCP_ENABLE_DAQ_EVENT_LIST // Enable XCP event info by protocol or by A2L
    DBG_PRINT1("DAQ_EVT_LIST,");
#endif
#ifdef XCP_ENABLE_DAQ_EVENT_INFO // Enable XCP event info by protocol instead of A2L
    DBG_PRINT1("DAQ_EVT_INFO,");
#endif
#ifdef XCP_ENABLE_CHECKSUM // Enable BUILD_CHECKSUM command
    DBG_PRINT1("CHECKSUM,");
#endif
#ifdef XCP_ENABLE_INTERLEAVED // Enable interleaved command execution
    DBG_PRINT1("INTERLEAVED,");
#endif
    DBG_PRINT1(")\n\n");
#endif

    gXcp.SessionStatus |= SS_INITIALIZED;
  }
}

void XcpStart(void)
{
    if (!isInitialized()) return;

#if XCP_TRANSPORT_LAYER_TYPE==XCP_TRANSPORT_LAYER_ETH
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103

    // XCP server clock default description
    gXcp.ClockInfo.server.timestampTicks = XCP_TIMESTAMP_TICKS;
    gXcp.ClockInfo.server.timestampUnit = XCP_TIMESTAMP_UNIT;
    gXcp.ClockInfo.server.stratumLevel = XCP_STRATUM_LEVEL_UNKNOWN;
#ifdef XCP_DAQ_CLOCK_64BIT
    gXcp.ClockInfo.server.nativeTimestampSize = 8; // NATIVE_TIMESTAMP_SIZE_DLONG;
    gXcp.ClockInfo.server.valueBeforeWrapAround = 0xFFFFFFFFFFFFFFFFULL;
#else
    gXcp.ClockInfo.server.nativeTimestampSize = 4; // NATIVE_TIMESTAMP_SIZE_LONG;
    gXcp.ClockInfo.server.valueBeforeWrapAround = 0xFFFFFFFFULL;
#endif   
#endif // XCP_PROTOCOL_LAYER_VERSION >= 0x0103
#ifdef XCP_ENABLE_PTP

    uint8_t uuid[8] = XCP_DAQ_CLOCK_UIID;
    memcpy(gXcp.ClockInfo.server.UUID, uuid, 8);

    DBG_PRINTF3("  ServerClock: ticks=%u, unit=%s, size=%u, UUID=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n\n", gXcp.ClockInfo.server.timestampTicks, (gXcp.ClockInfo.server.timestampUnit == DAQ_TIMESTAMP_UNIT_1NS) ? "ns" : "us", gXcp.ClockInfo.server.nativeTimestampSize, gXcp.ClockInfo.server.UUID[0], gXcp.ClockInfo.server.UUID[1], gXcp.ClockInfo.server.UUID[2], gXcp.ClockInfo.server.UUID[3], gXcp.ClockInfo.server.UUID[4], gXcp.ClockInfo.server.UUID[5], gXcp.ClockInfo.server.UUID[6], gXcp.ClockInfo.server.UUID[7]);

    // If the server clock is PTP synchronized, both origin and local timestamps are considered to be the same.
    gXcp.ClockInfo.relation.timestampLocal = 0;
    gXcp.ClockInfo.relation.timestampOrigin = 0;

	  // XCP grandmaster clock default description
    gXcp.ClockInfo.grandmaster.timestampTicks = XCP_TIMESTAMP_TICKS;
	  gXcp.ClockInfo.grandmaster.timestampUnit = XCP_TIMESTAMP_UNIT;
	  gXcp.ClockInfo.grandmaster.nativeTimestampSize = 8; // NATIVE_TIMESTAMP_SIZE_DLONG;
	  gXcp.ClockInfo.grandmaster.valueBeforeWrapAround = 0xFFFFFFFFFFFFFFFFULL;
    gXcp.ClockInfo.grandmaster.stratumLevel = XCP_STRATUM_LEVEL_UNKNOWN;
    gXcp.ClockInfo.grandmaster.epochOfGrandmaster = XCP_EPOCH_ARB;
    if (ApplXcpGetClockInfoGrandmaster(gXcp.ClockInfo.grandmaster.UUID, &gXcp.ClockInfo.grandmaster.epochOfGrandmaster, &gXcp.ClockInfo.grandmaster.stratumLevel)) {
      DBG_PRINTF1("  GrandmasterClock: UUID=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X stratumLevel=%u, epoch=%u\n", gXcp.ClockInfo.grandmaster.UUID[0], gXcp.ClockInfo.grandmaster.UUID[1], gXcp.ClockInfo.grandmaster.UUID[2], gXcp.ClockInfo.grandmaster.UUID[3], gXcp.ClockInfo.grandmaster.UUID[4], gXcp.ClockInfo.grandmaster.UUID[5], gXcp.ClockInfo.grandmaster.UUID[6], gXcp.ClockInfo.grandmaster.UUID[7], gXcp.ClockInfo.grandmaster.stratumLevel, gXcp.ClockInfo.grandmaster.epochOfGrandmaster);
      DBG_PRINT1("  ClockRelation: local=0, origin=0\n");
    }
#endif // PTP
#endif // XCP_TRANSPORT_LAYER_TYPE==XCP_TRANSPORT_LAYER_ETH

    DBG_PRINT3("Start XCP protocol layer\n");

    gXcp.SessionStatus |= SS_STARTED;
}


/**************************************************************************/
// Eventlist
/**************************************************************************/

#ifdef XCP_ENABLE_DAQ_EVENT_LIST

// Get a pointer to and the size of the XCP event list
tXcpEvent* XcpGetEventList(uint16_t* eventCount) {
    if (!isInitialized()) return NULL;
    if (eventCount!=NULL) *eventCount = gXcp.EventCount;
    return gXcp.EventList;
}

void XcpClearEventList() {
    gXcp.EventCount = 0;
}

tXcpEvent* XcpGetEvent(uint16_t event) {
    if (!isStarted() || event >= gXcp.EventCount) return NULL;
    return &gXcp.EventList[event];
}


// Create an XCP event, <rate> in us, 0 = sporadic, <priority> 0-normal, >=1 realtime, <sampleCount> only for packed mode events only, <size> only for extended events
// Returns the XCP event number for XcpEventXxx() or 0xFFFF when out of memory
uint16_t XcpCreateEvent(const char* name, uint32_t cycleTimeNs, uint8_t priority, uint16_t sampleCount, uint32_t size) {

    uint16_t e;
    uint32_t c;

    if (!isInitialized()) {
      DBG_PRINT1("ERROR: XCP driver not initialized\n");
      return (uint16_t)0xFFFF; // Uninitialized or out of memory
    }
    if (gXcp.EventCount >= XCP_MAX_EVENT) {
      DBG_PRINT1("ERROR: XCP too many events\n");
      return (uint16_t)0xFFFF; // Uninitialized or out of memory
    }

    // Convert cycle time to ASAM coding time cycle and time unit
    // RESOLUTION OF TIMESTAMP "UNIT_1NS" = 0, "UNIT_10NS" = 1, ...
    e = gXcp.EventCount;
    c = cycleTimeNs;
    gXcp.EventList[e].timeUnit = 0;
    while (c >= 256) {
        c /= 10;
        gXcp.EventList[e].timeUnit++;
    }
    gXcp.EventList[e].timeCycle = (uint8_t)c;

    strncpy(gXcp.EventList[e].shortName,name,XCP_MAX_EVENT_NAME);
    gXcp.EventList[e].shortName[XCP_MAX_EVENT_NAME] = 0;
    gXcp.EventList[e].priority = priority;
    gXcp.EventList[e].sampleCount = sampleCount;
    gXcp.EventList[e].size = size;
#ifdef XCP_ENABLE_TEST_CHECKS
    gXcp.EventList[e].time = 0;
#endif
#ifdef XCP_ENABLE_MULTITHREAD_EVENTS
    mutexInit(&gXcp.EventList[e].mutex, 0, 1000);
#endif
#ifdef DBG_LEVEL
     uint64_t ns = (uint64_t)(gXcp.EventList[e].timeCycle * pow(10, gXcp.EventList[e].timeUnit));
     DBG_PRINTF1("  Event %u: %s cycle=%" PRIu64 "ns, prio=%u, sc=%u, size=%u\n", e, gXcp.EventList[e].shortName, ns, gXcp.EventList[e].priority, gXcp.EventList[e].sampleCount, gXcp.EventList[e].size);
     if (cycleTimeNs != ns) DBG_PRINTF1("Warning: cycle time %uns, loss of significant digits!\n", cycleTimeNs);
#endif

    return gXcp.EventCount++;
}

#endif


/****************************************************************************/
/* Test printing                                                            */
/****************************************************************************/

#ifdef DBG_LEVEL

static void XcpPrintCmd() {

    switch (CRO_CMD) {

    case CC_SET_MTA:
        printf("SET_MTA addr=%08Xh, addrext=%02Xh\n", CRO_SET_MTA_ADDR, CRO_SET_MTA_EXT);
        break;

    case CC_DOWNLOAD:
        {
            uint16_t i;
            printf("DOWNLOAD size=%u, data=", CRO_DOWNLOAD_SIZE);
            for (i = 0; (i < CRO_DOWNLOAD_SIZE) && (i < CRO_DOWNLOAD_MAX_SIZE); i++) {
                printf("%02X ", CRO_DOWNLOAD_DATA[i]);
            }
            printf("\n");
        }
        break;

    case CC_SHORT_DOWNLOAD:
        if (DBG_LEVEL >= 3) {
            uint16_t i;
            printf("SHORT_DOWNLOAD addr=%08Xh, addrext=%02Xh, size=%u, data=", CRO_SHORT_DOWNLOAD_ADDR, CRO_SHORT_DOWNLOAD_EXT, CRO_SHORT_DOWNLOAD_SIZE);
            for (i = 0; (i < CRO_SHORT_DOWNLOAD_SIZE) && (i < CRO_SHORT_DOWNLOAD_MAX_SIZE); i++) {
                printf("%02X ", CRO_SHORT_DOWNLOAD_DATA[i]);
            }
            printf("\n");
        }
        break;

    case CC_UPLOAD:
        if (DBG_LEVEL >= 3) {
            printf("UPLOAD size=%u\n", CRO_UPLOAD_SIZE);
        }
        break;

    case CC_SHORT_UPLOAD:
        if (DBG_LEVEL >= 3 || !isDaqRunning()) { // Polling DAQ 
            printf("SHORT_UPLOAD addr=%08Xh, addrext=%02Xh, size=%u\n", CRO_SHORT_UPLOAD_ADDR, CRO_SHORT_UPLOAD_EXT, CRO_SHORT_UPLOAD_SIZE);
        }
        break;

#ifdef XCP_ENABLE_CAL_PAGE
    case CC_SET_CAL_PAGE:
        printf("SET_CAL_PAGE segment=%u,page =%u,mode=%02Xh\n", CRO_SET_CAL_PAGE_SEGMENT, CRO_SET_CAL_PAGE_PAGE, CRO_SET_CAL_PAGE_MODE);
        break;

    case CC_GET_CAL_PAGE:
        printf("GET_CAL_PAGE segment=%u, mode=%u\n", CRO_GET_CAL_PAGE_SEGMENT, CRO_GET_CAL_PAGE_MODE);
        break;
#endif

#ifdef XCP_ENABLE_CHECKSUM
    case CC_BUILD_CHECKSUM: /* Build Checksum */
        printf("BUILD_CHECKSUM size=%u\n", CRO_BUILD_CHECKSUM_SIZE);
        break;
#endif

    case CC_SYNCH:
            printf("SYNCH\n");
            break;

    case CC_GET_COMM_MODE_INFO:
            printf("GET_COMM_MODE_INFO\n");
            break;

    case CC_DISCONNECT:
            printf("DISCONNECT\n");
            break;

    case CC_GET_ID:
            printf("GET_ID type=%u\n", CRO_GET_ID_TYPE);
            break;

    case CC_GET_STATUS:
            printf("GET_STATUS\n");
            break;

     case CC_GET_DAQ_PROCESSOR_INFO:
            printf("GET_DAQ_PROCESSOR_INFO\n");
            break;

     case CC_GET_DAQ_RESOLUTION_INFO:
            printf("GET_DAQ_RESOLUTION_INFO\n");
            break;

     case CC_GET_DAQ_EVENT_INFO:
            printf("GET_DAQ_EVENT_INFO event=%u\n", CRO_GET_DAQ_EVENT_INFO_EVENT);
            break;

     case CC_FREE_DAQ:
            printf("FREE_DAQ\n");
            break;

     case CC_ALLOC_DAQ:
            printf("ALLOC_DAQ count=%u\n", CRO_ALLOC_DAQ_COUNT);
            break;

     case CC_ALLOC_ODT:
            printf("ALLOC_ODT daq=%u, count=%u\n", CRO_ALLOC_ODT_DAQ, CRO_ALLOC_ODT_COUNT);
            break;

     case CC_ALLOC_ODT_ENTRY:
            if (DBG_LEVEL >= 3) {
              printf("ALLOC_ODT_ENTRY daq=%u, odt=%u, count=%u\n", CRO_ALLOC_ODT_ENTRY_DAQ, CRO_ALLOC_ODT_ENTRY_ODT, CRO_ALLOC_ODT_ENTRY_COUNT);
            }
            break;

     case CC_GET_DAQ_LIST_MODE:
            printf("GET_DAQ_LIST_MODE daq=%u\n",CRO_GET_DAQ_LIST_MODE_DAQ );
            break;

     case CC_SET_DAQ_LIST_MODE:
            printf("SET_DAQ_LIST_MODE daq=%u, mode=%02Xh, eventchannel=%u\n",CRO_SET_DAQ_LIST_MODE_DAQ, CRO_SET_DAQ_LIST_MODE_MODE, CRO_SET_DAQ_LIST_MODE_EVENTCHANNEL);
            break;

     case CC_SET_DAQ_PTR:
            if (DBG_LEVEL >= 3) {
                printf("SET_DAQ_PTR daq=%u,odt=%u,idx=%u\n", CRO_SET_DAQ_PTR_DAQ, CRO_SET_DAQ_PTR_ODT, CRO_SET_DAQ_PTR_IDX);
            }
            break;

     case CC_WRITE_DAQ:
            printf("WRITE_DAQ size=%u,addr=%08Xh,%02Xh\n", CRO_WRITE_DAQ_SIZE, CRO_WRITE_DAQ_ADDR, CRO_WRITE_DAQ_EXT);
            break;

     case CC_WRITE_DAQ_MULTIPLE:
         if (DBG_LEVEL >= 3) {
             printf("WRITE_DAQ_MULTIPLE count=%u\n", CRO_WRITE_DAQ_MULTIPLE_NODAQ);
             for (int i = 0; i < CRO_WRITE_DAQ_MULTIPLE_NODAQ; i++) {
                 printf("   %u: size=%u,addr=%08Xh,%02Xh\n", i, CRO_WRITE_DAQ_MULTIPLE_SIZE(i), CRO_WRITE_DAQ_MULTIPLE_ADDR(i), CRO_WRITE_DAQ_MULTIPLE_EXT(i));
             }
         }
         break;

     case CC_START_STOP_DAQ_LIST:
            printf("START_STOP mode=%s, daq=%u\n", (CRO_START_STOP_DAQ_LIST_MODE == 2)?"select": (CRO_START_STOP_DAQ_LIST_MODE == 1)?"start":"stop", CRO_START_STOP_DAQ_LIST_DAQ);
            break;

     case CC_START_STOP_SYNCH:
            printf("CC_START_STOP_SYNCH mode=%s\n", (CRO_START_STOP_SYNCH_MODE == 3) ? "prepare" : (CRO_START_STOP_SYNCH_MODE == 2) ? "stop_selected" : (CRO_START_STOP_SYNCH_MODE == 1) ? "start_selected" : "stop_all");
            break;

     case CC_GET_DAQ_CLOCK:
            if (DBG_LEVEL >= 3 || !isDaqRunning()) {
              printf("GET_DAQ_CLOCK\n");
            }
            break;

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
     case CC_TIME_CORRELATION_PROPERTIES:
         printf("GET_TIME_CORRELATION_PROPERTIES set=%02Xh, request=%u, clusterId=%u\n", CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES, CRO_TIME_SYNCH_PROPERTIES_GET_PROPERTIES_REQUEST, CRO_TIME_SYNCH_PROPERTIES_CLUSTER_ID );
         break;
#endif

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
     case CC_LEVEL_1_COMMAND:
         switch (CRO_LEVEL_1_COMMAND_CODE) {
           case CC_GET_VERSION:
               printf("GET_VERSION\n");
               break;
#ifdef XCP_ENABLE_PACKED_MODE
           case CC_GET_DAQ_LIST_PACKED_MODE:
               printf("GET_DAQ_LIST_PACKED_MODE daq=%u\n", CRO_GET_DAQ_LIST_PACKED_MODE_DAQ);
               break;
           case CC_SET_DAQ_LIST_PACKED_MODE:
               printf("SET_DAQ_LIST_PACKED_MODE daq=%u, sampleCount=%u\n", CRO_SET_DAQ_LIST_PACKED_MODE_DAQ,CRO_SET_DAQ_LIST_PACKED_MODE_SAMPLECOUNT);
               break;
#endif
           default:
               printf("UNKNOWN LEVEL 1 COMMAND %02X\n", CRO_LEVEL_1_COMMAND_CODE);
               break;
         }
         break;
#endif

     case CC_TRANSPORT_LAYER_CMD:
         switch (CRO_TL_SUBCOMMAND) {
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103             
           case CC_TL_GET_DAQ_CLOCK_MULTICAST:
               if (DBG_LEVEL >= 3 || !isDaqRunning()) {
                   printf("GET_DAQ_CLOCK_MULTICAST counter=%u, cluster=%u\n", CRO_GET_DAQ_CLOCK_MCAST_COUNTER, CRO_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER);
               }
             break;
           case CC_TL_GET_SERVER_ID_EXTENDED:
           case CC_TL_GET_SERVER_ID:
             printf("GET_SERVER_ID %u:%u:%u:%u:%u\n", CRO_TL_GET_SERVER_ID_ADDR(0), CRO_TL_GET_SERVER_ID_ADDR(1), CRO_TL_GET_SERVER_ID_ADDR(2), CRO_TL_GET_SERVER_ID_ADDR(3), CRO_TL_GET_SERVER_ID_PORT );
             break;
#endif
         }
         break;

    } /* switch */
}


static void XcpPrintRes() {
    
    if (CRM_CMD == PID_EV && CRM_EVENTCODE != EVC_TIME_SYNCH) {
        printf("<- EVENT: %02Xh\n", CRM_BYTE(1));
    }
    else if (CRM_CMD == PID_ERR) {
                const char* e;
        switch (CRM_ERR) {
                case  CRC_CMD_SYNCH: e = "CRC_CMD_SYNCH"; break;
                case  CRC_CMD_BUSY: e = "CRC_CMD_BUSY"; break;
                case  CRC_DAQ_ACTIVE: e = "CRC_DAQ_ACTIVE"; break;
                case  CRC_PRM_ACTIVE: e = "CRC_PRM_ACTIVE"; break;
                case  CRC_CMD_UNKNOWN: e = "CRC_CMD_UNKNOWN"; break;
                case  CRC_CMD_SYNTAX: e = "CRC_CMD_SYNTAX"; break;
                case  CRC_OUT_OF_RANGE: e = "CRC_OUT_OF_RANGE"; break;
                case  CRC_WRITE_PROTECTED: e = "CRC_WRITE_PROTECTED"; break;
                case  CRC_ACCESS_DENIED: e = "CRC_ACCESS_DENIED"; break;
                case  CRC_ACCESS_LOCKED: e = "CRC_ACCESS_LOCKED"; break;
                case  CRC_PAGE_NOT_VALID: e = "CRC_PAGE_NOT_VALID"; break;
                case  CRC_PAGE_MODE_NOT_VALID: e = "CRC_PAGE_MODE_NOT_VALID"; break;
                case  CRC_SEGMENT_NOT_VALID: e = "CRC_SEGMENT_NOT_VALID"; break;
                case  CRC_SEQUENCE: e = "CRC_SEQUENCE"; break;
                case  CRC_DAQ_CONFIG: e = "CRC_DAQ_CONFIG"; break;
                case  CRC_MEMORY_OVERFLOW: e = "CRC_MEMORY_OVERFLOW"; break;
                case  CRC_GENERIC: e = "CRC_GENERIC"; break;
                case  CRC_VERIFY: e = "CRC_VERIFY"; break;
                case  CRC_RESOURCE_TEMPORARY_NOT_ACCESSIBLE: e = "CRC_RESOURCE_TEMPORARY_NOT_ACCESSIBLE"; break;
                case  CRC_SUBCMD_UNKNOWN: e = "CRC_SUBCMD_UNKNOWN"; break;
                case  CRC_TIMECORR_STATE_CHANGE: e = "CRC_TIMECORR_STATE_CHANGE"; break;
                default: e = "Unknown errorcode";
        }
        printf("<- ERROR: %02Xh - %s\n", CRM_ERR, e );
    }
    else {
        switch (CRO_CMD) {

        case CC_CONNECT:
            printf("<- version=%02Xh/%02Xh, maxcro=%u, maxdto=%u, resource=%02X, mode=%u\n",
                CRM_CONNECT_PROTOCOL_VERSION, CRM_CONNECT_TRANSPORT_VERSION, CRM_CONNECT_MAX_CTO_SIZE, CRM_CONNECT_MAX_DTO_SIZE, CRM_CONNECT_RESOURCE,  CRM_CONNECT_COMM_BASIC);
            break;

        case CC_GET_COMM_MODE_INFO:
            printf("<- version=%02Xh, opt=%u, queue=%u, max_bs=%u, min_st=%u\n",
                CRM_GET_COMM_MODE_INFO_DRIVER_VERSION, CRM_GET_COMM_MODE_INFO_COMM_OPTIONAL, CRM_GET_COMM_MODE_INFO_QUEUE_SIZE, CRM_GET_COMM_MODE_INFO_MAX_BS, CRM_GET_COMM_MODE_INFO_MIN_ST);
            break;

        case CC_GET_STATUS:
            printf("<- sessionstatus=%02Xh, protectionstatus=%02Xh\n", CRM_GET_STATUS_STATUS, CRM_GET_STATUS_PROTECTION);
            break;

        case CC_GET_ID:
            printf("<- mode=%u,len=%u\n", CRM_GET_ID_MODE, CRM_GET_ID_LENGTH);
            break;

        case CC_UPLOAD:
            if (DBG_LEVEL >= 4) {
                printf("<- data=");
                for (int i = 0; i < CRO_UPLOAD_SIZE; i++) {
                    printf("%02Xh ", CRM_UPLOAD_DATA[i]);
                }
                printf("\n");
            }
            break;

        case CC_SHORT_UPLOAD:
            if (DBG_LEVEL >= 4) {
                printf("<- data=");
                for (int i = 0; i < (uint16_t)CRO_SHORT_UPLOAD_SIZE; i++) {
                    printf("%02Xh ", CRM_SHORT_UPLOAD_DATA[i]);
                }
                printf("\n");
            }
            break;

#ifdef XCP_ENABLE_CAL_PAGE
        case CC_GET_CAL_PAGE:
            printf("<- page=%u\n", CRM_GET_CAL_PAGE_PAGE);
            break;
#endif

#ifdef XCP_ENABLE_CHECKSUM
        case CC_BUILD_CHECKSUM:
            printf("<- sum=%08Xh\n", CRM_BUILD_CHECKSUM_RESULT);
            break;
#endif

        case CC_GET_DAQ_RESOLUTION_INFO:
            printf("<- mode=%02Xh, , ticks=%02Xh\n", CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE, CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS);
            break;

        case CC_GET_DAQ_PROCESSOR_INFO:
            printf("<- min=%u, max=%u, events=%u, keybyte=%02Xh, properties=%02Xh\n", CRM_GET_DAQ_PROCESSOR_INFO_MIN_DAQ, CRM_GET_DAQ_PROCESSOR_INFO_MAX_DAQ, CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT, CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE, CRM_GET_DAQ_PROCESSOR_INFO_PROPERTIES);
            break;

        case CC_GET_DAQ_EVENT_INFO:
            printf("<- 0xFF properties=%02Xh, unit=%u, cycle=%u\n", CRM_GET_DAQ_EVENT_INFO_PROPERTIES, CRM_GET_DAQ_EVENT_INFO_TIME_UNIT, CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE);
            break;

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
        case CC_GET_DAQ_CLOCK:
            if (DBG_LEVEL >= 3 || !isDaqRunning()) {
                if (isLegacyMode()) {
                    printf("<- L t=0x%" PRIx32 "\n", CRM_GET_DAQ_CLOCK_TIME);
                }
                else {
                    if (CRM_GET_DAQ_CLOCK_PAYLOAD_FMT == DAQ_CLOCK_PAYLOAD_FMT_SLV_32) {
                        printf("<- X t=0x%" PRIx32 " sync=%u\n", CRM_GET_DAQ_CLOCK_TIME, CRM_GET_DAQ_CLOCK_SYNCH_STATE);
                    }
                    else {
                        char ts[64];
                        uint64_t t = (((uint64_t)CRM_GET_DAQ_CLOCK_TIME64_HIGH) << 32) | CRM_GET_DAQ_CLOCK_TIME64_LOW;
                        clockGetString(ts, sizeof(ts), t);
                        printf("<- X t=%" PRIu64 " (%s), sync=%u\n", t&0xFFFFFFFF, ts, CRM_GET_DAQ_CLOCK_SYNCH_STATE64);
                    }
                }
            }
            break;

        case CC_TIME_CORRELATION_PROPERTIES:
            printf("<- config=%02Xh, clocks=%02Xh, state=%02Xh, info=%02Xh, clusterId=%u\n",
                CRM_TIME_SYNCH_PROPERTIES_SERVER_CONFIG, CRM_TIME_SYNCH_PROPERTIES_OBSERVABLE_CLOCKS, CRM_TIME_SYNCH_PROPERTIES_SYNCH_STATE, CRM_TIME_SYNCH_PROPERTIES_CLOCK_INFO, CRM_TIME_SYNCH_PROPERTIES_CLUSTER_ID );
            break;
#endif // >= 0x0103

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
        case CC_LEVEL_1_COMMAND:
            switch (CRO_LEVEL_1_COMMAND_CODE) {

            case CC_GET_VERSION:
                printf("<- protocol layer version: major=%02Xh/minor=%02Xh, transport layer version: major=%02Xh/minor=%02Xh\n",
                    CRM_GET_VERSION_PROTOCOL_VERSION_MAJOR,
                    CRM_GET_VERSION_PROTOCOL_VERSION_MINOR,
                    CRM_GET_VERSION_TRANSPORT_VERSION_MAJOR,
                    CRM_GET_VERSION_TRANSPORT_VERSION_MINOR);
                break;

#ifdef XCP_ENABLE_PACKED_MODE
            case CC_GET_DAQ_LIST_PACKED_MODE:
                printf("<- mode = %u\n", CRM_GET_DAQ_LIST_PACKED_MODE_MODE);
                break;
#endif
            }
            break;
#endif

        case CC_TRANSPORT_LAYER_CMD:
            switch (CRO_TL_SUBCOMMAND) {
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
            case CC_TL_GET_DAQ_CLOCK_MULTICAST:
                if (DBG_LEVEL >= 3 || !isDaqRunning()) {
                    if (isLegacyMode()) {
                        printf("<- L t=0x%" PRIx32 "\n", CRM_GET_DAQ_CLOCK_MCAST_TIME);
                    }
                    else {
                        if ((CRM_GET_DAQ_CLOCK_MCAST_PAYLOAD_FMT & ~DAQ_CLOCK_PAYLOAD_FMT_ID) == DAQ_CLOCK_PAYLOAD_FMT_SLV_32) {
                            printf("<- X t=0x%" PRIx32 " sync=%u", CRM_GET_DAQ_CLOCK_MCAST_TIME, CRM_GET_DAQ_CLOCK_MCAST_SYNCH_STATE);
                            if (CRM_GET_DAQ_CLOCK_MCAST_PAYLOAD_FMT & DAQ_CLOCK_PAYLOAD_FMT_ID) printf(" counter=%u, cluster=%u", CRM_GET_DAQ_CLOCK_MCAST_COUNTER, CRM_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER);
                        }
                        else {
                            char ts[64];
                            clockGetString(ts, sizeof(ts), (((uint64_t)CRM_GET_DAQ_CLOCK_MCAST_TIME64_HIGH)<<32)|CRM_GET_DAQ_CLOCK_MCAST_TIME64_LOW);
                            printf("<- X t=%s, sync=%u", ts, CRM_GET_DAQ_CLOCK_MCAST_SYNCH_STATE64);
                            if (CRM_GET_DAQ_CLOCK_MCAST_PAYLOAD_FMT & DAQ_CLOCK_PAYLOAD_FMT_ID) printf(" counter=%u, cluster=%u", CRM_GET_DAQ_CLOCK_MCAST_COUNTER64, CRM_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER64);
                        }
                        printf("\n");
                    }
                }

                break;

            case CC_TL_GET_SERVER_ID:
              printf("<- %u.%u.%u.%u:%u %s\n",
                CRM_TL_GET_SERVER_ID_ADDR(0), CRM_TL_GET_SERVER_ID_ADDR(1), CRM_TL_GET_SERVER_ID_ADDR(2), CRM_TL_GET_SERVER_ID_ADDR(3), CRM_TL_GET_SERVER_ID_PORT, &CRM_TL_GET_SERVER_ID_ID);
              break;
#endif
            }
            break;

        default:
            if (DBG_LEVEL >= 3) {
                printf("<- OK\n");
            }
            break;

        } /* switch */
    }
}


static void XcpPrintDaqList( uint16_t daq )
{
  int i,e;

  if (daq>=gXcp.Daq.DaqCount) return;

  printf("DAQ %u:\n",daq);
  printf(" eventchannel=%04Xh,",DaqListEventChannel(daq));
  printf(" firstOdt=%u,",DaqListFirstOdt(daq));
  printf(" lastOdt=%u,",DaqListLastOdt(daq));
  printf(" mode=%02Xh,", DaqListMode(daq));
  printf(" state=%02Xh,", DaqListState(daq));
#ifdef XCP_ENABLE_PACKED_MODE
  printf(" sampleCount=%u\n",DaqListSampleCount(daq));
#endif
  for (i=DaqListFirstOdt(daq);i<=DaqListLastOdt(daq);i++) {
    printf("  ODT %u (%u):",i-DaqListFirstOdt(daq),i);
    printf(" firstOdtEntry=%u, lastOdtEntry=%u, size=%u:\n", DaqListOdtFirstEntry(i), DaqListOdtLastEntry(i),DaqListOdtSize(i));
    for (e=DaqListOdtFirstEntry(i);e<=DaqListOdtLastEntry(i);e++) {
      printf("   %08X,%u\n",OdtEntryAddr(e), OdtEntrySize(e));
    }
  } /* j */
}


#endif /* DBG_PRINTS_ENABLED */

