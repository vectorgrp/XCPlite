// hello_xcp_cpp - simple xcplib C++ example

#include <array>    // for std::array
#include <atomic>   // for std::atomic
#include <csignal>  // for signal handling
#include <cstdint>  // for uintxx_t
#include <iostream> // for std::cout
#include <optional> // for std::optional

#include <a2l.hpp>    // for xcplib A2l generation application programming interface
#include <xcplib.hpp> // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP parameters

constexpr const char OPTION_PROJECT_NAME[] = "hello_xcp_cpp";
constexpr const char OPTION_PROJECT_VERSION[] = __TIME__;
constexpr bool OPTION_USE_TCP = true;
constexpr uint16_t OPTION_SERVER_PORT = 5555;
constexpr size_t OPTION_QUEUE_SIZE = 1024 * 64;
constexpr int OPTION_LOG_LEVEL = 3;
constexpr uint8_t OPTION_SERVER_ADDR[] = {0, 0, 0, 0};

//-----------------------------------------------------------------------------------------------------
// Demo floating average calculation class

namespace floating_average {

template <uint8_t N> class FloatingAverage {

  private:
    size_t current_index_;          // Current position in the ring buffer
    uint8_t sample_count_;          // Number of samples collected so far
    double sum_;                    // Running sum of all samples
    std::array<double, N> samples_; // Ring buffer for storing samples

  public:
    FloatingAverage();
    ~FloatingAverage() = default;
    double calc(double input);
};

template <uint8_t N> FloatingAverage<N>::FloatingAverage() : samples_{}, current_index_(0), sample_count_(0), sum_(0.0) {

    // Optional: For measurement of the complete instance, create an A2L typedef for this class (as template, not variant with parameter N)
    if (A2lOnce()) {
        A2lTypedefBegin(FloatingAverage, this, "Typedef for FloatingAverage<%u>", N);
        A2lTypedefMeasurementComponent(current_index_, "Current position in the ring buffer");
        A2lTypedefMeasurementComponent(sample_count_, "Number of samples collected so far");
        A2lTypedefMeasurementComponent(sum_, "Running sum of all samples");
        A2lTypedefEnd();
    }

    std::cout << "FloatingAverage<" << (unsigned int)N << "> instance created" << std::endl;
}

// Floating average calculate function - instrumented for XCP measurement
template <uint8_t N> [[nodiscard]] double FloatingAverage<N>::calc(double input) {

    // Calculate the floating average over N samples
    if (sample_count_ >= N) {
        sum_ -= samples_[current_index_];
    } else {
        sample_count_++;
    }
    samples_[current_index_] = input;
    sum_ += input;
    double average = sum_ / static_cast<double>(sample_count_);
    current_index_ = (current_index_ + 1) % N;

    // Observe variant N=64
    // Trigger event 'calc64' and register event and individual parameters, local variables and member variables once
    if (N == 64) {
        DaqEventVar(calc64,                                                                     //
                    A2L_MEAS_PHYS(input, "Input value for floating average", "V", 0.0, 1000.0), // Measure parameter 'input' as physical value in Volt
                    A2L_MEAS(average, "Current calculated average"),                            // Measure local variable 'average'
                    A2L_MEAS(current_index_, "Current position in ring buffer"),                // Measure member variable 'current_index_'
                    A2L_MEAS(sample_count_, "Number of samples collected"),                     // Measure member variable 'sample_count_'
                    A2L_MEAS(sum_, "Running sum of all samples"),                               // Measure member variable 'sum_'
                    A2L_MEAS_INST_PTR(FloatingAverage, this, "FloatingAverage",
                                      "This instance of FloatingAverage")); // Redundant, just to show we also can measure this instance as a whole with a typedef instance
    }

    return average;
}

} // namespace floating_average

//-----------------------------------------------------------------------------------------------------
// Demo random number generator with global calibration parameters

// Calibration parameters for the random number generator
struct ParametersT {
    double min; // Minimum random number value
    double max; // Maximum random number value
};

// Default parameter values
const ParametersT kParameters = {.min = -2.0, .max = +2.0};

// A global calibration parameter segment handle for struct 'ParametersT'
// Initialized in main(), after XCP initialization
std::optional<xcplib::CalSeg<ParametersT>> gCalSeg;

// Simple random number generation in range [min ..= max] using global calibration parameters
[[nodiscard]] double random_number() {
    static unsigned int seed{12345};
    seed = seed * 1103515245 + 12345;

    // Acquire access to calibration parameters with RAII guard "params", this is thread-safe, lock-free and reentrant
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

// Define a global variable to be measured later in the main loop
uint16_t global_counter{0};

int main() {

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "\nXCP on Ethernet demo - simple C++ example\n" << std::endl;

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton and activate XCP
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION /* EPK version*/, true /* activate */);

    // Initialize the XCP Server
    if (!XcpEthServerInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        std::cerr << "Failed to initialize XCP server" << std::endl;
        return 1;
    }

    // Enable runtime A2L generation for data declaration as code
    // The A2L file will be created when the XCP tool connects and, if it does not already exist on local file system and the version did not change
    if (!A2lInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        std::cerr << "Failed to initialize A2L generator" << std::endl;
        return 1;
    }

    // Create a global calibration segment wrapper for the struct 'ParametersT' and use its default values in constant 'kParameters'
    // This calibration segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independent page switching, checksum calculation and reinitialization (copy reference page to working page)
    gCalSeg.emplace("Parameters", &kParameters);

    // Register the calibration segment description as a typedef and an instance in the A2L file
    A2lTypedefBegin(ParametersT, &kParameters, "Typedef for ParametersT");
    A2lTypedefParameterComponent(min, "Minimum random number value", "", -100.0, 100.0);
    A2lTypedefParameterComponent(max, "Maximum random number value", "", -100.0, 100.0);
    A2lTypedefEnd();
    gCalSeg->CreateA2lTypedefInstance("ParametersT", "Random number generator parameters");

    // Create a simple arithmetic local variable
    uint16_t counter{0};

    // Create FloatingAverage calculator instances
    // Example1: On local stack with N=128
    floating_average::FloatingAverage<128> average_filter1;
    // Example2: On heap, instance behind a unique_ptr with N=64
    auto average_filter2 = std::make_unique<floating_average::FloatingAverage<64>>();
    // Example3:  On heap with N=64
    auto average_filter3 = new floating_average::FloatingAverage<64>();

    // Main loop
    std::cout << "Starting main loop... (Press Ctrl+C to exit)" << std::endl;
    while (gRun) {

        counter++;
        global_counter++;

        double input_voltage = random_number();

        // Calculate floating average 128 of input_voltage
        double average_voltage1 = average_filter1.calc(input_voltage); // Offset input to differentiate from average_filter2

        // Calculate floating average 64 with the 2 instances on heap
        // Note that the event 'calc64' instrumented inside the FloatingAverage<64>::calc() method, will trigger on each call of any instance (average_filter2 and average_filter3)
        // Events may be disabled and enabled, to filter out a particular instance to observe
        DaqEventEnable(calc64);
        double average_voltage2 = average_filter2->calc(input_voltage - 10.0);
        DaqEventDisable(calc64);
        double average_voltage3 = average_filter3->calc(input_voltage + 10.0);

        // Trigger data acquisition event "mainloop", once register event, global and local variables, and heap instance measurements
        DaqEventVar(mainloop,                                                                                                                  //
                    A2L_MEAS(global_counter, "Global counter variable"),                                                                       //
                    A2L_MEAS(counter, "Local counter variable"),                                                                               //
                    A2L_MEAS_PHYS(input_voltage, "Input voltage", "V", -100.0, 100.0),                                                         //
                    A2L_MEAS(average_voltage1, "Calculated average <128> on input_voltage"),                                                   //
                    A2L_MEAS(average_voltage2, "Calculated average <64>on input_voltage-10"),                                                  //
                    A2L_MEAS(average_voltage3, "Calculated average <64> on input_voltage+10"),                                                 //
                    A2L_MEAS_INST(average_filter1, "FloatingAverage", "Local (stack) instance of FloatingAverage<128>"),                       //
                    A2L_MEAS_INST_PTR(average_filter2, average_filter2.get(), "FloatingAverage", "Heap RAII instance of FloatingAverage<64>"), //
                    A2L_MEAS_INST_PTR(average_filter3, average_filter3, "FloatingAverage", "Heap instance of FloatingAverage<64>")

        );

        sleepUs(1000);
        A2lFinalize(); // @@@@ TEST: Manually finalize the A2L file to make it visible on file system without XCP tool connect
    }

    // Cleanup
    std::cout << "\nExiting ..." << std::endl;
    XcpDisconnect();
    XcpEthServerShutdown();

    return 0;
}
