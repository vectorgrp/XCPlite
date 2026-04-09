// external_example_cpp - C++ version demonstrating libxcplite as external library

// This example shows how to use libxcplite when it's installed as a binary
// library (either system-wide or in a local staging directory).
//
// The code is designed to demonstrate:
// - Including libxcplite headers from an installed location
// - Linking against the pre-built libxcplite library
// - Basic XCP measurement and calibration functionality for global variables

#include <csignal>
#include <cstdint>
#include <iostream>
#include <unistd.h>

// Include XCPlite/libxcplite C++ headers from the installed location
#include <a2l.hpp>
#include <xcplib.hpp>

//-----------------------------------------------------------------------------------------------------
// XCP configuration

#define OPTION_PROJECT_NAME "external_example_cpp"
#define OPTION_PROJECT_VERSION __TIME__
#define OPTION_USE_TCP true
#define OPTION_SERVER_PORT 5556
#define OPTION_SERVER_ADDR {0, 0, 0, 0}
#define OPTION_QUEUE_SIZE (1024 * 32)
#define OPTION_LOG_LEVEL 4

//-----------------------------------------------------------------------------------------------------
// Application variables

uint32_t counter_value = 0;
uint32_t loop_delay_us = 1000;

//-----------------------------------------------------------------------------------------------------
// Signal handling

static volatile bool g_running = true;

static void signalHandler(int sig) {
    (void)sig;
    std::cout << "\nShutdown signal received" << std::endl;
    g_running = false;
}

//-----------------------------------------------------------------------------------------------------
// Main

int main() {

    // Install signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Set XCP log level
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize XCP
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_LOCAL);

    // Initialize XCP Ethernet server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        std::cerr << "ERROR: XCP initialization failed" << std::endl;
        return 1;
    }

    std::cout << "XCP server listening on " << (OPTION_USE_TCP ? "TCP" : "UDP") << " port " << OPTION_SERVER_PORT << std::endl;
    std::cout << "Connect CANape to this address to start measurement\n" << std::endl;

    // Enable A2L generation
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT)) {
        return 1;
    }

    // Create a global calibration parameter (not using a calibration segment, thread safety not guaranteed)
    A2lCreateParameter(loop_delay_us, "Loop delay in microseconds", "us", 100, 100000);

    // Create measurement event and a global measurement variable
    DaqCreateEvent(MainTask);
    A2lCreateMeasurement(counter_value, "Counter value");

    std::cout << "Starting main loop (press Ctrl+C to stop)...\n" << std::endl;

    // Main application loop
    while (g_running) {
        counter_value++;

        // Trigger XCP measurement
        DaqTriggerEvent(MainTask);

        // Sleep
        usleep(loop_delay_us);

        // Print status
        if (counter_value % 1000 == 0) {
            std::cout << "Counter: " << counter_value << ", Delay: " << loop_delay_us << " us" << std::endl;
        }
    }

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server

    return 0;
}
