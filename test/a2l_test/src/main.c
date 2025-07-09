
#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "a2l.h"    // for xcplib A2l generation
#include "xcplib.h" // for xcplib application programming interface

#define OPTION_A2L_PROJECT_NAME "a2l_test"  // A2L project name
#define OPTION_A2L_FILE_NAME "a2l_test.a2l" // A2L file name
#define OPTION_USE_TCP false                // TCP or UDP
#define OPTION_SERVER_PORT 5555             // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}     // Bind addr, 0.0.0.0 = ANY

int main() {

    printf("A2l Generation Test:\n");
    printf("====================\n");

    // Initialize the XCP singleton
    // Provides the event and calibration segment list
    XcpInit();

    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!A2lInit(OPTION_A2L_FILE_NAME, OPTION_A2L_PROJECT_NAME, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, true, true)) {
        return 1;
    }

    A2lFinalize();

    return 0;
}
