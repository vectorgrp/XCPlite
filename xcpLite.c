

/*****************************************************************************
| File: 
|   xcpLite.c
|
|  Description:   
|    Implementation of the ASAM XCP Protocol Layer V1.4
|    
|    C and C++ target support
|    Lite Version (see feature list and restrictions below)
|
|  Features:
|     - XCP on UDP only
|     - Optimized transmit queue for multi threaded, no thread lock and zero copy data acquisition
|     - Supports DAQ_PACKED_MODE ELEMENT_GROUPED STS_LAST MANDATORY
|     - Supports PTP
|     - Optional integrated UDP stack
|     - Optional integrated A2L generator
|
|  Limitations:
|     - Only XCP on UDP on 32 bit x86 Linux and Windows platforms
|     - 8 bit and 16 bit CPUs are not supported
|     - No misra compliance
|     - Number of events limited to 255
|     - Number of DAQ lists limited to 256
|     - Overall number of ODTs limited to 64K
|     - No jumbo frame support, MAX_DTO < MTU < 1400
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
|     - Calibration pages are not supported
|     - Checksum is not supported
|     - Event messages (SERV_TEXT) are not supported
|     - User commands are not supported
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
|  No limitations and full compliance are available with the commercial version 
|  from Vector Informatik GmbH, please contact Vector
|***************************************************************************/


/***************************************************************************/
/* Include files                                                           */
/***************************************************************************/

#include "xcpLite.h"
#include "xcpAppl.h"



/****************************************************************************/
/* Macros                                                                   */
/****************************************************************************/

#define error(e) { err=(e); goto negative_response; }
#define check_error(e) { err=(e); if (err!=0) { goto negative_response; } }
#define error1(e,b1) { err=(e); CRM_BYTE(2)=(b1); gXcp.CrmLen=3; goto negative_response1; }
#define error2(e,b1,b2) { err=(e); CRM_BYTE(2)=(b1); CRM_BYTE(3)=(b2); gXcp.CrmLen=4; goto negative_response1; }

#define XCP_WRITE_BYTE_2_ADDR(addr, data)           *(addr) = (data) 
#define XCP_READ_BYTE_FROM_ADDR(addr)               *(addr) 
  
#define XcpSetMta(p,e) (gXcp.Mta = (p)) 


/****************************************************************************/
/* Global data                                                               */
/****************************************************************************/

tXcpData gXcp; 

const vuint8 gXcpSlaveId[kXcpSlaveIdLength] = kXcpSlaveIdString; // Name of the A2L file on local PC for auto detection

#if defined ( XCP_ENABLE_TESTMODE )
volatile vuint8 gXcpDebugLevel = XCP_DEBUG_LEVEL;
#endif


/***************************************************************************/
/* Prototypes for local functions                                          */
/***************************************************************************/

static vuint8 XcpWriteMta( vuint8 size, const BYTEPTR data );
static vuint8 XcpReadMta( vuint8 size, BYTEPTR data );
static void XcpFreeDaq( void );
static vuint8 XcpAllocMemory( void );
static vuint8 XcpAllocDaq( vuint8 daqCount );
static vuint8 XcpAllocOdt( vuint8 daq, vuint8 odtCount );
static vuint8 XcpAllocOdtEntry( vuint8 daq, vuint8 odt, vuint8 odtEntryCount );
static void XcpStartDaq( vuint16 daq );
static void XcpStartAllSelectedDaq( void );
static void XcpStopDaq( vuint16 daq );
static void XcpStopAllSelectedDaq( void );
static void XcpStopAllDaq( void );
#if defined ( XCP_ENABLE_TESTMODE )
static void XcpPrintCmd(const tXcpCto* pCmd);
static void XcpPrintRes(const tXcpCto* pCmd);
#endif



/*****************************************************************************
| NAME:             XcpWriteMta
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: size : number of data bytes.
|                   data : address of data.
| RETURN VALUES:    XCP_CMD_OK, XCP_CMD_DENIED
| DESCRIPTION:      Write n bytes.
|                   Copying of size bytes from data to gXcp.Mta
******************************************************************************/
static vuint8 XcpWriteMta( vuint8 size, const BYTEPTR data )
{

  /* Standard RAM memory write access */
  while ( size > 0 )  {
    XCP_WRITE_BYTE_2_ADDR( gXcp.Mta, *data );
    gXcp.Mta++; 
    data++; 
    size--;
  }
  return XCP_CMD_OK;
  
}

/*****************************************************************************
| NAME:             XcpReadMta
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: size :
|                   data : address of data
| RETURN VALUES:    XCP_CMD_OK
| DESCRIPTION:      Read n bytes.
|                   Copying of size bytes from data to gXcp.Mta
******************************************************************************/
static vuint8 XcpReadMta( vuint8 size, BYTEPTR data )
{
  /* Standard RAM memory read access */
  while (size > 0)  {
    *(data) = XCP_READ_BYTE_FROM_ADDR( gXcp.Mta );
    data++; 
    gXcp.Mta++; 
    size--;
  }
  return XCP_CMD_OK;
}


/****************************************************************************/
/* Data Aquisition Setup                                                    */
/****************************************************************************/


/*****************************************************************************
| NAME:             XcpFreeDaq
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: none
| RETURN VALUES:    none
| DESCRIPTION:      Free all dynamic DAQ lists
******************************************************************************/
static void XcpFreeDaq( void )
{
  gXcp.SessionStatus &= (vuint8)(~SS_DAQ);

  gXcp.Daq.DaqCount = 0;
  gXcp.Daq.OdtCount = 0;
  gXcp.Daq.OdtEntryCount = 0;

  gXcp.pOdt = (tXcpOdt*)0;
  gXcp.pOdtEntryAddr = 0;
  gXcp.pOdtEntrySize = 0;

  memset((BYTEPTR)&gXcp.Daq.u.b[0], 0, (vuint16)XCP_DAQ_MEM_SIZE);  
}

/*****************************************************************************
| NAME:             XcpAllocMemory
| CALLED BY:        XcpAllocDaq, XcpAllocOdt, XcpAllocOdtEntry, XcpInit
| PRECONDITIONS:    none
| INPUT PARAMETERS: none
| RETURN VALUES:    0, CRC_MEMORY_OVERFLOW
| DESCRIPTION:      Allocate Memory for daq,odt,odtEntries and Queue
|                   according to DaqCount, OdtCount and OdtEntryCount
******************************************************************************/
static vuint8 XcpAllocMemory( void )
{
  vuint16 s;
  
  /* Check memory overflow */
  s = (vuint16)( ( gXcp.Daq.DaqCount      *   (vuint8)sizeof(tXcpDaqList)                           ) + 
                 ( gXcp.Daq.OdtCount      *  (vuint16)sizeof(tXcpOdt)                               ) + 
                 ( gXcp.Daq.OdtEntryCount * ( (vuint8)sizeof(DAQBYTEPTR) + (vuint8)sizeof(vuint8) ) )
               );
  
  if (s>=(vuint16)XCP_DAQ_MEM_SIZE) return (vuint8)CRC_MEMORY_OVERFLOW;
  
  gXcp.pOdt = (tXcpOdt*)&gXcp.Daq.u.DaqList[gXcp.Daq.DaqCount];
  gXcp.pOdtEntryAddr = (DAQBYTEPTR*)&gXcp.pOdt[gXcp.Daq.OdtCount];
  gXcp.pOdtEntrySize = (vuint8*)&gXcp.pOdtEntryAddr[gXcp.Daq.OdtEntryCount]; 
  

  #if defined ( XCP_ENABLE_TESTMODE )
    if ( gXcpDebugLevel >= 2) ApplXcpPrint("[XcpAllocMemory] %u/%u Bytes used\n",s,XCP_DAQ_MEM_SIZE );
  #endif

  return (vuint8)0u;
}

/*****************************************************************************
| NAME:             XcpAllocDaq
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: daqCount : 
| RETURN VALUES:    return value of XcpAllocMemory, CRC_SEQUENCE
| DESCRIPTION:      Allocate DAQ list
******************************************************************************/
static vuint8 XcpAllocDaq( vuint8 daqCount )
{
  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
  if ( (gXcp.Daq.OdtCount!=0) || (gXcp.Daq.OdtEntryCount!=0) )
  {
    return (vuint8)CRC_SEQUENCE;
  }
  if( daqCount == 0 )
  {
    return (vuint8)CRC_OUT_OF_RANGE;
  }
  #endif

  gXcp.Daq.DaqCount = daqCount;

  return XcpAllocMemory();
  
}

/*****************************************************************************
| NAME:             XcpAllocOdt
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: daq      : 
|                   odtCount :
| RETURN VALUES:    return value of XcpAllocMemory,
|                   CRC_SEQUENCE, CRC_MEMORY_OVERFLOW
| DESCRIPTION:      Allocate all ODTs in a DAQ list
******************************************************************************/
static vuint8 XcpAllocOdt( vuint8 daq, vuint8 odtCount )
{
  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
  if ( (gXcp.Daq.DaqCount==0) || (gXcp.Daq.OdtEntryCount!=0) )
  {
    return (vuint8)CRC_SEQUENCE;
  }
  if( odtCount == 0 )
  {
    return (vuint8)CRC_OUT_OF_RANGE;
  }
  #endif

  gXcp.Daq.u.DaqList[daq].firstOdt = gXcp.Daq.OdtCount;
  gXcp.Daq.OdtCount = (vuint16)(gXcp.Daq.OdtCount+odtCount);
  gXcp.Daq.u.DaqList[daq].lastOdt = (vuint16)(gXcp.Daq.OdtCount-1);

  return XcpAllocMemory();
}

static void XcpAdjustOdtSize(vuint16 daq, vuint16 odt, vuint8 size) {
    vuint16 sc = DaqListSampleCount(daq);
    if (sc == 0) sc = 1;
    DaqListOdtSize(odt) = (vuint16)(DaqListOdtSize(odt) + size*sc); 
}

/*****************************************************************************
| NAME:             XcpAllocOdtEntry
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: daq :
|                   odt :
|                   odtEntryCount :
| RETURN VALUES:    return value of XcpAllocMemory
| DESCRIPTION:      Allocate all ODT entries
|                   Parameter odt is relative odt number
******************************************************************************/
static vuint8 XcpAllocOdtEntry( vuint8 daq, vuint8 odt, vuint8 odtEntryCount )
{
  int xcpFirstOdt;

  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
  if ( (gXcp.Daq.DaqCount==0) || (gXcp.Daq.OdtCount==0) )
  {
    return (vuint8)CRC_SEQUENCE;
  }
  if (odtEntryCount==0)
  {
    return (vuint8)CRC_OUT_OF_RANGE;
  }
  #endif

  /* Absolute ODT entry count count must fit in a WORD */
  if (gXcp.Daq.OdtEntryCount > (0xFFFFu - odtEntryCount))
  {
    return (vuint8)CRC_MEMORY_OVERFLOW;
  }
  xcpFirstOdt = gXcp.Daq.u.DaqList[daq].firstOdt;
  gXcp.pOdt[xcpFirstOdt+odt].firstOdtEntry = gXcp.Daq.OdtEntryCount;
  gXcp.Daq.OdtEntryCount = (vuint16)(gXcp.Daq.OdtEntryCount+odtEntryCount);
  gXcp.pOdt[xcpFirstOdt + odt].lastOdtEntry = (vuint16)(gXcp.Daq.OdtEntryCount - 1);
  gXcp.pOdt[xcpFirstOdt + odt].size = 0;

  return XcpAllocMemory();
}

static vuint8 XcpAddOdtEntry(vuint32 addr, vuint8 ext, vuint8 size) {
    if ((size == 0) || size > kXcpMaxOdtEntrySize) return CRC_OUT_OF_RANGE;
    if (0 == gXcp.Daq.DaqCount || 0 == gXcp.Daq.OdtCount || 0 == gXcp.Daq.OdtEntryCount) return CRC_DAQ_CONFIG;
    DAQBYTEPTR p = (DAQBYTEPTR)ApplXcpGetPointer(ext, addr);
    OdtEntrySize(gXcp.WriteDaqOdtEntry) = size;
    OdtEntryAddr(gXcp.WriteDaqOdtEntry) = p;
    XcpAdjustOdtSize(gXcp.WriteDaqDaq, gXcp.WriteDaqOdt, size);
    gXcp.WriteDaqOdtEntry++; // Autoincrement to next ODT entry, no autoincrementing over ODTs
    return 0;
}

/*****************************************************************************
| NAME:             XcpStartDaq
| CALLED BY:        XcpCommand, XcpStartAllSelectedDaq
| PRECONDITIONS:    none
| INPUT PARAMETERS: daq :
| RETURN VALUES:    none
| DESCRIPTION:      Start DAQ
******************************************************************************/
static void XcpStartDaq( vuint16 daq )
{
  /* Initialize the DAQ list */
  DaqListFlags(daq) |= (vuint8)DAQ_FLAG_RUNNING;
  gXcp.SessionStatus |= (vuint8)SS_DAQ;
}

/*****************************************************************************
| NAME:             XcpStartAllSelectedDaq
| CALLED BY:        XcpCommand, XcpInit
| PRECONDITIONS:    none
| INPUT PARAMETERS: none
| RETURN VALUES:    none
| DESCRIPTION:      Start all selected DAQs
******************************************************************************/
static void XcpStartAllSelectedDaq(void)
{
  vuint16 daq;

  /* Start all selected DAQs */
  for (daq=0;daq<gXcp.Daq.DaqCount;daq++)  {
    if ( (DaqListFlags(daq) & (vuint8)DAQ_FLAG_SELECTED) != 0 ) {
      XcpStartDaq(daq);
      DaqListFlags(daq) &= (vuint8)(~DAQ_FLAG_SELECTED);
#if defined ( XCP_ENABLE_TESTMODE )
      if (gXcpDebugLevel >= 2) {
          XcpPrintDaqList(daq);
      }
#endif
    }
  }
}

/*****************************************************************************
| NAME:             XcpStopDaq
| CALLED BY:        XcpCommand, XcpStopAllSelectedDaq
| PRECONDITIONS:    none
| INPUT PARAMETERS: daq : 
| RETURN VALUES:    none
| DESCRIPTION:      Stop DAQ
******************************************************************************/
static void XcpStopDaq( vuint16 daq )
{
  vuint8 i;

  DaqListFlags(daq) &= (vuint8)(DAQ_FLAG_DIRECTION|DAQ_FLAG_TIMESTAMP|DAQ_FLAG_NO_PID);

  /* Check if all DAQ lists are stopped */
  for (i=0;i<gXcp.Daq.DaqCount;i++)  {
    if ( (DaqListFlags(i) & (vuint8)DAQ_FLAG_RUNNING) != 0 )  {
      return;
    }
  }

  gXcp.SessionStatus &= (vuint8)(~SS_DAQ);
}

/*****************************************************************************
| NAME:             XcpStopAllSelectedDaq
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: none 
| RETURN VALUES:    none
| DESCRIPTION:      Stop all selected DAQs
******************************************************************************/
static void XcpStopAllSelectedDaq(void)
{
  vuint16 daq;

  for (daq=0;daq<gXcp.Daq.DaqCount;daq++) {
    if ( (DaqListFlags(daq) & (vuint8)DAQ_FLAG_SELECTED) != 0 ) {
      XcpStopDaq(daq);
      DaqListFlags(daq) &= (vuint8)(~DAQ_FLAG_SELECTED);
    }
  }
}

/*****************************************************************************
| NAME:             XcpStopAllDaq
| CALLED BY:        XcpCommand, XcpDisconnect
| PRECONDITIONS:    none
| INPUT PARAMETERS: none 
| RETURN VALUES:    none
| DESCRIPTION:      Stop all DAQs
******************************************************************************/
static void XcpStopAllDaq( void )
{
  for (vuint8 daq=0; daq<gXcp.Daq.DaqCount; daq++) {
    DaqListFlags(daq) &= (vuint8)(DAQ_FLAG_DIRECTION|DAQ_FLAG_TIMESTAMP|DAQ_FLAG_NO_PID);
  }

  gXcp.SessionStatus &= (vuint8)(~SS_DAQ);  
}


/****************************************************************************/
/* Data Aquisition Processor                                                */
/****************************************************************************/


/*****************************************************************************
| NAME:             XcpEvent,XcpEventExt
| CALLED BY:        application, thread save
| PRECONDITIONS:    XCP is initialized 
| INPUT PARAMETERS: event: event channel number to process
| DESCRIPTION:      Measurement data acquisition, sample and transmit measurement date associated to event
******************************************************************************/

void XcpEvent(unsigned int event) {
#if defined ( XCP_ENABLE_WIRINGPI )
    digitalWrite(PI_IO_1, HIGH);
#endif
    XcpEventExt(event, 0);
#if defined ( XCP_ENABLE_WIRINGPI )
    digitalWrite(PI_IO_1, LOW);
#endif
}

void XcpEventExt(unsigned int event, BYTEPTR offset)
{
  BYTEPTR d;
  BYTEPTR d0;
  void* p0;
  unsigned int e,el,odt,daq,hs,n,sc;
  
  if ( (gXcp.SessionStatus & (vuint8)SS_DAQ) == 0 ) return; // DAQ not running
  
  for (daq=0; daq<gXcp.Daq.DaqCount; daq++) {

      if ((DaqListFlags(daq) & (vuint8)DAQ_FLAG_RUNNING) == 0) continue; // DAQ list not active
      if ( DaqListEventChannel(daq) != event ) continue; // DAQ list not associated with this event
      sc = DaqListSampleCount(daq); // Packed mode sample count, 0 if not packed

      for (hs=6,odt=DaqListFirstOdt(daq);odt<=DaqListLastOdt(daq);hs=2,odt++)  { 
                      
        // Get DTO buffer, overrun if not available
        if ((d0 = ApplXcpGetDtoBuffer(&p0, DaqListOdtSize(odt)+hs)) == NULL) {
#if defined ( XCP_ENABLE_TESTMODE )
            if (gXcpDebugLevel >= 1) ApplXcpPrint("DAQ queue overflow! Event %u skipped\n", event);
#endif
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
            *((vuint32*)&d0[2]) = ApplXcpGetClock();
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
                if (sc>1) n *= sc; // packed mode
                memcpy((DAQBYTEPTR)d, offset + (size_t) OdtEntryAddr(e), n);
                d += n;
                e++;
            } // ODT entry
        }

        ApplXcpCommitDtoBuffer(p0);
               
      } /* odt */

  } /* daq */
  
}


/****************************************************************************/
/* Command Processor                                                        */
/****************************************************************************/

/*****************************************************************************
| NAME:             XcpDisconnect
| CALLED BY:        XcpCommand
| PRECONDITIONS:    XCP is initialized
| INPUT PARAMETERS: none 
| RETURN VALUES:    none
| DESCRIPTION:      Stops DAQ and goes to disconnected state
******************************************************************************/
void XcpDisconnect( void )
{
  gXcp.SessionStatus &= (vuint8)(~SS_CONNECTED);
  XcpStopAllDaq();
}


/*****************************************************************************
| NAME:             XcpCommand
| CALLED BY:        XCP Transport Layer
| PRECONDITIONS:    XCP initialized
| INPUT PARAMETERS: pCmd : data of received transport layer CTO message.
| RETURN VALUES:    none
| DESCRIPTION:      Handles incoming XCP commands
******************************************************************************/

void XcpCommand( const vuint32* pCommand )
{
  const tXcpCto* pCmd = (const tXcpCto*) pCommand; 
  vuint8 err;

  // Prepare the default response
  CRM_CMD = 0xFF; /* No Error */
  gXcp.CrmLen = 1; /* Length = 1 */

  // CONNECT ?
  if (CRO_CMD==CC_CONNECT) 
  {
#if defined ( XCP_ENABLE_TESTMODE )
      if (gXcpDebugLevel >= 1) {
          ApplXcpPrint("-> CONNECT mode=%u\n", CRO_CONNECT_MODE);
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
    CRM_CONNECT_MAX_CTO_SIZE = kXcpMaxCTO;
    CRM_CONNECT_MAX_DTO_SIZE = kXcpMaxDTO; 
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
          if (gXcpDebugLevel >= 1) ApplXcpPrint("Command ignored because not in connected state, no response sent!\n");
#endif
          return;
      }

#ifdef XCP_ENABLE_TESTMODE
      if (gXcpDebugLevel >= 1) XcpPrintCmd(pCmd);
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
            CRM_GET_COMM_MODE_INFO_DRIVER_VERSION = XCP_VERSION;
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
                  case IDT_ASAM_NAME:
                      CRM_GET_ID_LENGTH = kXcpSlaveIdLength;
                      XcpSetMta((BYTEPTR)&gXcpSlaveId[0], 0x00);
                      break;
#ifdef XCP_ENABLE_A2L
                  case IDT_ASAM_UPLOAD:
                      {
                        char* p;
                        unsigned int n;
                        if (!ApplXcpReadA2LFile(&p, &n)) error(CRC_ACCESS_DENIED);
                        CRM_GET_ID_LENGTH = (vuint32)n;
                        XcpSetMta(p, 0x00);
                      }
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
              XcpSetMta(ApplXcpGetPointer(CRO_SET_MTA_EXT,CRO_SET_MTA_ADDR),CRO_SET_MTA_EXT);
            }
            break;

          case CC_DOWNLOAD: 
            {
              vuint8 size;
              size = CRO_DOWNLOAD_SIZE;
              if (size>CRO_DOWNLOAD_MAX_SIZE) error(CRC_OUT_OF_RANGE)
              err = XcpWriteMta(size,CRO_DOWNLOAD_DATA);
              if (err == XCP_CMD_PENDING) return; // No response
              if (err == XCP_CMD_DENIED) error(CRC_WRITE_PROTECTED);
              if (err == XCP_CMD_SYNTAX) error(CRC_CMD_SYNTAX);
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
              XcpSetMta(ApplXcpGetPointer(CRO_SHORT_UPLOAD_EXT,CRO_SHORT_UPLOAD_ADDR),CRO_SHORT_UPLOAD_EXT);
              err = XcpReadMta(CRO_SHORT_UPLOAD_SIZE,CRM_SHORT_UPLOAD_DATA);
              gXcp.CrmLen = (vuint8)(CRM_SHORT_UPLOAD_LEN+CRO_SHORT_UPLOAD_SIZE);
              if (err == XCP_CMD_PENDING) return; // No response
              if (err == XCP_CMD_DENIED) error(CRC_ACCESS_DENIED);
            }
            break;

            case CC_GET_DAQ_PROCESSOR_INFO: 
            {
    
              gXcp.CrmLen = CRM_GET_DAQ_PROCESSOR_INFO_LEN;
              CRM_GET_DAQ_PROCESSOR_INFO_MIN_DAQ = 0;          
              CRM_GET_DAQ_PROCESSOR_INFO_MAX_DAQ = (gXcp.Daq.DaqCount); /* dynamic or static */ 
              CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT = (0x00); /* Unknown */    
              CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE = (vuint8)DAQ_HDR_ODT_DAQB; /* DTO identification field type: Relative ODT number, absolute list number (BYTE) */
              CRM_GET_DAQ_PROCESSOR_INFO_PROPERTIES = (vuint8)( DAQ_PROPERTY_CONFIG_TYPE | DAQ_PROPERTY_TIMESTAMP | DAQ_OVERLOAD_INDICATION_PID );
            }
            break;

            case CC_GET_DAQ_RESOLUTION_INFO: 
              {
                gXcp.CrmLen = CRM_GET_DAQ_RESOLUTION_INFO_LEN;
                CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_DAQ = 1;
                CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_STIM = 1;
                CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_DAQ  = (vuint8)kXcpMaxOdtEntrySize;
                CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_STIM = (vuint8)kXcpMaxOdtEntrySize;
                CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE = kXcpDaqTimestampUnit | DAQ_TIMESTAMP_FIXED | (vuint8)sizeof(vuint32);
                CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS = (kXcpDaqTimestampTicksPerUnit);  
              }
              break;
  
            case CC_FREE_DAQ: 
            {
                XcpFreeDaq();
            }
            break;

          case CC_ALLOC_DAQ:
            {
                vuint8 count = (vuint8)CRO_ALLOC_DAQ_COUNT;
                check_error(XcpAllocDaq(count));
            }
            break;

          case CC_ALLOC_ODT:
            {
              vuint8 daq = (vuint8)CRO_ALLOC_ODT_DAQ;
              vuint8 count = CRO_ALLOC_ODT_COUNT;
              if (daq >= gXcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
              check_error( XcpAllocOdt(daq, count) ) 
            }
            break;

          case CC_ALLOC_ODT_ENTRY:
            {
              vuint8 daq = (vuint8)CRO_ALLOC_ODT_ENTRY_DAQ;
              vuint8 odt = CRO_ALLOC_ODT_ENTRY_ODT;
              vuint8 count = CRO_ALLOC_ODT_ENTRY_COUNT;
              if ((daq >= gXcp.Daq.DaqCount) || (odt >= (vuint8)DaqListOdtCount(daq))) error(CRC_OUT_OF_RANGE);
              check_error( XcpAllocOdtEntry(daq, odt, count) ) 
            }
            break;

          case CC_GET_DAQ_LIST_MODE:
            {
              vuint8 daq = (vuint8)CRO_GET_DAQ_LIST_MODE_DAQ;
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
              vuint8 event = (vuint8)(CRO_SET_DAQ_LIST_MODE_EVENTCHANNEL&0xFFu);
              if (daq >= gXcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
              if (CRO_SET_DAQ_LIST_MODE_PRIORITY != 0) error(CRC_OUT_OF_RANGE);  /* Priorization is not supported */
              DaqListEventChannel(daq) = event; 
              DaqListFlags(daq) = CRO_SET_DAQ_LIST_MODE_MODE;
              break;
            }

          case CC_SET_DAQ_PTR: /* Set target for WRITE_DAQ or WRITE_DAQ_MULTIPLE */
            {
              vuint16 daq = CRO_SET_DAQ_PTR_DAQ;
              vuint8 odt = CRO_SET_DAQ_PTR_ODT;
              vuint8 idx = CRO_SET_DAQ_PTR_IDX;
              vuint16 odt0 = (vuint16)(DaqListFirstOdt(daq)+odt); // Absolute odt index
              if ((daq >= gXcp.Daq.DaqCount) || (odt >= DaqListOdtCount(daq)) || (idx >= DaqListOdtEntryCount(odt0))) error(CRC_OUT_OF_RANGE); 
              // Save info for WRITE_DAQ and WRITE_DAQ_MULTIPLE
              gXcp.WriteDaqOdtEntry = (vuint16)(DaqListOdtFirstEntry(odt0)+idx); // Absolute odt entry index
              gXcp.WriteDaqOdt = odt0; // Absolute odt index
              gXcp.WriteDaqDaq = daq; 
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
              if ( (CRO_START_STOP_MODE==1 ) || (CRO_START_STOP_MODE==2) )  {
                DaqListFlags(daq) |= (vuint8)DAQ_FLAG_SELECTED;
                if ( CRO_START_STOP_MODE == 1 ) XcpStartDaq(daq);
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
              if (CRO_START_STOP_MODE==2) { /* stop selected */
                XcpStopAllSelectedDaq();
              } 
              else {
                if (CRO_START_STOP_MODE==1) { /* start selected */
                  XcpStartAllSelectedDaq();
                }
                else { /* CRO_START_STOP_MODE==0 : stop all */
                  XcpStopAllDaq();
                }
              }
  
            }
            break;

#ifdef XCP_ENABLE_PTP
            case CC_TIME_CORRELATION_PROPERTIES:
            {
              if ((CRO_TIME_SYNC_PROPERTIES_SET_PROPERTIES & TIME_SYNC_SET_PROPERTIES_RESP_FORMAT_MASK) >= 1) {
#if defined ( XCP_ENABLE_TESTMODE )
                  if (gXcpDebugLevel >= 1) ApplXcpPrint("Timesync extended mode activated\n");
#endif                 
                gXcp.SessionStatus = (vuint8)(gXcp.SessionStatus & ~SS_LEGACY_MODE);
              }
              
              if (CRO_TIME_SYNC_PROPERTIES_SET_PROPERTIES & TIME_SYNC_SET_PROPERTIES_CLUSTER_AFFILIATION_MASK) {
                gXcp.ClusterId = CRO_TIME_SYNC_PROPERTIES_CLUSTER_AFFILIATION;
              }

              // Time sync bride is not supported
              if (CRO_TIME_SYNC_PROPERTIES_SET_PROPERTIES & TIME_SYNC_SET_PROPERTIES_TIME_SYNC_BRIDGE_MASK) {
                  error(CRC_OUT_OF_RANGE);
              }

              // Response
              gXcp.CrmLen = CRM_TIME_SYNC_PROPERTIES_LEN;
              
              //CRM_TIME_SYNC_PROPERTIES_SLV_CONFIG = 0; // TIME_SYNC_BRIDGE = 0, DAQ_TS_RELATION = 0 XCP slave clock, RESPONSE_FMT = 0 - GET_DAQ_CLOCK response is always in legacy mode
              CRM_TIME_SYNC_PROPERTIES_SLV_CONFIG = 1; // TIME_SYNC_BRIDGE = 0, DAQ_TS_RELATION = 0 - XCP slave clock, RESPONSE_FMT = 1 - GET_DAQ_CLOCK response in extended format, GET_DAQ_CLOCK_MULTICAST response with EV_TIME_SYNC extended format
              //CRM_TIME_SYNC_PROPERTIES_OBSERVABLE_CLOCKS = 0; // XCP_SLV_CLK = 0 - free running observable, GRANDM_CLK = 0, ECU_CLK = 0
              CRM_TIME_SYNC_PROPERTIES_OBSERVABLE_CLOCKS = 1; // XCP_SLV_CLK = 1 - slave clock synchronized and observable, GRANDM_CLK = 0, ECU_CLK = 0
              CRM_TIME_SYNC_PROPERTIES_SYNC_STATE = 0x01; // SLV_CLK_SYNC_STATE = 1 - slave clock synchronised to grandmaster
              //CRM_TIME_SYNC_PROPERTIES_SYNC_STATE = 0x00; // SLV_CLK_SYNC_STATE = 0 - slave synchronising in progress to grandmaster
              CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO = 0x01|0x02|0x04; // SLC_CLK_INFO = 1 - info available in upload, GRANM_CLK_INFO = 1 - available, CLK_RELATION = 1 - available
              CRM_TIME_SYNC_PROPERTIES_RESERVED = 0x0;
              CRM_TIME_SYNC_PROPERTIES_CLUSTER_AFFILIATION = gXcp.ClusterId; // clusterAffiliation;

              // check whether MTA based upload is requested
              if (CRO_TIME_SYNC_PROPERTIES_GET_PROPERTIES & TIME_SYNC_GET_PROPERTIES_SLV_CLK_INFO) {
                  static unsigned char buf[sizeof(T_CLOCK_INFORMATION) + sizeof(T_CLOCK_RELATION) + sizeof(T_CLOCK_INFORMATION_GRANDM)];
                  unsigned char* p = &buf[0];
                  if (CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO & TIME_SYNC_GET_PROPERTIES_SLV_CLK_INFO) {
                      ApplXcpPrint("SLV CLK info to MTA\n");
                      memcpy(p, (unsigned char*)&gXcp.XcpSlaveClockInfo, sizeof(T_CLOCK_INFORMATION));
                      p += sizeof(T_CLOCK_INFORMATION);
                  }

                  if (CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO & TIME_SYNC_GET_PROPERTIES_GRANDM_CLK_INFO) {
                      memcpy(p, (unsigned char*)&gXcp.GrandmasterClockInfo, sizeof(T_CLOCK_INFORMATION_GRANDM));
                      p += sizeof(T_CLOCK_INFORMATION_GRANDM);
                  }

                  if (CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO & TIME_SYNC_GET_PROPERTIES_CLK_RELATION) {
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

                  XcpSetMta((MTABYTEPTR)buf, 0);
              }
            }
            break; 
#endif

#ifdef XCP_ENABLE_MULTICAST
          case CC_TRANSPORT_LAYER_CMD:
              switch (CRO_TL_SUBCOMMAND) {

              case CC_TL_GET_DAQ_CLOCK_MULTICAST:
                  CRM_GET_DAQ_CLOCK_TRIGGER_INFO = 0x18 + 0x02; // TIME_OF_SAMPLING (Bitmask 0x18, 3 - Sampled on reception) + TRIGGER_INITIATOR ( Bitmask 0x07, 2 - GET_DAQ_CLOCK_MULTICAST)
                  goto get_daq_clock_multicast;

              case CC_TL_GET_SLAVE_ID:
              default: /* unknown transport layer command */
                  error(CRC_CMD_UNKNOWN);
              }
              break;
#endif

          case CC_GET_DAQ_CLOCK:
          {
              CRM_GET_DAQ_CLOCK_TRIGGER_INFO = 0x18; // TIME_OF_SAMPLING (Bitmask 0x18, 3 - Sampled on reception) 
              CRM_GET_DAQ_CLOCK_RES1 = 0x00;
              CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = 0x01; // FMT_XCP_SLV = size of payload is DWORD
              gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN;
#ifdef XCP_ENABLE_PTP
              if (gXcp.SessionStatus & SS_LEGACY_MODE) {
                  // Legacy format
                  gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN;
                  CRM_GET_DAQ_CLOCK_TIME = ApplXcpGetClock();
              }
              else {
                  // Extended format
#ifdef TIMESTAMP_DLONG
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
#else
              gXcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN;
              CRM_GET_DAQ_CLOCK_TIME = ApplXcpGetClock();
#endif
          }
          break; 
          
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

              /* Packed mode */
              /*
              @@@@ TODO: Not implemented yet, never used by CANape
              case CC_GET_DAQ_LIST_PACKED_MODE:
              {
                  vuint16 daq = CRO_GET_DAQ_LIST_PACKED_MODE_DAQ;
                  gXcp.CrmLen = CRM_GET_DAQ_LIST_PACKED_MODE_LEN;
                  CRM_GET_DAQ_LIST_PACKED_MODE_MODE = 0; 
              }
              break;
              */

              case CC_SET_DAQ_LIST_PACKED_MODE:
              {
                  vuint16 daq = CRO_SET_DAQ_LIST_PACKED_MODE_DAQ;
                  if (daq >= gXcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
                  if (CRO_SET_DAQ_LIST_PACKED_MODE_MODE!=0x01) error(CRC_DAQ_CONFIG); // only element grouped implemented
                  if (CRO_SET_DAQ_LIST_PACKED_MODE_TIMEMODE != 0x00) error(CRC_DAQ_CONFIG); // only late timestamp implemented
                  DaqListSampleCount(daq) = CRO_SET_DAQ_LIST_PACKED_MODE_SAMPLECOUNT;
              }
              break;

              default: /* unknown command */
                  error(CRC_CMD_UNKNOWN);
              }
              break;

          default: /* unknown command */
            {
              error(CRC_CMD_UNKNOWN) 
            }

      } // switch()
  } 

  // Transmit command response
#if defined ( XCP_ENABLE_TESTMODE )
  if (gXcpDebugLevel >= 1) XcpPrintRes(pCmd);
#endif
  ApplXcpSendCrm(&gXcp.Crm.b[0], gXcp.CrmLen);
  return;

  // Transmit error response
  negative_response:
  gXcp.CrmLen = 2;
  CRM_CMD = (vuint8)PID_ERR;
  CRM_ERR = (vuint8)err;
#if defined ( XCP_ENABLE_TESTMODE )
  if (gXcpDebugLevel >= 1) XcpPrintRes(pCmd);
#endif
  ApplXcpSendCrm(&gXcp.Crm.b[0], gXcp.CrmLen);
  return;
}



/*****************************************************************************
| NAME:             XcpInit
| CALLED BY:        application
| PRECONDITIONS:    none
| INPUT PARAMETERS: none
| RETURN VALUES:    none
| DESCRIPTION:      Initialization of the XCP Protocol Layer
******************************************************************************/
void XcpInit( void )
{
  /* Initialize all XCP variables to zero */
  memset((BYTEPTR)&gXcp,0,(vuint16)sizeof(gXcp)); 
   
#ifdef XCP_ENABLE_PTP

  // UUID is contructed will be updated to match the MAC address of the XCP slave
  unsigned char uuid1[8] = SLAVE_UUID;
  memcpy(&gXcp.XcpSlaveClockInfo.UUID[0], &uuid1[0], 8);
  gXcp.XcpSlaveClockInfo.timestampTicks = kXcpDaqTimestampTicksPerUnit;
  gXcp.XcpSlaveClockInfo.timestampUnit = kXcpDaqTimestampUnit;
  gXcp.XcpSlaveClockInfo.stratumLevel = 255UL; // STRATUM_LEVEL_UNKNOWN
#ifdef TIMESTAMP_DLONG
  gXcp.XcpSlaveClockInfo.nativeTimestampSize = 8UL; // NATIVE_TIMESTAMP_SIZE_DLONG;
  gXcp.XcpSlaveClockInfo.valueBeforeWrapAround = 0xFFFFFFFFFFFFFFFFULL;
#else
  gXcp.XcpSlaveClockInfo.nativeTimestampSize = 4UL; // NATIVE_TIMESTAMP_SIZE_LONG;
  gXcp.XcpSlaveClockInfo.valueBeforeWrapAround = 0xFFFFFFFFULL;
#endif

  // Grandmaster clock description:
  // UUID will later be updated with the details received from the PTP grandmaster
  unsigned char uuid2[8] = MASTER_UUID;
  memcpy(&gXcp.XcpSlaveClockInfo.UUID[0], &uuid2[0], 8);
  gXcp.GrandmasterClockInfo.timestampTicks = kXcpDaqTimestampTicksPerUnit;
  gXcp.GrandmasterClockInfo.timestampUnit = kXcpDaqTimestampUnit;
  gXcp.GrandmasterClockInfo.stratumLevel = 0; // GPS
  gXcp.GrandmasterClockInfo.nativeTimestampSize = 8UL; // NATIVE_TIMESTAMP_SIZE_DLONG;
  gXcp.GrandmasterClockInfo.valueBeforeWrapAround = 0xFFFFFFFFFFFFFFFFULL;
  gXcp.GrandmasterClockInfo.epochOfGrandmaster = 0UL; // EPOCH_TAI;

  // If the slave clock is PTP synchronized, both origin and local timestamps are considered to be the same.
  // Timestamps will be updated to the current value pair
  gXcp.SlvGrandmClkRelationInfo.timestampLocal = 0;
  gXcp.SlvGrandmClkRelationInfo.timestampOrigin = 0;
#endif

  /* Initialize the session status */
  gXcp.SessionStatus = 0;

}


/****************************************************************************/
/* Test                                                                     */
/****************************************************************************/

#if defined ( XCP_ENABLE_TESTMODE )

static void XcpPrintCmd(const tXcpCto * pCmd) {

    switch (CRO_CMD) {

    case CC_SET_MTA:
        ApplXcpPrint("SET_MTA addr=%08Xh, addrext=%02Xh\n", CRO_SET_MTA_ADDR, CRO_SET_MTA_EXT);
        break;

    case CC_DOWNLOAD:
        {
            vuint16 i;
            ApplXcpPrint("DOWNLOAD size=%u, data=", CRO_DOWNLOAD_SIZE);
            for (i = 0; (i < CRO_DOWNLOAD_SIZE) && (i < CRO_DOWNLOAD_MAX_SIZE); i++)
            {
                ApplXcpPrint("%02X ", CRO_DOWNLOAD_DATA[i]);
            }
            ApplXcpPrint("\n");
        }
        break;

    case CC_UPLOAD:
        if (gXcpDebugLevel >= 2 || gXcpDebugLevel >= 1 && CRO_UPLOAD_SIZE <= 8) {
            ApplXcpPrint("UPLOAD size=%u\n", CRO_UPLOAD_SIZE);
        }
        break;

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

     case CC_SHORT_UPLOAD:
            ApplXcpPrint("SHORT_UPLOAD addr=%08Xh, addrext=%02Xh, size=%u\n", CRO_SHORT_UPLOAD_ADDR, CRO_SHORT_UPLOAD_EXT, CRO_SHORT_UPLOAD_SIZE);
            break;

     case CC_GET_DAQ_PROCESSOR_INFO:
            ApplXcpPrint("GET_DAQ_PROCESSOR_INFO\n");
            break;

     case CC_GET_DAQ_RESOLUTION_INFO:
            ApplXcpPrint("GET_DAQ_RESOLUTION_INFO\n");
            break;

     case CC_FREE_DAQ:
            ApplXcpPrint("-> FREE_DAQ\n");
            break;

     case CC_ALLOC_DAQ:
            ApplXcpPrint("ALLOC_DAQ count=%u\n", CRO_ALLOC_DAQ_COUNT);
            break;

     case CC_ALLOC_ODT:
            ApplXcpPrint("ALLOC_ODT daq=%u, count=%u\n", CRO_ALLOC_ODT_DAQ, CRO_ALLOC_ODT_COUNT);
            break;

     case CC_ALLOC_ODT_ENTRY:
         if (gXcpDebugLevel >= 2) {
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
            ApplXcpPrint("SET_DAQ_PTR daq=%u,odt=%u,idx=%u\n", CRO_SET_DAQ_PTR_DAQ, CRO_SET_DAQ_PTR_ODT, CRO_SET_DAQ_PTR_IDX);
            break;

     case CC_WRITE_DAQ: /* Write DAQ entry */
            ApplXcpPrint("WRITE_DAQ size=%u,addr=%08Xh,%02Xh\n", CRO_WRITE_DAQ_SIZE, CRO_WRITE_DAQ_ADDR, CRO_WRITE_DAQ_EXT);
            break;

     case CC_WRITE_DAQ_MULTIPLE: /* Write multiple DAQ entries */
         ApplXcpPrint("WRITE_MULTIPLE_DAQ count=%u\n", CRO_WRITE_DAQ_MULTIPLE_NODAQ);
         if (gXcpDebugLevel >= 2) {
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
            ApplXcpPrint("GET_DAQ_CLOCK\n");
            break;

     case CC_TIME_CORRELATION_PROPERTIES:
         ApplXcpPrint("GET_TIME_CORRELATION_PROPERTIES set=%02Xh, get=%02Xh, clusterId=%04Xh\n", CRO_TIME_SYNC_PROPERTIES_SET_PROPERTIES, CRO_TIME_SYNC_PROPERTIES_GET_PROPERTIES, CRO_TIME_SYNC_PROPERTIES_CLUSTER_AFFILIATION );
         break;

     case CC_LEVEL_1_COMMAND:
         switch (CRO_LEVEL_1_COMMAND_CODE) {
           case CC_GET_VERSION:
               ApplXcpPrint("GET_VERSION\n");
               break;
           case CC_GET_DAQ_LIST_PACKED_MODE:
               ApplXcpPrint("GET_DAQ_LIST_PACKED_MODE daq=%u\n", CRO_GET_DAQ_LIST_PACKED_MODE_DAQ);
               break;
           case CC_SET_DAQ_LIST_PACKED_MODE:
               ApplXcpPrint("SET_DAQ_LIST_PACKED_MODE daq=%u, sampleCount=%u\n", CRO_SET_DAQ_LIST_PACKED_MODE_DAQ,CRO_SET_DAQ_LIST_PACKED_MODE_SAMPLECOUNT);
               break;
           default:
               ApplXcpPrint("UNKNOWN LEVEL 1 COMMAND %02X\n", CRO_LEVEL_1_COMMAND_CODE);
               break;
         }
         break;
         
    } /* switch */
}

static void XcpPrintRes(const tXcpCto* pCmd) {

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
        ApplXcpPrint("<- Error: %02Xh - %s\n", CRM_ERR, e );
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
            if (gXcpDebugLevel >= 2) {
                ApplXcpPrint("<- data=");
                for (int i = 0; i < CRO_UPLOAD_SIZE; i++) {
                    ApplXcpPrint("%02Xh ", CRM_UPLOAD_DATA[i]);
                }
                ApplXcpPrint("\n");
            }
            break;

        case CC_SHORT_UPLOAD:
            if (gXcpDebugLevel >= 2) {
                ApplXcpPrint("<- data=");
                for (int i = 0; i < (vuint16)CRO_SHORT_UPLOAD_SIZE; i++) {
                    ApplXcpPrint("%02Xh ", CRM_SHORT_UPLOAD_DATA[i]);
                }
                ApplXcpPrint("\n");
            }
            break;

        case CC_GET_DAQ_RESOLUTION_INFO:
            ApplXcpPrint("<- mode=%02Xh, , ticks=%02Xh\n", CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE, CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS);
            break;

        case CC_GET_DAQ_CLOCK:
            if (gXcp.SessionStatus & SS_LEGACY_MODE) {
                ApplXcpPrint("<- t=%ul (%gs)\n", CRM_GET_DAQ_CLOCK_TIME, (double)CRM_GET_DAQ_CLOCK_TIME / (1000.0 * kApplXcpDaqTimestampTicksPerMs));
            }
            else {
                if (CRM_GET_DAQ_CLOCK_PAYLOAD_FMT == 0x01) {
                    ApplXcpPrint("<- t=%ul (%gs) %u\n", CRM_GET_DAQ_CLOCK_TIME, (double)CRM_GET_DAQ_CLOCK_TIME / (1000.0 * kApplXcpDaqTimestampTicksPerMs), CRM_GET_DAQ_CLOCK_SYNC_STATE);
                }
                else {
                    ApplXcpPrint("<- t=%llull (%gs) %u\n", CRM_GET_DAQ_CLOCK_TIME64, (double)CRM_GET_DAQ_CLOCK_TIME64 / (1000.0 * kApplXcpDaqTimestampTicksPerMs), CRM_GET_DAQ_CLOCK_SYNC_STATE64);
                }
            }
            break;

        case CC_LEVEL_1_COMMAND:
            switch (CRO_LEVEL_1_COMMAND_CODE) {
            case CC_GET_VERSION:
                ApplXcpPrint("<- protocol layer version: major=%02Xh/minor=%02Xh, transport layer version: major=%02Xh/minor=%02Xh\n",
                    CRM_GET_VERSION_PROTOCOL_VERSION_MAJOR,
                    CRM_GET_VERSION_PROTOCOL_VERSION_MINOR,
                    CRM_GET_VERSION_TRANSPORT_VERSION_MAJOR,
                    CRM_GET_VERSION_TRANSPORT_VERSION_MINOR);
                    break;
            case CC_GET_DAQ_LIST_PACKED_MODE:
                ApplXcpPrint("<- mode = %u\n", CRM_GET_DAQ_LIST_PACKED_MODE_MODE);
                break;
            }

        default:
            if (gXcpDebugLevel >= 2) {
                ApplXcpPrint("<- OK\n");
            }
            break;


        } /* switch */
    }
}


void XcpPrintDaqList( vuint16 daq )
{
  int i,e;
    
  if (daq>=gXcp.Daq.DaqCount) return;

  ApplXcpPrint("DAQ %u:\n",daq);
  ApplXcpPrint(" eventchannel=%04Xh,",DaqListEventChannel(daq));
  ApplXcpPrint(" firstOdt=%u,",DaqListFirstOdt(daq));
  ApplXcpPrint(" lastOdt=%u,",DaqListLastOdt(daq));
  ApplXcpPrint(" flags=%02Xh,",DaqListFlags(daq));
  ApplXcpPrint(" sampleCount=%u\n",DaqListSampleCount(daq)); 
  for (i=DaqListFirstOdt(daq);i<=DaqListLastOdt(daq);i++) {
    ApplXcpPrint("  ODT %u (%u):",i-DaqListFirstOdt(daq),i);
    ApplXcpPrint(" firstOdtEntry=%u, lastOdtEntry=%u, size=%u:\n", DaqListOdtFirstEntry(i), DaqListOdtLastEntry(i),DaqListOdtSize(i));
    for (e=DaqListOdtFirstEntry(i);e<=DaqListOdtLastEntry(i);e++) {
      ApplXcpPrint("   %p-%p,%u\n",OdtEntryAddr(e), OdtEntryAddr(e)+OdtEntrySize(e)-1,OdtEntrySize(e));
    }
  } /* j */
} 

  
#endif /* XCP_ENABLE_TESTMODE */


