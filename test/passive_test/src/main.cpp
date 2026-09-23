// passive_test_cpp - verifies that XCPlite/libxcplite is fully passive when initialized with XCP_MODE_DEACTIVATE
//
// Goal:
//   When XcpInit() is called with XCP_MODE_DEACTIVATE, the whole library must behave passively:
//     - XCP is not activated, no server, no A2L file, no DAQ, no calibration segments
//     - Instrumentation (events, xcp::CalSeg lock guard) are no-ops with minimal overhead
//     - xcp::CalSeg<T>::lock() returns the default (reference) page unlocked
//     - Nothing asserts and the log output stays clean
//
// This is the C++ version, see main.c for the C version.

#include <cstdint>  // for uintxx_t
#include <iostream> // for std::cout
#include <optional> // for std::optional

#include <a2l.hpp>    // for A2l generation application programming interface
#include <xcplib.hpp> // for application programming interface

#include "../../support/check.h" // for the shared CHECK macro

//-----------------------------------------------------------------------------------------------------
// Test options

constexpr const char OPTION_PROJECT_NAME[] = "passive_test_cpp";
constexpr const char OPTION_PROJECT_VERSION[] = "V1.0.0";
constexpr bool OPTION_USE_TCP = false;
constexpr uint8_t OPTION_SERVER_ADDR[] = {0, 0, 0, 0};
constexpr uint16_t OPTION_SERVER_PORT = 5555;
constexpr uint32_t OPTION_QUEUE_SIZE = (1024 * 32);
constexpr uint8_t OPTION_A2L_MODE = (A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS);
constexpr int OPTION_LOG_LEVEL = 3;
constexpr const char A2L_FILE_NAME[] = "passive_test_cpp.a2l";

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

struct ParametersT {
    uint32_t delay_us;    // Mainloop delay time in us
    uint16_t counter_max; // Maximum value for the counter
    double min;           // Minimum value
    double max;           // Maximum value
};

const ParametersT kParameters = {.delay_us = 1000, .counter_max = 1024, .min = -2.0, .max = +2.0};

std::optional<xcp::CalSeg<ParametersT>> gCalSeg;

//-----------------------------------------------------------------------------------------------------
// Demo measurement values

uint16_t global_counter{0};

int main(int argc, char *argv[]) {

    (void)argc;
    std::cout << "\nXCP passive mode (XCP_MODE_DEACTIVATE) C++ test - " << argv[0] << "\n" << std::endl;

    // Make sure a stale A2L file from a previous (active) run does not create a false positive
    std::remove(A2L_FILE_NAME);

    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton in passive mode - init succeeds but XCP stays deactivated
    std::cout << "XcpInit(XCP_MODE_DEACTIVATE):" << std::endl;
    bool init_ok = XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_DEACTIVATE);
    CHECK(init_ok, "XcpInit() returns true in passive mode");
    CHECK(!XcpIsActivated(), "XcpIsActivated() is false");
    CHECK(XcpGetInitMode() == XCP_MODE_DEACTIVATE, "XcpGetInitMode() == XCP_MODE_DEACTIVATE");

    XcpSetElfName(argv[0]); // Passive: must not crash or start anything

    // Server init must be a passive no-op and report not running
    std::cout << "XcpEthServerInit():" << std::endl;
    bool srv_ok = XcpEthServerInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE);
    CHECK(srv_ok, "XcpEthServerInit() returns true (passive)");
    // Passive contract: XcpEthServerStatus() reports true when deactivated, so a while(XcpEthServerStatus()) mainloop keeps running
    CHECK(XcpEthServerStatus(), "XcpEthServerStatus() returns true (passive, keeps mainloop alive)");

    // A2L generation must be a passive no-op
    std::cout << "A2lInit():" << std::endl;
    bool a2l_ok = A2lInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_A2L_MODE);
    CHECK(a2l_ok, "A2lInit() returns true (passive)");

    // Calibration segment creation is passive, lock() returns the default page
    std::cout << "Calibration segment:" << std::endl;
    gCalSeg.emplace("params", &kParameters);
    CHECK(gCalSeg->getIndex() == XCP_UNDEFINED_CALSEG, "CalSeg constructor does not create a segment (index stays undefined)");

    {
        auto params = gCalSeg->lock();
        CHECK(params.get() == &kParameters, "CalSeg::lock() returns the default (reference) page pointer");
        CHECK(params->delay_us == kParameters.delay_us && params->counter_max == kParameters.counter_max, "Default page holds the initial default values");
    }

    // A2L instance creation on a passive segment must be a no-op
    gCalSeg->CreateA2lTypedefInstance("ParametersT", "Demo calibration parameters (passive)");

    // Run a few mainloop iterations with event triggering - all must be passive and must not assert
    std::cout << "Measurement events:" << std::endl;
    for (int i = 0; i < 100; i++) {
        {
            auto params = gCalSeg->lock();
            global_counter++;
            uint32_t delay_us = params->delay_us;
            (void)delay_us;
        }
        double value = static_cast<double>(i);
        DaqEventVar(mainloop,                                            //
                    A2L_MEAS(global_counter, "Global counter variable"), //
                    A2L_MEAS_PHYS(value, "Loop value", "V", -100.0, 100.0));
    }
    CHECK(global_counter == 100, "Mainloop ran 100 iterations without asserting");
    CHECK(!XcpIsConnected(), "XcpIsConnected() is false");
    CHECK(!XcpIsDaqRunning(), "XcpIsDaqRunning() is false");

    // Shutdown path must be passive too
    std::cout << "Shutdown:" << std::endl;
    XcpDisconnect();
    A2lFinalize();
    // Passive contract: XcpEthServerShutdown() returns false when deactivated (nothing was started)
    bool shutdown_ok = XcpEthServerShutdown();
    CHECK(!shutdown_ok, "XcpEthServerShutdown() returns false (passive, nothing to shut down)");

    // Verify no A2L file was generated in passive mode
    FILE *f = std::fopen(A2L_FILE_NAME, "r");
    if (f != nullptr) {
        std::fclose(f);
    }
    CHECK(f == nullptr, "No A2L file was generated (%s does not exist)", A2L_FILE_NAME);

    std::cout << "\n" << OPTION_PROJECT_NAME << ": PASSED" << std::endl;
    return 0;
}
