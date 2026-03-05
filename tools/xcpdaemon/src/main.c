// xcpdaemon

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include <a2l.h>    // for A2l generation
#include <xcplib.h> // for application programming interface

//-----------------------------------------------------------------------------------------------------

// XCP parameters
#define OPTION_PROJECT_NAME "xcpdaemon"   // A2L project name
#define OPTION_PROJECT_EPK "V1_" __TIME__ // EPK version string
#define OPTION_USE_TCP false              // TCP or UDP
#define OPTION_SERVER_PORT 5555           // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}   // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 32)     // Size of the measurement queue in bytes
#define OPTION_LOG_LEVEL 6                // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------

// Signal handler for clean shutdown
static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

// Demo main
int main(void) {

    printf("\nXCP daemon\n");
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, activate SHM XCP server
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_EPK, XCP_MODE_SHM_SERVER);

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Enable A2L generation and prepare the A2L file, finalize the A2L file on XCP connect
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    // Variables on stack
    uint16_t counter = 0;

    // Register measurement variables located on stack
    DaqCreateEvent(daemon);
    A2lSetStackAddrMode(daemon);
    A2lCreateMeasurement(counter, "Mainloop counter");

#ifdef OPTION_SHM_MODE
    // Print current status of the shared memory
    printf("\n--------------------------------------------------------------\n");
    printf("Shared memory status after initialization:\n");
    struct tXcpData;
    extern struct tXcpData *gXcpData;
    extern void XcpShmDebugPrint(struct tXcpData * xcp_data);
    XcpShmDebugPrint(gXcpData);
    printf("--------------------------------------------------------------\n");
#endif

    uint32_t delay_us = 1000;
    while (running) {

        counter++;

        // Trigger the measurement event for globals, local variables on stack, and event synchronized calibration access without using a calibration segment
        DaqTriggerEvent(daemon);

        // Check server status
        if (!XcpEthServerStatus()) {
            printf("\nXCP Server failed\n");
            break;
        }

        // Sleep for the specified delay parameter in microseconds
        sleepUs(delay_us);

    } // while (running)

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
    return 0;
}
