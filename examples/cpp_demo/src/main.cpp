
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "a2l.h"
#include "platform.h"
#include "xcplib.h"
#include "xcplib.hpp"
#include <thread>

#define M_2PI (M_PI * 2)

//-----------------------------------------------------------------------------------------------------
// XCP parameters
#define OPTION_ENABLE_A2L_GENERATOR         // Enable A2L file generation
#define OPTION_A2L_PROJECT_NAME "cpp_demo"  // A2L project name
#define OPTION_A2L_FILE_NAME "cpp_demo.a2l" // A2L filename
#define OPTION_USE_TCP false                // TCP or UDP
#define OPTION_SERVER_PORT 5555             // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}     // Bind addr, 0.0.0.0 = ANY
// #define OPTION_SERVER_ADDR {172, 19, 8, 57} // 172.19.8.57
// #define OPTION_SERVER_ADDR {192, 168, 0, 128} // 192.168.0.128
#define OPTION_QUEUE_SIZE 1024 * 16 // Size of the measurement queue in bytes
#define OPTION_LOG_LEVEL 4

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

typedef struct {
    uint16_t counter_max; // Maximum value for the counter
    uint32_t delay_us;    // Sleep time in microseconds for the main loop
} parameters_t;

// Default values
const parameters_t parameters = {.counter_max = 1000, .delay_us = 1000};

//-----------------------------------------------------------------------------------------------------
// Demo global measurement values

static uint8_t temperature = 50; // In Celsius
static float speed = 0.0f;       // Speed in km/h

//-----------------------------------------------------------------------------------------------------
// Waveform generator class

/*
An instance of class SigGen creates a sine signal in its member variable value
Depending on calibration parameters ampl, phase, offset and period
*/

typedef struct {
    double ampl;       // Amplitude of the sine wave
    double phase;      // Phase shift in radians
    double offset;     // Offset of the sine wave
    double period;     // Period of the sine wave in seconds
    uint32_t delay_us; // Delay in microseconds for the main loop
} signal_parameters_t;

// Default values, 2 diffents parameters sets
const signal_parameters_t signal_parameters_1 = {
    .ampl = 100.0,
    .phase = 0.0,
    .offset = 0.0,
    .period = 2.0,   // s
    .delay_us = 1000 // us
};
const signal_parameters_t signal_parameters_2 = {
    .ampl = 50.0,
    .phase = 1.0,
    .offset = 0.0,
    .period = 2.0,   // s
    .delay_us = 1000 // us
};

class SigGen {

  private:
    std::thread *t;                                        // Thread for the signal generator task
    xcplib::CalSeg<signal_parameters_t> signal_parameters; // Wrapper for signal parameters to enable calibration access
    tXcpEventId event;                                     // Event for measurement updates
    const char *instance_name;                             // Instance name

  public:
    double value; // Current value

    // Constructor - creates the signal generator with the given instance name and parameters
    SigGen(const char *instance_name, signal_parameters_t params) : signal_parameters(instance_name, params), value(0), instance_name(instance_name) {

        A2lLock(); // Take care for thread safety when registering measurements and parameters

        // Define a typedef for signal_parameters_t once (this is thread safe)
        A2lTypedefBegin(signal_parameters_t, "Signal parameters typedef");
        A2lTypedefParameterComponent(ampl, signal_parameters_t, "Amplitude", "Volt", 0, 100);
        A2lTypedefParameterComponent(phase, signal_parameters_t, "Phase", "", 0, M_PI);
        A2lTypedefParameterComponent(offset, signal_parameters_t, "Offset", "Volt", -100, 100);
        A2lTypedefParameterComponent(period, signal_parameters_t, "Period", "s", 0.01, 10.0);
        A2lTypedefParameterComponent(delay_us, signal_parameters_t, "Delay time in us", "us", 0, 100000);
        A2lTypedefEnd();

        // Create an instance of the signal parameters in the CalSeg wrapper for calibration access
        // The wrapper class 'CalSeg' instance 'signal_parameters' created a calibration memory segment in its constructor
        A2lSetSegmentAddrMode(signal_parameters.getIndex(), signal_parameters);
        A2lCreateTypedefNamedInstance(instance_name, signal_parameters, signal_parameters_t, "Signal parameters");

        // Create a measurement event for each instance of SigGen
        event = XcpCreateEvent(instance_name, 0, 0);

        // Register member variables of this instance as measurements
        A2lSetRelativeAddrMode_s(instance_name, (const uint8_t *)this);
        A2lCreatePhysMeasurementInstance(instance_name, value, "Signal generator output", "", -100, 100);

        A2lUnlock();

        // Start thread
        t = new std::thread([this]() { task(); });
    }

    // Cyclic calculation function - runs in a separate thread
    void task(void) {

        uint32_t delay_us = 1000; // us
        double time, start_time;  // s

        start_time = (double)clockGet() / CLOCK_TICKS_PER_S; // time in s since start of the signal generator

        // Register the local (stack) measurement variable 'time' for measurement
        A2lLock();
        A2lSetStackAddrMode_s(instance_name);
        A2lCreatePhysMeasurementInstance(instance_name, time, "Signal generator time", "s", 0, 3600);
        A2lUnlock();

        for (;;) {

            time = (double)clockGet() / CLOCK_TICKS_PER_S - start_time; // time in s since start of the signal generator

            // Calculate a sine wave signal depending in the signal parameters
            {
                auto p = signal_parameters.lock();
                value = p->offset + p->ampl * sin((time - start_time) * M_2PI / p->period + p->phase);
                delay_us = p->delay_us;
            }

            DaqEventRelative_i(event, (const uint8_t *)this); // Trigger with this as dynamic addressing base to make the member variables accessible

            sleepNs(delay_us * 1000);
        }
    }

    ~SigGen() { delete t; }
};

//-----------------------------------------------------------------------------------------------------

int main(void) {

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

    // Create a calibration segment wrapper for the struct 'parameters_t' and use its default values in constant 'parameters'
    // This calibration segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independent page switching, checksum calculation and reinitialization (copy reference page to working page)
    auto calseg = xcplib::CreateCalSeg("Parameters", parameters);

    // Register calibration parameters in this memory segment
    A2lSetSegmentAddrMode(calseg.getIndex(), parameters);
    A2lCreateParameter(parameters, counter_max, "Maximum counter value", "", 0, 2000);
    A2lCreateParameter(parameters, delay_us, "Mainloop delay time in us", "us", 0, 999999);

    // Create a measurement event 'mainloop'
    DaqCreateEvent(mainloop);

    // Register the global measurement variables 'temperature' and 'speed'
    A2lSetAbsoluteAddrMode(mainloop);
    const char *conv = A2lCreateLinearConversion(Temperature, "Temperature in °C from unsigned byte", "°C", 1.0, -50.0);
    A2lCreatePhysMeasurement(temperature, "Motor temperature in °C", conv, -50.0, 200.0);
    A2lCreatePhysMeasurement(speed, "Speed in km/h", "km/h", 0, 250.0);

    // Register a local measurement variable 'loop_counter'
    uint16_t loop_counter = 0;
    A2lSetStackAddrMode(mainloop);
    A2lCreateMeasurement(loop_counter, "Loop counter, local measurement variable on stack", "");

    // Create 2 signal generator instances of class SigGen with individual parameters
    SigGen siggen1("SigGen1", signal_parameters_1);
    SigGen siggen2("SigGen2", signal_parameters_2);

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

        // Update global measurement variables
        if (loop_counter == 0) {
            temperature += 1;
            if (temperature > 150)
                temperature = 0; // Reset temperature to -50 °C
        }
        speed += (250.0f - speed) * 0.0001;
        if (speed > 245.0f)
            speed = 0; // Reset speed to 0 km/h

        // Trigger the measurement 'mainloop'
        DaqEvent(mainloop);

        // Use delay parameter - done outside the lock to allow XCP modifications
        sleepNs(delay_us * 1000);
    } // mainloop

    // Cleanup
    XcpDisconnect();
    XcpEthServerShutdown();

    return 0;
}
