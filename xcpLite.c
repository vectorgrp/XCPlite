

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
|   GET_COMM_MODE_INFO GET_ID
|   SET_MTA UPLOAD SHORT_UPLOAD DOWNLOAD SHORT_DOWNLOAD
|   GET_CAL_PAGE SET_CAL_PAGE BUILD_CHECKSUM
|   GET_DAQ_RESOLUTION_INFO GET_DAQ_PROCESSOR_INFO GET_DAQ_EVENT_INFO GET_DAQ_LIST_INFO
|   FREE_DAQ ALLOC_DAQ ALLOC_ODT ALLOC_ODT_ENTRY SET_DAQ_PTR WRITE_DAQ WRITE_DAQ_MULTIPLE
|   GET_DAQ_LIST_MODE SET_DAQ_LIST_MODE START_STOP_SYNCH START_STOP_DAQ_LIST
|   GET_DAQ_CLOCK TIME_CORRELATION_PROPERTIES
|   GET_VERSION
|
|  Limitations:
|     - Testet on 32 bit or 64 bit Linux and Windows platforms
|     - 8 bit and 16 bit CPUs are not supported
|     - No Motorola byte sex
|     - No misra compliance
|     - Overall number of ODTs limited to 64K
|     - Overall number of ODT entries is limited to 64K
|     - Jumbo frame support
|     - Fixed DAQ+ODT 2 byte DTO header
|     - Fixed 32 bit time stamp
|     - Only dynamic DAQ list allocation supported
|     - Resume is not supported
|     - Overload indication by event is not supported
|     - DAQ does not support address extensions and prescaler
|     - DAQ list and event channel prioritization is not supported
|     - ODT optimization not supported
|     - Interleaved communication mode is not supported
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

#include "xcpAppl.h"  /* External dependencies */
#include "xcpLite.h"


/****************************************************************************/
/* Global data                                                               */
/****************************************************************************/

tXcpData gXcp; 



/****************************************************************************/
/* Macros                                                                   */
/****************************************************************************/

#define error(e) { err=(e); goto negative_response; }
#define check_error(e) { err=(e); if (err!=0) { goto negative_response; } }

#define isConnected() (gXcp.SessionStatus & SS_CONNECTED)
#define isDaqRunning() (gXcp.SessionStatus & SS_DAQ)


/****************************************************************************/
/* Test                                                                     */
/****************************************************************************/

#if defined ( XCP_ENABLE_TESTMODE )
static void XcpPrintCmd(const tXcpCto * pCmd);
static void XcpPrintRes(const tXcpCto * pCmd);
static void XcpPrintDaqList(vuint16 daq);
#endif                 


/****************************************************************************/
/* Status                                                                   */
/****************************************************************************/

vuint8 XcpIsConnected() {
    return isConnected();
}

vuint8 XcpIsDaqRunning() {
    return isDaqRunning();
}

vuint16 XcpGetClusterId() {
    return gXcp.ClusterId;
}

vuint8 XcpIsDaqPacked() {
#ifdef XCP_ENABLE_PACKED_MODE
    for (vuint16 daq = 0; daq < gXcp.Daq.DaqCount; daq++) {
        if (DaqListSampleCount(daq) > 1) return 1;
    }
#endif
    return 0;
}


/****************************************************************************/
/* Calibration                                                              */
/****************************************************************************/

// Write n bytes. Copying of size bytes from data to gXcp.Mta
vuint8  XcpWriteMta( vuint8 size, const vuint8* data )
{

  /* Standard RAM memory write access */
  while ( size > 0 )  {
    *gXcp.Mta = *data;
    gXcp.Mta++; 
    data++; 
    size--;
  }
  return XCP_CMD_OK;
  
}

// Read n bytes. Copying of size bytes from data to gXcp.Mta
vuint8  XcpReadMta( vuint8 size, vuint8* data )
{
  /* Standard RAM memory read access */
  while (size > 0)  {
    *data = *gXcp.Mta;
    data++; 
    gXcp.Mta++; 
    size--;
  }
  return XCP_CMD_OK;
}



/****************************************************************************/
/* Data Aquisition Setup                                                    */
/****************************************************************************/

// Free all dynamic DAQ lists
void  XcpFreeDaq( void )
{
  ApplXcpDaqStop();
  gXcp.SessionStatus &= (vuint8)(~SS_DAQ);

  gXcp.Daq.DaqCount = 0;
  gXcp.Daq.OdtCount = 0;
  gXcp.Daq.OdtEntryCount = 0;

  gXcp.pOdt = (tXcpOdt*)0;
  gXcp.pOdtEntryAddr = 0;
  gXcp.pOdtEntrySize = 0;

  memset((vuint8*)&gXcp.Daq.u.b[0], 0, XCP_DAQ_MEM_SIZE);  
}

// Allocate Memory for daq,odt,odtEntries and Queue according to DaqCount, OdtCount and OdtEntryCount
vuint8  XcpAllocMemory( void )
{
  vuint32 s;
  
  /* Check memory overflow */
  s = ( gXcp.Daq.DaqCount * (vuint32)sizeof(tXcpDaqList ) + 
      ( gXcp.Daq.OdtCount * (vuint32)sizeof(tXcpOdt) ) +
      ( gXcp.Daq.OdtEntryCount * ((vuint32)sizeof(vuint8*) + (vuint32)sizeof(vuint8) ) ) );
  
  if (s>=XCP_DAQ_MEM_SIZE) return CRC_MEMORY_OVERFLOW;
  
  gXcp.pOdt = (tXcpOdt*)&gXcp.Daq.u.DaqList[gXcp.Daq.DaqCount];
  gXcp.pOdtEntryAddr = (vuint32*)&gXcp.pOdt[gXcp.Daq.OdtCount];
  gXcp.pOdtEntrySize = (vuint8*)&gXcp.pOdtEntryAddr[gXcp.Daq.OdtEntryCount]; 
  

  #if defined ( XCP_ENABLE_TESTMODE )
    if ( ApplXcpDebugLevel >= 3) ApplXcpPrint("[XcpAllocMemory] %u of %u Bytes used\n",s,XCP_DAQ_MEM_SIZE );
  #endif

  return 0;
}

// Allocate daqCount DAQ lists
vuint8  XcpAllocDaq( vuint16 daqCount )
{
  if ( (gXcp.Daq.OdtCount!=0) || (gXcp.Daq.OdtEntryCount!=0) )  {
    return CRC_SEQUENCE;
  }
  if( daqCount == 0 || daqCount>255)  {
    return CRC_OUT_OF_RANGE;
  }
  
  gXcp.Daq.DaqCount = (vuint8)daqCount;
  return XcpAllocMemory();
}

// Allocate odtCount ODTs in a DAQ list
vuint8 XcpAllocOdt( vuint16 daq, vuint8 odtCount )
{
  vuint32 n;

  if ( (gXcp.Daq.DaqCount==0) || (gXcp.Daq.OdtEntryCount!=0) )  {
    return CRC_SEQUENCE;
  }
  if( odtCount == 0 ) {
    return CRC_OUT_OF_RANGE;
  }
  
  n = (vuint32)gXcp.Daq.OdtCount + (vuint32)odtCount;
  if (n > 0xFFFF) return CRC_OUT_OF_RANGE; // Overall number of ODTs limited to 64K

  gXcp.Daq.u.DaqList[daq].firstOdt = gXcp.Daq.OdtCount;
  gXcp.Daq.OdtCount = (vuint16)n;
  gXcp.Daq.u.DaqList[daq].lastOdt = (vuint16)(gXcp.Daq.OdtCount-1);

  return XcpAllocMemory();
}

// Adjust ODT size by size
void  XcpAdjustOdtSize(vuint16 daq, vuint16 odt, vuint8 size) {
#ifdef XCP_ENABLE_PACKED_MODE
    vuint16 sc = DaqListSampleCount(daq);
    if (sc == 0) sc = 1;
    DaqListOdtSize(odt) = (vuint16)(DaqListOdtSize(odt) + size*sc); 
#else
    DaqListOdtSize(odt) = (vuint16)(DaqListOdtSize(odt) + size);
#endif
}

// Allocate all ODT entries, Parameter odt is relative odt number
vuint8 XcpAllocOdtEntry( vuint16 daq, vuint8 odt, vuint8 odtEntryCount )
{
  int xcpFirstOdt;
  vuint32 n;

  if ( (gXcp.Daq.DaqCount==0) || (gXcp.Daq.OdtCount==0) )  {
    return (vuint8)CRC_SEQUENCE;
  }
  if (odtEntryCount==0)  {
    return (vuint8)CRC_OUT_OF_RANGE;
  }
  
  /* Absolute ODT entry count is limited to 64K */
  n = (vuint32)gXcp.Daq.OdtEntryCount + (vuint32)odtEntryCount;
  if (n>0xFFFF) return CRC_MEMORY_OVERFLOW;

  xcpFirstOdt = gXcp.Daq.u.DaqList[daq].firstOdt;
  gXcp.pOdt[xcpFirstOdt+odt].firstOdtEntry = gXcp.Daq.OdtEntryCount;
  gXcp.Daq.OdtEntryCount = (vuint16)n;
  gXcp.pOdt[xcpFirstOdt + odt].lastOdtEntry = (vuint16)(gXcp.Daq.OdtEntryCount - 1);
  gXcp.pOdt[xcpFirstOdt + odt].size = 0;

  return XcpAllocMemory();
}

// Set ODT entry pointer
vuint8  XcpSetDaqPtr(vuint16 daq, vuint8 odt, vuint8 idx) {

    vuint16 odt0 = (vuint16)(DaqListFirstOdt(daq) + odt); // Absolute odt index
    if ((daq >= gXcp.Daq.DaqCount) || (odt >= DaqListOdtCount(daq)) || (idx >= DaqListOdtEntryCount(odt0))) return CRC_OUT_OF_RANGE;
    // Save info for XcpAddOdtEntry from WRITE_DAQ and WRITE_DAQ_MULTIPLE
    gXcp.WriteDaqOdtEntry = (vuint16)(DaqListOdtFirstEntry(odt0) + idx); // Absolute odt entry index
    gXcp.WriteDaqOdt = odt0; // Absolute odt index
    gXcp.WriteDaqDaq = daq;
    return 0;
}

// Add an ODT entry to current DAQ/ODT
vuint8  XcpAddOdtEntry(vuint32 addr, vuint8 ext, vuint8 size) {
    if ((size == 0) || size > XCP_MAX_ODT_ENTRY_SIZE) return CRC_OUT_OF_RANGE;
    if (0 == gXcp.Daq.DaqCount || 0 == gXcp.Daq.OdtCount || 0 == gXcp.Daq.OdtEntryCount) return CRC_DAQ_CONFIG;
    OdtEntrySize(gXcp.WriteDaqOdtEntry) = size;
    OdtEntryAddr(gXcp.WriteDaqOdtEntry) = addr; // Holds A2L/XCP address
    XcpAdjustOdtSize(gXcp.WriteDaqDaq, gXcp.WriteDaqOdt, size);
    gXcp.WriteDaqOdtEntry++; // Autoincrement to next ODT entry, no autoincrementing over ODTs
    return 0;
}

// Set DAQ list mode
void  XcpSetDaqListMode(vuint16 daq, vuint16 event, vuint8 mode ) {
  DaqListEventChannel(daq) = event;
  DaqListFlags(daq) = mode;
}

// Start DAQ
void  XcpStartDaq( vuint16 daq )
{
  gXcp.DaqStartClock64 = ApplXcpGetClock64();
  gXcp.DaqOverflowCount = 0;
  DaqListFlags(daq) |= (vuint8)DAQ_FLAG_RUNNING;

  ApplXcpDaqStart();
  gXcp.SessionStatus |= (vuint8)SS_DAQ;
}

// Start all selected DAQs
void  XcpStartAllSelectedDaq()
{
  vuint16 daq;

  /* Start all selected DAQs */
  gXcp.DaqStartClock64 = ApplXcpGetClock64();
  gXcp.DaqOverflowCount = 0;
  for (daq=0;daq<gXcp.Daq.DaqCount;daq++)  {
    if ( (DaqListFlags(daq) & (vuint8)DAQ_FLAG_SELECTED) != 0 ) {
      DaqListFlags(daq) |= (vuint8)DAQ_FLAG_RUNNING;
      DaqListFlags(daq) &= (vuint8)(~DAQ_FLAG_SELECTED);
#if defined ( XCP_ENABLE_TESTMODE )
      if (ApplXcpDebugLevel >= 2) {
          XcpPrintDaqList(daq);
      }
#endif
    }
  }

  ApplXcpDaqStart();
  gXcp.SessionStatus |= (vuint8)SS_DAQ;
}

// Stop DAQ
void  XcpStopDaq( vuint16 daq )
{
  vuint8 i;

  DaqListFlags(daq) &= (vuint8)(DAQ_FLAG_DIRECTION|DAQ_FLAG_TIMESTAMP|DAQ_FLAG_NO_PID);

  /* Check if all DAQ lists are stopped */
  for (i=0;i<gXcp.Daq.DaqCount;i++)  {
    if ( (DaqListFlags(i) & (vuint8)DAQ_FLAG_RUNNING) != 0 )  {
      return;
    }
  }

  ApplXcpDaqStop();
  gXcp.SessionStatus &= (vuint8)(~SS_DAQ);
}

// Stop all selected DAQs
void  XcpStopAllSelectedDaq()
{
  vuint16 daq;

  for (daq=0;daq<gXcp.Daq.DaqCount;daq++) {
    if ( (DaqListFlags(daq) & (vuint8)DAQ_FLAG_SELECTED) != 0 ) {
      XcpStopDaq(daq);
      DaqListFlags(daq) &= (vuint8)(~DAQ_FLAG_SELECTED);
    }
  }
}

// Stop all DAQs
void  XcpStopAllDaq( void )
{
  for (vuint8 daq=0; daq<gXcp.Daq.DaqCount; daq++) {
    DaqListFlags(daq) &= (vuint8)(DAQ_FLAG_DIRECTION|DAQ_FLAG_TIMESTAMP|DAQ_FLAG_NO_PID);
  }

  ApplXcpDaqStop();
  gXcp.SessionStatus &= (vuint8)(~SS_DAQ);
}


/****************************************************************************/
/* Data Aquisition Processor                                                */
/****************************************************************************/

// Measurement data acquisition, sample and transmit measurement date associated to event

static void XcpEvent_(vuint16 event, vuint8* base, vuint64 clock)
{
  vuint8* d;
  vuint8* d0;
  void* p0;
  vuint32 e, el, odt, daq, hs, n;
#ifdef XCP_ENABLE_PACKED_MODE
  vuint32 sc;
#endif
  
  for (daq=0; daq<gXcp.Daq.DaqCount; daq++) {

      if ((DaqListFlags(daq) & (vuint8)DAQ_FLAG_RUNNING) == 0) continue; // DAQ list not active
      if ( DaqListEventChannel(daq) != event ) continue; // DAQ list not associated with this event
#ifdef XCP_ENABLE_PACKED_MODE
      sc = DaqListSampleCount(daq); // Packed mode sample count, 0 if not packed
#endif
      for (hs=2+XCP_TIMESTAMP_SIZE,odt=DaqListFirstOdt(daq);odt<=DaqListLastOdt(daq);hs=2,odt++)  { 
                      
        // Get DTO buffer, overrun if not available
        if ((d0 = ApplXcpGetDtoBuffer(&p0, DaqListOdtSize(odt)+hs)) == 0) {
#if defined ( XCP_ENABLE_TESTMODE )
            if (ApplXcpDebugLevel >= 2) ApplXcpPrint("DAQ queue overflow! Event %u skipped\n", event);
#endif
            gXcp.DaqOverflowCount++;
            DaqListFlags(daq) |= DAQ_FLAG_OVERRUN;
            return; // Skip rest of this event on queue overrun
        }
  
        /* ODT,DAQ header */
        d0[0] = (vuint8)(odt-DaqListFirstOdt(daq)); /* Relative odt number */
        d0[1] = (vuint8)daq;
        
        /* Use BIT7 of PID or ODT to indicate overruns */  
        if ( (DaqListFlags(daq) & DAQ_FLAG_OVERRUN) != 0 ) {
          d0[0] |= 0x80;
          DaqListFlags(daq) &= (vuint8)(~DAQ_FLAG_OVERRUN);
        }
  
        /* Timestamp */
#if (XCP_TIMESTAMP_SIZE==8) // @@@@ XCP V1.6
        if (hs==10) *((vuint64*)&d0[2]) = clock;
#else
        if (hs==6) *((vuint32*)&d0[2]) = (vuint32)clock;
#endif

        /* Copy data */
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
                memcpy((vuint8*)d, &base[OdtEntryAddr(e)], n);
                d += n;
                e++;
            } // ODT entry
        }

        ApplXcpCommitDtoBuffer(p0);
               
      } /* odt */

  } /* daq */
  
}

void XcpEventAt(vuint16 event, vuint64 clock) {
    if ((gXcp.SessionStatus & (vuint8)SS_DAQ) == 0) return; // DAQ not running
    XcpEvent_(event, ApplXcpGetBaseAddr(), clock);
}

void XcpEventExt(vuint16 event, vuint8* base) {
    if ((gXcp.SessionStatus & (vuint8)SS_DAQ) == 0) return; // DAQ not running
    XcpEvent_(event, base, ApplXcpGetClock64());
}

void XcpEvent(vuint16 event) {
    if ((gXcp.SessionStatus & (vuint8)SS_DAQ) == 0) return; // DAQ not running
    XcpEvent_(event, ApplXcpGetBaseAddr(), ApplXcpGetClock64());
}

/****************************************************************************/
/* Command Processor                                                        */
/****************************************************************************/


// Stops DAQ and goes to disconnected state
void  XcpDisconnect( void )
{
  gXcp.SessionStatus &= (vuint8)(~SS_CONNECTED);
  XcpStopAllDaq();
}


//  Handles incoming XCP commands
void  XcpCommand( const vuint32* pCommand )
{
  const tXcpCto* pCmd = (const tXcpCto*) pCommand; 
  vuint8 err = 0;

  // Prepare the default response
  CRM_CMD = PID_RES; /* Response, no error */
  gXcp.CrmLen = 1; /* Length = 1 */

  // CONNECT ?
  if (CRO_CMD==CC_CONNECT) 
  {
#if defined ( XCP_ENABLE_TESTMODE )
      if (ApplXcpDebugLevel >= 1) {
          ApplXcpPrint("CONNECT mode=%u\n", CRO_CONNECT_MODE);
          if (gXcp.SessionStatus & SS_CONNECTED) ApplXcpPrint("  Already connected! DAQ setup cleared! Legacy mode activated!\n");
      }
#endif
      
    // Set Session Status
    gXcp.SessionStatus = (vuint8)(SS_CONNECTED | SS_LEGACY_MODE);

    /* Reset DAQ */
    XcpFreeDaq();

    // Response
    gXcp.CrmLen = CRM_CONNECT_LEN;
    CRM_CONNECT_TRANSPORT_VERSION = (vuint8)( (vuint16)XCP_TRANSPORT_LAYER_VERSION >> 8 ); /* Major versions of the XCP Protocol Layer and Transport Layer Specifications. */
    CRM_CONNECT_PROTOCOL_VERSION =  (vuint8)( (vuint16)XCP_PROTOCOL_LAYER_VERSION >> 8 );
    CRM_CONNECT_MAX_CTO_SIZE = XCPTL_CTO_SIZE;
    CRM_CONNECT_MAX_DTO_SIZE = XCPTL_DTO_SIZE; 
    CRM_CONNECT_RESOURCE = 0x00;                  /* Reset resource mask */
    CRM_CONNECT_RESOURCE |= (vuint8)RM_DAQ;       /* Data Acquisition */
    CRM_CONNECT_COMM_BASIC = 0;
    CRM_CONNECT_COMM_BASIC |= (vuint8)CMB_OPTIONAL;
#if defined ( XCP_CPUTYPE_BIGENDIAN )
    CRM_CONNECT_COMM_BASIC |= (vuint8)PI_MOTOROLA;
#endif

  }

  // Handle other all other commands 
  else {

#ifdef XCP_ENABLE_TESTMODE
      if (ApplXcpDebugLevel >= 1) XcpPrintCmd(pCmd);
#endif
      if (!(gXcp.SessionStatus & SS_CONNECTED)) { // Must be connected
#ifdef XCP_ENABLE_TESTMODE
          if (ApplXcpDebugLevel >= 1) ApplXcpPrint("Command ignored because not in connected state, no response sent!\n");
#endif
          return;
      }

      switch (CRO_CMD)
      {

        case CC_SYNC:
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
            CRM_GET_COMM_MODE_INFO_COMM_OPTIONAL = 0;
            CRM_GET_COMM_MODE_INFO_QUEUE_SIZE = 0;
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
              gXcp.CrmLen = CRM_GET_ID_LEN;
              CRM_GET_ID_MODE = 0;
              CRM_GET_ID_LENGTH = 0;
              switch (CRO_GET_ID_TYPE) {
                  case IDT_ASCII:
                      CRM_GET_ID_LENGTH = ApplXcpSlaveIdLen;
                      gXcp.Mta = (vuint8*)&ApplXcpSlaveId[0];
                      break;
#ifdef XCP_ENABLE_A2L_NAME
                  case IDT_ASAM_NAME:
                      if (!ApplXcpGetA2LFilename((char**)&gXcp.Mta, &CRM_GET_ID_LENGTH,0)) error(CRC_OUT_OF_RANGE);
                      break;
#endif
#ifdef XCP_ENABLE_FILE_UPLOAD
                  case IDT_VECTOR_MDI:
                  case IDT_ASAM_UPLOAD:
                      if (!ApplXcpReadFile(CRO_GET_ID_TYPE, &gXcp.Mta, &CRM_GET_ID_LENGTH)) error(CRC_ACCESS_DENIED);
                      break;
#endif
                  case IDT_ASAM_PATH:
                  case IDT_ASAM_URL:
                      CRM_GET_ID_LENGTH = 0;     // No error, just return length=0, INCA always polls ID_TYPE 0-4
                      break;
                  default:
                      error(CRC_OUT_OF_RANGE);
              }
            }
            break;                    

          case CC_GET_STATUS: 
            {
              gXcp.CrmLen = CRM_GET_STATUS_LEN;
              CRM_GET_STATUS_STATUS = (vuint8)gXcp.SessionStatus;
              CRM_GET_STATUS_PROTECTION = 0;
              CRM_GET_STATUS_CONFIG_ID = 0; /* Session configuration ID not available. */
            }
            break;

          case CC_SET_MTA:
            {
              gXcp.Mta = ApplXcpGetPointer(CRO_SET_MTA_EXT,CRO_SET_MTA_ADDR);
            }
            break;

          case CC_DOWNLOAD:
          {
              vuint8 size;
              size = CRO_DOWNLOAD_SIZE;
              if (size > CRO_DOWNLOAD_MAX_SIZE) error(CRC_OUT_OF_RANGE)
              err = XcpWriteMta(size, CRO_DOWNLOAD_DATA);
              if (err == XCP_CMD_DENIED) error(CRC_WRITE_PROTECTED);
          }
          break;

          case CC_SHORT_DOWNLOAD:
          {
              gXcp.Mta = ApplXcpGetPointer(CRO_SHORT_DOWNLOAD_EXT, CRO_SHORT_DOWNLOAD_ADDR);
              vuint8 size;
              size = CRO_SHORT_DOWNLOAD_SIZE;
              if (size > CRO_SHORT_DOWNLOAD_MAX_SIZE) error(CRC_OUT_OF_RANGE)
              err = XcpWriteMta(size, CRO_SHORT_DOWNLOAD_DATA);
              if (err == XCP_CMD_DENIED) error(CRC_WRITE_PROTECTED);
          }
          break;

          case CC_UPLOAD:
            {
              vuint8 size = CRO_UPLOAD_SIZE;
              if (size > (vuint8)CRM_UPLOAD_MAX_SIZE) error(CRC_OUT_OF_RANGE);
              err = XcpReadMta(size,CRM_UPLOAD_DATA);
              gXcp.CrmLen = (vuint8)(CRM_UPLOAD_LEN+size);
              if (err == XCP_CMD_PENDING) return; // No response
              if (err == XCP_CMD_DENIED) error(CRC_ACCESS_DENIED);
            }
            break;

          case CC_SHORT_UPLOAD:
            {
              if (CRO_SHORT_UPLOAD_SIZE > (vuint8)CRM_SHORT_UPLOAD_MAX_SIZE) error(CRC_OUT_OF_RANGE);
              gXcp.Mta = ApplXcpGetPointer(CRO_SHORT_UPLOAD_EXT,CRO_SHORT_UPLOAD_ADDR);
              err = XcpReadMta(CRO_SHORT_UPLOAD_SIZE,CRM_SHORT_UPLOAD_DATA);
              gXcp.CrmLen = (vuint8)(CRM_SHORT_UPLOAD_LEN+CRO_SHORT_UPLOAD_SIZE);
              if (err == XCP_CMD_PENDING) return; // No response
              if (err == XCP_CMD_DENIED) error(CRC_ACCESS_DENIED);
            }
            break;

#ifdef XCP_ENABLE_CAL_PAGE
          case CC_SET_CAL_PAGE:
          {
              check_error(ApplXcpSetCalPage(CRO_SET_CAL_PAGE_SEGMENT, CRO_SET_CAL_PAGE_PAGE, CRO_SET_CAL_PAGE_MODE));
          }
          break;

          case CC_GET_CAL_PAGE:
          {
              gXcp.CrmLen = CRM_GET_CAL_PAGE_LEN;
              CRM_GET_CAL_PAGE_PAGE = ApplXcpGetCalPage(CRO_GET_CAL_PAGE_SEGMENT, CRO_GET_CAL_PAGE_MODE);
          }
          break;
#endif


#if defined ( XCP_ENABLE_CHECKSUM )
          case CC_BUILD_CHECKSUM: /* Build Checksum */
          {
              vuint32 n = CRO_BUILD_CHECKSUM_SIZE;
              vuint32 s = 0;
              vuint32 d,i;
              if (n % 4 != 0) error(CRC_OUT_OF_RANGE)
              n = n / 4;
              for (i = 0; i < n; i++) { XcpReadMta(4, (vuint8*)&d); s += d; }
              CRM_BUILD_CHECKSUM_RESULT = s;
              CRM_BUILD_CHECKSUM_TYPE = XCP_CHECKSUM_TYPE_ADD44;
              gXcp.CrmLen = CRM_BUILD_CHECKSUM_LEN;
          }
          break;
#endif


            case CC_GET_DAQ_PROCESSOR_INFO: 
            {
              gXcp.CrmLen = CRM_GET_DAQ_PROCESSOR_INFO_LEN;
              CRM_GET_DAQ_PROCESSOR_INFO_MIN_DAQ = 0;          
              CRM_GET_DAQ_PROCESSOR_INFO_MAX_DAQ = (gXcp.Daq.DaqCount); /* dynamic or */ 
#if defined ( XCP_ENABLE_DAQ_EVENT_INFO ) 
              CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT = ApplXcpEventCount;
#else
              CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT = 0; /* Unknown */
#endif    
              CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE = (vuint8)DAQ_HDR_ODT_DAQB; /* DTO identification field type: Relative ODT number, absolute list number (BYTE) */
              CRM_GET_DAQ_PROCESSOR_INFO_PROPERTIES = (vuint8)( DAQ_PROPERTY_CONFIG_TYPE | DAQ_PROPERTY_TIMESTAMP | DAQ_OVERLOAD_INDICATION_PID );
            }
            break;

            case CC_GET_DAQ_RESOLUTION_INFO: 
              {
                gXcp.CrmLen = CRM_GET_DAQ_RESOLUTION_INFO_LEN;
                CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_DAQ = 1;
                CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_STIM = 1;
                CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_DAQ  = (vuint8)XCP_MAX_ODT_ENTRY_SIZE;
                CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_STIM = (vuint8)XCP_MAX_ODT_ENTRY_SIZE;
#if (XCP_TIMESTAMP_SIZE==8) // @@@@ XCP V1.6
                CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE = XCP_TIMESTAMP_UNIT | DAQ_TIMESTAMP_FIXED | DAQ_TIMESTAMP_QWORD;
#else
                CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE = XCP_TIMESTAMP_UNIT | DAQ_TIMESTAMP_FIXED | DAQ_TIMESTAMP_DWORD;
#endif
                CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS = (XCP_TIMESTAMP_TICKS);  
              }
              break;

#ifdef XCP_ENABLE_DAQ_EVENT_INFO  
            case CC_GET_DAQ_EVENT_INFO:
            {
                vuint16 event = CRO_GET_DAQ_EVENT_INFO_EVENT;
                if (event >= (vuint16)ApplXcpEventCount) error(CRC_OUT_OF_RANGE);
                gXcp.CrmLen = CRM_GET_DAQ_EVENT_INFO_LEN;
                CRM_GET_DAQ_EVENT_INFO_PROPERTIES = DAQ_EVENT_PROPERTIES_DAQ | DAQ_EVENT_PROPERTIES_EVENT_CONSISTENCY;
#ifdef XCP_ENABLE_PACKED_MODE
                if (ApplXcpEventList[event].sampleCount) CRM_GET_DAQ_EVENT_INFO_PROPERTIES |= DAQ_EVENT_PROPERTIES_PACKED;
#endif
                if (ApplXcpEventList[event].size) CRM_GET_DAQ_EVENT_INFO_PROPERTIES |= DAQ_EVENT_PROPERTIES_EXT;
                CRM_GET_DAQ_EVENT_INFO_MAX_DAQ_LIST = 0xFF;
                CRM_GET_DAQ_EVENT_INFO_NAME_LENGTH = (vuint8)strlen(ApplXcpEventList[event].name);
                CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE = ApplXcpEventList[event].timeCycle;
                CRM_GET_DAQ_EVENT_INFO_TIME_UNIT = ApplXcpEventList[event].timeUnit;
                CRM_GET_DAQ_EVENT_INFO_PRIORITY = 0;
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0106
                CRM_GET_DAQ_EVENT_INFO_SAMPLECOUNT = ApplXcpEventList[event].sampleCount;
                CRM_GET_DAQ_EVENT_INFO_SIZE = ApplXcpEventList[event].size;
#endif
                gXcp.Mta = (vuint8*)ApplXcpEventList[event].name;

            }
            break;
#endif

            case CC_FREE_DAQ: 
            {
                XcpFreeDaq();
#ifdef XCP_ENABLE_GRANDMASTER_CLOCK_INFO // Check if clock status allows to start DAQ
                if (!ApplXcpPrepareDaq()) error(CRC_TIMECORR_STATE_CHANGE);;
#endif

            }
            break;

          case CC_ALLOC_DAQ:
            {
                vuint16 count = CRO_ALLOC_DAQ_COUNT;
                check_error(XcpAllocDaq(count));
            }
            break;

          case CC_ALLOC_ODT:
            {
              vuint16 daq = CRO_ALLOC_ODT_DAQ;
              vuint8 count = CRO_ALLOC_ODT_COUNT;
              if (daq >= gXcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
              check_error( XcpAllocOdt(daq, count) ) 
            }
            break;

          case CC_ALLOC_ODT_ENTRY:
            {
              vuint16 daq = CRO_ALLOC_ODT_ENTRY_DAQ;
              vuint8 odt = CRO_ALLOC_ODT_ENTRY_ODT;
              vuint8 count = CRO_ALLOC_ODT_ENTRY_COUNT;
              if ((daq >= gXcp.Daq.DaqCount) || (odt >= DaqListOdtCount(daq))) error(CRC_OUT_OF_RANGE);
              check_error( XcpAllocOdtEntry(daq, odt, count) ) 
            }
            break;

          case CC_GET_DAQ_LIST_MODE:
            {
              vuint16 daq = CRO_GET_DAQ_LIST_MODE_DAQ;
              if (daq >= gXcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
              gXcp.CrmLen = CRM_GET_DAQ_LIST_MODE_LEN;
              CRM_GET_DAQ_LIST_MODE_MODE = DaqListFlags(daq);
              CRM_GET_DAQ_LIST_MODE_PRESCALER = 1;
              CRM_GET_DAQ_LIST_MODE_EVENTCHANNEL = (DaqListEventChannel(daq));
              CRM_GET_DAQ_LIST_MODE_PRIORITY = 0;  // DAQ-list prioritization is not supported
            }
            break;

          case CC_SET_DAQ_LIST_MODE:
            {
              vuint16 daq = CRO_SET_DAQ_LIST_MODE_DAQ;
              vuint16 event = CRO_SET_DAQ_LIST_MODE_EVENTCHANNEL;
              vuint8 mode = CRO_SET_DAQ_LIST_MODE_MODE;
              if (daq >= gXcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
              if (mode & (DAQ_FLAG_NO_PID | DAQ_FLAG_RESUME | DAQ_FLAG_DIRECTION | DAQ_FLAG_CMPL_DAQ_CH | DAQ_FLAG_SELECTED | DAQ_FLAG_RUNNING)) error(CRC_OUT_OF_RANGE);  /* no pid, resume, stim not supported*/
              if (0==(mode & (DAQ_FLAG_TIMESTAMP| DAQ_FLAG_SELECTED))) error(CRC_OUT_OF_RANGE);  /* No timestamp not supported*/
              if (CRO_SET_DAQ_LIST_MODE_PRIORITY != 0) error(CRC_OUT_OF_RANGE);  /* Priorization is not supported */
              XcpSetDaqListMode(daq, event, CRO_SET_DAQ_LIST_MODE_MODE);
              break;
            }

          case CC_SET_DAQ_PTR: /* Set target for WRITE_DAQ or WRITE_DAQ_MULTIPLE */
            {
              vuint16 daq = CRO_SET_DAQ_PTR_DAQ;
              vuint8 odt = CRO_SET_DAQ_PTR_ODT;
              vuint8 idx = CRO_SET_DAQ_PTR_IDX;
              check_error(XcpSetDaqPtr(daq,odt,idx));
            }
            break;

          case CC_WRITE_DAQ: /* Write ODT entry */
            {    
                vuint8 err = XcpAddOdtEntry(CRO_WRITE_DAQ_ADDR, CRO_WRITE_DAQ_EXT, CRO_WRITE_DAQ_SIZE);
                if (err) error(err);
            }
            break;

          case CC_WRITE_DAQ_MULTIPLE: /* Write multiple ODT entries */
              {
                 for (int i = 0; i < CRO_WRITE_DAQ_MULTIPLE_NODAQ; i++) {
                     vuint8 err = XcpAddOdtEntry(CRO_WRITE_DAQ_MULTIPLE_ADDR(i), CRO_WRITE_DAQ_MULTIPLE_EXT(i), CRO_WRITE_DAQ_MULTIPLE_SIZE(i));
                     if (err) error(err);
                  }
              }
              break;

          case CC_START_STOP_DAQ_LIST:
            {
              vuint16 daq = CRO_START_STOP_DAQ;
              if (daq >= gXcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE); 
              if ( (CRO_START_STOP_MODE==1 ) || (CRO_START_STOP_MODE==2) )  { // start or select
                DaqListFlags(daq) |= (vuint8)DAQ_FLAG_SELECTED;
                if (CRO_START_STOP_MODE == 1) { // start 
                    XcpStartDaq(daq); 
                }
                gXcp.CrmLen = CRM_START_STOP_LEN;
                CRM_START_STOP_FIRST_PID = 0; // Absolute DAQ, Relative ODT - DaqListFirstPid(daq);
              } 
              else {
                XcpStopDaq(daq);
              }

            }
            break;

          case CC_START_STOP_SYNCH:
            {
              if ((0 == gXcp.Daq.DaqCount) || (0 == gXcp.Daq.OdtCount) || (0 == gXcp.Daq.OdtEntryCount)) error(CRC_DAQ_CONFIG);
              switch (CRO_START_STOP_MODE) {
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
              case 3: /* prepare for start selected */
                  ApplXcpPrepareDaqStart();
                  break;
#endif
              case 2: /* stop selected */
                  XcpStopAllSelectedDaq();
                  break;
              case 1: /* start selected */
                  XcpStartAllSelectedDaq();
                  break;
              case 0: /* stop all */
                  XcpStopAllDaq();
                  break;
              default: 
                  error(CRC_OUT_OF_RANGE);
              }
  
            }
            break;

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
            case CC_TIME_CORRELATION_PROPERTIES:
            {
              if ((CRO_TIME_SYNC_PROPERTIES_SET_PROPERTIES & TIME_SYNC_SET_PROPERTIES_RESPONSE_FMT) >= 1) { // set extended format
                  #if defined ( XCP_ENABLE_TESTMODE )
                  if (ApplXcpDebugLevel >= 2) ApplXcpPrint("  Timesync extended mode activated (RESPONSE_FMT=%u)\n", CRO_TIME_SYNC_PROPERTIES_SET_PROPERTIES & TIME_SYNC_SET_PROPERTIES_RESPONSE_FMT);
                  #endif                 
                gXcp.SessionStatus = (vuint8)(gXcp.SessionStatus & ~SS_LEGACY_MODE); 
              }
              
              if (CRO_TIME_SYNC_PROPERTIES_SET_PROPERTIES & TIME_SYNC_SET_PROPERTIES_CLUSTER_ID) { // set cluster id
                  #if defined ( XCP_ENABLE_TESTMODE )
                  if (ApplXcpDebugLevel >= 2) ApplXcpPrint("  Cluster id set to %u\n", CRO_TIME_SYNC_PROPERTIES_CLUSTER_ID);
                  #endif                 
                  gXcp.ClusterId = CRO_TIME_SYNC_PROPERTIES_CLUSTER_ID; // Set cluster id
                  ApplXcpSetClusterId(gXcp.ClusterId);
              }

              if (CRO_TIME_SYNC_PROPERTIES_SET_PROPERTIES & TIME_SYNC_SET_PROPERTIES_TIME_SYNC_BRIDGE) { // set time sync bride is not supported -> error
                  error(CRC_OUT_OF_RANGE);
              }

              gXcp.CrmLen = CRM_TIME_SYNC_PROPERTIES_LEN;
              
#ifndef XCP_ENABLE_GRANDMASTER_CLOCK_INFO
              CRM_TIME_SYNC_PROPERTIES_SLAVE_CONFIG = SLAVE_CONFIG_RESPONSE_FMT_ADVANCED | SLAVE_CONFIG_DAQ_TS_SLAVE | SLAVE_CONFIG_TIME_SYNC_BRIDGE_NONE;  // SLAVE_CONFIG_RESPONSE_FMT_LEGACY
              CRM_TIME_SYNC_PROPERTIES_OBSERVABLE_CLOCKS = SLAVE_CLOCK_FREE_RUNNING | SLAVE_GRANDM_CLOCK_NONE | ECU_CLOCK_NONE;
              CRM_TIME_SYNC_PROPERTIES_SYNC_STATE = SLAVE_CLOCK_STATE_FREE_RUNNING;
              CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO = CLOCK_INFO_SLAVE; 
              CRM_TIME_SYNC_PROPERTIES_RESERVED = 0x0;
              CRM_TIME_SYNC_PROPERTIES_CLUSTER_ID = gXcp.ClusterId;
              if (CRO_TIME_SYNC_PROPERTIES_GET_PROPERTIES_REQUEST & TIME_SYNC_GET_PROPERTIES_GET_CLK_INFO) { // check whether MTA based upload is requested
                  if (ApplXcpDebugLevel >= 1) ApplXcpPrint("  SLAVE CLOCK info on MTA\n");
                  gXcp.Mta = (vuint8*)&gXcp.SlaveClockInfo;
              }
#else
              CRM_TIME_SYNC_PROPERTIES_SLAVE_CONFIG = SLAVE_CONFIG_RESPONSE_FMT_ADVANCED | SLAVE_CONFIG_DAQ_TS_SLAVE | SLAVE_CONFIG_TIME_SYNC_BRIDGE_NONE;  // SLAVE_CONFIG_RESPONSE_FMT_LEGACY
              CRM_TIME_SYNC_PROPERTIES_OBSERVABLE_CLOCKS = SLAVE_CLOCK_SYNCED | SLAVE_GRANDM_CLOCK_READABLE | ECU_CLOCK_NONE;
              CRM_TIME_SYNC_PROPERTIES_SYNC_STATE = SLAVE_CLOCK_STATE_SYNCH;
              CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO = CLOCK_INFO_SLAVE | CLOCK_INFO_SLAVE_GRANDM | CLOCK_INFO_RELATION;
              CRM_TIME_SYNC_PROPERTIES_RESERVED = 0x0;
              CRM_TIME_SYNC_PROPERTIES_CLUSTER_ID = gXcp.ClusterId;
              if (CRO_TIME_SYNC_PROPERTIES_GET_PROPERTIES_REQUEST & TIME_SYNC_GET_PROPERTIES_GET_CLK_INFO) { // check whether MTA based upload is requested
                  static unsigned char buf[sizeof(T_CLOCK_INFO_SLAVE) + sizeof(T_CLOCK_INFO_RELATION) + sizeof(T_CLOCK_INFO_GRANDMASTER)];
                  unsigned char* p = &buf[0];
                  ApplXcpGetClockInfo(&gXcp.SlaveClockInfo, &gXcp.GrandmasterClockInfo);
                  if (CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO & CLOCK_INFO_SLAVE) {
                      if (ApplXcpDebugLevel >= 1) ApplXcpPrint("  -> SLAVE CLOCK info on MTA\n");
                      memcpy(p, (unsigned char*)&gXcp.SlaveClockInfo, sizeof(T_CLOCK_INFO_SLAVE));
                      p += sizeof(T_CLOCK_INFO_SLAVE);
                  }

                  if (CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO & CLOCK_INFO_SLAVE_GRANDM) {
                      if (ApplXcpDebugLevel >= 1) ApplXcpPrint("  -> GRANDMASTER CLK info on MTA\n");
                      memcpy(p, (unsigned char*)&gXcp.GrandmasterClockInfo, sizeof(T_CLOCK_INFO_GRANDMASTER));
                      p += sizeof(T_CLOCK_INFO_GRANDMASTER);
                  }

                  if (CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO & CLOCK_INFO_RELATION) {
                      if (ApplXcpDebugLevel >= 1) ApplXcpPrint("  -> CLOCK RELATION info on MTA\n");
                      gXcp.ClockRelationInfo.timestampLocal = ApplXcpGetClock64();
                      gXcp.ClockRelationInfo.timestampOrigin = gXcp.ClockRelationInfo.timestampLocal;
                      memcpy(p, (unsigned char*)&gXcp.ClockRelationInfo, sizeof(T_CLOCK_INFO_RELATION));
                      p += sizeof(T_CLOCK_INFO_RELATION);
                  }

                  gXcp.Mta = (vuint8*)buf;
              }
#endif

            }
            break; 
#endif

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
            case CC_TRANSPORT_LAYER_CMD:

              switch (CRO_TL_SUBCOMMAND) {

#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
              case CC_TL_GET_DAQ_CLOCK_MULTICAST: 
              {
                  vuint16 clusterId = CRO_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER;
                  if (gXcp.ClusterId != clusterId) error(CRC_OUT_OF_RANGE);
                  CRM_GET_DAQ_CLOCK_TRIGGER_INFO = 0x18 + 0x02; // TIME_OF_SAMPLING (Bitmask 0x18, 3 - Sampled on reception) + TRIGGER_INITIATOR ( Bitmask 0x07, 2 - GET_DAQ_CLOCK_MULTICAST)
                  CRM_CMD = PID_EV;
                  CRM_EVENTCODE = EVC_TIME_SYNC;
#ifdef XCP_DAQ_CLOCK_64BIT
                  CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = 0x42; // FMT_XCP_SLV = size of payload is DLONG + CLUSTER_ID
                  CRM_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER64 = CRO_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER;
                  CRM_DAQ_CLOCK_MCAST_COUNTER64 = CRO_DAQ_CLOCK_MCAST_COUNTER;
                  CRM_DAQ_CLOCK_MCAST_SYNC_STATE64 = 1;
                  gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN + 8;
#else
                  CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = 0x41; // FMT_XCP_SLV = size of payload is DWORD + CLUSTER_ID
                  CRM_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER = CRO_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER;
                  CRM_DAQ_CLOCK_MCAST_COUNTER = CRO_DAQ_CLOCK_MCAST_COUNTER;
                  CRM_DAQ_CLOCK_MCAST_SYNC_STATE = 1;
                  gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN + 4;
#endif
                  goto getDaqClockMulticast;
              }
#endif

              case CC_TL_GET_SLAVE_ID:
              default: /* unknown transport layer command */
                  error(CRC_CMD_UNKNOWN);
              }
              break;
#endif

          case CC_GET_DAQ_CLOCK:
          {
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
              CRM_GET_DAQ_CLOCK_RES1 = 0x00; // Placeholder for event code
              CRM_GET_DAQ_CLOCK_TRIGGER_INFO = 0x18; // TIME_OF_SAMPLING (Bitmask 0x18, 3 - Sampled on reception) 
              CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = 0x01; // FMT_XCP_SLV = size of payload is DWORD
              gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN;
  #ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
    #ifdef XCP_DAQ_CLOCK_64BIT
              gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN + 5;
              CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = 0x2;// FMT_XCP_SLV = size of payload is DLONG
              CRM_GET_DAQ_CLOCK_SYNC_STATE64 = 1;
    #else
              gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN + 1;
              CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = 0x01; // FMT_XCP_SLV = size of payload is DWORD
              CRM_GET_DAQ_CLOCK_SYNC_STATE = 1;
    #endif
              getDaqClockMulticast:
  #endif
              if (!(gXcp.SessionStatus & SS_LEGACY_MODE)) { // Extended format
  #ifdef XCP_DAQ_CLOCK_64BIT
                  CRM_GET_DAQ_CLOCK_TIME64 = ApplXcpGetClock64();
  #else
                  CRM_GET_DAQ_CLOCK_TIME = ApplXcpGetClock();
  #endif
              }
              else 
#endif // >= 0x0130
              { // Legacy format
                  gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN;
                  CRM_GET_DAQ_CLOCK_TIME = ApplXcpGetClock();
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
                CRM_GET_VERSION_PROTOCOL_VERSION_MAJOR = (vuint8)((vuint16)XCP_PROTOCOL_LAYER_VERSION >> 8);
                CRM_GET_VERSION_PROTOCOL_VERSION_MINOR = (vuint8)(XCP_PROTOCOL_LAYER_VERSION & 0xFF);
                CRM_GET_VERSION_TRANSPORT_VERSION_MAJOR = (vuint8)((vuint16)XCP_TRANSPORT_LAYER_VERSION >> 8);
                CRM_GET_VERSION_TRANSPORT_VERSION_MINOR = (vuint8)(XCP_TRANSPORT_LAYER_VERSION & 0xFF);
                break;

  #ifdef XCP_ENABLE_PACKED_MODE
              case CC_SET_DAQ_LIST_PACKED_MODE:
              {
                  vuint16 daq = CRO_SET_DAQ_LIST_PACKED_MODE_DAQ;
                  if (daq >= gXcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
                  if (CRO_SET_DAQ_LIST_PACKED_MODE_MODE!=0x01) error(CRC_DAQ_CONFIG); // only element grouped implemented
                  //if (CRO_SET_DAQ_LIST_PACKED_MODE_TIMEMODE != 0x00) error(CRC_DAQ_CONFIG); // early or late timestamp implemented ?
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
#if defined ( XCP_ENABLE_TESTMODE )
  if (ApplXcpDebugLevel >= 1) XcpPrintRes(pCmd);
#endif
  ApplXcpSendCrm(&gXcp.Crm.b[0], gXcp.CrmLen);
  return;

  // Transmit error response
  negative_response:
  gXcp.CrmLen = 2;
  CRM_CMD = PID_ERR;
  CRM_ERR = err;
#if defined ( XCP_ENABLE_TESTMODE )
  if (ApplXcpDebugLevel >= 1) XcpPrintRes(pCmd);
#endif
  ApplXcpSendCrm(&gXcp.Crm.b[0], gXcp.CrmLen);
  return;
}


/*****************************************************************************
| Event
******************************************************************************/
void XcpSendEvent(vuint8 evc, const vuint8* d, vuint8 l)
{
    vuint8 i;
    if (gXcp.SessionStatus & SS_CONNECTED) {
        CRM_BYTE(0) = PID_EV; /* Event*/
        CRM_BYTE(1) = evc;  /* Event Code*/
        gXcp.CrmLen = 2;
        for (i = 0; i < l; i++) CRM_BYTE(gXcp.CrmLen++) = d[i++];
        ApplXcpSendCrm(&gXcp.Crm.b[0], gXcp.CrmLen);
    }
}


/*****************************************************************************
| Initialization of the XCP Protocol Layer
******************************************************************************/
void  XcpInit( void )
{
  /* Initialize all XCP variables to zero */
  memset((vuint8*)&gXcp,0,(vuint16)sizeof(gXcp)); 
   
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103

  gXcp.ClusterId = 1;  // XCP default cluster id (multicast addr 239,255,0,1, group 127,0,1 (mac 01-00-5E-7F-00-01)

  // XCP slave clock default description
  gXcp.SlaveClockInfo.timestampTicks = XCP_TIMESTAMP_TICKS;
  gXcp.SlaveClockInfo.timestampUnit = XCP_TIMESTAMP_UNIT;
  gXcp.SlaveClockInfo.stratumLevel = XCP_STRATUM_LEVEL_ARB;
#ifdef XCP_DAQ_CLOCK_64BIT
  gXcp.SlaveClockInfo.nativeTimestampSize = 8; // NATIVE_TIMESTAMP_SIZE_DLONG;
  gXcp.SlaveClockInfo.valueBeforeWrapAround = 0xFFFFFFFFFFFFFFFFULL;
#else
  gXcp.SlaveClockInfo.nativeTimestampSize = 4; // NATIVE_TIMESTAMP_SIZE_LONG;
  gXcp.SlaveClockInfo.valueBeforeWrapAround = 0xFFFFFFFFULL;
#endif

#ifdef XCP_ENABLE_GRANDMASTER_CLOCK_INFO

  // XCP grandmaster clock default description
  gXcp.GrandmasterClockInfo.timestampTicks = XCP_TIMESTAMP_TICKS;
  gXcp.GrandmasterClockInfo.timestampUnit = XCP_TIMESTAMP_UNIT;
  gXcp.GrandmasterClockInfo.stratumLevel = XCP_STRATUM_LEVEL_ARB; 
  gXcp.GrandmasterClockInfo.nativeTimestampSize = 8; // NATIVE_TIMESTAMP_SIZE_DLONG;
  gXcp.GrandmasterClockInfo.valueBeforeWrapAround = 0xFFFFFFFFFFFFFFFFULL;
  gXcp.GrandmasterClockInfo.epochOfGrandmaster = XCP_DAQ_CLOCK_EPOCH;
  ApplXcpGetClockInfo(&gXcp.SlaveClockInfo, &gXcp.GrandmasterClockInfo);

  // If the slave clock is PTP synchronized, both origin and local timestamps are considered to be the same.
  // Timestamps will be updated to the current value pair
  gXcp.ClockRelationInfo.timestampLocal = 0;
  gXcp.ClockRelationInfo.timestampOrigin = 0;
#endif
#endif

  /* Initialize the session status */
  gXcp.SessionStatus = 0;
}


#ifdef XCP_ENABLE_GRANDMASTER_CLOCK_INFO
void XcpSetGrandmasterClockInfo(vuint8* id, vuint8 epoch, vuint8 stratumLevel) {

    memcpy(gXcp.SlaveClockInfo.UUID, id, 8);
    memcpy(gXcp.GrandmasterClockInfo.UUID, id, 8);
    gXcp.SlaveClockInfo.stratumLevel = gXcp.GrandmasterClockInfo.stratumLevel = stratumLevel;
    gXcp.GrandmasterClockInfo.epochOfGrandmaster = epoch;

}
#endif


/****************************************************************************/
/* Test                                                                     */
/****************************************************************************/

#if defined ( XCP_ENABLE_TESTMODE )

static void  XcpPrintCmd(const tXcpCto * pCmd) {

    switch (CRO_CMD) {

    case CC_SET_MTA:
        ApplXcpPrint("SET_MTA addr=%08Xh, addrext=%02Xh\n", CRO_SET_MTA_ADDR, CRO_SET_MTA_EXT);
        break;

    case CC_DOWNLOAD:
        {
            vuint16 i;
            ApplXcpPrint("DOWNLOAD size=%u, data=", CRO_DOWNLOAD_SIZE);
            for (i = 0; (i < CRO_DOWNLOAD_SIZE) && (i < CRO_DOWNLOAD_MAX_SIZE); i++) {
                ApplXcpPrint("%02X ", CRO_DOWNLOAD_DATA[i]);
            }
            ApplXcpPrint("\n");
        }
        break;

    case CC_SHORT_DOWNLOAD:
        if (ApplXcpDebugLevel >= 2) {
            vuint16 i;
            ApplXcpPrint("SHORT_DOWNLOAD addr=%08Xh, addrext=%02Xh, size=%u, data=", CRO_SHORT_DOWNLOAD_ADDR, CRO_SHORT_DOWNLOAD_EXT, CRO_SHORT_DOWNLOAD_SIZE);
            for (i = 0; (i < CRO_SHORT_DOWNLOAD_SIZE) && (i < CRO_SHORT_DOWNLOAD_MAX_SIZE); i++) {
                ApplXcpPrint("%02X ", CRO_SHORT_DOWNLOAD_DATA[i]);
            }
            ApplXcpPrint("\n");
        }
        break;

    case CC_UPLOAD:
        if (ApplXcpDebugLevel >= 2) {
            ApplXcpPrint("UPLOAD size=%u\n", CRO_UPLOAD_SIZE);
        }
        break;

    case CC_SHORT_UPLOAD:
        if (ApplXcpDebugLevel >= 2 || !(gXcp.SessionStatus & SS_DAQ)) { // Polling DAQ on level 2
            ApplXcpPrint("SHORT_UPLOAD addr=%08Xh, addrext=%02Xh, size=%u\n", CRO_SHORT_UPLOAD_ADDR, CRO_SHORT_UPLOAD_EXT, CRO_SHORT_UPLOAD_SIZE);
        }
        break;

#ifdef XCP_ENABLE_CAL_PAGE
    case CC_SET_CAL_PAGE:
        ApplXcpPrint("SET_CAL_PAGE segment=%u,page =%u,mode=%02Xh\n", CRO_SET_CAL_PAGE_SEGMENT, CRO_SET_CAL_PAGE_PAGE, CRO_SET_CAL_PAGE_MODE);
        break;

    case CC_GET_CAL_PAGE:
        ApplXcpPrint("GET_CAL_PAGE Segment=%u, Mode=%u\n", CRO_GET_CAL_PAGE_SEGMENT, CRO_GET_CAL_PAGE_MODE);
        break;
#endif

#ifdef XCP_ENABLE_CHECKSUM
    case CC_BUILD_CHECKSUM: /* Build Checksum */
        ApplXcpPrint("BUILD_CHECKSUM size=%u\n", CRO_BUILD_CHECKSUM_SIZE);
        break;
#endif

    case CC_SYNC:
            ApplXcpPrint("SYNC\n");
            break;
            
    case CC_GET_COMM_MODE_INFO:
            ApplXcpPrint("GET_COMM_MODE_INFO\n");
            break;

    case CC_DISCONNECT:
            ApplXcpPrint("DISCONNECT\n");
            break;

    case CC_GET_ID:
            ApplXcpPrint("GET_ID type=%u\n", CRO_GET_ID_TYPE);
            break;

    case CC_GET_STATUS:
            ApplXcpPrint("GET_STATUS\n");
            break;

     case CC_GET_DAQ_PROCESSOR_INFO:
            ApplXcpPrint("GET_DAQ_PROCESSOR_INFO\n");
            break;

     case CC_GET_DAQ_RESOLUTION_INFO:
            ApplXcpPrint("GET_DAQ_RESOLUTION_INFO\n");
            break;

     case CC_GET_DAQ_EVENT_INFO:
            ApplXcpPrint("GET_DAQ_EVENT_INFO event=%u\n", CRO_GET_DAQ_EVENT_INFO_EVENT);
            break;

     case CC_FREE_DAQ:
            ApplXcpPrint("FREE_DAQ\n");
            break;

     case CC_ALLOC_DAQ:
            ApplXcpPrint("ALLOC_DAQ count=%u\n", CRO_ALLOC_DAQ_COUNT);
            break;

     case CC_ALLOC_ODT:
            ApplXcpPrint("ALLOC_ODT daq=%u, count=%u\n", CRO_ALLOC_ODT_DAQ, CRO_ALLOC_ODT_COUNT);
            break;

     case CC_ALLOC_ODT_ENTRY:
            if (ApplXcpDebugLevel >= 2) {
              ApplXcpPrint("ALLOC_ODT_ENTRY daq=%u, odt=%u, count=%u\n", CRO_ALLOC_ODT_ENTRY_DAQ, CRO_ALLOC_ODT_ENTRY_ODT, CRO_ALLOC_ODT_ENTRY_COUNT);
            }
            break;

     case CC_GET_DAQ_LIST_MODE:
            ApplXcpPrint("GET_DAQ_LIST_MODE daq=%u\n",CRO_GET_DAQ_LIST_MODE_DAQ );
            break;

     case CC_SET_DAQ_LIST_MODE:
            ApplXcpPrint("SET_DAQ_LIST_MODE daq=%u, mode=%02Xh, eventchannel=%u\n",CRO_SET_DAQ_LIST_MODE_DAQ, CRO_SET_DAQ_LIST_MODE_MODE, CRO_SET_DAQ_LIST_MODE_EVENTCHANNEL);
            break;
           
     case CC_SET_DAQ_PTR:
            if (ApplXcpDebugLevel >= 2) {
                ApplXcpPrint("SET_DAQ_PTR daq=%u,odt=%u,idx=%u\n", CRO_SET_DAQ_PTR_DAQ, CRO_SET_DAQ_PTR_ODT, CRO_SET_DAQ_PTR_IDX);
            }
            break;

     case CC_WRITE_DAQ: 
            ApplXcpPrint("WRITE_DAQ size=%u,addr=%08Xh,%02Xh\n", CRO_WRITE_DAQ_SIZE, CRO_WRITE_DAQ_ADDR, CRO_WRITE_DAQ_EXT);
            break;

     case CC_WRITE_DAQ_MULTIPLE: 
         if (ApplXcpDebugLevel >= 2) {
             ApplXcpPrint("WRITE_DAQ_MULTIPLE count=%u\n", CRO_WRITE_DAQ_MULTIPLE_NODAQ);
             for (int i = 0; i < CRO_WRITE_DAQ_MULTIPLE_NODAQ; i++) {
                 ApplXcpPrint("   %u: size=%u,addr=%08Xh,%02Xh\n", i, CRO_WRITE_DAQ_MULTIPLE_SIZE(i), CRO_WRITE_DAQ_MULTIPLE_ADDR(i), CRO_WRITE_DAQ_MULTIPLE_EXT(i));
             }
         }
         break;

     case CC_START_STOP_DAQ_LIST:
            ApplXcpPrint("START_STOP mode=%02Xh, daq=%u\n", CRO_START_STOP_MODE, CRO_START_STOP_DAQ);
            break;

     case CC_START_STOP_SYNCH:
            ApplXcpPrint("CC_START_STOP_SYNCH mode=%02Xh\n", CRO_START_STOP_MODE);
            break;

     case CC_GET_DAQ_CLOCK:
            if (ApplXcpDebugLevel >= 2) {
              ApplXcpPrint("GET_DAQ_CLOCK\n");
            }
            break;

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
     case CC_TIME_CORRELATION_PROPERTIES:
         ApplXcpPrint("GET_TIME_CORRELATION_PROPERTIES set=%02Xh, request=%u, clusterId=%u\n", CRO_TIME_SYNC_PROPERTIES_SET_PROPERTIES, CRO_TIME_SYNC_PROPERTIES_GET_PROPERTIES_REQUEST, CRO_TIME_SYNC_PROPERTIES_CLUSTER_ID );
         break;
#endif

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
     case CC_LEVEL_1_COMMAND:
         switch (CRO_LEVEL_1_COMMAND_CODE) {
           case CC_GET_VERSION:
               ApplXcpPrint("GET_VERSION\n");
               break;
#ifdef XCP_ENABLE_PACKED_MODE
           case CC_GET_DAQ_LIST_PACKED_MODE:
               ApplXcpPrint("GET_DAQ_LIST_PACKED_MODE daq=%u\n", CRO_GET_DAQ_LIST_PACKED_MODE_DAQ);
               break;
           case CC_SET_DAQ_LIST_PACKED_MODE:
               ApplXcpPrint("SET_DAQ_LIST_PACKED_MODE daq=%u, sampleCount=%u\n", CRO_SET_DAQ_LIST_PACKED_MODE_DAQ,CRO_SET_DAQ_LIST_PACKED_MODE_SAMPLECOUNT);
               break;
#endif
           default:
               ApplXcpPrint("UNKNOWN LEVEL 1 COMMAND %02X\n", CRO_LEVEL_1_COMMAND_CODE);
               break;
         }
         break;
#endif
         
     case CC_TRANSPORT_LAYER_CMD:
         switch (CRO_TL_SUBCOMMAND) {
           case CC_TL_GET_DAQ_CLOCK_MULTICAST:
               if (ApplXcpDebugLevel >= 2) {
                   ApplXcpPrint("GET_DAQ_CLOCK_MULTICAST counter=%u, cluster=%u\n", CRO_DAQ_CLOCK_MCAST_COUNTER, CRO_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER);
               }
             break;
         }
         break;

    } /* switch */
}

static void  XcpPrintRes(const tXcpCto* pCmd) {

    if (CRM_CMD == PID_ERR) {
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
        ApplXcpPrint("<- ERROR: %02Xh - %s\n", CRM_ERR, e );
    }
    else {
        switch (CRO_CMD) {

        case CC_CONNECT:
            ApplXcpPrint("<- version=%02Xh/%02Xh, maxcro=%u, maxdto=%u, resource=%02X, mode=%u\n",
                CRM_CONNECT_PROTOCOL_VERSION, CRM_CONNECT_TRANSPORT_VERSION, CRM_CONNECT_MAX_CTO_SIZE, CRM_CONNECT_MAX_DTO_SIZE, CRM_CONNECT_RESOURCE,  CRM_CONNECT_COMM_BASIC);
            break;

        case CC_GET_COMM_MODE_INFO:
            ApplXcpPrint("<- version=%02Xh, opt=%u, queue=%u, max_bs=%u, min_st=%u\n",
                CRM_GET_COMM_MODE_INFO_DRIVER_VERSION, CRM_GET_COMM_MODE_INFO_COMM_OPTIONAL, CRM_GET_COMM_MODE_INFO_QUEUE_SIZE, CRM_GET_COMM_MODE_INFO_MAX_BS, CRM_GET_COMM_MODE_INFO_MIN_ST);
            break;

        case CC_GET_STATUS:
            ApplXcpPrint("<- sessionstatus=%02Xh, protectionstatus=%02Xh\n", CRM_GET_STATUS_STATUS, CRM_GET_STATUS_PROTECTION);
            break;

        case CC_GET_ID:
            ApplXcpPrint("<- mode=%u,len=%u\n", CRM_GET_ID_MODE, CRM_GET_ID_LENGTH);
            break;

        case CC_UPLOAD:
            if (ApplXcpDebugLevel >= 3) {
                ApplXcpPrint("<- data=");
                for (int i = 0; i < CRO_UPLOAD_SIZE; i++) {
                    ApplXcpPrint("%02Xh ", CRM_UPLOAD_DATA[i]);
                }
                ApplXcpPrint("\n");
            }
            break;

        case CC_SHORT_UPLOAD:
            if (ApplXcpDebugLevel >= 3) {
                ApplXcpPrint("<- data=");
                for (int i = 0; i < (vuint16)CRO_SHORT_UPLOAD_SIZE; i++) {
                    ApplXcpPrint("%02Xh ", CRM_SHORT_UPLOAD_DATA[i]);
                }
                ApplXcpPrint("\n");
            }
            break;

#ifdef XCP_ENABLE_CAL_PAGE
        case CC_GET_CAL_PAGE:
            ApplXcpPrint("<- page=%u\n", CRM_GET_CAL_PAGE_PAGE);
            break;
#endif

#ifdef XCP_ENABLE_CHECKSUM 
        case CC_BUILD_CHECKSUM:
            ApplXcpPrint("<- sum=%08Xh\n", CRM_BUILD_CHECKSUM_RESULT);
            break;
#endif

        case CC_GET_DAQ_RESOLUTION_INFO:
            ApplXcpPrint("<- mode=%02Xh, , ticks=%02Xh\n", CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE, CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS);
            break;

        case CC_GET_DAQ_PROCESSOR_INFO:
            ApplXcpPrint("<- min=%u, max=%u, events=%u, keybyte=%02Xh, properties=%02Xh\n", CRM_GET_DAQ_PROCESSOR_INFO_MIN_DAQ, CRM_GET_DAQ_PROCESSOR_INFO_MAX_DAQ, CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT, CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE, CRM_GET_DAQ_PROCESSOR_INFO_PROPERTIES);
            break;

        case CC_GET_DAQ_EVENT_INFO:
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0106
            ApplXcpPrint("<- 0xFF properties=%02Xh, unit=%u, cycle=%u, sampleCount=%u, size=%u\n", CRM_GET_DAQ_EVENT_INFO_PROPERTIES, CRM_GET_DAQ_EVENT_INFO_TIME_UNIT, CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE, CRM_GET_DAQ_EVENT_INFO_SAMPLECOUNT, CRM_GET_DAQ_EVENT_INFO_SIZE);
#else
            ApplXcpPrint("<- 0xFF properties=%02Xh, unit=%u, cycle=%u\n", CRM_GET_DAQ_EVENT_INFO_PROPERTIES, CRM_GET_DAQ_EVENT_INFO_TIME_UNIT, CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE);
#endif
            break;

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
        case CC_GET_DAQ_CLOCK:
        getDaqClockMulticast:
            if (ApplXcpDebugLevel >= 2) {
                if (gXcp.SessionStatus & SS_LEGACY_MODE) {
                    ApplXcpPrint("<- t=%u", CRM_GET_DAQ_CLOCK_TIME);
                }
                else {
                    if (CRM_GET_DAQ_CLOCK_PAYLOAD_FMT == 0x01) { // CRM_GET_DAQ_CLOCK_PAYLOAD_FMT
                        ApplXcpPrint("<- t=%u %u", CRM_GET_DAQ_CLOCK_TIME, CRM_GET_DAQ_CLOCK_SYNC_STATE);
                    }
                    else {
                        ApplXcpPrint("<- t=%08X.%08X %u", (vuint32)(CRM_GET_DAQ_CLOCK_TIME64>>32), (vuint32)(CRM_GET_DAQ_CLOCK_TIME64), CRM_GET_DAQ_CLOCK_SYNC_STATE64);
                    }
                }
                if (CRM_CMD == PID_EV) {
                    ApplXcpPrint(" event\n");
                }
                else {
                    ApplXcpPrint("\n");
                }
            }
            break;

        case CC_TIME_CORRELATION_PROPERTIES:
            ApplXcpPrint("<- config=%02Xh, clocks=%02Xh, state=%02Xh, info=%02Xh, clusterId=%u\n", 
                CRM_TIME_SYNC_PROPERTIES_SLAVE_CONFIG, CRM_TIME_SYNC_PROPERTIES_OBSERVABLE_CLOCKS, CRM_TIME_SYNC_PROPERTIES_SYNC_STATE, CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO, CRM_TIME_SYNC_PROPERTIES_CLUSTER_ID );
            break;
#endif

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
        case CC_LEVEL_1_COMMAND:
            switch (CRO_LEVEL_1_COMMAND_CODE) {

            case CC_GET_VERSION:
                ApplXcpPrint("<- protocol layer version: major=%02Xh/minor=%02Xh, transport layer version: major=%02Xh/minor=%02Xh\n",
                    CRM_GET_VERSION_PROTOCOL_VERSION_MAJOR,
                    CRM_GET_VERSION_PROTOCOL_VERSION_MINOR,
                    CRM_GET_VERSION_TRANSPORT_VERSION_MAJOR,
                    CRM_GET_VERSION_TRANSPORT_VERSION_MINOR);
                break;

#ifdef XCP_ENABLE_PACKED_MODE
            case CC_GET_DAQ_LIST_PACKED_MODE:
                ApplXcpPrint("<- mode = %u\n", CRM_GET_DAQ_LIST_PACKED_MODE_MODE);
                break;
#endif
            }
            break;
#endif

        case CC_TRANSPORT_LAYER_CMD:
            switch (CRO_TL_SUBCOMMAND) {
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
            case CC_TL_GET_DAQ_CLOCK_MULTICAST:
                goto getDaqClockMulticast;
            case CC_TL_GET_SLAVE_ID:
                break;
#endif
            }
            break;

        case CC_START_STOP_SYNCH:
            ApplXcpPrint("<- OK (DaqStartClock = %gs)\n", (double)gXcp.DaqStartClock64 / XCP_TIMESTAMP_TICKS_S);
            break;

        default:
            if (ApplXcpDebugLevel >= 2) {
                ApplXcpPrint("<- OK\n");
            }
            break;

        } /* switch */
    }
}


static void XcpPrintDaqList( vuint16 daq )
{
  int i,e;
    
  if (daq>=gXcp.Daq.DaqCount) return;

  ApplXcpPrint("DAQ %u:\n",daq);
  ApplXcpPrint(" eventchannel=%04Xh,",DaqListEventChannel(daq));
  ApplXcpPrint(" firstOdt=%u,",DaqListFirstOdt(daq));
  ApplXcpPrint(" lastOdt=%u,",DaqListLastOdt(daq));
  ApplXcpPrint(" flags=%02Xh,",DaqListFlags(daq));
#ifdef XCP_ENABLE_PACKED_MODE
  ApplXcpPrint(" sampleCount=%u\n",DaqListSampleCount(daq)); 
#endif
  for (i=DaqListFirstOdt(daq);i<=DaqListLastOdt(daq);i++) {
    ApplXcpPrint("  ODT %u (%u):",i-DaqListFirstOdt(daq),i);
    ApplXcpPrint(" firstOdtEntry=%u, lastOdtEntry=%u, size=%u:\n", DaqListOdtFirstEntry(i), DaqListOdtLastEntry(i),DaqListOdtSize(i));
    for (e=DaqListOdtFirstEntry(i);e<=DaqListOdtLastEntry(i);e++) {
      ApplXcpPrint("   %08X,%u\n",OdtEntryAddr(e), OdtEntrySize(e));
    }
  } /* j */
} 

  
#endif /* XCP_ENABLE_TESTMODE */


