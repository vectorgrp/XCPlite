/*****************************************************************************
| Project Name:   XCP Protocol Layer
|    File Name:   xcp_def.h
|    V1.0 23.9.2020
|
|  Description:   Header of XCP Protocol Layer - XCP default settings
|                 XCP V1.0 slave device driver
|                 Lite Version
|                 Don't change this file !
|***************************************************************************/


#if defined ( __XCP_DEF_H__ )
#else
#define __XCP_DEF_H__


/*------------------------------------------------------------------------------------*/
/* Default settings */

/* XCP transport layer */
#if !defined(XCP_TRANSPORT_LAYER_VERSION)
  #define XCP_TRANSPORT_LAYER_VERSION 0x0100
#endif
#if !defined(kXcpMaxCTO)
  #define kXcpMaxCTO 8      /* Maximum CTO Message Lenght */
#endif
#if !defined(kXcpMaxDTO)
  #define kXcpMaxDTO 8      /* Maximum DTO Message Lenght */
#endif

/* ROM memory qualifiers */
#if !defined(MEMORY_ROM)
  #define MEMORY_ROM const
#endif
#if !defined(V_MEMROM0)
  #define V_MEMROM0
#endif
#if !defined(MEMORY_CONST)
  #define MEMORY_CONST
#endif



/* General settings */
#if !defined(XCP_ENABLE_PARAMETER_CHECK) && !defined(XCP_DISABLE_PARAMETER_CHECK)
  #define XCP_DISABLE_PARAMETER_CHECK
#endif
#if !defined(XCP_ENABLE_COMM_MODE_INFO) && !defined(XCP_DISABLE_COMM_MODE_INFO)
  #define XCP_ENABLE_COMM_MODE_INFO
#endif


/* Block transfer */
#if !defined(XCP_ENABLE_BLOCK_UPLOAD) && !defined(XCP_DISABLE_BLOCK_UPLOAD)
  #define XCP_DISABLE_BLOCK_UPLOAD
#endif
#if !defined(XCP_ENABLE_BLOCK_DOWNLOAD) && !defined(XCP_DISABLE_BLOCK_DOWNLOAD)
  #define XCP_DISABLE_BLOCK_DOWNLOAD
#endif

/* Enable transmission of event messages */
#if !defined(XCP_ENABLE_SEND_EVENT) && !defined(XCP_DISABLE_SEND_EVENT)
  #define XCP_DISABLE_SEND_EVENT
#endif


/* Disable/Enable Interrupts */
/* Has to be defined only if xcpSendCallBack may interrupt xcpEvent */
#if !defined(ApplXcpInterruptDisable)
  #define ApplXcpInterruptDisable()
#endif
#if !defined(ApplXcpInterruptEnable)
  #define ApplXcpInterruptEnable()
#endif

/* Custom initialization not needed */
#if !defined(ApplXcpInit)
  #define ApplXcpInit()
#endif

/* Custom background processing not needed */
#if !defined(ApplXcpBackground)
  #define ApplXcpBackground()
#endif

/* Flush of transmit queue not needed */
#if !defined(ApplXcpSendFlush)
  #define ApplXcpSendFlush()
#endif

/* XCP page switching */
#if !defined ( XCP_ENABLE_CALIBRATION_PAGE ) && !defined ( XCP_DISABLE_CALIBRATION_PAGE )
  #define XCP_DISABLE_CALIBRATION_PAGE
#endif

/* XCP protocol data acquisition parameters (DAQ) */
 
  #if !defined(XCP_ENABLE_SEND_QUEUE) && !defined(XCP_DISABLE_SEND_QUEUE)
    #define XCP_ENABLE_SEND_QUEUE
  #endif
  #if !defined(kXcpDaqMemSize)
    #define kXcpDaqMemSize 256
  #endif
  #if !defined(kXcpStiOdtCount)
    #define kXcpStiOdtCount 1
  #endif
  #if !defined(XCP_ENABLE_DAQ_PROCESSOR_INFO) && !defined(XCP_DISABLE_DAQ_PROCESSOR_INFO)
    #define XCP_ENABLE_DAQ_PROCESSOR_INFO
  #endif
  #if !defined(XCP_ENABLE_DAQ_RESOLUTION_INFO) && !defined(XCP_DISABLE_DAQ_RESOLUTION_INFO)
    #define XCP_ENABLE_DAQ_RESOLUTION_INFO
  #endif
  #if !defined(XCP_ENABLE_DAQ_PRESCALER) && !defined(XCP_DISABLE_DAQ_PRESCALER)
    #define XCP_DISABLE_DAQ_PRESCALER
  #endif
  #if !defined(XCP_ENABLE_DAQ_OVERRUN_INDICATION) && !defined(XCP_DISABLE_DAQ_OVERRUN_INDICATION)
    #define XCP_ENABLE_DAQ_OVERRUN_INDICATION
  #endif
  #if !defined(XCP_ENABLE_DAQ_RESUME) && !defined(XCP_DISABLE_DAQ_RESUME)
    #define XCP_DISABLE_DAQ_RESUME
  #endif
  
  #if !defined(XCP_ENABLE_DAQ_EVENT_INFO) && !defined(XCP_DISABLE_DAQ_EVENT_INFO)
    #define XCP_DISABLE_DAQ_EVENT_INFO
  #endif



/* XCP slave device identification (optional) */
#define kXcpStationIdLength 5    /* Slave device identification length */
#define kXcpStationIdString "xcpPi"  /* Slave device identification */
V_MEMROM0 extern vuint8 MEMORY_ROM kXcpStationId[];

#endif
