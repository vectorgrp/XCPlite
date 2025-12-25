// ptp_demo xcplib example

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "platform.h" // for sleepMs, ...

#include "ptp/ptp.h"

//-----------------------------------------------------------------------------------------------------
// XCP

#define OPTION_ENABLE_XCP

#ifdef OPTION_ENABLE_XCP

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

#define OPTION_PROJECT_NAME "ptp_demo"      // Project name, used to build the A2L and BIN file name
#define OPTION_PROJECT_EPK "V1.2_" __TIME__ // EPK version string
#define OPTION_USE_TCP false                // TCP or UDP
#define OPTION_SERVER_PORT 5555             // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}     // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 16         // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3                  // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

#endif

//-----------------------------------------------------------------------------------------------------
// PTP params

#define PTP_LOG_LEVEL 3

// #define PTP_INTERFACE NULL
// Network interface for PTP hardware timestamping if auto detection (NULL) and bind to ANY is not appropriate
#define PTP_DOMAIN 0

// Bind to INADDR_ANY and use interface name for SO_BINDTODEVICE (recommended for multicast)
#define PTP_ADDRESS {0, 0, 0, 0} // ANY
// #define PTP_INTERFACE "en0" // MacOS
// #define PTP_INTERFACE "eth0" // Pi
// #define PTP_INTERFACE "enp4s0" // VP6450 1G1
#define PTP_INTERFACE "enp2s0f1" // VP6450 10G1
// #define PTP_INTERFACE "eno1" // VP6450 mgmt

//-----------------------------------------------------------------------------------------------------
// Demo main

static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

int main(void) {

    printf("\nPTP observer with XCP demo\n");
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

#ifdef OPTION_ENABLE_XCP
    // XCP: Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_EPK, true /* activate */);
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }
#endif

    // Start the PTP observer
    printf("Starting PTP observer...\n");
    uint8_t ptp_domain = PTP_DOMAIN;
    uint8_t ptp_bindAddr[4] = PTP_ADDRESS;
    char *ptp_interface = PTP_INTERFACE;
    if (!ptpInit(PTP_MODE_OBSERVER, ptp_domain, ptp_bindAddr, ptp_interface, PTP_LOG_LEVEL)) {
        printf("Failed to start PTP observer\n");
        return 1;
    }

#ifdef OPTION_ENABLE_XCP
    A2lFinalize(); // @@@@ TEST: Manually finalize the A2L file to make it visible without XCP tool connect
#endif

    // Mainloop
    printf("Start main loop...\n");
    while (running) {
        ptpBackgroundTask();
        sleepMs(1000); // 1s
    } // for (;;)

    ptpShutdown();

#ifdef OPTION_ENABLE_XCP
    XcpDisconnect();
    XcpEthServerShutdown();
#endif

    return 0;
}
