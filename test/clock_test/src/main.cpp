// clock_test
// For testing time synchronization and clock behavior
// See README.md for details

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

#include "dbg_print.h" // for DBG_PRINT_ERROR, DBG_PRINTF_WARNING, ...
#include "platform.h"  // for clockGetRealtimeNs, clockGetMonotonicNs
#include "util.h"      // for syncInit, syncUpdate, syncInterpolateT1

//-----------------------------------------------------------------------------------------------------
// XCP

// Include XCPlite/libxcplite C++ headers
#include <a2l.hpp>    // for A2l generation application programming interface
#include <xcplib.hpp> // for application programming interface

constexpr const char XCP_OPTION_PROJECT_NAME[] = "clock_test";
constexpr const char XCP_OPTION_PROJECT_VERSION[] = "V200";
constexpr bool XCP_OPTION_USE_TCP = false;
constexpr uint8_t XCP_OPTION_SERVER_ADDR[4] = {0, 0, 0, 0};
constexpr uint16_t XCP_OPTION_SERVER_PORT = 5555;
constexpr size_t XCP_OPTION_QUEUE_SIZE = (1024 * 32);
constexpr int XCP_OPTION_LOG_LEVEL = 4; // Default XCP log level: 0=none, 1=error, 2=warning, 3=info, 4=XCP protocol debug, 5=very verbose

//-----------------------------------------------------------------------------------------------------
// Optional: Use a PTP4L synchronized real-time clock instead of the system monotonic clock

#ifdef OPTION_ENABLE_PTP

uint8_t XCP_GRANDMASTER_UUID[] = {0x68, 0xB9, 0x83, 0xFF, 0xFE, 0x00, 0x8E, 0x9F}; // Grandmaster UUID
uint8_t XCP_CLIENT_UUID[] = {0x68, 0xB9, 0x83, 0xFF, 0xFE, 0x00, 0x8E, 0x9F};      // Local clock UUID

#include <array>
#include <cstdio>

// Helper function to get PTP clock identities using linuxptp pmc command
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

#endif

//-----------------------------------------------------------------------------------------------------
// Adjustable clock parameters

// Clock parameter structure
typedef struct clock_params {
    int32_t drift;       // time drift in ns/s (ppb)
    int32_t drift_drift; // time drift drift in ns/s2
    int32_t offset;      // time offset in ns
    uint32_t jitter;     // time jitter in ns
} tClockParams;

// Default clock parameter values
static tClockParams clock_params = {
    .drift = 0,       // time drift in ns/s (ppb)
    .drift_drift = 0, // time drift drift in ns/s2
    .offset = 0,      // time offset in ns
    .jitter = 0,      // time jitter in ns
};

static tXcpCalSegIndex gClockParameters = XCP_UNDEFINED_CALSEG; // clock parameters calibration segment

//-----------------------------------------------------------------------------------------------------
// Globals

// Test clock synchronizer state
static tClockSynchronizer gClockSynchronizer; // Global clock synchronizer instance

// Test start clock offset
static uint64_t gClockStart = 0; // Start time of the test in system clock nanoseconds

// Last system clock value used by xcpClock()
static uint64_t gXcpSystemClockLast = 0;

// Last raw xcp clock value (without jitter and offset) used by xcpClock()
static uint64_t gXcpRawClockLast = 0;

// Last xcp clock value (with jitter and offset and clamped to avoid declining time) used by xcpClock()
static uint64_t gXcpClockLast = 0;

// Last XCP clock values of the XCP events 'loop' and 'pps'
static uint64_t gXcpClockLastPps = 0; // Last pulse per second event clock value in nanoseconds

// Test clock current drift
static int32_t gClockDriftCurrent = 0; // Current drift of the test clock in ns/s (adjusted by drift_drift)

// User calibration update detection
static int32_t gClockDriftLast = 0;      // Last drift parameter
static int32_t gClockDriftDriftLast = 0; // Last drift drift parameter
static int32_t gClockOffsetLast = 0;     // Last offset parameter
static int32_t gClockJitterLast = 0;     // Last jitter parameter

//-----------------------------------------------------------------------------------------------------
// Reference (original) clock used (maybe system monotonic raw or real-time PTP or NTP disciplined clock)

uint64_t systemClock(void) {

#ifdef OPTION_ENABLE_PTP
    return clockGetRealtimeNs() - gClockStart;
#else
    return clockGetMonotonicNs() - gClockStart;
#endif
}

void systemClockInit(void) {

    gClockStart = 0;
    gClockStart = systemClock();
}

//-----------------------------------------------------------------------------------------------------
// Client clock callbacks for XCP

// Get current client clock value in nanoseconds
// Sets
//   gXcpSystemClockLast to the last system clock value used for interpolation
//   gXcpClockLast to the last xcp clock value (without jitter and offset) used
uint64_t xcpClock(void) {

    tClockParams *params = (tClockParams *)XcpLockCalSeg(gClockParameters);
    uint64_t jitter = fast_rand(params->jitter);
    int64_t offset = params->offset;
    int32_t drift = params->drift;
    int32_t drift_drift = params->drift_drift;
#ifdef OPTION_CAL_RCU_REFCOUNT
    XcpUnlockCalSeg(gClockParameters, params);
#else
    XcpUnlockCalSeg(gClockParameters);
#endif

    uint64_t system_clock = systemClock();
    uint64_t xcp_raw_clock;
    if (gClockSynchronizer.is_sync) {
        xcp_raw_clock = syncInterpolateT1(&gClockSynchronizer, system_clock);
    } else {
        xcp_raw_clock = system_clock + offset;
    }

    gXcpSystemClockLast = system_clock;
    gXcpRawClockLast = xcp_raw_clock;

    uint64_t xcp_clock = (uint64_t)((int64_t)xcp_raw_clock + offset) + jitter;

    // Avoid declining time (jitter is alway positive or zero)
    if (xcp_clock < gXcpClockLast) {
        xcp_clock = gXcpClockLast;
    }

    gXcpClockLast = xcp_clock;
    return xcp_clock;
}

// Get current clock state
// @return CLOCK_STATE_SYNCH, CLOCK_STATE_SYNCH_IN_PROGRESS, CLOCK_STATE_FREE_RUNNING
uint8_t xcpClockState(void) { return CLOCK_STATE_FREE_RUNNING; }

// Get client and grandmaster clock uuid, stratum level and epoch
// @param client_uuid Pointer to 8 byte array to store the client UUID
// @param grandmaster_uuid Pointer to 8 byte array to store the grandmaster UUID
// @param epoch Pointer to store the epoch
// @param stratum Pointer to store the stratum level
// @return true if PTP is available and grandmaster found, must not be sync yet
bool xcpClockInfo(uint8_t *client_uuid, uint8_t *grandmaster_uuid, uint8_t *epoch, uint8_t *stratum) {

#ifdef OPTION_ENABLE_PTP

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

#else
    return false; // PTP not available
#endif
}

// Register PTP client clock callbacks for XCP
void testRegisterClockCallbacks(void) {
    ApplXcpRegisterGetClockCallback(xcpClock);
    ApplXcpRegisterGetClockStateCallback(xcpClockState);
    ApplXcpRegisterGetClockInfoGrandmasterCallback(xcpClockInfo);
}

//-----------------------------------------------------------------------------------------------------
// Demo main

static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

int main(int argc, char *argv[]) {

    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    // Get PTP clock identities
#ifdef OPTION_ENABLE_PTP
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
    if (!XcpInit(XCP_OPTION_PROJECT_NAME, XCP_OPTION_PROJECT_VERSION, XCP_MODE_LOCAL)) {
        return 1;
    }

    // Initialize the test clock synchronizer, using the system monotonic clock as reference and with no initial offset
    {
        systemClockInit();
        syncInit(&gClockSynchronizer, SYNC_MODE_DEFAULT, 0);
        uint64_t t = systemClock();
        syncSet(&gClockSynchronizer, t, t, clock_params.drift); // Initialize, set to sync
        if (!gClockSynchronizer.is_sync) {
            DBG_PRINT_ERROR("Clock synchronizer not initialized\n");
            return 1;
        }
        gClockDriftCurrent = clock_params.drift;
    }

    // Register XCP clock callbacks, to provide the test clock as application defined timestamps and information about clock state and identity
    testRegisterClockCallbacks();

    // Create XCP on Ethernet server
    if (!XcpEthServerInit(XCP_OPTION_SERVER_ADDR, XCP_OPTION_SERVER_PORT, XCP_OPTION_USE_TCP, XCP_OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Initialize A2L generation, no parameter persistence
    if (!A2lInit(XCP_OPTION_SERVER_ADDR, XCP_OPTION_SERVER_PORT, XCP_OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    // Create XCP calibration parameter segment for the adjustable clock parameters
    gClockParameters = XcpCreateCalSeg("clock_params", &clock_params, sizeof(clock_params));
    assert(gClockParameters != XCP_UNDEFINED_CALSEG);
    A2lSetSegmentAddrMode(gClockParameters, clock_params);
    A2lCreateParameter(clock_params.drift, "Master time drift (ns/s)", "", -100000, +100000);
    A2lCreateParameter(clock_params.drift_drift, "Master time drift drift (ns/s2)", "", -1000, +1000);
    A2lCreateParameter(clock_params.jitter, "Master time jitter (ns)", "", 0, 1000000);
    A2lCreateParameter(clock_params.offset, "Master time offset (ns)", "", -1000000000, +1000000000);
    gClockDriftDriftLast = clock_params.drift_drift;
    gClockDriftLast = clock_params.drift;
    gClockOffsetLast = clock_params.offset;
    gClockJitterLast = clock_params.jitter;

    uint8_t counter{0};       // Measurement value: loop counter
    uint64_t system_clock{0}; // Measurement value: current normalized system wall clock
    uint64_t xcp_clock{0};    // Measurement value: current XCP event timestamp
    uint8_t xcp_clock_pps{0}; // Measurement value: pulse per second event state

    xcp_clock = xcpClock();
    printf("Start loop, initial XCP clock = %" PRIu64 " ns", xcp_clock);

    while (running) {

        // Measure a counter and the current event clock values from the system clock and the XCP clock
        counter++;
        xcp_clock = xcpClock();             // Simulate a test clock (drifting and jittery clock) for XCP
        system_clock = gXcpSystemClockLast; // The system clock used by the last call to xcpClock()

        DaqEventAtVar(loop, xcp_clock,                                                                 //
                      A2L_MEAS(counter, "Main loop counter"),                                          //
                      A2L_MEAS(system_clock, "Current event timestamp value from system clock in ns"), //
                      A2L_MEAS(xcp_clock, "Current event timestamp value from XCP clock in ns"));

        // PPS event exactly every 1s in test time
        // Simulate a pulse per second event signal with a pulse width of exactly 100ms in test time scale
        if ((xcp_clock - gXcpClockLastPps) > 1000000000) { // Every second in simulated time

            xcp_clock_pps = 1;
            uint64_t t = (xcp_clock / 1000000000) * 1000000000;                       // Round down to last second in XCP time
            DaqEventAtVar(pps1, t, A2L_MEAS(xcp_clock_pps, "100 ms long PPS pulse")); // Start of the pulse
            xcp_clock_pps = 0;
            DaqEventAtVar(pps2, t + 100000000, A2L_MEAS(xcp_clock_pps, "100 ms long PPS pulse")); // End of the pulse (+100ms)

            printf("PPS event at xcp_clock = %" PRIu64 " ns\n", t);
            gXcpClockLastPps = xcp_clock;
        }

        // Approximately every 100ms in system time
        if (counter % 10 == 0) {
            printf("system_clock = %" PRIu64 " ns, xcp_clock = %" PRIu64 " ns, diff to system_clock = %" PRIi64 " ns\n", system_clock, xcp_clock,
                   (int64_t)(xcp_clock - system_clock));
        }

        // Approximately every second in system time
        if (counter % 100 == 0) {

            tClockParams *params = (tClockParams *)XcpLockCalSeg(gClockParameters);

            // Accumulate drift_drift
            gClockDriftCurrent += params->drift_drift;

            // Calibration parameter change detection
            // Parameter change detection
            if (gClockDriftLast != params->drift                  //
                || gClockOffsetLast != params->offset             //
                || gClockJitterLast != params->jitter             //
                || gClockDriftDriftLast != params->drift_drift) { //

                printf("User update: offset = %d ns, drift = %d ns/s\n", params->offset, params->drift);
                gClockDriftCurrent = params->drift; // Reset (Thats for integrating the drift_drift into the current drift value)

                gClockDriftLast = params->drift;
                gClockDriftDriftLast = params->drift_drift;
                gClockOffsetLast = params->offset;
                gClockJitterLast = params->jitter;
            }

#ifdef OPTION_CAL_RCU_REFCOUNT
            XcpUnlockCalSeg(gClockParameters, params);
#else
            XcpUnlockCalSeg(gClockParameters);
#endif

            // Update the clock synchronizers anchor (just use the last pair it generated in this loop)
            // Must be done regularly to avoid integer overflows in the interpolation function
            syncSet(&gClockSynchronizer, gXcpRawClockLast /* t1 */, gXcpSystemClockLast /* t2 */, gClockDriftCurrent);
            printf("Sync update\n");

        } // every 1s

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } // while running

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
    return 0;
}
