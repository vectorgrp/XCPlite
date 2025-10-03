// no_a2l_demo xcplib example
// Demonstrates XCPlite usage without runtime A2L generation
// Requires manual or tool based A2L file creation and update
// Limited to parameters and measurements in addressable (4 GB - 32bit) global memory

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "xcplib.h" // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP params

#define OPTION_PROJECT_NAME "no_a2l_demo" // Project name, used to build the A2L and BIN file name
#define OPTION_USE_TCP true               // TCP or UDP
#define OPTION_SERVER_PORT 5555           // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}   // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 16       // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3                // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

// Calibration parameters structure
struct params {
    uint16_t counter_max; // Maximum value for the counter
    uint32_t delay_us;    // Sleep time in microseconds for the main loop
};

// Default values (reference page, "FLASH") for the calibration parameters
const struct params params = {.counter_max = 1024, .delay_us = 1000};

// A global calibration segment handle for the calibration parameters
// A calibration segment has a working page ("RAM") and a reference page ("FLASH"), it is described by a MEMORY_SEGMENT in the A2L file
// Using the calibration segment to access parameters assures safe (thread safe against XCP modifications), wait-free and consistent access
// It supports RAM/FLASH page switching, reinitialization (copy FLASH to RAM page) and persistence (save RAM page to BIN file)
tXcpCalSegIndex calseg = XCP_UNDEFINED_CALSEG;

//-----------------------------------------------------------------------------------------------------
// Demo global measurement values

uint16_t counter = 0;

//-----------------------------------------------------------------------------------------------------
// Demo main

static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

int main(void) {

    printf("\nXCP on Ethernet no_a2l_demo C xcplib demo\n");
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("A2L base address = %p:\n", ApplXcpGetBaseAddr());
    printf("&counter = %p, A2L-addr = 0x%08X\n", &counter, ApplXcpGetAddr((void *)&counter));
    printf("&params = %p, A2L-addr = 0x%08X, size = %u\n", &params, ApplXcpGetAddr((void *)&params), (uint32_t)sizeof(params));

    // XCP: Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // XCP: Initialize the XCP singleton, activate XCP, must be called before starting the server
    //      If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    XcpInit(true);
    ApplXcpSetA2lName(OPTION_PROJECT_NAME); // @@@@ This is still required to enable GET_ID for XCP_IDT_ASCII

    // XCP: Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // XCP: Create a calibration segment named 'params' for the calibration parameters in 'const struct params params' as reference page
    calseg = XcpCreateCalSeg("params", &params, sizeof(params));

    // XCP: Create a measurement event named "mainloop"
    DaqCreateEvent(mainloop);

    // Mainloop
    printf("Start main loop...\n");
    uint32_t delay_us;
    while (running) {
        // XCP: Lock the calibration parameter segment for consistent and safe access
        //      Calibration segment locking is wait-free, locks may be recursive, calibration segments may be shared among multiple threads
        //      Returns a pointer to the active page (working or reference) of the calibration segment
        {
            struct params *params = (struct params *)XcpLockCalSeg(calseg);

            delay_us = params->delay_us; // Get the delay_us calibration value

            counter++;
            if (counter > params->counter_max) { // Get the counter_max calibration value and reset counter
                counter = 0;
            }

            // XCP: Unlock the calibration segment
            XcpUnlockCalSeg(calseg);
        }

        // XCP: Trigger the measurement event "mainloop"
        DaqEvent(mainloop);

        // Sleep for the specified delay parameter in microseconds, don't sleep with the XCP lock held to give the XCP client a chance to update params
        sleepUs(delay_us);

    } // for (;;)

    // XCP: Force disconnect the XCP client
    XcpDisconnect();

    // XCP: Stop the XCP server
    XcpEthServerShutdown();

    return 0;
}
