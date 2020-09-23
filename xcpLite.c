

/*****************************************************************************
| Project Name:   XCP Protocol Layer
|    File Name:   xcpLite.c
|    V1.0 23.9.2020
|
|  Description:   Implementation of the XCP Protocol Layer 
|                 XCP V1.0 slave device driver
|                 Lite Version (see feature list below)
|                 Don't change this file !
|
|
|     Limitations of the Lite version
|
|     - Motorola byte sex (big endian) is not supported
|     - Platform must support unaligned word and dword memory access
|     - 8 bit and 16 bit CPUs are not supported
|     - Daq and Event numbers are BYTE
|     - Only dynamic DAQ list allocation supported
|     - MAX_DTO is limited to max. 255
|     - Resume is not supported
|     - Overload indication by event is not supported
|     - DAQ does not support address extensions
|     - DAQ-list and event channel prioritization is not supported
|     - Event channels contain one DAQ-list
|     - ODT optimization not supported
|     - Interleaved communication mode is not supported
|     - Seed & key is not supported
|     - Flash programming is not supported
|     - Calibration pages are not supported
|     - Checksum is not supported
|     - Event messages (SERV_TEXT) are not supported
|     - User commands are not supported
|
|       All these feature are available in the basic version available free of charge from
|       Vector Informatik GmbH
|
|Limitations of the XCP basic version:
|
|     - Stimulation (Bypassing) is not available
|         XCP_ENABLE_STIM
|     - Bit stimulation is not available
|         XCP_ENABLE_STIM_BIT
|     - SHORT_DOWNLOAD is not available
|         XCP_ENABLE_SHORT_DOWNLOAD
|     - MODIFY_BITS is not available
|         XCP_ENABLE_MODIFY_BITS
|     - FLASH and EEPROM Programming is not available
|         XCP_ENABLE_PROGRAM, XCP_ENABLE_BOOTLOADER_DOWNLOAD, XCP_ENABLE_READ_EEPROM, XCP_ENABLE_WRITE_EEPROM
|     - Block mode for UPLOAD, DOWNLOAD and PROGRAM is not available
|         XCP_ENABLE_BLOCK_UPLOAD, XCP_ENABLE_BLOCK_DOWNLOAD
|     - Resume mode is not available
|         XCP_ENABLE_DAQ_RESUME
|     - Memory write and read protection is not supported
|         XCP_ENABLE_WRITE_PROTECTION
|         XCP_ENABLE_READ_PROTECTION
|     - Checksum calculation with AUTOSAR CRC module is not supported
|         XCP_ENABLE_AUTOSAR_CRC_MODULE
|     
|       All these feature are available in the full version available from
|       Vector Informatik GmbH
|***************************************************************************/


/***************************************************************************/
/* Include files                                                           */
/***************************************************************************/

#include "xcpLite.h"


/***************************************************************************/
/* Version check                                                           */
/***************************************************************************/
#if ( CP_XCP_VERSION != 0x0130u )
  #error "Source and Header file are inconsistent!"
#endif
#if ( CP_XCP_RELEASE_VERSION != 0x04u )
  #error "Source and Header file are inconsistent!"
#endif

/****************************************************************************/
/* Macros                                                                   */
/****************************************************************************/

/* Definition of macros that have to be used within the context of XcpCommand. */
/* They have to be declared global Due to MISRA rule 91. */


#define error(e) { err=(e); goto negative_response; }
#define check_error(e) { err=(e); if (err!=0) { goto negative_response; } }
#define error1(e,b1) { err=(e); CRM_BYTE(2)=(b1); xcp.CrmLen=3; goto negative_response1; }
#define error2(e,b1,b2) { err=(e); CRM_BYTE(2)=(b1); CRM_BYTE(3)=(b2); xcp.CrmLen=4; goto negative_response1; }


#if defined ( XCP_ENABLE_MEM_ACCESS_BY_APPL )
  #define XCP_WRITE_BYTE_2_ADDR(addr, data)           ApplXcpWrite( (vuint32)(addr), (vuint8)(data) ) 
  #define XCP_READ_BYTE_FROM_ADDR(addr)               ApplXcpRead ( (vuint32)(addr) ) 
  
#else
  #define XCP_WRITE_BYTE_2_ADDR(addr, data)           *(addr) = (data) 
  #define XCP_READ_BYTE_FROM_ADDR(addr)               *(addr) 
  
#endif


  
/****************************************************************************/
/* Constants                                                                */
/****************************************************************************/

/****************************************************************************/
/* 8 Bit Constants for export                                               */
/****************************************************************************/

/* Global constants with XCP Protocol Layer main and subversion */
V_MEMROM0 const vuint8 kXcpMainVersion    = (vuint8)(CP_XCP_VERSION >> 8);
V_MEMROM0 const vuint8 kXcpSubVersion     = (vuint8)(CP_XCP_VERSION & 0x00ff);
V_MEMROM0 const vuint8 kXcpReleaseVersion = (vuint8)(CP_XCP_RELEASE_VERSION);



/****************************************************************************/
/* Local data                                                               */
/****************************************************************************/


RAM tXcpData xcp; 

#if defined ( XCP_ENABLE_SEND_QUEUE )
#else
static tXcpDto dto;
#endif


#if defined ( XCP_ENABLE_TESTMODE )
vuint8 gDebugLevel;
#endif



/***************************************************************************/
/* Prototypes for local functions                                          */
/***************************************************************************/

static vuint8 XcpWriteMta( vuint8 size, const BYTEPTR data );
static vuint8 XcpReadMta( vuint8 size, BYTEPTR data );

#if defined ( XcpMemClr )
#else
static void XcpMemClr( BYTEPTR p, vuint16 n );
#endif


#if defined ( XCP_ENABLE_SEND_QUEUE )
static vuint8 XcpSendDtoFromQueue( void );
static void XcpQueueInit( void );
#endif

#if defined ( XCP_ENABLE_DAQ )
static void XcpFreeDaq( void );
static vuint8 XcpAllocMemory( void );
static vuint8 XcpAllocDaq( vuint8 daqCount );
static vuint8 XcpAllocOdt( vuint8 daq, vuint8 odtCount );
static vuint8 XcpAllocOdtEntry( vuint8 daq, vuint8 odt, vuint8 odtEntryCount );
static void XcpStartDaq( vuint8 daq );
static void XcpStartAllSelectedDaq( void );
static void XcpStopDaq( vuint8 daq );
static void XcpStopAllSelectedDaq( void );
static void XcpStopAllDaq( void );


#endif



/*****************************************************************************
| NAME:             XcpMemSet
| CALLED BY:        XcpFreeDaq
| PRECONDITIONS:    none
| INPUT PARAMETERS: p : pointer to start address.
|                   n : number of data bytes.
|                   d : data byte to initialize with.
| RETURN VALUES:    none 
| DESCRIPTION:      Initialize n bytes starting from address p with b.
******************************************************************************/
                                 
#if defined ( XcpMemSet )
 /* XcpMemSet is overwritten */
#else
void XcpMemSet( BYTEPTR p, vuint16 n, vuint8 b )
{
  for ( ; n > 0; n-- )
  {
    *p = b;
    p++; 
  }
}                
#endif

/*****************************************************************************
| NAME:             XcpMemClr
| CALLED BY:        XcpFreeDaq, XcpInit
| PRECONDITIONS:    none
| INPUT PARAMETERS: p : pointer to start address.
|                   n : number of data bytes.
| RETURN VALUES:    none 
| DESCRIPTION:      Initialize n bytes starting from address p 0.
******************************************************************************/

#if defined ( XcpMemClr )
 /* XcpMemClr is overwritten */
#else
/* A macro would be more efficient. Due to MISRA rule 96 violations a function is implemented. */
static void XcpMemClr( BYTEPTR p, vuint16 n )
{
  XcpMemSet( p, n, (vuint8)0u);
}
#endif

/*****************************************************************************
| NAME:             XcpMemCpy
| CALLED BY:        XcpEvent
| PRECONDITIONS:    none
| INPUT PARAMETERS: dest : pointer to destination address.
|                   src  : pointer to source address.
|                   n    : number of data bytes to copy.
| RETURN VALUES:    none 
| DESCRIPTION:      Copy n bytes from src to dest.
|                   A maximum of 255 Bytes can be copied at once.
******************************************************************************/

/* Optimize this function !
   High impact on performance
   It is used in the inner loop of XcpEvent for data acquisition sampling 
*/

#if defined ( XcpMemCpy ) 
 /* XcpMemCpy is overwritten */
#else
void XcpMemCpy( DAQBYTEPTR dest, const DAQBYTEPTR src, vuint8 n )
{
  for ( ; n > 0; n-- )
  {
    /* ESCAN00092933 */
    XCP_WRITE_BYTE_2_ADDR( dest, XCP_READ_BYTE_FROM_ADDR(src) );
    dest++; 
    src++; 
  }
}
#endif



/****************************************************************************/
/* Transmit                                                                 */
/****************************************************************************/


/*****************************************************************************
| NAME:             XcpSendCrm
| CALLED BY:        XcpBackground, XcpCommand, XcpSendCallBack, application
| PRECONDITIONS:    XCP is initialized and in connected state and 
|                   a command packet (CMD) has been received.
| INPUT PARAMETERS: none
| RETURN VALUES:    none 
| DESCRIPTION:      Transmission of a command response packet (RES), 
|                    or error packet (ERR) if no other packet is pending.
******************************************************************************/
void XcpSendCrm( void )
{
  
#if defined ( XCP_ENABLE_SEND_QUEUE )

  ApplXcpInterruptDisable();

  if ( (xcp.SendStatus & (vuint8)XCP_SEND_PENDING) != 0 )
  {
    if ( (xcp.SendStatus & (vuint8)XCP_CRM_REQUEST) != 0 )
    {
      XCP_ASSERT(0);
      xcp.SessionStatus |= (SessionStatusType)SS_ERROR; 
    }
    xcp.SendStatus |= (vuint8)XCP_CRM_REQUEST;
  } 
  else
  {
    xcp.SendStatus |= (vuint8)XCP_CRM_PENDING;
    ApplXcpSend(xcp.CrmLen,&xcp.Crm.b[0]);
  }

  ApplXcpInterruptEnable();

#else

  ApplXcpSend(xcp.CrmLen,&xcp.Crm.b[0]);

#endif

  ApplXcpSendFlush();
  
}




#if defined ( XCP_ENABLE_DAQ )
/*****************************************************************************
| NAME:             XcpSendDto
| CALLED BY:        XcpSendDtoFromQueue, XcpEvent, XcpSendCallBack
| PRECONDITIONS:    none
| INPUT PARAMETERS: dto : pointer to XCP packet type definition
| RETURN VALUES:    none 
| DESCRIPTION:      Send a DTO.
******************************************************************************/
  #if defined ( XcpSendDto )
  /* XcpSendDto is redefined */
  #else
void XcpSendDto( const tXcpDto *dto )
{
  ApplXcpSend( dto->l, &dto->b[0] );
}
  #endif
#endif /* XCP_ENABLE_DAQ */


#if defined ( XCP_ENABLE_SEND_QUEUE )
/*****************************************************************************
| NAME:             XcpSendDtoFromQueue
| CALLED BY:        XcpEvent, XcpSendCallBack
| PRECONDITIONS:    none
| INPUT PARAMETERS: none
| RETURN VALUES:    0 : DTO has NOT been transmitted from queue. 
|                   1 : DTO has been transmitted from queue. 
| DESCRIPTION:      Send a DTO from the queue.
******************************************************************************/
static vuint8 XcpSendDtoFromQueue( void )
{
  ApplXcpInterruptDisable();
  if ( ( (xcp.SendStatus & (vuint8)XCP_SEND_PENDING) == 0 ) && ( xcp.QueueLen != 0 ))
  {
    xcp.SendStatus |= (vuint8)XCP_DTO_PENDING;
    XcpSendDto(&xcp.pQueue[xcp.QueueRp]);
    xcp.QueueRp++;
    if ( xcp.QueueRp >= xcp.QueueSize )
    {
      xcp.QueueRp = (vuint16)0u;
    }
    xcp.QueueLen--;
    ApplXcpInterruptEnable();
    return (vuint8)1u;
  }
  ApplXcpInterruptEnable();
  return (vuint8)0u;
 
}
#endif /* XCP_ENABLE_SEND_QUEUE */


/****************************************************************************/
/* Transmit Queue */
/****************************************************************************/

#if defined ( XCP_ENABLE_SEND_QUEUE )

/*****************************************************************************
| NAME:             XcpQueueInit
| CALLED BY:        XcpFreeDaq, XcpStopDaq, XcpStopAllDaq
| PRECONDITIONS:    none
| INPUT PARAMETERS: none
| RETURN VALUES:    none 
| DESCRIPTION:      Initialize the transmit queue.
******************************************************************************/
static void XcpQueueInit(void)
{
  xcp.QueueLen = (vuint16)0u;
  xcp.QueueRp = (vuint16)0u;
}

#endif /* XCP_ENABLE_SEND_QUEUE */


/****************************************************************************/
/* Handle Mta (Memory-Transfer-Address) */
/****************************************************************************/

/* Assign a pointer to a Mta */
#if defined ( XcpSetMta )
#else
    #define XcpSetMta(p,e) (xcp.Mta = (p)) 
#endif

/*****************************************************************************
| NAME:             XcpWriteMta
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: size : number of data bytes.
|                   data : address of data.
| RETURN VALUES:    XCP_CMD_OK, XCP_CMD_DENIED
| DESCRIPTION:      Write n bytes.
|                   Copying of size bytes from data to xcp.Mta
******************************************************************************/
static vuint8 XcpWriteMta( vuint8 size, const BYTEPTR data )
{
#if defined ( XCP_ENABLE_CALIBRATION_MEM_ACCESS_BY_APPL )
  vuint8 r;
#endif

  /* DPRAM Client */

  /* Checked ram memory write access */

  /* EEPROM write access */

  /* Standard RAM memory write access */
#if defined ( XCP_ENABLE_CALIBRATION_MEM_ACCESS_BY_APPL ) && !defined ( XCP_ENABLE_MEM_ACCESS_BY_APPL )
  r = ApplXcpCalibrationWrite(xcp.Mta, size, data);
  xcp.Mta += size; 
  return r;
#else
  while ( size > (vuint8)0u ) 
  {
    XCP_WRITE_BYTE_2_ADDR( xcp.Mta, *data );
    xcp.Mta++; 
    data++; 
    size--;
  }
  return (vuint8)XCP_CMD_OK;
#endif
  
}

/*****************************************************************************
| NAME:             XcpReadMta
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: size :
|                   data : address of data
| RETURN VALUES:    XCP_CMD_OK
| DESCRIPTION:      Read n bytes.
|                   Copying of size bytes from data to xcp.Mta
******************************************************************************/
static vuint8 XcpReadMta( vuint8 size, BYTEPTR data )
{
#if defined ( XCP_ENABLE_CALIBRATION_MEM_ACCESS_BY_APPL )
  vuint8 r;
#endif

  /* DPRAM Client */

  /* Checked ram memory read access */

  /* EEPROM read access */

#if defined ( XCP_ENABLE_CALIBRATION_MEM_ACCESS_BY_APPL ) && !defined ( XCP_ENABLE_MEM_ACCESS_BY_APPL )
  r = ApplXcpCalibrationRead(xcp.Mta, size, data);
  xcp.Mta += size; 
  return r;
#else
  /* Standard RAM memory read access */
  while (size > 0)
  {
    /* 
       Compiler bug Tasking
       *(data++) = *(xcp.Mta++);
    */
    *(data) = XCP_READ_BYTE_FROM_ADDR( xcp.Mta );
    data++; 
    xcp.Mta++; 
    size--;
  }
  return (vuint8)XCP_CMD_OK;
#endif
  
}


/****************************************************************************/
/* Data Aquisition Setup                                                    */
/****************************************************************************/


#if defined ( XCP_ENABLE_DAQ )

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
  xcp.SessionStatus &= (SessionStatusType)((~SS_DAQ) & 0xFFu);

  xcp.Daq.DaqCount = 0;
  xcp.Daq.OdtCount = 0;
  xcp.Daq.OdtEntryCount = 0;

  xcp.pOdt = (tXcpOdt*)0;
  xcp.pOdtEntryAddr = 0;
  xcp.pOdtEntrySize = 0;

  XcpMemClr((BYTEPTR)&xcp.Daq.u.b[0], (vuint16)kXcpDaqMemSize);  /* Deviation of MISRA rule 44. */
  #if defined ( kXcpMaxEvent ) && ! defined ( XCP_ENABLE_DAQ_PRESCALER )
    XcpMemSet( (BYTEPTR)&xcp.Daq.EventDaq[0], (vuint16)sizeof(xcp.Daq.EventDaq), (vuint8)0xFFu);  /* Deviation of MISRA rule 44. */
  #endif

  #if defined ( XCP_ENABLE_SEND_QUEUE )
  XcpQueueInit();
  #endif
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
  #if defined ( XCP_ENABLE_NO_P2INT_CAST  )
  vuint8* p;
  vuint8  i;
  #endif

  /* Check memory overflow */
  s = (vuint16)( ( xcp.Daq.DaqCount      *   (vuint8)sizeof(tXcpDaqList)                           ) + 
                 ( xcp.Daq.OdtCount      *  (vuint16)sizeof(tXcpOdt)                               ) + 
                 ( xcp.Daq.OdtEntryCount * ( (vuint8)sizeof(DAQBYTEPTR) + (vuint8)sizeof(vuint8) ) )
               );
  

  if (s>=(vuint16)kXcpDaqMemSize) 
  {
    return (vuint8)CRC_MEMORY_OVERFLOW;
  }

  
  xcp.pOdt = (tXcpOdt*)&xcp.Daq.u.DaqList[xcp.Daq.DaqCount];
  xcp.pOdtEntryAddr = (DAQBYTEPTR*)&xcp.pOdt[xcp.Daq.OdtCount];
  xcp.pOdtEntrySize = (vuint8*)&xcp.pOdtEntryAddr[xcp.Daq.OdtEntryCount]; 
  
  #if defined ( XCP_ENABLE_SEND_QUEUE )
  
  xcp.pQueue = (tXcpDto*)&xcp.pOdtEntrySize[xcp.Daq.OdtEntryCount];
    

  xcp.QueueSize = ((vuint16)kXcpDaqMemSize - s) / sizeof(tXcpDto);

    #if defined ( kXcpSendQueueMinSize )
  if (xcp.QueueSize<(vuint16)kXcpSendQueueMinSize)
  {
    return (vuint8)CRC_MEMORY_OVERFLOW;
  }
    #else
  /* At least one queue entry per odt */
  if (xcp.QueueSize<xcp.Daq.OdtCount)
  {
    return (vuint8)CRC_MEMORY_OVERFLOW;
  }
    #endif
  #endif /* XCP_ENABLE_SEND_QUEUE */

  #if defined ( XCP_ENABLE_TESTMODE )
  if ( gDebugLevel != 0)
  {
    ApplXcpPrint("[XcpAllocMemory] %u/%u Bytes used\n",s,kXcpDaqMemSize );
    #if defined ( XCP_ENABLE_SEND_QUEUE )
    ApplXcpPrint("[XcpAllocMemory] Queuesize=%u\n",xcp.QueueSize );
    #endif
  }
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
  if ( (xcp.Daq.OdtCount!=0) || (xcp.Daq.OdtEntryCount!=0) )
  {
    return (vuint8)CRC_SEQUENCE;
  }
  if( daqCount == 0 )
  {
    return (vuint8)CRC_OUT_OF_RANGE;
  }
  #endif

  xcp.Daq.DaqCount = daqCount;

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
  if ( (xcp.Daq.DaqCount==0) || (xcp.Daq.OdtEntryCount!=0) )
  {
    return (vuint8)CRC_SEQUENCE;
  }
  if( odtCount == 0 )
  {
    return (vuint8)CRC_OUT_OF_RANGE;
  }
  #endif

  /* Absolute ODT count must fit in a BYTE */
  #if !defined ( XCP_ENABLE_DAQ_HDR_ODT_DAQ )
   #if defined XCP_ENABLE_DAQ_OVERRUN_INDICATION
    if (((vuint16)xcp.Daq.OdtCount+(vuint16)odtCount) > (vuint16)0x7Bu) 
   #else
    if (((vuint16)xcp.Daq.OdtCount+(vuint16)odtCount) > (vuint16)0xFBu)
   #endif
  {
    return (vuint8)CRC_MEMORY_OVERFLOW;
  }
  #endif

  xcp.Daq.u.DaqList[daq].firstOdt = xcp.Daq.OdtCount;
  xcp.Daq.OdtCount += odtCount;
  xcp.Daq.u.DaqList[daq].lastOdt = (tXcpOdtIdx)(xcp.Daq.OdtCount-1);

  return XcpAllocMemory();
  
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
  tXcpOdtIdx xcpFirstOdt;

  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
  if ( (xcp.Daq.DaqCount==0) || (xcp.Daq.OdtCount==0) )
  {
    return (vuint8)CRC_SEQUENCE;
  }
  if (odtEntryCount==0)
  {
    return (vuint8)CRC_OUT_OF_RANGE;
  }
  #endif

  /* Absolute ODT entry count count must fit in a WORD */
  if (xcp.Daq.OdtEntryCount > (0xFFFFu - odtEntryCount))
  {
    return (vuint8)CRC_MEMORY_OVERFLOW;
  }
  xcpFirstOdt = xcp.Daq.u.DaqList[daq].firstOdt;
  xcp.pOdt[xcpFirstOdt+odt].firstOdtEntry = xcp.Daq.OdtEntryCount;
  xcp.Daq.OdtEntryCount += (vuint16)odtEntryCount;
  xcp.pOdt[xcpFirstOdt+odt].lastOdtEntry = (vuint16)(xcp.Daq.OdtEntryCount-1);

  return XcpAllocMemory();
  
}

/*****************************************************************************
| NAME:             XcpStartDaq
| CALLED BY:        XcpCommand, XcpStartAllSelectedDaq
| PRECONDITIONS:    none
| INPUT PARAMETERS: daq :
| RETURN VALUES:    none
| DESCRIPTION:      Start DAQ
******************************************************************************/
static void XcpStartDaq( vuint8 daq )
{
  /* Initialize the DAQ list */
  DaqListFlags(daq) |= (vuint8)DAQ_FLAG_RUNNING;
  #if defined ( XCP_ENABLE_DAQ_PRESCALER )
  DaqListCycle(daq) = 1;
  #endif

  xcp.SessionStatus |= (SessionStatusType)SS_DAQ;
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
  vuint8 daq;

  /* Start all selected DAQs */
  for (daq=0;daq<xcp.Daq.DaqCount;daq++)
  {
    if ( (DaqListFlags(daq) & (vuint8)DAQ_FLAG_SELECTED) != 0 )
    {
      XcpStartDaq(daq);
      DaqListFlags(daq) &= (vuint8)(~DAQ_FLAG_SELECTED & 0x00FFu);
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
static void XcpStopDaq( vuint8 daq )
{
  vuint8 i;

  DaqListFlags(daq) &= (vuint8)(DAQ_FLAG_DIRECTION|DAQ_FLAG_TIMESTAMP|DAQ_FLAG_NO_PID);

  /* Check if all DAQ lists are stopped */
  for (i=0;i<xcp.Daq.DaqCount;i++)
  {
    if ( (DaqListFlags(i) & (vuint8)DAQ_FLAG_RUNNING) != 0 )
    {
      return;
    }
  }

  xcp.SessionStatus &= (SessionStatusType)(~SS_DAQ & 0x00FFu);

  #if defined ( XCP_ENABLE_SEND_QUEUE )
  XcpQueueInit();
  #endif
  
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
  vuint8 daq;

  for (daq=0;daq<xcp.Daq.DaqCount;daq++)
  {
    if ( (DaqListFlags(daq) & (vuint8)DAQ_FLAG_SELECTED) != 0 )
    {
      XcpStopDaq(daq);
      DaqListFlags(daq) &= (vuint8)(~DAQ_FLAG_SELECTED & 0x00FFu);
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
  vuint8 daq;

  for (daq=0; daq<xcp.Daq.DaqCount; daq++)
  {
    DaqListFlags(daq) &= (vuint8)(DAQ_FLAG_DIRECTION|DAQ_FLAG_TIMESTAMP|DAQ_FLAG_NO_PID);
  }

  xcp.SessionStatus &= (SessionStatusType)(~SS_DAQ & 0x00FFu);

  #if defined ( XCP_ENABLE_SEND_QUEUE )
  XcpQueueInit();
  #endif
}


/****************************************************************************/
/* Data Aquisition Processor                                                */
/****************************************************************************/


/*****************************************************************************
| NAME:             XcpEvent
| CALLED BY:        application
| PRECONDITIONS:    The XCP is initialized and in connected state.
| INPUT PARAMETERS: event : event channel number to process
| RETURN VALUES:    status code (XCP_EVENT_...)
| DESCRIPTION:      Handling of data acquisition or stimulation event channel.
******************************************************************************/


vuint8 XcpEvent(vuint8 event) {

    return XcpEventExt(event, 0);
}

vuint8 XcpEventExt(vuint8 event, BYTEPTR offset)
{
  tXcpDto *dtop;
  BYTEPTR d;
  vuint8  status;
  vuint16 e,el;
  vuint8  n;
  vuint8  daq;
  tXcpOdtIdx odt;
  vuint8  i;
  
  
  status = (vuint8)0u;

  if ( (xcp.SessionStatus & (SessionStatusType)SS_DAQ) == 0 )
  {
    return (vuint8)XCP_EVENT_NOP;
  }

  

  #if defined ( kXcpMaxEvent ) && ! defined ( XCP_ENABLE_DAQ_PRESCALER )

    #if defined ( XCP_ENABLE_PARAMETER_CHECK )
  if (event >= (vuint8)kXcpMaxEvent)
  {
    return (vuint8)XCP_EVENT_NOP;
  }
    #endif

  BEGIN_PROFILE(4); /* Timingtest */
  daq = xcp.Daq.EventDaq[event];
  if ( ((daq<xcp.Daq.DaqCount) && ( (DaqListFlags(daq) & (vuint8)DAQ_FLAG_RUNNING) != 0 )) != 0 )
  {

  #else

  BEGIN_PROFILE(4); /* Timingtest */
  for (daq=0; daq<xcp.Daq.DaqCount; daq++)
  {
    if ( (DaqListFlags(daq)& (vuint8)DAQ_FLAG_RUNNING) == 0 )
    {
      continue; 
    }
    if ( DaqListEventChannel(daq) != event )
    {
      continue; 
    }

  #endif

  #if defined ( XCP_ENABLE_DAQ_PRESCALER )
    DaqListCycle(daq)--;
    if ( DaqListCycle(daq) == (vuint8)0 )
    {
      DaqListCycle(daq) = DaqListPrescaler(daq);
  #endif

  

      for (odt=DaqListFirstOdt(daq);odt<=DaqListLastOdt(daq);odt++)
      {
#if defined ( XCP_ENABLE_SEND_QUEUE )
        vuint16 qs;
#endif
        status |= (vuint8)XCP_EVENT_DAQ;


        ApplXcpInterruptDisable(); /* The following code is not reentrant */

  #if defined ( XCP_ENABLE_SEND_QUEUE )
        /* Check if there is space in the queue for this ODT */
        if (xcp.QueueLen>=xcp.QueueSize)
        {
          status |= (vuint8)XCP_EVENT_DAQ_OVERRUN; /* Queue overflow */
          DaqListFlags(daq) |= (vuint8)DAQ_FLAG_OVERRUN;
          goto next_odt; 
        }

        qs = (xcp.QueueRp + xcp.QueueLen);
        if(qs >= xcp.QueueSize)
        {
          qs -= xcp.QueueSize;
        }

        dtop = &xcp.pQueue[qs];
    #if defined ( XCP_SEND_QUEUE_SAMPLE_ODT )
        xcp.QueueLen++;
    #endif
  #else
        dtop = &dto;
  #endif /* XCP_ENABLE_SEND_QUEUE */

  #if defined ( XCP_ENABLE_DAQ_HDR_ODT_DAQ )

        /* ODT,DAQ */
        dtop->b[0] = odt-DaqListFirstOdt(daq); /* Relative odt number */
        dtop->b[1] = daq;
        i = 2;

  #else

        /* PID */
        dtop->b[0] = odt;
        i = 1;

  #endif

        /* Use BIT7 of PID or ODT to indicate overruns */
  #if defined ( XCP_ENABLE_SEND_QUEUE )
    #if defined ( XCP_ENABLE_DAQ_OVERRUN_INDICATION )
        if ( (DaqListFlags(daq) & (vuint8)DAQ_FLAG_OVERRUN) != 0 )
        {
          dtop->b[0] |= (vuint8)0x80;
          DaqListFlags(daq) &= (vuint8)(~DAQ_FLAG_OVERRUN & 0xFFu);
        }
    #endif
  #endif

        /* Timestamps */
  #if defined ( XCP_ENABLE_DAQ_TIMESTAMP )

    #if !defined ( XCP_ENABLE_DAQ_TIMESTAMP_FIXED )
        if ( (DaqListFlags(daq) & (vuint8)DAQ_FLAG_TIMESTAMP) != 0 )
        {
    #endif

          if (odt==DaqListFirstOdt(daq))
          {
    

    #if defined ( XCP_ENABLE_DAQ_HDR_ODT_DAQ )

            *(XcpDaqTimestampType*)&dtop->b[2] = ApplXcpGetTimestamp();
            i = 2 + XcpDaqTimestampSize;

    #else /* XCP_ENABLE_DAQ_HDR_ODT_DAQ */

      
            *(XcpDaqTimestampType*)&dtop->b[i] = ApplXcpGetTimestamp();
      
            i += XcpDaqTimestampSize;

    #endif /* XCP_ENABLE_DAQ_HDR_ODT_DAQ */
          }

    #if !defined ( XCP_ENABLE_DAQ_TIMESTAMP_FIXED )
        }
    #endif

  #endif /* XCP_ENABLE_DAQ_TIMESTAMP */

        /* Sample data */
        /* This is the inner loop, optimize here */
        e = DaqListOdtFirstEntry(odt);
        if (OdtEntrySize(e)==0)
        {
          goto next_odt; 
        }
        el = DaqListOdtLastEntry(odt);
        d = (vuint8*)&dtop->b[i];
        
        while (e<=el) { // inner DAQ loop
          n = OdtEntrySize(e);
          if (n == 0) break;
          XcpMemCpy((DAQBYTEPTR)d, offset+(vuint32)OdtEntryAddr(e), n);
          d += n; 
          e++;
        }
        dtop->l = (vuint8)(d-(&dtop->b[0]) );
        XCP_ASSERT(dtop->l<=kXcpMaxDTO);

        /* Queue or transmit the DTO */
  #if defined ( XCP_ENABLE_SEND_QUEUE )
    #if defined ( XCP_SEND_QUEUE_SAMPLE_ODT )
        /* No action yet */
    #else
        if ( (xcp.SendStatus & (vuint8)XCP_SEND_PENDING) != 0 )
        {
          xcp.QueueLen++;
        }
        else
        {
          xcp.SendStatus |= (vuint8)XCP_DTO_PENDING;
          XcpSendDto(dtop);
        }
    #endif
  #else
        XcpSendDto(&dto);
  #endif /* XCP_ENABLE_SEND_QUEUE */
        next_odt:

        ApplXcpInterruptEnable();

      } /* odt */

  #if defined ( XCP_ENABLE_SEND_QUEUE ) && defined ( XCP_SEND_QUEUE_SAMPLE_ODT )
      (void)XcpSendDtoFromQueue();
  #endif


  #if defined ( XCP_ENABLE_DAQ_PRESCALER )
    }
  #endif
  
  } /* daq */

  END_PROFILE(4); /* Timingtest */
  return status; 
  
}

#endif /* XCP_ENABLE_DAQ */




/****************************************************************************/
/* Command Processor                                                        */
/****************************************************************************/


/*****************************************************************************
| NAME:             XcpDisconnect
| CALLED BY:        XcpCommand
| PRECONDITIONS:    XCP is initialized and in connected state.
| INPUT PARAMETERS: none 
| RETURN VALUES:    none
| DESCRIPTION:      If the XCP slave is connected to a XCP master a call of this
|                   function discontinues the connection (transition to disconnected state).
|                   If the XCP slave is not connected this function performs no action.
******************************************************************************/
void XcpDisconnect( void )
{
  

  xcp.SessionStatus &= (SessionStatusType)(~SS_CONNECTED & 0xFFu);

#if defined ( XCP_ENABLE_DAQ )
  XcpStopAllDaq();
#endif


  
}


/*****************************************************************************
| NAME:             XcpCommand
| CALLED BY:        XcpSendCallBack, XCP Transport Layer
| PRECONDITIONS:    none
| INPUT PARAMETERS: pCmd : data of received CTO message.
| RETURN VALUES:    none
| DESCRIPTION:      
******************************************************************************/
void XcpCommand( const vuint32* pCommand )
{
  const tXcpCto* pCmd = (const tXcpCto*) pCommand; 
  vuint8 err;

  
    
  /* XCP Command Handler */

  BEGIN_PROFILE(1); /* Timingtest */

  /* CONNECT */
  if (CRO_CMD==CC_CONNECT)
  {

    /* Prepare the default response */
    CRM_CMD = 0xFF; /* No Error */
    xcp.CrmLen = 1; /* Length = 1 */

#if defined ( XCP_ENABLE_TESTMODE )
    if ( gDebugLevel != 0) 
    {
      ApplXcpPrint("\n-> CONNECT mode=%u\n",CRO_CONNECT_MODE);
    }
#endif

    /* DPRAM */
    /* DPRAM Client */
    
    /* Reset DAQ */
    /* Do not reset DAQ if in resume mode */ 
#if defined ( XCP_ENABLE_DAQ )
    if ( (xcp.SessionStatus & (SessionStatusType)SS_RESUME) == 0 )
    {
      XcpFreeDaq();
  #if defined ( XCP_ENABLE_SEND_QUEUE )
      xcp.SendStatus = 0; /* Clear all transmission flags */
  #endif
    }
#endif /* XCP_ENABLE_DAQ */



    /* Reset Session Status */
    xcp.SessionStatus = (SessionStatusType)SS_CONNECTED;

    xcp.CrmLen = CRM_CONNECT_LEN;

    /* Versions of the XCP Protocol Layer and Transport Layer Specifications. */
    CRM_CONNECT_TRANSPORT_VERSION = (vuint8)( (vuint16)XCP_TRANSPORT_LAYER_VERSION >> 8 );
    CRM_CONNECT_PROTOCOL_VERSION =  (vuint8)( (vuint16)XCP_VERSION >> 8 );

    CRM_CONNECT_MAX_CTO_SIZE = kXcpMaxCTO;
    CRM_CONNECT_MAX_DTO_SIZE_WRITE(kXcpMaxDTO); 

#if defined ( XCP_ENABLE_CALIBRATION_PAGE )
    CRM_CONNECT_RESOURCE = RM_CAL_PAG;            /* Calibration */
#else
    CRM_CONNECT_RESOURCE = 0x00;                  /* Reset resource mask */
#endif
#if defined ( XCP_ENABLE_DAQ )
    CRM_CONNECT_RESOURCE |= (vuint8)RM_DAQ;       /* Data Acquisition */
#endif

    CRM_CONNECT_COMM_BASIC = 0;
#if defined ( XCP_ENABLE_COMM_MODE_INFO )
    CRM_CONNECT_COMM_BASIC |= (vuint8)CMB_OPTIONAL;
#endif
#if defined ( XCP_CPUTYPE_BIGENDIAN )
    CRM_CONNECT_COMM_BASIC |= (vuint8)PI_MOTOROLA;
#endif

    XCP_PRINT(("<- 0xFF version=%02Xh/%02Xh, maxcro=%02Xh, maxdto=%02Xh, resource=%02X, mode=%02X\n",
        CRM_CONNECT_PROTOCOL_VERSION,
        CRM_CONNECT_TRANSPORT_VERSION,
        CRM_CONNECT_MAX_CTO_SIZE,
        CRM_CONNECT_MAX_DTO_SIZE,
        CRM_CONNECT_RESOURCE,
        CRM_CONNECT_COMM_BASIC));

    goto positive_response; 

  }

  /* Handle other commands only if connected */
  else /* CC_CONNECT */
  {
    if ( (xcp.SessionStatus & (SessionStatusType)SS_CONNECTED) != 0 )
    {
      /* Ignore commands if the previous command sequence has not been completed */
#if defined ( XCP_ENABLE_SEND_QUEUE )
      if ( (xcp.SendStatus & (vuint8)(XCP_CRM_PENDING|XCP_CRM_REQUEST)) != 0 )
      {
        xcp.SessionStatus |= (SessionStatusType)SS_ERROR; 
        END_PROFILE(1); /* Timingtest */

        /* No response */
        return;
      }
#endif

      #if defined ( XCP_ENABLE_GET_SESSION_STATUS_API )
        xcp.SessionStatus |= (SessionStatusType)SS_POLLING;
      #endif

      /* Prepare the default response */
      CRM_CMD = 0xFF; /* No Error */
      xcp.CrmLen = 1; /* Length = 1 */

      switch (CRO_CMD)
      {

        case CC_SYNC:
          {
            /* Always return a negative response with the error code ERR_CMD_SYNCH. */
            xcp.CrmLen = CRM_SYNCH_LEN;
            CRM_CMD    = PID_ERR;
            CRM_ERR    = CRC_CMD_SYNCH;
           
#if defined ( XCP_ENABLE_TESTMODE )
            if ( gDebugLevel != 0) 
            {
              ApplXcpPrint("-> SYNC\n");
              ApplXcpPrint("<- 0xFE 0x00\n");
            }
#endif
          }
          break;


#if defined ( XCP_ENABLE_COMM_MODE_INFO )
        case CC_GET_COMM_MODE_INFO:
          {
            xcp.CrmLen = CRM_GET_COMM_MODE_INFO_LEN;
            /* Transmit the version of the XCP Protocol Layer implementation.    */
            /* The higher nibble is the main version, the lower the sub version. */
            /* The lower nibble overflows, if the sub version is greater than 15.*/
            CRM_GET_COMM_MODE_INFO_DRIVER_VERSION = (vuint8)( ((CP_XCP_VERSION & 0x0F00) >> 4) |
                                                               (CP_XCP_VERSION & 0x000F)         );
            CRM_GET_COMM_MODE_INFO_COMM_OPTIONAL = 0;
            CRM_GET_COMM_MODE_INFO_QUEUE_SIZE = 0;
            CRM_GET_COMM_MODE_INFO_MAX_BS = 0;
            CRM_GET_COMM_MODE_INFO_MIN_ST = 0;

  #if defined ( XCP_ENABLE_TESTMODE )
            if ( gDebugLevel != 0)
            {
              ApplXcpPrint("-> GET_COMM_MODE_INFO\n");
              ApplXcpPrint("<- 0xFF \n");
            }
  #endif

          }
          break;
#endif /* XCP_ENABLE_COMM_MODE_INFO */


          case CC_DISCONNECT:
            {
              xcp.CrmLen = CRM_DISCONNECT_LEN;
              XcpDisconnect();

#if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> DISCONNECT\n");
                ApplXcpPrint("<- 0xFF\n");
              }
#endif
            }
            break;

                       
#if defined ( kXcpStationIdLength ) 
          case CC_GET_ID:
            {
              xcp.CrmLen = CRM_GET_ID_LEN;
              CRM_GET_ID_MODE = 0;
              CRM_GET_ID_LENGTH_WRITE(0x00); 

  #if defined ( kXcpStationIdLength )
              if ( CRO_GET_ID_TYPE == IDT_ASAM_NAME )   /* Type = ASAM MC2 */
              {
                CRM_GET_ID_LENGTH_WRITE((vuint32)kXcpStationIdLength); 
                XcpSetMta( ApplXcpGetPointer(0xFF, (vuint32)(&kXcpStationId[0])), 0xFF); 
              } 
  #endif
              
  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> GET_ID type=%u\n",CRO_GET_ID_TYPE);
                ApplXcpPrint("<- 0xFF mode=%u,len=%u\n",CRM_GET_ID_MODE,CRM_GET_ID_LENGTH);
              }
  #endif
            }
            break;                    
#endif


          case CC_GET_STATUS: 
            {
              xcp.CrmLen = CRM_GET_STATUS_LEN;
              CRM_GET_STATUS_STATUS = (vuint8)xcp.SessionStatus;

              CRM_GET_STATUS_PROTECTION = 0;


              /* Session configuration ID not available. */
              CRM_GET_STATUS_CONFIG_ID_WRITE(0x00);

#if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> GET_STATUS\n");
                ApplXcpPrint("<- 0xFF sessionstatus=%02Xh, protectionstatus=%02X\n",CRM_GET_STATUS_STATUS,CRM_GET_STATUS_PROTECTION);
              }
#endif
            }
            break;


          case CC_SET_MTA:
            {
#if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> SET_MTA addr=%08Xh, addrext=%02Xh\n",CRO_SET_MTA_ADDR,CRO_SET_MTA_EXT);
              }
#endif
              XcpSetMta(ApplXcpGetPointer(CRO_SET_MTA_EXT,CRO_SET_MTA_ADDR),CRO_SET_MTA_EXT);


#if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF\n");
              }
#endif
            }
            break;


          case CC_DOWNLOAD: 
            {
#if defined ( XCP_ENABLE_CALIBRATION )
              vuint8 size;

#if defined ( XCP_ENABLE_TESTMODE )
              if (gDebugLevel && (CRO_CMD != CC_DOWNLOAD_NEXT))
              {
                vuint16 i;
                ApplXcpPrint("-> DOWNLOAD size=%u, data=",CRO_DOWNLOAD_SIZE);
                for (i=0;(i<CRO_DOWNLOAD_SIZE) && (i<CRO_DOWNLOAD_MAX_SIZE);i++)
                {
                  ApplXcpPrint("%02X ",CRO_DOWNLOAD_DATA[i]);
                }
                ApplXcpPrint("\n");
              }
#endif

             
              size = CRO_DOWNLOAD_SIZE;
              if (size>CRO_DOWNLOAD_MAX_SIZE)
              {
                error(CRC_OUT_OF_RANGE)
              }

              err = XcpWriteMta(size,CRO_DOWNLOAD_DATA);
              if (err==(vuint8)XCP_CMD_PENDING) 
              {
                goto no_response;
              }
              if (err==(vuint8)XCP_CMD_DENIED)
              {
                error(CRC_WRITE_PROTECTED) 
              }
              if (err==(vuint8)XCP_CMD_SYNTAX)
              {
                error(CRC_CMD_SYNTAX) 
              }

                    
#if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF\n");
              }
#endif
#else
              error(CRC_CMD_UNKNOWN) 
#endif /* !defined ( XCP_ENABLE_CALIBRATION ) */
                              
            }
            break;

          case CC_DOWNLOAD_MAX:
            {
#if defined ( XCP_ENABLE_CALIBRATION )
  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                vuint16 i;
                ApplXcpPrint("DOWNLOAD_MAX data=");
                for (i=0;i<CRO_DOWNLOAD_MAX_MAX_SIZE;i++)
                {
                  ApplXcpPrint("%02X ",CRO_DOWNLOAD_MAX_DATA[i]);
                }
                ApplXcpPrint("\n");
              }
  #endif

             
              err = XcpWriteMta(CRO_DOWNLOAD_MAX_MAX_SIZE,CRO_DOWNLOAD_MAX_DATA);
              if (err==(vuint8)XCP_CMD_PENDING)
              {
                return;
              }
              if (err==(vuint8)XCP_CMD_DENIED)
              {
                error(CRC_WRITE_PROTECTED) 
              }
              if (err==(vuint8)XCP_CMD_SYNTAX)
              {
                error(CRC_CMD_SYNTAX) 
              }

  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF\n");
              }
  #endif
#else
              error(CRC_ACCESS_DENIED) 
#endif /* !defined ( XCP_ENABLE_CALIBRATION ) */
            }
            break;



          case CC_UPLOAD:
            {
              vuint8 size = CRO_UPLOAD_SIZE;

#if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> UPLOAD size=%u\n",size);
              }
#endif

              if ( size > (vuint8)CRM_UPLOAD_MAX_SIZE )
              {
                error(CRC_OUT_OF_RANGE) 
              }
              err = XcpReadMta(size,CRM_UPLOAD_DATA);
              xcp.CrmLen = (vuint8)((CRM_UPLOAD_LEN+size)&0xFFu);
              if (err==(vuint8)XCP_CMD_PENDING)
              {
                goto no_response; 
              }
              if (err==(vuint8)XCP_CMD_DENIED)
              {
                error(CRC_ACCESS_DENIED) 
              }

#if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                vuint16 i;
                ApplXcpPrint("<- 0xFF data=");
                for (i=0;i<size;i++) 
                {
                  ApplXcpPrint("%02X ",CRM_UPLOAD_DATA[i]);
                }
                ApplXcpPrint("\n");
              }
#endif
            }
            break;

          case CC_SHORT_UPLOAD:
            {
#if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> SHORT_UPLOAD addr=%08Xh, addrext=%02Xh, size=%u\n",CRO_SHORT_UPLOAD_ADDR,CRO_SHORT_UPLOAD_EXT,CRO_SHORT_UPLOAD_SIZE);
              }
#endif

#if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if (CRO_SHORT_UPLOAD_SIZE > (vuint8)CRM_SHORT_UPLOAD_MAX_SIZE)
              {
                error(CRC_OUT_OF_RANGE) 
              }
#endif
              XcpSetMta(ApplXcpGetPointer(CRO_SHORT_UPLOAD_EXT,CRO_SHORT_UPLOAD_ADDR),CRO_SHORT_UPLOAD_EXT);
              err = XcpReadMta(CRO_SHORT_UPLOAD_SIZE,CRM_SHORT_UPLOAD_DATA);
              xcp.CrmLen = (vuint8)((CRM_SHORT_UPLOAD_LEN+CRO_SHORT_UPLOAD_SIZE)&0xFFu);
              if (err==(vuint8)XCP_CMD_PENDING)
              {
                goto no_response; 
              }
              if (err==(vuint8)XCP_CMD_DENIED)
              {
                error(CRC_ACCESS_DENIED) 
              }

#if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                vuint16 i;
                ApplXcpPrint("<- 0xFF data=");
                for (i=0; i < (vuint16)CRO_SHORT_UPLOAD_SIZE; i++)
                {
                  ApplXcpPrint("%02X ",CRM_SHORT_UPLOAD_DATA[i]);
                }
                ApplXcpPrint("\n");
              }
#endif
            }
            break;





#if defined ( XCP_ENABLE_DAQ )

  #if defined ( XCP_ENABLE_DAQ_PROCESSOR_INFO )

          case CC_GET_DAQ_PROCESSOR_INFO: 
            {
    #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> GET_DAQ_PROCESSOR_INFO\n");
              }
    #endif

              xcp.CrmLen = CRM_GET_DAQ_PROCESSOR_INFO_LEN;
              CRM_GET_DAQ_PROCESSOR_INFO_MIN_DAQ = 0;          
             
              CRM_GET_DAQ_PROCESSOR_INFO_MAX_DAQ_WRITE(xcp.Daq.DaqCount); /* dynamic or static */ 
    #if defined ( kXcpMaxEvent )
              CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT_WRITE(kXcpMaxEvent);
    #else
              CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT_WRITE(0x00); /* Unknown */    
    #endif
    #if defined ( XCP_ENABLE_DAQ_HDR_ODT_DAQ )
              /* DTO identification field type: Relative ODT number, absolute list number (BYTE) */
              CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE = (vuint8)DAQ_HDR_ODT_DAQB;
    #else
              /* DTO identification field type: Absolute ODT number */
              CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE = (vuint8)DAQ_HDR_PID;
    #endif
              CRM_GET_DAQ_PROCESSOR_INFO_PROPERTIES = (vuint8)( DAQ_PROPERTY_CONFIG_TYPE
    #if defined ( XCP_ENABLE_DAQ_TIMESTAMP )
                | DAQ_PROPERTY_TIMESTAMP
    #endif
    #if defined ( XCP_ENABLE_DAQ_PRESCALER )
                | DAQ_PROPERTY_PRESCALER
    #endif
    #if defined ( XCP_ENABLE_DAQ_OVERRUN_INDICATION ) /* DAQ_PROPERTY_OVERLOAD_INDICATION */
                | DAQ_OVERLOAD_INDICATION_PID
    #endif
                );

    #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF\n");
              }
    #endif
            }
            break;

  #endif /* XCP_ENABLE_DAQ_PROCESSOR_INFO */

  #if defined ( XCP_ENABLE_DAQ_RESOLUTION_INFO )

            case CC_GET_DAQ_RESOLUTION_INFO: 
              {
    #if defined ( XCP_ENABLE_TESTMODE )
                if ( gDebugLevel != 0) 
                {
                  ApplXcpPrint("-> GET_DAQ_RESOLUTION_INFO\n");
                }
    #endif

                xcp.CrmLen = CRM_GET_DAQ_RESOLUTION_INFO_LEN;
                CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_DAQ = 1;
                CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_STIM = 1;
                CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_DAQ  = (vuint8)XCP_MAX_ODT_ENTRY_SIZE;
                CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_STIM = (vuint8)XCP_MAX_ODT_ENTRY_SIZE;
    #if defined ( XCP_ENABLE_DAQ_TIMESTAMP )
                CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE = kXcpDaqTimestampUnit | (vuint8)kXcpDaqTimestampSize
      #if defined ( XCP_ENABLE_DAQ_TIMESTAMP_FIXED )
                | DAQ_TIMESTAMP_FIXED
      #endif
                ;
                CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS_WRITE(kXcpDaqTimestampTicksPerUnit);  /* BCD coded */ 
    #else
                CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE = 0;
                CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS_WRITE(0x00);
    #endif /* XCP_ENABLE_DAQ_TIMESTAMP */
          
    #if defined ( XCP_ENABLE_TESTMODE )
                if ( gDebugLevel != 0)
                {
                  ApplXcpPrint("<- 0xFF , mode=%02Xh, , ticks=%02Xh\n",CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE,CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS);
                }
    #endif
              }
              break;
  #endif /* XCP_ENABLE_DAQ_RESOLUTION_INFO */

  #if defined ( XCP_ENABLE_DAQ_EVENT_INFO ) && defined ( kXcpMaxEvent )
            case CC_GET_DAQ_EVENT_INFO: 
              {
                vuint8 event = (vuint8)CRO_GET_DAQ_EVENT_INFO_EVENT;

    #if defined ( XCP_ENABLE_TESTMODE )
                if ( gDebugLevel != 0)
                {
                  ApplXcpPrint("-> GET_DAQ_EVENT_INFO event=%u\n",event);
                }
    #endif

    #if defined ( XCP_ENABLE_PARAMETER_CHECK )
                if (event >= (vuint8)kXcpMaxEvent )
                {
                  error(CRC_OUT_OF_RANGE) 
                } 
    #endif

                xcp.CrmLen = CRM_GET_DAQ_EVENT_INFO_LEN;
                CRM_GET_DAQ_EVENT_INFO_PROPERTIES = kXcpEventDirection[event];
                CRM_GET_DAQ_EVENT_INFO_MAX_DAQ_LIST = 1; /* Only one DAQ-List available per event channel */
                CRM_GET_DAQ_EVENT_INFO_NAME_LENGTH = kXcpEventNameLength[event];
                CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE = kXcpEventCycle[event];
    #if defined ( XCP_ENABLE_CANAPE_5_5_X_SUPPORT )
                CRM_GET_DAQ_EVENT_INFO_TIME_UNIT = kXcpEventUnit[event];
    #else
                CRM_GET_DAQ_EVENT_INFO_TIME_UNIT = (vuint8)(kXcpEventUnit[event]>>4); /* ESCAN00090639 */
    #endif
                CRM_GET_DAQ_EVENT_INFO_PRIORITY = 0;     /* Event channel prioritization is not supported. */
                XcpSetMta( ApplXcpGetPointer( 0xFF, (vuint32)kXcpEventName[event]), 0xFF ); 

    #if defined ( XCP_ENABLE_TESTMODE )
                if ( gDebugLevel != 0)
                {
                  ApplXcpPrint("<- 0xFF name=%s, unit=%u, cycle=%u\n",kXcpEventName[event],CRM_GET_DAQ_EVENT_INFO_TIME_UNIT,CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE);
                }
    #endif
              }
              break;
  #endif /* XCP_ENABLE_DAQ_EVENT_INFO && kXcpMaxEvent */


          case CC_FREE_DAQ:
            {
  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> FREE_DAQ\n");
              }
  #endif

             
              XcpFreeDaq();

  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF\n");
              }
  #endif
            }
            break;

          case CC_ALLOC_DAQ:
            {
              vuint8 count = (vuint8)CRO_ALLOC_DAQ_COUNT;
 
  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> ALLOC_DAQ count=%u\n",count);
              }
  #endif

              check_error( XcpAllocDaq(count) ) 

  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF\n");
              }
  #endif
            }
            break;

          case CC_ALLOC_ODT:
            {
              vuint8 daq = (vuint8)CRO_ALLOC_ODT_DAQ;
              vuint8 count = CRO_ALLOC_ODT_COUNT;
 
  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> ALLOC_ODT daq=%u, count=%u\n",daq,count);
              }
  #endif

  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if (daq>=xcp.Daq.DaqCount)
              {
                error(CRC_OUT_OF_RANGE) 
              }
  #endif

              check_error( XcpAllocOdt(daq, count) ) 

  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF\n");
              }
  #endif
            }
            break;

          case CC_ALLOC_ODT_ENTRY:
            {
              vuint8 daq = (vuint8)CRO_ALLOC_ODT_ENTRY_DAQ;
              vuint8 odt = CRO_ALLOC_ODT_ENTRY_ODT;
              vuint8 count = CRO_ALLOC_ODT_ENTRY_COUNT;
 
  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> ALLOC_ODT_ENTRY daq=%u, odt=%u, count=%u\n",daq,odt,count);
              }
  #endif

  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if ( (daq>=xcp.Daq.DaqCount) || (odt>=(vuint8)DaqListOdtCount(daq)) )
              {
                error(CRC_OUT_OF_RANGE) 
              }
  #endif

              check_error( XcpAllocOdtEntry(daq, odt, count) ) 

  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF\n");
              }
  #endif
            }
            break;

          case CC_GET_DAQ_LIST_MODE:
            {
              vuint8 daq = (vuint8)CRO_GET_DAQ_LIST_MODE_DAQ;

  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> GET_DAQ_LIST_MODE daq=%u\n",daq);
              }
  #endif

  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if (daq>=xcp.Daq.DaqCount)
              {
                error(CRC_OUT_OF_RANGE) 
              }
  #endif
          
              xcp.CrmLen = CRM_GET_DAQ_LIST_MODE_LEN;
              CRM_GET_DAQ_LIST_MODE_MODE = DaqListFlags(daq);
  #if defined ( XCP_ENABLE_DAQ_PRESCALER )
              CRM_GET_DAQ_LIST_MODE_PRESCALER = DaqListPrescaler(daq);
  #else
              CRM_GET_DAQ_LIST_MODE_PRESCALER = 1;
  #endif
  #if defined ( kXcpMaxEvent )
              CRM_GET_DAQ_LIST_MODE_EVENTCHANNEL_WRITE(0); /* #### Lookup in EventDaq[] */ 
  #else
              CRM_GET_DAQ_LIST_MODE_EVENTCHANNEL_WRITE(DaqListEventChannel(daq));
  #endif
              CRM_GET_DAQ_LIST_MODE_PRIORITY = 0;  /* DAQ-list prioritization is not supported. */

  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF mode=%02X, prescaler=%u, eventChannel=%u, priority=%u, /*maxOdtEntrySize=%u*/  \n",
                  CRM_GET_DAQ_LIST_MODE_MODE,CRM_GET_DAQ_LIST_MODE_PRESCALER,CRM_GET_DAQ_LIST_MODE_EVENTCHANNEL,CRM_GET_DAQ_LIST_MODE_PRIORITY);
              }
  #endif
            }
            break;

          case CC_SET_DAQ_LIST_MODE:
            {
              vuint8 daq = (vuint8)CRO_SET_DAQ_LIST_MODE_DAQ;
  #if defined ( XCP_ENABLE_TESTMODE ) || defined ( XCP_ENABLE_DAQ_PRESCALER ) || ( !defined ( XCP_ENABLE_DAQ_PRESCALER ) && defined ( XCP_ENABLE_PARAMETER_CHECK ) )
              vuint8 xcpPrescaler = CRO_SET_DAQ_LIST_MODE_PRESCALER;
  #endif
              vuint8 event = (vuint8)(CRO_SET_DAQ_LIST_MODE_EVENTCHANNEL&0xFFu);
 
  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> SET_DAQ_LIST_MODE daq=%u, mode=%02Xh, prescaler=%u, eventchannel=%u\n",
                  daq,CRO_SET_DAQ_LIST_MODE_MODE,xcpPrescaler,event);
              }
  #endif

  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if (daq>=xcp.Daq.DaqCount)
              {
                error(CRC_OUT_OF_RANGE) 
              } 
    #if defined ( kXcpMaxEvent )
              if (event >= (vuint8)kXcpMaxEvent)
              {
                error(CRC_OUT_OF_RANGE) 
              } 
    #endif
    #if !defined ( XCP_ENABLE_DAQ_PRESCALER )
              if (xcpPrescaler!=1)
              {
                error(CRC_OUT_OF_RANGE) 
              }
    #endif
              if (CRO_SET_DAQ_LIST_MODE_PRIORITY!=0)   /* Priorization is not supported */
              {
                error(CRC_OUT_OF_RANGE) 
              } 
  #endif

  #if defined ( XCP_ENABLE_DAQ_PRESCALER )
              if (xcpPrescaler==0)
              {
                xcpPrescaler = 1;
              }
              DaqListPrescaler(daq) = xcpPrescaler;
  #endif
  #if defined ( kXcpMaxEvent ) && ! defined ( XCP_ENABLE_DAQ_PRESCALER )
              xcp.Daq.EventDaq[event] = daq;
  #else
              DaqListEventChannel(daq) = event;
  #endif
              DaqListFlags(daq) = CRO_SET_DAQ_LIST_MODE_MODE;


  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF\n");
              }
  #endif
              break;
            }


          case CC_SET_DAQ_PTR:
            {
              vuint8 daq = (vuint8) (CRO_SET_DAQ_PTR_DAQ&0xFFu);
              vuint8 odt = CRO_SET_DAQ_PTR_ODT;
              vuint8 idx = CRO_SET_DAQ_PTR_IDX;
              tXcpOdtIdx odt0 = (tXcpOdtIdx)(DaqListFirstOdt(daq)+odt); /* Absolute odt number */

  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> SET_DAQ_PTR daq=%u,odt=%u,idx=%u\n",daq,odt,idx);
              }
  #endif

  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if ( (daq>=xcp.Daq.DaqCount) || (odt>=(vuint8)DaqListOdtCount(daq)) || (idx>=(vuint8)DaqListOdtEntryCount(odt0)) )
              {
                error(CRC_OUT_OF_RANGE) 
              } 
  #endif

              xcp.CrmLen = CRM_SET_DAQ_PTR_LEN;
              xcp.DaqListPtr = DaqListOdtFirstEntry(odt0)+idx;
  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF \n");
              }
  #endif
            }
            break;

          case CC_WRITE_DAQ: /* Write DAQ entry */
            {
              DAQBYTEPTR addr;
  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> WRITE_DAQ size=%u,addr=%08Xh,%02Xh\n",CRO_WRITE_DAQ_SIZE,CRO_WRITE_DAQ_ADDR,CRO_WRITE_DAQ_EXT);
              }
  #endif

           
  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if ( ((vuint8)CRO_WRITE_DAQ_SIZE==(vuint8)0u ) || (CRO_WRITE_DAQ_SIZE > (vuint8)XCP_MAX_ODT_ENTRY_SIZE) )
              {
                error(CRC_OUT_OF_RANGE) 
              }
              if ( (0u == xcp.Daq.DaqCount) || (0u == xcp.Daq.OdtCount) || (0u == xcp.Daq.OdtEntryCount) )
              {
                error(CRC_DAQ_CONDIF) 
              } 
  #endif
              addr = (DAQBYTEPTR)ApplXcpGetPointer(CRO_WRITE_DAQ_EXT,CRO_WRITE_DAQ_ADDR);

              xcp.CrmLen = CRM_WRITE_DAQ_LEN;
              OdtEntrySize(xcp.DaqListPtr) = CRO_WRITE_DAQ_SIZE;
              OdtEntryAddr(xcp.DaqListPtr) = addr;
          
              xcp.DaqListPtr++; /* Autoincrement */
          
  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF\n");
              }
  #endif
            }
            break;

          case CC_START_STOP_DAQ_LIST:
            {
              vuint8 daq = (vuint8)(CRO_START_STOP_DAQ&0xFFu);

              
  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if (daq>=xcp.Daq.DaqCount)
              {
                error(CRC_OUT_OF_RANGE) 
              } 
  #endif

              if ( (CRO_START_STOP_MODE==1 ) || (CRO_START_STOP_MODE==2) )
              {
                DaqListFlags(daq) |= (vuint8)DAQ_FLAG_SELECTED;
  #if defined ( XCP_ENABLE_TESTMODE )
                  if ( gDebugLevel != 0)
                  {
                    XcpPrintDaqList((vuint8)(CRO_START_STOP_DAQ)&0xFFu);
                    ApplXcpPrint("-> START_STOP mode=%02Xh, daq=%u\n",CRO_START_STOP_MODE,CRO_START_STOP_DAQ);
                  }
  #endif
                if ( CRO_START_STOP_MODE == (vuint8)1u )
                {
                  XcpStartDaq(daq);
                }
                xcp.CrmLen = CRM_START_STOP_LEN;
                CRM_START_STOP_FIRST_PID = DaqListFirstPid(daq);
              } 
              else
              {
                XcpStopDaq(daq);
  #if defined ( XCP_ENABLE_TESTMODE )
                if ( gDebugLevel != 0)
                {
                  ApplXcpPrint("-> START_STOP mode=%02Xh\n",CRO_START_STOP_MODE);
                }
  #endif
              }

  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF\n");
              }
  #endif
            }
            break;

          case CC_START_STOP_SYNCH:
            {
  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> CC_START_STOP_SYNCH mode=%02Xh\n",CRO_START_STOP_MODE);
              }
  #endif

             
              if( (0 == xcp.Daq.DaqCount) || (0 == xcp.Daq.OdtCount) || (0 == xcp.Daq.OdtEntryCount) )
              {
                error(CRC_DAQ_CONDIF) 
              }

              if (CRO_START_STOP_MODE==2) /* stop selected */
              {
                XcpStopAllSelectedDaq();
              } 
              else
              {
                if (CRO_START_STOP_MODE==1) /* start selected */
                {
                  XcpStartAllSelectedDaq();
                }
                else
                {
                  /* CRO_START_STOP_MODE==0 : stop all */
                  XcpStopAllDaq();
                }
              }
  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("<- 0xFF\n");
              }
  #endif
            }
            break;

  #if defined ( XCP_ENABLE_DAQ_TIMESTAMP )
          case CC_GET_DAQ_CLOCK:
            {
              xcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN;
             
              CRM_GET_DAQ_CLOCK_TIME_WRITE((vuint32)ApplXcpGetTimestamp()); 

    #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> GET_DAQ_CLOCK\n");
                ApplXcpPrint("<- 0xFF time=%04Xh\n",CRM_GET_DAQ_CLOCK_TIME);
              }
    #endif
            }
            break;
  #endif

#endif /* XCP_ENABLE_DAQ */


          
               
          default: /* unknown */
            {
  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0)
              {
                ApplXcpPrint("-> UNKNOWN COMMAND %02X\n", CRO_CMD);
              }
  #endif
              error(CRC_CMD_UNKNOWN) 
            }

      } /* switch */

      goto positive_response; 
    }

    /* Not connected */
    else
    {
      goto no_response; 
    }
  } /* CC_CONNECT */

negative_response:
  xcp.CrmLen = 2;


  CRM_CMD = (vuint8)PID_ERR;
  CRM_ERR = (vuint8)err;
#if defined ( XCP_ENABLE_TESTMODE )
  if ( gDebugLevel != 0) 
  {
    ApplXcpPrint("<- 0xFE error=%02Xh\n",err);
  }
#endif
 
positive_response:
  XcpSendCrm();

no_response:
  END_PROFILE(1); /* Timingtest */
  return;
  
}


/****************************************************************************/
/* Send notification callback                                               */
/****************************************************************************/


/*****************************************************************************
| NAME:             XcpSendCallBack
| CALLED BY:        XCP Transport Layer
| PRECONDITIONS:    none
| INPUT PARAMETERS: none
| RETURN VALUES:    0 : if the XCP Protocol Layer is idle (no transmit messages are pending)
| DESCRIPTION:      Notifies the XCP Protocol Layer about the successful
|                   transmission of a XCP packet.
******************************************************************************/
vuint8 XcpSendCallBack( void )
{
  BEGIN_PROFILE(2); /* Timingtest */

  #if defined ( XCP_ENABLE_DAQ ) && defined ( XCP_ENABLE_SEND_QUEUE )

  /* Clear all pending flags */
  /* A pending flag indicates that ApplXcpSend() is in progress */
  xcp.SendStatus &= (vuint8)(~XCP_SEND_PENDING & 0xFFu);

  /* Now check if there is another transmit request */
 
  /* Send a RES or ERR (CRM) message */
  if ( (xcp.SendStatus & (vuint8)XCP_CRM_REQUEST) != 0 )
  {
    xcp.SendStatus &= (vuint8)(~XCP_CRM_REQUEST & 0xFFu);
    XcpSendCrm();
    END_PROFILE(2); /* Timingtest */
    return (vuint8)0x01u;
  }

  

  /* Send a DAQ message from the queue or from the buffer */
  if ( (xcp.SessionStatus & (SessionStatusType)SS_DAQ) != 0 )
  {
    if ( XcpSendDtoFromQueue() != 0 )
    {
      END_PROFILE(2); /* Timingtest */
      return (vuint8)0x01u;
    }
  }
#endif /* XCP_ENABLE_DAQ && XCP_ENABLE_SEND_QUEUE */

  /* Continue a pending block upload command */

  END_PROFILE(2); /* Timingtest */
  return (vuint8)0x00u;
  
}


/****************************************************************************/
/* Initialization / de-initialization                                       */
/****************************************************************************/


/*****************************************************************************
| NAME:             XcpInit
| CALLED BY:        application
| PRECONDITIONS:    the data link layer has to be initialized.
| INPUT PARAMETERS: none
| RETURN VALUES:    none
| DESCRIPTION:      Initialization of the XCP Protocol Layer
|                   Application specific initialization
|                    ( e.g. Vector XCP on CAN Transport Layer )
******************************************************************************/
void XcpInit( void )
{

  /* Application specific initialization function. */
  ApplXcpInit();

  /* Initialize all XCP variables to zero */
  XcpMemClr((BYTEPTR)&xcp,(vuint16)sizeof(xcp)); 
   
  /* Initialize the session status */
  xcp.SessionStatus = (SessionStatusType)0u;

  #if defined ( XCP_ENABLE_SEND_QUEUE)
    /* Initialize the transmit queue  */
    xcp.SendStatus = (vuint8)0u;
  #endif

  
}



#if defined ( XCP_ENABLE_GET_SESSION_STATUS_API )
/*****************************************************************************
| NAME:             XcpGetSessionStatus
| CALLED BY:        -
| PRECONDITIONS:    none
| INPUT PARAMETERS: none
| RETURN VALUES:    Xcp session state
| DESCRIPTION:      Get the session state of the XCP Protocol Layer
******************************************************************************/
SessionStatusType XcpGetSessionStatus( void )
{
  SessionStatusType result;

  result = xcp.SessionStatus;
  /* Reset the polling state */
  xcp.SessionStatus &= (SessionStatusType)(~SS_POLLING);
  return(result);
} 
#endif



/****************************************************************************/
/* Test                                                                     */
/* Some screen output functions for test and diagnostics                    */
/****************************************************************************/


#if defined ( XCP_ENABLE_TESTMODE )
  #if defined ( XCP_ENABLE_DAQ )

/*****************************************************************************
| NAME:             XcpPrintDaqList
| CALLED BY:        
| PRECONDITIONS:    none
| INPUT PARAMETERS: 
| RETURN VALUES:    none
| DESCRIPTION:      Print all DAQ lists to screen
******************************************************************************/
void XcpPrintDaqList( vuint8 daq )
{
  vuint8 i;
  vuint16 e;
    
  if (daq>=xcp.Daq.DaqCount)
  {
    return;
  }

  ApplXcpPrint("DAQ %u:\n",daq);
    #if defined ( kXcpMaxEvent ) 
  for (i=0;i<kXcpMaxEvent;i++)
  {
    if (xcp.Daq.EventDaq[i]==daq)
    {
      ApplXcpPrint(" eventchannel=%04Xh,",i);
    }
  }
    #else
  ApplXcpPrint(" eventchannel=%04Xh,",DaqListEventChannel(daq));
    #endif
    #if defined (XCP_ENABLE_DAQ_PRESCALER )
  ApplXcpPrint(" prescaler=%u,",DaqListPrescaler(daq));
    #endif
  ApplXcpPrint(" firstOdt=%u,",DaqListFirstOdt(daq));
  ApplXcpPrint(" lastOdt=%u,",DaqListLastOdt(daq));
  ApplXcpPrint(" flags=%02Xh\n",DaqListFlags(daq));
  ApplXcpPrint(" firstPid=%02Xh\n",DaqListFirstPid(daq)); 
  for (i=DaqListFirstOdt(daq);i<=DaqListLastOdt(daq);i++)
  {
    ApplXcpPrint("  ODT %u (%u):\n",i-DaqListFirstOdt(daq),i);
    ApplXcpPrint("   pid=%u:\n",i);
    ApplXcpPrint("   firstOdtEntry=%u,lastOdtEntry=%u:\n",DaqListOdtFirstEntry(i),DaqListOdtLastEntry(i));
    for (e=DaqListOdtFirstEntry(i);e<=DaqListOdtLastEntry(i);e++)
    {
      ApplXcpPrint("   [%08Xh,%u]\n",OdtEntryAddr(e),OdtEntrySize(e));
    }
  } /* j */
} /* Deviation of MISRA rule 82 (more than one return path). */

  #endif /* XCP_ENABLE_DAQ */
#endif /* XCP_ENABLE_TESTMODE */


















