
#include <cmath>
#include <iostream>
#include <string>
#include <thread>

#include "a2l.h"
#include "platform.h"
#include "xcplib.h"
#include "xcplib.hpp"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double k2Pi = (kPi * 2);

//-----------------------------------------------------------------------------------------------------
// XCP parameters
#define OPTION_ENABLE_A2L_GENERATOR         // Enable A2L file generation
#define OPTION_A2L_PROJECT_NAME "cpp_demo"  // A2L project name
#define OPTION_A2L_FILE_NAME "cpp_demo.a2l" // A2L filename
#define OPTION_USE_TCP false                // TCP or UDP
#define OPTION_SERVER_PORT 5555             // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}     // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 64)       // Size of the measurement queue in bytes
#define OPTION_LOG_LEVEL 3

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

struct ParametersT {
    uint16_t counter_max; // Maximum value for the counter
    uint32_t delay_us;    // Sleep time in microseconds for the main loop
};

// Default values
constexpr ParametersT parameters = {.counter_max = 1000, .delay_us = 1000};

//-----------------------------------------------------------------------------------------------------
// Demo global measurement values

uint8_t temperature = 50; // In Celsius
double speed = 0.0f;      // Speed in km/h

//-----------------------------------------------------------------------------------------------------
// Waveform generator class

/*
An instance of class SigGen creates a sine signal in its member variable value
Depending on calibration parameters ampl, phase, offset and period
*/

struct SignalParametersT {
    double ampl;       // Amplitude of the sine wave
    double phase;      // Phase shift in radians
    double offset;     // Offset of the sine wave
    double period;     // Period of the sine wave in seconds
    uint32_t delay_us; // Delay in microseconds for the main loop
};

// Default values, 2 different parameters sets ....
constexpr SignalParametersT signalParameters1 = {
    .ampl = 100.0,
    .phase = 0.0,
    .offset = 0.0,
    .period = 2.0,   // s
    .delay_us = 1000 // us
};
constexpr SignalParametersT signalParameters2 = {
    .ampl = 50.0,
    .phase = 1.0,
    .offset = 0.0,
    .period = 2.0,   // s
    .delay_us = 1000 // us
};

class SigGen {

  private:
    // signal_parameters_t *signal_parameters; // Parameters for the signal generators replaced by a wrapper to make them adjustable by the XCP tool
    // XCP calibration parameter struct wrapper
    xcplib::CalSeg<SignalParametersT> signal_parameters_; // Wrapper for signal parameters to enable calibration access

    const char *instance_name_; // Instance name
    std::thread *thread_;       // Thread for the signal generator task

  public:
    double value{0}; // Current value

    // Constructor - creates the signal generator with the given instance name and parameters
    SigGen(const char *instance_name, SignalParametersT params) : signal_parameters_(instance_name, params), instance_name_(instance_name) {

        // A2L registration
        A2lLock(); // Take care for thread safety when registering measurements and parameters

        // Once define a typedef for signal_parameters_t once
        // All typedef registration macros use a once pattern, so there will be exactly one typedef in the A2L file
        A2lTypedefBegin(SignalParametersT, "Signal parameters typedef");
        A2lTypedefParameterComponent(ampl, SignalParametersT, "Amplitude", "Volt", 0, 100);
        A2lTypedefParameterComponent(phase, SignalParametersT, "Phase", "", 0, k2Pi);
        A2lTypedefParameterComponent(offset, SignalParametersT, "Offset", "Volt", -100, 100);
        A2lTypedefParameterComponent(period, SignalParametersT, "Period", "s", 0.01, 10.0);
        A2lTypedefParameterComponent(delay_us, SignalParametersT, "Delay time in us", "us", 0, 100000);
        A2lTypedefEnd();

        // Create an instance of the signal parameters member variable in the CalSeg<> wrapper for calibration access
        // The wrapper class 'CalSeg' instance 'signal_parameters' created a calibration memory segment in its constructor
        A2lSetSegmentAddrMode(signal_parameters_.getIndex(), signal_parameters_);
        A2lCreateTypedefNamedInstance(instance_name, signal_parameters_, SignalParametersT, "Signal parameters");

        // Create a measurement event for each instance of SigGen
        // The event name is the instance name
        DaqCreateEvent_s(instance_name);

        // Register the member variable 'value' of this instance as measurement
        A2lSetRelativeAddrMode_s(instance_name, this);
        A2lCreatePhysMeasurementInstance(instance_name, value, "Signal generator output", "", -100, 100);

        // Start thread
        thread_ = new std::thread([this]() { task(); });
    }

    // Cyclic calculation function - runs in a separate thread
    void task() {

        double time = 0;
        uint32_t delay_us = 1000;                                                // us
        double start_time = static_cast<double>(clockGet()) / CLOCK_TICKS_PER_S; // time in s since start of the signal generator

        // A2L registration
        // Register the local (stack) measurement variable 'time' for measurement
        A2lLock();
        A2lSetStackAddrMode_s(instance_name_);
        A2lCreatePhysMeasurementInstance(instance_name_, time, "Signal generator time", "s", 0, 3600);
        A2lUnlock();

        for (;;) {

            time = static_cast<double>(clockGet()) / CLOCK_TICKS_PER_S - start_time; // time in s since start of the signal generator

            // Calculate a sine wave signal depending on the signal parameters
            {
                auto p = signal_parameters_.lock();
                value = p->offset + p->ampl * sin((time * k2Pi / p->period) + p->phase);
                delay_us = p->delay_us;
            }

            // XCP event by event name, event lookup is once and will be cached in a static thread local variable
            DaqEventRelative_s(instance_name_, this); // Trigger with this as dynamic addressing base to make member variables accessible

            sleepNs(delay_us * 1000);
        }
    }

    ~SigGen() {

        if (thread_ != nullptr) {
            if (thread_->joinable()) {
                thread_->join();
            }
            delete thread_;
        }
    }
};

} // namespace

//-----------------------------------------------------------------------------------------------------

int main() {

    std::cout << "\nXCP on Ethernet cpp_demo C++ xcplib demo\n" << std::endl;

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton
    XcpInit();

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        std::cerr << "Failed to initialize XCP server" << std::endl;
        return 1;
    }

    // Enable A2L generation
    if (!A2lInit(OPTION_A2L_FILE_NAME, OPTION_A2L_PROJECT_NAME, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, true, true)) {
        std::cerr << "Failed to initialize A2L generator" << std::endl;
        return 1;
    }

    // Create a calibration segment wrapper for the struct 'parameters_t' and use its default values in constant 'kParameters'
    // This calibration segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independent page switching, checksum calculation and reinitialization (copy reference page to working page)
    auto calseg = xcplib::CreateCalSeg("Parameters", parameters);

    // Register calibration parameters in this memory segment
    A2lSetSegmentAddrMode(calseg.getIndex(), parameters);
    A2lCreateParameter(parameters, counter_max, "Maximum counter value", "", 0, 2000);
    A2lCreateParameter(parameters, delay_us, "Mainloop delay time in us", "us", 0, 999999);

    // Create 2 signal generator instances of class SigGen with individual parameters
    SigGen siggen1("SigGen1", signalParameters1);
    SigGen siggen2("SigGen2", signalParameters2);
    A2lLock();
    A2lUnlock();

    // Create a measurement event 'mainloop'
    DaqCreateEvent(mainloop);

    // Register the global measurement variables 'temperature' and 'speed'
    A2lSetAbsoluteAddrMode(mainloop);
    const char *conv = A2lCreateLinearConversion(Temperature, "Temperature in °C from unsigned byte", "°C", 1.0, -50.0);
    A2lCreatePhysMeasurement(temperature, "Motor temperature in °C", conv, -50.0, 200.0);
    A2lCreatePhysMeasurement(speed, "Speed in km/h", "km/h", 0, 250.0);

    // Register local variables measurement 'loop_counter' and sum
    uint16_t loop_counter = 0;
    double sum = 0;
    A2lSetStackAddrMode(mainloop);
    A2lCreateMeasurement(loop_counter, "Loop counter, local measurement variable on stack", "");
    A2lCreateMeasurement(sum, "Sum of SigGen1 and SigGen2 value", "Volt");

    // Optional for testing: Force finalizing the A2L file, otherwise it will be finalized on XCP tool connect
    sleepMs(100);
    A2lFinalize();

    // Main loop
    std::cout << "Starting main loop..." << std::endl;
    uint32_t delay_us = parameters.delay_us; // Default
    for (;;) {

        // Access calibration parameters 'delay' and 'counter_max' safely
        // Use RAII guard for automatic lock/unlock the calibration parameter segment 'calseg'
        {
            auto parameters = calseg.lock();

            // Get the calibration parameter 'delay' parameter in microseconds
            delay_us = parameters->delay_us;

            // Increment a local measurement 'loop_counter' variable using a calibration parameter 'counter_max' as a limit
            loop_counter++;
            if (loop_counter > parameters->counter_max)
                loop_counter = 0;

        } // Guard automatically unlocks here

        // Update some measurement variables
        sum = siggen1.value + siggen2.value; // Add the values of the two signal generators (Note: this is not thread safe for simplicity)
        if (loop_counter == 0) {
            temperature += 1;
            if (temperature > 150)
                temperature = 0; // Reset temperature to -50 °C
        }
        speed += (250.0f - speed) * 0.0001;
        if (speed > 245.0f)
            speed = 0; // Reset speed to 0 km/h

        // XCP event
        // Trigger the measurement 'mainloop'
        DaqEvent(mainloop);

        // Use delay parameter - done outside the lock to avoid starvation of XCP tool calibration access
        sleepNs(delay_us * 1000);
    } // mainloop

    // Cleanup
    XcpDisconnect();
    XcpEthServerShutdown();

    return 0;
}
