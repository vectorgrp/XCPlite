// cpp_demo xcplib C++ example

#include <cstdint> // for uintxx_t
#include <iostream>

#include "platform.h"

#include "a2l.hpp"
#include "xcplib.hpp"

#include "sig_gen.hpp"

namespace {

//-----------------------------------------------------------------------------------------------------
// XCP parameters
#define OPTION_PROJECT_NAME "cpp_demo"  // A2L project name
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 64)   // Size of the measurement queue in bytes
#define OPTION_LOG_LEVEL 3

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

struct ParametersT {
    uint16_t counter_max; // Maximum value for the counter
    uint32_t delay_us;    // Sleep time in microseconds for the main loop
};

// Default values
constexpr ParametersT kParameters = {.counter_max = 1000, .delay_us = 1000};

//-----------------------------------------------------------------------------------------------------
// Demo global measurement values

uint8_t temperature = 50; // In Celsius
double speed = 0.0f;      // Speed in km/h

} // namespace

//-----------------------------------------------------------------------------------------------------
// Demo signal generator class

constexpr double kPi = 3.14159265358979323846;
constexpr double k2Pi = (kPi * 2);

// Default parameter values for multiple instances
const signal_generator::SignalParametersT kSignalParameters1 = {
    .ampl = 12.5,
    .phase = 0.0,
    .offset = 0.0,
    .period = 0.4, // s
#ifdef CANAPE_24
    .lookup =
        {
            .values = {0.0, 0.5, 1.0, 0.50, 0.00, -0.5, -1, -0.5, 0, 0.0, 0.0},
            .axis = {0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0},
        },
#endif
    .delay_us = 1000,                                   // us
    .signal_type = signal_generator::SignalTypeT::SINE, // Type of the signal
};
const signal_generator::SignalParametersT kSignalParameters2 = {
    .ampl = 80.0,
    .phase = kPi / 2,
    .offset = 0.0,
    .period = 10.0, // s
#ifdef CANAPE_24
    .lookup =
        {
            .values = {0.0, 0.10, 0.30, 0.60, 0.80, 1.00, 0.80, 0.60, 0.30, 0.10, 0.0},
            .axis = {0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0},
        },
#endif
    .delay_us = 1000,                                   // us
    .signal_type = signal_generator::SignalTypeT::SINE, // Type of the signal
};

//-----------------------------------------------------------------------------------------------------

int main() {

    std::cout << "\nXCP on Ethernet cpp_demo C++ xcplib demo\n" << std::endl;

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, activate XCP, must be called before starting the server
    // If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    XcpInit(true);

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        std::cerr << "Failed to initialize XCP server" << std::endl;
        return 1;
    }

    // Enable A2L generation
    // Set mode to write once to create stable A2L files, this also enables calibration segment persistence and freeze support
    if (!A2lInit(OPTION_PROJECT_NAME, NULL, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ONCE | A2L_MODE_AUTO_GROUPS)) {
        std::cerr << "Failed to initialize A2L generator" << std::endl;
        return 1;
    }

    // Create a calibration segment wrapper for the struct 'parameters_t' and use its default values in constant 'kParameters'
    // This calibration segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independent page switching, checksum calculation and reinitialization (copy reference page to working page)
    auto calseg = xcplib::CreateCalSeg("Parameters", kParameters);

    // Add the calibration segment description as a typedef instance to the A2L file
    A2lTypedefBegin(ParametersT, "A2L Typedef for ParametersT");
    A2lTypedefParameterComponent(counter_max, ParametersT, "Maximum counter value", "", 0, 2000);
    A2lTypedefParameterComponent(delay_us, ParametersT, "Mainloop delay time in us", "us", 0, 999999);
    A2lTypedefEnd();
    calseg.CreateA2lTypedefInstance("ParametersT", "Main parameters");

    // Local variables
    uint16_t loop_counter = 0;
    double sum = 0;

    // Create a measurement event 'mainloop'
    DaqCreateEvent(mainloop);

    // Register the global measurement variables 'temperature' and 'speed'
    A2lSetAbsoluteAddrMode(mainloop);
    A2lCreateLinearConversion(temperature, "Temperature in °C from unsigned byte", "°C", 1.0, -50.0);
    A2lCreatePhysMeasurement(temperature, "Motor temperature in °C", "conv.temperature", -50.0, 200.0);
    A2lCreatePhysMeasurement(speed, "Speed in km/h", "km/h", 0, 250.0);

    // Register the local measurement variables 'loop_counter' and sum
    A2lSetStackAddrMode(mainloop);
    A2lCreateMeasurement(loop_counter, "Loop counter, local measurement variable on stack");
    A2lCreateMeasurement(sum, "Sum of SigGen1 and SigGen2 value");

    // Signal generator class demo
    // See sig_gen.cpp for details how to measure instance member variables and stack variables in member functions
    // Create 2 signal generator instances of class SignalGenerator with individual parameters
    // Note that the signal generator threads register measurements in the A2L file as well
    // This is not in conflict because the main thread has already registered its measurements above
    // Otherwise use A2lLock() and A2lUnlock() to avoid race conditions when registering measurements, the A2L generator for measurements is not thread safe by itself
    signal_generator::SignalGenerator signal_generator_1("SigGen1", kSignalParameters1);
    signal_generator::SignalGenerator signal_generator_2("SigGen2", kSignalParameters2);

    // Optional for testing: Force finalizing the A2L file, otherwise it will be finalized on XCP tool connect
    sleepMs(100);
    A2lFinalize();

    // Main loop
    std::cout << "Starting main loop..." << std::endl;
    uint32_t delay_us = kParameters.delay_us; // Default
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

        // Sum the values of signal generator 1+2 into the local variable sum
        sum = signal_generator_1.GetValue() + signal_generator_2.GetValue();

        // Update some more local and global variables
        if (loop_counter == 0) {
            temperature += 1;
            if (temperature > 150)
                temperature = 0; // Reset temperature to -50 °C
        }
        speed += (250.0f - speed) * 0.0001;
        if (speed > 245.0f)
            speed = 0; // Reset speed to 0 km/h

        // Trigger the XCP measurement mainloop for temperature, speed, loop_counter and sum
        DaqEvent(mainloop);

        // Use delay parameter - done outside the lock to avoid starvation of XCP tool calibration access
        sleepNs(delay_us * 1000);
    } // mainloop

    // Cleanup
    XcpDisconnect();
    XcpEthServerShutdown();

    return 0;
}
