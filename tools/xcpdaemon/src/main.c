// xcpdaemon - XCP daemon application
//
// See README.md

#include <assert.h>  // for assert
#include <fcntl.h>   // for open(), O_RDWR
#include <getopt.h>  // for getopt_long
#include <glob.h>    // for glob()
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <stdlib.h>  // for strtol
#include <string.h>  // for sprintf
#include <unistd.h>  // for fork(), setsid(), dup2(), getpid()

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
#define OPTION_PID_FILE "/tmp/xcpdaemon.pid"                                                        // PID file written when running as daemon

//-----------------------------------------------------------------------------------------------------

// Signal handlers
static volatile bool running = true;
static volatile bool print_status_requested = false;
static void sig_handler(int sig) { running = false; }
static void sighup_handler(int sig) { print_status_requested = true; }

static void print_usage(const char *prog) {
    printf("Usage: %s [COMMANDS] [OPTIONS] \n", prog);
    printf("Options:\n");
    printf("  -l, --log-level <0-4>  Log level: 0=off 1=error 2=warn 3=info 4=debug [default: %d]\n", OPTION_LOG_LEVEL);
    printf("  -p, --port <port>      XCP server port [default: %d]\n", OPTION_SERVER_PORT);
    printf("  -a, --addr <ip>        Bind address [default: 0.0.0.0]\n");
    printf("      --tcp              Use TCP transport\n");
    printf("      --udp              Use UDP transport (default)\n");
    printf("  -q, --queue-size <n>   Measurement queue size in bytes [default: %d]\n", OPTION_QUEUE_SIZE);
    printf("  -d, --daemonize        Fork to background, write PID to %s\n", OPTION_PID_FILE);
    printf("  -h, --help             Show this help message\n");
    printf("Commands (execute and exit):\n");
    printf("  status                 Print shared memory status and exit\n");
    printf("  clean                  Unlink shared memory and delete main.bin / main.a2l\n");
    printf("  cleanall               Delete all finalized application A2L files, then clean\n");
    printf("  help                   Show this help message\n");
}

typedef enum { CMD_RUN = 0, CMD_STATUS, CMD_CLEAN, CMD_CLEANALL } tCommand;

//-----------------------------------------------------------------------------------------------------

// Fork to background, detach from terminal, write PID file
static int do_daemonize(void) {
    // First fork: parent exits, child is adopted by init
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid > 0)
        exit(0);

    // New session: detach from controlling terminal
    if (setsid() < 0) {
        perror("setsid");
        return -1;
    }

    // Second fork: prevent daemon from reacquiring a controlling terminal
    pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid > 0)
        exit(0);

    // Redirect stdin/stdout/stderr to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO)
            close(fd);
    }

    // Write PID file
    FILE *f = fopen(OPTION_PID_FILE, "w");
    if (f) {
        fprintf(f, "%d\n", (int)getpid());
        fclose(f);
    }

    return 0;
}

#ifdef OPTION_SHM_MODE

// Unlink shared memory regions and delete the master persistence/A2L files
static int do_clean_files(void) {

    remove("/tmp/xcpdata.lock");
    remove("/tmp/xcpqueue.lock");
    remove(SHM_PROJECT_NAME ".bin");
    printf("Deleted '" SHM_PROJECT_NAME ".bin'...\n");
    remove(SHM_PROJECT_NAME ".a2l");
    printf("Deleted '" SHM_PROJECT_NAME ".a2l'...\n");

    return 0;
}

static int do_unlink(void) {
    printf("Unlinking '/xcpqueue'...\n");
    platformShmUnlink("/xcpqueue");
    printf("Unlinking '/xcpdata'...\n");
    platformShmUnlink("/xcpdata");
    return 0;
}

static int do_clean(void) {
    do_clean_files();
    do_unlink();
    return 0;
}

// Delete all finalized application A2L files listed in SHM, then run clean
static int do_cleanall(void) {

    do_clean_files();

    // Remove all application A2L files listed in SHM, if any
    size_t shm_size = sizeof(tXcpData);
    gXcpData = (tXcpData *)platformShmOpenAttach("/xcpdata", &shm_size);
    if (gXcpData != NULL) {
        uint8_t count = (uint8_t)atomic_load(&gXcpData->shm_header.app_count);
        printf("Deleting application A2L files for %u registered application(s):\n", (unsigned)count);
        for (uint8_t i = 0; i < count; i++) {
            const char *project_name = gXcpData->shm_header.app_list[i].project_name;
            if (project_name[0] != '\0') {
                // A2L filename is <project_name>_<epk>.a2l — use project_name as prefix glob
                char pattern[XCP_PROJECT_NAME_MAX_LENGTH + 8];
                snprintf(pattern, sizeof(pattern), "%s*.a2l", project_name);
                glob_t g;
                if (glob(pattern, 0, NULL, &g) == 0) {
                    for (size_t j = 0; j < g.gl_pathc; j++) {
                        if (remove(g.gl_pathv[j]) == 0)
                            printf("  Deleted '%s'\n", g.gl_pathv[j]);
                        else
                            printf("  Cannot delete '%s': %s%s\n", g.gl_pathv[j], strerror(errno),
                                   errno == EBUSY ? " (file is open by another process, stop applications first)" : "");
                    }
                    globfree(&g);
                } else {
                    printf("  No files matching '%s' found\n", pattern);
                }
            }
        }
        platformShmClose("/xcpdata", gXcpData, shm_size, false);
        gXcpData = NULL;
    }

    // Unlink shared memory regions
    do_unlink();

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
#endif // SHM_MODE

//-----------------------------------------------------------------------------------------------------

int main(int argc, char *argv[]) {

    // Line-buffer stdout so every printf() line is immediately visible in the
    // systemd journal, even when stdout is a pipe rather than a terminal.
    setvbuf(stdout, NULL, _IOLBF, 0);

    // Runtime options (overrideable from command line)
    uint8_t log_level = OPTION_LOG_LEVEL;
    uint16_t port = OPTION_SERVER_PORT;
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    bool use_tcp = OPTION_USE_TCP;
    uint32_t queue_size = OPTION_QUEUE_SIZE;
    bool daemonize = false;
    tCommand cmd = CMD_RUN;

    // Long-only option identifiers (> 127)
    enum { OPT_TCP = 256, OPT_UDP };

    static const struct option long_opts[] = {{"log-level", required_argument, NULL, 'l'}, {"port", required_argument, NULL, 'p'}, {"addr", required_argument, NULL, 'a'},
                                              {"tcp", no_argument, NULL, OPT_TCP},         {"udp", no_argument, NULL, OPT_UDP},    {"queue-size", required_argument, NULL, 'q'},
                                              {"daemonize", no_argument, NULL, 'd'},       {"help", no_argument, NULL, 'h'},       {NULL, 0, NULL, 0}};
    int opt;
    while ((opt = getopt_long(argc, argv, "l:p:a:q:dh", long_opts, NULL)) != -1) {
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
        case 'd':
            daemonize = true;
            break;
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
        else if (strcmp(subcmd, "help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown command '%s'\n", subcmd);
            print_usage(argv[0]);
            return 1;
        }
    }

#ifdef OPTION_SHM_MODE

    // Handle action commands (execute and exit)
    if (cmd == CMD_CLEAN) {
        do_clean();
        return 0;
    }

    if (cmd == CMD_STATUS) {
        return do_status();
    }

    if (cmd == CMD_CLEANALL) {
        return do_cleanall();
    }

#endif // SHM_MODE

    if (daemonize) {
        printf("Starting XCP daemon V%s in background (PID file: %s)\n", OPTION_PROJECT_VERSION, OPTION_PID_FILE);
        fflush(stdout);
        if (do_daemonize() < 0)
            return 1;
    } else {
        printf("\nXCP daemon V%s\n", OPTION_PROJECT_VERSION);
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGHUP, sighup_handler);

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(log_level);

    // Initialize the XCP singleton, activate SHM XCP server
    if (!XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_SHM_SERVER)) {
        printf("XCP daemon initialization failed, exit\n");
        return 1;
    }

    // Initialize the XCP Server
    if (!XcpEthServerInit(addr, port, use_tcp, queue_size)) {
        printf("Server initialization failed, exit\n");
        return 1;
    }

    // Enable A2L generation and prepare the A2L file, finalize the A2L file on XCP connect
    if (!A2lInit(addr, port, use_tcp, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        printf("A2L initialization failed, exit\n");
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
#endif // SHM_MODE

    DBG_PRINT3("\nStart XCP daemon, press Ctrl-C to stop...\n");
    // running = false;
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

        // SIGHUP: print shared memory status
        if (print_status_requested) {
            print_status_requested = false;
            XcpShmDebugPrint();
        }
#endif // SHM_MODE

        // Sleep for the specified delay parameter in milliseconds
        sleepMs(delay_ms);

    } // while (running)

    DBG_PRINT3("\nStop XCP daemon\n");

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server
    if (daemonize)
        remove(OPTION_PID_FILE);
    return 0;
}
