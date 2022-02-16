/*----------------------------------------------------------------------------
| File:
|   util.c
|
| Description:
|   Some helper functions
|
|   Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "main_cfg.h"
#include "platform.h"
#include "util.h"




/**************************************************************************/
// cmd line parser
/**************************************************************************/

// help
void cmdline_usage(const char* appName) {
        printf(
        "\n"
        "Usage:\n"
        "  %s [options]\n"
        "\n"
        "  Options:\n"
        "    -dx              Set output verbosity to x (default is 1)\n"
        "    -bind <ipaddr>   Server IP address for socket bind (default is localhost, any = 0.0.0.0)\n"
        "    -port <portname> Server port (default is 5555)\n"
#ifdef OPTION_ENABLE_TCP
            "    -tcp             Use TCP (default is UDP)\n"
#endif
#if OPTION_ENABLE_PTP
            "    -ptp [domain]    Enable PTP (default is off, default domain is 0)\n"
#endif
#if OPTION_ENABLE_XLAPI_V3
        "    -v3              V3 enable (default: off)\n"
        "    -net <netname>   V3 network (default: NET1)\n"
        "    -seg <segname>   V3 segment (default: SEG1)\n"
        "    -addr <mac>      V3 endpoint IPv4 addr (default: 192.168.0.200)\n"
        "    -mac <mac>       V3 endpoint MAC addr (default: 0xdc:0xa6:0x32:0x7e:0x66:0xdc)\n"
#if OPTION_ENABLE_PCAP
            "    -pcap <file>     V3 log all ethernet frames to PCAP file\n"
#endif
#endif
        "\n", appName
    );
}


//-----------------------------------------------------------------------------------------------------
// Options

// Commandline Options amd Defaults

uint32_t gDebugLevel = OPTION_DEBUG_LEVEL;

BOOL gOptionUseTCP = FALSE;
uint16_t gOptionPort = OPTION_SERVER_PORT;
uint8_t gOptionAddr[4] = OPTION_SERVER_ADDR;

#if OPTION_ENABLE_XLAPI_V3

BOOL gOptionUseXLAPI = FALSE;
uint8_t gOptionXlServerAddr[4] = OPTION_SERVER_XL_ADDR;
uint8_t gOptionXlServerMac[6] = OPTION_SERVER_XL_MAC;
char gOptionXlServerNet[32] = OPTION_SERVER_XL_NET;
char gOptionXlServerSeg[32] = OPTION_SERVER_XL_SEG;
BOOL gOptionPCAP = FALSE;
char gOptionPCAP_File[FILENAME_MAX];

#if OPTION_ENABLE_PCAP
BOOL gOptionPCAP = FALSE;
char gOptionPCAP_File[FILENAME_MAX] = APP_NAME ".pcap";
#endif

#endif




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
        }
        else if (strcmp(argv[i], "-bind") == 0) {
            if (++i < argc) {
                if (inet_pton(AF_INET, argv[i], &gOptionAddr)) {
                    printf("Set ip addr for bind to %s\n", argv[i]);
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
#ifdef OPTION_ENABLE_TCP
        else if (strcmp(argv[i], "-tcp") == 0) {
            gOptionUseTCP = !gOptionUseTCP;
        }
#endif
#if OPTION_ENABLE_XLAPI_V3
        else if (strcmp(argv[i], "-v3") == 0) {
            gOptionUseXLAPI = TRUE;
        }
        else if (strcmp(argv[i], "-net") == 0) {
            gOptionUseXLAPI = TRUE;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                strcpy(gOptionXlServerNet, argv[++i]);
                printf("Set XL net to %s\n", argv[i]);
            }
        }
        else if (strcmp(argv[i], "-seg") == 0) {
            gOptionUseXLAPI = TRUE;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                strcpy(gOptionXlServerSeg, argv[++i]);
                printf("Set XL seg to %s\n", argv[i]);
            }
        }
        else if (strcmp(argv[i], "-addr") == 0) {
            if (++i < argc) {
                if (inet_pton(AF_INET, argv[i], &gOptionXlServerAddr)) {
                    printf("Set XL ip addr to %s\n", argv[i]);
                }
            }
        }
        else if (strcmp(argv[i], "-mac") == 0) {
            if (++i < argc) {
                unsigned int i1, i2, i3, i4, i5, i6;
                if (sscanf(argv[i + 1], "%u:%u:%u:%u:%u:%u", &i1, &i2, &i3, &i4, &i5, &i6) == 6) {
                    printf("Set XL mac addr to %u:%u:%u:%u:%u:%u\n", i1, i2, i3, i4, i5, i6);
                    gOptionXlServerMac[0] = (uint8_t)i1;
                    gOptionXlServerMac[1] = (uint8_t)i2;
                    gOptionXlServerMac[2] = (uint8_t)i3;
                    gOptionXlServerMac[3] = (uint8_t)i4;
                    gOptionXlServerMac[4] = (uint8_t)i5;
                    gOptionXlServerMac[5] = (uint8_t)i6;
                }
            }
        }

#if OPTION_ENABLE_PCAP
        else if (strcmp(argv[i], "-pcap") == 0) {
            if (++i < argc) {
                strcpy(gOptionPCAP_File, argv[i]);
            }
            printf("Capture to %s\n", gOptionPCAP_File);
            gOptionPCAP = TRUE;
        }
#endif
#endif // V3
        else {
            printf("Unknown command line option %s\n", argv[i]);
            return FALSE;
        }
    }

    if (gDebugLevel) printf("Set screen output verbosity to %u\n", gDebugLevel);

#ifdef OPTION_ENABLE_TCP
    printf("Using %s socket\n", gOptionUseTCP ? "TCP" : "UDP");
#endif
#if OPTION_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        printf("Using XL-API V3 with %u.%u.%u.%u:%u\n", gOptionXlServerAddr[0], gOptionXlServerAddr[1], gOptionXlServerAddr[2], gOptionXlServerAddr[3], gOptionPort);
    }
#endif
    return TRUE;
}

