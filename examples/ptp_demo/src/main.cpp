// ptp_demo xcplib example (C++ version)
// PTP observer or PTP master with XCP interface
// For analyzing PTP masters and testing PTP client stability
// Supports IEEE 1588-2008 PTPv2 over UDP/IPv4 in E2E mode

#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "ptp/ptp.h"

//-----------------------------------------------------------------------------------------------------
// XCP

#ifdef OPTION_ENABLE_XCP

#include <a2l.hpp>    // for xcplib A2l generation application programming interface
#include <xcplib.hpp> // for xcplib application programming interface

constexpr const char OPTION_PROJECT_NAME[] = "ptp_demo";
constexpr const char OPTION_PROJECT_VERSION[] = "v1.0";
constexpr bool OPTION_USE_TCP = false;
constexpr uint8_t OPTION_SERVER_ADDR[4] = {0, 0, 0, 0};
constexpr uint16_t OPTION_SERVER_PORT = 5555;
constexpr size_t OPTION_QUEUE_SIZE = 1024 * 16;
constexpr int OPTION_LOG_LEVEL = 3;

#endif

//-----------------------------------------------------------------------------------------------------
// PTP params

constexpr int PTP_LOG_LEVEL = 3;
constexpr int PTP_DOMAIN = 0;
constexpr uint8_t PTP_UUID[8] = {0x00, 0x1A, 0xB6, 0x00, 0x00, 0x00, 0x00, 0x01};
constexpr uint8_t PTP_ADDRESS[4] = {0, 0, 0, 0};
constexpr const char PTP_INTERFACE[] = "eth0";
constexpr int PTP_MODE = PTP_MODE_OBSERVER;

//-----------------------------------------------------------------------------------------------------
// Demo main

static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

static void print_usage(const char *prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -i, --interface <name>  Network interface name (default: eth0)\n"
              << "  -m, --mode <mode>       PTP mode: observer or master (default: observer)\n"
              << "  -d, --domain <number>   PTP domain number 0-255 (default: 0)\n"
              << "  -u, --uuid <hex>        PTP UUID as 16 hex digits (default: 001AB60000000001)\n"
              << "  -h, --help              Show this help message\n\n"
              << "Example:\n  " << prog_name << " -i en0 -m master -d 1 -u 001AB60000000002\n";
}

int main(int argc, char *argv[]) {

    // Default values
    std::string ptp_interface = PTP_INTERFACE;
    int ptp_mode = PTP_MODE;
    int ptp_domain = PTP_DOMAIN;
    uint8_t ptp_uuid[8];
    std::memcpy(ptp_uuid, PTP_UUID, sizeof(ptp_uuid));

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "-i") == 0 || std::strcmp(argv[i], "--interface") == 0) {
            if (i + 1 < argc) {
                ptp_interface = argv[++i];
            } else {
                std::cerr << "Error: -i/--interface requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "-m") == 0 || std::strcmp(argv[i], "--mode") == 0) {
            if (i + 1 < argc) {
                i++;
                if (std::strcmp(argv[i], "observer") == 0) {
                    ptp_mode = PTP_MODE_OBSERVER;
                } else if (std::strcmp(argv[i], "master") == 0) {
                    ptp_mode = PTP_MODE_MASTER;
                } else {
                    std::cerr << "Error: Invalid mode '" << argv[i] << "'. Use 'observer' or 'master'\n";
                    print_usage(argv[0]);
                    return 1;
                }
            } else {
                std::cerr << "Error: -m/--mode requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "-d") == 0 || std::strcmp(argv[i], "--domain") == 0) {
            if (i + 1 < argc) {
                i++;
                int domain = std::atoi(argv[i]);
                if (domain >= 0 && domain <= 255) {
                    ptp_domain = domain;
                } else {
                    std::cerr << "Error: Invalid domain '" << argv[i] << "'. Must be 0-255\n";
                    print_usage(argv[0]);
                    return 1;
                }
            } else {
                std::cerr << "Error: -d/--domain requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "-u") == 0 || std::strcmp(argv[i], "--uuid") == 0) {
            if (i + 1 < argc) {
                i++;
                if (std::strlen(argv[i]) != 16) {
                    std::cerr << "Error: UUID must be exactly 16 hexadecimal digits\n";
                    print_usage(argv[0]);
                    return 1;
                }
                for (int j = 0; j < 8; j++) {
                    char hex[3] = {argv[i][j * 2], argv[i][j * 2 + 1], '\0'};
                    char *endptr;
                    long val = std::strtol(hex, &endptr, 16);
                    if (*endptr != '\0' || val < 0 || val > 255) {
                        std::cerr << "Error: Invalid UUID hex string '" << argv[i] << "'\n";
                        print_usage(argv[0]);
                        return 1;
                    }
                    ptp_uuid[j] = static_cast<uint8_t>(val);
                }
            } else {
                std::cerr << "Error: -u/--uuid requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
        } else {
            std::cerr << "Error: Unknown option '" << argv[i] << "'\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    const char *mode_str = (ptp_mode == PTP_MODE_MASTER) ? "master" : "observer";
    std::cout << "\nPTP " << mode_str << " at " << ptp_interface << std::endl;

    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

#ifdef OPTION_ENABLE_XCP
    XcpSetLogLevel(OPTION_LOG_LEVEL);
    char epk[32];
    std::snprintf(epk, sizeof(epk), "%s_%s_%s", OPTION_PROJECT_VERSION, mode_str, __TIME__);
    XcpInit(OPTION_PROJECT_NAME, epk, true);
    if (!XcpEthServerInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }
    if (!A2lInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }
#endif

    std::cout << "Starting PTP ..." << std::endl;
    uint8_t ptp_bindAddr[4];
    std::memcpy(ptp_bindAddr, PTP_ADDRESS, sizeof(ptp_bindAddr));
    if (!ptpInit(ptp_mode, ptp_domain, ptp_uuid, ptp_bindAddr, const_cast<char *>(ptp_interface.c_str()), PTP_LOG_LEVEL)) {
        std::cerr << "Failed to start PTP" << std::endl;
        return 1;
    }

#ifdef OPTION_ENABLE_XCP
    A2lFinalize();
#endif

    std::cout << "Start main task ..." << std::endl;
    while (running) {
        if (!ptpTask())
            running = false;
#ifdef OPTION_ENABLE_XCP
        if (!XcpEthServerStatus())
            running = false;
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

#ifdef OPTION_ENABLE_XCP
    XcpDisconnect();
    XcpEthServerShutdown();
#endif

    ptpShutdown();
    return 0;
}
