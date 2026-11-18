// cpp_demo XCPlite/libxcplite C++ example

#include <atomic>     // for std::atomic
#include <csignal>    // for signal(), SIGINT, SIGTERM
#include <cstdint>    // for uintxx_t
#include <filesystem> // for std::filesystem::exists
#include <iostream>   // for std::cout
#include <memory>     // for std::unique_ptr
#include <thread>     // for std::thread

// Include XCPlite/libxcplite C++ headers
#include "a2l.hpp"    // for A2l generation application programming interface
#include "xcplib.hpp" // for application programming interface

#include "sig_gen.hpp" // for signal_generator::SignalGenerator

constexpr double kPi = 3.14159265358979323846;
constexpr double k2Pi = (kPi * 2);

// Signal handler for clean shutdown
static volatile bool running = true;
static void sig_handler(int) { running = false; }

// Sleep some time in us
// Not named sleep: on Linux this would conflict with sleep() of the C library
static void sleep_us(uint32_t us) {
    sleepUs(us); // from xcplib
}

//-----------------------------------------------------------------------------------------------------
// XCP

// #define OPTION_OBSERVE_STARTUP // Wait for XCP client to observe initialization (needs deterministic span and context creation!)

constexpr const char *OPTION_PROJECT_NAME = "thread_context_demo";
constexpr const char OPTION_PROJECT_VERSION[] = "V100";
constexpr bool OPTION_USE_TCP = false;
constexpr uint8_t OPTION_SERVER_ADDR[] = {0, 0, 0, 0};
constexpr uint16_t OPTION_SERVER_PORT = 5555;
constexpr size_t OPTION_QUEUE_SIZE = (1024 * 64);
constexpr int OPTION_LOG_LEVEL = 4;
#ifdef OPTION_OBSERVE_STARTUP
constexpr uint8_t OPTION_XCP_MODE = (XCP_MODE_PERSISTENCE | XCP_MODE_LOCAL);
constexpr uint8_t OPTION_A2L_MODE = (A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS);
#else
constexpr uint8_t OPTION_XCP_MODE = (XCP_MODE_LOCAL);
constexpr uint8_t OPTION_A2L_MODE = (A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS);
#endif

void init_xcp() {

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, activate XCP, must be called before starting the server
    // If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    if (!XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION /* EPK version*/, OPTION_XCP_MODE)) {
        std::cerr << "Failed to initialize XCP" << std::endl;
        exit(1);
    }

    // Initialize the XCP Server
    if (!XcpEthServerInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        std::cerr << "Failed to initialize XCP server" << std::endl;
        exit(1);
    }

    // Enable A2L generation
    // Set mode to write once to create stable A2L files, this also enables calibration segment persistence and freeze support
    if (!A2lInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_A2L_MODE)) {
        std::cerr << "Failed to initialize A2L generator" << std::endl;
        exit(1);
    }
}

void shutdown_xcp() {
    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
}

#ifdef OPTION_OBSERVE_STARTUP

// Wait until an XCP client has started the measurement, so the trace contains the startup
// This is possible only when the A2L file of a previous run exists (A2L_MODE_WRITE_ONCE,XCP_MODE_PERSISTENCE): The events of the contexts are then
// preloaded from the binary persistence file and the client can set up the measurement before the instrumented code has been executed
// In the first run, the A2L file is generated while the application runs, a connect would finalize it before anything is registered
// Returns immediately in the first run, when XCP is not activated or on SIGINT/SIGTERM
void wait_xcp() {

    if (!XcpIsActivated())
        return;

    if (!std::filesystem::exists(A2lGetFilename())) {
        std::cout << "First run, " << A2lGetFilename() << " is generated now, restart to measure the startup" << std::endl;
        return;
    }

    std::cout << "Waiting for the XCP client to start the measurement (Ctrl-C to abort) ..." << std::endl;
    while (running && !XcpIsDaqRunning()) {
        sleep_us(10000); // 10ms
    }
}

#endif

//-----------------------------------------------------------------------------------------------------

int main() {

    std::cout << "\nXCP on Ethernet telemetry_demo C++ demo\n" << std::endl;
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    init_xcp();
#ifdef OPTION_OBSERVE_STARTUP
    wait_xcp();
#endif

    auto startup_context = xcp::Context("Startup");
    xcp::Scope startup_scope(startup_context);

    {
        // The signal generator instances are created in the span initialize and destroyed at the end of this block
        std::unique_ptr<signal_generator::SignalGenerator> signal_generator_1;
        std::unique_ptr<signal_generator::SignalGenerator> signal_generator_2;

        // Initialize
        {
            DaqSpan(initialize);
            sleep_us(10000);

            // Simplified signal generator C++ class taken from cpp_demo
            // Create 2 instances
            // The parameters are the reference page of the calibration segment, they must outlive the instances (static)
            static const signal_generator::SignalParametersT kSignalParameters1 = {
                .ampl = 50,
                .phase = 0.0,
                .offset = -50.0,
                .period = 1.0,                                          // s
                .delay_us = 5000,                                       // us
                .signal_type = signal_generator::SignalTypeT::TRIANGLE, // Type of the signal
            };
            static const signal_generator::SignalParametersT kSignalParameters2 = {
                .ampl = 50.0,
                .phase = 0.0,
                .offset = +50.0,
                .period = 0.5,                                      // s
                .delay_us = 5000,                                   // us
                .signal_type = signal_generator::SignalTypeT::SINE, // Type of the signal
            };
            signal_generator_1 = std::make_unique<signal_generator::SignalGenerator>("SigGen1", &kSignalParameters1);
            signal_generator_2 = std::make_unique<signal_generator::SignalGenerator>("SigGen2", &kSignalParameters2);

            sleep_us(10000); // Wait a bit to let the signal generator threads initialize
        }

        // Main loop
        auto mainloop_context = xcp::Context("Mainloop");
        xcp::Scope mainloop_scope(mainloop_context);
        std::cout << "Starting main loop..." << std::endl;
        double channel1, channel2, sum;
        while (running) {

            {
                DaqSpan(calc_sum);

                // Poll the current values of signal generator 1+2
                // Each poll in its own span, the span poll in SignalGenerator::Poll is nested in poll_1 or poll_2
                {
                    DaqSpan(get_channel1);
                    sleep_us(100);
                    channel1 = signal_generator_1->Poll();
                    sleep_us(100);
                }
                {
                    DaqSpan(get_channel2);
                    sleep_us(100);
                    channel2 = signal_generator_2->Poll();
                    sleep_us(100);
                }
                sum = channel1 + channel2;
            }

            // Trigger the XCP measurement event mainloop for local variables 'channel1' and 'channel2' in context 'Mainloop' with event 'mainloop'
            // DaqEventVar will also work, but the value will not be displayed in the Mainloop context track
            DaqEventCtxVar(mainloop,                                                                                      //
                           A2L_MEAS_PHYS(channel1, "Polled current value of signal generator 1", "Volt", -100.0, +100.0), //
                           A2L_MEAS_PHYS(channel2, "Polled current value of signal generator 2", "Volt", -100.0, +100.0), //
                           A2L_MEAS_PHYS(sum, "Calculated sum of channels", "Volt", -1000.0, +1000.0)                     //
            );

            {
                DaqSpan(analyze);
                sleep_us(1000); // Simulate some work time in span analyze
            }

            sleep_us(15000);

        } // while (running)

    } // Detroy signal generator instances before shutting down the XCP server and A2L generator

    shutdown_xcp();
    return 0;
}
