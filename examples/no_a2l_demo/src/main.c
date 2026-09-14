// no_a2l_demo - XCPlite example
// Demonstrates XCPlite operation without runtime A2L generation
// This concept works on microcontrollers and microprocessors without filesystem support
// Requires manual or tool based XCPlite specific A2L file creation and update process or direct ELF support
// See ../README.md for details

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <stdlib.h>  // for malloc, free
#include <string.h>  // for sprintf

#include "xcplib.h" // for libxcplite application programming interface

// Internal libxcplite includes to simplify multi platform support and keep the demo code simple and readable
#include "platform.h" // for platform abstraction - thread local, threads, mutex, sockets, sleepUs, ...

static volatile bool global_running = true;
static void sig_handler(int sig) { global_running = false; }

//-----------------------------------------------------------------------------------------------------
// XCP configuration parameters

#define OPTION_PROJECT_VERSION "V2.1.10" // EPK version string

#define OPTION_PROJECT_NAME "no_a2l_demo" // Project name, used to build the volatile and BIN file name
#define OPTION_USE_TCP false              // TCP or UDP
#define OPTION_SERVER_PORT 5555           // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}   // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 8)      // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3                // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

// Local calibration parameters
struct counter_control {
    uint16_t counter_max;
    uint16_t counter_inc;
};
const struct counter_control counter_control = {.counter_max = 1000, .counter_inc = 1};

// Global calibration parameters
struct params {

    uint32_t delay_us; // Sleep time in microseconds for the main and the task loop

    // Calibration parameters of various basic and complex types
    double test_par_double;
    bool test_par_bool;
    uint64_t test_par_uint64;
    uint32_t test_par_uint32;
    uint16_t test_par_uint16;
    enum { OFF = 0, ON = 1, STANDBY = 2 } test_par_enum;
    uint8_t test_par_uint8_array[10];
    struct test_par_struct {
        uint16_t test_field_uint16;
        int16_t test_field_int16;
        float test_field_float;
        uint8_t test_field_uint8_array[3];
    } test_par_struct;
};
const struct params params = {.delay_us = 1000,
                              .test_par_double = 0.123456789,
                              .test_par_bool = true,
                              .test_par_uint64 = 0x1234567812345678,
                              .test_par_uint32 = 0x1234,
                              .test_par_uint16 = 0x1234,
                              .test_par_enum = ON,
                              .test_par_uint8_array = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
                              .test_par_struct = {2, -2, 0.4f, {0, 1, 2}}};

// Define a global calibration parameter segment named 'params' for the calibration parameters in 'const struct params params' as default/reference page
CalSegDecl(params);

// Example metadata annotations For the calibration parameter 'delay_us' in the params calibration segment

// Metadata annotation as comments for Vector A2L Toolset Creator
/*
@@ STRUCTURE = params
@@ ELEMENT = delay_us
@@ DESCRIPTION = "Sleep time in microseconds for the main and the task loop"
@@ UNIT = "us"
@@ LIMITS = 1.0, 10000.0
@@ DATA_TYPE = UWORD [0 ... 10000]
@@ END
*/

// Metadata annotation as code (static data in a special ELF section)
// Via linker map file and xcpclient tool ELF->A2L generation, not supported by Vector A2L Toolset ELF reader
// For struct instance fields, use __ as path separator (params__delay_us means params.delay_us)

// Define physical unit and limits for the calibration parameter 'delay_us' in the params calibration segment
XCP_LIMITS(params__delay_us, 1, 10000);
XCP_UNIT(params__delay_us, "us");

// Define the enum conversion for the calibration parameter test_par_enum
// @@@@ UNUSED: Remove, enum support now build into xcpclient tool
// XCP_UNIT(params__test_par_enum, "0 \"OFF\" 1 \"ON\" 2 \"STANDBY\" ");

//-----------------------------------------------------------------------------------------------------
// Demo global measurement values

// Global measurement variable
// Modified in function foo
// Measuring it in main or task, is possible, but asynchronous and may give inconsistent results
XCP_COMMENT(counter, "Global measurement variable"); // Example for meta data annotation as code
XCP_READ_WRITE(counter);                             // Example for meta data annotation as code
uint16_t counter = 0;
XCP_COMMENT(global_counter, "Global measurement variable"); // Example for meta data annotation as code
XCP_READ_WRITE(global_counter);                             // Example for meta data annotation as code
uint16_t global_counter = 0;

// A2L Creator code parser annotation
/*
@@ SYMBOL = counter
@@ DESCRIPTION = "Global measurement variable"
@@ END
*/

// More global measurement variables of various basic and complex types
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
bool global_test_bool = true;
uint8_t global_test_array[3] = {1, 2, 3};
struct test_struct {
    uint16_t a;
    int16_t b;
    float f;
    uint8_t d[3];
};
struct test_struct global_test_struct = {1, -2, 0.3f, {1, 2, 3}};

//-----------------------------------------------------------------------------------------------------
// Demo thread

THREAD_FUNC_RETURN task(void *p) {
    printf("Start thread %u ...\n", get_thread_id());

    // Static local scope measurement variable
    XCP_COMMENT(static_counter, "Static local measurement variable in thread function `task`"); // Example for meta data annotation as code
    volatile static uint16_t static_counter = 0;

    // Local measurement variable
    XCP_COMMENT(counter, "Local measurement variable in thread function `task`"); // Example for meta data annotation as code
    volatile uint32_t counter = 0;

    // Heap measurement variable
    struct test_struct *volatile heap_struct = (struct test_struct *)malloc(sizeof(struct test_struct));
    assert(heap_struct);
    heap_struct->a = 11;
    heap_struct->b = -22;
    heap_struct->f = 0.33f;
    heap_struct->d[0] = 11;
    heap_struct->d[1] = 22;
    heap_struct->d[2] = 33;

    DaqCreateEvent(task);

    while (global_running) {

        counter++;
        static_counter++;

        DaqTriggerEventExt(task, heap_struct);

        // Sleep for a tunable amount of time (not inside the lock for the calibration parameter block, to not block the XCP server or other threads unnecessarily long)
        uint32_t delay = CalSegLock(params)->delay_us;
        CalSegUnlock(params);
        sleepUs(delay);
    }

    free(heap_struct);
    THREAD_FUNC_END; // Exit the thread
}

//-----------------------------------------------------------------------------------------------------
// Demo functions

// Avoid inlining to be able to measure local variables
// xcpclient ELF->A2L does not support inlined function and silently drop them
XCP_NOINLINE void foo(void) {

    // Static local scope measurement variable
    XCP_COMMENT(static_counter, "Local static measurement variable in function `foo`");
    volatile static uint16_t static_counter = 0;

    // Local measurement variable
    XCP_COMMENT(foo__counter, "Local captured measurement variable in function `foo`");
    uint32_t counter = 0;

    // More local measurement variables
    // Measured via capture, variables stay in their registers
    float test_float = 0.1f;
    double test_double = 0.2;
    uint8_t test_uint8 = 1;
    uint16_t test_uint16 = 2;
    uint32_t test_uint32 = 3;
    uint64_t test_uint64 = 4;
    struct test_struct test_struct = {1, -2, 0.3f, {1, 2, 3}};
    uint8_t test_array[3] = {1, 2, 3};

    // Measure via stack, register variables spilled to stack
    XCP_MEAS int8_t test_int8 = -1;
    XCP_MEAS int16_t test_int16 = -2;
    XCP_MEAS int32_t test_int32 = -3;
    XCP_MEAS uint64_t test_int64 = 1;

    global_counter++;
    static_counter++;
    counter = global_counter;

    DaqCreateAndTriggerEventCapture(foo, counter, test_float, test_double, test_uint8, test_uint16, test_uint32, test_uint64, test_struct, test_array);
}

// Never called
// Just to demonstrate the DaqCreateAndTriggerEvent macro creates the event, without runing the code in foo
void bar(void) { DaqCreateAndTriggerEvent(bar); }

//-----------------------------------------------------------------------------------------------------
// Demo main

int main(int argc, char *argv[]) {

    printf("\nXCP on Ethernet no_a2l_demo C demo\n");
    printf("XCPlite version: %u.%u.%u\n", OPTION_VERSION_MAJOR, OPTION_VERSION_MINOR, OPTION_VERSION_PATCH);

    // Print build configuration
#ifdef NDEBUG
    printf("Build: Release ");
#else
    printf("Build: Debug ");
#endif
#ifdef __OPTIMIZE__
    printf("(__OPTIMIZE__ = %u)\n", __OPTIMIZE__);
#else
    printf("(no optimization)\n");
#endif
    printf("Address of 'params': %p (%u:%08X)(%zu bytes)\n", &params, ApplXcpGetAddrExt((uint8_t *)&params), ApplXcpGetAddr((uint8_t *)&params), sizeof(params));
    printf("Address of 'counter': %p (%u:%08X)\n", &counter, ApplXcpGetAddrExt((uint8_t *)&counter), ApplXcpGetAddr((uint8_t *)&counter));
    printf("\n");

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Create the EPK software version string in an initialized memory section for offline A2L generation
    XcpCreateEpk(OPTION_PROJECT_VERSION);

    // Initialize the XCP singleton, activate XCP, must be called before starting the server
    // @@@@ TODO: Using binary persistence files not supported, | XCP_MODE_PERSISTENCE
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_LOCAL);
    XcpSetElfName(argv[0]); // Set ELF file name for upload via GET_ID, optional

    // Initialize the XCP Server
    const uint8_t __addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(__addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Create threads
    THREAD_HANDLE __t1 = 0;
    create_thread(&__t1, NULL, task, NULL);

    // Demo measurement variables
    XCP_COMMENT(static_counter, "Static local measurement variable in function `main`");
    volatile static uint16_t static_counter = 0;

    // Calibration parameter counter_max
    CalSegCreate(counter_control);

    // Create a measurement event named "mainloop"
    DaqCreateEvent(mainloop);

    // Mainloop
    printf("Start main loop...\n");
    while (global_running) {

        // Lock the calibration parameter block for safe access
        // Calibration segment or block locking is wait-free, locks may be recursive, calibration segments may be shared among multiple threads
        // Returns a pointer to the active page (working or reference) of the calibration segment or block
        const struct counter_control *p_counter_control = CalSegLock(counter_control);

        counter += p_counter_control->counter_inc;
        if (counter > p_counter_control->counter_max) { // Limit the global counter with the counter_max calibration value
            counter = 0;
        }

        // Unlock the calibration block
        CalSegUnlock(counter_control);

        static_counter++;

        // Demonstrate calibration thread safety and consistency
        const struct params *p_params = CalSegLock(params);
        if (!((p_params->test_par_uint64 >> 32) == (p_params->test_par_uint64 & 0xFFFFFFFF))) {
            printf("Calibration parameter test_par_uint64 is not consistent, value: %016" PRIx64 "\n", p_params->test_par_uint64);
        }
        if (!(p_params->test_par_uint32 == p_params->test_par_uint16)) {
            printf("Calibration parameter test_par_uint32 is not consistently changed with test_par_uint16, value: %08" PRIx32 " != %04" PRIx16 "\n", p_params->test_par_uint32,
                   p_params->test_par_uint16);
        }
        CalSegUnlock(params);

        // Function calls
        foo(); // Call a function to demonstrate the DaqCreateAndTriggerEvent macro in foo

        // Trigger the measurement event "mainloop"
        DaqTriggerEvent(mainloop);

        // Sleep for a tunable amount of time (not inside the lock for the calibration parameter block, to not block the XCP server or other threads unnecessarily long)
        uint32_t delay = CalSegLock(params)->delay_us;
        CalSegUnlock(params);
        sleepUs(delay);

    } // for (;;)

    // Wait for the thread to stop
    if (__t1)
        join_thread(__t1);

    XcpDisconnect(); // Force disconnect the XCP client
    // XcpFreeze(); // Save current calibration changes to binary persistence file
    XcpEthServerShutdown(); // Stop the XCP server

    return 0;
}
