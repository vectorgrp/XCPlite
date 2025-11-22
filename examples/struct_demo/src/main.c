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
#define OPTION_PROJECT_EPK __TIME__       // EPK version string
#define OPTION_USE_TCP true               // TCP or UDP
#define OPTION_SERVER_PORT 5555           // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}   // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 32       // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3                // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------
// Measurement variables and structs

typedef struct {
    int16_t word_field;
    uint8_t byte_field;
    // Compiler adds 1 byte trailing padding here to align the struct size for array usage
} struct2_t;

static_assert(sizeof(struct2_t) == 4, "struct2_t size incorrect");

typedef struct {
    uint8_t byte_field;
    // Compiler adds 1 byte alignment padding here to align the next field
    int16_t word_field;
    uint8_t array_field[256];         // Array field
    struct2_t struct_field;           // Struct field
    struct2_t array_struct_field[10]; // Array of struct field
} struct1_t;

static_assert(sizeof(struct1_t) == 4 + 256 + 4 + 4 * 10, "struct1_t size incorrect");

// Global measurement variables
static uint16_t static_counter = 0;
static struct2_t static_struct2 = {.byte_field = 1, .word_field = 2};
static struct1_t static_struct1 = {
    .byte_field = 1, .word_field = 2, .array_field = {0}, .struct_field = {.byte_field = 1, .word_field = 2}, .array_struct_field = {{.byte_field = 1, .word_field = 2}}};
static struct1_t static_struct1_array[10];
static struct2_t static_struct2_array[10];

static_assert(sizeof(static_struct1_array) == (sizeof(struct1_t) * 10), "static_struct1_array size incorrect");
static_assert(sizeof(static_struct2_array) == (sizeof(struct2_t) * 10), "static_struct2_array size incorrect");

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
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_EPK, true);

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Enable A2L generation and prepare the A2L file, finalize the A2L file on XCP connect
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    // Create a A2L typedef for struct2_t
    A2lTypedefBegin(struct2_t, &static_struct2, "A2L typedef for struct2_t");
    A2lTypedefMeasurementComponent(byte_field, "Byte field");
    A2lTypedefMeasurementComponent(word_field, "Word field");
    A2lTypedefEnd();

    // Create a typedef for struct1_t
    A2lTypedefBegin(struct1_t, &static_struct1, "A2L typedef for struct1_t");
    A2lTypedefMeasurementComponent(byte_field, "Byte field");
    A2lTypedefMeasurementComponent(word_field, "Word field");
    A2lTypedefMeasurementArrayComponent(array_field, "Array field of 256 bytes");
    A2lTypedefComponent(struct_field, struct2_t, 1);
    A2lTypedefComponent(array_struct_field, struct2_t, 8);
    A2lTypedefEnd();

    // Local stack measurement variables
    uint16_t counter = 0;                                                                                                                 // Local counter variable for measurement
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
    DaqCreateEvent(event2); // Relative heap addressing mode needs an individual event for each pointer

    // Create a A2L measurement variables for the counters
    // Create A2L typedef instances for the structs and the array of structs

    // Stack
    A2lSetStackAddrMode(event); // stack relative addressing mode
    A2lCreateMeasurement(counter, "Mainloop counter");
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
    A2lSetRelativeAddrMode(event2, heap_struct1); // relative addressing mode for heap_struct1
    A2lCreateTypedefReference(heap_struct1, struct1_t, "Pointer to struct1_t on heap");

    A2lFinalize(); // Optional: Finalize the A2L file generation early, to write the A2L immediately, not when the client connects

    while (running) {

        // Modify some static and stack variables
        counter++;
        static_counter++;
        local_struct1_array[counter % 8].word_field = counter;
        local_struct1_array[counter % 8].struct_field.word_field = counter;
        static_struct1_array[counter % 8].word_field = counter;
        static_struct1_array[counter % 8].struct_field.word_field = counter;
        heap_struct1->word_field++; // Modify the heap variable
        heap_struct1->struct_field.word_field++;
        heap_struct2->word_field++;

        // Trigger the measurement events
        DaqTriggerEvent(event);
        DaqTriggerEventExt(event2, heap_struct1);

        sleepUs(1000);
    } // for(;;)

    // Force disconnect the XCP client
    XcpDisconnect();

    // Stop the XCP server
    XcpEthServerShutdown();

    return 0;
}
