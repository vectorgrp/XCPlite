
#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "a2l.h"    // for xcplib A2l generation
#include "xcplib.h" // for xcplib application programming interface

#define OPTION_PROJECT_NAME "a2l_test"  // A2L project name
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY

typedef struct params {
    uint16_t uint8;
    uint16_t uint16;
    uint32_t uint32;
    uint64_t uint64;
    int8_t int8;
    int16_t int16;
    int32_t int32;
    int64_t int64;
    int8_t map[8][8];    // A map with 8x8 points and fix axis
    float curve[8];      // A curve with 8 points and shared axis curve_axis
    float curve_axis[8]; // Axis for the curve
} params_t;

const params_t params = {
    .uint8 = 0,
    .uint16 = 0,
    .uint32 = 0,
    .uint64 = 0,
    .int8 = 0,
    .int16 = 0,
    .int32 = 0,
    .int64 = 0,

    .map = {{0, 0, 0, 0, 0, 0, 0, 0},
            {0, 1, 1, 1, 1, 1, 0, 0},
            {0, 1, 3, 3, 3, 1, 0, 0},
            {0, 1, 3, 3, 3, 1, 0, 0},
            {0, 1, 3, 3, 3, 1, 0, 0},
            {0, 1, 1, 1, 1, 1, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0}},
    .curve = {0, 1, 2, 3, 4, 3, 2, 1},
    .curve_axis = {0, 1, 2, 4, 6, 9, 13, 15},
};

int main() {

    printf("A2l Generation Test:\n");
    printf("====================\n");

    // Initialize the XCP singleton, activate XCP, must be called before starting the server
    // If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    XcpInit(true);

    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!A2lInit(OPTION_PROJECT_NAME, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, true, true, true)) {
        return 1;
    }

    // Create a calibration segment for the calibration parameter struct
    // This segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independent page switching, checksum calculation and reinitialization (copy reference page to working page)
    tXcpCalSegIndex calseg = XcpCreateCalSeg("segment_name", &params, sizeof(params));
    assert(calseg != XCP_UNDEFINED_CALSEG); // Ensure the calibration segment was created successfully
    A2lTypedefBegin(params_t, "comment");
    A2lTypedefParameterComponent(uint8, params_t, "comment", "unit", 0, 255);
    A2lTypedefParameterComponent(uint16, params_t, "comment", "unit", 0, 65535);
    A2lTypedefParameterComponent(uint32, params_t, "comment", "unit", 0, 4294967295);
    A2lTypedefParameterComponent(uint64, params_t, "comment", "unit", 0, 18446744073709551615ULL);
    A2lTypedefParameterComponent(int8, params_t, "comment", "unit", -128, 127);
    A2lTypedefParameterComponent(int16, params_t, "comment", "unit", -32768, 32767);
    A2lTypedefParameterComponent(int32, params_t, "comment", "unit", -2147483648, 2147483647);
    A2lTypedefParameterComponent(int64, params_t, "comment", "unit", -9223372036854775807LL, 9223372036854775807LL);
    A2lTypedefMapComponent(map, params_t, 8, 8, "comment", "", -128, 127);
    A2lTypedefCurveComponentWithSharedAxis(curve, params_t, 8, "comment", "unit", 0, 1000.0, "curve_axis");
    A2lTypedefAxisComponent(curve_axis, params_t, 8, "comment", "unit", 0, 20);
    A2lTypedefEnd();

    A2lSetSegmentAddrMode(calseg, params);
    A2lCreateTypedefInstance(params, params_t, "comment");

    A2lFinalize();

    return 0;
}
