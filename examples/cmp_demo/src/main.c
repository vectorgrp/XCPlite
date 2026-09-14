// cmp_demo - XCP tunnelled through an emulated ASAM CMP capture module
//
// Demonstrates supplying your own backend for the xcplib raw Ethernet transport
// (OPTION_ENABLE_UDP_RAW) from OUTSIDE the library. xcplib is used as installed and
// unmodified: it builds plain Ethernet/IPv4/UDP frames and hands them to the six
// eth_hal_* functions, which this project implements in socket_raw_hal_cmp.c.
//
// CMP serves testing of XCP tools which communicate through capture modules. It is not an
// ECU developer feature, so nothing about it lives in libxcplite.
//
// The demo emulates a Capture Module with one interface, behind which sits one XCP ECU:
//
//   ECU  -> tool   frames xcplib builds leave as CMP Captured Data Messages (0x01)
//   tool -> ECU    CMP Transmit Data Messages (0x04, new in CMP 1.1) are unwrapped and
//                  their inner frame handed to xcplib, which answers the XCP command
//
// The tool therefore never talks IP to the ECU directly - everything is tunnelled inside
// CMP over UDP. See README.md.
//
// Build and run (xcplite must be installed with the raw configuration first, see README):
//   cmake -B build -S . -Dxcplite_DIR=<install>/lib/cmake/xcplite
//   cmake --build build
//   ./build/cmp_demo --sink 192.168.0.10:55555

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf, sscanf
#include <stdlib.h>  // for strtoul
#include <string.h>  // for strcmp

// Include XCPlite/libxcplite C headers
#include <a2l.h>    // for A2l generation
#include <xcplib.h> // for application programming interface

#include "cmp_backend.h"   // for the CMP backend configuration
#include "cmp_discovery.h" // for the multicast discovery responder
#include "cmp_rest.h"      // for the REST interface of the emulated capture module

//-----------------------------------------------------------------------------------------------------
// XCP params

#define OPTION_PROJECT_NAME "cmp_demo"
#define OPTION_PROJECT_VERSION "V1.0.0"
#define OPTION_SERVER_PORT 5555
#define OPTION_QUEUE_SIZE (1024 * 32)
#define OPTION_LOG_LEVEL 4

#define OPTION_XCP_MODE (XCP_MODE_PERSISTENCE | XCP_MODE_LOCAL)
#define OPTION_A2L_MODE (A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)

// Address of the emulated ECU. It only ever appears INSIDE the CMP payload, so unlike the
// plain raw transport it does not have to be free on any real network - but it must not
// collide with the address the Data Sink uses to reach us.
#define DEFAULT_ECU_IP {192, 168, 0, 220}

// UDP port we listen on for CMP Transmit Data and Control Messages. 55555 is the port the
// specification uses in its DNS-SD examples (12.2.2.2).
#define DEFAULT_CMP_PORT 55555

#define DEFAULT_REST_PORT 8080 // 12.3 says "should" be 80, which would need privileges
#define DEFAULT_OUTER_MTU 1500

#define DEFAULT_DEVICE_ID 1
#define DEFAULT_STREAM_ID 0
#define DEFAULT_INTERFACE_ID 1

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
    printf("\nUsage: %s [options]\n"
           "\n"
           "Data Sink (the XCP tool) and the CMP transport:\n"
           "  --sink <a.b.c.d:port>  Data Sink address for Captured Data Messages.\n"
           "                         Default: learned from the first CMP message received.\n"
           "  --listen <port>        UDP port to listen on for CMP messages (default: %u)\n"
           "  --mtu <bytes>          MTU of the path to the Data Sink (default: %u, max %u).\n"
           "                         CMP messages must not be IP fragmented, so this bounds\n"
           "                         the largest ECU frame that can be carried.\n"
           "  --rest-port <port>     REST interface port (default: %u, 0 disables it)\n"
           "  --no-discovery         Do not answer CMP_CM_DISCOVERY on %s:%u (12.1.1).\n"
           "                         Without it the module has to be configured statically,\n"
           "                         which section 12 permits.\n"
           "\n"
           "Capture module identity:\n"
           "  --device-id <n>        CMP DeviceId (default: %u)\n"
           "  --stream-id <n>        CMP StreamId (default: %u)\n"
           "  --interface-id <n>     CMP InterfaceId of the emulated ECU link (default: %u)\n"
           "  --ecu-mac <xx:..:xx>   MAC of the emulated ECU (default: derived from DeviceId)\n"
           "\n"
           "Emulated ECU, seen only inside the CMP payload:\n"
           "  --ip <a.b.c.d>         IPv4 address of the ECU (default: 192.168.0.220)\n"
           "  --port <port>          XCP UDP port of the ECU (default: %u)\n"
           "\n"
           "Needs no privileges: the CMP transport is an ordinary UDP socket.\n\n",
           argv0, (unsigned)DEFAULT_CMP_PORT, (unsigned)DEFAULT_OUTER_MTU, (unsigned)CMP_MAX_OUTER_MTU, (unsigned)DEFAULT_REST_PORT, CMP_DISCOVERY_GROUP,
           (unsigned)CMP_DISCOVERY_PORT, (unsigned)DEFAULT_DEVICE_ID, (unsigned)DEFAULT_STREAM_ID, (unsigned)DEFAULT_INTERFACE_ID, (unsigned)OPTION_SERVER_PORT);
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

// Parse "a.b.c.d:port" into a dotted quad string and a port
static bool parseEndpoint(const char *s, char *ip, size_t ip_size, uint16_t *port) {
    unsigned v[4], p;
    if (sscanf(s, "%u.%u.%u.%u:%u", &v[0], &v[1], &v[2], &v[3], &p) != 5)
        return false;
    for (int i = 0; i < 4; i++) {
        if (v[i] > 255)
            return false;
    }
    if (p == 0 || p > 65535)
        return false;
    snprintf(ip, ip_size, "%u.%u.%u.%u", v[0], v[1], v[2], v[3]);
    *port = (uint16_t)p;
    return true;
}

// Parse "xx:xx:xx:xx:xx:xx", returns false on a malformed or unusable address
static bool parseMac(const char *s, uint8_t *mac) {
    unsigned v[6];
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
        return false;
    for (int i = 0; i < 6; i++) {
        if (v[i] > 255)
            return false;
        mac[i] = (uint8_t)v[i];
    }
    // socket_raw.c rejects both of these when it reads the MAC back from the HAL
    if ((mac[0] & 0x01) != 0) {
        printf("ERROR: '%s' is a multicast MAC address\n", s);
        return false;
    }
    if ((mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0) {
        printf("ERROR: the MAC address must not be all zero\n");
        return false;
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

    // Line buffer stdout so the log stays readable and in order when it is redirected to a
    // file or a pipe, which is how the test script and any CI run it.
    setvbuf(stdout, NULL, _IOLBF, 0);

    uint8_t addr[4] = DEFAULT_ECU_IP;
    uint16_t port = OPTION_SERVER_PORT;
    uint16_t rest_port = DEFAULT_REST_PORT;
    bool discovery = true;

    char sink_ip[16] = {0};
    uint16_t sink_port = 0;

    tCmpBackendConfig cmp = {
        .device_id = DEFAULT_DEVICE_ID,
        .stream_id = DEFAULT_STREAM_ID,
        .interface_id = DEFAULT_INTERFACE_ID,
        .local_port = DEFAULT_CMP_PORT,
        .sink_ip = NULL,
        .sink_port = 0,
        .outer_mtu = DEFAULT_OUTER_MTU,
        .ecu_mac = {0, 0, 0, 0, 0, 0},
    };

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sink") && i + 1 < argc) {
            if (!parseEndpoint(argv[++i], sink_ip, sizeof(sink_ip), &sink_port)) {
                printf("Invalid Data Sink endpoint '%s', expected a.b.c.d:port\n", argv[i]);
                return 1;
            }
        } else if (!strcmp(argv[i], "--listen") && i + 1 < argc) {
            cmp.local_port = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--mtu") && i + 1 < argc) {
            unsigned long mtu = strtoul(argv[++i], NULL, 10);
            if (mtu < 576 || mtu > CMP_MAX_OUTER_MTU) {
                printf("Invalid MTU '%s', expected 576..%u\n", argv[i], (unsigned)CMP_MAX_OUTER_MTU);
                return 1;
            }
            cmp.outer_mtu = (uint16_t)mtu;
        } else if (!strcmp(argv[i], "--rest-port") && i + 1 < argc) {
            rest_port = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--no-discovery")) {
            discovery = false;
        } else if (!strcmp(argv[i], "--device-id") && i + 1 < argc) {
            cmp.device_id = (uint16_t)strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--stream-id") && i + 1 < argc) {
            cmp.stream_id = (uint8_t)strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--interface-id") && i + 1 < argc) {
            cmp.interface_id = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--ecu-mac") && i + 1 < argc) {
            if (!parseMac(argv[++i], cmp.ecu_mac)) {
                printf("Invalid MAC address '%s', expected xx:xx:xx:xx:xx:xx\n", argv[i]);
                return 1;
            }
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

    if (sink_ip[0] != 0) {
        cmp.sink_ip = sink_ip; // not copied by the backend, and sink_ip outlives it
        cmp.sink_port = sink_port;
    }

    printf("\nXCP over ASAM CMP - cmp_demo %uBit %s\n", (uint32_t)(sizeof(void *) * 8), OPTION_PROJECT_VERSION);
    printf("  Emulated ECU: %u.%u.%u.%u:%u (inside the CMP payload only)\n", addr[0], addr[1], addr[2], addr[3], port);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    XcpSetLogLevel(OPTION_LOG_LEVEL);

    if (!XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, OPTION_XCP_MODE)) {
        printf("Failed to initialize XCP\n");
        return 1;
    }
    XcpSetElfName(argv[0]);

    // Configure our own HAL backend before starting the server, which is what opens it.
    // socketRawSetInterface() is deliberately not called: its opaque string cannot carry
    // this much, so the backend takes a typed configuration instead.
    cmpBackendConfigure(&cmp);

    // XCP: Initialize the XCP Server.
    // The address is mandatory here: the raw transport rejects 0.0.0.0 (ANY), there is no
    // IP stack which could resolve it. useTCP is false, the raw transport is UDP only.
    if (!XcpEthServerInit(addr, port, false, OPTION_QUEUE_SIZE)) {
        printf("Failed to start the XCP server.\n"
               "  Check that UDP port %u is free for the CMP transport.\n",
               cmp.local_port);
        return 1;
    }

    // Discovery must be opened BEFORE the REST thread starts, because that thread is what
    // polls its socket. It exists to advertise the REST port, so it is pointless without one.
    static char serial_number[32]; // static: cmpDiscoveryStart keeps the pointer
    snprintf(serial_number, sizeof(serial_number), "cmp_demo-%04X", cmp.device_id);
    if (discovery && rest_port != 0) {
        tCmpDiscoveryConfig discovery_config = {
            .http_port = rest_port,
            .description = "XCPlite cmp_demo, emulated ASAM CMP capture module",
            .serial = serial_number,
        };
        if (!cmpDiscoveryStart(&discovery_config)) {
            printf("WARNING: discovery is not available, the capture module has to be configured\n"
                   "         statically. Section 12 permits exactly that.\n");
        }
    }

    // The REST interface is how a Data Sink discovers that this capture module supports
    // transmission (7.2.2), so start it once the backend can report its status.
    if (rest_port != 0 && !cmpRestStart(rest_port)) {
        printf("WARNING: the REST interface is not available. A Data Sink which relies on it\n"
               "         to detect transmission support will not send us anything.\n");
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

    A2lFinalize(); // @@@@ TEST: finalize now, before the first connect

    printf("\nCapture module running.\n");
    printf("  The XCP tool is a CMP Data Sink: it reaches the ECU by sending Transmit Data\n");
    printf("  Messages to our CMP port, not by addressing %u.%u.%u.%u directly.\n", addr[0], addr[1], addr[2], addr[3]);
    if (cmp.sink_ip == NULL) {
        printf("  No --sink given, so nothing is captured until the Data Sink speaks first.\n");
    }
    printf("\n");

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
    cmpRestStop();          // Stop the REST interface, joins the thread that polls discovery
    cmpDiscoveryStop();     // Safe only once that thread is gone
    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
    return 0;
}
