// hello_xcp xcplib example

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "a2l.h"    // for xcplib A2l generation
#include "xcplib.h" // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP params

#define OPTION_PROJECT_NAME "hello_xcp" // Project name, used to build the A2L and BIN file name
#define OPTION_USE_TCP true             // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 16     // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3              // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

// Calibration parameters structure
typedef struct params {
    uint16_t counter_max; // Maximum value for the counter
    uint32_t delay_us;    // Sleep time in microseconds for the main loop
    float acceleration;   // Acceleration in m/s^2
} parameters_t;

// Default values (reference page, "FLASH") for the calibration parameters
const parameters_t params = {.counter_max = 1024, .delay_us = 1000, .acceleration = 0.01f};

// A global calibration segment handle for the calibration parameters
// A calibration segment has a working page ("RAM") and a reference page ("FLASH"), it is described by a MEMORY_SEGMENT in the A2L file
// Using the calibration segment to access parameters assures safe (thread safe against XCP modifications), wait-free and consistent access
// It supports RAM/FLASH page switching, reinitialization (copy FLASH to RAM page) and persistence (save RAM page to BIN file)
tXcpCalSegIndex calseg = XCP_UNDEFINED_CALSEG;

//-----------------------------------------------------------------------------------------------------
// Demo global measurement values

uint8_t temperature = 50; // Temperature in Deg Celcius as Byte, 0 is -55 °C, 255 is +200 °C
float speed = 0.0f;       // Speed in km/h

//-----------------------------------------------------------------------------------------------------
// Demo function with XCP instrumentation

float calc_speed(float current_speed) {

    float new_speed = 0.0f;

    // XCP: Create a measurement event and once register local measurement variables for current_speed and new_speed
    DaqCreateEvent(calc_speed);
    A2lOnce() {
        A2lSetStackAddrMode(calc_speed); // Set stack relative addressing mode with fixed event speed
        A2lCreatePhysMeasurementInstance("calc_speed", current_speed, "Parameter current_speed in function calculate_speed", "km/h", 0, 250.0);
        A2lCreatePhysMeasurementInstance("calc_speed", new_speed, "Loop counter, local measurement variable on stack", "km/h", 0, 250.0);
    }

    // XCP: Lock access to calibration parameters
    parameters_t *params = (parameters_t *)XcpLockCalSeg(calseg);

    // Calculate new speed based on acceleration and sample rate
    new_speed = (float)(current_speed + params->acceleration * params->delay_us * 3.6 / 1000000.0); // km/h
    if (new_speed < 0.0f) {
        new_speed = 0.0f;
    } else if (new_speed > 250.0f) {
        new_speed = 250.0f; // Limit speed to 250 km/h
    }

    // XCP: Unlock the calibration segment
    XcpUnlockCalSeg(calseg);

    // XCP: Trigger the measurement event "calc_speed"
    DaqEvent(calc_speed);

    return new_speed;
}

//-----------------------------------------------------------------------------------------------------
// Demo main

static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

int main(void) {

    printf("\nXCP on Ethernet hello_xcp C xcplib demo\n");
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // XCP: Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // XCP: Initialize the XCP singleton, activate XCP, must be called before starting the server
    // If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    XcpInit(true);

    // XCP: Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // XCP: Enable inline A2L generation
    // In WRITE_ONCE mode:
    //   If the A2l file aready exists, check if software version (EPK) still matches and load calibration values from the binary persistence file (.bin)
    //   If not, create a new A2L file (.a2l) and binary persistence file (.bin) with default calibration values
    // In WRITE_ALWAYS mode:
    //   Recreate the A2L file on each application start, calibration values will be initialized to default
    //   Binary persistence is not supported
    // Finalize the A2L file on XCP connect
    // Optionally create A2L groups for calibration segments and events
    if (!A2lInit(OPTION_PROJECT_NAME, NULL /* EPK */, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    // XCP: Create a calibration segment named 'Parameters' for the calibration parameter struct instance 'params' as reference page
    calseg = XcpCreateCalSeg("Parameters", &params, sizeof(params));

    // XCP: Register the calibration parameters in the calibration segment
    A2lSetSegmentAddrMode(calseg, params);
    A2lCreateParameter(params.counter_max, "Maximum counter value", "", 0, 2000);
    A2lCreateParameter(params.delay_us, "Mainloop delay time in us", "us", 0, 999999);
    A2lCreateParameter(params.acceleration, "Acceleration", "m/(s*s)", -10, 10);

    // XCP: Create a measurement event named "mainloop"
    DaqCreateEvent(mainloop);

    // XCP: Register global measurement variables (temperature, speed)
    A2lSetAbsoluteAddrMode(mainloop);
    A2lCreateLinearConversion(temperature, "Temperature in °C from unsigned byte", "°C", 1.0, -55.0);
    A2lCreatePhysMeasurement(temperature, "Motor temperature in °C", "conv.temperature", -55.0, 200.0);
    A2lCreatePhysMeasurement(speed, "Speed in km/h", "km/h", 0, 250.0);

    // XCP: Register a local measurement variable (loop_counter)
    uint16_t loop_counter = 0;
    A2lSetStackAddrMode(mainloop); // Set stack relative addressing mode with fixed event mainloop
    A2lCreateMeasurement(loop_counter, "Loop counter, local measurement variable on stack");

    // Mainloop
    printf("Start main loop...\n");
    while (running) {
        // XCP: Lock the calibration parameter segment for consistent and safe access
        // Calibration segment locking is wait-free, locks may be recursive
        // Returns a pointer to the active page (working or reference) of the calibration segment
        parameters_t *params = (parameters_t *)XcpLockCalSeg(calseg);

        uint32_t delay_us = params->delay_us; // Get the delay calibration parameter in microseconds

        // Local variables
        loop_counter++;
        if (loop_counter > params->counter_max) { // Get the counter_max calibration value and reset loop_counter
            loop_counter = 0;
        }

        // Global measurement variables
        temperature = 50 + 21;
        speed = calc_speed(speed); // Demo function to calculate a new speed based on the current speed and acceleration

        // XCP: Unlock the calibration segment
        XcpUnlockCalSeg(calseg);

        // XCP: Trigger the measurement event "mainloop"
        DaqEvent(mainloop);

        // Sleep for the specified delay parameter in microseconds, don't sleep with the XCP lock held to give the XCP client a chance to update params
        sleepUs(delay_us);

        A2lFinalize(); // @@@@ Test: Manually finalize the A2L file to make it visible without XCP tool connect

    } // for (;;)

    // XCP: Force disconnect the XCP client
    XcpDisconnect();

    // XCP: Stop the XCP server
    XcpEthServerShutdown();

    return 0;
}
