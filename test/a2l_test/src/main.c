
#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <stdlib.h>  // for system
#include <string.h>  // for sprintf

#include "a2l.h"    // for xcplib A2l generation
#include "xcplib.h" // for xcplib application programming interface

#include "xcpLite.h"

// Not public API
extern bool A2lCheckFinalizeOnConnect(void);

// static bool file_exists(const char *path) {
//     FILE *file = fopen(path, "r");
//     if (file) {
//         fclose(file);
//         return true;
//     }
//     return false;
// }

#define OPTION_PROJECT_NAME "a2l_test"    // A2L project name
#define OPTION_LOG_LEVEL 4                // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug
#define OPTION_USE_TCP false              // TCP or UDP
#define OPTION_SERVER_PORT 5555           // Port
#define OPTION_SERVER_ADDR {127, 0, 0, 1} // Bind addr, 0.0.0.0 = ANY

//-----------------------------------------------------------------------------------------------------
// Measurements

uint8_t uint8 = 0;
uint16_t uint16 = 1;
uint32_t uint32 = 2;
uint64_t uint64 = 3;
int8_t int8 = 4;
int16_t int16 = 5;
int32_t int32 = 6;
int64_t int64 = 7;
float float4 = 8.0f;
double double8 = 9.0;

//-----------------------------------------------------------------------------------------------------
// Parameters

typedef struct params {

    uint16_t uint8;
    uint16_t uint16;
    uint32_t uint32;
    uint64_t uint64;

    int8_t int8;
    int16_t int16;
    int32_t int32;
    int64_t int64;

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

params_t params = {
    .uint8 = 0,
    .uint16 = 0,
    .uint32 = 0,
    .uint64 = 0,
    .int8 = 0,
    .int16 = 0,
    .int32 = 0,
    .int64 = 0,

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

int main() {

    printf("A2l Generation Test:\n");
    printf("====================\n");

    // XCP must be initialized and activated before A2L generation
    // Initialize the XCP singleton, activate XCP
    // If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    XcpInit(true);
    XcpSetLogLevel(OPTION_LOG_LEVEL); // Set the log level for XCP

    // No need to start the XCP server
    assert(XcpIsInitialized());
    assert(!XcpIsStarted());

    // Initialize the A2L generator in manual finalization mode with auto group generation
    // Empty version string to allow diff with the expected output
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!A2lInit(OPTION_PROJECT_NAME, "", addr, OPTION_SERVER_PORT, OPTION_USE_TCP, true /*write_always*/, false /*finalize_on_connect*/, true /*auto_grouping*/)) {
        return 1;
    }

    // XCP connect should be refused until the A2L file is finalized
    printf("Test connection refusal:\n");
    assert(!A2lCheckFinalizeOnConnect());

    // Create a calibration segment for the calibration parameter struct
    // This segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independent page switching, checksum calculation and reinitialization (copy reference page to working page)
    tXcpCalSegIndex calseg1 = XcpCreateCalSeg("segment_1", &params, sizeof(params));
    assert(calseg1 != XCP_UNDEFINED_CALSEG); // Ensure the calibration segment was created successfully
    A2lTypedefBegin(params_t, "comment");

    A2lTypedefParameterComponent(uint8, params_t, "comment", "unit", 0, 255);
    A2lTypedefParameterComponent(uint16, params_t, "comment", "unit", 0, 65535);
    A2lTypedefParameterComponent(uint32, params_t, "comment", "unit", 0, 4294967295);
    A2lTypedefParameterComponent(uint64, params_t, "comment", "unit", 0, 18446744073709551615ULL);

    A2lTypedefParameterComponent(int8, params_t, "comment", "unit", -128, 127);
    A2lTypedefParameterComponent(int16, params_t, "comment", "unit", -32768, 32767);
    A2lTypedefParameterComponent(int32, params_t, "comment", "unit", -2147483648, 2147483647);
    A2lTypedefParameterComponent(int64, params_t, "comment", "unit", -9223372036854775807LL, 9223372036854775807LL);

    A2lTypedefParameterComponent(float4, params_t, "comment", "unit", -1000.0, 1000.0);
    A2lTypedefParameterComponent(double8, params_t, "comment", "unit", -1000.0, 1000.0);

    A2lTypedefCurveComponent(curve1, params_t, 8, "comment", "unit", -20, 20);

    A2lTypedefCurveComponentWithSharedAxis(curve2, params_t, 8, "comment", "unit", 0, 1000.0, "curve2_axis");
    A2lTypedefAxisComponent(curve2_axis, params_t, 8, "comment", "unit", 0, 20);

    A2lTypedefMapComponent(map1, params_t, 8, 8, "comment", "", -128, 127);

    A2lTypedefMapComponentWithSharedAxis(map2, params_t, 8, 4, "comment", "", -128, 127, "map2_x_axis", "map2_y_axis");
    A2lTypedefAxisComponent(map2_x_axis, params_t, 8, "comment", "unit", 0, 1000.0);
    A2lTypedefAxisComponent(map2_y_axis, params_t, 4, "comment", "unit", 0, 500.0);

    A2lTypedefMapComponentWithSharedAxis(map3, params_t, 8, 4, "comment", "", -128, 127, "map3_x_axis", NULL);
    A2lTypedefAxisComponent(map3_x_axis, params_t, 8, "comment", "unit", 0, 1000.0);

    A2lTypedefEnd();

    A2lSetSegmentAddrMode(calseg1, params);
    A2lCreateTypedefInstance(params, params_t, "comment");

    // Create a calibration segment with individual calibration parameters
    tXcpCalSegIndex calseg2 = XcpCreateCalSeg("segment_2", &params, sizeof(params));
    assert(calseg2 != XCP_UNDEFINED_CALSEG); // Ensure the calibration segment was created successfully
    A2lSetSegmentAddrMode(calseg2, params);
    A2lCreateParameter(params, uint8, "Comment", "unit", 0, 255);
    A2lCreateParameter(params, uint16, "Comment", "unit", 0, 65535);
    A2lCreateParameter(params, uint32, "Comment", "unit", 0, 4294967295);
    A2lCreateParameter(params, uint64, "Comment", "unit", 0, 18446744073709551615ULL);
    A2lCreateParameter(params, int8, "Comment", "unit", -128, 127);
    A2lCreateParameter(params, int16, "Comment", "unit", -32768, 32767);
    A2lCreateParameter(params, int32, "Comment", "unit", -2147483648, 2147483647);
    A2lCreateParameter(params, float4, "Comment", "unit", -1000.0, 1000.0);
    A2lCreateParameter(params, double8, "Comment", "unit", -1000.0, 1000.0);
    A2lCreateCurve(params, curve1, 8, "Comment", "unit", -20, 20);
    A2lCreateMap(params, map1, 8, 8, "Comment", "", -128, 127);

    /*
    A2lCreateCurveWithSharedAxis(params, curve2, 8, "Comment", "unit", 0, 1000.0, "params.curve2_axis");
    A2lCreateAxis(params, curve2_axis, 8, "Comment", "unit", 0, 20);


    A2lCreateMapWithSharedAxis(params, map2, 8, 4, "Comment", "", -128, 127, "params.map2_x_axis", "params.map2_y_axis");
    A2lCreateAxis(params, map2_x_axis, 8, "Comment", "unit", 0, 1000.0);
    A2lCreateAxis(params, map2_y_axis, 4, "Comment", "unit", 0, 500.0);

    A2lCreateMapWithSharedAxis(params, map3, 8, 4, "Comment", "", -128, 127, "params.map3_x_axis", NULL);
    A2lCreateAxis(params, map3_x_axis, 8, "Comment", "unit", 0, 1000.0);
*/

    // Register global measurement variables
    // Set absolute addressing mode with default event
    DaqCreateEvent(event);
    A2lSetAbsoluteAddrMode(event);
    A2lCreateLinearConversion(linear_conversion, "x*2-50", "°C", 2.0, -50.0);
    A2lCreatePhysMeasurement(uint8, "uint8_t", linear_conversion, -50.0, 200.0);
    A2lCreatePhysMeasurement(uint16, "uint16_t", "unit", 0, 65535.0);
    A2lCreatePhysMeasurement(uint32, "uint32_t", "unit", 0, 4294967295.0);
    A2lCreatePhysMeasurement(uint64, "uint64_t", "unit", 0, 18446744073709551615ULL);
    A2lCreatePhysMeasurement(int8, "int8_t", "unit", -128.0, 127.0);
    A2lCreatePhysMeasurement(int16, "int16_t", "unit", -32768.0, 32767.0);
    A2lCreatePhysMeasurement(int32, "int32_t", "unit", -2147483648.0, 2147483647.0);
    A2lCreatePhysMeasurement(int64, "int64_t", "unit", -9223372036854775807.0, 9223372036854775807.0);
    A2lCreatePhysMeasurement(float4, "float4", "unit", -1000.0, 1000.0);
    A2lCreatePhysMeasurement(double8, "double8", "unit", -1000.0, 1000.0);

    A2lFinalize();
    assert(A2lCheckFinalizeOnConnect()); // XCP connect is now allowed, the A2L file is finalized

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
    int result = system("../a2ltool/target/release/a2ltool -c -s  --show-xcp a2l_test.a2l");
    if (result == 0) {
        printf("A2L validation passed\n");
    } else {
        printf("A2L validation failed with exit code: %d\n", result);
    }

    // Compare a2l_test.a2l to the expected output in test/a2l_test/a2l_test_expected.a2l
    printf("Comparing generated A2L file with expected output...\n");
    result = system("diff a2l_test.a2l ./test/a2l_test/a2l_test_expected.a2l");
    if (result == 0) {
        printf("A2L file matches expected output\n");
    } else if (result == 1) {
        printf("A2L file does not match expected output\n");
    } else {
        printf("Error comparing A2L file, exit code: %d\n", result);
    }

    // Run A2l update
    printf("Running A2L update tool...\n");
    result = system("../a2ltool/target/release/a2ltool  -e build/a2l_test.out --update  -o a2l_test_updated.a2l  a2l_test.a2l");
    if (result == 0) {
        printf("A2L update passed\n");
    } else {
        printf("A2L update failed with exit code: %d\n", result);
    }

    // Compare a2l_test.a2l to the updated output in a2l_test_updated.a2l
    printf("Comparing updated A2L file with original...\n");
    result = system("diff a2l_test.a2l a2l_test_updated.a2l");
    if (result == 0) {
        printf("Updated A2L file matches expected output\n");
    } else if (result == 1) {
        printf("Updated A2L file does not match expected output\n");
    } else {
        printf("Error comparing A2L file, exit code: %d\n", result);
    }

    printf("Done\n");
    return 0;
}
