// hello_xcp_cpp - simple xcplib C++ example

#include <array>    // for std::array
#include <atomic>   // for std::atomic
#include <csignal>  // for signal handling
#include <cstdint>  // for uintxx_t
#include <iostream> // for std::cout
#include <optional> // for std::optional

#include "a2l.hpp"    // for xcplib A2l generation application programming interface
#include "xcplib.hpp" // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP parameters
#define OPTION_PROJECT_NAME "hello_xcp_cpp" // A2L project name
#define OPTION_PROJECT_EPK __TIME__         // EPK version string
#define OPTION_USE_TCP true                 // TCP or UDP, use TCP
#define OPTION_SERVER_PORT 5555             // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}     // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 64)       // Size of the measurement queue in bytes
#define OPTION_LOG_LEVEL 3                  // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------
// Floating average calculation class

namespace floating_average {

template <size_t N> class FloatingAverage {

  private:
    std::array<double, N> samples_; // Ring buffer for storing samples
    size_t current_index_;          // Current position in the ring buffer
    size_t sample_count_;           // Number of samples collected so far
    double sum_;                    // Running sum of all samples

  public:
    FloatingAverage();
    ~FloatingAverage() = default;
    double calculate(double input);
};

template <size_t N> FloatingAverage<N>::FloatingAverage() : samples_{}, current_index_(0), sample_count_(0), sum_(0.0) {

    // Create a measurement event "avg_calc"
    XcpCreateDaqEvent(avg_calc);

    if (A2lOnce()) {
        // Register member variables for XCP measurement using 'this' as the base address for relative mode
        A2lSetRelativeAddrMode(avg_calc, this);
        A2lCreateMeasurement(current_index_, "Current position in ring buffer");
        A2lCreateMeasurement(sample_count_, "Number of samples collected");
        A2lCreateMeasurement(sum_, "Running sum of all samples");
    }

    std::cout << "FloatingAverage<" << N << "> instance created" << std::endl;
}

// Floating avarage calculate function - instrumented for XCP measurement
template <size_t N> double FloatingAverage<N>::calculate(double input) {

    double average; // Current calculated average

    // Calculate the floating average over N samples
    if (sample_count_ >= N) {
        sum_ -= samples_[current_index_];
    } else {
        sample_count_++;
    }
    samples_[current_index_] = input;
    sum_ += input;
    average = sum_ / static_cast<double>(sample_count_);
    current_index_ = (current_index_ + 1) % N;

    XcpTriggerDaqEvent(avg_calc,                                                      // Event
                       (input, "Input value for floating average", "V", 0.0, 1000.0), // input value in Volts from 0..1000
                       (average, "Current calculated average")                        // calculated average
    );

    return average;
}

} // namespace floating_average

//-----------------------------------------------------------------------------------------------------
// Calibration parameters for the random number generator

struct ParametersT {
    double min; // Minimum random number value
    double max; // Maximum random number value
};

// Default parameter values
const ParametersT kParameters = {.min = 2.0, .max = 3.0};

// A calibration segment wrapper for the parameters
std::optional<xcplib::CalSeg<ParametersT>> gCalSeg;

// Simple random number generation between min and max calibration parameters
double random_number() {
    static unsigned int seed = 12345;
    seed = seed * 1103515245 + 12345;

    // Lock access to calibration parameters RAII guard
    auto params = gCalSeg->lock();

    double random = params->min + ((seed / 65536) % 32768) / 32768.0 * (params->max - params->min);
    return random;
};

//-----------------------------------------------------------------------------------------------------

// Signal handler for graceful exit on Ctrl+C
std::atomic<bool> gRun{true};
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        gRun = false;
    }
}

int main() {

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "\nXCP on Ethernet demo - simple C++ example\n" << std::endl;

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, activate XCP
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_EPK, true);

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        std::cerr << "Failed to initialize XCP server" << std::endl;
        return 1;
    }

    // Enable A2L generation
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        std::cerr << "Failed to initialize A2L generator" << std::endl;
        return 1;
    }

    // Create a global calibration segment wrapper for the struct 'ParametersT' and use its default values in constant 'kParameters'
    // This calibration segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independent page switching, checksum calculation and reinitialization (copy reference page to working page)
    gCalSeg.emplace("Parameters", &kParameters);

    // Add the calibration segment description as a typedef and an instance to the A2L file
    A2lTypedefBegin(ParametersT, "A2L Typedef for ParametersT");
    A2lTypedefParameterComponent(min, ParametersT, "Minimum random number value", "", -100.0, 100.0);
    A2lTypedefParameterComponent(max, ParametersT, "Maximum random number value", "", -100.0, 100.0);
    A2lTypedefEnd();
    gCalSeg->CreateA2lTypedefInstance("ParametersT", "Random number generator parameters");

    // Create a FloatingAverage calculator instance with 128 samples
    floating_average::FloatingAverage<128> average;

    // Main loop
    std::cout << "Demo class instance created. Starting main loop... (Press Ctrl+C to exit)" << std::endl;
    while (gRun) {

        double voltage = random_number();
        double average_voltage = average.calculate(voltage);

        XcpDaqEvent(mainloop,                                                        //
                    (voltage, "Input value for floating average", "V", 0.0, 1000.0), //
                    (average_voltage, "Calculated voltage average")                  //
        );

        sleepUs(1000);
        A2lFinalize(); // @@@@ TEST: Manually finalize the A2L file to make it visible without XCP tool connect
    }

    // Cleanup
    std::cout << "\nExiting ..." << std::endl;
    XcpDisconnect();
    XcpEthServerShutdown();

    return 0;
}
