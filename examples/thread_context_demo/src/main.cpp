// cpp_demo XCPlite/libxcplite C++ example

#include <atomic>   // for std::atomic
#include <csignal>  // for signal(), SIGINT, SIGTERM
#include <cstdint>  // for uintxx_t
#include <iostream> // for std::cout
#include <thread>   // for std::thread

// Include XCPlite/libxcplite C++ headers
#include "a2l.hpp"    // for A2l generation application programming interface
#include "xcplib.hpp" // for application programming interface

#include "sig_gen.hpp" // for signal_generator::SignalGenerator

//-----------------------------------------------------------------------------------------------------
// XCP parameters

constexpr const char *OPTION_PROJECT_NAME = "thread_context_demo";
constexpr const char OPTION_PROJECT_VERSION[] = "V0.9";
constexpr bool OPTION_USE_TCP = false;
constexpr uint8_t OPTION_SERVER_ADDR[] = {0, 0, 0, 0};
constexpr uint16_t OPTION_SERVER_PORT = 5555;
constexpr size_t OPTION_QUEUE_SIZE = (1024 * 64);
constexpr int OPTION_LOG_LEVEL = 4;

//-----------------------------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------------------------
// Demo signal generator default parameters

constexpr double kPi = 3.14159265358979323846;
constexpr double k2Pi = (kPi * 2);

// Signal handler for clean shutdown
static volatile bool running = true;
static void sig_handler(int) { running = false; }

int main() {

    std::cout << "\nXCP on Ethernet telemetry_demo C++ demo\n" << std::endl;
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, activate XCP, must be called before starting the server
    // If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION /* EPK version*/, XCP_MODE_PERSISTENCE | XCP_MODE_LOCAL);

    // Initialize the XCP Server
    if (!XcpEthServerInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        std::cerr << "Failed to initialize XCP server" << std::endl;
        return 1;
    }

    // Enable A2L generation
    // Set mode to write once to create stable A2L files, this also enables calibration segment persistence and freeze support
    if (!A2lInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        std::cerr << "Failed to initialize A2L generator" << std::endl;
        return 1;
    }

    {
        // Simplified signal generator C++ class taken from cpp_demo
        const signal_generator::SignalParametersT kSignalParameters1 = {
            .ampl = 12.5,
            .phase = 0.0,
            .offset = 0.0,
            .period = 1.0,                                          // s
            .delay_us = 1000,                                       // us
            .signal_type = signal_generator::SignalTypeT::TRIANGLE, // Type of the signal
        };
        signal_generator::SignalGenerator signal_generator_1("SigGen1", &kSignalParameters1);

        const signal_generator::SignalParametersT kSignalParameters2 = {
            .ampl = 80.0,
            .phase = kPi / 2,
            .offset = 0.0,
            .period = 10.0,                                     // s
            .delay_us = 1000,                                   // us
            .signal_type = signal_generator::SignalTypeT::SINE, // Type of the signal
        };
        signal_generator::SignalGenerator signal_generator_2("SigGen2", &kSignalParameters2);

        sleepUs(100000); // Wait a bit to let the signal generator threads initialize

        // Poll once to register the instrumentation in Poll
        (void)signal_generator_1.Poll();
        (void)signal_generator_2.Poll();

        // Create the A2L conversion for the span names
        xcp::A2lCreateSpanConversion();

        // Main loop
        std::cout << "Starting main loop..." << std::endl;
        double channel1, channel2;
        while (running) {

            // Poll the current values of signal generator 1+2
            channel1 = signal_generator_1.Poll();
            channel2 = signal_generator_2.Poll();

            // Trigger the XCP measurement event mainloop for local variables channel1 and channel2
            DaqEventVar(mainloop,                                                                           //
                        A2L_MEAS_PHYS(channel1, "Current value of signal generator 1", "Volt", -1.0, +1.0), //
                        A2L_MEAS_PHYS(channel2, "Current value of signal generator 2", "Volt", -1.0, +1.0)  //
            );

            // std::cout << channel1 << " , " << channel2 << std::endl;
            sleepUs(100000);

        } // while (running)

    } // Detroy signal generator instances before shutting down the XCP server and A2L generator

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
    return 0;
}
