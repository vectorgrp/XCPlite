// ptp_demo xcplib example

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP params

#define OPTION_PROJECT_NAME "ptp_demo"  // Project name, used to build the A2L and BIN file name
#define OPTION_PROJECT_EPK __TIME__     // EPK version string
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 16     // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3              // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

// New option in V1.1: Enable variadic all in one macros for simple arithmetic types, see examples below
#define OPTION_USE_VARIADIC_MACROS

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

// Calibration parameters structure
typedef struct params {
    uint32_t delay_us; // Sleep time in microseconds for the main loop
} parameters_t;

// Default values (reference page, "FLASH") for the calibration parameters
const parameters_t params = {.delay_us = 1000};

// A global calibration segment handle for the calibration parameters
// A calibration segment has a working page ("RAM") and a reference page ("FLASH"), it is described by a MEMORY_SEGMENT in the A2L file
// Using the calibration segment to access parameters assures safe (thread safe against XCP modifications), wait-free and consistent access
// It supports RAM/FLASH page switching, reinitialization (copy FLASH to RAM page) and persistence (save RAM page to BIN file)
tXcpCalSegIndex calseg = XCP_UNDEFINED_CALSEG;

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

    // XCP: Create a calibration segment named 'Parameters' for the calibration parameter struct instance 'params' as reference page
    calseg = XcpCreateCalSeg("params", &params, sizeof(params));

    // XCP: Option1: Register the individual calibration parameters in the calibration segment
    A2lSetSegmentAddrMode(calseg, params);
    A2lCreateParameter(params.delay_us, "Mainloop delay time in us", "us", 0, 999999);

    uint16_t counter = 0;

    // Mainloop
    printf("Start main loop...\n");
    while (running) {

        counter++;

        DaqEventVar(mainloop, A2L_MEAS(counter, "Mainloop counter"));

        const parameters_t *params = (parameters_t *)XcpLockCalSeg(calseg);
        uint32_t delay_us = params->delay_us; // Get the delay calibration parameter in microseconds
        XcpUnlockCalSeg(calseg);
        sleepUs(delay_us);

        A2lFinalize(); // @@@@ TEST: Manually finalize the A2L file to make it visible without XCP tool connect

    } // for (;;)

    XcpDisconnect();
    XcpEthServerShutdown();
    return 0;
}
