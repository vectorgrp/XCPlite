// passive_test - verifies that XCPlite/libxcplite is fully passive when initialized with XCP_MODE_DEACTIVATE
//
// Goal:
//   When XcpInit() is called with XCP_MODE_DEACTIVATE, the whole library must behave passively:
//     - XCP is not activated, no server, no A2L file, no DAQ, no calibration segments
//     - Instrumentation macros (events, calibration lock/unlock) are no-ops with minimal overhead
//     - Calibration segment access returns the default (reference) page unlocked
//     - Nothing asserts and the log output stays clean
//
// This is the C version, see main.cpp for the C++ version.

#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf, fopen
#include <string.h>  // for strcmp

#include <a2l.h>    // for A2l generation
#include <xcplib.h> // for application programming interface

#include "xcplite.h" // for XcpGetInitMode / XcpIsActivated internal declarations

#include "../../support/check.h" // for the shared CHECK macro

//-----------------------------------------------------------------------------------------------------
// Test options

#define OPTION_PROJECT_NAME "passive_test"
#define OPTION_PROJECT_VERSION "V1.0.0"
#define OPTION_USE_TCP false
#define OPTION_SERVER_PORT 5555
#define OPTION_SERVER_ADDR {0, 0, 0, 0}
#define OPTION_QUEUE_SIZE (1024 * 32)
#define OPTION_A2L_MODE (A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)
#define OPTION_LOG_LEVEL 3 // 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = print XCP commands
#define A2L_FILE_NAME OPTION_PROJECT_NAME ".a2l"

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters (same layout as hello_xcp)

typedef struct params {
    uint32_t delay_us;    // Mainloop delay time in us
    uint16_t counter_max; // Maximum value for the counter
    float flow_rate;      // Flow rate in m3/h
} params_t;

const params_t params = {.delay_us = 1000, .counter_max = 1024, .flow_rate = 0.301f};

//-----------------------------------------------------------------------------------------------------
// Demo global and local measurement values

uint32_t global_counter = 0;
uint8_t outside_temperature = -5 + 55;
uint8_t inside_temperature = 20 + 55;
double heat_energy = 0.0;

int main(int argc, char *argv[]) {

    (void)argc;
    printf("\nXCP passive mode (XCP_MODE_DEACTIVATE) C test - %s\n\n", argv[0]);

    // Make sure a stale A2L file from a previous (active) run does not create a false positive
    remove(A2L_FILE_NAME);

    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // XCP: Initialize the XCP singleton in passive mode - init succeeds but XCP stays deactivated
    printf("XcpInit(XCP_MODE_DEACTIVATE):\n");
    bool init_ok = XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_DEACTIVATE);
    CHECK(init_ok, "XcpInit() returns true in passive mode");
    CHECK(!XcpIsActivated(), "XcpIsActivated() is false");
    CHECK(XcpGetInitMode() == XCP_MODE_DEACTIVATE, "XcpGetInitMode() == XCP_MODE_DEACTIVATE");

    XcpSetElfName(argv[0]); // Passive: must not crash or start anything

    // XCP: Server init must be a passive no-op and report not running
    printf("XcpEthServerInit():\n");
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    bool srv_ok = XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE);
    CHECK(srv_ok, "XcpEthServerInit() returns true (passive)");
    // Passive contract: XcpEthServerStatus() reports true when deactivated, so a while(XcpEthServerStatus()) mainloop keeps running
    CHECK(XcpEthServerStatus(), "XcpEthServerStatus() returns true (passive, keeps mainloop alive)");

    // XCP: A2L generation must be a passive no-op
    printf("A2lInit():\n");
    bool a2l_ok = A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_A2L_MODE);
    CHECK(a2l_ok, "A2lInit() returns true (passive)");

    // XCP: Calibration segment creation is passive, CalSegLock returns the default page
    printf("Calibration segment:\n");
    CalSegCreate(params);
    CHECK(calseg_id_params == XCP_UNDEFINED_CALSEG, "CalSegCreate() does not create a segment (index stays undefined)");

    const params_t *p = CalSegLock(params);
    CHECK(p == &params, "CalSegLock() returns the default (reference) page pointer");
    CHECK(p->delay_us == params.delay_us && p->counter_max == params.counter_max && p->flow_rate == params.flow_rate, "Default page holds the initial default values");
    CalSegUnlock(params);

    // XCP: Run a few mainloop iterations with event triggering - all must be passive and must not assert
    printf("Measurement events:\n");
    for (int i = 0; i < 100; i++) {
        const params_t *pp = CalSegLock(params);
        global_counter++;
        inside_temperature = 20 + 55;
        outside_temperature = -5 + 55;
        heat_energy += 0.001;
        uint32_t delay_us = pp->delay_us;
        (void)delay_us;
        CalSegUnlock(params);

        // Combined create/register/trigger event - passive when XCP is deactivated
        DaqEventVar(mainloop,                                                //
                    A2L_MEAS(global_counter, "Global free running counter"), //
                    A2L_MEAS(outside_temperature, "Outside temperature"),    //
                    A2L_MEAS(inside_temperature, "Inside temperature"),      //
                    A2L_MEAS(heat_energy, "Accumulated heat energy in kWh"));
    }
    CHECK(global_counter == 100, "Mainloop ran 100 iterations without asserting");
    CHECK(!XcpIsConnected(), "XcpIsConnected() is false");
    CHECK(!XcpIsDaqRunning(), "XcpIsDaqRunning() is false");

    // XCP: Shutdown path must be passive too
    printf("Shutdown:\n");
    XcpDisconnect();
    A2lFinalize();
    // Passive contract: XcpEthServerShutdown() returns false when deactivated (nothing was started)
    bool shutdown_ok = XcpEthServerShutdown();
    CHECK(!shutdown_ok, "XcpEthServerShutdown() returns false (passive, nothing to shut down)");

    // Verify no A2L file was generated in passive mode
    FILE *f = fopen(A2L_FILE_NAME, "r");
    CHECK(f == NULL, "No A2L file was generated (" A2L_FILE_NAME " does not exist)");
    if (f != NULL) {
        fclose(f);
    }

    printf("\n%s: PASSED\n", OPTION_PROJECT_NAME);
    return 0;
}
