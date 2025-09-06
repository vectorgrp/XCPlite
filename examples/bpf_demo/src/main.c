// bpf_demo xcplib example

#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "a2l.h"    // for xcplib A2l generation
#include "xcplib.h" // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP params

#define OPTION_PROJECT_NAME "bpf_demo"  // Project name, used to build the A2L and BIN file name
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 16     // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3              // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------
// Global variables

typedef struct {
    uint8_t byte_field;
    int16_t word_field;
} demo_struct_t;

static uint16_t static_counter = 0;                                      // Local counter variable for measurement
static demo_struct_t static_struct = {.byte_field = 1, .word_field = 2}; // Single instance of demo_struct_t

//-----------------------------------------------------------------------------------------------------
// Demo main

int main(void) {

    printf("\nXCP on Ethernet bpf_demo C xcplib demo\n");

    // Init XCP
    XcpSetLogLevel(OPTION_LOG_LEVEL);
    XcpInit(true);
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }
    if (!A2lInit(OPTION_PROJECT_NAME, NULL, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    // Create a measurement event named "mainloop_event"
    DaqCreateEvent(mainloop_event);

    // Create a A2L typedef for demo_struct_t
    A2lTypedefBegin(demo_struct_t, "A2L typedef for demo_struct_t");
    A2lTypedefMeasurementComponent(byte_field, demo_struct_t);
    A2lTypedefMeasurementComponent(word_field, demo_struct_t);
    A2lTypedefEnd();

    // Create and register a static/global measurement variables (static_counter, static_struct)
    A2lSetAbsoluteAddrMode(mainloop_event); // absolute addressing mode
    A2lCreateMeasurement(static_counter, "Global measurement variable ");
    A2lCreateTypedefInstance(static_struct, demo_struct_t, "Instance of demo_struct_t");

    // Create and register a local measurement variables (loop_counter)
    uint16_t loop_counter = 0;
    A2lSetStackAddrMode(mainloop_event); // Set stack relative addressing mode with fixed event mainloop_event
    A2lCreateMeasurement(loop_counter, "Local measurement variable on stack");

    A2lFinalize(); // @@@@ Test: Manually finalize the A2L file to make it visible without XCP tool connect

    // Mainloop
    printf("Start main loop...\n");
    for (;;) {

        // Local variables
        loop_counter++;

        // XCP: Trigger the measurement event "mainloop_event" to timestamp and send measurement to the XCP client
        DaqEvent(mainloop_event);

        // Sleep for the specified delay parameter in microseconds, don't sleep with the XCP lock held to give the XCP client a chance to update params
        sleepUs(1000);

    } // for (;;)

    // XCP: Force disconnect the XCP client
    XcpDisconnect();

    // XCP: Stop the XCP server
    XcpEthServerShutdown();

    return 0;
}
