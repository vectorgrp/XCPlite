// xcpdaemon - XCP daemon application
// This application serves as a daemon for multi application measurement and calibration use cases
// Just another XCP instrumented application in SHM mode, but it is configured to be the only XCP server
// Creates the master A2L file and manages the binary calibration data persistence file
// Must not be started first
// It has own measurement and calibration objects to monitor the system and multiple XCP /SHM instrumented applications

#include <assert.h>  // for assert
#include <getopt.h>  // for getopt_long
#include <glob.h>    // for glob()
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <stdlib.h>  // for strtol
#include <string.h>  // for sprintf

#include "a2l.h"        // for A2l generation
#include "dbg_print.h"  // for DBG_LEVEL, DBG_PRINT, ...
#include "platform.h"   // for platform defines (WIN_, LINUX_, MACOS_)
#include "shm.h"        // for A2L generation
#include "xcp.h"        // for XCP protocol definitions
#include "xcplib.h"     // for application programming interface
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcplite.h"    // for XCP protocol layer interface functions

#ifdef OPTION_SHM_MODE
extern tXcpData *gXcpData;
extern tXcpLocalData gXcpLocalData;
#endif

//-----------------------------------------------------------------------------------------------------

// XCP parameters
#define OPTION_PROJECT_NAME "xcpdaemon"                                             // A2L project name
#define OPTION_PROJECT_VERSION "105"                                                // EPK version string (default, is contructed from the applications version strings)
#define OPTION_USE_TCP false                                                        // TCP or UDP
#define OPTION_SERVER_PORT 5555                                                     // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}                                             // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 32)                                               // Size of the measurement queue in bytes
#define OPTION_XCP_MODE (XCP_MODE_PERSISTENCE | XCP_MODE_SHM | XCP_MODE_SHM_SERVER) // XCP mode
#define OPTION_A2L_MODE (A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS) // A2L generation mode
#define OPTION_LOG_LEVEL 3                                                                          // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------

// Signal handler for clean shutdown
static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("       %s [OPTIONS] status\n", prog);
    printf("       %s [OPTIONS] clean\n", prog);
    printf("       %s [OPTIONS] cleanall\n", prog);
    printf("Options:\n");
    printf("  -l, --log-level <0-4>  Log level: 0=off 1=error 2=warn 3=info 4=debug [default: %d]\n", OPTION_LOG_LEVEL);
    printf("  -p, --port <port>      XCP server port [default: %d]\n", OPTION_SERVER_PORT);
    printf("  -a, --addr <ip>        Bind address [default: 0.0.0.0]\n");
    printf("      --tcp              Use TCP transport\n");
    printf("      --udp              Use UDP transport (default)\n");
    printf("  -q, --queue-size <n>   Measurement queue size in bytes [default: %d]\n", OPTION_QUEUE_SIZE);
    printf("  -h, --help             Show this help message\n");
    printf("Commands (execute and exit):\n");
    printf("  status                 Print shared memory status and exit\n");
    printf("  clean                  Unlink shared memory and delete master.bin / master.a2l\n");
    printf("  cleanall               Delete all finalized application A2L files, then clean\n");
}

typedef enum { CMD_RUN = 0, CMD_STATUS, CMD_CLEAN, CMD_CLEANALL } tCommand;

//-----------------------------------------------------------------------------------------------------

// Unlink shared memory regions and delete the master persistence/A2L files
static void do_clean(void) {
    printf("Unlinking '/xcpdata'...\n");
    platformShmUnlink("/xcpdata");
    printf("Unlinking '/xcpqueue'...\n");
    platformShmUnlink("/xcpqueue");
    remove("/tmp/xcpdata.lock");
    remove("/tmp/xcpqueue.lock");
    printf("Deleting '" SHM_PROJECT_NAME ".bin'...\n");
    remove(SHM_PROJECT_NAME ".bin");
    printf("Deleting '" SHM_PROJECT_NAME ".a2l'...\n");
    remove(SHM_PROJECT_NAME ".a2l");
    printf("Done.\n");
}

#ifdef OPTION_SHM_MODE

// Delete all finalized application A2L files listed in SHM, then run clean
static int do_cleanall(void) {
    size_t shm_size = sizeof(tXcpData);
    gXcpData = (tXcpData *)platformShmOpenAttach("/xcpdata", &shm_size);
    if (gXcpData == NULL) {
        printf("No shared memory found\n");
        return 1;
    }

    uint8_t count = XcpShmGetAppCount();
    printf("Deleting application A2L files for %u registered application(s):\n", (unsigned)count);
    for (uint8_t i = 0; i < count; i++) {
        if (XcpShmIsA2lFinalized(i)) {
            const char *a2l_name = gXcpData->shm_header.app_list[i].a2l_name;
            if (a2l_name[0] != '\0') {
                char pattern[XCP_A2L_FILENAME_MAX_LENGTH + 8];
                snprintf(pattern, sizeof(pattern), "%s*.a2l", a2l_name);
                glob_t g;
                if (glob(pattern, 0, NULL, &g) == 0) {
                    for (size_t j = 0; j < g.gl_pathc; j++) {
                        if (remove(g.gl_pathv[j]) == 0)
                            printf("  Deleted '%s'\n", g.gl_pathv[j]);
                        else
                            printf("  Cannot delete '%s'\n", g.gl_pathv[j]);
                    }
                    globfree(&g);
                } else {
                    printf("  No files matching '%s' found\n", pattern);
                }
            }
        }
    }

    printf("Unlinking '/xcpdata'...\n");
    platformShmClose("/xcpdata", gXcpData, shm_size, true);
    gXcpData = NULL;

    do_clean();
    return 0;
}

// Print the shared memory status and exit
static int do_status(void) {
    size_t shm_size = sizeof(tXcpData);
    gXcpData = (tXcpData *)platformShmOpenAttach("/xcpdata", &shm_size);
    if (gXcpData == NULL) {
        printf("No shared memory found\n");
        return 1;
    }
    XcpShmDebugPrint();
    platformShmClose("/xcpdata", gXcpData, shm_size, false);
    gXcpData = NULL;
    return 0;
}
#endif // OPTION_SHM_MODE

//-----------------------------------------------------------------------------------------------------

int main(int argc, char *argv[]) {

    // Runtime options (overrideable from command line)
    uint8_t log_level = OPTION_LOG_LEVEL;
    uint16_t port = OPTION_SERVER_PORT;
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    bool use_tcp = OPTION_USE_TCP;
    uint32_t queue_size = OPTION_QUEUE_SIZE;
    tCommand cmd = CMD_RUN;

    // Long-only option identifiers (> 127)
    enum { OPT_TCP = 256, OPT_UDP };

    static const struct option long_opts[] = {
        {"log-level", required_argument, NULL, 'l'}, {"port", required_argument, NULL, 'p'},       {"addr", required_argument, NULL, 'a'}, {"tcp", no_argument, NULL, OPT_TCP},
        {"udp", no_argument, NULL, OPT_UDP},         {"queue-size", required_argument, NULL, 'q'}, {"help", no_argument, NULL, 'h'},       {NULL, 0, NULL, 0}};
    int opt;
    while ((opt = getopt_long(argc, argv, "l:p:a:q:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'l': {
            char *end;
            long v = strtol(optarg, &end, 10);
            if (*end != '\0' || v < 0 || v > 4) {
                fprintf(stderr, "Error: --log-level must be 0..4\n");
                return 1;
            }
            log_level = (uint8_t)v;
            break;
        }
        case 'p': {
            char *end;
            long v = strtol(optarg, &end, 10);
            if (*end != '\0' || v < 1 || v > 65535) {
                fprintf(stderr, "Error: --port must be 1..65535\n");
                return 1;
            }
            port = (uint16_t)v;
            break;
        }
        case 'a': {
            unsigned int a0, a1, a2, a3;
            if (sscanf(optarg, "%u.%u.%u.%u", &a0, &a1, &a2, &a3) != 4 || a0 > 255 || a1 > 255 || a2 > 255 || a3 > 255) {
                fprintf(stderr, "Error: invalid address '%s'\n", optarg);
                return 1;
            }
            addr[0] = (uint8_t)a0;
            addr[1] = (uint8_t)a1;
            addr[2] = (uint8_t)a2;
            addr[3] = (uint8_t)a3;
            break;
        }
        case OPT_TCP:
            use_tcp = true;
            break;
        case OPT_UDP:
            use_tcp = false;
            break;
        case 'q': {
            char *end;
            long long v = strtoll(optarg, &end, 10);
            if (*end != '\0' || v < 1024 || v > 0x7FFFFFFF) {
                fprintf(stderr, "Error: --queue-size must be >= 1024\n");
                return 1;
            }
            queue_size = (uint32_t)v;
            break;
        }
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    // Parse optional subcommand (positional argument after options)
    if (optind < argc) {
        const char *subcmd = argv[optind];
        if (strcmp(subcmd, "clean") == 0)
            cmd = CMD_CLEAN;
        else if (strcmp(subcmd, "cleanall") == 0)
            cmd = CMD_CLEANALL;
        else if (strcmp(subcmd, "status") == 0)
            cmd = CMD_STATUS;
        else {
            fprintf(stderr, "Unknown command '%s'\n", subcmd);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Handle action commands (execute and exit)
    if (cmd == CMD_CLEAN) {
        do_clean();
        return 0;
    }

#ifdef OPTION_SHM_MODE

    if (cmd == CMD_STATUS) {
        return do_status();
    }

    if (cmd == CMD_CLEANALL) {
        return do_cleanall();
    }

#endif // OPTION_SHM_MODE

    printf("\nXCP daemon V%s\n", OPTION_PROJECT_VERSION);
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(log_level);

    // Initialize the XCP singleton, activate SHM XCP server
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_SHM_SERVER);

    // Initialize the XCP Server
    if (!XcpEthServerInit(addr, port, use_tcp, queue_size)) {
        return 1;
    }

    // Enable A2L generation and prepare the A2L file, finalize the A2L file on XCP connect
    if (!A2lInit(addr, port, use_tcp, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    uint32_t delay_ms = 10;
    uint16_t counter = 0;

    // Register measurement variables located on stack
    DaqCreateEvent(xcpdaemon);
    A2lSetStackAddrMode(xcpdaemon);
    A2lCreateMeasurement(counter, "Mainloop counter");
    A2lCreateParameter(delay_ms, "Mainloop delay", "ms", 1, 1000);

    A2lSetAbsoluteAddrMode(xcpdaemon);

#ifdef OPTION_SHM_MODE

    // Local
    A2lCreateMeasurement(gXcpLocalData.daq_start_clock, "DAQ start clock");
    A2lCreateMeasurement(gXcpLocalData.shm_app_id, "Application id");
    A2lCreateMeasurement(gXcpLocalData.init_mode, "Initialization mode");
    // A2lCreateMeasurementString(gXcpLocalData.project_name, "Project name");
    // A2lCreateMeasurementString(gXcpLocalData.epk, "EPK version");

    // Shared
    A2lSetRelativeAddrMode(xcpdaemon, gXcpData);
    A2lTypedefBegin(tXcpData, gXcpData, "XCP shared state typedef");
    A2lTypedefMeasurementComponent(session_status, "XCP session status");
    // A2lTypedefMeasurementComponent(daq_running, "DAQ is running "); @@@@ TODO Does not work for atomics
    A2lTypedefMeasurementComponent(daq_overflow_count, "DAQ overflow count");
    A2lTypedefEnd();
    A2lCreateTypedefReference(gXcpData, tXcpData, "XCP shared state");

    // A2lCreateMeasurement(gXcpData->shm_header.app_count, "Application count");
    // A2lTypedefBegin(tXcpShmApp, &gXcpData->shm_header.app_list, "Calibration parameters typedef");
    // A2lTypedefMeasurementComponent(pid, "Process id ");
    // A2lTypedefMeasurementComponent(is_server, "Is server ");
    // A2lTypedefMeasurementComponent(is_leader, "Is leader ");
    // A2lTypedefEnd();
    // A2lCreateTypedefReference(gXcpData, tXcpShmApp, "Shared memory");

    if (DBG_LEVEL >= 3) {
        // Print current status of the shared memory
        printf("\n--------------------------------------------------------------\n");
        printf("Shared memory status after initialization:\n");
        XcpShmDebugPrint();
        printf("--------------------------------------------------------------\n");
    }
#endif // OPTION_SHM_MODE

    DBG_PRINT3("\nStart XCP daemon, press Ctrl-C to stop...\n");

    while (running) {

        counter++;

        // Check server status
        if (!XcpEthServerStatus()) {
            printf("\nXCP Server failed\n");
            break;
        }

        // Every second
        if (counter % (1000 / delay_ms) == 0) {
        }

#ifdef OPTION_SHM_MODE
        DaqTriggerEventExt(xcpdaemon, gXcpData);
#endif // OPTION_SHM_MODE

        // Sleep for the specified delay parameter in milliseconds
        sleepMs(delay_ms);

    } // while (running)

    DBG_PRINT3("\nStop XCP daemon\n");

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
    return 0;
}
