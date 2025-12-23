/* ptp_cfg.h */

#define PTP_INTERFACE "eth0" // Network interface for PTP hardware timestamping if auto detection does not work
// #define PTP_INTERFACE "eno1" // Network interface for PTP hardware timestamping if auto detection does not work
// #define PTP_INTERFACE NULL

#define gPtpDebugLevel 3

#define OPTION_ENABLE_PTP_XCP

//----------------------------------------------------------------------------
#define OPTION_ENABLE_PTP_OBSERVER

#define MASTER_DRIFT_FILTER_SIZE 60
#define MASTER_JITTER_RMS_FILTER_SIZE 60
#define MASTER_JITTER_AVG_FILTER_SIZE 60

//----------------------------------------------------------------------------
// #define OPTION_ENABLE_PTP_MASTER

//---------------------------------------------------------------------------
// #define OPTION_ENABLE_PTP_CLOCK

//---------------------------------------------------------------------------
// #define OPTION_ENABLE_PTP_CLIENT

#ifdef OPTION_ENABLE_PTP_CLIENT
#define MAX_MASTERS 16
#define GRANDMASTER_LOST_TIMEOUT 10 // s
#define MASTER_DRIFT_FILTER_SIZE 16
#endif