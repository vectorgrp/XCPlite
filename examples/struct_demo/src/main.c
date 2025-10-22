// struct_demo xcplib example

// This is a pure measurement only example, no calibration segments used
// Demonstrates how to create typedefs for nested structs and arrays of struct
// and how to use the typedefs to create measurement variable instances

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <stdlib.h>  // for free, malloc
#include <string.h>  // for sprintf

#include "a2l.h"    // for xcplib A2l generation
#include "xcplib.h" // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------

// XCP parameters
#define OPTION_PROJECT_NAME "struct_demo" // A2L project name
#define OPTION_USE_TCP true               // TCP or UDP
#define OPTION_SERVER_PORT 5555           // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}   // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 32       // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3                // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------
// Measurement variables and structs

typedef struct {
    uint8_t byte_field; // Basic type fields
    int16_t word_field;
} struct2_t;

typedef struct {
    uint8_t byte_field;
    int16_t word_field;
    uint8_t array_field[256]; // Array field
    struct2_t struct_field;   // Struct field
} struct1_t;

// Global measurement variables
static uint16_t static_counter = 0;                                   // Local counter variable for measurement
static struct2_t static_struct2 = {.byte_field = 1, .word_field = 2}; // Single instance of struct2_t
static struct1_t static_struct1 = {.byte_field = 1, .word_field = 2, .array_field = {0}, .struct_field = {.byte_field = 1, .word_field = 2}}; // Single instance of struct1_t
static struct1_t static_struct1_array[10];                                                                                                    // Array of struct1_t

//-----------------------------------------------------------------------------------------------------

static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

// Demo main
int main(void) {

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("\nXCP on Ethernet struct measurement xcplib demo\n");

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

    // Enable A2L generation and prepare the A2L file, finalize the A2L file on XCP connect
    if (!A2lInit(OPTION_PROJECT_NAME, __DATE__ "_" __TIME__ /* EPK */, addr, OPTION_SERVER_PORT, OPTION_USE_TCP,
                 A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

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
    uint16_t local_counter = 0;                                                                                                           // Local counter variable for measurement
    struct2_t local_struct2 = {.byte_field = 1, .word_field = 2};                                                                         // Single instance of struct2_t
    struct1_t local_struct1 = {.byte_field = 1, .word_field = 2, .array_field = {0}, .struct_field = {.byte_field = 1, .word_field = 2}}; // Single instance of struct1_t
    struct1_t local_struct1_array[8];                                                                                                     // Array of struct1_t

    // Heap measurement variables
    struct1_t *heap_struct1 = malloc(sizeof(struct1_t)); // Pointer to a struct1_t on the heap
    struct2_t *heap_struct2 = malloc(sizeof(struct2_t)); // Pointer to a struct2_t on the heap
    *heap_struct1 = local_struct1;
    *heap_struct2 = local_struct2;

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
    A2lCreateMeasurement(local_counter, "Stack measurement variable");
    A2lCreateTypedefInstance(local_struct2, struct2_t, "Instance of test_struct2_t");
    A2lCreateTypedefInstance(local_struct1, struct1_t, "Instance of test_struct1_t");
    A2lCreateTypedefArray(local_struct1_array, struct1_t, 8, "Array [10] of struct1_t");

    // static/global
    A2lSetAbsoluteAddrMode(event); // absolute addressing mode
    A2lCreateMeasurement(static_counter, "Global measurement variable ");
    A2lCreateTypedefInstance(static_struct2, struct2_t, "Instance of test_struct2_t");
    A2lCreateTypedefInstance(static_struct1, struct1_t, "Instance of test_struct1_t");
    A2lCreateTypedefArray(static_struct1_array, struct1_t, 8, "Array [10] of struct1_t");

    // Heap
    A2lSetRelativeAddrMode1(event_heap); // relative addressing mode for heap_struct1_array, first base pointer
    A2lCreateTypedefInstance(heap_struct1, struct1_t, "Pointer to struct1_t on heap");
    A2lSetRelativeAddrMode2(event_heap); // relative addressing mode for heap_struct2_array, second base pointer
    A2lCreateTypedefInstance(heap_struct2, struct2_t, "Pointer to struct2_t on heap");

    A2lFinalize(); // Optional: Finalize the A2L file generation early, to write the A2L immediately, not when the client connects

    while (running) {

        // Modify some static and stack variables
        local_counter++;
        static_counter++;
        local_struct1_array[local_counter % 8].word_field = local_counter;
        local_struct1_array[local_counter % 8].struct_field.word_field = local_counter;
        static_struct1_array[local_counter % 8].word_field = local_counter;
        static_struct1_array[local_counter % 8].struct_field.word_field = local_counter;
        heap_struct1->word_field++; // Modify the heap variable
        heap_struct1->struct_field.word_field++;
        heap_struct2->word_field++;

        // Trigger the measurement events
        DaqEvent(event);
        DaqEvent2(event_heap, heap_struct1, heap_struct2);

        sleepUs(1000);
    } // for(;;)

    // Force disconnect the XCP client
    XcpDisconnect();

    // Stop the XCP server
    XcpEthServerShutdown();

    return 0;
}
