// class_demo xcplib C++ example - Demonstrating class member variable measurement

#include <array>    // for std::array
#include <atomic>   // for std::atomic
#include <cstdint>  // for uintxx_t
#include <iostream> // for std::cout
#include <memory>   // for std::unique_ptr
#include <random>   // for random number generation

#include "a2l.hpp"    // for xcplib A2l generation application programming interface
#include "xcplib.hpp" // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP parameters
#define OPTION_PROJECT_NAME "class_demo" // A2L project name
#define OPTION_USE_TCP false             // TCP or UDP
#define OPTION_SERVER_PORT 5555          // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}  // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 64)    // Size of the measurement queue in bytes
#define OPTION_LOG_LEVEL 3

//-----------------------------------------------------------------------------------------------------

namespace floating_average {

template <size_t N> class FloatingAverage {

  private:
    std::array<double, N> samples_; // Ring buffer for storing samples
    size_t current_index_;          // Current position in the ring buffer
    size_t sample_count_;           // Number of samples collected so far
    double sum_;                    // Running sum of all samples
    double average_;                // Current calculated average

  public:
    FloatingAverage();
    ~FloatingAverage() = default;
    double calculate(double input);
};

template <size_t N> FloatingAverage<N>::FloatingAverage() : samples_{}, current_index_(0), sample_count_(0), sum_(0.0), average_(0.0) {
    std::cout << "FloatingAverage<" << N << "> instance created" << std::endl;
}

// Floating avarage calculate function - instrumented for XCP measurement
template <size_t N> double FloatingAverage<N>::calculate(double input) {

    // Create a measurement event for this member function
    DaqCreateEvent(avg_calc);

    // Register member variables for XCP measurement using relative addressing
    // This uses 'this' as the base address for measuring member variables
    if (A2lOnce()) {
        A2lSetRelativeAddrMode(avg_calc, this);
        A2lCreateMeasurement(current_index_, "Current position in ring buffer");
        A2lCreateMeasurement(sample_count_, "Number of samples collected");
        A2lCreateMeasurement(sum_, "Running sum of all samples");
        A2lCreateMeasurement(average_, "Current calculated average");

        // Also register local (stack) variables for this function
        A2lSetStackAddrMode(avg_calc);
        A2lCreateMeasurement(input, "Input value for floating average");
    }

    // Calculate the average
    if (sample_count_ >= N) {
        sum_ -= samples_[current_index_];
    } else {
        sample_count_++;
    }
    samples_[current_index_] = input;
    sum_ += input;
    average_ = sum_ / static_cast<double>(sample_count_);
    current_index_ = (current_index_ + 1) % N;

    // Trigger XCP measurement event
    // Use relative addressing to make member variables accessible
    DaqEventRelative(avg_calc, this);

    return average_;
}

} // namespace floating_average

//-----------------------------------------------------------------------------------------------

int main() {

    std::cout << "\nXCP on Ethernet class_demo - C++ class instrumentation example\n" << std::endl;

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, activate XCP
    XcpInit(true);

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        std::cerr << "Failed to initialize XCP server" << std::endl;
        return 1;
    }

    // Enable A2L generation
    if (!A2lInit(OPTION_PROJECT_NAME, NULL, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_AUTO_GROUPS)) {
        std::cerr << "Failed to initialize A2L generator" << std::endl;
        return 1;
    }

    // Create a FloatingAverage instance with 10 samples
    std::unique_ptr<floating_average::FloatingAverage<10>> avg_instance = std::make_unique<floating_average::FloatingAverage<10>>();

    // Setup random number generation for values between -1.0 and 1.0
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dis(-1.0, 1.0);

    // Main loop
    std::cout << "Demo class instances created. Starting main loop..." << std::endl;
    for (;;) {

        // Generate a random number between -1.0 and 1.0 and calculate floating average
        double random_value = dis(gen);
        double current_average = avg_instance->calculate(random_value);

        sleepUs(1000);

        // Test: Force finalize A2L file early for testing
        A2lFinalize();
    }

    // Cleanup (this code is never reached in this example)
    XcpDisconnect();
    XcpEthServerShutdown();

    return 0;
}
