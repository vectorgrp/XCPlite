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
constexpr int OPTION_LOG_LEVEL = 4;
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

    // Optional: For measurement of the complete instance, create an A2L typedef for this class
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

    // Trigger event 'calc' (create, if not exists) and register individual local variables and member variables
    DaqEventExtVar(calc, this,                                                                 //
                   A2L_MEAS_PHYS(input, "Input value for floating average", "V", 0.0, 1000.0), //
                   A2L_MEAS(average, "Current calculated average"),                            //
                   A2L_MEAS(current_index_, "Current position in ring buffer"),                //
                   A2L_MEAS(sample_count_, "Number of samples collected"),                     //
                   A2L_MEAS(sum_, "Running sum of all samples"));

    return average;
}

} // namespace floating_average

//-----------------------------------------------------------------------------------------------------
// Demo random number generator with calibration parameters

// Calibration parameters for the random number generator
struct ParametersT {
    double min; // Minimum random number value
    double max; // Maximum random number value
};

// Default parameter values
const ParametersT kParameters = {.min = -2.0, .max = +2.0};

// A calibration segment wrapper for the parameters
std::optional<xcplib::CalSeg<ParametersT>> gCalSeg;

// Simple random number generation between min and max calibration parameters
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

    // Initialize the XCP singleton, activate XCP
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION /* EPK version*/, true /* activate */);

    // Initialize the XCP Server
    if (!XcpEthServerInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        std::cerr << "Failed to initialize XCP server" << std::endl;
        return 1;
    }

    // Enable runtime A2L generation for data declaration as code
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

    // Create FloatingAverage calculator instances with 128 samples
    // Local stack instance
    floating_average::FloatingAverage<128> average_filter;
    // Heap instance behind a unique_ptr
    auto average_filter2 = std::make_unique<floating_average::FloatingAverage<128>>();

    // Optional: Register the complete FloatingAverage instance as measurement on event 'mainloop' (a typedef 'FloatingAverage' is created in the constructor)
    DaqCreateEvent(mainloop);
    A2lSetStackAddrMode(mainloop);
    A2lCreateTypedefInstance(average_filter, FloatingAverage, "Stack instance of FloatingAverage<128>");

    // Optional: Register the heap FloatingAverage instance as measurement on event 'evt_heap'
    DaqCreateEvent(evt_heap);
    A2lSetRelativeAddrMode(evt_heap, average_filter2.get());
    A2lCreateInstance(average_filter2, FloatingAverage, 1, average_filter2.get(), "Heap instance of FloatingAverage<128>");

    // Define a local variable to be measured later in the main loop
    // Prefixing a local variable with XCP_MEAS is not needed in this example, as it only uses the combined variadic trigger and register templates
    uint16_t counter{0};

    // Main loop
    std::cout << "Starting main loop... (Press Ctrl+C to exit)" << std::endl;
    while (gRun) {

        counter++;
        global_counter++;

        double voltage = random_number();
        double average_voltage = average_filter.calc(voltage); // Offset input to differentiate from average_filter2

        // Trigger event "mainloop" (create, if not already exists), register local variable measurements
        DaqEventVar(mainloop,                                                        //
                    A2L_MEAS(global_counter, "Global counter variable"),             //
                    A2L_MEAS(counter, "Local counter variable"),                     //
                    A2L_MEAS_PHYS(voltage, "Input voltage", "V", 0.0, 1000.0),       //
                    A2L_MEAS(average_voltage, "Calculated voltage floating average") //
        );

        // Optional: Another FloatingAverage instance on heap
        // Note that the event 'calc' instrumented inside the FloatingAverage<>::calc() method, will trigger on each call of any instance (average_filter and average_filter2)
        // Events may be disabled and enabled, to filter out a particular instances to observe
        DaqEventEnable(calc);
        double average_voltage2 = average_filter2->calc(voltage - 10.0); // Add anoffset to differentiate from the other instance 'average_filter'
        DaqEventDisable(calc);
        assert(abs((average_voltage2 + 10.0) - (average_voltage)) < 0.00000001); // Should be identical

        // Trigger the event "evt_heap" to measure heap instance average_filter2
        DaqTriggerEventExt(evt_heap, average_filter2.get());

        sleepUs(1000);
        A2lFinalize(); // @@@@ TEST: Manually finalize the A2L file to make it visible without XCP tool connect
    }

    // Cleanup
    std::cout << "\nExiting ..." << std::endl;
    XcpDisconnect();
    XcpEthServerShutdown();

    return 0;
}
