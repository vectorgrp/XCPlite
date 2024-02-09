/*----------------------------------------------------------------------------
| File:
|   options.c
|
| Description:
|   Cmdline parser and options
|
|   Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "platform.h"
#include "dbg_print.h"
#include "options.h"


//-----------------------------------------------------------------------------------------------------
// Options

// Commandline Options and Defaults

#if OPTION_ENABLE_TCP
BOOL gOptionUseTCP = OPTION_USE_TCP;
#endif
uint16_t gOptionPort = OPTION_SERVER_PORT;
uint8_t gOptionBindAddr[4] = OPTION_SERVER_ADDR;




//-----------------------------------------------------------------------------------------------------
// cmd line parser

#if defined(_WIN) || defined(_LINUX)

// help
void cmdline_usage(const char* appName) {

    printf(
        "\n"
        "Usage:\n"
        "  %s [options]\n"
        "\n"
        "  Options:\n"
        "    -log <x>         Set console log output verbosity to x (default: 2)\n"
        "    -bind <ipaddr>   XCP server adapter IP address to bind (default is ANY (0.0.0.0))\n"
        "    -port <portname> XCP server port (default is 5555)\n"
#if OPTION_ENABLE_TCP
#if OPTION_USE_TCP
        "    -udp             Use UDP for XCP\n"
#else
        "    -tcp             Use TCP for XCP\n"
#endif
#endif
        "\n"
        "  Keys:\n"
        "    ESC              Exit\n"
      "%s\n",
        appName,
        ""
    );
}


BOOL cmdline_parser(int argc, char* argv[]) {

    // Parse commandline
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            cmdline_usage(argv[0]);
            return FALSE;
        }
#if OPTION_ENABLE_DBG_PRINTS
        else if (strcmp(argv[i], "-log") == 0) {
          if (++i < argc) {
            extern unsigned int gDebugLevel;
            if (sscanf(argv[i], "%u", &gDebugLevel) == 1) {
              printf("Debug output level = %u\n", gDebugLevel);
            }
          }
        }
#endif
        else if (strcmp(argv[i], "-bind") == 0) {
            if (++i < argc) {
                if (inet_pton(AF_INET, argv[i], &gOptionBindAddr)) {
                    printf("Bind to ETH adapter with IP address %s\n", argv[i]);
                }
            }
        }
        else if (strcmp(argv[i], "-port") == 0) {
            if (++i < argc) {
                if (sscanf(argv[i], "%hu", &gOptionPort) == 1) {
                    printf("Set XCP port to %u\n", gOptionPort);
                }
            }
        }
#if OPTION_ENABLE_TCP
        else if (strcmp(argv[i], "-tcp") == 0) {
            gOptionUseTCP = TRUE;
            printf("Use TCP\n");
        }
        else if (strcmp(argv[i], "-udp") == 0) {
            gOptionUseTCP = FALSE;
            printf("Use UDP\n");
        }
#endif
  
        else {
            printf("Unknown command line option %s\n", argv[i]);
            return FALSE;
        }
    }
    return TRUE;
}

#endif
