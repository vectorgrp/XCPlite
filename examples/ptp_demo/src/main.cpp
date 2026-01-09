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
#include "ptp/ptp_master.h"
#include "ptp/ptp_observer.h"

//-----------------------------------------------------------------------------------------------------
// XCP

#ifdef OPTION_ENABLE_XCP

#include <a2l.hpp>    // for xcplib A2l generation application programming interface
#include <xcplib.hpp> // for xcplib application programming interface

constexpr const char OPTION_PROJECT_NAME[] = "ptp_demo";
constexpr const char OPTION_PROJECT_VERSION[] = "V1.4.2";
constexpr bool OPTION_USE_TCP = false;
constexpr uint8_t OPTION_SERVER_ADDR[4] = {0, 0, 0, 0};
constexpr uint16_t OPTION_SERVER_PORT = 5555;
constexpr size_t OPTION_QUEUE_SIZE = 1024 * 16;
constexpr int OPTION_LOG_LEVEL = 2; // 0=none, 1=error, 2=warning, 3=info

#endif

//-----------------------------------------------------------------------------------------------------
// PTP params

#define PTP_MODE_OBSERVER 0x01
#define PTP_MODE_MASTER 0x02
#define PTP_MODE_AUTO_OBSERVER 0x03

constexpr uint8_t PTP_BIND_ADDRESS[4] = {0, 0, 0, 0}; // Default bind to any addresses
constexpr const char PTP_INTERFACE[] = "eth0";        // Default network interface
constexpr int PTP_DOMAIN = 0;                         // Default domain: 0
constexpr int PTP_MODE = PTP_MODE_AUTO_OBSERVER;      // Default observer mode: automatic observer in passive mode
constexpr int PTP_LOG_LEVEL = 1;                      // Default log level

//-----------------------------------------------------------------------------------------------------
// Demo main

static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

static void print_usage(const char *prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -i, --interface <name>  Network interface name (default: eth0)\n"
              << "  -m, --master            Creates a PTP master with uuid and domain\n"
              << "  -o, --observer          Observer for uuid and domain (default: multi observer)\n"
              << "  -p, --passive           Passive observer mode (default: active)\n"
              << "  -d, --domain <number>   Domain number 0-255 (default: 0)\n"
              << "  -u, --uuid <hex>        UUID as 16 hex digits (default: 001AB60000000001)\n"
              << "  -l, --loglevel <level>  Set log level (0..5)\n"
              << "  -h, --help              Show this help message\n\n"
              << "Example:\n  " << prog_name << " -i en0 -m master -d 1 -u 001AB60000000002\n";
}

int main(int argc, char *argv[]) {

    // Default values
    std::string ptp_interface = PTP_INTERFACE;
    int ptp_mode = PTP_MODE;
    int ptp_domain = PTP_DOMAIN;
    int ptp_log_level = PTP_LOG_LEVEL;
    int ptp_active_mode = true;
    uint8_t ptp_uuid[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // default create from MAC in ptpCreateMaster

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
        }

        else if (std::strcmp(argv[i], "-m") == 0 || std::strcmp(argv[i], "--master") == 0) {
            ptp_mode = PTP_MODE_MASTER;
        } else if (std::strcmp(argv[i], "-o") == 0 || std::strcmp(argv[i], "--observer") == 0) {
            ptp_mode = PTP_MODE_OBSERVER;
        }

        else if (std::strcmp(argv[i], "-p") == 0 || std::strcmp(argv[i], "--passive") == 0) {
            ptp_active_mode = false;
        }

        else if (std::strcmp(argv[i], "-d") == 0 || std::strcmp(argv[i], "--domain") == 0) {
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
        } else if (std::strcmp(argv[i], "-l") == 0 || std::strcmp(argv[i], "--loglevel") == 0) {
            if (i + 1 < argc) {
                i++;
                int log_level = std::atoi(argv[i]);
                if (log_level >= 0 && log_level <= 5) {
                    ptp_log_level = log_level;
                } else {
                    std::cerr << "Error: Invalid log level '" << argv[i] << "'. Must be 0-5\n";
                    print_usage(argv[0]);
                    return 1;
                }
            } else {
                std::cerr << "Error: -l/--loglevel requires an argument\n";
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

    std::cout << "Starting PTP on " << ptp_interface << "..." << std::endl;

    tPtp *ptp;
    if (NULL == (ptp = ptpCreateInterface(PTP_BIND_ADDRESS, const_cast<char *>(ptp_interface.c_str()), ptp_log_level))) {
        std::cerr << "Failed to start PTP interface" << std::endl;
        return 1;
    }

    // Automatic observer mode: Create observers for all masters seen on any address, uuid and domain
    if (ptp_mode == PTP_MODE_AUTO_OBSERVER) {
#ifdef PTP_OBSERVER_LIST
        // Preload the observer list from file, to keep the index of known master stable, which leads to a stable A2L file and CANape configurations
        std::cout << "Enable auto observer mode" << std::endl;
        if (!ptpLoadObserverList(ptp, "ptp_demo_observers.lst", ptp_active_mode)) {
            std::cout << "No observer list loaded" << std::endl;
        }
#endif
        ptpEnableAutoObserver(ptp, ptp_active_mode);

    }

    // Specific observer mode: Create one observer for given uuid, domain and any address
    else if (ptp_mode == PTP_MODE_OBSERVER) {

        // Create an observer on interface ptp
        // The PTP observer will listen to a master with ptp_domain, ptp_uuid and any address
        // If multiple masters are present, the first one matching will be selected
        uint8_t ptp_address[4] = {0, 0, 0, 0}; // Listen on all addresses
        tPtpObserverHandle ptpObs = ptpCreateObserver(ptp, "Observer1", ptp_active_mode, ptp_domain, ptp_uuid, ptp_address);
        if (NULL == ptpObs) {
            std::cerr << "Failed to create PTP observer" << std::endl;
            ptpShutdown(ptp);
            return 1;
        }

    }
    // Master mode: Create a master for given uuid and domain
    else if (ptp_mode == PTP_MODE_MASTER) {

        // Create a master on interface ptp
        tPtpMasterHandle ptpMaster = ptpCreateMaster(ptp, "Master1", ptp_domain, ptp_uuid);
        if (NULL == ptpMaster) {
            std::cerr << "Failed to create PTP master" << std::endl;
            ptpShutdown(ptp);
            return 1;
        }
    }

    std::cout << "Start main task ..." << std::endl;
    std::chrono::steady_clock::time_point last_status_print = std::chrono::steady_clock::now();
    while (running) {
        if (!ptpTask(ptp))
            running = false;

#ifdef OPTION_ENABLE_XCP
        if (!XcpEthServerStatus())
            running = false;
#endif

        // Status print
        if (ptp_log_level == 1 || ptp_log_level == 2) {
            if (std::chrono::steady_clock::now() - last_status_print >= std::chrono::seconds(1)) {
                ptpPrintState(ptp);
                last_status_print = std::chrono::steady_clock::now();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

#ifdef OPTION_ENABLE_XCP
    XcpDisconnect();
    XcpEthServerShutdown();
    A2lFinalize(); // Finalize A2L generation, if not done yet
#endif

#ifdef OPTION_ENABLE_XCP
    // Save observer list to file
    if (ptp_mode == PTP_MODE_AUTO_OBSERVER) {
        if (!ptpSaveObserverList(ptp, "ptp_demo_observers.lst")) {
            std::cout << "Failed to save observer list" << std::endl;
        }
    }
#endif

    ptpShutdown(ptp);
    return 0;
}
