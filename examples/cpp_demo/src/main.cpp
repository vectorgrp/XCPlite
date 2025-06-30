
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

//-----------------------------------------------------------------------------------------------------
// XCP parameters
#define OPTION_ENABLE_A2L_GENERATOR         // Enable A2L file generation
#define OPTION_A2L_PROJECT_NAME "cpp_demo"  // A2L project name
#define OPTION_A2L_FILE_NAME "cpp_demo.a2l" // A2L filename
#define OPTION_USE_TCP false                // TCP or UDP
#define OPTION_SERVER_PORT 5555             // Port
// #define OPTION_SERVER_ADDR {0, 0, 0, 0}     // Bind addr, 0.0.0.0 = ANY
#define OPTION_SERVER_ADDR {172, 19, 8, 57} // 172.19.8.57
#define OPTION_QUEUE_SIZE 1024 * 16         // Size of the measurement queue in bytes
#define OPTION_LOG_LEVEL 3

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

typedef struct {
    uint16_t counter_max; // Maximum value for the counter
    uint32_t delay_us;    // Sleep time in microseconds for the main loop
} parameters_t;

// Default values
const parameters_t parameters = {.counter_max = 1000, .delay_us = 1000};

//-----------------------------------------------------------------------------------------------------
// Demo measurement values

static uint8_t temperature = 50; // In Celsius
static float speed = 0.0f;       // Speed in km/h

//-----------------------------------------------------------------------------------------------------

int main(void) {

    std::cout << "\nXCP on Ethernet cpp_demo C++ xcplib demo\n" << std::endl;

    //-----------------------------------------------------------------------------------------------------
    // XCP Server Setup

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
#ifdef OPTION_ENABLE_A2L_GENERATOR
    if (!A2lInit(OPTION_A2L_FILE_NAME, OPTION_A2L_PROJECT_NAME, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, true, true)) {
        std::cerr << "Failed to initialize A2L generator" << std::endl;
        return 1;
    }
#else
    ApplXcpSetA2lName(OPTION_A2L_FILE_NAME);
#endif

    // Create a calibration segment wrapper for the struct 'parameters_t' and its default values in constant 'parameters'
    // This calibration segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independent page switching, checksum calculation and reinitialization (copy reference page to working page)
    auto calseg = xcplib::CreateCalSeg("Parameters", parameters);

    // Register calibration parameters in segment 'calseg'
    A2lSetSegmentAddrMode(calseg.getIndex(), parameters);
    A2lCreateParameter(parameters, counter_max, "Maximum counter value", "", 0, 2000);
    A2lCreateParameter(parameters, delay_us, "Mainloop delay time in us", "us", 0, 999999);

    // Create a measurement event 'mainloop'
    DaqCreateEvent(mainloop);

    // Register global measurement variables 'temperature' and 'speed'
    A2lSetAbsoluteAddrMode(mainloop);
    const char *conv = A2lCreateLinearConversion(Temperature, "Temperature in °C from unsigned byte", "°C", 1.0, -50.0);
    A2lCreatePhysMeasurement(temperature, "Motor temperature in °C", conv, -50.0, 200.0);
    A2lCreatePhysMeasurement(speed, "Speed in km/h", "km/h", 0, 250.0);

    // Register local measurement variable 'loop_counter'
    uint16_t loop_counter = 0;
    A2lSetStackAddrMode(mainloop);
    A2lCreateMeasurement(loop_counter, "Loop counter, local measurement variable on stack", "");

    A2lFinalize();

    std::cout << "Starting main loop..." << std::endl;

    //-----------------------------------------------------------------------------------------------------
    // Main application loop demonstrating RAII guard pattern

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
            if (loop_counter > parameters->counter_max) {
                loop_counter = 0;
            }

        } // Guard automatically unlocks here

        // Update global measurement variables
        temperature = static_cast<uint8_t>(50 + 0.001);
        speed += (250.0f - speed) * 0.001;

        // Trigger measurement events
        DaqEvent(mainloop);

        // Use delay parameter - done outside the lock to allow XCP modifications
        sleepNs(delay_us * 1000);
    } // mainloop

    // Cleanup
    XcpDisconnect();
    XcpEthServerShutdown();

    return 0;
}
