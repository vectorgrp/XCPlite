// multi_thread_demo xcplib example

#include <assert.h>  // for assert
#include <math.h>    // for M_PI, sin
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "a2l.h"      // for xcplib A2l generation
#include "platform.h" // for sleepMs
#include "xcplib.h"   // for xcplib application programming interface

#ifdef _WIN
#define M_PI 3.14159265358979323846
#endif
#define M_2PI (M_PI * 2)

//-----------------------------------------------------------------------------------------------------

// XCP parameters
#define OPTION_ENABLE_A2L_GENERATOR                  // Enable A2L file generation
#define OPTION_A2L_PROJECT_NAME "multi_thread_demo"  // A2L project name
#define OPTION_A2L_FILE_NAME "multi_thread_demo.a2l" // A2L file name
#define OPTION_USE_TCP false                         // TCP or UDP
#define OPTION_SERVER_PORT 5555                      // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}              // Bind addr, 0.0.0.0 = ANY
// #define OPTION_SERVER_ADDR {127, 0, 0, 1} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 32 // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3

//-----------------------------------------------------------------------------------------------------

// Demo calibration parameters
typedef struct params {
    uint16_t counter_max; // Maximum value of the counter
    double ampl;          // Amplitude of the sine wave
    double period;        // Period of the sine wave in seconds
    uint32_t delay_us;    // Delay in microseconds for the main loop
    bool run;             // Stop flag for the task
} params_t;

static const params_t params = {.counter_max = 1000, .ampl = 100.0, .period = 1.0, .delay_us = 1000, .run = true}; // Default parameters
static tXcpCalSegIndex calseg = XCP_UNDEFINED_CALSEG;                                                              // Calibration segment handle

//-----------------------------------------------------------------------------------------------------

#ifdef _WIN
DWORD WINAPI task(LPVOID p)
#else
void *task(void *p)
#endif
{
    (void)p;

    bool run = true;
    uint32_t delay_us = 1000;
    uint64_t start_time = ApplXcpGetClock64(); // Get the start time in clock ticks

    uint16_t counter = 0; // Local counter variable for measurement
    double channel = 0;

    // Register measurement variables located on stack
    tXcpEventId event = XcpCreateEventInstance("task", 0, 0);
    uint16_t task_id = event;
    char task_name[32];
    sprintf(task_name, "task_%u", task_id);

    A2lCreateMeasurementInstance(task_name, event, counter, "task loop counter", "");
    A2lCreateMeasurementInstance(task_name, event, channel, "task sine signal", "");

    printf("Start task %u\n", task_id);

    while (run) {

        {
            params_t *params = (params_t *)XcpLockCalSeg(calseg);

            counter++;
            if (counter > params->counter_max) {
                counter = 0;
            }

            channel = (task_id * 10) + params->ampl * sin(M_2PI / params->period * ((double)(ApplXcpGetClock64() - start_time) / CLOCK_TICKS_PER_S));

            // Sleep time
            delay_us = params->delay_us;

            // Stop
            run = params->run;

            XcpUnlockCalSeg(calseg);
        }

        // Measurement event
        XcpEventDyn(&event);

        // Sleep for the specified delay parameter in microseconds
        sleepNs(delay_us * 1000);
    }

    return NULL; // Exit the thread
}

// Demo main
int main(void) {

    printf("\nXCP on Ethernet multi thread xcplib demo\n");

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, must be called before starting the server
    XcpInit();

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Enable A2L generation and prepare the A2L file, finalize the A2L file on XCP connect, no auto grouping
#ifdef OPTION_ENABLE_A2L_GENERATOR
    if (!A2lInit(OPTION_A2L_FILE_NAME, OPTION_A2L_PROJECT_NAME, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, true, false)) {
        return 1;
    }
#else
    // Set the A2L filename for upload, assuming the A2L file exists
    ApplXcpSetA2lName(OPTION_A2L_FILE_NAME);
#endif

    // Create a calibration segment for the calibration parameter struct
    // This segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independant page switching, checksum calculation and reinitialization (copy reference page to working page)
    // Note that it can be used in only one ECU thread (in Rust terminology, it is Send, but not Sync)
    calseg = XcpCreateCalSeg("Parameters", &params, sizeof(params));

    // Register individual calibration parameters in the calibration segment
    A2lSetSegmentAddrMode(calseg, params);
    A2lCreateParameter(params, counter_max, "Max counter value, wrap around", "", 0, 10000.0);
    A2lCreateParameter(params, ampl, "Amplitude", "Volt", 0, 100.0);
    A2lCreateParameter(params, period, "Period", "s", 0.1, 10.0);
    A2lCreateParameter(params, delay_us, "task delay time in us", "us", 0, 1000000);
    A2lCreateParameter(params, run, "stop task", "", 0, 1);
    A2lCreateParameterGroup("Parameters", 5, "params.counter_max", "params.ampl", "params.period", "params.delay_us", "params.run");

    // Create multiple instances of the same task
    THREAD t[10];
    for (int i = 0; i < 10; i++) {
        create_thread(&t[i], task); // create multiple instances of the same task
    }

    sleepMs(1000);
    A2lFinalize(); // Optional: Finalize the A2L file generation early, to write the A2L now, not when the client connects
    for (int i = 0; i < 10; i++) {
        join_thread(t[i]);
    }

    // Force disconnect the XCP client
    XcpDisconnect();

    // Stop the XCP server
    XcpEthServerShutdown();

    return 0;
}
