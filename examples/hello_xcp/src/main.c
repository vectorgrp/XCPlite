// hello_xcp - simple XCPlite/libxcplite C example

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

// Include XCPlite/libxcplite C headers
#include <a2l.h>    // for A2l generation
#include <xcplib.h> // for application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP params

#define OPTION_PROJECT_NAME "hello_xcp" // Project name, used to build the A2L and BIN file name
#define OPTION_PROJECT_VERSION "V2.3"   // EPK version string
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 32)   // Size of the measurement queue in bytes, should be large enough to cover at least 10ms of expected traffic
#define OPTION_LOG_LEVEL 4              // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = print XCP commands

// XCP mode:
#ifdef OPTION_SHM_MODE
#define OPTION_XCP_MODE (XCP_MODE_PERSISTENCE | XCP_MODE_SHM_AUTO) // XCP multi application mode, leader becomes XCP server
#else
#define OPTION_XCP_MODE (XCP_MODE_PERSISTENCE | XCP_MODE_LOCAL) // XCP single application server mode
#endif

// A2L generation mode:
// A2L_MODE_WRITE_ONCE:
//   If the A2l file aready exists, check if software version (EPK) still matches and load calibration values from the binary persistence file (.bin)
//   If not, create a new A2L file (.a2l) and binary persistence file (.bin) with default calibration values
// A2L_MODE_WRITE_ALWAYS:
//   Recreate the A2L file on each application start, calibration values will always be initialized to default
//   Binary persistence is not supported
// A2L_MODE_FINALIZE_ON_CONNECT:
//   Finalize the A2L file on XCP connect
// A2L_MODE_AUTO_GROUPS:
//   Optionally create A2L groups for calibration segments and events
#define OPTION_A2L_MODE (A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)

// New option in V1.1: Enable variadic all in one macros for simple arithmetic types, see examples below
#define OPTION_USE_VARIADIC_MACROS

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

// A calibration parameter struct
typedef struct params {
    uint32_t delay_us;    // Mainloop delay time in us
    uint16_t counter_max; // Maximum value for the counter
    float flow_rate;      // Flow rate in m3/h
} params_t;

// Default values (reference page, "FLASH") for the calibration parameters
const params_t params = {.delay_us = 1000, .counter_max = 1024, .flow_rate = 0.301f};

// A global calibration segment index (handle for the calibration parameter segment for `params`).
// The calibration parameter segment can be shared and accessed lock-less in in multiple threads.
// A calibration segment has a working page ("RAM") and a reference page ("FLASH"), it is described by a MEMORY_SEGMENT in the A2L file.
// Using the calibration segment to access parameters assures safe (thread safe against XCP modifications), wait-free and consistent access.
// It supports offline calibration, RAM/FLASH page switching, reinitialization (copy FLASH to RAM page) and persistence (save to BIN file).
// The segment is created in main() after XcpInit(), CalSegLock(params) uses this index in all functions.
static tXcpCalSegIndex calseg_id_params = XCP_UNDEFINED_CALSEG;

// The example code shows 2 variants: Using the CalXxx macros or using the explicit CalSegXxx functions.
// The macros provide the option to deactivate XCP by XcpInit(XCP_MODE_DEACTIVATE), without any other code changes.

//-----------------------------------------------------------------------------------------------------
// Demo global measurement values

// Temperatures are in Deg Celsius as Byte, 0 is -55 °C, 255 is +200 °C
uint8_t outside_temperature = -5 + 55;
uint8_t inside_temperature = 20 + 55;
// Heat Energy in kW
double heat_energy = 0.0;
// A global counter limited by the calibration parameter counter_max
uint32_t global_counter = 0;

//-----------------------------------------------------------------------------------------------------
// Read sensor values

// Simulate reading temperature sensors
#define read_outside_sensor() (outside_temperature)
#define read_inside_sensor() (inside_temperature)

//-----------------------------------------------------------------------------------------------------
// Demo functions with XCP instrumentation

// Calculate heat power from temperature difference and flow rate calibration parameter
// Temperatures are in uint8_t in Deg Celsius offset by -55 °C, conversion rule identifier is "conv.temperature"
float calc_power(uint8_t t1, uint8_t t2) {

    double diff_temp = (double)t2 - (double)t1; // Diff temperature in Kelvin
    double heat_power = diff_temp * 10.0f;      // Heat power in kW

#ifndef OPTION_USE_VARIADIC_MACROS
    // Create a measurement event 'calc_power' and register local measurement variables and function parameters
    DaqCreateEvent(calc_power);
    A2lOnce() {
        // t1, t2, diff_temp and heat_power below only exist on the stack while calc_power() is executing, so they have no fixed
        // address: XCP must locate them relative to the stack frame that is current when event 'calc_power' fires.
        // A2lSetStackAddrMode(event) tells the following A2lCreate* calls to register their variables that way (address extension 2, "Stack frame relative"),
        // instead of the plain absolute addressing used below for the truly global mainloop variables (A2lSetAbsoluteAddrMode).
        A2lSetStackAddrMode(calc_power);
        A2lCreatePhysMeasurementInstance("calc_power", t1, "Parameter t1 in function calc_power", "conv.temperature", -55.0, 200.0);
        A2lCreatePhysMeasurementInstance("calc_power", t2, "Parameter t2 in function calc_power", "conv.temperature", -55.0, 200.0);
        A2lCreatePhysMeasurementInstance("calc_power", diff_temp, "Local variable diff temperature in function calc_power", "K", -100.0, 100.0);
        A2lCreatePhysMeasurementInstance("calc_power", heat_power, "Local variable calculated heat power in function calc_power", "W", 0.0, 10.0);
    }
    // Note: in the OPTION_USE_VARIADIC_MACROS path below (the default), DaqEventVar/A2L_MEAS pick the correct addressing
    // mode (stack vs. absolute) automatically per variable, so no explicit A2lSetStackAddrMode/A2lSetAbsoluteAddrMode call is needed there.
#endif

    // Lock access to calibration parameters
    // Note: calc_power() is called from main()'s mainloop while it already holds a lock on this same segment
    const params_t *p = (params_t *)XcpLockCalSeg(calseg_id_params);

    heat_power = diff_temp * p->flow_rate * 1000.0 * 1.16; // in kWh, 1.16Wh per K per liter - calculate heat power using the flow rate calibration parameter

    // Unlock the calibration segment
    XcpUnlockCalSeg(calseg_id_params);

#ifndef OPTION_USE_VARIADIC_MACROS
    // Trigger the measurement event "calc_power"
    DaqTriggerEvent(calc_power);
#else
    // Trigger the measurement event "calc_power" and register local measurement variables and parameters
    DaqEventVar(calc_power,                                                                    //
                A2L_MEAS(t1, "Parameter t1 in function calc_power"),                           //
                A2L_MEAS(t2, "Parameter t2 in function calc_power"),                           //
                A2L_MEAS(diff_temp, "Local variable diff temperature in function calc_power"), //
                A2L_MEAS(heat_power, "Local variable calculated heat power in function calc_power"));
#endif

    return (float)heat_power;
}

//-----------------------------------------------------------------------------------------------------
// Demo main

static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

int main(int argc, char *argv[]) {

    printf("\nXCP on Ethernet hello_xcp C demo%u V%s - %s\n", (uint32_t)(sizeof(void *) * 8), OPTION_PROJECT_VERSION, argv[0]);
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, activate XCP, must be called before starting the server
    // If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    if (!XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, OPTION_XCP_MODE)) {
        printf("Failed to initialize XCP\n");
        return 1;
    }
    XcpSetElfName(argv[0]); // Set ELF file name for upload via GET_ID, optional

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Enable runtime A2L generation for data declaration as code, optional
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_A2L_MODE)) {
        return 1;
    }

    // Create a calibration segment named 'params' for the calibration parameter struct instance 'params' as reference page
    calseg_id_params = XcpCreateCalSeg("params", &params, sizeof(params));
    // A2lSetSegmentAddrMode(seg_index, seg_instance) must come before the A2lCreateParameter calls below:
    // it makes them register their variables as offsets into calibration segment 'calseg_id_params'
    // ("Calibration segment relative" addressing, see docs/TECHNICAL.md)  instead of by absolute address,
    // which is what lets the segment's page switching (RAM/FLASH), checksum and persistence mechanisms apply to these parameters.
    A2lSetSegmentAddrMode(calseg_id_params, params);
    // Option 1: Register the individual calibration parameters in the calibration segment
    A2lCreateParameter(params.counter_max, "Maximum counter value", "", 0, 65535);
    A2lCreateParameter(params.delay_us, "Mainloop delay time in us", "us", 0, 500000);
    A2lCreateParameter(params.flow_rate, "Flow rate", "m3/h", 0.0, 2.0);
    // Option 2: Register the calibration segment as a typedef instance
    // A2lTypedefBegin(params_t, &params, "Calibration parameters typedef");
    // A2lTypedefParameterComponent(counter_max, "Maximum counter value", "", 0, 2000);
    // A2lTypedefParameterComponent(flow_rate, "Flow rate", "m3/h", 0.0, 2.0);
    // A2lTypedefEnd();
    // A2lCreateTypedefInstance(params, params_t, "Calibration parameters");

    uint16_t counter = 0;

#ifndef OPTION_USE_VARIADIC_MACROS
    // Create a measurement event named "mainloop"
    DaqCreateEvent(mainloop);

    // Register global measurement variables on event "mainloop"
    // outside_temperature, inside_temperature, heat_energy and global_counter are global variables: they exist at one fixed
    // address for the whole program lifetime, so XCP can read them directly by that address ("Absolute" addressing) - no
    // stack frame or calibration segment offset is involved, unlike the two modes used elsewhere in this file.
    A2lSetAbsoluteAddrMode(mainloop);
    A2lCreateLinearConversion(temperature, "Temperature in °C from unsigned byte", "C", 1.0, -55.0);
    A2lCreatePhysMeasurement(outside_temperature, "Temperature in °C read from outside sensor", "conv.temperature", -20, 50);
    A2lCreatePhysMeasurement(inside_temperature, "Temperature in °C read from inside sensor", "conv.temperature", 0, 40);
    A2lCreatePhysMeasurement(heat_energy, "Accumulated heat energy in kWh", "kWh", 0.0, 10000.0);
    A2lCreateMeasurement(global_counter, "Global free running counter");

    // Register local measurement variables on event "mainloop"
    // counter is local to main()'s stack frame (like t1/t2/diff_temp/heat_power in calc_power() above), so it needs
    // "Stack frame relative" addressing again, resolved against the stack frame current when event 'mainloop' fires.
    A2lSetStackAddrMode(mainloop);
    A2lCreateMeasurement(counter, "Mainloop counter");
#endif

    // Mainloop
    uint32_t delay_us = 1000; // Mainloop delay time in us
    while (running) {

        // Lock the calibration parameter segment for consistent and safe access
        // Calibration segment locking is wait-free, locks may be recursive
        // Returns a pointer to the active page (working or reference) of the calibration segment
        const params_t *p = (params_t *)XcpLockCalSeg(calseg_id_params);
        delay_us = p->delay_us; // Get the delay_us calibration value

        // Local variables
        counter++;
        if (counter > p->counter_max) { // Get the counter_max calibration value and reset counter
            counter = 0;
            printf("Reset counter to 0 at %u\n", p->counter_max);
        }

        // Global variables
        global_counter++;
        inside_temperature = read_inside_sensor();
        outside_temperature = read_outside_sensor();
        double heat_power = calc_power(outside_temperature, inside_temperature); // Demo function to calculate heat power in W
        heat_energy += heat_power / 3600e6;                                      // Integrate heat energy in kWh in a global measurement variable, kWh = W/1000  * us/ 3600e6

        // Unlock the calibration segment
        XcpUnlockCalSeg(calseg_id_params);

#ifndef OPTION_USE_VARIADIC_MACROS
        // Trigger the measurement event "mainloop"
        DaqTriggerEvent(mainloop);
#else
        // Register the linear conversion for temperature once
        A2lOnce(temperature) { A2lCreateLinearConversion(temperature, "Temperature in °C from unsigned byte", "C", 1.0, -55.0); }
        // Create and trigger measurement event mainloop, register global and local measurement variables
        DaqEventVar(mainloop,                                                                                                       //
                    A2L_MEAS(outside_temperature, "Temperature in °C read from outside sensor", "conv.temperature", -55, 255 - 55), //
                    A2L_MEAS(inside_temperature, "Temperature in °C read from inside sensor", "conv.temperature", -55, 255 - 55),   //
                    A2L_MEAS(heat_energy, "Accumulated heat energy in kWh", "kWh", 0.0, 10000.0),                                   //
                    A2L_MEAS(global_counter, "Global free running counter"),                                                        //
                    A2L_MEAS(counter, "Mainloop counter"));
#endif

        if (delay_us == 0 || delay_us > 10000000) { // Sanity check for delay_us
            delay_us = 1000;
            printf("WARNING: delay_us value %u out of limits\n", delay_us);
        }
        sleepUs(delay_us);

    } // for (;;)

    XcpDisconnect(); // Force disconnect the XCP client
    A2lFinalize();   // Finalize A2L generation, if not done yet
    // XcpFreeze(); // Save current calibration segments to binary persistence file (OPTION_ENABLE_PERSISTENCE and XCP_MODE_PERSISTENCE)
    XcpEthServerShutdown(); // Stop the XCP server

    return 0;
}
