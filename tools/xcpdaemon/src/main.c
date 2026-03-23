// xcpdaemon - XCP daemon application
// This application serves as a daemon for multi application measurement and calibration use cases
// Just another XCP instrumented application in SHM mode, but it is configured to be the only XCP server
// Creates the master A2L file and manages the binary calibration data persistence file
// Must not be started first
// It has own measurement and calibration objects to monitor the system and multiple XCP /SHM instrumented applications

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "a2l.h"        // for A2l generation
#include "dbg_print.h"  // for DBG_LEVEL, DBG_PRINT, ...
#include "platform.h"   // for platform defines (WIN_, LINUX_, MACOS_)
#include "shm.h"        // for A2L generation
#include "xcp.h"        // for XCP protocol definitions
#include "xcplib.h"     // for application programming interface
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcplite.h"    // for XCP protocol layer interface functions

#ifdef OPTION_SHM_MODE
extern tXcpData *gXcpData;
extern tXcpLocalData gXcpLocalData;
#endif

//-----------------------------------------------------------------------------------------------------

// XCP parameters
#define OPTION_PROJECT_NAME "xcpdaemon"                                             // A2L project name
#define OPTION_PROJECT_EPK "105"                                                    // EPK version string (default, is contructed from the applications version strings)
#define OPTION_USE_TCP false                                                        // TCP or UDP
#define OPTION_SERVER_PORT 5555                                                     // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}                                             // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 32)                                               // Size of the measurement queue in bytes
#define OPTION_XCP_MODE (XCP_MODE_PERSISTENCE | XCP_MODE_SHM | XCP_MODE_SHM_SERVER) // XCP mode
#define OPTION_A2L_MODE (A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS) // A2L generation mode
#define OPTION_LOG_LEVEL 3                                                                          // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

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

    uint32_t delay_ms = 10;
    uint16_t counter = 0;

    // Register measurement variables located on stack
    DaqCreateEvent(xcpdaemon);
    A2lSetStackAddrMode(xcpdaemon);
    A2lCreateMeasurement(counter, "Mainloop counter");
    A2lCreateParameter(delay_ms, "Mainloop delay", "ms", 1, 1000);

    A2lSetAbsoluteAddrMode(xcpdaemon);

#ifdef OPTION_SHM_MODE

    // Local
    A2lCreateMeasurement(gXcpLocalData.daq_start_clock, "DAQ start clock");
    A2lCreateMeasurement(gXcpLocalData.shm_app_id, "Application id");
    A2lCreateMeasurement(gXcpLocalData.init_mode, "Initialization mode");
    // A2lCreateMeasurementString(gXcpLocalData.project_name, "Project name");
    // A2lCreateMeasurementString(gXcpLocalData.epk, "EPK version");

    // Shared
    A2lSetRelativeAddrMode(xcpdaemon, gXcpData);
    A2lTypedefBegin(tXcpData, gXcpData, "XCP shared state typedef");
    A2lTypedefMeasurementComponent(session_status, "XCP session status");
    // A2lTypedefMeasurementComponent(daq_running, "DAQ is running "); @@@@ TODO Does not work for atomics
    A2lTypedefMeasurementComponent(daq_overflow_count, "DAQ overflow count");
    A2lTypedefEnd();
    A2lCreateTypedefReference(gXcpData, tXcpData, "XCP shared state");

    // A2lCreateMeasurement(gXcpData->shm_header.app_count, "Application count");
    // A2lTypedefBegin(tXcpShmApp, &gXcpData->shm_header.app_list, "Calibration parameters typedef");
    // A2lTypedefMeasurementComponent(pid, "Process id ");
    // A2lTypedefMeasurementComponent(is_server, "Is server ");
    // A2lTypedefMeasurementComponent(is_leader, "Is leader ");
    // A2lTypedefEnd();
    // A2lCreateTypedefReference(gXcpData, tXcpShmApp, "Shared memory");

    if (DBG_LEVEL >= 3) {
        // Print current status of the shared memory
        printf("\n--------------------------------------------------------------\n");
        printf("Shared memory status after initialization:\n");
        XcpShmDebugPrint();
        printf("--------------------------------------------------------------\n");
    }
#endif // OPTION_SHM_MODE

    DBG_PRINT3("\nStart XCP daemon, press Ctrl-C to stop...\n");

    while (running) {

        counter++;

        // Check server status
        if (!XcpEthServerStatus()) {
            printf("\nXCP Server failed\n");
            break;
        }

        // Every second
        if (counter % (1000 / delay_ms) == 0) {
        }

#ifdef OPTION_SHM_MODE
        DaqTriggerEventExt(xcpdaemon, gXcpData);
#endif // OPTION_SHM_MODE

        // Sleep for the specified delay parameter in milliseconds
        sleepMs(delay_ms);

    } // while (running)

    DBG_PRINT3("\nStop XCP daemon\n");

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
    return 0;
}
