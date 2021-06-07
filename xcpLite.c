

/*****************************************************************************
| File: 
|   xcpLite.c
|   Version 2.0 7.6.2021
|
|  Description:   
|    Partial and leightweight implementation of the ASAM XCP Protocol Layer V1.4
|    C and C++ target support
|
|  Limitations:
|     - Only XCP on UDP on 32/64 bit Intel platforms (no 8 bit and 16 bit CPUs, no Motorola)
|     - No misra compliance, warning free on VS 2019
|     - Overall number of ODTs limited to 64K
|     - Overall number of ODT entries is limited to 64K
|     - Fixed DAQ+ODT 2 byte DTO header
|     - Fixed 32 bit DAQ time stamp
|     - Only dynamic DAQ list allocation supported
|     - Resume not supported
|     - Overload indication by event is not supported
|     - DAQ does not support address extensions and prescaler
|     - DAQ list and event channel prioritization is not supported
|     - ODT optimization not supported
|     - Interleaved communication mode is not supported
|     - Seed & key is not supported
|     - Flash programming is not supported
|     - Calibration pages are not supported

|
|  More features, more transport layers (CAN, FlexRay) and platform support, misra compliance 
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
|  No limitations and full compliance are available with the commercial version 
|  from Vector Informatik GmbH, please contact Vector
|***************************************************************************/

#include "xcpLite.h"

/* External dependencies */
#include "xcpAppl.h"


/***************************************************************************/
/* Prototypes for local functions                                          */
/***************************************************************************/

#if defined ( XCP_ENABLE_TESTMODE )
static void XcpPrintCmd(const tXcpCto* pCmd);
static void XcpPrintRes(const tXcpCto* pCmd);
static void XcpPrintDaqList(vuint16 daq);
#endif



/****************************************************************************/
/* Global data                                                               */
/****************************************************************************/

tXcpData gXcp; 



/****************************************************************************/
/* Macros                                                                   */
/****************************************************************************/

#define error(e) { err=(e); goto negative_response; }
#define check_error(e) { err=(e); if (err!=0) { goto negative_response; } }

#define isConnected() (gXcp.SessionStatus& SS_CONNECTED)
#define isDaqRunning() (gXcp.SessionStatus& SS_DAQ)



/****************************************************************************/
/* Status                                                                   */
/****************************************************************************/

vuint8 XcpIsConnected(void) {
    return isConnected();
}

vuint8 XcpIsDaqRunning(void) {
    return isDaqRunning();
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
  s = ( gXcp.Daq.DaqCount * sizeof(tXcpDaqList ) + 
      ( gXcp.Daq.OdtCount *sizeof(tXcpOdt) ) + 
      ( gXcp.Daq.OdtEntryCount * ( sizeof(vuint8*) + sizeof(vuint8) ) ) );
  
  if (s>=XCP_DAQ_MEM_SIZE) return CRC_MEMORY_OVERFLOW;
  
  gXcp.pOdt = (tXcpOdt*)&gXcp.Daq.u.DaqList[gXcp.Daq.DaqCount];
  gXcp.pOdtEntryAddr = (vuint32*)&gXcp.pOdt[gXcp.Daq.OdtCount];
  gXcp.pOdtEntrySize = (vuint8*)&gXcp.pOdtEntryAddr[gXcp.Daq.OdtEntryCount]; 
  

  #if defined ( XCP_ENABLE_TESTMODE )
    if ( ApplXcpDebugLevel >= 2) ApplXcpPrint("[XcpAllocMemory] %u of %u Bytes used\n",s,XCP_DAQ_MEM_SIZE );
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
    OdtEntryAddr(gXcp.WriteDaqOdtEntry) = addr;
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
void  XcpStartAllSelectedDaq(void)
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
void  XcpStopAllSelectedDaq(void)
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

static void XcpEvent_(unsigned int event, vuint8* base, unsigned int clock)
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
      for (hs=6,odt=DaqListFirstOdt(daq);odt<=DaqListLastOdt(daq);hs=2,odt++)  { 
                      
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
        if (hs==6)  {
            *((vuint32*)&d0[2]) = clock;
        }

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

void XcpEventAt(vuint16 event, vuint32 clock) {
    if ((gXcp.SessionStatus & (vuint8)SS_DAQ) == 0) return; // DAQ not running
    XcpEvent_(event, ApplXcpGetBaseAddr(), clock);
}

void XcpEventExt(vuint16 event, vuint8* base) {
    if ((gXcp.SessionStatus & (vuint8)SS_DAQ) == 0) return; // DAQ not running
    XcpEvent_(event, base, ApplXcpGetClock());
}

void XcpEvent(vuint16 event) {
    if ((gXcp.SessionStatus & (vuint8)SS_DAQ) == 0) return; // DAQ not running
    XcpEvent_(event, ApplXcpGetBaseAddr(), ApplXcpGetClock());
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

      if (!(gXcp.SessionStatus & SS_CONNECTED)) { // Must be connected
#ifdef XCP_ENABLE_TESTMODE
          if (ApplXcpDebugLevel >= 1) ApplXcpPrint("Command ignored because not in connected state, no response sent!\n");
#endif
          return;
      }

#ifdef XCP_ENABLE_TESTMODE
      if (ApplXcpDebugLevel >= 1) XcpPrintCmd(pCmd);
#endif

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
                      if (!ApplXcpGetA2LFilename(&gXcp.Mta, &CRM_GET_ID_LENGTH,0)) error(CRC_OUT_OF_RANGE);
                      break;
#endif
#ifdef XCP_ENABLE_A2L_UPLOAD
                  case IDT_ASAM_UPLOAD:
                      if (!ApplXcpReadA2LFile(&gXcp.Mta, &CRM_GET_ID_LENGTH)) error(CRC_ACCESS_DENIED);
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
          }

#endif /* XCP_ENABLE_CHECKSUM */


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
                CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE = XCP_TIMESTAMP_UNIT | DAQ_TIMESTAMP_FIXED | (vuint8)sizeof(vuint32);
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
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0160
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
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0140
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

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0130
            case CC_TIME_CORRELATION_PROPERTIES:
            {
              if ((CRO_TIME_SYNC_PROPERTIES_SET_PROPERTIES & TIME_SYNC_SET_PROPERTIES_RESPONSE_FMT) >= 1) { // set extended format
                  #if defined ( XCP_ENABLE_TESTMODE )
                  if (ApplXcpDebugLevel >= 2) ApplXcpPrint("  Timesync extended mode activated\n");
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
              
#if !defined(XCP_ENABLE_PTP)

              CRM_TIME_SYNC_PROPERTIES_SLAVE_CONFIG = SLAVE_CONFIG_RESPONSE_FMT_ADVANCED | SLAVE_CONFIG_DAQ_TS_SLAVE | SLAVE_CONFIG_TIME_SYNC_BRIDGE_NONE;  // SLAVE_CONFIG_RESPONSE_FMT_LEGACY | SLAVE_CONFIG_DAQ_TS_SLAVE | SLAVE_CONFIG_TIME_SYNC_BRIDGE_NONE;
              CRM_TIME_SYNC_PROPERTIES_OBSERVABLE_CLOCKS = SLAVE_CLOCK_FREE_RUNNING | SLAVE_GRANDM_CLOCK_NONE | ECU_CLOCK_NONE;
              CRM_TIME_SYNC_PROPERTIES_SYNC_STATE = SLAVE_CLOCK_STATE_FREE_RUNNING;
              CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO = CLOCK_INFO_SLAVE; // | CLOCK_INFO_SLAVE_GRANDM | CLOCK_INFO_RELATION | CLOCK_INFO_ECU | CLOCK_INFO_ECU_GRANDM;
              CRM_TIME_SYNC_PROPERTIES_RESERVED = 0x0;
              CRM_TIME_SYNC_PROPERTIES_CLUSTER_ID = gXcp.ClusterId;
              if (CRO_TIME_SYNC_PROPERTIES_GET_PROPERTIES_REQUEST & TIME_SYNC_GET_PROPERTIES_GET_CLK_INFO) { // check whether MTA based upload is requested
                  if (ApplXcpDebugLevel >= 2) ApplXcpPrint("  SLAVE CLOCK info on MTA\n");
                 gXcp.Mta = (vuint8*)&gXcp.SlaveClockInfo;
              }

#else

              //CRM_TIME_SYNC_PROPERTIES_SLAVE_CONFIG = 0; // TIME_SYNC_BRIDGE = 0, DAQ_TS_RELATION = 0 XCP slave clock, RESPONSE_FMT = 0 - GET_DAQ_CLOCK response is always in legacy mode
              CRM_TIME_SYNC_PROPERTIES_SLAVE_CONFIG = 1; // TIME_SYNC_BRIDGE = 0, DAQ_TS_RELATION = 0 - XCP slave clock, RESPONSE_FMT = 1 - GET_DAQ_CLOCK response in extended format, GET_DAQ_CLOCK_MULTICAST response with EV_TIME_SYNC extended format
              //CRM_TIME_SYNC_PROPERTIES_OBSERVABLE_CLOCKS = 0; // XCP_SLV_CLK = 0 - free running observable, GRANDM_CLK = 0, ECU_CLK = 0
              CRM_TIME_SYNC_PROPERTIES_OBSERVABLE_CLOCKS = 1; // XCP_SLV_CLK = 1 - slave clock synchronized and observable, GRANDM_CLK = 0, ECU_CLK = 0
              CRM_TIME_SYNC_PROPERTIES_SYNC_STATE = 0x01; // SLV_CLK_SYNC_STATE = 1 - slave clock synchronised to grandmaster
              //CRM_TIME_SYNC_PROPERTIES_SYNC_STATE = 0x00; // SLV_CLK_SYNC_STATE = 0 - slave synchronising in progress to grandmaster
              CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO = 0x01 | 0x02 | 0x04; // SLC_CLK_INFO = 1 - info available in upload, GRANM_CLK_INFO = 1 - available, CLK_RELATION = 1 - available
              CRM_TIME_SYNC_PROPERTIES_RESERVED = 0x0;
              CRM_TIME_SYNC_PROPERTIES_CLUSTER_AFFILIATION = gXcp.ClusterId; // clusterAffiliation;
              // check whether MTA based upload is requested
              if (CRO_TIME_SYNC_PROPERTIES_GET_PROPERTIES & TIME_SYNC_GET_PROPERTIES_SLV_CLK_INFO) {
                  unsigned char buf[sizeof(T_CLOCK_INFORMATION) + sizeof(T_CLOCK_RELATION) + sizeof(T_CLOCK_INFORMATION_GRANDM)];
                  unsigned char* p = &buf[0];
                  if (CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO & TIME_SYNC_GET_PROPERTIES_SLV_CLK_INFO) {
                      ApplXcpPrint("  -> SLV CLK info on MTA\n");
                      memcpy(p, (unsigned char*)&gXcp.XcpSlaveClockInfo, sizeof(T_CLOCK_INFORMATION));
                      p += sizeof(T_CLOCK_INFORMATION);
                  }

                  if (CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO & TIME_SYNC_GET_PROPERTIES_GRANDM_CLK_INFO) {
                      ApplXcpPrint("  -> GRANDM CLK info on MTA\n");
                      memcpy(p, (unsigned char*)&gXcp.GrandmasterClockInfo, sizeof(T_CLOCK_INFORMATION_GRANDM));
                      p += sizeof(T_CLOCK_INFORMATION_GRANDM);
                  }

                  if (CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO & TIME_SYNC_GET_PROPERTIES_CLK_RELATION) {
                      ApplXcpPrint("  -> CLK RELATION info on MTA\n");
                      ApplXcpGetClock();
                      gXcp.SlvGrandmClkRelationInfo.timestampLocal = ApplXcpGetClock64();
                      gXcp.SlvGrandmClkRelationInfo.timestampOrigin = gXcp.SlvGrandmClkRelationInfo.timestampLocal;
                      memcpy(p, (unsigned char*)&gXcp.SlvGrandmClkRelationInfo, sizeof(T_CLOCK_RELATION));
                      p += sizeof(T_CLOCK_RELATION);
                  }

                  if (CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO & TIME_SYNC_GET_PROPERTIES_ECU_CLK_INFO) {
                      error(CRC_OUT_OF_RANGE)
                  }

                  if (CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO & TIME_SYNC_GET_PROPERTIES_ECU_GRANDM_CLK_INFO) {
                      error(CRC_OUT_OF_RANGE)
                  }

                  gXcp.Mta = (vuint8*)buf;
              }
#endif

            }
            break; 
#endif

          case CC_TRANSPORT_LAYER_CMD:

              switch (CRO_TL_SUBCOMMAND) {

#ifdef XCP_ENABLE_MULTICAST
              case CC_TL_GET_DAQ_CLOCK_MULTICAST: 
              {
                  vuint16 clusterId = CRO_TL_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER;
                  vuint8 counter = CRO_TL_DAQ_CLOCK_MCAST_COUNTER;
                  if (gXcp.ClusterId != clusterId) {
#if defined ( XCP_ENABLE_TESTMODE )
                      if (ApplXcpDebugLevel >= 1) ApplXcpPrint("  GET_DAQ_CLOCK_MULTICAST from cluster id %u ignored\n",clusterId);
#endif                 
                      return; // Ignore broadcasts from foreign masters
                  }
                  CRM_GET_DAQ_CLOCK_TRIGGER_INFO = 0x18 + 0x02; // TIME_OF_SAMPLING (Bitmask 0x18, 3 - Sampled on reception) + TRIGGER_INITIATOR ( Bitmask 0x07, 2 - GET_DAQ_CLOCK_MULTICAST)
                  CRM_CMD = PID_EV;
                  CRM_EVENTCODE = EVC_TIME_SYNC;
                  goto getDaqClockMulticast;
              }
#endif

              case CC_TL_GET_SLAVE_ID:
              default: /* unknown transport layer command */
                  error(CRC_CMD_UNKNOWN);
              }
              break;

          case CC_GET_DAQ_CLOCK:
          {
              CRM_GET_DAQ_CLOCK_RES1 = 0x00; // Placeholder for event code
              CRM_GET_DAQ_CLOCK_TRIGGER_INFO = 0x18; // TIME_OF_SAMPLING (Bitmask 0x18, 3 - Sampled on reception) 
#ifdef XCP_ENABLE_MULTICAST
              getDaqClockMulticast:
#endif
              CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = 0x01; // FMT_XCP_SLV = size of payload is DWORD
              gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN;
              if (gXcp.SessionStatus & SS_LEGACY_MODE) {
                  // Legacy format
                  gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN;
                  CRM_GET_DAQ_CLOCK_TIME = ApplXcpGetClock();
              }
              else {
                  // Extended format
#ifdef XCP_DAQ_CLOCK_64BIT
                  gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN + 5;
                  CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = 0x2;// FMT_XCP_SLV = size of payload is DLONG
                  CRM_GET_DAQ_CLOCK_TIME64 = ApplXcpGetClock64();
                  CRM_GET_DAQ_CLOCK_SYNC_STATE64 = 1;
#else
                  gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN + 1;
                  CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = 0x01; // FMT_XCP_SLV = size of payload is DWORD
                  CRM_GET_DAQ_CLOCK_TIME = ApplXcpGetClock();
                  CRM_GET_DAQ_CLOCK_SYNC_STATE = 1;
#endif
              }
          }
          break; 

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0140
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
#endif

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
   
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0130
  // UUID is contructed will be updated to match the MAC address of the XCP slave
  unsigned char uuid1[8] = XCPSIM_SLAVE_UUID;
  memcpy(&gXcp.SlaveClockInfo.UUID[0], &uuid1[0], 8);
  gXcp.SlaveClockInfo.timestampTicks = XCP_TIMESTAMP_TICKS;
  gXcp.SlaveClockInfo.timestampUnit = XCP_TIMESTAMP_UNIT;
  gXcp.SlaveClockInfo.stratumLevel = 255UL; // STRATUM_LEVEL_UNKNOWN
#ifdef XCP_DAQ_CLOCK_64BIT
  gXcp.SlaveClockInfo.nativeTimestampSize = 8UL; // NATIVE_TIMESTAMP_SIZE_DLONG;
  gXcp.SlaveClockInfo.valueBeforeWrapAround = 0xFFFFFFFFFFFFFFFFULL;
#else
  gXcp.SlaveClockInfo.nativeTimestampSize = 4UL; // NATIVE_TIMESTAMP_SIZE_LONG;
  gXcp.SlaveClockInfo.valueBeforeWrapAround = 0xFFFFFFFFULL;
#endif

#ifdef XCP_ENABLE_PTP

  // Grandmaster clock description:
  // UUID will later be updated with the details received from the PTP grandmaster
  unsigned char uuid2[8] = XCPSIM_SLAVE_XL_UUID;
  memcpy(&gXcp.XcpSlaveClockInfo.UUID[0], &uuid2[0], 8);
  gXcp.GrandmasterClockInfo.timestampTicks = XCP_TIMESTAMP_TICKS;
  gXcp.GrandmasterClockInfo.timestampUnit = XCP_TIMESTAMP_UNIT;
  gXcp.GrandmasterClockInfo.stratumLevel = 0; // GPS
  gXcp.GrandmasterClockInfo.nativeTimestampSize = 8UL; // NATIVE_TIMESTAMP_SIZE_DLONG;
  gXcp.GrandmasterClockInfo.valueBeforeWrapAround = 0xFFFFFFFFFFFFFFFFULL;
  gXcp.GrandmasterClockInfo.epochOfGrandmaster = 0UL; // EPOCH_TAI;

  // If the slave clock is PTP synchronized, both origin and local timestamps are considered to be the same.
  // Timestamps will be updated to the current value pair
  gXcp.SlvGrandmClkRelationInfo.timestampLocal = 0;
  gXcp.SlvGrandmClkRelationInfo.timestampOrigin = 0;
#endif
#endif

  /* Initialize the session status */
  gXcp.SessionStatus = 0;
}


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
        {
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

#if defined ( XCP_ENABLE_CHECKSUM )
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

     case CC_TIME_CORRELATION_PROPERTIES:
         ApplXcpPrint("GET_TIME_CORRELATION_PROPERTIES set=%02Xh, request=%u, clusterId=%u\n", CRO_TIME_SYNC_PROPERTIES_SET_PROPERTIES, CRO_TIME_SYNC_PROPERTIES_GET_PROPERTIES_REQUEST, CRO_TIME_SYNC_PROPERTIES_CLUSTER_ID );
         break;

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0140
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
             ApplXcpPrint("GET_DAQ_CLOCK_MULTICAST counter=%u, cluster=%u\n", CRO_TL_DAQ_CLOCK_MCAST_COUNTER, CRO_TL_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER);
             break;
         }
         break;

    } /* switch */
}

static void  XcpPrintRes(const tXcpCto* pCmd) {

    if (CRM_CMD == PID_ERR) {
        char* e;
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
            if (ApplXcpDebugLevel >= 2) {
                ApplXcpPrint("<- data=");
                for (int i = 0; i < CRO_UPLOAD_SIZE; i++) {
                    ApplXcpPrint("%02Xh ", CRM_UPLOAD_DATA[i]);
                }
                ApplXcpPrint("\n");
            }
            break;

        case CC_SHORT_UPLOAD:
            if (ApplXcpDebugLevel >= 2) {
                ApplXcpPrint("<- data=");
                for (int i = 0; i < (vuint16)CRO_SHORT_UPLOAD_SIZE; i++) {
                    ApplXcpPrint("%02Xh ", CRM_SHORT_UPLOAD_DATA[i]);
                }
                ApplXcpPrint("\n");
            }
            break;

#if defined ( XCP_ENABLE_CHECKSUM )
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
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0160
            ApplXcpPrint("<- 0xFF properties=%02Xh, unit=%u, cycle=%u, sampleCount=%u, size=%u\n", CRM_GET_DAQ_EVENT_INFO_PROPERTIES, CRM_GET_DAQ_EVENT_INFO_TIME_UNIT, CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE, CRM_GET_DAQ_EVENT_INFO_SAMPLECOUNT, CRM_GET_DAQ_EVENT_INFO_SIZE);
#else
            ApplXcpPrint("<- 0xFF properties=%02Xh, unit=%u, cycle=%u\n", CRM_GET_DAQ_EVENT_INFO_PROPERTIES, CRM_GET_DAQ_EVENT_INFO_TIME_UNIT, CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE);
#endif
            break;

        case CC_GET_DAQ_CLOCK:
        getDaqClockMulticast:
            if (ApplXcpDebugLevel >= 2) {
                if (gXcp.SessionStatus & SS_LEGACY_MODE) {
                    ApplXcpPrint("<- t=%ul (%gs)", CRM_GET_DAQ_CLOCK_TIME, (double)CRM_GET_DAQ_CLOCK_TIME / (1000.0 * XCP_TIMESTAMP_TICKS_MS));
                }
                else {
                    if (CRM_GET_DAQ_CLOCK_PAYLOAD_FMT == 0x01) { // CRM_GET_DAQ_CLOCK_PAYLOAD_FMT
                        ApplXcpPrint("<- t=%ul (32B %gs) %u", CRM_GET_DAQ_CLOCK_TIME, (double)CRM_GET_DAQ_CLOCK_TIME / (1000.0 * XCP_TIMESTAMP_TICKS_MS), CRM_GET_DAQ_CLOCK_SYNC_STATE);
                    }
                    else {
                        ApplXcpPrint("<- t=%llull (64B %gs) %u", CRM_GET_DAQ_CLOCK_TIME64, (double)CRM_GET_DAQ_CLOCK_TIME64 / (1000.0 * XCP_TIMESTAMP_TICKS_MS), CRM_GET_DAQ_CLOCK_SYNC_STATE64);
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

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0140
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
            case CC_TL_GET_DAQ_CLOCK_MULTICAST:
                goto getDaqClockMulticast;
            case CC_TL_GET_SLAVE_ID:
                break;
            }

        case CC_START_STOP_SYNCH:
            ApplXcpPrint("<- OK (DaqStartClock=%gs)\n", (double)gXcp.DaqStartClock64 / (1000.0 * XCP_TIMESTAMP_TICKS_MS));
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


