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
#define OPTION_PROJECT_EPK __TIME__     // EPK version string
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
    float flow_rate;      // Flow rate in m3/h
} parameters_t;

// Default values (reference page, "FLASH") for the calibration parameters
const parameters_t params = {.counter_max = 1024, .delay_us = 1000, .flow_rate = 0.300f};

// A global calibration segment handle for the calibration parameters
// A calibration segment has a working page ("RAM") and a reference page ("FLASH"), it is described by a MEMORY_SEGMENT in the A2L file
// Using the calibration segment to access parameters assures safe (thread safe against XCP modifications), wait-free and consistent access
// It supports RAM/FLASH page switching, reinitialization (copy FLASH to RAM page) and persistence (save RAM page to BIN file)
tXcpCalSegIndex calseg = XCP_UNDEFINED_CALSEG;

//-----------------------------------------------------------------------------------------------------
// Demo global measurement values

// Temperatures are in Deg Celcius as Byte, 0 is -55 °C, 255 is +200 °C
uint8_t outside_temperature = -5 + 55;
uint8_t inside_temperature = 20 + 55;
// Heat Energy in kW
double heat_energy = 0.0f;

uint32_t global_counter = 0;

//-----------------------------------------------------------------------------------------------------
// Read sensor values

// Simulate reading the outside temperature sensor, by writing to the global variable
#define read_outside_sensor() (outside_temperature)
#define read_inside_sensor() (inside_temperature)

//-----------------------------------------------------------------------------------------------------
// Demo function with XCP instrumentation

// Calculate heat power from temperature difference and flow rate calibration parameter
// Temperatures are in uint8_t in Deg Celsius offset by -55 °C, conversion rule identifier is "conv.temperature"
float calc_power(uint8_t t1, uint8_t t2) {

    double diff_temp = (double)t2 - (double)t1; // Diff temperature in Kelvin
    double heat_power = diff_temp * 10.0f;      // Heat power in kW

    // XCP: Create a measurement event and once register local measurement variables for current_speed and new_speed
    DaqCreateEvent(calc_power);
    A2lOnce() {
        A2lSetStackAddrMode(calc_power); // Set stack relative addressing mode with fixed event heat_power
        A2lCreatePhysMeasurementInstance("calc_power", t1, "Parameter t1 in function calc_power", "conv.temperature", -55.0, 200.0);
        A2lCreatePhysMeasurementInstance("calc_power", t2, "Parameter t2 in function calc_power", "conv.temperature", -55.0, 200.0);
        A2lCreatePhysMeasurementInstance("calc_power", diff_temp, "Local variable diff temperature in function calc_power", "K", -100.0, 100.0);
        A2lCreatePhysMeasurementInstance("calc_power", heat_power, "Local variable calculated heat power in function calc_power", "W", 0.0, 10.0);
    }

    // XCP: Lock access to calibration parameters
    const parameters_t *params = (parameters_t *)XcpLockCalSeg(calseg);

    heat_power = diff_temp * params->flow_rate * 1000.0 * 1.16; // in kWh, 1.16Wh per K per liter - calculate heat power using the flow rate calibration parameter

    // XCP: Unlock the calibration segment
    XcpUnlockCalSeg(calseg);

    // XCP: Trigger the measurement event "calc_power"
    DaqTriggerEvent(calc_power);

    return heat_power;
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
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_EPK, true);

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
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    // XCP: Create a calibration segment named 'Parameters' for the calibration parameter struct instance 'params' as reference page
    calseg = XcpCreateCalSeg("params", &params, sizeof(params));

    // XCP: Option1: Register the individual calibration parameters in the calibration segment
    A2lSetSegmentAddrMode(calseg, params);
    A2lCreateParameter(params.counter_max, "Maximum counter value", "", 0, 2000);
    A2lCreateParameter(params.delay_us, "Mainloop delay time in us", "us", 0, 999999);
    A2lCreateParameter(params.flow_rate, "Flow rate", "m3/h", 0.0, 2.0);

    // XCP: Option2: Register the calibration segment as a typedef instance
    // A2lTypedefBegin(parameters_t, "Calibration parameters typedef");
    // A2lTypedefParameterComponent(counter_max, parameters_t, "Maximum counter value", "", 0, 2000);
    // A2lTypedefParameterComponent(delay_us, parameters_t, "Mainloop delay time in us", "us", 0, 999999);
    // A2lTypedefParameterComponent(flow_rate, parameters_t, "Flow rate", "m3/h", 0.0, 2.0);
    // A2lTypedefEnd();
    // A2lSetSegmentAddrMode(calseg, params);
    // A2lCreateTypedefInstance(params, parameters_t, "Calibration parameters");

    // XCP: Create a measurement event named "mainloop"
    DaqCreateEvent(mainloop);

    // XCP: Register global measurement variables
    A2lSetAbsoluteAddrMode(mainloop);
    A2lCreateLinearConversion(temperature, "Temperature in °C from unsigned byte", "C", 1.0, -55.0);
    A2lCreatePhysMeasurement(outside_temperature, "Temperature in °C read from outside sensor", "conv.temperature", -20, 50);
    A2lCreatePhysMeasurement(inside_temperature, "Temperature in °C read from inside sensor", "conv.temperature", 0, 40);
    A2lCreatePhysMeasurement(heat_energy, "Accumulated heat energy in kWh", "kWh", 0.0, 10000.0);
    A2lCreateMeasurement(global_counter, "Global free running counter");

    // XCP: Register local measurement variables
    uint16_t loop_counter = 0;
    A2lSetStackAddrMode(mainloop); // Set stack relative addressing mode with fixed event mainloop
    A2lCreateMeasurement(loop_counter, "Loop counter, local measurement variable on stack");

    // Mainloop
    printf("Start main loop...\n");
    while (running) {
        // XCP: Lock the calibration parameter segment for consistent and safe access
        // Calibration segment locking is wait-free, locks may be recursive
        // Returns a pointer to the active page (working or reference) of the calibration segment
        const parameters_t *params = (parameters_t *)XcpLockCalSeg(calseg);

        uint32_t delay_us = params->delay_us; // Get the delay calibration parameter in microseconds

        // Local variables
        loop_counter++;
        if (loop_counter > params->counter_max) { // Get the counter_max calibration value and reset loop_counter
            printf("%u: params.counter_max = %u\n", global_counter, params->counter_max);
            loop_counter = 0;
        }

        // Global variables
        global_counter++;
        inside_temperature = read_inside_sensor();
        outside_temperature = read_outside_sensor();
        double heat_power = calc_power(outside_temperature, inside_temperature); // Demo function to calculate heat power in W
        heat_energy += heat_power / 1000.0 * (double)delay_us / 3600e6;          // Integrate heat energy in kWh in a global measurement variable, kWh = W/1000  * us/ 3600e6

        // XCP: Unlock the calibration segment
        XcpUnlockCalSeg(calseg);

        // XCP: Trigger the measurement event "mainloop"
        DaqTriggerEvent(mainloop);

        // Sleep for the specified delay parameter in microseconds, don't sleep with the XCP lock held to give the XCP client a chance to update params
        sleepUs(delay_us);

        A2lFinalize(); // @@@@ TEST: Manually finalize the A2L file to make it visible without XCP tool connect

    } // for (;;)

    // XCP: Force disconnect the XCP client
    XcpDisconnect();

    // XCP: Stop the XCP server
    XcpEthServerShutdown();

    return 0;
}
