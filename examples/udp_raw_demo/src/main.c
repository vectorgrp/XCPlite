// udp_raw_demo - XCPlite/libxcplite demo for the raw Ethernet transport (OPTION_ENABLE_UDP_RAW)
//
// XCP on UDP/IPv4 implemented inside xcplib on top of a raw Ethernet HAL, for targets
// which have no TCP/IP stack. This demo is the Linux development and test vehicle.
//
// Unlike the other examples this one needs an explicit local IPv4 address: there is no
// IP stack and no DHCP, so binding to 0.0.0.0 (ANY) has no meaning. The interface and the
// address are therefore taken from the command line.
//
// Build and run (see docs/SOCKET_RAW.md for the full network setup):
//   ./build.sh raw examples
//   sudo setcap cap_net_raw+ep ./build-raw/udp_raw_demo
//   ./build-raw/udp_raw_demo --if eth0 --ip 192.168.1.240

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <stdlib.h>  // for strtoul
#include <string.h>  // for strcmp

// Include XCPlite/libxcplite C headers
#include <a2l.h>    // for A2l generation
#include <xcplib.h> // for application programming interface

#include "sockets.h" // for socketRawSetInterface

//-----------------------------------------------------------------------------------------------------
// XCP params

#define OPTION_PROJECT_NAME "udp_raw_demo"
#define OPTION_PROJECT_VERSION "V2.2.0"
#define OPTION_SERVER_PORT 5555
#define OPTION_QUEUE_SIZE (1024 * 32)
#define OPTION_LOG_LEVEL 4

#define OPTION_XCP_MODE (XCP_MODE_PERSISTENCE | XCP_MODE_LOCAL)
#define OPTION_A2L_MODE (A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)

// Defaults, overridden by --if and --ip
#define DEFAULT_IFNAME "eth0"
#define DEFAULT_IP {192, 168, 0, 220}

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

typedef struct params {
    uint32_t delay_us;    // Mainloop delay time in us
    uint16_t counter_max; // Maximum value for the counter
    float amplitude;      // Amplitude of the demo signal
} params_t;

const params_t params = {.delay_us = 1000, .counter_max = 1024, .amplitude = 100.0f};

tXcpCalSegIndex params_calseg = XCP_UNDEFINED_CALSEG;

//-----------------------------------------------------------------------------------------------------
// Demo global measurement values

uint32_t global_counter = 0;
double demo_signal = 0.0;

//-----------------------------------------------------------------------------------------------------
// Command line

static void usage(const char *argv0) {
    printf("\nUsage: %s [--if <interface>] [--ip <a.b.c.d>] [--port <port>]\n"
           "\n"
           "  --if    Ethernet interface for the raw transport (default: %s)\n"
           "  --ip    local IPv4 address of this target (default: 192.168.90.2)\n"
           "          There is no IP stack and no DHCP, so a concrete address is required.\n"
           "          It must NOT be an address owned by the kernel of this machine.\n"
           "  --port  XCP UDP port (default: %u)\n"
           "\n"
           "Needs CAP_NET_RAW:  sudo setcap cap_net_raw+ep %s\n"
           "See docs/SOCKET_RAW.md for the test network setup.\n\n",
           argv0, DEFAULT_IFNAME, (unsigned)OPTION_SERVER_PORT, argv0);
}

// Parse "a.b.c.d" into 4 bytes, returns false on a malformed address
static bool parseIp(const char *s, uint8_t *addr) {
    unsigned v[4];
    if (sscanf(s, "%u.%u.%u.%u", &v[0], &v[1], &v[2], &v[3]) != 4)
        return false;
    for (int i = 0; i < 4; i++) {
        if (v[i] > 255)
            return false;
        addr[i] = (uint8_t)v[i];
    }
    return true;
}

//-----------------------------------------------------------------------------------------------------
// Demo main

static volatile bool running = true;
static void sig_handler(int sig) {
    (void)sig;
    running = false;
}

int main(int argc, char *argv[]) {

    const char *ifname = DEFAULT_IFNAME;
    uint8_t addr[4] = DEFAULT_IP;
    uint16_t port = OPTION_SERVER_PORT;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--if") && i + 1 < argc) {
            ifname = argv[++i];
        } else if (!strcmp(argv[i], "--ip") && i + 1 < argc) {
            if (!parseIp(argv[++i], addr)) {
                printf("Invalid IPv4 address '%s'\n", argv[i]);
                return 1;
            }
        } else if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            port = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else {
            usage(argv[0]);
            return (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) ? 0 : 1;
        }
    }

    printf("\nXCP on raw Ethernet udp_raw_demo %uBit %s\n", (uint32_t)(sizeof(void *) * 8), OPTION_PROJECT_VERSION);
    printf("  Interface : %s\n", ifname);
    printf("  Address   : %u.%u.%u.%u:%u\n\n", addr[0], addr[1], addr[2], addr[3], port);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    XcpSetLogLevel(OPTION_LOG_LEVEL);

    if (!XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, OPTION_XCP_MODE)) {
        printf("Failed to initialize XCP\n");
        return 1;
    }
    XcpSetElfName(argv[0]);

    // XCP: Select the Ethernet interface for the raw transport, before starting the server
    socketRawSetInterface(ifname);

    // XCP: Initialize the XCP Server.
    // The address is mandatory here: the raw transport rejects 0.0.0.0 (ANY), there is no
    // IP stack which could resolve it. useTCP is false, the raw transport is UDP only.
    if (!XcpEthServerInit(addr, port, false, OPTION_QUEUE_SIZE)) {
        printf("Failed to start the XCP server.\n"
               "  Check that the interface exists, that this binary has CAP_NET_RAW\n"
               "  (sudo setcap cap_net_raw+ep %s) and that the address is not owned by the kernel.\n",
               argv[0]);
        return 1;
    }

    if (!A2lInit(addr, port, false, OPTION_A2L_MODE)) {
        return 1;
    }

    params_calseg = XcpCreateCalSeg("params", &params, sizeof(params));
    assert(params_calseg != XCP_UNDEFINED_CALSEG);

    A2lSetSegmentAddrMode(params_calseg, params);
    A2lCreateParameter(params.counter_max, "Maximum counter value", "", 0, 65535);
    A2lCreateParameter(params.delay_us, "Mainloop delay time in us", "us", 0, 500000);
    A2lCreateParameter(params.amplitude, "Amplitude of the demo signal", "", 0.0, 1000.0);

    uint16_t counter = 0;

    // XCP: Create a measurement event and register the measurement variables
    DaqCreateEvent(mainloop);
    A2lOnce() {
        A2lSetAbsoluteAddrMode(mainloop);
        A2lCreateMeasurement(global_counter, "Global free running counter");
        A2lCreatePhysMeasurement(demo_signal, "Demo signal", "", -1000.0, 1000.0);
        A2lSetStackAddrMode(mainloop);
        A2lCreateMeasurement(counter, "Mainloop counter");
    }

    printf("XCP server running. Connect with CANape or xcpclient to %u.%u.%u.%u:%u\n", addr[0], addr[1], addr[2], addr[3], port);
    printf("Try 'ping %u.%u.%u.%u' first - it proves the Ethernet HAL, ARP and the IPv4 header build.\n\n", addr[0], addr[1], addr[2], addr[3]);

    // Mainloop
    uint32_t delay_us = 1000;
    while (running) {

        const params_t *p = (params_t *)XcpLockCalSeg(params_calseg);
        delay_us = p->delay_us;

        counter++;
        if (counter > p->counter_max) {
            counter = 0;
        }
        global_counter++;
        demo_signal = (double)p->amplitude * (double)counter / 1000.0;

        XcpUnlockCalSeg(params_calseg);

        // XCP: Trigger the measurement event
        DaqTriggerEvent(mainloop);

        sleepUs(delay_us);
    }

    printf("\nShutting down...\n");
    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
    return 0;
}
