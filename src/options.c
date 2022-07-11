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
#include "options.h"
#include "platform.h"


//-----------------------------------------------------------------------------------------------------
// Options

// Commandline Options and Defaults

uint32_t gDebugLevel = OPTION_DEBUG_LEVEL;

#if OPTION_ENABLE_TCP
BOOL gOptionUseTCP = OPTION_USE_TCP;
#endif
uint16_t gOptionPort = OPTION_SERVER_PORT;
uint8_t gOptionBindAddr[4] = OPTION_SERVER_ADDR;


//-----------------------------------------------------------------------------------------------------
// cmd line parser


// help
void cmdline_usage(const char* appName) {

#if OPTION_ENABLE_XLAPI_V3 || OPTION_ENABLE_XLAPI_IAP
    const uint8_t xl_addr[4] = OPTION_SERVER_XL_ADDR;
    const uint8_t xl_mac[6] = OPTION_SERVER_XL_MAC;
    const char* xl_net = OPTION_SERVER_XL_NET;
    const char* xl_seg = OPTION_SERVER_XL_SEG;
#endif

    printf(
        "\n"
        "Usage:\n"
        "  %s [options]\n"
        "\n"
        "  Options:\n"
        "    -dx              Set output verbosity to x (default is 1)\n"
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
        char c;
        if (strcmp(argv[i], "-h") == 0) {
            cmdline_usage(argv[0]);
            return FALSE;
        }
        else if (sscanf(argv[i], "-d%c", &c) == 1) {
            gDebugLevel = c - '0';
            printf("Debug output level = %u\n", gDebugLevel);
        }
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
        }
#endif
        else {
            printf("Unknown command line option %s\n", argv[i]);
            return FALSE;
        }
    }

    return TRUE;
}

