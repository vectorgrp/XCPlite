// struct_demo xcplib example

#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <stdlib.h>  // for free, malloc
#include <string.h>  // for sprintf

#include "a2l.h"      // for xcplib A2l generation
#include "platform.h" // for sleepMs
#include "xcplib.h"   // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------

// XCP parameters
#define OPTION_ENABLE_A2L_GENERATOR            // Enable A2L file generation
#define OPTION_A2L_PROJECT_NAME "struct_demo"  // A2L project name
#define OPTION_A2L_FILE_NAME "struct_demo.a2l" // A2L file name
#define OPTION_USE_TCP false                   // TCP or UDP
#define OPTION_SERVER_PORT 5555                // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}        // Bind addr, 0.0.0.0 = ANY
// s#define OPTION_SERVER_ADDR {127, 0, 0, 1} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 32 // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 5

//-----------------------------------------------------------------------------------------------------

// Demo calibration parameters
typedef struct params {

    uint32_t delay_us; // Delay in microseconds for the main loop

} params_t;

const params_t params = {.delay_us = 1000};

//-----------------------------------------------------------------------------------------------------

typedef struct {
    uint8_t byte_field; // Basic type fields
    int16_t word_field;
} struct2_t;

typedef struct {
    uint8_t byte_field;
    int16_t word_field;
    uint8_t array_field[4]; // Array field
    struct2_t struct_field; // Struct field
} struct1_t;

// Global measurement variables
static uint16_t static_counter = 0;                                   // Local counter variable for measurement
static struct2_t static_struct2 = {.byte_field = 1, .word_field = 2}; // Single instance of struct2_t
static struct1_t static_struct1 = {
    .byte_field = 1, .word_field = 2, .array_field = {0, 1, 2, 3}, .struct_field = {.byte_field = 1, .word_field = 2}}; // Single instance of struct1_t
static struct1_t static_struct1_array[10];                                                                              // Array of struct1_t

//-----------------------------------------------------------------------------------------------------

// Demo main
int main(void) {

    printf("\nXCP on Ethernet struct measurement xcplib demo\n");

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, must be called before starting the server
    XcpInit();

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Enable A2L generation and prepare the A2L file, finalize the A2L file on XCP connect
#ifdef OPTION_ENABLE_A2L_GENERATOR
    if (!A2lInit(OPTION_A2L_FILE_NAME, OPTION_A2L_PROJECT_NAME, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, true, true)) {
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
    tXcpCalSegIndex calseg = XcpCreateCalSeg("Parameters", &params, sizeof(params));

    // Register individual calibration parameters in the calibration segment
    A2lSetSegmentAddrMode(calseg, params);
    A2lCreateParameter(params, delay_us, "mainloop delay time in us", "us", 0, 1000000);

    // Create a A2L typedef for struct2_t
    A2lTypedefBegin(struct2_t, "A2L typedef for struct2_t");
    A2lTypedefMeasurementComponent(byte_field, struct2_t);
    A2lTypedefMeasurementComponent(word_field, struct2_t);
    A2lTypedefEnd();

    // Create a typedef for struct1_t
    A2lTypedefBegin(struct1_t, "A2L typedef for struct1_t");
    A2lTypedefMeasurementComponent(byte_field, struct1_t);
    A2lTypedefMeasurementComponent(word_field, struct1_t);
    A2lTypedefMeasurementArrayComponent(array_field, struct1_t);
    A2lTypedefComponent(struct_field, struct2_t, 1, struct1_t);
    A2lTypedefEnd();

    // Local stack measurement variables
    uint16_t local_counter = 0;                                   // Local counter variable for measurement
    struct2_t local_struct2 = {.byte_field = 1, .word_field = 2}; // Single instance of struct2_t
    struct1_t local_struct1 = {.byte_field = 1, .word_field = 2, .array_field = {0, 1, 2, 3}, .struct_field = {.byte_field = 1, .word_field = 2}}; // Single instance of struct1_t
    struct1_t local_struct1_array[8];                                                                                                              // Array of struct1_t

    // Heap measurement variables
    struct1_t *heap_struct1 = malloc(sizeof(struct1_t)); // Pointer to a struct1_t on the heap
    *heap_struct1 = local_struct1;

    // Initialize some values
    for (int i = 0; i < 8; i++) {
        local_struct1_array[i] = local_struct1;
        static_struct1_array[i] = local_struct1;
        local_struct1_array[i].byte_field = i;  // Initialize the array with different values
        static_struct1_array[i].byte_field = i; // Initialize the array with different values
    }

    // Create measurement events
    DaqCreateEvent(event);
    DaqCreateEvent(event_heap); // Relative heap addressing mode needs an individual event for each pointer

    // Create a A2L measurement variables for the counters
    // Create A2L typedef instances for the structs and the array of structs

    // Stack
    A2lSetStackAddrMode(event); // stack relative addressing mode
    A2lCreateMeasurement(local_counter, "Stack measurement variable", "");
    A2lCreateTypedefInstance(local_struct2, struct2_t, "Instance of test_struct2_t");
    A2lCreateTypedefInstance(local_struct1, struct1_t, "Instance of test_struct1_t");
    A2lCreateTypedefArray(local_struct1_array, struct1_t, 8, "Array [10] of struct1_t");

    // static/global
    A2lSetAbsoluteAddrMode(event); // absolute addressing mode
    A2lCreateMeasurement(static_counter, "Global measurement variable ", "");
    A2lCreateTypedefInstance(static_struct2, struct2_t, "Instance of test_struct2_t");
    A2lCreateTypedefInstance(static_struct1, struct1_t, "Instance of test_struct1_t");
    A2lCreateTypedefArray(static_struct1_array, struct1_t, 8, "Array [10] of struct1_t");

    // Heap
    A2lSetRelativeAddrMode(event_heap, heap_struct1); // relative addressing mode for heap_struct1_array
    A2lCreateTypedefReference(heap_struct1, struct1_t, "Pointer to struct1_t on heap");

    A2lFinalize(); // Optional: Finalize the A2L file generation early, to write the A2L immediately, not when the client connects

    for (;;) {

        // Modify some static and stack variables
        local_counter++;
        static_counter++;
        local_struct1_array[local_counter % 8].word_field = local_counter;
        local_struct1_array[local_counter % 8].struct_field.word_field = local_counter;
        static_struct1_array[local_counter % 8].word_field = local_counter;
        static_struct1_array[local_counter % 8].struct_field.word_field = local_counter;
        heap_struct1->word_field = local_counter; // Modify the heap variable
        heap_struct1->struct_field.word_field = local_counter;

        // Sleep for the delay parameter microseconds
        params_t *params = (params_t *)XcpLockCalSeg(calseg);
        sleepNs(params->delay_us * 1000);
        XcpUnlockCalSeg(calseg);

        // Trigger the measurement events
        DaqEvent(event);
        DaqEventRelative(event_heap, heap_struct1);

    } // for(;;)

    // Force disconnect the XCP client
    XcpDisconnect();

    // Stop the XCP server
    XcpEthServerShutdown();

    return 0;
}
