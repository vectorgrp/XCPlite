/* xcpLite.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __XCPLITE_H_ 
#define __XCPLITE_H_

/***************************************************************************/
/* Configuration                                                           */
/***************************************************************************/

#include "xcptl_cfg.h"  // Transport layer configuration
#include "xcp_cfg.h" // Protocol layer configuration



/***************************************************************************/
/* Commands                                                                */
/***************************************************************************/

/*-------------------------------------------------------------------------*/
/* Standard Commands */

#define CC_CONNECT                        0xFF
#define CC_DISCONNECT                     0xFE
#define CC_GET_STATUS                     0xFD
#define CC_SYNC                           0xFC

#define CC_GET_COMM_MODE_INFO             0xFB
#define CC_GET_ID                         0xFA
#define CC_SET_REQUEST                    0xF9
#define CC_GET_SEED                       0xF8
#define CC_UNLOCK                         0xF7
#define CC_SET_MTA                        0xF6
#define CC_UPLOAD                         0xF5
#define CC_SHORT_UPLOAD                   0xF4
#define CC_BUILD_CHECKSUM                 0xF3
                                          
#define CC_TRANSPORT_LAYER_CMD            0xF2
#define CC_USER_CMD                       0xF1


/*-------------------------------------------------------------------------*/
/* Calibration Commands*/

#define CC_DOWNLOAD                       0xF0

#define CC_DOWNLOAD_NEXT                  0xEF
#define CC_DOWNLOAD_MAX                   0xEE
#define CC_SHORT_DOWNLOAD                 0xED
#define CC_MODIFY_BITS                    0xEC


/*-------------------------------------------------------------------------*/
/* Page switching Commands (PAG) */

#define CC_SET_CAL_PAGE                   0xEB
#define CC_GET_CAL_PAGE                   0xEA

#define CC_GET_PAG_PROCESSOR_INFO         0xE9
#define CC_GET_SEGMENT_INFO               0xE8
#define CC_GET_PAGE_INFO                  0xE7
#define CC_SET_SEGMENT_MODE               0xE6
#define CC_GET_SEGMENT_MODE               0xE5
#define CC_COPY_CAL_PAGE                  0xE4


/*-------------------------------------------------------------------------*/
/* DATA Acquisition and Stimulation Commands (DAQ/STIM) */
                                          
#define CC_CLEAR_DAQ_LIST                 0xE3
#define CC_SET_DAQ_PTR                    0xE2
#define CC_WRITE_DAQ                      0xE1
#define CC_SET_DAQ_LIST_MODE              0xE0
#define CC_GET_DAQ_LIST_MODE              0xDF
#define CC_START_STOP_DAQ_LIST            0xDE
#define CC_START_STOP_SYNCH               0xDD

#define CC_GET_DAQ_CLOCK                  0xDC
#define CC_READ_DAQ                       0xDB
#define CC_GET_DAQ_PROCESSOR_INFO         0xDA
#define CC_GET_DAQ_RESOLUTION_INFO        0xD9 
#define CC_GET_DAQ_LIST_INFO              0xD8
#define CC_GET_DAQ_EVENT_INFO             0xD7

#define CC_FREE_DAQ                       0xD6
#define CC_ALLOC_DAQ                      0xD5
#define CC_ALLOC_ODT                      0xD4
#define CC_ALLOC_ODT_ENTRY                0xD3


/*-------------------------------------------------------------------------*/
/* Non volatile memory Programming Commands PGM */

#define CC_PROGRAM_START                  0xD2
#define CC_PROGRAM_CLEAR                  0xD1
#define CC_PROGRAM                        0xD0
#define CC_PROGRAM_RESET                  0xCF
#define CC_GET_PGM_PROCESSOR_INFO         0xCE
#define CC_GET_SECTOR_INFO                0xCD
#define CC_PROGRAM_PREPARE                0xCC
#define CC_PROGRAM_FORMAT                 0xCB 
#define CC_PROGRAM_NEXT                   0xCA
#define CC_PROGRAM_MAX                    0xC9
#define CC_PROGRAM_VERIFY                 0xC8

/*-------------------------------------------------------------------------*/
/* XCP >= V1.1 Commands */
# define CC_WRITE_DAQ_MULTIPLE            0xC7 /* XCP V1.1 specific commands */

/*-------------------------------------------------------------------------*/
/* XCP >= V1.3 Commands */
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0130
# define CC_TIME_CORRELATION_PROPERTIES   0xC6 /* XCP V1.3 specific commands */
#endif

/*-------------------------------------------------------------------------*/
/* XCP >= V1.4 Commands */
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0140
#define CC_LEVEL_1_COMMAND                0xC0 /* XCP V1.4 Level 1 Commands: */
#define CC_GET_VERSION                    0x00  
#define CC_SET_DAQ_LIST_PACKED_MODE       0x01
#define CC_GET_DAQ_LIST_PACKED_MODE       0x02
#define CC_SW_DBG_OVER_XCP                0xFC  
#endif

/*-------------------------------------------------------------------------*/
/* Packet Identifiers Slave -> Master */
#define PID_RES                           0xFF   /* response packet        */
#define PID_ERR                           0xFE   /* error packet           */
#define PID_EV                            0xFD   /* event packet           */
#define PID_SERV                          0xFC   /* service request packet */


/*-------------------------------------------------------------------------*/
/* Command Return Codes */

#define CRC_CMD_SYNCH                           0x00
#define CRC_CMD_BUSY                            0x10
#define CRC_DAQ_ACTIVE                          0x11
#define CRC_PRM_ACTIVE                          0x12
#define CRC_CMD_UNKNOWN                         0x20
#define CRC_CMD_SYNTAX                          0x21
#define CRC_OUT_OF_RANGE                        0x22
#define CRC_WRITE_PROTECTED                     0x23
#define CRC_ACCESS_DENIED                       0x24
#define CRC_ACCESS_LOCKED                       0x25
#define CRC_PAGE_NOT_VALID                      0x26
#define CRC_PAGE_MODE_NOT_VALID                 0x27
#define CRC_SEGMENT_NOT_VALID                   0x28
#define CRC_SEQUENCE                            0x29
#define CRC_DAQ_CONFIG                          0x2A
#define CRC_MEMORY_OVERFLOW                     0x30
#define CRC_GENERIC                             0x31
#define CRC_VERIFY                              0x32
#define CRC_RESOURCE_TEMPORARY_NOT_ACCESSIBLE   0x33
#define CRC_SUBCMD_UNKNOWN                      0x34
#define CRC_TIMECORR_STATE_CHANGE               0x35


/*-------------------------------------------------------------------------*/
/* Event Codes */

#define EVC_RESUME_MODE        0x00
#define EVC_CLEAR_DAQ          0x01
#define EVC_STORE_DAQ          0x02
#define EVC_STORE_CAL          0x03
#define EVC_CMD_PENDING        0x05
#define EVC_DAQ_OVERLOAD       0x06
#define EVC_SESSION_TERMINATED 0x07
#define EVC_TIME_SYNC          0x08 
#define EVC_STIM_TIMEOUT       0x09 
#define EVC_SLEEP              0x0A 
#define EVC_WAKEUP             0x0B
#define EVC_ECU_STATE          0x0C
#define EVC_USER               0xFE
#define EVC_TRANSPORT          0xFF


/*-------------------------------------------------------------------------*/
/* Service Request Codes */

#define SERV_RESET                            0x00 /* Slave requesting to be reset */
#define SERV_TEXT                             0x01 /* Plain ASCII text null terminated */




/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/

/*-------------------------------------------------------------------------*/
/* ResourceMask (CONNECT) */

#define RM_CAL_PAG                  0x01
#define RM_DAQ                      0x04
#define RM_STIM                     0x08
#define RM_PGM                      0x10
#define RM_DBG                      0x20


/*-------------------------------------------------------------------------*/
/* CommModeBasic (CONNECT) */

#define PI_MOTOROLA                   0x01

#define CMB_BYTE_ORDER                (0x01u<<0)
#define CMB_ADDRESS_GRANULARITY       (0x03u<<1)
#define CMB_SLAVE_BLOCK_MODE          (0x01u<<6)
#define CMB_OPTIONAL                  (0x01u<<7)

#define CMB_ADDRESS_GRANULARITY_BYTE  (0<<1)
#define CMB_ADDRESS_GRANULARITY_WORD  (1<<1)
#define CMB_ADDRESS_GRANULARITY_DWORD (2<<1)
#define CMB_ADDRESS_GRANULARITY_QWORD (3<<1)


/*-------------------------------------------------------------------------*/
/* Protocol Info (GET_COMM_MODE_INFO - COMM_OPTIONAL) */

#define CMO_MASTER_BLOCK_MODE  0x01
#define CMO_INTERLEAVED_MODE   0x02   


/*-------------------------------------------------------------------------*/
/* Session Status (GET_STATUS and SET_REQUEST) */

#define SS_STORE_CAL_REQ       0x0001u 
#define SS_BLOCK_UPLOAD        0x0002u /* Internal */
#define SS_STORE_DAQ_REQ       0x0004u
#define SS_CLEAR_DAQ_REQ       0x0008u
#define SS_LEGACY_MODE         0x0010u /* Internal XCP 1.3 legacy mode */
#define SS_CONNECTED           0x0020u /* Internal */
#define SS_DAQ                 0x0040u
#define SS_RESUME              0x0080u


/*-------------------------------------------------------------------------*/
/* Identifier Type (GET_ID) */

#define IDT_ASCII              0
#define IDT_ASAM_NAME          1
#define IDT_ASAM_PATH          2
#define IDT_ASAM_URL           3
#define IDT_ASAM_UPLOAD        4
#define IDT_ASAM_EPK           5
#define IDT_ASAM_ECU           6
#define IDT_ASAM_SYSID         7

/*-------------------------------------------------------------------------*/
/* Checksum Types (BUILD_CHECKSUM) */

#define XCP_CHECKSUM_TYPE_ADD11      0x01  /* Add BYTE into a BYTE checksum, ignore overflows */
#define XCP_CHECKSUM_TYPE_ADD12      0x02  /* Add BYTE into a WORD checksum, ignore overflows */
#define XCP_CHECKSUM_TYPE_ADD14      0x03  /* Add BYTE into a DWORD checksum, ignore overflows */
#define XCP_CHECKSUM_TYPE_ADD22      0x04  /* Add WORD into a WORD checksum, ignore overflows, blocksize must be modulo 2 */
#define XCP_CHECKSUM_TYPE_ADD24      0x05  /* Add WORD into a DWORD checksum, ignore overflows, blocksize must be modulo 2 */ 
#define XCP_CHECKSUM_TYPE_ADD44      0x06  /* Add DWORD into DWORD, ignore overflows, blocksize must be modulo 4 */
#define XCP_CHECKSUM_TYPE_CRC16      0x07  /* See CRC error detection algorithms */
#define XCP_CHECKSUM_TYPE_CRC16CCITT 0x08  /* See CRC error detection algorithms */
#define XCP_CHECKSUM_TYPE_CRC32      0x09  /* See CRC error detection algorithms */
#define XCP_CHECKSUM_TYPE_DLL        0xFF  /* User defined, ASAM MCD 2MC DLL Interface */


/*-------------------------------------------------------------------------*/
/* Page Mode (SET_CAL_PAGE) */

#define CAL_ECU                0x01
#define CAL_XCP                0x02
#define CAL_ALL                0x80        /* not supported */


/*-------------------------------------------------------------------------*/
/* PAG_PROPERTIES (GET_PAG_PROCESSOR_INFO) */

#define PAG_PROPERTY_FREEZE               0x01


/*-------------------------------------------------------------------------*/
/* PAGE_PROPERTIES (GET_PAGE_INFO)*/

#define ECU_ACCESS_TYPE                   0x03
#define XCP_READ_ACCESS_TYPE              0x0C
#define XCP_WRITE_ACCESS_TYPE             0x30

/* ECU_ACCESS_TYPE */
#define ECU_ACCESS_NONE                   (0<<0)
#define ECU_ACCESS_WITHOUT                (1<<0)
#define ECU_ACCESS_WITH                   (2<<0)
#define ECU_ACCESS_DONT_CARE              (3<<0)

/* XCP_READ_ACCESS_TYPE */
#define XCP_READ_ACCESS_NONE              (0<<2)
#define XCP_READ_ACCESS_WITHOUT           (1<<2)
#define XCP_READ_ACCESS_WITH              (2<<2)
#define XCP_READ_ACCESS_DONT_CARE         (3<<2)

/* XCP_WRITE_ACCESS_TYPE */
#define XCP_WRITE_ACCESS_NONE             (0<<4)
#define XCP_WRITE_ACCESS_WITHOUT          (1<<4)
#define XCP_WRITE_ACCESS_WITH             (2<<4)
#define XCP_WRITE_ACCESS_DONT_CARE        (3<<4)


/*-------------------------------------------------------------------------*/
/* SEGMENT_MODE (GET_SEGMENT_MODE, SET_SEGMENT_MODE) */

#define SEGMENT_FLAG_FREEZE               0x01 /* */


/*-------------------------------------------------------------------------*/
/* DAQ_LIST_MODE (GET_DAQ_LIST_MODE, SET_DAQ_LIST_MODE) */

#define DAQ_FLAG_SELECTED                 0x01u /* */
#define DAQ_FLAG_DIRECTION                0x02u /* Data Stimulation Mode */

#define DAQ_FLAG_CMPL_DAQ_CH              0x04u  /* Complementary DAQ channel */

#define DAQ_FLAG_TIMESTAMP                0x10u /* Timestamps */
#define DAQ_FLAG_NO_PID                   0x20u /* No PID */
#define DAQ_FLAG_RUNNING                  0x40u /* Is started */
#define DAQ_FLAG_RESUME                   0x80u /* Resume Mode */

#define DAQ_FLAG_RESERVED                 0x08u 
#define DAQ_FLAG_OVERRUN                  0x08u /* Overun (Internal Use) */


/*-------------------------------------------------------------------------*/
/* GET_DAQ_PROCESSOR_INFO */

/* DAQ_PROPERTIES */
#define DAQ_PROPERTY_CONFIG_TYPE          0x01
#define DAQ_PROPERTY_PRESCALER            0x02
#define DAQ_PROPERTY_RESUME               0x04
#define DAQ_PROPERTY_BIT_STIM             0x08
#define DAQ_PROPERTY_TIMESTAMP            0x10
#define DAQ_PROPERTY_NO_PID               0x20
#define DAQ_PROPERTY_OVERLOAD_INDICATION  0xC0

/* DAQ Overload Indication Type */
#define DAQ_OVERLOAD_INDICATION_NONE      (0<<6)
#define DAQ_OVERLOAD_INDICATION_PID       (1<<6)
#define DAQ_OVERLOAD_INDICATION_EVENT     (2<<6)

/* DAQ_KEY_BYTE */
#define DAQ_OPT_TYPE                      0x0F
#define DAQ_EXT_TYPE                      0x30
#define DAQ_HDR_TYPE                      0xC0

/* DAQ Optimisation Type */
#define DAQ_OPT_DEFAULT                   (0<<0)
#define DAQ_OPT_ODT_16                    (1<<0)
#define DAQ_OPT_ODT_32                    (2<<0)
#define DAQ_OPT_ODT_64                    (3<<0)
#define DAQ_OPT_ALIGNMENT                 (4<<0)
#define DAQ_OPT_MAX_ENTRY_SIZE            (5<<0)

/* DAQ Address Extension Scope */
#define DAQ_EXT_FREE                      (0<<4)
#define DAQ_EXT_ODT                       (1<<4)
#define DAQ_EXT_DAQ                       (3<<4)

/* DAQ Identification Field Type */
#define DAQ_HDR_PID                       (0<<6)
#define DAQ_HDR_ODT_DAQB                  (1<<6)
#define DAQ_HDR_ODT_DAQW                  (2<<6)
#define DAQ_HDR_ODT_FIL_DAQW              (3<<6)
 

/*-------------------------------------------------------------------------*/
/* GET_DAQ_RESOLUTION_INFO */

/* TIMESTAMP_MODE */
#define DAQ_TIMESTAMP_SIZE  0x07
#define DAQ_TIMESTAMP_FIXED 0x08
#define DAQ_TIMESTAMP_UNIT  0xF0

/* DAQ Timestamp Size */
#define DAQ_TIMESTAMP_OFF         (0<<0)
#define DAQ_TIMESTAMP_BYTE        (1<<0)
#define DAQ_TIMESTAMP_WORD        (2<<0)
#define DAQ_TIMESTAMP_DWORD       (4<<0)

/* DAQ Timestamp Unit */
#define DAQ_TIMESTAMP_UNIT_1NS    (0<<4)
#define DAQ_TIMESTAMP_UNIT_10NS   (1<<4)
#define DAQ_TIMESTAMP_UNIT_100NS  (2<<4)
#define DAQ_TIMESTAMP_UNIT_1US    (3<<4)
#define DAQ_TIMESTAMP_UNIT_10US   (4<<4)  
#define DAQ_TIMESTAMP_UNIT_100US  (5<<4)
#define DAQ_TIMESTAMP_UNIT_1MS    (6<<4)
#define DAQ_TIMESTAMP_UNIT_10MS   (7<<4)
#define DAQ_TIMESTAMP_UNIT_100MS  (8<<4)
#define DAQ_TIMESTAMP_UNIT_1S     (9<<4)


/*-------------------------------------------------------------------------*/
/* DAQ_LIST_PROPERTIES (GET_DAQ_LIST_INFO) */

#define DAQ_LIST_PREDEFINED           0x01
#define DAQ_LIST_FIXED_EVENT          0x02
#define DAQ_LIST_DIR_DAQ              0x04
#define DAQ_LIST_DIR_STIM             0x08
#define DAQ_LIST_PACKED               0x10


/*-------------------------------------------------------------------------*/
/* EVENT_PROPERTY (GET_DAQ_EVENT_INFO) */

#define DAQ_EVENT_DIRECTION_DAQ      0x04
#define DAQ_EVENT_DIRECTION_STIM     0x08
#define DAQ_EVENT_DIRECTION_DAQ_STIM 0x0C


/*-------------------------------------------------------------------------*/
/* Comm mode programming parameter (PROGRAM_START) */

#define PI_PGM_BLOCK_DOWNLOAD      0x01
#define PI_PGM_BLOCK_UPLOAD        0x40   


/*-------------------------------------------------------------------------*/
/* PGM_PROPERTIES (GET_PGM_PROCESSOR_INFO) */

#define PGM_ACCESS_TYPE                   0x03
#define PGM_COMPRESSION_TYPE              0x0C
#define PGM_ENCRYPTION_TYPE               0x30
#define PGM_NON_SEQ_TYPE                  0xC0

/* PGM Access Mode */
#define PGM_ACCESS_ABSOLUTE               (1<<0)
#define PGM_ACCESS_FUNCTIONAL             (2<<0)
#define PGM_ACCESS_FREE                   (3<<0)

/* PGM Compression type */
#define PGM_COMPRESSION_NONE              (0<<2)
#define PGM_COMPRESSION_SUPPORTED         (1<<2)
#define PGM_COMPRESSION_REQUIRED          (3<<2)

/* PGM Encryption type */
#define PGM_ENCRYPTION_NONE               (0<<4)
#define PGM_ENCRYPTION_SUPPORTED          (1<<4)
#define PGM_ENCRYPTION_REQUIRED           (3<<4)

/* PGM non sequential programming type */
#define PGM_NON_SEQ_NONE                  (0<<6)
#define PGM_NON_SEQ_SUPPORTED             (1<<6)
#define PGM_NON_SEQ_REQUIRED              (3<<6)


/***************************************************************************/
/* XCP Protocol Commands and Responces, Type Definition */
/***************************************************************************/

/* Protocol command structure definition */
#define CRO_CMD                                         CRO_BYTE(0)
#define CRM_CMD                                         CRM_BYTE(0)
#define CRM_ERR                                         CRM_BYTE(1)
#define CRM_EVENTCODE                                   CRM_BYTE(1)



/* CONNECT */
#define CRO_CONNECT_LEN                                 2
#define CRO_CONNECT_MODE                                CRO_BYTE(1)
#define CRM_CONNECT_LEN                                 8
#define CRM_CONNECT_RESOURCE                            CRM_BYTE(1)
#define CRM_CONNECT_COMM_BASIC                          CRM_BYTE(2)
#define CRM_CONNECT_MAX_CTO_SIZE                        CRM_BYTE(3)
#define CRM_CONNECT_MAX_DTO_SIZE                        CRM_WORD(2)
#define CRM_CONNECT_PROTOCOL_VERSION                    CRM_BYTE(6)
#define CRM_CONNECT_TRANSPORT_VERSION                   CRM_BYTE(7)


/* DISCONNECT */                                     
#define CRO_DISCONNECT_LEN                              1
#define CRM_DISCONNECT_LEN                              1


/* GET_STATUS */                                        
#define CRO_GET_STATUS_LEN                              1
#define CRM_GET_STATUS_LEN                              6
#define CRM_GET_STATUS_STATUS                           CRM_BYTE(1)
#define CRM_GET_STATUS_PROTECTION                       CRM_BYTE(2)
#define CRM_GET_STATUS_CONFIG_ID                        CRM_WORD(2)

/* SYNCH */                                       
#define CRO_SYNCH_LEN                                   1
#define CRM_SYNCH_LEN                                   2
#define CRM_SYNCH_RESULT                                CRM_BYTE(1)
                                                 
                                                       
/* GET_COMM_MODE_INFO */
#define CRO_GET_COMM_MODE_INFO_LEN                      1
#define CRM_GET_COMM_MODE_INFO_LEN                      8
#define CRM_GET_COMM_MODE_INFO_COMM_OPTIONAL            CRM_BYTE(2)
#define CRM_GET_COMM_MODE_INFO_MAX_BS                   CRM_BYTE(4)
#define CRM_GET_COMM_MODE_INFO_MIN_ST                   CRM_BYTE(5)
#define CRM_GET_COMM_MODE_INFO_QUEUE_SIZE               CRM_BYTE(6)
#define CRM_GET_COMM_MODE_INFO_DRIVER_VERSION           CRM_BYTE(7)


/* GET_ID */                                            
#define CRO_GET_ID_LEN                                  2
#define CRO_GET_ID_TYPE                                 CRO_BYTE(1)
#define CRM_GET_ID_LEN                                  8
#define CRM_GET_ID_MODE                                 CRM_BYTE(1)
#define CRM_GET_ID_LENGTH                               CRM_DWORD(1)
#define CRM_GET_ID_DATA                                 (&CRM_BYTE(8))


/* SET_REQUEST */
#define CRO_SET_REQUEST_LEN                             4
#define CRO_SET_REQUEST_MODE                            CRO_BYTE(1)
#define CRO_SET_REQUEST_CONFIG_ID                       CRO_WORD(1)
#define CRM_SET_REQUEST_LEN                             1

                                                        
/* GET_SEED */                                          
#define CRO_GET_SEED_LEN                                3
#define CRO_GET_SEED_MODE                               CRO_BYTE(1)
#define CRO_GET_SEED_RESOURCE                           CRO_BYTE(2)
#define CRM_GET_SEED_LEN                                (CRM_GET_SEED_LENGTH+2u)
#define CRM_GET_SEED_LENGTH                             CRM_BYTE(1)
#define CRM_GET_SEED_DATA                               (&CRM_BYTE(2))

                                                        
/* UNLOCK */                                            
#define CRO_UNLOCK_LEN                                  8
#define CRO_UNLOCK_LENGTH                               CRO_BYTE(1)
#define CRO_UNLOCK_KEY                                  (&CRO_BYTE(2))
#define CRM_UNLOCK_LEN                                  2
#define CRM_UNLOCK_PROTECTION                           CRM_BYTE(1)

                                                        
/* SET_MTA */
#define CRO_SET_MTA_LEN                                 8
#define CRO_SET_MTA_EXT                                 CRO_BYTE(3)
#define CRO_SET_MTA_ADDR                                CRO_DWORD(1)
#define CRM_SET_MTA_LEN                                 1


/* UPLOAD */                                            
#define CRM_UPLOAD_MAX_SIZE                             ((vuint8)(XCPTL_CTO_SIZE-1))
#define CRO_UPLOAD_LEN                                  2
#define CRO_UPLOAD_SIZE                                 CRO_BYTE(1)
#define CRM_UPLOAD_LEN                                  1 /* +CRO_UPLOAD_SIZE */
#define CRM_UPLOAD_DATA                                 (&CRM_BYTE(1))


/* SHORT_UPLOAD */
#define CRO_SHORT_UPLOAD_LEN                            8
#define CRO_SHORT_UPLOAD_SIZE                           CRO_BYTE(1)
#define CRO_SHORT_UPLOAD_EXT                            CRO_BYTE(3)
#define CRO_SHORT_UPLOAD_ADDR                           CRO_DWORD(1)
#define CRM_SHORT_UPLOAD_MAX_SIZE                       ((vuint8)(XCPTL_CTO_SIZE-1))
#define CRM_SHORT_UPLOAD_LEN                            1u /* +CRO_SHORT_UPLOAD_SIZE */
#define CRM_SHORT_UPLOAD_DATA                           (&CRM_BYTE(1))


/* BUILD_CHECKSUM */
#define CRO_BUILD_CHECKSUM_LEN                          8
#define CRO_BUILD_CHECKSUM_SIZE                         CRO_DWORD(1)
#define CRM_BUILD_CHECKSUM_LEN                          8
#define CRM_BUILD_CHECKSUM_TYPE                         CRM_BYTE(1)
#define CRM_BUILD_CHECKSUM_RESULT                       CRM_DWORD(1)

       
/* DOWNLOAD */                                           
#define CRO_DOWNLOAD_MAX_SIZE                           ((vuint8)(XCPTL_CTO_SIZE-2))
#define CRO_DOWNLOAD_LEN                                2 /* + CRO_DOWNLOAD_SIZE */
#define CRO_DOWNLOAD_SIZE                               CRO_BYTE(1)
#define CRO_DOWNLOAD_DATA                               (&CRO_BYTE(2))
#define CRM_DOWNLOAD_LEN                                1


/* DOWNLOAD_NEXT */                                          
#define CRO_DOWNLOAD_NEXT_MAX_SIZE                      ((vuint8)(XCPTL_CTO_SIZE-2))
#define CRO_DOWNLOAD_NEXT_LEN                           2 /* + size */
#define CRO_DOWNLOAD_NEXT_SIZE                          CRO_BYTE(1)
#define CRO_DOWNLOAD_NEXT_DATA                          (&CRO_BYTE(2))
#define CRM_DOWNLOAD_NEXT_LEN                           1

                                                        
/* DOWNLOAD_MAX */
#define CRO_DOWNLOAD_MAX_MAX_SIZE                       ((vuint8)(XCPTL_CTO_SIZE-1))
#define CRO_DOWNLOAD_MAX_DATA                           (&CRO_BYTE(1))
#define CRM_DOWNLOAD_MAX_LEN                            1


/* SHORT_DOWNLOAD */
#define CRO_SHORT_DOWNLOAD_MAX_SIZE                     ((vuint8)(XCPTL_CTO_SIZE-8))
#define CRO_SHORT_DOWNLOAD_LEN                          8
#define CRO_SHORT_DOWNLOAD_SIZE                         CRO_BYTE(1)
#define CRO_SHORT_DOWNLOAD_EXT                          CRO_BYTE(3)
#define CRO_SHORT_DOWNLOAD_ADDR                         CRO_DWORD(1)
#define CRO_SHORT_DOWNLOAD_DATA                         (&CRO_BYTE(8))
#define CRM_SHORT_DOWNLOAD_LEN                          1 /* +CRO_SHORT_UPLOAD_SIZE */

                                                        
/* MODIFY_BITS */
#define CRO_MODIFY_BITS_LEN                             6
#define CRO_MODIFY_BITS_SHIFT                           CRO_BYTE(1)
#define CRO_MODIFY_BITS_AND                             CRO_WORD(1)
#define CRO_MODIFY_BITS_XOR                             CRO_WORD(2)
#define CRM_MODIFY_BITS_LEN                             1

                                                        
/* SET_CAL_PAGE */                                    
#define CRO_SET_CAL_PAGE_LEN                            4
#define CRO_SET_CAL_PAGE_MODE                           CRO_BYTE(1)
#define CRO_SET_CAL_PAGE_SEGMENT                        CRO_BYTE(2)
#define CRO_SET_CAL_PAGE_PAGE                           CRO_BYTE(3)
#define CRM_SET_CAL_PAGE_LEN                            1

                                                        
/* GET_CAL_PAGE */                                      
#define CRO_GET_CAL_PAGE_LEN                            3
#define CRO_GET_CAL_PAGE_MODE                           CRO_BYTE(1)
#define CRO_GET_CAL_PAGE_SEGMENT                        CRO_BYTE(2)
#define CRM_GET_CAL_PAGE_LEN                            4
#define CRM_GET_CAL_PAGE_PAGE                           CRM_BYTE(3)


/* GET_PAG_PROCESSOR_INFO */
#define CRO_GET_PAG_PROCESSOR_INFO_LEN                  1
#define CRM_GET_PAG_PROCESSOR_INFO_LEN                  3
#define CRM_GET_PAG_PROCESSOR_INFO_MAX_SEGMENT          CRM_BYTE(1)
#define CRM_GET_PAG_PROCESSOR_INFO_PROPERTIES           CRM_BYTE(2)


/* GET_SEGMENT_INFO */
#define CRO_GET_SEGMENT_INFO_LEN                        5
#define CRO_GET_SEGMENT_INFO_MODE                       CRO_BYTE(1)
#define CRO_GET_SEGMENT_INFO_NUMBER                     CRO_BYTE(2)
#define CRO_GET_SEGMENT_INFO_MAPPING_INDEX              CRO_BYTE(3)
#define CRO_GET_SEGMENT_INFO_MAPPING                    CRO_BYTE(4)
#define CRM_GET_SEGMENT_INFO_LEN                        8
#define CRM_GET_SEGMENT_INFO_MAX_PAGES                  CRM_BYTE(1)
#define CRM_GET_SEGMENT_INFO_ADDRESS_EXTENSION          CRM_BYTE(2)
#define CRM_GET_SEGMENT_INFO_MAX_MAPPING                CRM_BYTE(3)
#define CRM_GET_SEGMENT_INFO_COMPRESSION                CRM_BYTE(4)
#define CRM_GET_SEGMENT_INFO_ENCRYPTION                 CRM_BYTE(5)
#define CRM_GET_SEGMENT_INFO_MAPPING_INFO               CRM_DWORD(1)


/* GET_PAGE_INFO */
#define CRO_GET_PAGE_INFO_LEN                           4
#define CRO_GET_PAGE_INFO_SEGMENT_NUMBER                CRO_BYTE(2)
#define CRO_GET_PAGE_INFO_PAGE_NUMBER                   CRO_BYTE(3)
#define CRM_GET_PAGE_INFO_LEN                           3
#define CRM_GET_PAGE_INFO_PROPERTIES                    CRM_BYTE(1)
#define CRM_GET_PAGE_INFO_INIT_SEGMENT                  CRM_BYTE(2)


/* SET_SEGMENT_MODE */
#define CRO_SET_SEGMENT_MODE_LEN                        3
#define CRO_SET_SEGMENT_MODE_MODE                       CRO_BYTE(1)
#define CRO_SET_SEGMENT_MODE_SEGMENT                    CRO_BYTE(2)
#define CRM_SET_SEGMENT_MODE_LEN                        1


/* GET_SEGMENT_MODE */
#define CRO_GET_SEGMENT_MODE_LEN                        3
#define CRO_GET_SEGMENT_MODE_SEGMENT                    CRO_BYTE(2)
#define CRM_GET_SEGMENT_MODE_LEN                        3
#define CRM_GET_SEGMENT_MODE_MODE                       CRM_BYTE(2)


/* COPY_CAL_PAGE */
#define CRO_COPY_CAL_PAGE_LEN                           5
#define CRO_COPY_CAL_PAGE_SRC_SEGMENT                   CRO_BYTE(1)
#define CRO_COPY_CAL_PAGE_SRC_PAGE                      CRO_BYTE(2)
#define CRO_COPY_CAL_PAGE_DEST_SEGMENT                  CRO_BYTE(3)
#define CRO_COPY_CAL_PAGE_DEST_PAGE                     CRO_BYTE(4)
#define CRM_COPY_CAL_PAGE_LEN                           1


/* CLEAR_DAQ_LIST */
#define CRO_CLEAR_DAQ_LIST_LEN                          4
#define CRO_CLEAR_DAQ_LIST_DAQ                          CRO_WORD(1)
#define CRM_CLEAR_DAQ_LIST_LEN                          1

                                                        
/* SET_DAQ_PTR */
#define CRO_SET_DAQ_PTR_LEN                             6
#define CRO_SET_DAQ_PTR_DAQ                             CRO_WORD(1)
#define CRO_SET_DAQ_PTR_ODT                             CRO_BYTE(4)
#define CRO_SET_DAQ_PTR_IDX                             CRO_BYTE(5)
#define CRM_SET_DAQ_PTR_LEN                             1


/* WRITE_DAQ */
#define CRO_WRITE_DAQ_LEN                               8
#define CRO_WRITE_DAQ_BITOFFSET                         CRO_BYTE(1)
#define CRO_WRITE_DAQ_SIZE                              CRO_BYTE(2)
#define CRO_WRITE_DAQ_EXT                               CRO_BYTE(3)
#define CRO_WRITE_DAQ_ADDR                              CRO_DWORD(1)
#define CRM_WRITE_DAQ_LEN                               1


/* WRITE_DAQ_MULTIPLE */
#define CRO_WRITE_DAQ_MULTIPLE_LEN                      8
#define CRO_WRITE_DAQ_MULTIPLE_NODAQ                    CRO_BYTE(1)
#define CRO_WRITE_DAQ_MULTIPLE_BITOFFSET(i)             CRO_BYTE(2 + (8*(i)))
#define CRO_WRITE_DAQ_MULTIPLE_SIZE(i)                  CRO_BYTE(3 + (8*(i)))
#define CRO_WRITE_DAQ_MULTIPLE_ADDR(i)                  CRO_DWORD(1 + (2*(i)))
#define CRO_WRITE_DAQ_MULTIPLE_EXT(i)                   CRO_BYTE(8 + (8*(i)))
#define CRM_WRITE_DAQ_MULTIPLE_LEN                      1


/* SET_DAQ_LIST_MODE */
#define CRO_SET_DAQ_LIST_MODE_LEN                       8
#define CRO_SET_DAQ_LIST_MODE_MODE                      CRO_BYTE(1)
#define CRO_SET_DAQ_LIST_MODE_DAQ                       CRO_WORD(1)
#define CRO_SET_DAQ_LIST_MODE_EVENTCHANNEL              CRO_WORD(2)
#define CRO_SET_DAQ_LIST_MODE_PRESCALER                 CRO_BYTE(6)
#define CRO_SET_DAQ_LIST_MODE_PRIORITY                  CRO_BYTE(7)
#define CRM_SET_DAQ_LIST_MODE_LEN                       6


/* GET_DAQ_LIST_MODE */
#define CRO_GET_DAQ_LIST_MODE_LEN                       4
#define CRO_GET_DAQ_LIST_MODE_DAQ                       CRO_WORD(1)
#define CRM_GET_DAQ_LIST_MODE_LEN                       8
#define CRM_GET_DAQ_LIST_MODE_MODE                      CRM_BYTE(1)
#define CRM_GET_DAQ_LIST_MODE_EVENTCHANNEL              CRM_WORD(2)
#define CRM_GET_DAQ_LIST_MODE_PRESCALER                 CRM_BYTE(6)
#define CRM_GET_DAQ_LIST_MODE_PRIORITY                  CRM_BYTE(7)


/* START_STOP_DAQ_LIST */                                     
#define CRO_START_STOP_LEN                              4
#define CRO_START_STOP_MODE                             CRO_BYTE(1)
#define CRO_START_STOP_DAQ                              CRO_WORD(1)
#define CRM_START_STOP_LEN                              2
#define CRM_START_STOP_FIRST_PID                        CRM_BYTE(1)


/* START_STOP_SYNCH */  
#define CRO_START_STOP_SYNC_LEN                         2
#define CRO_START_STOP_SYNC_MODE                        CRO_BYTE(1)
#define CRM_START_STOP_SYNC_LEN                         1


/* GET_DAQ_CLOCK */
#define CRO_GET_DAQ_CLOCK_LEN                           1
#define CRM_GET_DAQ_CLOCK_LEN                           8
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0130
#define CRM_GET_DAQ_CLOCK_RES1                          CRM_BYTE(1)
#define CRM_GET_DAQ_CLOCK_TRIGGER_INFO                  CRM_BYTE(2)
#define CRM_GET_DAQ_CLOCK_PAYLOAD_FMT                   CRM_BYTE(3)
#define CRM_GET_DAQ_CLOCK_TIME                          CRM_DWORD(1)
#define CRM_GET_DAQ_CLOCK_TIME64                        CRM_DDWORD(1) // Byte number is 4
#define CRM_GET_DAQ_CLOCK_SYNC_STATE                    CRM_BYTE(8)
#define CRM_GET_DAQ_CLOCK_SYNC_STATE64                  CRM_BYTE(12)
#else
#define CRM_GET_DAQ_CLOCK_TIME                          CRM_DWORD(1)
#endif

/* READ_DAQ */
#define CRO_READ_DAQ_LEN                                1
#define CRM_READ_DAQ_LEN                                8
#define CRM_READ_DAQ_BITOFFSET                          CRM_BYTE(1)
#define CRM_READ_DAQ_SIZE                               CRM_BYTE(2)
#define CRM_READ_DAQ_EXT                                CRM_BYTE(3)
#define CRM_READ_DAQ_ADDR                               CRM_DWORD(1)


/* GET_DAQ_PROCESSOR_INFO */
#define CRO_GET_DAQ_PROCESSOR_INFO_LEN                  1
#define CRM_GET_DAQ_PROCESSOR_INFO_LEN                  8
#define CRM_GET_DAQ_PROCESSOR_INFO_PROPERTIES           CRM_BYTE(1)
#define CRM_GET_DAQ_PROCESSOR_INFO_MAX_DAQ              CRM_WORD(1)
#define CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT            CRM_WORD(2)
#define CRM_GET_DAQ_PROCESSOR_INFO_MIN_DAQ              CRM_BYTE(6)
#define CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE         CRM_BYTE(7)


/* GET_DAQ_RESOLUTION_INFO */                               
#define CRO_GET_DAQ_RESOLUTION_INFO_LEN                 1
#define CRM_GET_DAQ_RESOLUTION_INFO_LEN                 8
#define CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_DAQ     CRM_BYTE(1)
#define CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_DAQ        CRM_BYTE(2)
#define CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_STIM    CRM_BYTE(3)
#define CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_STIM       CRM_BYTE(4)
#define CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE      CRM_BYTE(5)
#define CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS     CRM_WORD(3)
#define CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS_WRITE(ticks) CRM_WORD_WRITE(3, ticks)

                                                        
/* GET_DAQ_LIST_INFO */
#define CRO_GET_DAQ_LIST_INFO_LEN                       4
#define CRO_GET_DAQ_LIST_INFO_DAQ                       CRO_WORD(1)
#define CRM_GET_DAQ_LIST_INFO_LEN                       6
#define CRM_GET_DAQ_LIST_INFO_PROPERTIES                CRM_BYTE(1)
#define CRM_GET_DAQ_LIST_INFO_MAX_ODT                   CRM_BYTE(2)
#define CRM_GET_DAQ_LIST_INFO_MAX_ODT_ENTRY             CRM_BYTE(3)
#define CRM_GET_DAQ_LIST_INFO_FIXED_EVENT               CRM_WORD(2)


/* GET_DAQ_EVENT_INFO */
#define CRO_GET_DAQ_EVENT_INFO_LEN                      4
#define CRO_GET_DAQ_EVENT_INFO_EVENT                    CRO_WORD(1)
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0160
#define CRM_GET_DAQ_EVENT_INFO_LEN                      12
#else
#define CRM_GET_DAQ_EVENT_INFO_LEN                      7
#endif
#define CRM_GET_DAQ_EVENT_INFO_PROPERTIES               CRM_BYTE(1)
#define CRM_GET_DAQ_EVENT_INFO_MAX_DAQ_LIST             CRM_BYTE(2)
#define CRM_GET_DAQ_EVENT_INFO_NAME_LENGTH              CRM_BYTE(3)
#define CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE               CRM_BYTE(4)
#define CRM_GET_DAQ_EVENT_INFO_TIME_UNIT                CRM_BYTE(5)
#define CRM_GET_DAQ_EVENT_INFO_PRIORITY                 CRM_BYTE(6)
#define CRM_GET_DAQ_EVENT_INFO_SIZE                     CRM_WORD(4)  // @@@@ XCP V1.6 ext event full data size
#define CRM_GET_DAQ_EVENT_INFO_SAMPLECOUNT              CRM_WORD(5)  // @@@@ XCP V1.6 packed sample count  

#define DAQ_EVENT_PROPERTIES_DAQ      0x04
#define DAQ_EVENT_PROPERTIES_STIM     0x08
#define DAQ_EVENT_PROPERTIES_PACKED   0x10
#define DAQ_EVENT_PROPERTIES_EXT      0x20 // @@@@ XCP V1.6
#define DAQ_EVENT_PROPERTIES_EVENT_CONSISTENCY  0x80


/* FREE_DAQ */
#define CRO_FREE_DAQ_LEN                                1
#define CRM_FREE_DAQ_LEN                                1


/* ALLOC_DAQ */
#define CRO_ALLOC_DAQ_LEN                               4
#define CRO_ALLOC_DAQ_COUNT                             CRO_WORD(1)
#define CRM_ALLOC_DAQ_LEN                               1


/* ALLOC_ODT */
#define _CRO_ALLOC_ODT_LEN                              3
#define _CRO_ALLOC_ODT_DAQ                              CRO_WORD(1)
#define _CRO_ALLOC_ODT_COUNT                            CRO_BYTE(1)
#define CRO_ALLOC_ODT_LEN                               5
#define CRO_ALLOC_ODT_DAQ                               CRO_WORD(1)
#define CRO_ALLOC_ODT_COUNT                             CRO_BYTE(4)
#define CRM_ALLOC_ODT_LEN                               1


/* ALLOC_ODT_ENTRY */
#define CRO_ALLOC_ODT_ENTRY_LEN                         6
#define CRO_ALLOC_ODT_ENTRY_DAQ                         CRO_WORD(1)
#define CRO_ALLOC_ODT_ENTRY_ODT                         CRO_BYTE(4)
#define CRO_ALLOC_ODT_ENTRY_COUNT                       CRO_BYTE(5)
#define CRM_ALLOC_ODT_ENTRY_LEN                         1


/* PROGRAM_START */                                    
#define CRO_PROGRAM_START_LEN                           1
#define CRM_PROGRAM_START_LEN                           7
#define CRM_PROGRAM_COMM_MODE_PGM                       CRM_BYTE(2) 
#define CRM_PROGRAM_MAX_CTO_PGM                         CRM_BYTE(3) 
#define CRM_PROGRAM_MAX_BS_PGM                          CRM_BYTE(4) 
#define CRM_PROGRAM_MIN_ST_PGM                          CRM_BYTE(5) 
#define CRM_PROGRAM_QUEUE_SIZE_PGM                      CRM_BYTE(6) 

                                                        
/* PROGRAM_CLEAR */                                    
#define CRO_PROGRAM_CLEAR_LEN                           8
#define CRO_PROGRAM_CLEAR_MODE                          CRO_BYTE(1)
#define CRO_PROGRAM_CLEAR_SIZE                          CRO_DWORD(1)
#define CRM_PROGRAM_CLEAR_LEN                           1

                                                        
/* PROGRAM */
#define CRO_PROGRAM_MAX_SIZE                            ((vuint8)(XCPTL_CTO_SIZE-2))
#define CRO_PROGRAM_LEN                                 2 /* + CRO_PROGRAM_SIZE */ 
#define CRO_PROGRAM_SIZE                                CRO_BYTE(1)
#define CRO_PROGRAM_DATA                                (&CRO_BYTE(2))
#define CRM_PROGRAM_LEN                                 1


/* PROGRAM RESET */
#define CRO_PROGRAM_RESET_LEN                           1
#define CRM_PROGRAM_RESET_LEN                           1

                                                        
/* GET_PGM_PROCESSOR_INFO*/
#define CRO_GET_PGM_PROCESSOR_INFO_LEN                  1
#define CRM_GET_PGM_PROCESSOR_INFO_LEN                  3
#define CRM_GET_PGM_PROCESSOR_INFO_PROPERTIES           CRM_BYTE(1)
#define CRM_GET_PGM_PROCESSOR_INFO_MAX_SECTOR           CRM_BYTE(2)

 
/* GET_SECTOR_INFO */
#define CRO_PROGRAM_GET_SECTOR_INFO_LEN                 3
#define CRO_PROGRAM_GET_SECTOR_INFO_MODE                CRO_BYTE(1)
#define CRO_PROGRAM_GET_SECTOR_INFO_NUMBER              CRO_BYTE(2)
#define CRM_PROGRAM_GET_SECTOR_INFO_LEN                 8
#define CRM_PROGRAM_GET_SECTOR_CLEAR_SEQ_NUM            CRM_BYTE(1)
#define CRM_PROGRAM_GET_SECTOR_PGM_SEQ_NUM              CRM_BYTE(2)
#define CRM_PROGRAM_GET_SECTOR_PGM_METHOD               CRM_BYTE(3)
#define CRM_PROGRAM_GET_SECTOR_SECTOR_INFO              CRM_DWORD(1)
#define CRM_PROGRAM_GET_SECTOR_SECTOR_INFO_WRITE(info)  CRM_DWORD_WRITE(1, info)


/* PROGRAM_PREPARE */                                   
#define CRO_PROGRAM_PREPARE_LEN                         4
#define CRO_PROGRAM_PREPARE_SIZE                        CRO_WORD(1)
#define CRM_PROGRAM_PREPARE_LEN                         1


/* PROGRAM_FORMAT */
#define CRO_PROGRAM_FORMAT_LEN                          5
#define CRO_PROGRAM_FORMAT_COMPRESSION_METHOD           CRO_BYTE(1)
#define CRO_PROGRAM_FORMAT_ENCRYPTION_METHOD            CRO_BYTE(2)
#define CRO_PROGRAM_FORMAT_PROGRAMMING_METHOD           CRO_BYTE(3)
#define CRO_PROGRAM_FORMAT_ACCESS_METHOD                CRO_BYTE(4)
#define CRM_PROGRAM_FORMAT_LEN                          1


/* PROGRAM_NEXT */
#define CRO_PROGRAM_NEXT_MAX_SIZE                       ((vuint8)(XCPTL_CTO_SIZE-2))
#define CRO_PROGRAM_NEXT_LEN                            2 /* + size */
#define CRO_PROGRAM_NEXT_SIZE                           CRO_BYTE(1)
#define CRO_PROGRAM_NEXT_DATA                           (&CRO_BYTE(2))
#define CRM_PROGRAM_NEXT_LEN                            3
#define CRM_PROGRAM_NEXT_ERR_SEQUENCE                   CRM_BYTE(1)
#define CRM_PROGRAM_NEXT_SIZE_EXPECTED_DATA             CRM_BYTE(2)


/* PROGRAM_MAX */
#define CRO_PROGRAM_MAX_MAX_SIZE                        ((vuint8)(XCPTL_CTO_SIZE-1))
#define CRO_PROGRAM_MAX_DATA                            (&CRO_BYTE(1))
#define CRM_PROGRAM_MAX_LEN                             1


/* PROGRAM_VERIFY */
#define CRO_PROGRAM_VERIFY_LEN                          8
#define CRO_PROGRAM_VERIFY_MODE                         CRO_BYTE(1)
#define CRO_PROGRAM_VERIFY_TYPE                         CRO_WORD(1)
#define CRO_PROGRAM_VERIFY_VALUE                        CRO_DWORD(1)
#define CRM_PROGRAM_VERIFY_LEN                          1


/* GET_SLAVE_ID */
#define CRO_GET_SLAVE_ID_LEN                            6
#define CRO_GET_SLAVE_ID_SUB_CODE                       CRO_BYTE(1)
#define CRO_GET_SLAVE_ID_X                              CRO_BYTE(2)
#define CRO_GET_SLAVE_ID_C                              CRO_BYTE(3)
#define CRO_GET_SLAVE_ID_P                              CRO_BYTE(4)
#define CRO_GET_SLAVE_ID_MODE                           CRO_BYTE(5)
#define CRM_GET_SLAVE_ID_LEN                            8
#define CRM_GET_SLAVE_ID_X                              CRM_BYTE(1)
#define CRM_GET_SLAVE_ID_C                              CRM_BYTE(2)
#define CRM_GET_SLAVE_ID_P                              CRM_BYTE(3)
#define CRM_GET_SLAVE_ID_CAN_ID_CMD_STIM                CRM_DWORD(1)

                                                        
/* GET_DAQ_ID */                                  
#define CRO_GET_DAQ_ID_LEN                              3
#define CRO_GET_DAQ_ID_SUB_CODE                         CRO_BYTE(1)
#define CRO_GET_DAQ_ID_DAQ                              CRO_WORD(1)
#define CRM_GET_DAQ_ID_LEN                              8
#define CRM_GET_DAQ_ID_FIXED                            CRM_BYTE(1)
#define CRM_GET_DAQ_ID_ID                               CRM_DWORD(1)


/* SET_DAQ_ID */                                 
#define CRO_SET_DAQ_ID_LEN                              8
#define CRO_SET_DAQ_ID_SUB_CODE                         CRO_BYTE(1)
#define CRO_SET_DAQ_ID_DAQ                              CRO_WORD(1)
#define CRO_SET_DAQ_ID_ID                               CRO_DWORD(1)
#define CRM_SET_DAQ_ID_LEN                              1


/* SET_SLAVE_PORT */
#define CRO_SET_SLAVE_PORT_LEN                          4
#define CRO_SET_SLAVE_PORT_SUB_CODE                     CRO_BYTE(1)
#define CRO_SET_SLAVE_PORT_PORT                         CRO_WORD(1)
#define CRM_SET_SLAVE_PORT                              1  


/* Level 1 commands */
#define CRO_LEVEL_1_COMMAND_LEN                             2
#define CRO_LEVEL_1_COMMAND_CODE                            CRO_BYTE(1)


/* GET_VERSION */
#define CRO_GET_VERSION_LEN                                 2
#define CRM_GET_VERSION_LEN                                 6
#define CRM_GET_VERSION_RESERVED                            CRM_BYTE(1)
#define CRM_GET_VERSION_PROTOCOL_VERSION_MAJOR              CRM_BYTE(2)
#define CRM_GET_VERSION_PROTOCOL_VERSION_MINOR              CRM_BYTE(3)
#define CRM_GET_VERSION_TRANSPORT_VERSION_MAJOR             CRM_BYTE(4)
#define CRM_GET_VERSION_TRANSPORT_VERSION_MINOR             CRM_BYTE(5)


/* GET_DAQ_LIST_PACKED_MODE */
#define CRO_GET_DAQ_LIST_PACKED_MODE_DAQ                    CRM_WORD(1)
#define CRM_GET_DAQ_LIST_PACKED_MODE_LEN                    8
#define CRM_GET_DAQ_LIST_PACKED_MODE_MODE                   CRM_BYTE(2)


/* SET_DAQ_LIST_PACKED_MODE */
#define CRO_SET_DAQ_LIST_PACKED_MODE_DAQ                    CRO_WORD(1)
#define CRO_SET_DAQ_LIST_PACKED_MODE_MODE                   CRO_BYTE(4)
#define CRO_SET_DAQ_LIST_PACKED_MODE_TIMEMODE               CRO_BYTE(5)
#define CRO_SET_DAQ_LIST_PACKED_MODE_SAMPLECOUNT            CRO_WORD(3)


/* TIME SYNCHRONIZATION PROPERTIES*/
#define CRO_TIME_SYNC_PROPERTIES_LEN                        6
#define CRO_TIME_SYNC_PROPERTIES_SET_PROPERTIES             CRO_BYTE(1)
#define CRO_TIME_SYNC_PROPERTIES_GET_PROPERTIES_REQUEST     CRO_BYTE(2)
#define CRO_TIME_SYNC_PROPERTIES_CLUSTER_ID                 CRO_WORD(2)

/* CRO_TIME_SYNC_PROPERTIES_SET_PROPERTIES: */
#define TIME_SYNC_SET_PROPERTIES_RESPONSE_FMT       (3 << 0)
#define TIME_SYNC_SET_PROPERTIES_TIME_SYNC_BRIDGE   (3 << 2)
#define TIME_SYNC_SET_PROPERTIES_CLUSTER_ID         (1 << 4)

/* CRO_TIME_SYNC_PROPERTIES_GET_PROPERTIES_REQUEST: */
#define TIME_SYNC_GET_PROPERTIES_GET_CLK_INFO       (1 << 0)

#define CRM_TIME_SYNC_PROPERTIES_LEN                        8
#define CRM_TIME_SYNC_PROPERTIES_SLAVE_CONFIG               CRM_BYTE(1)
#define CRM_TIME_SYNC_PROPERTIES_OBSERVABLE_CLOCKS          CRM_BYTE(2)
#define CRM_TIME_SYNC_PROPERTIES_SYNC_STATE                 CRM_BYTE(3)
#define CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO                 CRM_BYTE(4)
#define CRM_TIME_SYNC_PROPERTIES_RESERVED                   CRM_BYTE(5)
#define CRM_TIME_SYNC_PROPERTIES_CLUSTER_ID                 CRM_WORD(3)

/* CRM_TIME_SYNC_PROPERTIES_SLAVE_CONFIG: */
#define SLAVE_CONFIG_RESPONSE_FMT_LEGACY                    (0)
#define SLAVE_CONFIG_RESPONSE_FMT_ADVANCED                  (2)
#define SLAVE_CONFIG_DAQ_TS_ECU                             (1 << 2)
#define SLAVE_CONFIG_DAQ_TS_SLAVE                           (0 << 2)
#define SLAVE_CONFIG_TIME_SYNC_BRIDGE_NONE                  (0 << 3)

/* CRM_TIME_SYNC_PROPERTIES_OBSERVABLE_CLOCKS: */
#define SLAVE_CLOCK_FREE_RUNNING     (0<<0)
#define SLAVE_CLOCK_SYNCED          (1<<0)
#define SLAVE_CLOCK_NONE            (2<<0)
#define SLAVE_GRANDM_CLOCK_NONE     (0<<2)
#define SLAVE_GRANDM_CLOCK_READABLE (1<<2)
#define SLAVE_GRANDM_CLOCK_EVENT    (2<<2)
#define ECU_CLOCK_NONE        (0<<4)
#define ECU_CLOCK_READABLE    (1<<4)
#define ECU_CLOCK_EVENT       (2<<4)
#define ECU_CLOCK_NOTREADABLE (2<<4)

/* CRM_TIME_SYNC_PROPERTIES_SYNC_STATE: */
#define SLAVE_CLOCK_STATE_FREE_RUNNING  (7<<0)
#define GRANDM_CLOCK_STATE_SYNC        (1<<3)  
#define ECU_CLOCK_STATE_SYNC           (1<<4)

/* CRM_TIME_SYNC_PROPERTIES_CLOCK_INFO: */
#define CLOCK_INFO_SLAVE            (1<<0)
#define CLOCK_INFO_SLAVE_GRANDM     (1<<1)
#define CLOCK_INFO_RELATION         (1<<2)
#define CLOCK_INFO_ECU              (1<<3)
#define CLOCK_INFO_ECU_GRANDM       (1<<4)



#define SLAVE_CLOCK_FREE_RUNNING     (0<<0)
#define OBSERVABLE_CLOCKS_GRANDM    (3<<2)
#define OBSERVABLE_CLOCKS_ECU       (3<<4)



#define TIME_SYNC_GET_PROPERTIES_SLV_CLK_INFO               (1 << 0)
#define TIME_SYNC_GET_PROPERTIES_GRANDM_CLK_INFO            (1 << 1)
#define TIME_SYNC_GET_PROPERTIES_CLK_RELATION               (1 << 2)
#define TIME_SYNC_GET_PROPERTIES_ECU_CLK_INFO               (1 << 3)
#define TIME_SYNC_GET_PROPERTIES_ECU_GRANDM_CLK_INFO        (1 << 4)

#define EV_TIME_SYNC_RESPONSE_FMT_LEGACY                    0UL
#define EV_TIME_SYNC_RESPONSE_FMT_TRIGGER_SUBSET            1UL
#define EV_TIME_SYNC_RESPONSE_FMT_TRIGGER_ALL               2UL

#define DAQ_TSTAMP_RELATION_SLAVE_CLOCK                     0UL
#define DAQ_TSTAMP_RELATION_ECU_CLOCK                       1UL

#define TIME_SYNC_BRIDGE_NOT_AVAILABLE                      0UL
#define TIME_SYNC_BRIDGE_DISABLED                           1UL
#define TIME_SYNC_BRIDGE_ENABLED                            2UL

#define OBSERVABLE_CLOCK_XCP_SLV_CLK_FREE_RUNNING           0UL
#define OBSERVABLE_CLOCK_XCP_SLV_CLK_SYNCED                 1UL
#define OBSERVABLE_CLOCK_XCP_SLV_NOT_AVAILABLE              2UL

#define OBSERVABLE_CLOCK_DEDICATED_GRANDM_CLK_NOT_AVAILABLE 0UL
#define OBSERVABLE_CLOCK_DEDICATED_GRANDM_CLK_RANDOM_ACCESS 1UL
#define OBSERVABLE_CLOCK_DEDICATED_GRANDM_CLK_SELF_TRIGGER  2UL

#define OBSERVABLE_CLOCK_ECU_CLK_NOT_AVAILABLE              0UL
#define OBSERVABLE_CLOCK_ECU_CLK_RANDOM_ACCESS              1UL
#define OBSERVABLE_CLOCK_ECU_CLK_SELF_TRIGGER               2UL
#define OBSERVABLE_CLOCK_ECU_CLK_NOT_READABLE               3UL

#define SLV_CLK_SYNC_STATE_SYNCHRONIZATION_IN_PROGRESS      0UL
#define SLV_CLK_SYNC_STATE_SYNCHRONIZED                     1UL
#define SLV_CLK_SYNC_STATE_SYNTONIZATION_IN_PROGRESS        2UL
#define SLV_CLK_SYNC_STATE_SYNTONIZED                       3UL
#define SLV_CLK_SYNC_STATE_SYNTONIZATION_NOT_AVAILABLE      7UL
#define DEDICATED_GRANDM_CLK_SYNCHRONIZATION_IN_PROGRESS    0UL
#define DEDICATED_GRANDM_CLK_SYNCHRONIZED                   1UL
#define ECU_CLK_NOT_SYNCHRONIZED_TO_GRANDMASTER             0UL
#define ECU_CLK_SYNCHRONIZED_TO_GRANDMASTER                 1UL
#define ECU_CLK_SYNCHRONIZATION_STATE_UNKNOWN               2UL

#define SYNC_STATE_NO_SYNC_SUPPORT                          ((SLV_CLK_SYNC_STATE_SYNTONIZATION_NOT_AVAILABLE << 0) | (DEDICATED_GRANDM_CLK_SYNCHRONIZATION_IN_PROGRESS << 3) | (ECU_CLK_SYNCHRONIZATION_STATE_UNKNOWN << 4))

#define TRIG_INITIATOR_SYNC_LINE                            0UL
#define TRIG_INITIATOR_XCP_INDEPENDENT                      1UL
#define TRIG_INITIATOR_MULTICAST                            2UL
#define TRIG_INITIATOR_MULTICAST_TS_BRIDGE                  3UL
#define TRIG_INITIATOR_SYNC_STATE_CHANGE                    4UL
#define TRIG_INITIATOR_LEAP_SECOND                          5UL
#define TRIG_INITIATOR_ECU_RESET_RELEASE                    6UL
#define TRIG_INITIATOR_RESERVED                             7UL

#define TIME_OF_TS_SAMPLING_PROTOCOL_PROCESSOR              0UL
#define TIME_OF_TS_SAMPLING_LOW_JITTER                      1UL
#define TIME_OF_TS_SAMPLING_PHY_TRANSMISSION                2UL
#define TIME_OF_TS_SAMPLING_PHY_RECEPTION                   3UL

#define EV_TIME_SYNC_PAYLOAD_FMT_DWORD                      1UL
#define EV_TIME_SYNC_PAYLOAD_FMT_DLONG                      2UL

/* TRIGGER_INITIATOR:
    0 = HW trigger, i.e.Vector Syncline
    1 = Event derived from XCP - independent time synchronization event – e.g.globally synchronized pulse per second signal
    2 = GET_DAQ_CLOCK_MULTICAST
    3 = GET_DAQ_CLOCK_MULTICAST via Time Sync Bridge
    4 = State change in syntonization / synchronization to grandmaster clock(either established or lost, additional information is provided by the SYNC_STATE field - see Table 236)
    5 = Leap second occurred on grandmaster clock
    6 = release of ECU reset
*/

/***************************************************************************/
/* XCP Transport Layer Commands and Responces, Type Definition */
/***************************************************************************/


/* Transport Layer commands */
#define CC_TL_GET_SLAVE_ID                                  0xFF
#define CC_TL_GET_SLAVE_ID_EXTENDED                         0xFD
#define CC_TL_SET_SLAVE_IP                                  0xFC
#define CC_TL_GET_DAQ_CLOCK_MULTICAST                       0xFA
#define CRO_TL_SUBCOMMAND                                   CRO_BYTE(1)

/* GET_SLAVE_ID */
#define CRO_TL_SLV_DETECT_PORT                              CRO_WORD(1)
#define CRO_TL_SLV_DETECT_MCAST_IP_ADDR                     CRO_DWORD(1)

#define TL_SLV_DETECT_STATUS_PROTOCOL_TCP                   0
#define TL_SLV_DETECT_STATUS_PROTOCOL_UDP                   1
#define TL_SLV_DETECT_STATUS_PROTOCOL_TCP_UDP               2
#define TL_SLV_DETECT_STATUS_PROTOCOL_RESERVED              3
#define TL_SLV_DETECT_STATUS_IP_VERSION_IPV4                (0)
#define TL_SLV_DETECT_STATUS_IP_VERSION_RESERVED            (1<<2)
#define TL_SLV_DETECT_STATUS_SLV_AVAILABILITY_BUSY          (1<<3)
#define TL_SLV_DETECT_STATUS_SLV_ID_EXT_SUPPORTED           (1<<4)

/* GET_SLAVE_ID_EXTENDED */
#define TL_SLV_DETECT_STATUS_SLV_ID_EXT_RADAR_DATA          (1<<0)
#define TL_SLV_DETECT_STATUS_SLV_ID_EXT_XCP_ON_PCIE         (1<<1)

/* GET_DAQ_CLOCK_MULTICAST */
#define CRO_TL_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER             CRO_WORD(1)
#define CRO_TL_DAQ_CLOCK_MCAST_COUNTER                        CRO_BYTE(4)




/****************************************************************************/
/* Implementation                                                           */
/****************************************************************************/
  
#define CRO_BYTE(x)               (pCmd->b[x])
#define CRO_WORD(x)               (pCmd->w[x])
#define CRO_DWORD(x)              (pCmd->dw[x])

#define CRM_BYTE(x)               (gXcp.Crm.b[x])
#define CRM_WORD(x)               (gXcp.Crm.w[x])
#define CRM_DWORD(x)              (gXcp.Crm.dw[x])
#define CRM_DDWORD(x)             (*(vuint64*)&gXcp.Crm.dw[x])

  

/****************************************************************************/
/* Defaults and checks                                                      */
/****************************************************************************/

/* Check limits of the XCP imnplementation */
#if defined( XCPTL_CTO_SIZE )
#if ( XCPTL_MAX_CTO > 255 )
  #error "XCPTL_CTO_SIZE must be <= 255"
#endif
#if ( XCPTL_CTO_SIZE < 0x08 )
  #error "XCPTL_CTO_SIZE must be > 7"
#endif
#else
#error "Please define XCPTL_CTO_SIZE"
#endif

#if defined( XCPTL_DTO_SIZE )
#if ( XCPTL_DTO_SIZE < 0x08 )
#error "XCPTL_DTO_SIZE must be > 7"
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

/* Check configuration of XCP_TIMESTAMP_UNIT. */
#if defined ( XCP_TIMESTAMP_UNIT )
#if ( (XCP_TIMESTAMP_UNIT >> 4) > 9 ) || ( (XCP_TIMESTAMP_UNIT & 0x0F) > 0 )
#error "XCP_TIMESTAMP_UNIT is not valid."
#endif
#else
#error "Please define XCP_TIMESTAMP_UNIT"
#endif

/* Check configuration of XCP_TIMESTAMP_TICKS. */
#if defined ( XCP_TIMESTAMP_TICKS )
#if ( (XCP_TIMESTAMP_TICKS > 0xFFFF) || (XCP_TIMESTAMP_TICKS == 0) )
#error "Iillegal range of XCP_TIMESTAMP_TICKS"
#endif
#else
#error "Please define XCP_TIMESTAMP_TICKS"
#endif

/* Check configuration of kXcpDaqTimestampTicksPerUnit. */
#if defined ( XCP_TIMESTAMP_TICKS_MS )
#else
#error "Please define XCP_TIMESTAMP_TICKS_MS"
#endif



/****************************************************************************/
/* XCP Packet Type Definition                                               */
/****************************************************************************/

typedef union { 
  /* There might be a loss of up to 3 bytes. */
  vuint8  b[ ((XCPTL_CTO_SIZE + 3) & 0xFFC)      ];
  vuint16 w[ ((XCPTL_CTO_SIZE + 3) & 0xFFC) / 2  ];
  vuint32 dw[ ((XCPTL_CTO_SIZE + 3) & 0xFFC) / 4 ];
} tXcpCto;


/****************************************************************************/
/* DAQ Type Definition                                                      */
/****************************************************************************/


/* ODT */
/* Size must be even !!! */
typedef struct {
  vuint16 firstOdtEntry;       /* Absolute odt entry number */
  vuint16 lastOdtEntry;        /* Absolute odt entry number */
  vuint16 size;                /* Number of bytes */
} tXcpOdt;


/* DAQ list */
typedef struct {
  vuint16 lastOdt;             /* Absolute odt number */
  vuint16 firstOdt;            /* Absolute odt number */
  vuint16 eventChannel; 
#ifdef XCP_ENABLE_PACKED_MODE
  vuint16 sampleCount;         /* Packed mode */
#endif
  vuint8 flags;
  vuint8 res;
} tXcpDaqList;


/* Dynamic DAQ list structures */
typedef struct {
  vuint16         DaqCount;
  vuint16         OdtCount;       /* Absolute */
  vuint16         OdtEntryCount;  /* Absolute */
  union { 
    vuint8        b[XCP_DAQ_MEM_SIZE];
    tXcpDaqList   DaqList[XCP_DAQ_MEM_SIZE/sizeof(tXcpDaqList)]; 
  } u;
} tXcpDaq;


/* Event */
typedef struct {
    const char* name;
    vuint8 timeUnit;
    vuint8 timeCycle;
    vuint16 sampleCount; // packed event sample count
    vuint32 size; // ext event size
} tXcpEvent;


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
#define DaqListFlags(i)         gXcp.Daq.u.DaqList[i].flags
#define DaqListEventChannel(i)  gXcp.Daq.u.DaqList[i].eventChannel
#define DaqListSampleCount(i)    gXcp.Daq.u.DaqList[i].sampleCount


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

/* Return values for XcpEvent() */
#define XCP_EVENT_NOP               0x00u   /* Inactive (DAQ not running, Event not configured) */
#define XCP_EVENT_DAQ               0x01u   /* DAQ active */
#define XCP_EVENT_DAQ_OVERRUN       0x02u   /* DAQ queue overflow */
#define XCP_EVENT_DAQ_TIMEOUT       0x04u   /* Timeout supervision violation */
#define XCP_EVENT_STIM              0x08u   /* STIM active */
#define XCP_EVENT_STIM_OVERRUN      0x10u   /* STIM data not available */

/* Bitmasks for gXcp.SendStatus */
#define XCP_CRM_REQUEST             0x01u
#define XCP_DTO_REQUEST             0x02u
#define XCP_EVT_REQUEST             0x04u
#define XCP_CRM_PENDING             0x10u
#define XCP_DTO_PENDING             0x20u
#define XCP_EVT_PENDING             0x40u
#define XCP_SEND_PENDING            (XCP_DTO_PENDING|XCP_CRM_PENDING|XCP_EVT_PENDING)



/****************************************************************************/
/* XCP clock information                                                    */
/****************************************************************************/


typedef struct _T_CLOCK_INFORMATION {
    vuint8      UUID[8];
    vuint16     timestampTicks;
    vuint8      timestampUnit;
    vuint8      stratumLevel;
    vuint8      nativeTimestampSize;
    vuint8      fill[3]; // for alignment (8 byte) of structure
    vuint64     valueBeforeWrapAround;
} T_CLOCK_INFORMATION;

#ifdef XCP_ENABLE_PTP

typedef struct _T_CLOCK_INFORMATION_GRANDM {
    vuint8     UUID[8];
    vuint16    timestampTicks;
    vuint8     timestampUnit;
    vuint8     stratumLevel;
    vuint8     nativeTimestampSize;
    vuint8     epochOfGrandmaster;
    vuint8     fill[2]; // for alignment (8 byte) of structure
    vuint64    valueBeforeWrapAround;
} T_CLOCK_INFORMATION_GRANDM;

typedef struct _T_CLOCK_RELATION {
    vuint64  timestampOrigin;
    vuint64  timestampLocal;
} T_CLOCK_RELATION;

#endif

/****************************************************************************/
/* XCP data strucure                                                        */
/****************************************************************************/

typedef struct {

    /* Crm has to be the first object of this structure !! (refer to XcpInit()) */

    tXcpCto Crm;                           /* RES,ERR Message buffer */
    vuint8 CrmLen;                        /* RES,ERR Message length */

    vuint8 SessionStatus;

    vuint8* Mta;                        /* Memory Transfer Address */

    /*
      Dynamic DAQ list structures
      This structure should be stored in resume mode
    */
    tXcpDaq Daq;
    tXcpOdt* pOdt;
    vuint32* pOdtEntryAddr;
    vuint8* pOdtEntrySize;

    vuint64 DaqStartClock64;
    vuint32 DaqOverflowCount;

    /* State info from SET_DAQ_PTR for WRITE_DAQ and WRITE_DAQ_MULTIPLE */
    vuint16 WriteDaqOdtEntry;
    vuint16 WriteDaqOdt;
    vuint16 WriteDaqDaq;

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0130
    vuint16 ClusterId;
    T_CLOCK_INFORMATION SlaveClockInfo;

#ifdef XCP_ENABLE_PTP
  T_CLOCK_INFORMATION_GRANDM GrandmasterClockInfo;
  T_CLOCK_RELATION SlvGrandmClkRelationInfo;

#endif

#endif

} tXcpData;


/****************************************************************************/
/* Interface                                                                */
/****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif


/* Initialization for the XCP Protocol Layer */
extern void XcpInit( void );
extern void XcpDisconnect(void);

/* Trigger a XCP data acquisition or stimulation event */
extern void XcpEvent(vuint16 event); 
extern void XcpEventExt(vuint16 event, vuint8* base);
extern void XcpEventAt(vuint16 event, vuint32 clock );

/* XCP command processor */
extern void XcpCommand( const vuint32* pCommand );

/* Send an XCP event message */
extern void XcpSendEvent(vuint8 evc, const vuint8* d, vuint8 l);

/* Check status */
extern vuint8 XcpIsConnected(void);
extern vuint8 XcpIsDaqRunning(void);


/*-----------------------------------------------------------------------------------*/
/* Functions or Macros that have to be provided externally to the XCP Protocol Layer */
/*-----------------------------------------------------------------------------------*/


/* Callback functions for xcpLite.c */
/* All functions must be thread save */

/* Transmission of a single XCP CRM Packet (for a command resonse message ) */
#if defined ( ApplXcpSendCrm )
  // defined as macro
#else
extern void ApplXcpSendCrm(vuint8 len, const vuint8* msg);
#endif

/* Prepare start of data acquisition */
#if defined ( ApplXcpPrepareDaqStart )
  // defined as macro
#else
extern void ApplXcpPrepareDaqStart();
#endif

/* Start of data acquisition (clear transport layer queue) */
#if defined ( ApplXcpDaqStart )
  // defined as macro
#else
extern void ApplXcpDaqStart();
#endif

/* Stop of data acquisition */
#if defined ( ApplXcpDaqStop )
  // defined as macro
#else
extern void ApplXcpDaqStop();
#endif

/* Set cluster id */
#if defined ( ApplXcpSetClusterId )
  // defined as macro
#else
extern void ApplXcpSetClusterId( vuint16 clusterId );
#endif

/* Get and commit a transmit buffer for a single XCP DTO Packet (for a data transfer message) */
#if defined ( ApplXcpGetDtoBuffer )
  // defined as macro
#else
extern unsigned char* ApplXcpGetDtoBuffer(vuint8 len);
#endif
#if defined ( ApplXcpCommitDtoBuffer )
// defined as macro
#else
extern void ApplXcpCommitDtoBuffer(vuint8 *buf);
#endif

/* Generate a native pointer from XCP address extension and address */
#if defined ( ApplXcpGetPointer )
// defined as macro
#else
extern vuint8* ApplXcpGetPointer(vuint8 addr_ext, vuint32 addr);
#endif
#if defined ( ApplXcpGetAddr )
// defined as macro
#else
extern vuint32 ApplXcpGetAddr(vuint8* p);
#endif
#if defined ( ApplXcpGetBaseAddr )
// defined as macro
#else
extern vuint8 *ApplXcpGetBaseAddr();
#endif

#ifdef XCP_ENABLE_CAL_PAGE
extern vuint8 ApplXcpGetCalPage(vuint8 segment, vuint8 mode);
extern vuint8 ApplXcpSetCalPage(vuint8 segment, vuint8 page, vuint8 mode);
#endif

// DAQ clock provided by application
#if defined ( ApplXcpGetClock )
// defined as macro
#else
extern vuint32 ApplXcpGetClock(void);
#endif
#if defined ( ApplXcpGetClock64 )
// defined as macro
#else
extern vuint64 ApplXcpGetClock64(void);
#endif

/* Info for GET_EVENT_INFO*/
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
#if defined ( ApplXcpEventCount )
// defined as macro
#else
extern vuint16 ApplXcpEventCount;
#endif
#if defined ( ApplXcpEventList )
// defined as macro
#else
extern tXcpEvent ApplXcpEventList[];
#endif
#endif

/* Info for GET_ID */
#if defined ( ApplXcpSlaveIdLen )
// defined as macro
#else
extern vuint16 ApplXcpSlaveIdLen;
#endif
#if defined ( ApplXcpSlaveId )
// defined as macro
#else
extern const vuint8 ApplXcpSlaveId[];
#endif

/* Info for GET_ID "a2l file name" (without extension) provided by application*/
#if defined ( XCP_ENABLE_A2L_NAME )
extern vuint8 ApplXcpGetA2LFilename(vuint8** p, vuint32* n, int path);
#endif


/* Info for GET_ID 4, A2L upload */
#if defined ( XCP_ENABLE_A2L_UPLOAD )
extern vuint8 ApplXcpReadA2LFile(vuint8** p, vuint32* n);
#endif


/****************************************************************************/
/* Test and debug                                                           */
/****************************************************************************/


#if defined ( XCP_ENABLE_TESTMODE )

#if defined ( ApplXcpDebugLevel )
// defined as macro
#else
extern volatile unsigned int ApplXcpDebugLevel;
#endif

#if defined ( ApplXcpPrint )
// defined as macro
#else
extern void ApplXcpPrint( const char *str, ... );
#endif

#endif 

#ifdef __cplusplus
}
#endif



/*----------------------------------------------------------------------------*/
/* Platform specific external dependencies */

#include "xcpAppl.h"


#endif

