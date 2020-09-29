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



/* XCP protocol data acquisition parameters (DAQ) */
  
  #if !defined(kXcpDaqMemSize)
    #define kXcpDaqMemSize 256
  #endif
  #if !defined(XCP_ENABLE_DAQ_PROCESSOR_INFO) && !defined(XCP_DISABLE_DAQ_PROCESSOR_INFO)
    #define XCP_ENABLE_DAQ_PROCESSOR_INFO
  #endif
  #if !defined(XCP_ENABLE_DAQ_RESOLUTION_INFO) && !defined(XCP_DISABLE_DAQ_RESOLUTION_INFO)
    #define XCP_ENABLE_DAQ_RESOLUTION_INFO
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
extern vuint8 MEMORY_ROM kXcpStationId[];

#endif
