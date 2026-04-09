// ptptool
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

#include "platform.h" // for clockGetMonotonicNs, clockGetRealtimeNs, clockGet

#include "ptp/ptp.h"

// PTP client clock implementation for debugging XCP PTP support
#ifdef OPTION_ENABLE_PTP_CLIENT
#include "ptp/ptp_client.h"
#endif

// PTP master to test client stability
#ifdef OPTION_ENABLE_PTP_MASTER
#include "ptp/ptp_master.h"
#endif

// PTP observer to monitor multiple PTP masters
#ifdef OPTION_ENABLE_PTP_OBSERVER
#include "ptp/ptp_observer.h"
#endif

//-----------------------------------------------------------------------------------------------------
// XCP

#ifdef OPTION_ENABLE_XCP

// Include XCPlite/libxcplite C++ headers
#include <a2l.hpp>    // for A2l generation application programming interface
#include <xcplib.hpp> // for application programming interface

constexpr const char XCP_OPTION_PROJECT_NAME[] = "ptptool";
constexpr const char XCP_OPTION_PROJECT_VERSION[] = "V0.0.2";
constexpr bool XCP_OPTION_USE_TCP = false;
constexpr uint8_t XCP_OPTION_SERVER_ADDR[4] = {0, 0, 0, 0};
constexpr uint16_t XCP_OPTION_SERVER_PORT = 5555;
constexpr size_t XCP_OPTION_QUEUE_SIZE = (1024 * 16);
#endif
uint8_t XCP_GRANDMASTER_UUID[] = {0x68, 0xB9, 0x83, 0xFF, 0xFE, 0x00, 0x8E, 0x9F}; // Grandmaster UUID
uint8_t XCP_CLIENT_UUID[] = {0x68, 0xB9, 0x83, 0xFF, 0xFE, 0x00, 0x8E, 0x9F};      // Local clock UUID
constexpr int XCP_OPTION_LOG_LEVEL = 2; // Default XCP log level: 0=none, 1=error, 2=warning, 3=info, 4=XCP protocol debug, 5=very verbose

//-----------------------------------------------------------------------------------------------------
// PTP params

#define PTP_MODE_CLIENT_ONLY 0x00
#define PTP_MODE_OBSERVER 0x01
#define PTP_MODE_MASTER 0x02
#define PTP_MODE_AUTO_OBSERVER 0x03

constexpr uint8_t PTP_BIND_ADDRESS[4] = {0, 0, 0, 0}; // Default bind to any addresses
constexpr const char PTP_INTERFACE[] = "eth0";        // Default network interface
constexpr int PTP_DOMAIN = 0;                         // Default domain: 0
constexpr int PTP_MODE = PTP_MODE_CLIENT_ONLY;        // Default observer mode: client only
constexpr int PTP_LOG_LEVEL = 3;                      // Default log level

//-----------------------------------------------------------------------------------------------------
// Logging

int ptp_log_level = PTP_LOG_LEVEL;

//-----------------------------------------------------------------------------------------------------
// PTP client clock callbacks for XCP

// Get current clock value in nanoseconds
// Return value is assumed to be from a PTP synchronized clock with ns resolution and PTP or ARB epoch */
uint64_t ptpClientGetClock(void) {

    // Use the PTP demo client clock
#ifdef OPTION_ENABLE_PTP_CLIENT_CLOCK
    if (gPtpClient != NULL) {
        // Interpolate grandmaster clock from last known offset and drift
        return ptpClientGetGrandmasterClock();
    } else {
        // No PTP client available, return system monotonic clock
        return clockGetMonotonicNs();
    }
#else
    // Return the system realtime clock, which must be synchronized to PTP grandmaster by other means
    // For example by running a PTP4L and PHC2SYS on Linux
    return clockGetRealtimeNs();
#endif
}

// Get current clock state
// @return CLOCK_STATE_SYNCH, CLOCK_STATE_SYNCH_IN_PROGRESS, CLOCK_STATE_FREE_RUNNING
uint8_t ptpClientGetClockState(void) {

    /* Possible return values:
        CLOCK_STATE_SYNCH, CLOCK_STATE_SYNCH_IN_PROGRESS, CLOCK_STATE_FREE_RUNNING
    */

#ifdef OPTION_ENABLE_PTP_CLIENT
    if (gPtpClient != NULL) {
        if (gPtpClient->gmValid) {
            // Check if master is sufficiently synchronized
            if (gPtpClient->is_sync) {
                return CLOCK_STATE_SYNCH; // Clock is synchronized to grandmaster
            } else {
                return CLOCK_STATE_SYNCH_IN_PROGRESS; // Clock is synchronizing to grandmaster
            }
        }
    }
    return CLOCK_STATE_FREE_RUNNING; // No grandmaster found, clock is free running
#else
    return CLOCK_STATE_SYNCH; // Clock is assumed to be synchronized, add other means to check synchronization if needed
#endif
}

// Get client and grandmaster clock uuid, stratum level and epoch
// @param client_uuid Pointer to 8 byte array to store the client UUID
// @param grandmaster_uuid Pointer to 8 byte array to store the grandmaster UUID
// @param epoch Pointer to store the epoch
// @param stratum Pointer to store the stratum level
// @return true if PTP is available and grandmaster found, must not be sync yet
bool ptpClientGetClockInfo(uint8_t *client_uuid, uint8_t *grandmaster_uuid, uint8_t *epoch, uint8_t *stratum) {

    /*
      Possible return values:
        stratum: XCP_STRATUM_LEVEL_UNKNOWN, XCP_STRATUM_LEVEL_RTC,XCP_STRATUM_LEVEL_GPS
        epoch: XCP_EPOCH_TAI, XCP_EPOCH_UTC, XCP_EPOCH_ARB
    */

#ifdef OPTION_ENABLE_PTP_CLIENT
    if (gPtpClient != NULL) {
        if (client_uuid != NULL)
            memcpy(client_uuid, gPtpClient->client_uuid, 8);
        if (grandmaster_uuid != NULL)
            memset(grandmaster_uuid, 0, 8);
        if (epoch != NULL)
            *epoch = CLOCK_EPOCH_TAI; // @@@@ TODO: Determine actual epoch from grandmaster info
        if (stratum != NULL)
            *stratum = CLOCK_STRATUM_LEVEL_UNKNOWN; // @@@@ TODO: Determine actual stratum from grandmaster info
        if (gPtpClient->gmValid) {
            if (grandmaster_uuid != NULL)
                memcpy(grandmaster_uuid, gPtpClient->gm.uuid, 8);
            return true;
        }
    }
#else
    if (client_uuid != NULL)
        memcpy(client_uuid, XCP_CLIENT_UUID, 8);
    if (grandmaster_uuid != NULL)
        memcpy(grandmaster_uuid, XCP_GRANDMASTER_UUID, 8);
    if (epoch != NULL)
        *epoch = CLOCK_EPOCH_TAI;
    if (stratum != NULL)
        *stratum = CLOCK_STRATUM_LEVEL_UNKNOWN;
    return true;
#endif
}

// Register PTP client clock callbacks for XCP
#ifdef OPTION_ENABLE_XCP
void ptpClientRegisterClockCallbacks(void) {
    ApplXcpRegisterGetClockCallback(ptpClientGetClock);
    ApplXcpRegisterGetClockStateCallback(ptpClientGetClockState);
    ApplXcpRegisterGetClockInfoGrandmasterCallback(ptpClientGetClockInfo);
}
#endif

//-----------------------------------------------------------------------------------------------------
// Demo main

static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

static void print_usage(const char *prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -i, --interface <name>        Network interface name (default: eth0)\n"
#ifdef OPTION_ENABLE_PTP_CLIENT
              << "  -c, --client                  Enable PTP client mode\n"
#endif
#ifdef OPTION_ENABLE_PTP_MASTER
              << "  -m, --master                  Creates a PTP master with uuid and domain\n"
              << "      --announce_interval <ms>  Announce interval in ms (default: 1000)\n"
              << "      --sync_interval <ms>      SYNC interval in ms (default: 1000)\n"
              << "      --offset <ns>             PTP master time offset in ns (default: 0)\n"
              << "      --drift <ns/s>            PTP master time drift in ns/s (default: 0)\n"
              << "      --drift_drift <ns/s2>     PTP master time drift drift in ns/s2 (default: 0)\n"
              << "      --jitter <ns>             PTP master time jitter in ns (default: 0)\n"
#endif
#ifdef OPTION_ENABLE_PTP_OBSERVER
              << "  -o, --observer                Observer for uuid and domain\n"
              << "  -a, --auto                    Multi observer mode\n"
              << "  -p, --passive                 Passive observer mode (default: active)\n"
#endif
              << "  -d, --domain <number>        Domain number 0-255 (default: 0)\n"
              << "  -u, --uuid <hex>             UUID as 16 hex digits (default: 001AB60000000001)\n"
              << "  -l, --ptp_log_level <level>  Set PTP log level (3..7, default: 3)\n"
              << "  -x, --xcp_log_level <level>  Set XCP log level (0..5, default: 2 - errors+warnings)\n"
              << "  -h, --help                   Show this help message\n\n"
              << "Example:\n  " << prog_name << " -i en0 -m master -d 1 -u 001AB60000000002\n";
}

int main(int argc, char *argv[]) {

#ifndef OPTION_SOCKET_HW_TIMESTAMPS
    printf("Please enable OPTION_SOCKET_HW_TIMESTAMPS in src/xcplib_cfg.h for PTP tool\n");
    return 1;
#endif

    // Default values
    uint8_t xcp_log_level = XCP_OPTION_LOG_LEVEL;
    std::string ptp_interface = PTP_INTERFACE;
    int ptp_mode = PTP_MODE;
    int ptp_domain = PTP_DOMAIN;
    int ptp_client_mode = false;
    uint8_t ptp_uuid[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // default create from MAC in ptpCreateMaster
#ifdef OPTION_ENABLE_PTP_OBSERVER
    int ptp_passive_mode = false;
#endif
#ifdef OPTION_ENABLE_PTP_MASTER
    uint32_t ptp_announce_interval_ms = 0; // use default
    uint32_t ptp_sync_interval_ms = 0;     // use default
    int32_t ptp_master_offset = 0;
    int32_t ptp_master_drift = 0;
    int32_t ptp_master_drift_drift = 0;
    int32_t ptp_master_jitter = 0;
#endif

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "-i") == 0 || std::strcmp(argv[i], "--interface") == 0) {
            if (i + 1 < argc) {
                ptp_interface = argv[++i];
                std::cout << "PTP interface set to " << ptp_interface << "\n";
            } else {
                std::cerr << "Error: -i/--interface requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "-c") == 0 || std::strcmp(argv[i], "--client") == 0) {
            ptp_client_mode = true;
            std::cout << "Client mode selected\n";
        }
#ifdef OPTION_ENABLE_PTP_MASTER
        else if (std::strcmp(argv[i], "-m") == 0 || std::strcmp(argv[i], "--master") == 0) {
            ptp_mode = PTP_MODE_MASTER;
            std::cout << "PTP master mode selected\n";
        } else if (std::strcmp(argv[i], "--announce_interval") == 0) {
            if (i + 1 < argc) {
                ptp_announce_interval_ms = std::atoi(argv[++i]);
                std::cout << "PTP master announce interval set to " << ptp_announce_interval_ms << " ms\n";
            } else {
                std::cerr << "Error: --announce_interval requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--sync_interval") == 0) {
            if (i + 1 < argc) {
                ptp_sync_interval_ms = std::atoi(argv[++i]);
                std::cout << "PTP master sync interval set to " << ptp_sync_interval_ms << " ms\n";
            } else {
                std::cerr << "Error: --sync_interval requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--offset") == 0) {
            if (i + 1 < argc) {
                ptp_master_offset = std::atoi(argv[++i]);
                std::cout << "PTP master offset set to " << ptp_master_offset << " ns\n";
            } else {
                std::cerr << "Error: --offset requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--drift") == 0) {
            if (i + 1 < argc) {
                ptp_master_drift = std::atoi(argv[++i]);
                std::cout << "PTP master drift set to " << ptp_master_drift << " ns/s\n";
            } else {
                std::cerr << "Error: --drift requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--drift_drift") == 0) {
            if (i + 1 < argc) {
                ptp_master_drift_drift = std::atoi(argv[++i]);
                std::cout << "PTP master drift drift set to " << ptp_master_drift_drift << " ns/s2\n";
            } else {
                std::cerr << "Error: --drift_drift requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--jitter") == 0) {
            if (i + 1 < argc) {
                ptp_master_jitter = std::atoi(argv[++i]);
                std::cout << "PTP master jitter set to " << ptp_master_jitter << " ns\n";
            } else {
                std::cerr << "Error: --jitter requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
        }
#endif

#ifdef OPTION_ENABLE_PTP_OBSERVER
        else if (std::strcmp(argv[i], "-o") == 0 || std::strcmp(argv[i], "--observer") == 0) {
            ptp_mode = PTP_MODE_OBSERVER;
            std::cout << "PTP observer mode selected\n";
        } else if (std::strcmp(argv[i], "-a") == 0 || std::strcmp(argv[i], "--auto") == 0) {
            ptp_mode = PTP_MODE_AUTO_OBSERVER;
            std::cout << "PTP auto observer mode selected\n";
        } else if (std::strcmp(argv[i], "-p") == 0 || std::strcmp(argv[i], "--passive") == 0) {
            ptp_passive_mode = true;
            std::cout << "Passive observer mode selected\n";
        }

#endif

        else if (std::strcmp(argv[i], "-d") == 0 || std::strcmp(argv[i], "--domain") == 0) {
            if (i + 1 < argc) {
                i++;
                int domain = std::atoi(argv[i]);
                if (domain >= 0 && domain <= 255) {
                    ptp_domain = domain;
                    std::cout << "PTP domain set to " << ptp_domain << "\n";
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
        } else if (std::strcmp(argv[i], "-l") == 0 || std::strcmp(argv[i], "--ptp_log_level") == 0) {
            if (i + 1 < argc) {
                i++;
                int log_level = std::atoi(argv[i]);
                if (log_level >= 2 && log_level <= 7) {
                    ptp_log_level = log_level;
                } else {
                    std::cerr << "Error: Invalid log level '" << argv[i] << "'. Must be 2-7\n";
                    print_usage(argv[0]);
                    return 1;
                }
            } else {
                std::cerr << "Error: -l/--ptp_log_level requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
        }

        else if (std::strcmp(argv[i], "-x") == 0 || std::strcmp(argv[i], "--xcp_log_level") == 0) {
            if (i + 1 < argc) {
                i++;
                int log_level = std::atoi(argv[i]);
                if (log_level >= 0 && log_level <= 6) {
                    xcp_log_level = log_level;
                } else {
                    if (log_level < 2) {
                        std::cerr << "Warning: XCP log level less than 2 may disable important warnings.\n";
                    }
                    if (log_level < 1) {
                        std::cerr << "Warning: XCP log level 0 disables all logging, including errors.\n";
                    }
                    std::cerr << "Error: Invalid log level '" << argv[i] << "'. Must be 0-6\n";
                    print_usage(argv[0]);
                    return 1;
                }
            } else {
                std::cerr << "Error: -x/--xcp_log_level requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
        }

        else if (std::strcmp(argv[i], "-u") == 0 || std::strcmp(argv[i], "--uuid") == 0) {
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

    const char *mode_str = (ptp_mode == PTP_MODE_MASTER) ? "master" : (ptp_mode == PTP_MODE_OBSERVER) ? "observer" : "client";
    std::cout << "\nPTP " << mode_str << " at " << ptp_interface << std::endl;

    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    // Create a PTP interface
    std::cout << "Starting PTP on " << ptp_interface << "..." << std::endl;
    tPtp *ptp;
    if (NULL == (ptp = ptpCreateInterface(PTP_BIND_ADDRESS, const_cast<char *>(ptp_interface.c_str()), false))) {
        std::cerr << "Failed to start PTP interface" << std::endl;
        return 1;
    }

    // Initialize XCP server
#ifdef OPTION_ENABLE_XCP
    XcpSetLogLevel(xcp_log_level);

    // Initialize XCP
    char epk[32];
    std::snprintf(epk, sizeof(epk), "%s_%s_%s", XCP_OPTION_PROJECT_VERSION, mode_str, __TIME__);
    XcpInit(XCP_OPTION_PROJECT_NAME, epk, XCP_MODE_LOCAL);

// Enable PTP synchronized clock for XCP
#ifdef XCP_OPTION_PTP
#ifdef OPTION_ENABLE_PTP_CLIENT
    if (ptp_client_mode && ptp_mode != PTP_MODE_MASTER && ptp_mode != PTP_MODE_OBSERVER && ptp_mode != PTP_MODE_AUTO_OBSERVER) {
        // Create a PTP client
        // To obtain grandmaster and client clock UUID
        // To test PTP clock synchronization stability
        tPtpClient *obs = ptpCreateClient(ptp);
        if (NULL == obs) {
            std::cerr << "Failed to create PTP client" << std::endl;
            ptpShutdown(ptp);
            return 1;
        }
        // Wait until PTP clock is locked onto a grandmaster and grandmaster UUID is known
        // Must not be synchronized before starting XCP DAQ measurement
        std::cout << "Waiting for PTP clock ";
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            uint8_t clock_state = ptpClientTask(ptp);
            if (clock_state == CLOCK_STATE_SYNCH_IN_PROGRESS) {
                std::cout << std::endl << "PTP clock synchronization in progress..." << std::endl;
                break;

            } else if (clock_state == CLOCK_STATE_SYNCH) {
                std::cout << std::endl << "PTP clock synchronized to grandmaster." << std::endl;
                break;
            }
            std::cout << ".";
            std::cout.flush();
        }
        if (!running) { // Ctrl-C while waiting
            std::cout << std::endl;
            ptpShutdown(ptp);
            return 0;
        }
    }
#endif
    // Enable PTP in XCP
    // Register XCP clock callbacks, provides PTP synchronized timestamps to XCP and information about clock state and identity
    ptpClientRegisterClockCallbacks();
#endif

    // Create XCP on Ethernet server
    if (!XcpEthServerInit(XCP_OPTION_SERVER_ADDR, XCP_OPTION_SERVER_PORT, XCP_OPTION_USE_TCP, XCP_OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Initialize A2L generation, no parameter persistence
    if (!A2lInit(XCP_OPTION_SERVER_ADDR, XCP_OPTION_SERVER_PORT, XCP_OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }
#endif

// Specific observer mode:
// Create one observer for given uuid, domain and any address
#ifdef OPTION_ENABLE_PTP_OBSERVER

    // Automatic observer mode:
    // Create observers for all masters seen on any address, uuid and domain
    if (ptp_mode == PTP_MODE_AUTO_OBSERVER) {
#ifdef PTP_OBSERVER_LIST
        // Preload the observer list from file, to keep the index of known master stable, which leads to a stable A2L file and CANape configurations
        std::cout << "Enable auto observer mode" << std::endl;
        if (!ptpLoadObserverList(ptp, "ptptool.obs", !ptp_passive_mode)) {
            std::cout << "No observer list loaded" << std::endl;
        }
#endif
        ptpEnableAutoObserver(ptp, !ptp_passive_mode);

    } else if (ptp_mode == PTP_MODE_OBSERVER) {

        // Create an observer on interface ptp
        // The PTP observer will listen to a master with ptp_domain, ptp_uuid and any address
        // If multiple masters are present, the first one matching will be selected
        uint8_t ptp_address[4] = {0, 0, 0, 0}; // Listen on all addresses
        tPtpObserver *obs = ptpCreateObserver(ptp, "Observer1", !ptp_passive_mode, ptp_domain, ptp_uuid, ptp_address);
        if (NULL == obs) {
            std::cerr << "Failed to create PTP observer" << std::endl;
            ptpShutdown(ptp);
            return 1;
        }
    }
#endif

// Master mode:
// Create a master for given uuid and domain
#ifdef OPTION_ENABLE_PTP_MASTER
    if (ptp_mode == PTP_MODE_MASTER) {

        // Create a master on interface ptp
        tPtpMaster *ptpMaster = ptpCreateMaster(ptp, "Master1", ptp_domain, ptp_uuid, ptp_announce_interval_ms, ptp_sync_interval_ms, ptp_master_offset, ptp_master_drift,
                                                ptp_master_drift_drift, ptp_master_jitter);
        if (NULL == ptpMaster) {
            std::cerr << "Failed to create PTP master" << std::endl;
            ptpShutdown(ptp);
            return 1;
        }
    }
#endif

    std::cout << "Start main task ..." << std::endl;
    std::chrono::steady_clock::time_point clock = std::chrono::steady_clock::now();
    double sys_time = 0.0;
    uint64_t sys_time_0 = 0;
    double ptp_time = 0.0;
    uint64_t ptp_time_0 = 0;
    uint8_t counter{0};
    int delay_ms = 10;
    while (running) {

        counter++;

        // PTP clock and system clock relative to start
        if (ptpTask(ptp) == CLOCK_STATE_SYNCH) {
            if (sys_time_0 == 0) {
                sys_time_0 = clockGet();
                ptp_time_0 = ptpClientGetClock();
            } else {
                ptp_time = (double)(ptpClientGetClock() - ptp_time_0);
                sys_time = (double)(clockGet() - sys_time_0);
            }
        }

#ifdef OPTION_ENABLE_XCP
        if (!XcpEthServerStatus())
            running = false;

        DaqEventVar(mainloop,                                                                                    //
                    A2L_MEAS(counter, "Local counter variable"),                                                 //
                    A2L_MEAS_PHYS(delay_ms, "Loop delay in milliseconds", "ms", 0.0, 1E3),                       //
                    A2L_MEAS_PHYS(ptp_time, "Current PTP clock value in double ns since start", "ns", 0.0, 1E6), //
                    A2L_MEAS_PHYS(sys_time, "Current system clock value in double ns since start", "ns", 0.0, 1E6));
#endif

        // Status print every second, in log levels >=5, more detailed info are printed inside observer, master and client tasks
        if (ptp_log_level == 3 || ptp_log_level == 4) {
            if (std::chrono::steady_clock::now() - clock >= std::chrono::seconds(1)) {
                ptpPrintState(ptp);
                clock = std::chrono::steady_clock::now();
            }
        }

        // delay_ms = rand() % 200 + 1; // 1..200 ms
        delay_ms = 10; // 10 ms
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

    } // while running

#ifdef OPTION_ENABLE_PTP_OBSERVER
#ifdef OPTION_ENABLE_XCP
    // Save observer list to file
    if (ptp_mode == PTP_MODE_AUTO_OBSERVER) {
        if (!ptpSaveObserverList(ptp, "ptptool.obs")) {
            std::cout << "Failed to save observer list" << std::endl;
        }
    }
#endif
#endif

    ptpShutdown(ptp);

#ifdef OPTION_ENABLE_XCP
    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
#endif

    return 0;
}
