#pragma once

/* ptp.h */

#define FILTER_MAX_SIZE 40

typedef struct average_filter {
    int64_t a[FILTER_MAX_SIZE];
    int64_t as;
    uint8_t ai;
    uint8_t size;
    uint8_t am;
} filter_average_t;


typedef struct median_filter {
    uint64_t a[FILTER_MAX_SIZE];
    uint8_t ai;
    uint8_t size;
} filter_median_t;

#pragma pack(push, 1)

// 10 bytes
struct ptptime {
    uint16_t timestamp_s_hi; // 0
    uint32_t timestamp_s;    // 2
    uint32_t timestamp_ns;   // 6
};                          // 10

// 44 bytes
struct ptphdr
{
#define PTP_SYNC         0x00
#define PTP_DELAY_REQ    0x01
#define PTP_PDELAY_REQ   0x02
#define PTP_PDELAY_RESP  0x03
#define PTP_FOLLOW_UP    0x08
#define PTP_PDELAY_RESP_FOLLOW_UP  0x0A
#define PTP_ANNOUNCE     0x0B
#define PTP_SIGNALING    0x0C
#define PTP_MANAGEMENT   0x0D
    uint8_t type;                   // 0
    uint8_t version;                // 1
    uint16_t len;                   // 2
    uint8_t domain;                 // 4
    uint8_t res1;                   // 5
#define PTP_FLAGS_TWO_STEP        0x0200   // Sync, Pdelay_Resp
#define PTP_FLAG_UNICAST          0x0400   // All
#define PTP_FLAG_UTC_OFFSET_VALID 0x0004   // Announce
#define PTP_FLAG_PTP_TIMESCALE    0x0008   // Announce
#define PTP_FLAG_TIME_TRACEABLE   0x0010   // Announce
#define PTP_FLAG_FREQ_TRACEABLE   0x0020   // Announce
    uint16_t flags;                 // 6
    uint64_t correction;            // 8
    uint32_t res2;                  // 16
    uint8_t clockId[8];             // 20
    uint16_t sourcePortId;          // 28
    uint16_t sequenceId;            // 30
    uint8_t controlField;           // 32
    uint8_t logMessageInterval;     // 33
    struct ptptime timestamp;       // 34
                                    // 44
    union {

        // PDELAY_RESP
        struct {
            uint8_t clockId[8];
            uint16_t sourcePortId;
        } r; // 54

        // ANOUNCE
        struct {
            uint16_t utcOffset;
            uint8_t  res3;
            uint8_t  priority1;
            uint8_t  clockClass;
#define PTP_CLOCK_ACC_ATOMIC 0x20
#define PTP_CLOCK_ACC_GPS    0x22
#define PTP_CLOCK_ACC_NTP    0x2F // 0x23-0x2F
#define PTP_CLOCK_ACC_HANDSET
#define PTP_CLOCK_ACC_DEFAULT  0xFE

            uint8_t  clockAccuraccy; // 0xfe unknown
            uint16_t clockVariance;
            uint8_t  priority2;
            uint8_t  grandmasterId[8];
            uint16_t stepsRemoved;
            uint8_t timeSource; // INTERNAL_OSC 0xa0
        } a; // 64

    } u;
};
#pragma pack(pop)


typedef struct {
    unsigned int index;
    uint8_t  domain;
    uint8_t  id[8];
    uint8_t  addr[4];
    uint16_t utcOffset;
    uint16_t flags;
    uint8_t  priority1;
    uint8_t  clockClass; // 6,7 GPS
    uint8_t  clockAccuraccy; // 0xfe unknown
    uint16_t clockVariance;
    uint8_t  priority2;
    uint16_t sourcePortId;
    uint8_t  grandmasterId[8];
    uint16_t stepsRemoved;
    uint8_t  timeSource; // INTERNAL_OSC 0xa0, GPS ?????
    uint64_t lastSeenTime;
} tPtpMaster;



typedef struct {

    BOOL Enabled; // Status
    uint8_t Sync; // Sync status

    tXcpThread ThreadHandle320;
    tXcpThread ThreadHandle319;
    SOCKET Sock320;
    SOCKET Sock319;

    // List of all announced masters
    #define PTP_MAX_MASTER 16
    tPtpMaster MasterList[PTP_MAX_MASTER];
    unsigned int MasterCount;

    uint8_t Domain; // Domain to be used

    // Sync and followup states
    uint64_t sync_local_time;
    uint64_t sync_master_time;
    uint32_t sync_correction;
    uint16_t sync_seq;
    uint8_t  sync_steps;
    uint64_t flup_local_time;
    uint64_t flup_master_time;
    uint32_t flup_correction;
    uint16_t flup_seq;

    // Clock servo and synch
    #define OFFSET_FILTER_SIZE 9
    filter_average_t OffsetFilter;
    filter_median_t OffsetTimeFilter;
    #define DRIFT_FILTER_SIZE 20
    filter_average_t DriftFilter;

    int64_t RawOffset; // current sync pair grandmaster offset
    int64_t LastRefOffset;// last sync pair grandmaster offset
    uint64_t LastLocalTime; // last sync local time


    int64_t RefOffset; // estimated clock offset to grandmaster
    uint64_t RefTime; // estimated time for RefOffset

    int32_t Drift; // estimated and filtered clock drift to grandmaster drift

    uint32_t SyncCounter;

    int64_t CorrOffset;
    tPtpMaster* Gm; // Grandmaster, NULL is not found yet

    //int32_t CorrDrift;
    //uint8_t CorrServo; // Servo on/off
    //uint8_t CorrServoRate; // Max Servo time correction rate in %


} tPtp;

//extern tPtp gPtp;

extern int ptpInit(uint8_t ptpDomain);
extern int ptpShutdown();
extern uint64_t ptpClockGet64();
extern void ptpClockCheckStatus();

#if OPTION_ENABLE_PTP_TEST
extern void ptpCreateTestEvent();
extern void ptpCreateTestA2lDescription();
#endif

extern uint8_t* ptpClockGetUUID();

// Clock info and state
// For simlicity matches XCP clock definitions

#define CLOCK_STATE_SYNCH_IN_PROGRESS                  (0 << 0)
#define CLOCK_STATE_SYNCH                              (1 << 0)
#define CLOCK_STATE_FREE_RUNNING                       (7 << 0)
#define CLOCK_STATE_GRANDMASTER_STATE_SYNC_IN_PROGRESS (0 << 3)
#define CLOCK_STATE_GRANDMASTER_STATE_SYNC             (1 << 3)
extern uint8_t ptpClockGetState();

#define CLOCK_STRATUM_LEVEL_UNKNOWN   255
#define CLOCK_STRATUM_LEVEL_ARB       16   // unsychronized
#define CLOCK_STRATUM_LEVEL_UTC       0    // Atomic reference clock
#define CLOCK_EPOCH_TAI 0 // Atomic monotonic time since 1.1.1970 (TAI)
#define CLOCK_EPOCH_UTC 1 // Universal Coordinated Time (with leap seconds) since 1.1.1970 (UTC)
#define CLOCK_EPOCH_ARB 2 // Arbitrary (unknown)
extern BOOL ptpClockGetGrandmasterInfo(uint8_t* uuid, uint8_t* epoch, uint8_t* stratumLevel);

extern BOOL ptpClockPrepareDaq();

