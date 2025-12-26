
#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <stdlib.h>  // for system
#include <string.h>  // for sprintf

#include "a2l.h"    // for xcplib A2l generation
#include "xcplib.h" // for xcplib application programming interface

#include "xcpLite.h"

//  Not public API, external linkage for testing
extern bool A2lCheckFinalizeOnConnect(uint8_t connect_mode);

// a2ltool from crates.io
// Set the correct path or install it
#define A2lTOOL_PATH "a2ltool"

#define OPTION_PROJECT_NAME "a2l_test"  // A2L project name
#define OPTION_PROJECT_EPK __TIME__     // EPK version string
#define OPTION_LOG_LEVEL 4              // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debugs
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY

//-----------------------------------------------------------------------------------------------------
// Measurements

// Basic types
static uint8_t uint8 = 0;
static uint16_t uint16 = 1;
static uint32_t uint32 = 2;
static uint64_t uint_64 = 3;
static int8_t int8 = 4;
static int16_t int16 = 5;
static int32_t int32 = 6;
static int64_t int_64 = 7;
static float float4 = 8.0f;
static double double8 = 9.0;

// Multidimensional
static int16_t array[16] = {0, 1, 2, 3, 4, 3, 2, 1, 0, -1, -2, -3, -4, -3, -2, -1};
static double matrix[2][2] = {{0, 1}, {2, 3}};

// Structs
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

static struct1_t struct1 = {.byte_field = 1, .word_field = 2, .array_field = {0, 1, 2, 3}, .struct_field = {.byte_field = 1, .word_field = 2}}; // Single instance of struct1_t
static struct2_t struct2 = {.byte_field = 1, .word_field = 2};                                                                                  // Single instance of struct2_t

// Array of structs
static struct1_t struct1_array[16]; // Array of struct1_t

//-----------------------------------------------------------------------------------------------------
// Parameters

// Parameterstruct
typedef struct params {

    uint8_t uint8;
    uint16_t uint16;
    uint32_t uint32;
    uint64_t uint_64;

    int8_t int8;
    int16_t int16;
    int32_t int32;
    int64_t int_64;

    float float4;
    double double8;

    int16_t curve1[16]; // A curve with 16 points and fixed axis

    double curve2[8];     // A curve with 8 points and shared axis curve_axis
    float curve2_axis[8]; // Axis

    int8_t map1[8][8]; // A map with 8x8 points and fix axis

    int32_t map2[4][8];      // A map with 8x4 points and shared axis
    int16_t map2_x_axis[8];  // X-axis
    uint16_t map2_y_axis[4]; // Y-axis

    uint64_t map3[4][4];    // A map with 8x4 points and shared axis and fixed axis
    int64_t map3_x_axis[8]; // X-axis

} params_t;

// A const instance of the params_t struct
const params_t params = {
    .uint8 = 0,
    .uint16 = 0,
    .uint32 = 0,
    .uint_64 = 0,
    .int8 = 0,
    .int16 = 0,
    .int32 = 0,
    .int_64 = 0,

    .float4 = 0.0f,
    .double8 = 0.0,

    .curve1 = {0, 1, 2, 3, 4, 3, 2, 1, 0, -1, -2, -3, -4, -3, -2, -1},
    .curve2 = {0, 1, 2, 3, 4, 3, 2, 1},
    .curve2_axis = {0, 1, 2, 4, 6, 9, 13, 15},

    .map1 = {{0, 0, 0, 0, 0, 0, 0, 0},
             {0, 1, 1, 1, 1, 1, 0, 0},
             {0, 1, 3, 3, 3, 1, 0, 0},
             {0, 1, 3, 3, 3, 1, 0, 0},
             {0, 1, 3, 3, 3, 1, 0, 0},
             {0, 1, 1, 1, 1, 1, 0, 0},
             {0, 0, 0, 0, 0, 0, 0, 0},
             {0, 0, 0, 0, 0, 0, 0, 0}},

    .map2 =
        {
            {0, 0, 0, 0, 0, 0, 0, 0},
            {0, 1, 1, 1, 1, 1, 0, 0},
            {0, 1, 3, 3, 3, 1, 0, 0},
            {0, 1, 3, 3, 3, 1, 0, 0},
        },

    .map2_x_axis = {0, 1, 2, 3, 4, 5, 6, 7},
    .map2_y_axis = {0, 1, 2, 3},

    .map3 =
        {
            {0, 0, 0, 0},
            {0, 1, 1, 1},
            {0, 1, 3, 3},
            {0, 1, 3, 3},
        },

};

// A static instance of the params_t struct in static memory
static params_t static_params = {
    .uint8 = 0,
    .uint16 = 0,
    .uint32 = 0,
    .uint_64 = 0,
    .int8 = 0,
    .int16 = 0,
    .int32 = 0,
    .int_64 = 0,

    .float4 = 0.0f,
    .double8 = 0.0,

    .curve1 = {0, 1, 2, 3, 4, 3, 2, 1, 0, -1, -2, -3, -4, -3, -2, -1},
    .curve2 = {0, 1, 2, 3, 4, 3, 2, 1},
    .curve2_axis = {0, 1, 2, 4, 6, 9, 13, 15},

    .map1 = {{0, 0, 0, 0, 0, 0, 0, 0},
             {0, 1, 1, 1, 1, 1, 0, 0},
             {0, 1, 3, 3, 3, 1, 0, 0},
             {0, 1, 3, 3, 3, 1, 0, 0},
             {0, 1, 3, 3, 3, 1, 0, 0},
             {0, 1, 1, 1, 1, 1, 0, 0},
             {0, 0, 0, 0, 0, 0, 0, 0},
             {0, 0, 0, 0, 0, 0, 0, 0}},

    .map2 =
        {
            {0, 0, 0, 0, 0, 0, 0, 0},
            {0, 1, 1, 1, 1, 1, 0, 0},
            {0, 1, 3, 3, 3, 1, 0, 0},
            {0, 1, 3, 3, 3, 1, 0, 0},
        },

    .map2_x_axis = {0, 1, 2, 3, 4, 5, 6, 7},
    .map2_y_axis = {0, 1, 2, 3},

    .map3 =
        {
            {0, 0, 0, 0},
            {0, 1, 1, 1},
            {0, 1, 3, 3},
            {0, 1, 3, 3},
        },

};

// Single parameters in static memory
static uint32_t static_counter_max = 60000;
static uint8_t static_uint8 = 0;
static uint16_t static_uint16 = 1;
static uint32_t static_uint32 = 2;
static uint64_t static_uint64 = 3;
static int8_t static_int8 = 4;
static int16_t static_int16 = 5;
static int32_t static_int32 = 6;
static int64_t static_int64 = 7;
static float static_float4 = 8.0f;
static double static_double8 = 9.0;

// Curves and maps as static parameters
int16_t static_curve1[16] = {0, 1, 2, 3, 4, 3, 2, 1, 0, -1, -2, -3, -4, -3, -2, -1}; // A curve with 16 points and fixed axis
double static_curve2[8] = {0, 1, 2, 3, 4, 3, 2, 1};                                  // A curve with 8 points and shared axis curve_axis
float static_curve2_axis[8] = {0, 1, 2, 4, 6, 9, 13, 15};                            // Axis
int8_t static_map1[8][8] = {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 1, 1, 1, 1, 1, 0, 0}, {0, 1, 3, 3, 3, 1, 0, 0}, {0, 1, 3, 3, 3, 1, 0, 0},
                            {0, 1, 3, 3, 3, 1, 0, 0}, {0, 1, 1, 1, 1, 1, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}}; // A map with 8x8 points and fix axis
int32_t static_map2[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 1, 0, 0},
    {0, 1, 3, 3, 3, 1, 0, 0},
    {0, 1, 3, 3, 3, 1, 0, 0},
}; // A map with 8x4 points and shared axis
int16_t static_map2_x_axis[8] = {0, 1, 2, 3, 4, 5, 6, 7};                              // X-axis
uint16_t static_map2_y_axis[4] = {0, 1, 2, 3};                                         // Y-axis
uint64_t static_map3[4][4] = {{0, 0, 0, 0}, {0, 1, 1, 1}, {0, 1, 3, 3}, {0, 1, 3, 3}}; // A map with 8x4 points and shared axis and fixed axis
int64_t static_map3_x_axis[8] = {0, 2, 5, 10};                                         // X-axis

//-----------------------------------------------------------------------------------------------------

int main() {

    printf("A2l Generation Test:\n");
    printf("====================\n");

    // XCP must be initialized and activated before A2L generation
    // Initialize the XCP singleton, activate XCP
    // If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_EPK, true);
    XcpSetLogLevel(OPTION_LOG_LEVEL); // Set the log level for XCP

    // No need to start the XCP server
    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------------------------
    // Calibration

    // Calibration of parameters in global memory without a calibration segment
    // Thead safety is assured by the sync event
    // Create the calibration sync event for static parameters
    tXcpEventId sync = XcpCreateEvent("sync", 0, 0);
    A2lSetDynAddrMode(sync, 1, &static_uint8);
    A2lBeginGroup("Global", "Parameters in global memory", true);

    // Create individual parameters in global memory
    A2lCreateParameter(static_counter_max, "Test period in ms (default 10s)", "ms", 0, 1000 * 10);
    A2lCreateParameter(static_uint8, "Global memory parameter", "unit", 0, 255);
    A2lCreateParameter(static_uint16, "Global memory parameter", "unit", 0, 65535);
    A2lCreateParameter(static_uint32, "Global memory parameter", "unit", 0, 4294967295);
    A2lCreateParameter(static_uint64, "Global memory parameter", "unit", 0, 1E15);
    A2lCreateParameter(static_int8, "Global memory parameter", "unit", -128, 127);
    A2lCreateParameter(static_int16, "Global memory parameter", "unit", -32768, 32767);
    A2lCreateParameter(static_int32, "Global memory parameter", "unit", -2147483647 - 1, 2147483647);
    A2lCreateParameter(static_float4, "Global memory parameter", "unit", -1000.0, 1000.0);
    A2lCreateParameter(static_double8, "Global memory parameter", "unit", -1000.0, 1000.0);
    A2lCreateCurve(static_curve1, 8, "Global memory parameter field", "unit", -20, 20);
    A2lCreateCurveWithSharedAxis(static_curve2, 8, "Global memory parameter", "unit", 0, 1000.0, "static_curve2_axis");
    A2lCreateAxis(static_curve2_axis, 8, "Global memory parameter", "unit", 0, 20);
    A2lCreateMap(static_map1, 8, 8, "Global memory parameter", "", -128, 127);
    A2lCreateMapWithSharedAxis(static_map2, 8, 4, "Global memory parameter", "", -128, 127, "static_map2_x_axis", "static_map2_y_axis");
    A2lCreateAxis(static_map2_x_axis, 8, "Global memory parameter", "unit", 0, 1000.0);
    A2lCreateAxis(static_map2_y_axis, 4, "Global memory parameter", "unit", 0, 500.0);
    A2lCreateMapWithSharedAxis(static_map3, 8, 4, "Global memory parameter", "", 0, 10000, "static_map3_x_axis", NULL);
    A2lCreateAxis(static_map3_x_axis, 8, "Global memory parameter", "unit", 0, 1000.0);

    // Create parameters in the struct instance params in global memory
    A2lCreateParameter(static_params.uint8, "Global memory parameter struct field", "unit", 0, 255);
    A2lCreateParameter(static_params.uint16, "Global memory parameter struct field", "unit", 0, 65535);
    A2lCreateParameter(static_params.uint32, "Global memory parameter struct field", "unit", 0, 4294967295);
    A2lCreateParameter(static_params.uint_64, "Global memory parameter struct field", "unit", 0, 1e15);
    A2lCreateParameter(static_params.int8, "Global memory parameter struct field", "unit", -128, 127);
    A2lCreateParameter(static_params.int16, "Global memory parameter struct field", "unit", -32768, 32767);
    A2lCreateParameter(static_params.int32, "Global memory parameter struct field", "unit", -2147483647 - 1, 2147483647);
    A2lCreateParameter(static_params.int_64, "Global memory parameter struct field", "unit", -1e14, 1e14);
    A2lCreateParameter(static_params.float4, "Global memory parameter struct field", "unit", -1000.0, 1000.0);
    A2lCreateParameter(static_params.double8, "Global memory parameter struct field", "unit", -1000.0, 1000.0);
    A2lCreateCurve(static_params.curve1, 8, "Global memory parameter struct field", "unit", -20, 20);
    A2lCreateCurveWithSharedAxis(static_params.curve2, 8, "Global memory parameter struct field", "unit", 0, 1000.0, "static_params.curve2_axis");
    A2lCreateAxis(static_params.curve2_axis, 8, "Global memory parameter struct field", "unit", 0, 20);
    A2lCreateMap(static_params.map1, 8, 8, "Global memory parameter struct field", "", -128, 127);
    A2lCreateMapWithSharedAxis(static_params.map2, 8, 4, "Global memory parameter struct field", "", -128, 127, "static_params.map2_x_axis", "static_params.map2_y_axis");
    A2lCreateAxis(static_params.map2_x_axis, 8, "Global memory parameter struct field", "unit", 0, 1000.0);
    A2lCreateAxis(static_params.map2_y_axis, 4, "Global memory parameter struct field", "unit", 0, 500.0);
    A2lCreateMapWithSharedAxis(static_params.map3, 8, 4, "Global memory parameter struct field", "", 0, 127, "static_params.map3_x_axis", NULL);
    A2lCreateAxis(static_params.map3_x_axis, 8, "Global memory parameter struct field", "unit", 0, 1000.0);

    A2lEndGroup();

    // Create a calibration segment for the calibration parameter struct
    // This segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independent page switching, checksum calculation and reinitialization (copy reference page to working page)
    tXcpCalSegIndex calseg1 = XcpCreateCalSeg("params", (const void *)&params, sizeof(params));
    assert(calseg1 != XCP_UNDEFINED_CALSEG); // Ensure the calibration segment was created successfully
    A2lTypedefBegin(params_t, &params, "Calibration parameters typedef");
    A2lTypedefParameterComponent(uint8, "Parameter typedef field", "unit", 0, 255);
    A2lTypedefParameterComponent(uint16, "Parameter typedef field", "unit", 0, 65535);
    A2lTypedefParameterComponent(uint32, "Parameter typedef field", "unit", 0, 4294967295);
    A2lTypedefParameterComponent(uint_64, "Parameter typedef field", "unit", 0, 1e15);
    A2lTypedefParameterComponent(int8, "Parameter typedef field", "unit", -128, 127);
    A2lTypedefParameterComponent(int16, "Parameter typedef field", "unit", -32768, 32767);
    A2lTypedefParameterComponent(int32, "Parameter typedef field", "unit", -2147483647 - 1, 2147483647);
    A2lTypedefParameterComponent(int_64, "Parameter typedef field", "unit", -1e6, 1e6);
    A2lTypedefParameterComponent(float4, "Parameter typedef field", "unit", -1000.0, 1000.0);
    A2lTypedefParameterComponent(double8, "Parameter typedef field", "unit", -1000.0, 1000.0);
    A2lTypedefCurveComponent(curve1, 16, "Parameter typedef field", "unit", -20, 20);
    A2lTypedefCurveComponentWithSharedAxis(curve2, 8, "Parameter typedef field", "unit", 0, 1000.0, "curve2_axis");
    A2lTypedefAxisComponent(curve2_axis, 8, "Parameter typedef field", "unit", 0, 20);
    A2lTypedefMapComponent(map1, 8, 8, "Parameter typedef field", "", -128, 127);
    A2lTypedefMapComponentWithSharedAxis(map2, 8, 4, "Parameter typedef field", "", -128, 127, "map2_x_axis", "map2_y_axis");
    A2lTypedefAxisComponent(map2_x_axis, 8, "Parameter typedef field", "unit", 0, 1000.0);
    A2lTypedefAxisComponent(map2_y_axis, 4, "Parameter typedef field", "unit", 0, 500.0);
    A2lTypedefMapComponentWithSharedAxis(map3, 4, 4, "Parameter typedef field", "", 0, 127, "map3_x_axis", NULL);
    A2lTypedefAxisComponent(map3_x_axis, 4, "Parameter typedef field", "unit", 0, 1000.0);
    A2lTypedefEnd();

    A2lSetSegmentAddrMode(calseg1, params);
    A2lCreateTypedefInstance(params, params_t, "Parameter typedef instance in calibration segment");

    // Create a calibration segment with individual calibration parameters
    tXcpCalSegIndex calseg2 = XcpCreateCalSeg("params2", (const void *)&params, sizeof(params));
    assert(calseg2 != XCP_UNDEFINED_CALSEG); // Ensure the calibration segment was created successfully
    A2lSetSegmentAddrMode(calseg2, params);
    A2lCreateParameter(params.uint8, "Parameter in calibration segment", "unit", 0, 255);
    A2lCreateParameter(params.uint16, "Parameter in calibration segment", "unit", 0, 65535);
    A2lCreateParameter(params.uint32, "Parameter in calibration segment", "unit", 0, 4294967295);
    A2lCreateParameter(params.uint_64, "Parameter in calibration segment", "unit", 0, 1e19);
    A2lCreateParameter(params.int8, "Parameter in calibration segment", "unit", -128, 127);
    A2lCreateParameter(params.int16, "Parameter in calibration segment", "unit", -32768, 32767);
    A2lCreateParameter(params.int32, "Parameter in calibration segment", "unit", -2147483647 - 1, 2147483647);
    A2lCreateParameter(params.int_64, "Parameter in calibration segment", "unit", -1e6, 1e6);
    A2lCreateParameter(params.float4, "Parameter in calibration segment", "unit", -1000.0, 1000.0);
    A2lCreateParameter(params.double8, "Parameter in calibration segment", "unit", -1000.0, 1000.0);
    A2lCreateCurve(params.curve1, 8, "Parameter in calibration segment", "unit", -20, 20);
    A2lCreateMap(params.map1, 8, 8, "Parameter in calibration segment", "unit", -128, 127);
    A2lCreateAxis(params.curve2_axis, 8, "Comment", "unit", 0, 20);
    A2lCreateAxis(params.map2_x_axis, 8, "Comment", "unit", 0, 1000.0);
    A2lCreateAxis(params.map2_y_axis, 4, "Comment", "unit", 0, 500.0);
    A2lCreateAxis(params.map3_x_axis, 8, "Comment", "unit", 0, 1000.0);
    A2lCreateCurveWithSharedAxis(params.curve2, 8, "Comment", "unit", 0, 1000.0, "params.curve2_axis");
    A2lCreateMapWithSharedAxis(params.map2, 8, 4, "Comment", "", -128, 127, "params.map2_x_axis", "params.map2_y_axis");
    A2lCreateMapWithSharedAxis(params.map3, 8, 4, "Comment", "", 0, 127, "params.map3_x_axis", NULL);

    //---------------------------------------------------------------------------------------------------------------------------------
    // Measurement

    // Events
    DaqCreateEvent(event);

    // Global measurement variables of basic types
    A2lSetAbsoluteAddrMode(event);
    ;
    A2lCreatePhysMeasurement(uint8, "Enumeration type value uint8_t",
                             A2lCreateEnumConversion(enum_conversion, "5 0 \"SINE\" 1 \"SQUARE\" 2 \"TRIANGLE\" 3 \"SAWTOOTH\" 4 \"ARBITRARY\""), 0, 4);
    ;
    A2lCreatePhysMeasurement(uint16, "uint16_t value with linear conversion", A2lCreateLinearConversion(linear_conversion, "Temperature as uint8*2-50", "°C", 2.0, -50.0), -50.0,
                             +300.0);
    A2lCreatePhysMeasurement(uint32, "uint32_t", "unit", 0, 4294967295.0);
    A2lCreatePhysMeasurement(uint_64, "uint64_t", "unit", 0, 1e14);
    A2lCreatePhysMeasurement(int8, "int8_t", "unit", -128.0, 127.0);
    A2lCreatePhysMeasurement(int16, "int16_t", "unit", -32768.0, 32767.0);
    A2lCreatePhysMeasurement(int32, "int32_t", "unit", -2147483648.0, 2147483647.0);
    A2lCreatePhysMeasurement(int_64, "int64_t", "unit", -1e14, 1e14);
    A2lCreatePhysMeasurement(float4, "float4", "unit", -1000.0, 1000.0);
    A2lCreatePhysMeasurement(double8, "double8", "unit", -1000.0, 1000.0);

    // Global measurement variables of multidimensional basic types
    A2lCreateMeasurementArray(array, "int16_t array");
    A2lCreateMeasurementMatrix(matrix, "double matrix");

    // Local (stack) variables of basic types
    uint8_t local_uint8 = 0;
    uint16_t local_uint16 = 1;
    uint32_t local_uint32 = 2;
    uint64_t local_uint64 = 3;
    int8_t local_int8 = 4;
    int16_t local_int16 = 5;
    int32_t local_int32 = 6;
    int64_t local_int64 = 7;
    float local_float4 = 8.0f;
    double local_double8 = 9.0;
    // Multidimensional
    int16_t local_array[16] = {0, 1, 2, 3, 4, 3, 2, 1, 0, -1, -2, -3, -4, -3, -2, -1};
    double local_matrix[2][2] = {{0, 1}, {2, 3}};

    A2lSetStackAddrMode(event);
    A2lCreatePhysMeasurement(local_uint8, "Boolean value", "conv.bool", 0, 1);
    A2lCreateMeasurement(local_uint16, "Integer value");
    A2lCreateMeasurement(local_uint32, "Integer value");
    A2lCreateMeasurement(local_uint64, "Integer value");
    A2lCreateMeasurement(local_int8, "Integer value");
    A2lCreateMeasurement(local_int16, "Integer value");
    A2lCreateMeasurement(local_int32, "Integer value");
    A2lCreateMeasurement(local_int64, "Integer value");
    A2lCreatePhysMeasurement(local_float4, "float4", "conv.linear_conversion", -1000.0, 1000.0);
    A2lCreatePhysMeasurement(local_double8, "double8", "conv.linear_conversion", -1000.0, 1000.0);
    A2lCreateMeasurementArray(local_array, "int16_t array");
    A2lCreatePhysMeasurementMatrix(local_matrix, "double matrix", "conv.linear_conversion", 0.0, 10.0);

    // Register measurement structs

    A2lTypedefBegin(struct2_t, &struct2, "A2L typedef for struct2_t");
    A2lTypedefMeasurementComponent(byte_field, "Byte field");
    A2lTypedefMeasurementComponent(word_field, "Word field");
    A2lTypedefEnd();

    A2lTypedefBegin(struct1_t, &struct1, "A2L typedef for struct1_t");
    A2lTypedefMeasurementComponent(byte_field, "Byte field");
    A2lTypedefMeasurementComponent(word_field, "Word field");
    A2lTypedefMeasurementArrayComponent(array_field, "Array field");
    A2lTypedefComponent(struct_field, struct1_t, 1);
    A2lTypedefEnd();

    // Local (Stack) variables of struct type
    struct2_t local_struct2 = {.byte_field = 1, .word_field = 2};                                                                                  // Single instance of struct2_t
    struct1_t local_struct1 = {.byte_field = 1, .word_field = 2, .array_field = {0, 1, 2, 3}, .struct_field = {.byte_field = 1, .word_field = 2}}; // Single instance of struct1_t
    struct1_t local_struct1_array[8];                                                                                                              // Array of struct1_t

    // Heap
    struct1_t *heap_struct1 = malloc(sizeof(struct1_t)); // Pointer to a struct1_t on the heap
    *heap_struct1 = local_struct1;

    // Stack
    A2lSetStackAddrMode(event); // stack relative addressing mode
    A2lCreateTypedefInstance(local_struct2, struct2_t, "Instance of test_struct2_t");
    A2lCreateTypedefInstance(local_struct1, struct1_t, "Instance of test_struct1_t");
    A2lCreateTypedefInstanceArray(local_struct1_array, struct1_t, 8, "Array [10] of struct1_t");

    // static/global
    A2lSetAbsoluteAddrMode(event); // absolute addressing mode
    A2lCreateTypedefInstance(struct2, struct2_t, "Instance of test_struct2_t");
    A2lCreateTypedefInstance(struct1, struct1_t, "Instance of test_struct1_t");
    A2lCreateTypedefInstanceArray(struct1_array, struct1_t, 16, "Array [16] of struct1_t");

    // Heap
    DaqCreateEvent(event_heap);
    A2lSetRelativeAddrMode(event_heap, heap_struct1); // relative addressing mode for heap_struct1_array
    A2lCreateTypedefReference(heap_struct1, struct1_t, "Pointer to struct1_t on heap");

    A2lFinalize();
    assert(A2lCheckFinalizeOnConnect(0)); // XCP connect is now allowed, the A2L file is finalized

    // Execute A2L checker using Rust a2ltool https://github.com/DanielT/a2ltool
    /*
    Options:
    -t, --enable-structures
            Enable the the use of INSTANCE, TYPEDEF_STRUCTURE & co. for all operations.
            Requires a2l version 1.7.1
    -s, --strict
            Parse all input in strict mode. An error wil be reported if the file has any
            inconsistency.
    -v, --verbose...
            Display additional information
        --debug-print
            Display internal data for debugging
        --ifdata-cleanup
            Remove all IF_DATA blocks that cannot be parsed according to A2ML
        --show-xcp
            Display the XCP settings in the a2l file, if they exist
    */
    printf("Running A2L validation tool...\n");
    // int result = system(A2lTOOL_PATH " --check --strict a2l_test.a2l");
    int result = system(A2lTOOL_PATH " --check  a2l_test.a2l");
    if (result == 0) {
        printf("A2L validation passed\n");
    } else {
        printf("A2L validation failed with exit code: %d\n", result);
    }

    // Compare a2l_test.a2l to the expected output in test/a2l_test/a2l_test_expected.a2l
    // printf("Comparing generated A2L file with expected output...\n");
    // result = system("diff a2l_test.a2l ./test/a2l_test/a2l_test_expected.a2l");
    // if (result == 0) {
    //     printf("A2L file matches expected output\n");
    // } else if (result == 1) {
    //     printf("A2L file does not match expected output\n");
    // } else {
    //     printf("Error comparing A2L file, exit code: %d\n", result);
    // }

    // Run A2l update
    // printf("Running A2L update tool...\n");
    // result = system(A2lTOOL_PATH "  --verbose --elffile build/a2l_test --update  --output a2l_test_updated.a2l  a2l_test.a2l");
    // if (result == 0) {
    //     printf("A2L update passed\n");
    // } else {
    //     printf("A2L update failed with exit code: %d\n", result);
    // }

    // Compare a2l_test.a2l to the updated output in a2l_test_updated.a2l
    // printf("Comparing updated A2L file with original...\n");
    // result = system("diff a2l_test.a2l a2l_test_updated.a2l");
    // if (result == 0) {
    //     printf("Updated A2L file matches expected output\n");
    // } else if (result == 1) {
    //     printf("Updated A2L file does not match expected output\n");
    // } else {
    //     printf("Error comparing A2L file, exit code: %d\n", result);
    // }

    printf("Exit...\n");
    return 0;
}
