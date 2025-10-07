// no_a2l_demo xcplib example
// Demonstrates XCPlite usage without runtime A2L generation
// Requires manual or tool based A2L file creation and update
// Limited to parameters and measurements in addressable (4 GB - 32bit) global memory

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "a2l.h"      // for _A2lGetAddr_ test output
#include "platform.h" // for platform abstraction for thread local, threads, mutex, sockets, sleepUs, ...
#include "xcplib.h"   // for xcplib application programming interface

static volatile bool global_running = true;
static void sig_handler(int sig) { global_running = false; }

//-----------------------------------------------------------------------------------------------------
// XCP params

#define OPTION_PROJECT_NAME "no_a2l_demo" // Project name, used to build the A2L and BIN file name
#define OPTION_USE_TCP true               // TCP or UDP
#define OPTION_SERVER_PORT 5555           // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}   // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 16       // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 2                // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

// Calibration parameters structure
struct params {
    uint16_t counter_max; // Maximum value for the counter
    uint32_t delay_us;    // Sleep time in microseconds for the main loop

    // Test parameters of various types
    uint8_t test_par_uint8_array[10];
    double test_par_double;
    enum { ENUM_0 = 0, ENUM_1 = 1, ENUM_2 = 2, ENUM_3 = 3 } test_par_enum;
    struct test_par_struct {
        uint16_t test_field_uint16;
        int16_t test_field_int16;
        float test_field_float;
        uint8_t test_field_uint8_array[3];
    } test_par_struct;
};

// Default values (reference page, "FLASH") for the calibration parameters
const struct params params = {.counter_max = 1024,
                              .delay_us = 1000,
                              .test_par_double = 0.123456789,
                              .test_par_enum = ENUM_2,
                              .test_par_uint8_array = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
                              .test_par_struct = {1, -2, 0.3f, {1, 2, 3}}};

//-----------------------------------------------------------------------------------------------------
// Demo global measurement values

uint16_t global_counter = 0;

uint8_t global_test_uint8 = 8;
uint16_t global_test_uint16 = 16;
uint32_t global_test_uint32 = 32;
uint64_t global_test_uint64 = 64;
int8_t global_test_int8 = -8;
int16_t global_test_int16 = -16;
int32_t global_test_int32 = -32;
int64_t global_test_int64 = -64;
float global_test_float = 0.4f;
double global_test_double = 0.8;
uint8_t global_test_array[3] = {1, 2, 3};
struct test_struct {
    uint16_t a;
    int16_t b;
    float f;
    uint8_t d[3];
} global_test_struct = {1, -2, 0.3f, {1, 2, 3}};

//-----------------------------------------------------------------------------------------------------
// Demo thread

void *task(void *p) {

    printf("Start thread %u ...\n", get_thread_id());

    // Thread local measurement variables
    static THREAD_LOCAL uint16_t thread_local_counter = 0;

    uint32_t counter = 0;

    DaqCreateEvent(task);
    // tXcpEventId event = DaqCreateEventInstance(task); // Create a measurement event instance for each instance of the this thread

    while (global_running) {

        counter++;

        thread_local_counter++;
        DaqCapture(task, thread_local_counter);

        DaqEvent(task);
        // DaqEvent_i(event);

        sleepUs(1000);
    }

    return 0;
}

//-----------------------------------------------------------------------------------------------------
// Demo function

void foo(void) {

    uint32_t counter = 0;

    // Local measurement variables
    uint64_t test_int64 = 1;
    float test_float = 0.1f;
    double test_double = 0.2;
    uint8_t test_uint8 = 1;
    uint16_t test_uint16 = 2;
    uint32_t test_uint32 = 3;
    uint64_t test_uint64 = 4;
    int8_t test_int8 = -1;
    int16_t test_int16 = -2;
    int32_t test_int32 = -3;
    int64_t test_int64_2 = -4;
    struct test_struct test_struct = {1, -2, 0.3f, {1, 2, 3}};
    // uint8_t test_array[3] = {1, 2, 3};

    global_counter++;
    counter++;

    DaqCreateEvent(foo);
    DaqEvent(foo);
}

//-----------------------------------------------------------------------------------------------------
// Demo main

int main(void) {

    printf("\nXCP on Ethernet no_a2l_demo C xcplib demo\n");
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // XCP: Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // XCP: Initialize the XCP singleton, activate XCP, must be called before starting the server
    //      If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    XcpInit(true);
    ApplXcpSetA2lName(OPTION_PROJECT_NAME); // @@@@ This is still required to enable GET_ID for XCP_IDT_ASCII

    // XCP: Initialize the XCP Server
    const uint8_t __addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(__addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // XCP: Create a calibration segment named 'params' for the calibration parameters in 'const struct params params' as reference page
    CalSegCreate(params);

    // Create 2 threads
    THREAD __t1 = 0;
    create_thread(&__t1, task);
    // THREAD __t2 = 0;
    // create_thread(&__t2, task);

    // Local measurment variables
    uint32_t counter = 0;

    // static measurement variable
    static uint16_t static_counter = 0;

    // XCP: Create a measurement event named "mainloop"
    DaqCreateEvent(mainloop);

    // Test output
    printf("A2L base address = %p:\n", ApplXcpGetBaseAddr());
    printf("Stackframe = %p\n", get_stack_frame_pointer());
    A2lSetDynAddrMode(XcpFindEvent("mainloop", NULL), 0 /* dyn base index, 0 is stack */, get_stack_frame_pointer());
    printf("&counter = %p, A2L-addr = %u:0x%08X\n", &counter, A2lGetAddrExt_(), A2lGetAddr_(&counter));
    printf("&static_counter = %p, A2L-addr = 0x%08X\n", &static_counter, ApplXcpGetAddr((void *)&static_counter));
    printf("&params = %p, A2L-addr = 0x%08X, size = %u\n", &params, ApplXcpGetAddr((void *)&params), (uint32_t)sizeof(params));
    /*

    /begin MEASUREMENT main.counter "" ULONG IDENTITY 0 0 0 4294967295 ECU_ADDRESS 0x1FFE4 ECU_ADDRESS_EXTENSION 2 READ_WRITE

    A2L base address = 0x55556f800000:
    Stackframe = 0x7fffce7e0ab0
    &counter = 0x7fffce7e0af4, A2L-addr = 2:0x00010044
    &static_counter = 0x55556f8302c6, A2L-addr = 0x000302C6
    &params = 0x55556f8158e0, A2L-addr = 0x000158E0, size = 48

    */

        // Mainloop
    printf("Start main loop...\n");
    uint32_t delay_us;
    while (global_running) {
        // XCP: Lock the calibration parameter segment for consistent and safe access
        //      Calibration segment locking is wait-free, locks may be recursive, calibration segments may be shared among multiple threads
        //      Returns a pointer to the active page (working or reference) of the calibration segment
        {
            struct params *params = CalSegLock(params);

            delay_us = params->delay_us; // Get the delay_us calibration value

            static_counter++;
            if (static_counter > params->counter_max) {
                static_counter = 0;
            }

            counter++;
            if (counter > params->counter_max) {
                counter = 0;
            }

            // XCP: Unlock the calibration segment
            CalSegUnlock(params);
        }

        // XCP: Trigger the measurement event "mainloop"
        DaqEvent(mainloop);

        // Sleep for the specified delay parameter in microseconds, don't sleep with the XCP lock held to give the XCP client a chance to update params
        sleepUs(delay_us);

        foo();

    } // for (;;)

    // XCP: Force disconnect the XCP client
    XcpDisconnect();

    // Wait for the thread to stop
    if (__t1)
        join_thread(__t1);
    // if (__t2)
    //     join_thread(__t2);

    // XCP: Stop the XCP server
    XcpEthServerShutdown();

    return 0;
}
