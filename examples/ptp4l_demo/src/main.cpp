// ptp4l_demo XCPlite/libxcplite example (C++ version)
// Demonstrates how to use a PTP (from linuxptp/ptp4l) synchronized local clock for XCP data acquisition timestamping

#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "platform.h" // for clockGetRealtimeNs, clockGetMonotonicNs

//-----------------------------------------------------------------------------------------------------
// XCP

// Include XCPlite/libxcplite C++ headers
#include <a2l.hpp>    // for A2l generation application programming interface
#include <xcplib.hpp> // for application programming interface

constexpr const char XCP_OPTION_PROJECT_NAME[] = "ptp4l_demo";
constexpr const char XCP_OPTION_PROJECT_VERSION[] = "V1.2.0";
constexpr bool XCP_OPTION_USE_TCP = false;
constexpr uint8_t XCP_OPTION_SERVER_ADDR[4] = {0, 0, 0, 0};
constexpr uint16_t XCP_OPTION_SERVER_PORT = 5555;
constexpr size_t XCP_OPTION_QUEUE_SIZE = (1024 * 32);
constexpr int XCP_OPTION_LOG_LEVEL = 3; // Default XCP log level: 0=none, 1=error, 2=warning, 3=info, 4=XCP protocol debug, 5=very verbose

uint8_t XCP_GRANDMASTER_UUID[] = {0x68, 0xB9, 0x83, 0xFF, 0xFE, 0x00, 0x8E, 0x9F}; // Grandmaster UUID
uint8_t XCP_CLIENT_UUID[] = {0x68, 0xB9, 0x83, 0xFF, 0xFE, 0x00, 0x8E, 0x9F};      // Local clock UUID

//-----------------------------------------------------------------------------------------------------
// Helper function to get PTP clock identities using linuxptp pmc command

#include <array>
#include <cstdio>

bool getPtp4lClockInfo(uint8_t *local_uuid, uint8_t *grandmaster_uuid) {
    FILE *fp;
    char buffer[256];
    bool found_local = false;
    bool found_grandmaster = false;

    // Get local clock identity
    fp = popen("sudo pmc -u -b 0 'GET DEFAULT_DATA_SET' 2>/dev/null | grep clockIdentity", "r");
    if (fp) {
        if (fgets(buffer, sizeof(buffer), fp) != nullptr) {
            unsigned int bytes[3];
            if (sscanf(buffer, " clockIdentity %X.%X.%X", &bytes[0], &bytes[1], &bytes[2]) == 3) {
                // printf("clockIdentity: %X.%X.X\n", bytes[0], bytes[1], bytes[2]);
                if (bytes[1] == 0xFFFE) {
                    local_uuid[2] = (uint8_t)bytes[0];
                    local_uuid[1] = (uint8_t)(bytes[0] >> 8);
                    local_uuid[0] = (uint8_t)(bytes[0] >> 16);
                    local_uuid[3] = 0xFF;
                    local_uuid[4] = 0xFE;
                    local_uuid[7] = (uint8_t)bytes[2];
                    local_uuid[6] = (uint8_t)(bytes[2] >> 8);
                    local_uuid[5] = (uint8_t)(bytes[2] >> 16);
                    found_local = true;
                }
            }
        }
        pclose(fp);
    }

    // Get grandmaster clock identity
    fp = popen("sudo pmc -u -b 0 'GET PARENT_DATA_SET' 2>/dev/null | grep grandmasterIdentity", "r");
    if (fp) {
        if (fgets(buffer, sizeof(buffer), fp) != nullptr) {
            unsigned int bytes[3];
            if (sscanf(buffer, " grandmasterIdentity %X.%X.%X", &bytes[0], &bytes[1], &bytes[2]) == 3) {
                // printf("grandmasterIdentity: %X.%X.%X\n", bytes[0], bytes[1], bytes[2]);
                if (bytes[1] == 0xFFFE) {
                    grandmaster_uuid[2] = (uint8_t)bytes[0];
                    grandmaster_uuid[1] = (uint8_t)(bytes[0] >> 8);
                    grandmaster_uuid[0] = (uint8_t)(bytes[0] >> 16);
                    grandmaster_uuid[3] = 0xFF;
                    grandmaster_uuid[4] = 0xFE;
                    grandmaster_uuid[7] = (uint8_t)bytes[2];
                    grandmaster_uuid[6] = (uint8_t)(bytes[2] >> 8);
                    grandmaster_uuid[5] = (uint8_t)(bytes[2] >> 16);
                    found_grandmaster = true;
                }
            }
            pclose(fp);
        }
    }

    return (found_local && found_grandmaster);
}

//-----------------------------------------------------------------------------------------------------
// PTP client clock callbacks for XCP

// Get current clock value in nanoseconds
// Return value is assumed to be from a PTP synchronized clock with ns resolution and PTP or ARB epoch */
uint64_t ptpClientGetClock(void) {

    // Return the system realtime clock, which may be synchronized to PTP grandmaster
    // by running a PTP4L and PHC2SYS on Linux
    return clockGetRealtimeNs();
}

// Get current clock state
// @return CLOCK_STATE_SYNCH, CLOCK_STATE_SYNCH_IN_PROGRESS, CLOCK_STATE_FREE_RUNNING
uint8_t ptpClientGetClockState(void) {

    /* Possible return values:
        CLOCK_STATE_SYNCH, CLOCK_STATE_SYNCH_IN_PROGRESS, CLOCK_STATE_FREE_RUNNING
    */

    return CLOCK_STATE_SYNCH; // Clock is assumed to be synchronized, add other means to check synchronization if needed
}

// Get client and grandmaster clock uuid, stratum level and epoch
// @param client_uuid Pointer to 8 byte array to store the client UUID
// @param grandmaster_uuid Pointer to 8 byte array to store the grandmaster UUID
// @param epoch Pointer to store the epoch
// @param stratum Pointer to store the stratum level
// @return true if PTP is available and grandmaster found, must not be sync yet
bool ptpClientGetClockInfo(uint8_t *client_uuid, uint8_t *grandmaster_uuid, uint8_t *epoch, uint8_t *stratum) {

    /*
      Possible return values:
        stratum: XCP_STRATUM_LEVEL_UNKNOWN, XCP_STRATUM_LEVEL_RTC,XCP_STRATUM_LEVEL_GPS
        epoch: XCP_EPOCH_TAI, XCP_EPOCH_UTC, XCP_EPOCH_ARB
    */

    if (client_uuid != NULL)
        memcpy(client_uuid, XCP_CLIENT_UUID, 8);
    if (grandmaster_uuid != NULL)
        memcpy(grandmaster_uuid, XCP_GRANDMASTER_UUID, 8);
    if (epoch != NULL)
        *epoch = CLOCK_EPOCH_TAI;
    if (stratum != NULL)
        *stratum = CLOCK_STRATUM_LEVEL_UNKNOWN;
    return true;
}

// Register PTP client clock callbacks for XCP
void ptpClientRegisterClockCallbacks(void) {
    ApplXcpRegisterGetClockCallback(ptpClientGetClock);
    ApplXcpRegisterGetClockStateCallback(ptpClientGetClockState);
    ApplXcpRegisterGetClockInfoGrandmasterCallback(ptpClientGetClockInfo);
}

//-----------------------------------------------------------------------------------------------------
// Demo main

static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

int main(int argc, char *argv[]) {

    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    // Get PTP clock identities
#ifdef _LINUX
    if (!getPtp4lClockInfo(XCP_CLIENT_UUID, XCP_GRANDMASTER_UUID)) {
        std::cerr << "Failed to get PTP clock identities from pmc command" << std::endl;
        return 1;
    }
    printf("Using PTP client clock identity: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n", XCP_CLIENT_UUID[0], XCP_CLIENT_UUID[1], XCP_CLIENT_UUID[2], XCP_CLIENT_UUID[3],
           XCP_CLIENT_UUID[4], XCP_CLIENT_UUID[5], XCP_CLIENT_UUID[6], XCP_CLIENT_UUID[7]);
    printf("Using PTP grandmaster clock identity: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n", XCP_GRANDMASTER_UUID[0], XCP_GRANDMASTER_UUID[1], XCP_GRANDMASTER_UUID[2],
           XCP_GRANDMASTER_UUID[3], XCP_GRANDMASTER_UUID[4], XCP_GRANDMASTER_UUID[5], XCP_GRANDMASTER_UUID[6], XCP_GRANDMASTER_UUID[7]);
#endif

    // Initialize XCP
    XcpSetLogLevel(XCP_OPTION_LOG_LEVEL);
    XcpInit(XCP_OPTION_PROJECT_NAME, XCP_OPTION_PROJECT_VERSION, XCP_MODE_LOCAL);

    // Enable PTP in XCP
    // Register XCP clock callbacks, to provide PTP synchronized timestamps and information about clock state and identity
    ptpClientRegisterClockCallbacks();

    // Create XCP on Ethernet server
    if (!XcpEthServerInit(XCP_OPTION_SERVER_ADDR, XCP_OPTION_SERVER_PORT, XCP_OPTION_USE_TCP, XCP_OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Initialize A2L generation, no parameter persistence
    if (!A2lInit(XCP_OPTION_SERVER_ADDR, XCP_OPTION_SERVER_PORT, XCP_OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    std::cout << "Start main task ..." << std::endl;
    uint8_t counter{0};
    while (running) {

        counter++;

        DaqEventVar(mainloop, A2L_MEAS(counter, "Local counter variable"));

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    } // while running

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
    return 0;
}
