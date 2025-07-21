// hello_xcp xcplib example

#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "a2l.h"      // for xcplib A2l generation
#include "platform.h" // for sleepNs
#include "xcplib.h"   // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------

// XCP params
#define OPTION_PROJECT_NAME "hello_xcp" // Project name, used to build the A2L and BIN file name
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 16     // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3

//-----------------------------------------------------------------------------------------------------
// Demo calibration params

typedef struct params {
    uint16_t counter_max; // Maximum value for the counter
    uint32_t delay_us;    // Sleep timein microseconds for the main loop
} parameters_t;

// Default values
const parameters_t params = {.counter_max = 1000, .delay_us = 1000};

//-----------------------------------------------------------------------------------------------------
// Demo measurement values

static uint8_t temperature = 50; // In Celcius, 0 is -55 °C, 255 is +200 °C
static float speed = 0.0f;       // Speed in km/h

//-----------------------------------------------------------------------------------------------------

// Demo main
int main(void) {

    printf("\nXCP on Ethernet hello_xcp C xcplib demo\n");

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, activate XCP, must be called before starting the server
    // If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    XcpInit(true);

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Enable A2L generation
    // If the A2l file aready exists, check if EPK matches and load the binary persistence file
    // If not, prepare the A2L file, finalize the A2L file on XCP connect
    if (!A2lInit(OPTION_PROJECT_NAME, NULL, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, true /* force_generation*/, true /* finalize_on_connect*/, true /* enable auto grouping*/)) {
        return 1;
    }

    // Create a calibration segment named 'Parameters' for the calibration parameter struct
    // This calibration segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration params
    // It supports XCP/ECU independent page switching, checksum calculation and reinitialization (copy reference page to working page)
    tXcpCalSegIndex calseg = XcpCreateCalSeg("Parameters", &params, sizeof(params));
    assert(calseg != XCP_UNDEFINED_CALSEG); // Ensure the calibration segment was created successfully

    // Register calibration params in the calibration segment
    A2lSetSegmentAddrMode(calseg, params);
    A2lCreateParameter(params.counter_max, "Maximum counter value", "", 0, 2000);
    A2lCreateParameter(params.delay_us, "Mainloop delay time in us", "us", 0, 999999);

    // Create a measurement event named "mainloop"
    DaqCreateEvent(mainloop);

    // Register global measurement variables (temperature, speed)
    // Set absolute addressing mode with default event mainloop
    A2lSetAbsoluteAddrMode(mainloop);
    A2lCreateLinearConversion(temperature_conversion, "Temperature in °C from unsigned byte", "°C", 1.0, -50.0);
    A2lCreatePhysMeasurement(temperature, "Motor temperature in °C", temperature_conversion, -50.0, 200.0);
    A2lCreatePhysMeasurement(speed, "Speed in km/h", "km/h", 0, 250.0);

    // Register a local measurement variable (loop_counter)
    uint16_t loop_counter = 0;
    A2lSetStackAddrMode(mainloop); // Set stack relative addressing mode with fixed event mainloop
    A2lCreateMeasurement(loop_counter, "Loop counter, local measurement variable on stack", "");

    A2lFinalize(); // Optional: Finalize the A2L file generation early, otherwise it would be written when the client tool connects

    // Mainloop
    printf("Start main loop...\n");
    for (;;) {
        // Lock the calibration parameter segment for consistent and safe access
        // Calibration segment locking is completely lock-free and wait-free (no mutexes, system calls or CAS operations )
        // It returns a pointer to the active page (working or reference) of the calibration segment
        parameters_t *params = (parameters_t *)XcpLockCalSeg(calseg);

        uint32_t delay_us = params->delay_us; // Get the delay parameter in microseconds

        // Local variable for measurement
        loop_counter++;
        if (loop_counter > params->counter_max) {
            loop_counter = 0;
        }

        // Global measurement variables
        temperature = 50 + 21;
        speed += (250 - speed) * 0.00001;

        // Unlock the calibration segment
        XcpUnlockCalSeg(calseg);

        // Trigger measurement events
        DaqEvent(mainloop);

        // Sleep for the specified delay parameter in microseconds, don't sleep with the XCP lock held to give the XCP client a chance to calibrate the params
        sleepNs(delay_us * 1000);

    } // for (;;)

    // Force disconnect the XCP client
    XcpDisconnect();

    // Stop the XCP server
    XcpEthServerShutdown();

    return 0;
}
