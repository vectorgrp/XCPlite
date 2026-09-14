// external_example - C version demonstrating libxcplite as external library

// This example shows how to use libxcplite when it's installed as a binary
// library (either system-wide or in a local staging directory).
//
// The code is designed to demonstrate:
// - Including libxcplite headers from an installed location
// - Linking against the pre-built libxcplite library
// - Basic XCP measurement and calibration functionality for global variables

// Disclaimer:
// This example does not use calibration segments, so thread safety is not guaranteed for direct write access to global calibration variables.

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Include libxcplite headers from the installed location
#include <a2l.h>
#include <xcplib.h>

//-----------------------------------------------------------------------------------------------------
// XCP configuration

#define OPTION_PROJECT_NAME "external_example"
#define OPTION_PROJECT_VERSION "V2.1.10"
#define OPTION_USE_TCP true
#define OPTION_SERVER_PORT 5555
#define OPTION_SERVER_ADDR {0, 0, 0, 0}
#define OPTION_QUEUE_SIZE (1024 * 32)
#define OPTION_LOG_LEVEL 4

#define OPTION_ENABLE_APP_ADDRESSING // Option to demonstrate application specific memory access

//-----------------------------------------------------------------------------------------------------
// Application variables

// Simple calibration parameter
uint32_t loop_delay_us = 1000;

// Simple measurement variable
uint32_t counter = 0;

//-----------------------------------------------------------------------------------------------------
// Signal handling

static volatile bool g_appRunning = true;

static void signalHandler(int sig) {
    (void)sig;
    printf("\nShutdown signal received\n");
    g_appRunning = false;
}

//-----------------------------------------------------------------------------------------------------
// User implemented  memory access
// Emulate memory access to a user specific virtual address range, without using a calibration segment
// Could be a file, or flash memory, or a memory-mapped peripheral, etc.
#ifdef OPTION_ENABLE_APP_ADDRESSING

#pragma pack(push, 1)
struct AppMemory {
    uint8_t test_byte;
    uint16_t test_word;
    uint32_t test_dword;
} app_memory = {
    .test_byte = 1,
    .test_word = 2,
    .test_dword = 4,
};
#pragma pack(pop)

// Write callback for application defined memory access, called by XCP when the master writes to an address with the application specific address extension
uint8_t cb_read(uint32_t src, uint8_t size, uint8_t *dst) {
    if (src + size > sizeof(app_memory)) {
        printf("ERROR: Read out of bounds: src=%u, size=%u\n", src, size);
        return CRC_ACCESS_DENIED;
    }
    memcpy(dst, (uint8_t *)&app_memory + src, size);
    return CRC_CMD_OK;
}

// Write callback for applicatiosn defined memory access, called by XCP when the master writes to an address with the application specific address extension
uint8_t cb_write(uint32_t dst, uint8_t size, const uint8_t *src, uint8_t delay) {

    (void)delay; // Not used in this example, but could be used to implement delayed writes for consistent calibration updates

    if (dst + size > sizeof(app_memory)) {
        printf("ERROR: Write out of bounds: dst=%u, size=%u\n", dst, size);
        return CRC_ACCESS_DENIED;
    }
    memcpy((uint8_t *)&app_memory + dst, src, size);
    return CRC_CMD_OK;
}

#endif

//-----------------------------------------------------------------------------------------------------
// Main

int main(void) {

    // Install signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Set XCP log level
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize XCP
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_LOCAL);
#ifdef OPTION_ENABLE_APP_ADDRESSING
    ApplXcpRegisterReadCallback(cb_read);
    ApplXcpRegisterWriteCallback(cb_write);
#endif

    // Initialize XCP Ethernet server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        printf("ERROR: XCP initialization failed\n");
        return 1;
    }

    printf("XCP server listening on %s port %d\n", OPTION_USE_TCP ? "TCP" : "UDP", OPTION_SERVER_PORT);
    printf("Connect CANape to this address to start measurement\n\n");

    // Enable A2L generation
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT)) {
        return 1;
    }

    // Create a global calibration parameter (not using a calibration segment, thread safety not guaranteed)
    A2lCreateParameter(loop_delay_us, "Loop delay in microseconds", "us", 100, 100000);

    // Create a calibration parameter in a user specific virtual address range, without using a calibration segment
#ifdef OPTION_ENABLE_APP_ADDRESSING
    ApplXcpSetBaseAddr((const uint8_t *)&app_memory); // Set the base address for application specific addressing mode to enable correct address calculation for the A2L file
    A2lSetApplicationAddrMode();
    A2lCreateParameter(app_memory.test_byte, "Test byte in application specific memory", "", 0, 255);
    A2lCreateParameter(app_memory.test_word, "Test word in application specific memory", "", 0, 65535);
    A2lCreateParameter(app_memory.test_dword, "Test dword in application specific memory", "", 0, 4294967295);
    ApplXcpSetBaseAddr(NULL); // Reset to default module base address
#endif

    // Create a measurement event and a global measurement variable
    DaqCreateEvent(MainTask);
    A2lCreateMeasurement(counter, "Incrementing counter");

    printf("Starting main loop (press Ctrl+C to stop)...\n\n");

    // Main application loop
    while (g_appRunning) {

        // Increment counter
        counter++;

        // Trigger XCP measurement event
        DaqTriggerEvent(MainTask);

        // Sleep using calibration value
        usleep(loop_delay_us);

        // Print status every 1000 iterations
        if (counter % 1000 == 0) {
            printf("Counter: %u, Delay: %u us\n", counter, loop_delay_us);
        }
    }

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
    return 0;
}
