#pragma once

/* ptpHdr.h */
/*
| Code released into public domain, no attribution required
*/

#pragma pack(push, 1)

#define PTP_SYNC 0
#define PTP_DELAY_REQ 1
#define PTP_PDELAY_REQ 2
#define PTP_PDELAY_RESP 3
#define PTP_FOLLOW_UP 8
#define PTP_DELAY_RESP 9
#define PTP_PDELAY_RESP_FOLLOW_UP 10
#define PTP_ANNOUNCE 11
#define PTP_SIGNALING 12
#define PTP_MANAGEMENT 13

#define PTP_FLAG_TWO_STEP 0x0200         // Sync, Pdelay_Resp
#define PTP_FLAG_UNICAST 0x0400          // All
#define PTP_FLAG_UTC_OFFSET_VALID 0x0004 // Announce
#define PTP_FLAG_PTP_TIMESCALE 0x0008    // Announce
#define PTP_FLAG_TIME_TRACEABLE 0x0010   // Announce
#define PTP_FLAG_FREQ_TRACEABLE 0x0020   // Announce

#define PTP_CLOCK_ACC_25NS 0x20   // 25ns
#define PTP_CLOCK_ACC_ATOMIC 0x20 // 25ns
#define PTP_CLOCK_ACC_GPS 0x22    // 250ns
#define PTP_CLOCK_ACC_1US 0x23    // 1us
#define PTP_CLOCK_ACC_1MS 0x29    // 1ms
#define PTP_CLOCK_ACC_NTP 0x2F    // 1s
#define PTP_CLOCK_ACC_1S 0x2F     // 1s
#define PTP_CLOCK_ACC_DEFAULT 0xFE

#define PTP_TIME_SOURCE_ATOMIC 0x10
#define PTP_TIME_SOURCE_GPS 0x20
#define PTP_TIME_SOURCE_RADIO 0x30
#define PTP_TIME_SOURCE_PTP 0x40
#define PTP_TIME_SOURCE_NTP 0x50
#define PTP_TIME_SOURCE_HANDSET 0x60
#define PTP_TIME_SOURCE_INTERNAL 0xA0

#define PTP_CLOCK_CLASS_PTP_PRIMARY 6
#define PTP_CLOCK_CLASS_PTP_PRIMARY_HOLDOVER 7
#define PTP_CLOCK_CLASS_ARB_PRIMARY 13
#define PTP_CLOCK_CLASS_ARB_PRIMARY_HOLDOVER 14
#define PTP_CLOCK_CLASS_DEFAULT 248 // 0xF8

// PTP TIME
// 10 bytes
struct ptptime {
    uint16_t timestamp_s_hi; // 0
    uint32_t timestamp_s;    // 2
    uint32_t timestamp_ns;   // 6
}; // 10

// PTP ANOUNCE message
// 20 bytes
struct announce {
    uint16_t utcOffset; // if PTP_FLAG_UTC_OFFSET_VALID
    uint8_t res;
    uint8_t priority1;
    uint8_t clockClass;
    uint8_t clockAccuraccy; // 0xfe unknown
    uint16_t clockVariance;
    uint8_t priority2;
    uint8_t grandmasterId[8];
    uint16_t stepsRemoved;
    uint8_t timeSource; // INTERNAL_OSC 0xa0
};

// PTP
// 44/64 bytes
struct ptphdr {
    // Header 34 bytes
    uint8_t type;               // 0
    uint8_t version;            // 1
    uint16_t len;               // 2
    uint8_t domain;             // 4
    uint8_t res1;               // 5
    uint16_t flags;             // 6
    uint64_t correction;        // 8
    uint32_t res2;              // 16
    uint8_t clockId[8];         // 20
    uint16_t sourcePortId;      // 28
    uint16_t sequenceId;        // 30
    uint8_t controlField;       // 32
    uint8_t logMessageInterval; // 33

    // Timestamp 10 bytes
    struct ptptime timestamp; // 34
                              // 44
    union {

        // DELAY_RESP
        struct {
            uint8_t clockId[8];
            uint16_t sourcePortId;
        } r; // 54

        // ANNOUNCE
        struct announce a;

    } u;
};

#pragma pack(pop)
