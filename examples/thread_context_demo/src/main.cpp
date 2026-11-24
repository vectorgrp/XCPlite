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

// Main loop parameters
struct MainloopParametersT {
    uint32_t idle_sleep_us; // Sleep time of the main loop, when there are no samples (in us)
};

//-----------------------------------------------------------------------------------------------------
// XCP

// #define OPTION_OBSERVE_STARTUP // Wait for XCP client to observe initialization (needs deterministic span and context creation!)

constexpr const char *OPTION_PROJECT_NAME = "thread_context_demo";
constexpr const char OPTION_PROJECT_VERSION[] = "V101";
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

    // Enter the 'Main' context, it is the current context of the main thread until the end of main()
    // Contexts with a literal name are entered with DaqContext, the signal generator instances use an xcp::Context member,
    // because their name is only known at runtime
    DaqContext(Main);

    {
        // The signal generator instances are created in the span initialize and destroyed at the end of this block
        std::unique_ptr<signal_generator::SignalGenerator> signal_generator_1;
        std::unique_ptr<signal_generator::SignalGenerator> signal_generator_2;

        // Initialize
        {
            DaqSpan(initialize);
            SimulateWork(10000); // 10ms

            // Simplified signal generator C++ class taken from cpp_demo
            // Create 2 instances
            // The parameters are the reference page of the calibration segment, they must outlive the instances (static)
            static const signal_generator::SignalParametersT kSignalParameters1 = {
                .ampl = 60,
                .phase = 0.0,
                .offset = -50.0,
                .period = 1.0,                                          // s
                .sample_time_us = 25,                                   // us
                .signal_type = signal_generator::SignalTypeT::TRIANGLE, // Type of the signal
            };
            static const signal_generator::SignalParametersT kSignalParameters2 = {
                .ampl = 50.0,
                .phase = 0.0,
                .offset = +50.0,
                .period = 0.5,                                      // s
                .sample_time_us = 1000,                             // us
                .signal_type = signal_generator::SignalTypeT::SINE, // Type of the signal
            };
            // SigGen1 queues up to 16 samples, SigGen2 has a single entry queue which always holds the latest sample
            // The instances are created stopped, they are started before the main loop
            signal_generator_1 = std::make_unique<signal_generator::SignalGenerator>("SigGen1", &kSignalParameters1, 16);
            signal_generator_2 = std::make_unique<signal_generator::SignalGenerator>("SigGen2", &kSignalParameters2, 1);

            SimulateWork(10000); // 10ms
        } // end of span 'initialize'

        // Main loop calibration parameters
        // The reference page must outlive the calibration segment (static)
        static const MainloopParametersT kMainloopParameters = {
            .idle_sleep_us = 100, // us  (Note: queue size of signal_generator_1 is 16, cycle time 25us)
        };
        A2lCreateTypedef(MainloopParametersT, "Typedef for MainloopParametersT", //
                         A2L_PARAMETER_COMPONENT(idle_sleep_us, "Main loop sleep time when there are no samples", "us", 0, 100000));
        xcp::CalSeg<MainloopParametersT> mainloop_parameters("Mainloop", &kMainloopParameters);
        mainloop_parameters.CreateA2lTypedefInstance("MainloopParametersT", "Main loop parameters");

        // Main loop
        double channel1 = 0, channel2 = 0; // Latest values of signal generator 1+2

        // Instrumentation
        uint32_t next_seq1 = 0, next_seq2 = 0; // Expected sequence number of the next sample

        // Start the signal generators one after the other (the slower signal_generator_2 first)
        // Start() returns after the first cycle of the task, the events of SigGen1 are then created before those of SigGen2,
        // so the event ids and the A2L file do not depend on the thread start-up timing
        std::cout << "Starting main loop..." << std::endl;
        signal_generator_2->Start();
        signal_generator_1->Start();
        while (running) {

            // Enter the 'Mainloop' context, it is the current context until the end of this cycle
            // The context is created in the first cycle, from then on entering it is a load and the attach of the scope
            DaqContext(Mainloop);

            // Poll loop: poll signal generator 1+2 until both queues are empty, the main loop does not sleep while samples are pending
            // Each channel in its own span, the spans poll in SignalGenerator::Poll are nested in get_channel1 or get_channel2
            // A sample triggers the event sample1 or sample2 with its value, its latency (time from calculation to poll) and the number of samples dropped before it
            {
                DaqSpan(poll_loop);

                bool pending = true;
                while (pending) {
                    pending = false;
                    {
                        DaqSpan(get_channel1);
                        SimulateWork(100);
                        if (auto sample = signal_generator_1->Poll()) {
                            double value1 = channel1 = sample->value;
                            pending = true;

                            // Instrumentation
                            uint32_t latency1 = static_cast<uint32_t>(clockGetMonotonicUs() - sample->time_us);
                            uint32_t dropped1 = sample->seq - next_seq1;
                            next_seq1 = sample->seq + 1;
                            DaqEventCtxVar(sample1, // context='Mainloop', span='get_channel1'                                                                           //
                                           A2L_MEAS_PHYS(value1, "Sample value of signal generator 1", "Volt", -100.0, +100.0), //
                                           A2L_MEAS_PHYS(latency1, "Sample latency of signal generator 1", "us", 0, 100000),    //
                                           A2L_MEAS(dropped1, "Samples of signal generator 1 dropped before this sample")       //
                            );
                        }
                        SimulateWork(100);
                    } // end of span 'get_channel1'
                    {
                        DaqSpan(get_channel2);
                        SimulateWork(100);
                        if (auto sample = signal_generator_2->Poll()) {
                            double value2 = channel2 = sample->value;
                            pending = true;

                            // Instrumentation
                            uint32_t latency2 = static_cast<uint32_t>(clockGetMonotonicUs() - sample->time_us);
                            uint32_t dropped2 = sample->seq - next_seq2;
                            next_seq2 = sample->seq + 1;
                            DaqEventCtxVar(sample2, // context='Mainloop', span='get_channel2'                                                                           //
                                           A2L_MEAS_PHYS(value2, "Sample value of signal generator 2", "Volt", -100.0, +100.0), //
                                           A2L_MEAS_PHYS(latency2, "Sample latency of signal generator 2", "us", 0, 100000),    //
                                           A2L_MEAS(dropped2, "Samples of signal generator 2 dropped before this sample")       //
                            );
                        }
                        SimulateWork(100);
                    } // end of span 'get_channel2'
                }
            } // end of span 'poll_loop'

            // Trigger the XCP measurement event mainloop for local variables 'channel1' and 'channel2' in context 'Mainloop' with event 'mainloop'
            // There is no active span
            // DaqEventVar will also work, but the value will not be displayed in the Mainloop context track
            DaqEventCtxVar(mainloop, // context='Mainloop', no span                                                                                       //
                           A2L_MEAS_PHYS(channel1, "Latest value of signal generator 1", "Volt", -100.0, +100.0), //
                           A2L_MEAS_PHYS(channel2, "Latest value of signal generator 2", "Volt", -100.0, +100.0)  //
            );

            {
                DaqSpan(analyze);
                SimulateWork(1000); // 1ms
            } // end of span 'analyze'

            // Both queues were empty, sleep
            sleep_us(mainloop_parameters.lock()->idle_sleep_us);

// @@@@ TODO: Remove
#ifdef OPTION_DAQ_CONTEXT_TEST
            running = false;
#endif

        } // while (running)

    } // Detroy signal generator instances before shutting down the XCP server and A2L generator

    shutdown_xcp();
    return 0;
}
