#pragma once

/* util.h */
/*
| Code released into public domain, no attribution required
*/

//-------------------------------------------------------------------------------
extern void seed16(unsigned int seed);
extern unsigned int random16();


//-------------------------------------------------------------------------------
extern void fastMathInit();
extern double fastSin(double x);


//-------------------------------------------------------------------------------

// Commandline Options amd Defaults
extern uint32_t gDebugLevel;
extern BOOL gOptionUseTCP;
extern uint16_t gOptionPort;
extern uint8_t gOptionAddr[4];
#if OPTION_ENABLE_XLAPI_V3
extern BOOL gOptionUseXLAPI;
extern uint8_t gOptionXlServerAddr[4];
extern uint8_t gOptionXlServerMac[6];
extern char gOptionXlServerNet[32];
extern char gOptionXlServerSeg[32];
extern BOOL gOptionPCAP;
extern char gOptionPCAP_File[FILENAME_MAX];
#if OPTION_ENABLE_PCAP
extern BOOL gOptionPCAP;
extern char gOptionPCAP_File[FILENAME_MAX];
#endif
#endif
#if OPTION_ENABLE_CDC
extern uint16_t gOptionServerCDCPort;
#endif
#if OPTION_ENABLE_PTP
extern BOOL gOptionPTP;
extern uint16_t gOptionPTPDomain;
#endif

extern void cmdline_usage(const char* appName);
extern BOOL cmdline_parser(int argc, char* argv[]);
