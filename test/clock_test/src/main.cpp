// clock_test

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

//-----------------------------------------------------------------------------------------------------
// XCP

// Include XCPlite/libxcplite C++ headers
#include <a2l.hpp>    // for A2l generation application programming interface
#include <xcplib.hpp> // for application programming interface

constexpr const char XCP_OPTION_PROJECT_NAME[] = "clock_test";
constexpr const char XCP_OPTION_PROJECT_VERSION[] = "V1.0.3";
constexpr bool XCP_OPTION_USE_TCP = false;
constexpr uint8_t XCP_OPTION_SERVER_ADDR[4] = {0, 0, 0, 0};
constexpr uint16_t XCP_OPTION_SERVER_PORT = 5555;
constexpr size_t XCP_OPTION_QUEUE_SIZE = (1024 * 32);
constexpr int XCP_OPTION_LOG_LEVEL = 3; // Default XCP log level: 0=none, 1=error, 2=warning, 3=info, 4=XCP protocol debug, 5=very verbose

//-----------------------------------------------------------------------------------------------------
// #define OPTION_ENABLE_PTP
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
// Client clock callbacks for XCP

static uint64_t testTimeAdjust(uint64_t originTime);

static uint64_t test_start_time = 0; // Start time of the test in nanoseconds

// Get current clock value in nanoseconds
uint64_t testGetClock(void) {

    uint64_t t = testTimeAdjust(clockGetMonotonicNs() - test_start_time);
    return t;
}

// Get current clock state
// @return CLOCK_STATE_SYNCH, CLOCK_STATE_SYNCH_IN_PROGRESS, CLOCK_STATE_FREE_RUNNING
uint8_t testGetClockState(void) { return CLOCK_STATE_FREE_RUNNING; }

// Get client and grandmaster clock uuid, stratum level and epoch
// @param client_uuid Pointer to 8 byte array to store the client UUID
// @param grandmaster_uuid Pointer to 8 byte array to store the grandmaster UUID
// @param epoch Pointer to store the epoch
// @param stratum Pointer to store the stratum level
// @return true if PTP is available and grandmaster found, must not be sync yet
bool testGetClockInfo(uint8_t *client_uuid, uint8_t *grandmaster_uuid, uint8_t *epoch, uint8_t *stratum) {

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
    ApplXcpRegisterGetClockCallback(testGetClock);
    ApplXcpRegisterGetClockStateCallback(testGetClockState);
    ApplXcpRegisterGetClockInfoGrandmasterCallback(testGetClockInfo);
}

//-----------------------------------------------------------------------------------------------------
// Adjustable clock

// Clock parameter structure
typedef struct clock_params {
    bool enable_test_time_adjustment; //
    int32_t drift;                    // PTP master time drift in ns/s
    int32_t drift_drift;              // PTP master time drift drift in ns/s2
    int32_t offset;                   // PTP master time offset in ns
    int32_t jitter;                   // PTP master time jitter in ns
} tClockParams;

// Default clock parameter values
static tClockParams clock_params = {
    .enable_test_time_adjustment = false,
    .drift = 0,       // PTP master time drift in ns/s
    .drift_drift = 0, // PTP master time drift drift in ns/s2
    .offset = 0,      // PTP master time offset in ns
    .jitter = 0,      // PTP master time jitter in ns
};

tXcpCalSegIndex clock_calseg = XCP_UNDEFINED_CALSEG; // clock parameters calibration segment

//-------------------------------------------------------------------------------------------------------
// Master time drift, drift_drift, jitter and offset calculation

// Test time state
int32_t testTimeDrift = 0;           // Current drift in ns/s
int32_t testTimeCurrentDrift = 0;    // Current drift including drift_drift
int64_t testTimeSyncDriftOffset = 0; // Current offset from drift accumulated on sync: testTime = originTime+testTimeSyncDriftOffset
uint64_t testTimeLast = 0;           // Current test time
uint64_t testTimeLastSync = 0;       // Original time of last sync
MUTEX testTimeMutex;

// Initialize test time parameters
static void testTimeInit(void) {

    testTimeDrift = 0;           // Current drift in ns/s
    testTimeCurrentDrift = 0;    // Current drift including drift_drift
    testTimeSyncDriftOffset = 0; // Current offset: testTime = originTime+testTimeSyncDriftOffset
    testTimeLast = 0;            // Current test time
    testTimeLastSync = 0;        // Original time of last sync
    mutexInit(&testTimeMutex, 0, 1000);

    test_start_time = clockGetMonotonicNs();
}

// Calculate simulated test time from origin time applying drift, drift_drift, offset and jitter
static uint64_t testTimeAdjust(uint64_t originTime) {

    uint64_t t = originTime;

    tClockParams *params = (tClockParams *)XcpLockCalSeg(clock_calseg);

    if (params->enable_test_time_adjustment) {

        assert(t >= testTimeLastSync);

        mutexLock(&testTimeMutex);

        // time since last sync
        uint64_t dt = t - testTimeLastSync;

        //  Apply drift offset
        int64_t drift_offset = (int64_t)((testTimeCurrentDrift * (int64_t)dt) / 1000000000) + testTimeSyncDriftOffset;
        t += drift_offset;

        // Apply jitter
        int64_t jitter_offset = 0;
        if (params->jitter > 0) {
            jitter_offset = (int64_t)(((double)rand() / (double)RAND_MAX) * 2.0 * (double)(params->jitter + 1) - (double)(params->jitter + 1));
            t += jitter_offset;
        }

        // Apply offset
        t += params->offset;

        mutexUnlock(&testTimeMutex);

        // warn if time is non monotonic
        if (t < testTimeLast) {
            DBG_PRINTF_ERROR("testTimeAdjust: Non monotonic time ! (dt=-%" PRIu64 ")\n", testTimeLast - t);
        }

        if (originTime != t) {
            DBG_PRINTF5("testTimeAdjust: originTime=%" PRIu64 " ns, drift_offset=%" PRIi64 " ns, jitter=%" PRIi64 " ns, offset=%d ns => testTime=%" PRIu64 " ns\n", originTime,
                        drift_offset, jitter_offset, params->offset, t);
        }
    }

    XcpUnlockCalSeg(clock_calseg);

    testTimeLast = t;
    return t;
}

// Recalculate test time sync offset and zero test time drift offset
// At drift 100ppm, calculation would overflow after 2,8s
static void testTimeSync(uint64_t originTime) {

    if (originTime < testTimeLastSync)
        return; // Ignore non monotonic time

    tClockParams *params = (tClockParams *)XcpLockCalSeg(clock_calseg);

    // Check if drift parameter has changed since last sync
    assert(params->drift >= -1000000 && params->drift <= +1000000);
    if (params->drift != testTimeDrift) {
        testTimeDrift = testTimeCurrentDrift = params->drift;

        DBG_PRINTF3("testTimeSync: New drift=%d ns/s\n", testTimeDrift);
    }

    mutexLock(&testTimeMutex);

    if (testTimeLastSync > 0) {

        // time since last sync
        uint64_t dt = originTime - testTimeLastSync;
        assert(dt < 2000000000); // Be sure integer calculation does not overflow

        int64_t o = (int64_t)((testTimeCurrentDrift * (int64_t)dt) / 1000000000);
        testTimeSyncDriftOffset += o;
        // printf("sync dt=%" PRIu64 ", driftOffset=%d, timeOffset=%d\n", dt, testTimeDriftOffset, testTimeSyncDriftOffset);

        // Apply drift drift
        testTimeCurrentDrift += (int32_t)((params->drift_drift * (int64_t)dt) / 1000000000);
    }

    testTimeLastSync = originTime;

    if ((testTimeSyncDriftOffset) != 0 || testTimeCurrentDrift != 0) {
        DBG_PRINTF5("testTimeSync: originTime=%" PRIu64 " ns, testTimeSyncDriftOffset=%" PRIi64 " ns, testTimeCurrentDrift=%d ns/s\n", originTime, testTimeSyncDriftOffset,
                    testTimeCurrentDrift);
    }

    XcpUnlockCalSeg(clock_calseg);

    mutexUnlock(&testTimeMutex);
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
    XcpInit(XCP_OPTION_PROJECT_NAME, XCP_OPTION_PROJECT_VERSION, XCP_MODE_LOCAL);

    // Register XCP clock callbacks, to provide application defined timestamps and information about clock state and identity
    testTimeInit();
    testRegisterClockCallbacks();

    // Create XCP on Ethernet server
    if (!XcpEthServerInit(XCP_OPTION_SERVER_ADDR, XCP_OPTION_SERVER_PORT, XCP_OPTION_USE_TCP, XCP_OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Initialize A2L generation, no parameter persistence
    if (!A2lInit(XCP_OPTION_SERVER_ADDR, XCP_OPTION_SERVER_PORT, XCP_OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    // Create XCP calibration parameter segment, if not already existing
    clock_calseg = XcpCreateCalSeg("clock_params", &clock_params, sizeof(clock_params));
    assert(clock_calseg != XCP_UNDEFINED_CALSEG);
    A2lSetSegmentAddrMode(clock_calseg, clock_params);
    A2lCreateParameter(clock_params.enable_test_time_adjustment, "Enable test time adjustment", "", 0, 1);
    A2lCreateParameter(clock_params.drift, "Master time drift (ns/s)", "", -100000, +100000);
    A2lCreateParameter(clock_params.drift_drift, "Master time drift drift (ns/s2)", "", -1000, +1000);
    A2lCreateParameter(clock_params.jitter, "Master time jitter (ns)", "", 0, 1000000);
    A2lCreateParameter(clock_params.offset, "Master time offset (ns)", "", -1000000000, +1000000000);

    std::cout << "Start mainloop ..." << std::endl;
    uint8_t counter{0};
    uint64_t system_clock{0};
    uint64_t xcp_clock{0};
    while (running) {

        // Measure a counter and the clock values
        counter++;
        system_clock = clockGetMonotonicNs() - test_start_time;
        xcp_clock = testTimeAdjust(system_clock);
        DaqEventAtVar(mainloop, xcp_clock,                                                             //
                      A2L_MEAS(counter, "Main loop counter"),                                          //
                      A2L_MEAS(system_clock, "Current event timestamp value from system clock in ns"), //
                      A2L_MEAS(xcp_clock, "Current event timestamp value from XCP clock in ns"));

        // Every second
        if (counter % 100 == 0) {
            testTimeSync(clockGetMonotonicNs() - test_start_time);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } // while running

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
    return 0;
}
