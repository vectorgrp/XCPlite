// ptp_demo xcplib example

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

#include "ptp/ptp_cfg.h" // for OPTION_ENABLE_PTP_MASTER, OPTION_ENABLE_PTP_OBSERVER
#ifdef OPTION_ENABLE_PTP_MASTER
#include "ptp/ptpMaster.h"
#endif
#ifdef OPTION_ENABLE_PTP_OBSERVER
#include "ptp/ptpObserver.h"
#endif

//-----------------------------------------------------------------------------------------------------
// XCP params

#define OPTION_PROJECT_NAME "ptp_demo"  // Project name, used to build the A2L and BIN file name
#define OPTION_PROJECT_EPK __TIME__     // EPK version string
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 16     // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 4              // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

// PTP observer and master log level set in ptp_cfg.h

//-----------------------------------------------------------------------------------------------------
// Demo main

static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

int main(void) {

    printf("\nXCP on Ethernet ptp_demo C xcplib demo\n");
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

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

// Start a PTP master
#ifdef OPTION_ENABLE_PTP_MASTER
    printf("Starting PTP master...\n");
    const uint8_t *uuid = (const uint8_t[]){0x00, 0x1A, 0xB6, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t domain = 0;
    uint8_t bindAddr[4] = {192, 168, 0, 206};
    if (!ptpMasterInit(uuid, domain, bindAddr)) {
        printf("Failed to start PTP master\n");
        return 1;
    }
#endif

// Start a PTP observer
#ifdef OPTION_ENABLE_PTP_OBSERVER
    printf("Starting PTP observer...\n");
    uint8_t obs_domain = 0;
    uint8_t obs_bindAddr[4] = {0, 0, 0, 0};
    if (!ptpObserverInit(obs_domain, obs_bindAddr)) {
        printf("Failed to start PTP observer\n");
        return 1;
    }
#endif

    // Mainloop
    printf("Start main loop...\n");
    while (running) {
        ptpObserverLoop();
        sleepMs(1000); // 1s
        A2lFinalize(); // @@@@ TEST: Manually finalize the A2L file to make it visible without XCP tool connect
    } // for (;;)

#ifdef OPTION_ENABLE_PTP_MASTER
    ptpMasterShutdown();
#endif
#ifdef OPTION_ENABLE_PTP_OBSERVER
    ptpObserverShutdown();
#endif

    XcpDisconnect();
    XcpEthServerShutdown();
    return 0;
}
