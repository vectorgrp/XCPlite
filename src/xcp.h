#pragma once

/* xcp.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */


/***************************************************************************/
/* Commands                                                                */
/***************************************************************************/

/*-------------------------------------------------------------------------*/
/* Standard Commands */

#define CC_CONNECT                        0xFF
#define CC_DISCONNECT                     0xFE
#define CC_GET_STATUS                     0xFD
#define CC_SYNCH                          0xFC

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
/* Data acquisition and Stimulation Commands (DAQ/STIM) */

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

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0101
#define CC_WRITE_DAQ_MULTIPLE            0xC7 /* XCP V1.1 */
#endif

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103 
#define CC_TIME_CORRELATION_PROPERTIES   0xC6 /* XCP V1.3 */
#endif

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104 
#define DTO_CTR_PROPERTIES                0xC5 /* XCP V1.4 */
#endif

/* 0xC1 ... 0xC4  not used */

#define CC_NOP                            0xC1 /* No response, used for testing */

// Level 1 commands
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
#define CC_LEVEL_1_COMMAND                0xC0 /* XCP V1.4 with sub commands  */
#define CC_GET_VERSION                    0x00
#define CC_SET_DAQ_LIST_PACKED_MODE       0x01
#define CC_GET_DAQ_LIST_PACKED_MODE       0x02
#define CC_SW_DBG_OVER_XCP                0xFC
#endif

/*-------------------------------------------------------------------------*/
/* Packet Identifiers Server -> Master */
#define PID_RES                           0xFF   /* response packet        */
#define PID_ERR                           0xFE   /* error packet           */
#define PID_EV                            0xFD   /* event packet           */
#define PID_SERV                          0xFC   /* service request packet */


/*-------------------------------------------------------------------------*/
/* Command Return Codes */

#define CRC_CMD_OK                              0x00
#define CRC_CMD_SYNCH                           0x00
#define CRC_CMD_PENDING                         0x01
#define CRC_CMD_IGNORED                         0x02
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
#define EVC_TIME_SYNCH         0x08
#define EVC_STIM_TIMEOUT       0x09
#define EVC_SLEEP              0x0A
#define EVC_WAKEUP             0x0B
#define EVC_ECU_STATE          0x0C
#define EVC_USER               0xFE
#define EVC_TRANSPORT          0xFF


/*-------------------------------------------------------------------------*/
/* Service Request Codes */

#define SERV_RESET                            0x00 /* Server requesting to be reset */
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
#define CMB_SERVER_BLOCK_MODE         (0x01u<<6)
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

// XCP states (low byte) */
#define SS_STORE_CAL_REQ       ((uint16_t)0x0001)
#define SS_PAG_CFG_LOST        ((uint16_t)0x0002)
#define SS_STORE_DAQ_REQ       ((uint16_t)0x0004)
#define SS_CLEAR_DAQ_REQ       ((uint16_t)0x0008)
#define SS_DAQ_CFG_LOST        ((uint16_t)0x0010)
#define SS_UNUSED              ((uint16_t)0x0020)
#define SS_DAQ                 ((uint16_t)0x0040)
#define SS_RESUME              ((uint16_t)0x0080)

// Internal states (high byte) */
#define SS_BLOCK_UPLOAD        ((uint16_t)0x0100) /* Block upload in progress */
#define SS_LEGACY_MODE         ((uint16_t)0x0200) /* XCP 1.3 legacy mode */
#define SS_CMD_PENDING         ((uint16_t)0x0800) /* async command pending */
#define SS_INITIALIZED         ((uint16_t)0x8000) /* initialized */
#define SS_STARTED             ((uint16_t)0x4000) /* started*/ 
#define SS_CONNECTED           ((uint16_t)0x2000) /* connected */


/*-------------------------------------------------------------------------*/
/* Identifier Type (GET_ID) */

#define IDT_ASCII                           0
#define IDT_ASAM_NAME                       1
#define IDT_ASAM_PATH                       2
#define IDT_ASAM_URL                        3
#define IDT_ASAM_UPLOAD                     4
#define IDT_ASAM_EPK                        5
#define IDT_ASAM_ECU                        6
#define IDT_ASAM_SYSID                      7
#define IDT_VECTOR_MAPNAMES                 0xDB
#define IDT_VECTOR_GET_A2LOBJECTS_FROM_ECU  0xA2


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

#define CAL_PAGE_MODE_ECU                0x01
#define CAL_PAGE_MODE_XCP                0x02
#define CAL_PAGE_MODE_ALL                0x80


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

#define SEGMENT_FLAG_FREEZE               0x01

/*-------------------------------------------------------------------------*/
/* SET_REQUEST_MODE */

#define SET_REQUEST_MODE_STORE_CAL              0x01 // Request to store calibration data
#define SET_REQUEST_MODE_STORE_DAQ_NORES        0x02 // Request to store DAQ configuration, no resume
#define SET_REQUEST_MODE_STORE_DAQ_RES          0x04 // Request to store DAQ configuration, resume enabled
#define SET_REQUEST_MODE_CLEAR_DAQ              0x08 // Request to clear DAQ configuration
#define SET_REQUEST_MODE_CLEAR_CAL_PAG_LOST     0x10 // Request to clear calibration page lost flag
#define SET_REQUEST_MODE_CLEAR_DAQ_LOST         0x20 // Request to clear DAQ configuration lost flag


/*-------------------------------------------------------------------------*/
/* DAQ list flags (from GET_DAQ_LIST_MODE, SET_DAQ_LIST_MODE) */

// DAQ list mode bit mask coding
#define DAQ_MODE_ALTERNATING              ((uint8_t)0x01) /* Bit0 - Enable/disable alternating display mode */
#define DAQ_MODE_DIRECTION                ((uint8_t)0x02) /* Bit1 - DAQ list stim mode */
#define DAQ_MODE_RESERVED2                ((uint8_t)0x04) /* Bit2 - Not used */
#define DAQ_MODE_DTO_CTR                  ((uint8_t)0x08) /* Bit3 - Use DTO CTR field */
#define DAQ_MODE_TIMESTAMP                ((uint8_t)0x10) /* Bit4 - Enable timestamp */
#define DAQ_MODE_PID_OFF                  ((uint8_t)0x20) /* Bit5 - Disable PID */
#define DAQ_MODE_RESERVED6                ((uint8_t)0x40) /* Bit6 - Not used */
#define DAQ_MODE_RESERVED7                ((uint8_t)0x80) /* Bit7 - Not used */


/*-------------------------------------------------------------------------*/
/* DAQ list state */

// DAQ list state bit mask coding
#define DAQ_STATE_STOPPED_UNSELECTED       ((uint8_t)0x00) /* Not selected, stopped */
#define DAQ_STATE_SELECTED                 ((uint8_t)0x01) /* Selected */
#define DAQ_STATE_RUNNING                  ((uint8_t)0x02) /* Running */
#define DAQ_STATE_OVERRUN                  ((uint8_t)0x04) /* Overrun */



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

/* TIMESTAMP_MODE Bitmasks */
#define DAQ_TIMESTAMP_TYPE  0x07
#define DAQ_TIMESTAMP_FIXED 0x08
#define DAQ_TIMESTAMP_UNIT  0xF0

/* DAQ Timestamp Type */
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

/* USER_CMD */
#define CRO_USER_CMD_LEN                                4
#define CRO_USER_CMD_SUBCOMMAND                         CRO_BYTE(1)
#define CRO_USER_CMD_PAR1                               CRO_BYTE(2)
#define CRO_USER_CMD_PAR2                               CRO_BYTE(3)

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
#define CRM_GET_ID_DATA_MAX_LEN                         (sizeof(CRM)-8)

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
#define CRO_UPLOAD_LEN                                  2
#define CRO_UPLOAD_SIZE                                 CRO_BYTE(1)
#define CRM_UPLOAD_MAX_SIZE                             ((uint8_t)(XCPTL_MAX_CTO_SIZE-1))
#define CRM_UPLOAD_LEN                                  1 /* +CRO_UPLOAD_SIZE */
#define CRM_UPLOAD_DATA                                 (&CRM_BYTE(1))

/* SHORT_UPLOAD */
#define CRO_SHORT_UPLOAD_LEN                            8
#define CRO_SHORT_UPLOAD_SIZE                           CRO_BYTE(1)
#define CRO_SHORT_UPLOAD_EXT                            CRO_BYTE(3)
#define CRO_SHORT_UPLOAD_ADDR                           CRO_DWORD(1)
#define CRM_SHORT_UPLOAD_MAX_SIZE                       ((uint8_t)(XCPTL_MAX_CTO_SIZE-1))
#define CRM_SHORT_UPLOAD_LEN                            1u /* +CRO_SHORT_UPLOAD_SIZE */
#define CRM_SHORT_UPLOAD_DATA                           (&CRM_BYTE(1))

/* BUILD_CHECKSUM */
#define CRO_BUILD_CHECKSUM_LEN                          8
#define CRO_BUILD_CHECKSUM_SIZE                         CRO_DWORD(1)
#define CRM_BUILD_CHECKSUM_LEN                          8
#define CRM_BUILD_CHECKSUM_TYPE                         CRM_BYTE(1)
#define CRM_BUILD_CHECKSUM_RESULT                       CRM_DWORD(1)

/* DOWNLOAD */
#define CRO_DOWNLOAD_MAX_SIZE                           ((uint8_t)(XCPTL_MAX_CTO_SIZE-2))
#define CRO_DOWNLOAD_LEN                                2 /* + CRO_DOWNLOAD_SIZE */
#define CRO_DOWNLOAD_SIZE                               CRO_BYTE(1)
#define CRO_DOWNLOAD_DATA                               (&CRO_BYTE(2))
#define CRM_DOWNLOAD_LEN                                1

/* DOWNLOAD_NEXT */
#define CRO_DOWNLOAD_NEXT_MAX_SIZE                      ((uint8_t)(XCPTL_MAX_CTO_SIZE-2))
#define CRO_DOWNLOAD_NEXT_LEN                           2 /* + size */
#define CRO_DOWNLOAD_NEXT_SIZE                          CRO_BYTE(1)
#define CRO_DOWNLOAD_NEXT_DATA                          (&CRO_BYTE(2))
#define CRM_DOWNLOAD_NEXT_LEN                           1

/* DOWNLOAD_MAX */
#define CRO_DOWNLOAD_MAX_MAX_SIZE                       ((uint8_t)(XCPTL_MAX_CTO_SIZE-1))
#define CRO_DOWNLOAD_MAX_DATA                           (&CRO_BYTE(1))
#define CRM_DOWNLOAD_MAX_LEN                            1

/* SHORT_DOWNLOAD */
#define CRO_SHORT_DOWNLOAD_MAX_SIZE                     ((uint8_t)(XCPTL_MAX_CTO_SIZE-8))
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

/* SET_REQUEST */
#define CRO_SET_REQUEST_LEN                             4
#define CRO_SET_REQUEST_MODE                            CRO_BYTE(1)
#define CRO_SET_REQUEST_ID                              CRO_WORD(1)
#define CRM_SET_REQUEST_LEN                             1

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
#define CRO_WRITE_DAQ_MULTIPLE_LEN(n)                   (2+(n)*8)
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
#define CRO_START_STOP_DAQ_LIST_LEN                     4
#define CRO_START_STOP_DAQ_LIST_MODE                    CRO_BYTE(1)
#define CRO_START_STOP_DAQ_LIST_DAQ                     CRO_WORD(1)
#define CRM_START_STOP_DAQ_LIST_LEN                     2
#define CRM_START_STOP_DAQ_LIST_FIRST_PID               CRM_BYTE(1)

/* START_STOP_SYNCH */
#define CRO_START_STOP_SYNCH_LEN                        2
#define CRO_START_STOP_SYNCH_MODE                       CRO_BYTE(1)
#define CRM_START_STOP_SYNCH_LEN                        1

/* GET_DAQ_CLOCK */
#define CRO_GET_DAQ_CLOCK_LEN                           1
#define CRM_GET_DAQ_CLOCK_LEN                           8
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
#define CRM_GET_DAQ_CLOCK_RES1                          CRM_BYTE(1)   // 1
#define CRM_GET_DAQ_CLOCK_TRIGGER_INFO                  CRM_BYTE(2)   // 2
#define CRM_GET_DAQ_CLOCK_PAYLOAD_FMT                   CRM_BYTE(3)   // 3
#define CRM_GET_DAQ_CLOCK_TIME                          CRM_DWORD(1)  // 4
#define CRM_GET_DAQ_CLOCK_SYNCH_STATE                   CRM_BYTE(8)   // 8
#define CRM_GET_DAQ_CLOCK_TIME64_LOW                    CRM_DWORD(1)  // 4
#define CRM_GET_DAQ_CLOCK_TIME64_HIGH                   CRM_DWORD(2)  // 8
#define CRM_GET_DAQ_CLOCK_SYNCH_STATE64                 CRM_BYTE(12)  // 12
#else
#define CRM_GET_DAQ_CLOCK_TIME                          CRM_DWORD(1)  // 4
#endif
#define DAQ_CLOCK_PAYLOAD_FMT_SLV_32                    (1<<0)
#define DAQ_CLOCK_PAYLOAD_FMT_SLV_64                    (2<<0) 
#define DAQ_CLOCK_PAYLOAD_FMT_ID                        (1<<6)

/* GET_DAQ_CLOCK_MULTICAST */
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
#define CRO_GET_DAQ_CLOC_MCAST_LEN                      4
#define CRM_GET_DAQ_CLOCK_MCAST_LEN                     8
#define CRO_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER      CRO_WORD(1)
#define CRO_GET_DAQ_CLOCK_MCAST_COUNTER                 CRO_BYTE(4)
#define CRM_GET_DAQ_CLOCK_MCAST_TRIGGER_INFO            CRM_BYTE(2)   // 2
#define CRM_GET_DAQ_CLOCK_MCAST_PAYLOAD_FMT             CRM_BYTE(3)   // 3
#define CRM_GET_DAQ_CLOCK_MCAST_TIME                    CRM_DWORD(1)  // 4
#define CRM_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER      CRM_WORD(4)   // 8
#define CRM_GET_DAQ_CLOCK_MCAST_COUNTER                 CRM_BYTE(10)  // 10
#define CRM_GET_DAQ_CLOCK_MCAST_SYNCH_STATE             CRM_BYTE(11)  // 11
#define CRM_GET_DAQ_CLOCK_MCAST_TIME64_LOW              CRM_DWORD(1)  // 4
#define CRM_GET_DAQ_CLOCK_MCAST_TIME64_HIGH             CRM_DWORD(2)  // 8
#define CRM_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER64    CRM_WORD(6)   // 12
#define CRM_GET_DAQ_CLOCK_MCAST_COUNTER64               CRM_BYTE(14)  // 14
#define CRM_GET_DAQ_CLOCK_MCAST_SYNCH_STATE64           CRM_BYTE(15)  // 15
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
#define CRM_GET_DAQ_EVENT_INFO_LEN                      7
#define CRM_GET_DAQ_EVENT_INFO_PROPERTIES               CRM_BYTE(1)
#define CRM_GET_DAQ_EVENT_INFO_MAX_DAQ_LIST             CRM_BYTE(2)
#define CRM_GET_DAQ_EVENT_INFO_NAME_LENGTH              CRM_BYTE(3)
#define CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE               CRM_BYTE(4)
#define CRM_GET_DAQ_EVENT_INFO_TIME_UNIT                CRM_BYTE(5)
#define CRM_GET_DAQ_EVENT_INFO_PRIORITY                 CRM_BYTE(6)
#define DAQ_EVENT_PROPERTIES_DAQ      0x04
#define DAQ_EVENT_PROPERTIES_STIM     0x08
#define DAQ_EVENT_PROPERTIES_PACKED   0x10
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
#define CRO_PROGRAM_MAX_SIZE                            ((uint8_t)(XCPTL_MAX_CTO_SIZE-2))
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
#define CRO_PROGRAM_NEXT_MAX_SIZE                       ((uint8_t)(XCPTL_MAX_CTO_SIZE-2))
#define CRO_PROGRAM_NEXT_LEN                            2 /* + size */
#define CRO_PROGRAM_NEXT_SIZE                           CRO_BYTE(1)
#define CRO_PROGRAM_NEXT_DATA                           (&CRO_BYTE(2))
#define CRM_PROGRAM_NEXT_LEN                            3
#define CRM_PROGRAM_NEXT_ERR_SEQUENCE                   CRM_BYTE(1)
#define CRM_PROGRAM_NEXT_SIZE_EXPECTED_DATA             CRM_BYTE(2)

/* PROGRAM_MAX */
#define CRO_PROGRAM_MAX_MAX_SIZE                        ((uint8_t)(XCPTL_MAX_CTO_SIZE-1))
#define CRO_PROGRAM_MAX_DATA                            (&CRO_BYTE(1))
#define CRM_PROGRAM_MAX_LEN                             1

/* PROGRAM_VERIFY */
#define CRO_PROGRAM_VERIFY_LEN                          8
#define CRO_PROGRAM_VERIFY_MODE                         CRO_BYTE(1)
#define CRO_PROGRAM_VERIFY_TYPE                         CRO_WORD(1)
#define CRO_PROGRAM_VERIFY_VALUE                        CRO_DWORD(1)
#define CRM_PROGRAM_VERIFY_LEN                          1

/* GET_SERVER_ID */
/*
#define CRO_GET_SERVER_ID_LEN                            6
#define CRO_GET_SERVER_ID_SUB_CODE                       CRO_BYTE(1)
#define CRO_GET_SERVER_ID_X                              CRO_BYTE(2)
#define CRO_GET_SERVER_ID_C                              CRO_BYTE(3)
#define CRO_GET_SERVER_ID_P                              CRO_BYTE(4)
#define CRO_GET_SERVER_ID_MODE                           CRO_BYTE(5)
#define CRM_GET_SERVER_ID_LEN                            8
#define CRM_GET_SERVER_ID_X                              CRM_BYTE(1)
#define CRM_GET_SERVER_ID_C                              CRM_BYTE(2)
#define CRM_GET_SERVER_ID_P                              CRM_BYTE(3)
#define CRM_GET_SERVER_ID_CAN_ID_CMD_STIM                CRM_DWORD(1)
*/

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

/* SET_SERVER_PORT */
#define CRO_SET_SERVER_PORT_LEN                          4
#define CRO_SET_SERVER_PORT_SUB_CODE                     CRO_BYTE(1)
#define CRO_SET_SERVER_PORT_PORT                         CRO_WORD(1)
#define CRM_SET_SERVER_PORT                              1

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
#define DPM_TIMESTAMP_MODE_LAST  0
#define DPM_TIMESTAMP_MODE_FIRST 1

/* TIME SYNCHRONIZATION PROPERTIES*/
#define CRO_TIME_SYNCH_PROPERTIES_LEN                       6
#define CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES            CRO_BYTE(1)
#define CRO_TIME_SYNCH_PROPERTIES_GET_PROPERTIES_REQUEST    CRO_BYTE(2)
#define CRO_TIME_SYNCH_PROPERTIES_CLUSTER_ID                CRO_WORD(2)

/* CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES: */
#define TIME_SYNCH_SET_PROPERTIES_RESPONSE_FMT              (3 << 0)
#define TIME_SYNCH_SET_PROPERTIES_TIME_SYNCH_BRIDGE         (3 << 2)
#define TIME_SYNCH_SET_PROPERTIES_CLUSTER_ID                (1 << 4)
#define TIME_SYNCH_RESPONSE_FMT_LEGACY                      0
#define TIME_SYNCH_RESPONSE_FMT_TRIGGER_SUBSET              1
#define TIME_SYNCH_RESPONSE_FMT_TRIGGER_ALL                 2

/* CRO_TIME_SYNCH_PROPERTIES_GET_PROPERTIES_REQUEST: */
#define TIME_SYNCH_GET_PROPERTIES_GET_CLK_INFO              (1 << 0)
#define CRM_TIME_SYNCH_PROPERTIES_LEN                       8
#define CRM_TIME_SYNCH_PROPERTIES_SERVER_CONFIG             CRM_BYTE(1)
#define CRM_TIME_SYNCH_PROPERTIES_OBSERVABLE_CLOCKS         CRM_BYTE(2)
#define CRM_TIME_SYNCH_PROPERTIES_SYNCH_STATE               CRM_BYTE(3)
#define CRM_TIME_SYNCH_PROPERTIES_CLOCK_INFO                CRM_BYTE(4)
#define CRM_TIME_SYNCH_PROPERTIES_RESERVED                  CRM_BYTE(5)
#define CRM_TIME_SYNCH_PROPERTIES_CLUSTER_ID                CRM_WORD(3)

/* CRM_TIME_SYNCH_PROPERTIES_SERVER_CONFIG: */
#define SERVER_CONFIG_RESPONSE_FMT_LEGACY                   (0)
#define SERVER_CONFIG_RESPONSE_FMT_ADVANCED                 (2)
#define SERVER_CONFIG_DAQ_TS_ECU                            (1 << 2)
#define SERVER_CONFIG_DAQ_TS_SERVER                         (0 << 2)
#define SERVER_CONFIG_TIME_SYNCH_BRIDGE_NONE                (0 << 3)

/* CRM_TIME_SYNCH_PROPERTIES_OBSERVABLE_CLOCKS: */
#define LOCAL_CLOCK_FREE_RUNNING    (0<<0)
#define LOCAL_CLOCK_SYNCHED         (1<<0)
#define LOCAL_CLOCK_NONE            (2<<0)
#define GRANDM_CLOCK_NONE           (0<<2)
#define GRANDM_CLOCK_READABLE       (1<<2)
#define GRANDM_CLOCK_EVENT          (2<<2)
#define ECU_CLOCK_NONE              (0<<4)
#define ECU_CLOCK_READABLE          (1<<4)
#define ECU_CLOCK_EVENT             (2<<4)
#define ECU_CLOCK_NOTREADABLE       (3<<4)

/* CRM_TIME_SYNCH_PROPERTIES_SYNCH_STATE: */
#define LOCAL_CLOCK_STATE_SYNCH_IN_PROGRESS       (0 << 0)
#define LOCAL_CLOCK_STATE_SYNCH                   (1 << 0)
#define LOCAL_CLOCK_STATE_SYNT_IN_PROGRESS        (2 << 0)
#define LOCAL_CLOCK_STATE_SYNT                    (3 << 0)
#define LOCAL_CLOCK_STATE_FREE_RUNNING            (7 << 0)
#define GRANDM_CLOCK_STATE_SYNCH_IN_PROGRESS      (0 << 3)
#define GRANDM_CLOCK_STATE_SYNCH                  (1 << 3)

/* CRM_TIME_SYNCH_PROPERTIES_CLOCK_INFO: */
#define CLOCK_INFO_SERVER           (1<<0)
#define CLOCK_INFO_GRANDM           (1<<1)
#define CLOCK_INFO_RELATION         (1<<2)
#define CLOCK_INFO_ECU              (1<<3)
#define CLOCK_INFO_ECU_GRANDM       (1<<4)

/* TRIGGER_INITIATOR:
    0 = HW trigger, i.e.Vector Syncline
    1 = Event derived from XCP - independent time synchronization event - e.g.globally synchronized pulse per second signal
    2 = GET_DAQ_CLOCK_MULTICAST
    3 = GET_DAQ_CLOCK_MULTICAST via Time Sync Bridge
    4 = State change in syntonization / synchronization to grandmaster clock(either established or lost, additional information is provided by the SYNCH_STATE field - see Table 236)
    5 = Leap second occurred on grandmaster clock
    6 = release of ECU reset
*/
#define TRIG_INITIATOR_SYNCH_LINE                           0UL
#define TRIG_INITIATOR_XCP_INDEPENDENT                      1UL
#define TRIG_INITIATOR_MULTICAST                            2UL
#define TRIG_INITIATOR_MULTICAST_TS_BRIDGE                  3UL
#define TRIG_INITIATOR_SYNCH_STATE_CHANGE                   4UL
#define TRIG_INITIATOR_LEAP_SECOND                          5UL
#define TRIG_INITIATOR_ECU_RESET_RELEASE                    6UL
#define TRIG_INITIATOR_RESERVED                             7UL

#define TIME_OF_TS_SAMPLING_PROTOCOL_PROCESSOR              0UL
#define TIME_OF_TS_SAMPLING_LOW_JITTER                      1UL
#define TIME_OF_TS_SAMPLING_PHY_TRANSMISSION                2UL
#define TIME_OF_TS_SAMPLING_PHY_RECEPTION                   3UL


/****************************************************************************/
/* XCP clock information                                                    */
/****************************************************************************/

#define XCP_STRATUM_LEVEL_UNKNOWN   255  // Unknown
#define XCP_STRATUM_LEVEL_RTC       3    // Realtime Clock
#define XCP_STRATUM_LEVEL_GPS       0    // GPS Clock

#define XCP_EPOCH_TAI 0 // Atomic monotonic time since 1.1.1970 (TAI)
#define XCP_EPOCH_UTC 1 // Universal Coordinated Time (with leap seconds) since 1.1.1970 (UTC)
#define XCP_EPOCH_ARB 2 // Arbitrary (unknown)

#pragma pack(push, 1)

typedef struct {
    uint8_t      UUID[8];
    uint16_t     timestampTicks;
    uint8_t      timestampUnit;
    uint8_t      stratumLevel;
    uint8_t      nativeTimestampSize;
    uint8_t      fill[3]; // for alignment (8 byte) of structure
    uint64_t     valueBeforeWrapAround;
} T_CLOCK_INFO;


#ifdef XCP_ENABLE_PTP

typedef struct {
    uint8_t     UUID[8];
    uint16_t    timestampTicks;
    uint8_t     timestampUnit;
    uint8_t     stratumLevel;
    uint8_t     nativeTimestampSize;
    uint8_t     epochOfGrandmaster;
    uint8_t     fill[2]; // for alignment (8 byte) of structure
    uint64_t    valueBeforeWrapAround;
} T_CLOCK_INFO_GRANDMASTER;

typedef struct {
    uint64_t  timestampOrigin;
    uint64_t  timestampLocal;
} T_CLOCK_INFO_RELATION;

#endif

#pragma pack(pop)

/***************************************************************************/
/* XCP Transport Layer Commands and Responces, Type Definition */
/***************************************************************************/


/* Transport Layer commands */
#define CC_TL_GET_SERVER_ID                                 0xFF
#define CC_TL_GET_SERVER_ID_EXTENDED                        0xFD
#define CC_TL_SET_SERVER_IP                                 0xFC
#define CC_TL_GET_DAQ_CLOCK_MULTICAST                       0xFA
#define CRO_TL_SUBCOMMAND                                   CRO_BYTE(1)


/* GET_SERVER_ID and GET_SERVER_ID_EXTENDED */
#define CRO_TL_GET_SERVER_ID_LEN                            21
#define CRO_TL_GET_SERVER_ID_PORT                           CRO_WORD(1)
#define CRO_TL_GET_SERVER_ID_ADDR(n)                        CRO_BYTE(4+n)
#define CRO_TL_GET_SERVER_ID_MODE                           CRO_BYTE(20)

/* GET_SERVER_ID */
/*
#define CRM_TL_GET_SERVER_ID_LEN(n)                         (24+1+(n))
#define CRM_TL_GET_SERVER_ID_ADDR(n)                        CRM_BYTE(n)
#define CRM_TL_GET_SERVER_ID_PORT                           CRM_WORD(8)
#define CRM_TL_GET_SERVER_ID_STATUS                         CRM_BYTE(18)
#define CRM_TL_GET_SERVER_ID_RESOURCE                       CRM_BYTE(19)
#define CRM_TL_GET_SERVER_ID_ID_LEN                         CRM_BYTE(20)
#define CRM_TL_GET_SERVER_ID_ID                             CRM_BYTE(21)
#define CRM_TL_GET_SERVER_ID_MAX_LEN                        128
*/

#define GET_SERVER_ID_STATUS_PROTOCOL_TCP                   0
#define GET_SERVER_ID_STATUS_PROTOCOL_UDP                   1
#define GET_SERVER_ID_STATUS_PROTOCOL_TCP_UDP               2
#define GET_SERVER_ID_STATUS_IP_VERSION_IPV4                (0)
#define GET_SERVER_ID_STATUS_SLV_AVAILABILITY_BUSY          (1<<3)
#define GET_SERVER_ID_STATUS_SLV_ID_EXT_SUPPORTED           (1<<4)

/* GET_SERVER_ID_EXT */
#define CRM_TL_GET_SERVER_ID_LEN(n)                         (24+1+(n))
#define CRM_TL_GET_SERVER_ID_ADDR(n)                        CRM_BYTE(2+n)
#define CRM_TL_GET_SERVER_ID_PORT                           CRM_WORD(18)
#define CRM_TL_GET_SERVER_ID_STATUS                         CRM_BYTE(20)
#define CRM_TL_GET_SERVER_ID_RESOURCE                       CRM_BYTE(21)
#define CRM_TL_GET_SERVER_ID_ID_LEN                         *(uint32_t*)&(CRM_BYTE(22)) // this is a DWORD on unaligned offset 
#define CRM_TL_GET_SERVER_ID_ID                             CRM_BYTE(26)
#define CRM_TL_GET_SERVER_ID_MAC(n)                         CRM_BYTE(26+n) /*+CRM_TL_GET_SERVER_ID_ID_LEN*/

#define CRM_TL_GET_SERVER_ID_MAX_LEN                        (XCPTL_MAX_CTO_SIZE-(26+6))

/* GET_SERVER_ID_EXTENDED */
#define TL_SLV_DETECT_STATUS_SLV_ID_EXT_RADAR_DATA          (1<<0)
#define TL_SLV_DETECT_STATUS_SLV_ID_EXT_XCP_ON_PCIE         (1<<1)

