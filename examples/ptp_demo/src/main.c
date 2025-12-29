// ptp_demo xcplib example
// PTP observer or PTP master with XCP interface
// For analyzing PTP masters and testing PTP client stability
// Supports IEEE 1588-2008 PTPv2 over UDP/IPv4 in E2E mode

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <stdlib.h>  // for atoi, strtol
#include <string.h>  // for sprintf

#include "platform.h" // for sleepMs, ...

#include "ptp/ptp.h"

//-----------------------------------------------------------------------------------------------------
// XCP

#ifdef OPTION_ENABLE_XCP

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

#define OPTION_PROJECT_NAME "ptp_demo" // Project name, used to build the A2L and BIN file name
#define OPTION_PROJECT_VERSION "v1.0"
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 16     // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3              // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

#endif

//-----------------------------------------------------------------------------------------------------
// PTP params

#define PTP_LOG_LEVEL 3

#define PTP_DOMAIN 0
#define PTP_UUID {0x00, 0x1A, 0xB6, 0x00, 0x00, 0x00, 0x00, 0x01} // Example UUID, should be unique per device

// Default bind to INADDR_ANY and use interface name for SO_BINDTODEVICE (recommended for multicast)
#define PTP_ADDRESS {0, 0, 0, 0} // ANY

// Default network interface for PTP hardware timestamping if auto detection (NULL) and bind to ANY is not specific enough
// #define PTP_INTERFACE NULL
#define PTP_INTERFACE "eth0"
// #define PTP_INTERFACE "enp4s0" // VP6450 1G1
// #define PTP_INTERFACE "enp2s0f1" // VP6450 10G1
// #define PTP_INTERFACE "eno1" // VP6450 mgmt

// Default mode
#define PTP_MODE PTP_MODE_OBSERVER
// #define PTP_MODE PTP_MODE_MASTER

//-----------------------------------------------------------------------------------------------------
// Demo main

static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

static void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -i, --interface <name>  Network interface name (default: eth0)\n");
    printf("  -m, --mode <mode>       PTP mode: observer or master (default: observer)\n");
    printf("  -d, --domain <number>   PTP domain number 0-255 (default: 0)\n");
    printf("  -u, --uuid <hex>        PTP UUID as 16 hex digits (default: 001AB60000000001)\n");
    printf("  -h, --help              Show this help message\n");
    printf("\nExample:\n");
    printf("  %s -i en0 -m master -d 1 -u 001AB60000000002\n", prog_name);
}

int main(int argc, char *argv[]) {

    // Default values
    char *ptp_interface = PTP_INTERFACE;
    int ptp_mode = PTP_MODE;
    int ptp_domain = PTP_DOMAIN;
    uint8_t ptp_uuid[8] = PTP_UUID;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interface") == 0) {
            if (i + 1 < argc) {
                ptp_interface = argv[++i];
            } else {
                printf("Error: -i/--interface requires an argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) {
            if (i + 1 < argc) {
                i++;
                if (strcmp(argv[i], "observer") == 0) {
                    ptp_mode = PTP_MODE_OBSERVER;
                } else if (strcmp(argv[i], "master") == 0) {
                    ptp_mode = PTP_MODE_MASTER;
                } else {
                    printf("Error: Invalid mode '%s'. Use 'observer' or 'master'\n", argv[i]);
                    print_usage(argv[0]);
                    return 1;
                }
            } else {
                printf("Error: -m/--mode requires an argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--domain") == 0) {
            if (i + 1 < argc) {
                i++;
                int domain = atoi(argv[i]);
                if (domain >= 0 && domain <= 255) {
                    ptp_domain = domain;
                } else {
                    printf("Error: Invalid domain '%s'. Must be 0-255\n", argv[i]);
                    print_usage(argv[0]);
                    return 1;
                }
            } else {
                printf("Error: -d/--domain requires an argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--uuid") == 0) {
            if (i + 1 < argc) {
                i++;
                // Parse UUID from hex string (16 hex digits = 8 bytes)
                if (strlen(argv[i]) != 16) {
                    printf("Error: UUID must be exactly 16 hexadecimal digits\n");
                    print_usage(argv[0]);
                    return 1;
                }
                for (int j = 0; j < 8; j++) {
                    char hex[3] = {argv[i][j * 2], argv[i][j * 2 + 1], '\0'};
                    char *endptr;
                    long val = strtol(hex, &endptr, 16);
                    if (*endptr != '\0' || val < 0 || val > 255) {
                        printf("Error: Invalid UUID hex string '%s'\n", argv[i]);
                        print_usage(argv[0]);
                        return 1;
                    }
                    ptp_uuid[j] = (uint8_t)val;
                }
            } else {
                printf("Error: -u/--uuid requires an argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else {
            printf("Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    char *mode_str = (ptp_mode == PTP_MODE_MASTER) ? "master" : "observer";
    printf("\nPTP %s at %s\n", mode_str, ptp_interface);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

#ifdef OPTION_ENABLE_XCP
    // XCP: Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);
    char epk[32];
    snprintf(epk, sizeof(epk), "%s_%s_%s", OPTION_PROJECT_VERSION, mode_str, __TIME__); // Use
    XcpInit(OPTION_PROJECT_NAME, epk, true /* activate */);
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }
#endif

    // Start the PTP observer or master
    printf("Starting PTP ...\n");
    uint8_t ptp_bindAddr[4] = PTP_ADDRESS;
    if (!ptpInit("PTP", ptp_mode, ptp_domain, ptp_uuid, ptp_bindAddr, ptp_interface, PTP_LOG_LEVEL)) {
        printf("Failed to start PTP\n");
        return 1;
    }

#ifdef OPTION_ENABLE_XCP
    A2lFinalize(); // @@@@ TEST: Manually finalize the A2L file to make it visible without XCP tool connect
#endif

    // Mainloop
    printf("Start main task ...\n");
    while (running) {
        if (!ptpTask())
            running = false;
#ifdef OPTION_ENABLE_XCP
        if (!XcpEthServerStatus())
            running = false;
#endif
        sleepMs(10); // 10ms (100Hz determines the granularity of the PTP SYNC and ANNOUNCE messages)
    } // for (;;)

#ifdef OPTION_ENABLE_XCP
    XcpDisconnect();
    XcpEthServerShutdown();
#endif

    ptpShutdown();

    return 0;
}
