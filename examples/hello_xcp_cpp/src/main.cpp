// hello_xcp_cpp - simple XCPlite/libxcplite C++ example

#include <array>    // for std::array
#include <atomic>   // for std::atomic
#include <csignal>  // for signal handling
#include <cstdint>  // for uintxx_t
#include <iostream> // for std::cout
#include <optional> // for std::optional

// Include XCPlite/libxcplite C++ headers
// #define OPTION_XCP_MODE 0 // To deactivate XCP, define OPTION_XCP_MODE here
#include <a2l.hpp>    // for A2l generation application programming interface
#include <xcplib.hpp> // for application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP parameters

constexpr const char OPTION_PROJECT_NAME[] = "hello_xcp_cpp"; // Project name, used to build the A2L and BIN file name
constexpr const char OPTION_PROJECT_VERSION[] = "107";        // EPK version string
constexpr bool OPTION_USE_TCP = false;                        // TCP or UDP
constexpr uint8_t OPTION_SERVER_ADDR[] = {0, 0, 0, 0};        // Bind addr, 0.0.0.0 = ANY
constexpr uint16_t OPTION_SERVER_PORT = 5555;                 // Port
constexpr uint16_t OPTION_QUEUE_SIZE = (1024 * 32);           // Size of the queue in bytes, should be large enough to cover at least 10ms of expected traffic
constexpr int OPTION_LOG_LEVEL = 3;                           // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

// XCP mode:
constexpr uint8_t OPTION_XCP_MODE = (XCP_MODE_PERSISTENCE | XCP_MODE_LOCAL); // XCP single application server mode
// constexpr uint8_t OPTION_XCP_MODE = (XCP_MODE_DEACTIVATE); // XCP deactivated, passive mode

// A2L generation mode:
constexpr uint8_t OPTION_A2L_MODE = (A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS);

#define OPTION_ENABLE_CALIBRATION // Enable parameter tuning in the code below

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

// Calibration parameters struct
struct ParametersT {
    uint32_t delay_us;    // Mainloop delay time in us
    uint16_t counter_max; // Maximum value for the counter
    double min;           // Minimum value for random number generation
    double max;           // Maximum value for random number generation
    uint8_t map[4][8];    // Demo 2D map with 4x8 values
    uint16_t curve[16];   // Demo curve with 16 values
    uint16_t axis[16];    // Demo axis with 16 values
};

// Default parameter values
const ParametersT kParameters = {.delay_us = 1000,
                                 .counter_max = 1024,
                                 .min = -2.0,
                                 .max = +2.0,
                                 .map = {{0, 1, 2, 3, 4, 5, 6, 7}, {11, 12, 13, 14, 15, 16, 17, 18}, {21, 22, 23, 24, 25, 26, 27, 28}, {31, 32, 33, 34, 35, 36, 37, 38}},
                                 .curve = {0, 10, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500},
                                 .axis = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};

// Create a global calibration parameter segment for struct 'ParametersT' to provide safe and consistent access to the calibration parameters
// Initialized in main(), after XCP initialization with
#define OPTION_ENABLE_CALIBRATION // Enable parameter tuning in the code below
// Option 1:
// This calibration segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
// It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
// It supports XCP/ECU independent page switching, checksum calculation and reinitialization (copy reference page to working page)
std::optional<xcplib::CalSeg<ParametersT>> gCalSeg;
// Option 2:
// This calibration block does not create a MEMORY_SEGMENT in the A2L file
// It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
// It supports XCP/ECU page switching, but can not be explicitly controlled via XCP commands
// std::optional<xcplib::CalBlk<ParametersT>> gCalSeg;
// Option 3:
// Calibration disabled
// const ParametersT *params = &kParameters; // Direct access to the calibration parameters constants

uint32_t kDelayUs = 1000; // Loop delay in microseconds

//-----------------------------------------------------------------------------------------------------
// Demo global measurement value

uint16_t global_counter{0};

//-----------------------------------------------------------------------------------------------------
// Demo class with XCP measurement instrumentation
// Floating average calculation

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
// Demo function with XCP instrumentation
// Random number generator with global calibration parameters

// Simple random number generation in range [min ..= max] using global calibration parameters
[[nodiscard]] double random_number() {
    static unsigned int seed{12345};
    seed = seed * 1103515245 + 12345;

// Acquire access to calibration parameters with RAII guard "params", this is thread-safe, lock-free and reentrant
#ifdef OPTION_ENABLE_CALIBRATION
    auto params = gCalSeg->lock();
#endif
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

int main(int argc, char *argv[]) {

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::cout << "\nXCP on Ethernet hello_xcp_cpp C++ demo - " << argv[0] << "\n" << std::endl;
    uint64_t start_time = clockGetMonotonicNs(); // Get the start time in nanoseconds

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton and activate XCP
    if (!XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION /* EPK version*/, OPTION_XCP_MODE)) {
        std::cerr << "Failed to initialize XCP" << std::endl;
        return 1;
    }
    XcpSetElfName(argv[0]); // Set ELF file name for upload via GET_ID, optional

    // Initialize the XCP Server
    if (!XcpEthServerInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        std::cerr << "Failed to initialize XCP server" << std::endl;
        return 1;
    }

    // Enable runtime A2L generation for data declaration as code, optional
    // The A2L file will be created when the XCP tool connects and, if it does not already exist on local file system and the version did not change
    if (!A2lInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_A2L_MODE)) {
        std::cerr << "Failed to initialize A2L generator" << std::endl;
        return 1;
    }

#ifdef OPTION_ENABLE_CALIBRATION
    // Register the ParametersT calibration segment description as a typedef and an instance in the A2L file
    A2lCreateTypedef(ParametersT, "Typedef for ParametersT",                //
                     A2L_MAP_COMPONENT(map, "Demo map", "", 0, 100),        //
                     A2L_AXIS_COMPONENT(axis, "Demo axis", "", 0, 100),     //
                     A2L_CURVE_COMPONENT(curve, "Demo curve", "", 0, 2000), //
                     // A2L_CURVE_WITH_AXIS_COMPONENT(curve, "Demo curve", "", 0, 100, axis), // CANape >= 24.0
                     A2L_PARAMETER_COMPONENT(min, "Minimum random number value", "", -100.0, 100.0), //
                     A2L_PARAMETER_COMPONENT(max, "Maximum random number value", "", -100.0, 100.0), //
                     A2L_PARAMETER_COMPONENT(counter_max, "Maximum counter value", "", 0, 65535),    //
                     A2L_PARAMETER_COMPONENT(delay_us, "Mainloop delay time in us", "us", 0, 500000));

    // Initialize the global calibration wrapper for the struct 'ParametersT' and set the default values in constant 'kParameters' as reference page (FLASH)
    gCalSeg.emplace("params", &kParameters);
    gCalSeg->CreateA2lTypedefInstance("ParametersT", "Demo calibration parameters for hello_xcp_cpp example");
#endif

    uint64_t run_time = clockGetMonotonicNs(); // Get the start time of the application thread in nanoseconds

    // Create a simple arithmetic local variable
    uint16_t counter{0};

    // Create FloatingAverage calculator instances
    // On local stack
    floating_average::FloatingAverage average_filter;
    // On heap
    // auto average_filter = std::make_unique<floating_average::FloatingAverage>();
    // auto average_filter = new floating_average::FloatingAverage();

    // Main loop
    std::cout << "Starting application main loop... (startup time: " << (run_time - start_time) / 1000 << " us) (Press Ctrl+C to exit)" << std::endl;
    while (gRun) {
        counter++;
        global_counter++;
        {
#ifdef OPTION_ENABLE_CALIBRATION
            auto params = gCalSeg->lock(); // Don't keep the calibration parameters locked for longer than necessary, to minimize delays of XCP write access from the tool
#endif
            kDelayUs = params->delay_us;         // Get the delay_us calibration value
            if (counter > params->counter_max) { // Get the counter_max calibration value and reset counter
                counter = 0;
            }
        }

        double input_voltage = random_number();
        double average_voltage;

        // Calculate floating average of input_voltage
        // Note that the event 'calcAvg' instrumented inside the FloatingAverage::calc() method, will trigger on each call of any instance (average_filter)
        // Events may be disabled and enabled, to filter out a particular instance to observe (DaqEventEnable(calcAvg), DaqEventDisable(calcAvg))
        average_voltage = average_filter.calc(input_voltage); // Offset input to differentiate from average_filter2
        // On heap
        // average_voltage = average_filter->calc(input_voltage);
        // average_voltage = average_filter->calc(input_voltage);

        // Trigger data acquisition event "mainloop", once register event, global and local variables, and heap instance measurements
        DaqEventVar(mainloop,                                                                                          //
                    A2L_MEAS(global_counter, "Global counter variable"),                                               //
                    A2L_MEAS(counter, "Local counter variable"),                                                       //
                    A2L_MEAS_PHYS(input_voltage, "Input voltage", "V", -100.0, 100.0),                                 //
                    A2L_MEAS_PHYS(average_voltage, "Calculated average voltage on input_voltage", "V", -100.0, 100.0), //
                    A2L_MEAS_INST(average_filter, "FloatingAverage", "Local (stack) instance of FloatingAverage<128>") // Measure complete instance of FloatingAverage
                    // A2L_MEAS_INST_PTR(average_filter, average_filter.get(), "FloatingAverage", "Heap RAII instance of FloatingAverage"),
                    // A2L_MEAS_INST_PTR(average_filter, average_filter, "FloatingAverage", "Heap instance of FloatingAverage")

        );

        // Original code with fixed delay, now replaced by tunable parameter delay_us
        sleepUs(kDelayUs); // Sleep for kDelayUs microseconds
    }

    XcpDisconnect(); // Force disconnect the XCP client
    A2lFinalize();   // Finalize A2L generation, if not done yet
    // XcpFreeze(); // Save current calibration changes to binary persistence file
    XcpEthServerShutdown(); // Stop the XCP server

    return 0;
}
