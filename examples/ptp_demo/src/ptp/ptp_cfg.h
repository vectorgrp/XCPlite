/* ptp_cfg.h */

// #define PTP_INTERFACE "eth0" // Network interface for PTP hardware timestamping if auto detection does not work
#define PTP_INTERFACE "eno1" // Network interface for PTP hardware timestamping if auto detection does not work
// #define PTP_INTERFACE NULL

#define gPtpDebugLevel 4

#define OPTION_ENABLE_PTP_OBSERVER
#define MASTER_DRIFT_FILTER_SIZE 32
#define MASTER_JITTER_RMS_FILTER_SIZE 32
#define MASTER_JITTER_AVG_FILTER_SIZE 32

// #define OPTION_ENABLE_PTP_MASTER

// #define OPTION_ENABLE_PTP_CLOCK

// #define OPTION_ENABLE_PTP_CLIENT

#define OPTION_ENABLE_PTP_XCP
