// fetchcontent_example_cpp - C++ version demonstrating libxcplite built from source via FetchContent

#include <csignal>
#include <cstdint>
#include <iostream>
#include <unistd.h>

// Include XCPlite/libxcplite C++ headers from the installed location
#include <a2l.hpp>
#include <xcplib.hpp>

//-----------------------------------------------------------------------------------------------------
// Runtime XCP configuration

#ifndef __XCPLIB_CFG_H__
#error "Default xcplib configuration not visible"
#endif

#define OPTION_PROJECT_NAME "fetchcontent_example_cpp"
#define OPTION_PROJECT_VERSION "V100"
#define OPTION_USE_TCP false
#define OPTION_SERVER_PORT 5555
#define OPTION_SERVER_ADDR {0, 0, 0, 0}
#define OPTION_QUEUE_SIZE (1024 * 8)
#define OPTION_LOG_LEVEL 4

// With calibration segment persistence enabled (xcplib_cfg_app.h)
#ifdef OPTION_ENABLE_A2L_GENERATOR
#ifdef OPTION_ENABLE_PERSISTENCE
#define OPTION_XCP_MODE (XCP_MODE_PERSISTENCE | XCP_MODE_LOCAL)
#define OPTION_A2L_MODE (A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT)
#else
#define OPTION_XCP_MODE (XCP_MODE_LOCAL)
#define OPTION_A2L_MODE (A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT)
#endif
#else
#define OPTION_XCP_MODE (XCP_MODE_LOCAL)
#endif

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
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, OPTION_XCP_MODE);

    // Initialize XCP Ethernet server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        std::cerr << "ERROR: XCP initialization failed" << std::endl;
        return 1;
    }

    std::cout << "XCP server listening on " << (OPTION_USE_TCP ? "TCP" : "UDP") << " port " << OPTION_SERVER_PORT << std::endl;
    std::cout << "Connect CANape to this address to start measurement\n" << std::endl;

    // Create measurement event and a global measurement variable
    DaqCreateEvent(MainTask);

#ifdef OPTION_ENABLE_A2L_GENERATOR
    // Enable A2L generation
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_A2L_MODE)) {
        return 1;
    }

    A2lSetAbsoluteAddrMode(MainTask);
    A2lCreateMeasurement(counter_value, "Counter value");

    // Create a global calibration parameter (not using a calibration segment, thread safety not guaranteed)
    A2lCreateParameter(loop_delay_us, "Loop delay in microseconds", "us", 100, 100000);
#endif

    std::cout << "Starting main loop (press Ctrl+C to stop)...\n" << std::endl;

    // Main application loop
    while (g_running) {
        counter_value++;

        // Trigger XCP measurement
        DaqTriggerEvent(MainTask);

        // Sleep
        usleep(loop_delay_us);
    }

    XcpDisconnect(); // Force disconnect the XCP client
#ifdef OPTION_ENABLE_A2L_GENERATOR
    A2lFinalize(); // Finalize A2L generation, if not done yet
#endif
    XcpEthServerShutdown(); // Stop the XCP server

    return 0;
}
