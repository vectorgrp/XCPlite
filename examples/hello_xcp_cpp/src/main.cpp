// hello_xcp_cpp - simple XCPlite/libxcplite C++ example

#include <array>    // for std::array
#include <atomic>   // for std::atomic
#include <csignal>  // for signal handling
#include <cstdint>  // for uintxx_t
#include <iostream> // for std::cout
#include <optional> // for std::optional

// Include XCPlite/libxcplite C++ headers
#include <a2l.hpp>    // for A2l generation application programming interface
#include <xcplib.hpp> // for application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP parameters

constexpr const char OPTION_PROJECT_NAME[] = "hello_xcp_cpp";   // Project name, used to build the A2L and BIN file name
constexpr const char OPTION_PROJECT_VERSION[] = "V1_" __TIME__; // EPK version string
constexpr bool OPTION_USE_TCP = false;                          // TCP or UDP
constexpr uint8_t OPTION_SERVER_ADDR[] = {0, 0, 0, 0};          // Bind addr, 0.0.0.0 = ANY
constexpr uint16_t OPTION_SERVER_PORT = 5555;                   // Port
#define OPTION_QUEUE_SIZE (1024 * 32)                           // Size of the queue in bytes, should be large enough to cover at least 10ms of expected traffic
constexpr int OPTION_LOG_LEVEL = 3;                             // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------
// Global calibration parameters

// Calibration parameters struct
struct ParametersT {
    double min;         // Minimum value for random number generation
    double max;         // Maximum value for random number generation
    uint8_t map[4][8];  // Demo 2D map with 4x8 values
    uint16_t curve[16]; // Demo curve with 16 values
    uint16_t axis[16];  // Demo axis with 16 values
};

// Default parameter values
const ParametersT kParameters = {.min = -2.0,
                                 .max = +2.0,
                                 .map = {{0, 1, 2, 3, 4, 5, 6, 7}, {11, 12, 13, 14, 15, 16, 17, 18}, {21, 22, 23, 24, 25, 26, 27, 28}, {31, 32, 33, 34, 35, 36, 37, 38}},
                                 .curve = {0, 10, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500},
                                 .axis = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};

// A global calibration parameter segment handle for struct 'ParametersT'
// Initialized in main(), after XCP initialization
std::optional<xcplib::CalSeg<ParametersT>> gCalSeg;

//-----------------------------------------------------------------------------------------------------
// Demo floating average calculation class

namespace floating_average {

class FloatingAverage {

    constexpr static size_t kMaxSamples_ = 64; // Maximum number of samples for the floating average

  private:
    size_t current_index_;         // Current position in the ring buffer
    uint8_t sample_count_;         // Number of samples collected so far
    double sum_;                   // Running sum of all samples
    double samples_[kMaxSamples_]; // Ring buffer for storing samples

  public:
    FloatingAverage();
    ~FloatingAverage() = default;
    double calc(double input);
};

FloatingAverage::FloatingAverage() : samples_{}, current_index_(0), sample_count_(0), sum_(0.0) {

    // Optional: For measurement of the complete instance, create an A2L typedef for this class (once executed and thread safe)
    A2lCreateTypedef(FloatingAverage, "Typedef for FloatingAverage",                                       // @@@@ TODO: All types are wrong, fixit
                     A2L_MEASUREMENT_COMPONENT(current_index_, "Current position in the ring buffer", ""), //
                     A2L_MEASUREMENT_COMPONENT(sample_count_, "Number of samples collected so far", ""),   //
                     A2L_MEASUREMENT_COMPONENT(sum_, "Running sum of all samples", ""),                    //
                     A2L_MEASUREMENT_ARRAY_COMPONENT(samples_, "Ring buffer for samples", ""));
}

// Floating average calculate function - instrumented for XCP measurement
[[nodiscard]] double FloatingAverage::calc(double input) {

    // Calculate the floating average over kMaxSamples_ samples
    if (sample_count_ >= kMaxSamples_) {
        sum_ -= samples_[current_index_];
    } else {
        sample_count_++;
    }
    samples_[current_index_] = input;
    sum_ += input;
    double average = sum_ / static_cast<double>(sample_count_);
    current_index_ = (current_index_ + 1) % kMaxSamples_;

    // Trigger event 'calcAvg' and register event and individual parameters, local variables and member variables once
    DaqEventVar(calcAvg,                                                                    //
                A2L_MEAS_PHYS(input, "Input value for floating average", "V", 0.0, 1000.0), // Measure parameter 'input' as physical value in Volt
                A2L_MEAS(average, "Current calculated average"),                            // Measure local variable 'average'
                A2L_MEAS(current_index_, "Current position in ring buffer"),                // Measure member variable 'current_index_'
                A2L_MEAS(sample_count_, "Number of samples collected"),                     // Measure member variable 'sample_count_'
                A2L_MEAS(sum_, "Running sum of all samples"),                               // Measure member variable 'sum_'
                A2L_MEAS_INST_PTR(FloatingAverage, this, "FloatingAverage",
                                  "This instance of FloatingAverage")); // Redundant, just to show we also can measure this instance as a whole with a typedef

    return average;
}

} // namespace floating_average

//-----------------------------------------------------------------------------------------------------
// Demo random number generator with global calibration parameters

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

    // Register the ParametersT calibration segment description as a typedef and an instance in the A2L file
    A2lCreateTypedef(ParametersT, "Typedef for ParametersT",                //
                     A2L_MAP_COMPONENT(map, "Demo map", "", 0, 100),        //
                     A2L_AXIS_COMPONENT(axis, "Demo axis", "", 0, 100),     //
                     A2L_CURVE_COMPONENT(curve, "Demo curve", "", 0, 2000), //
                     // A2L_CURVE_WITH_AXIS_COMPONENT(curve, "Demo curve", "", 0, 100, axis), // CANape >= 24.0
                     A2L_PARAMETER_COMPONENT(min, "Minimum random number value", "", -100.0, 100.0), //
                     A2L_PARAMETER_COMPONENT(max, "Maximum random number value", "", -100.0, 100.0));
    gCalSeg->CreateA2lTypedefInstance("ParametersT", "Demo calibration parameters for hello_xcp_cpp example");

    // Create a simple arithmetic local variable
    uint16_t counter{0};

    // Create FloatingAverage calculator instances
    // Example1: On local stack
    floating_average::FloatingAverage average_filter1;
    // Example2: On heap, instance behind a unique_ptr
    auto average_filter2 = std::make_unique<floating_average::FloatingAverage>();
    // Example3:  On heap
    auto average_filter3 = new floating_average::FloatingAverage();

    // Main loop
    std::cout << "Starting main loop... (Press Ctrl+C to exit)" << std::endl;

    const uint32_t kDelayUs = 1000;                                             // Loop delay in microseconds
    auto delay_us = CalVal(kDelayUs);                                           // Create a calibratable value for constant kDelayUs
    A2lCreateParameter(kDelayUs, "Loop delay in microseconds", "", 0, 1000000); // Create the A2L parameter description

    while (gRun) {
        counter++;
        global_counter++;

        double input_voltage = random_number();
        double average_voltage[3];

        // Calculate floating average of input_voltage
        // Note that the event 'calcAvg' instrumented inside the FloatingAverage::calc() method, will trigger on each call of any instance (average_filter2 and average_filter3)
        // Events may be disabled and enabled, to filter out a particular instance to observe
        DaqEventEnable(calcAvg);
        average_voltage[0] = average_filter1.calc(input_voltage); // Offset input to differentiate from average_filter2
        DaqEventDisable(calcAvg);

        // Calculate floating average with the 2 other instances on heap
        average_voltage[1] = average_filter2->calc(input_voltage - 10.0);
        average_voltage[2] = average_filter3->calc(input_voltage + 10.0);

        // Trigger data acquisition event "mainloop", once register event, global and local variables, and heap instance measurements
        DaqEventVar(mainloop,                                                                                                 //
                    A2L_MEAS(global_counter, "Global counter variable"),                                                      //
                    A2L_MEAS(counter, "Local counter variable"),                                                              //
                    A2L_MEAS_PHYS(input_voltage, "Input voltage", "V", -100.0, 100.0),                                        //
                    A2L_MEAS_ARRAY_PHYS(average_voltage, "Calculated average voltages on input_voltage", "V", -100.0, 100.0), //
                    // Measure all 3 complete instances of FloatingAverage
                    A2L_MEAS_INST(average_filter1, "FloatingAverage", "Local (stack) instance of FloatingAverage<128>"),                   //
                    A2L_MEAS_INST_PTR(average_filter2, average_filter2.get(), "FloatingAverage", "Heap RAII instance of FloatingAverage"), //
                    A2L_MEAS_INST_PTR(average_filter3, average_filter3, "FloatingAverage", "Heap instance of FloatingAverage")

        );

        // Sleep for a while, use a calibratable value for the delay
        auto l = delay_us.lock();
        sleepUs(*l);

        A2lFinalize(); // @@@@ TEST: Manually finalize the A2L file to make it visible on file system without XCP tool connect
    }

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server

    return 0;
}
