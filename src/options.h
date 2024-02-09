#pragma once

/* options.h */
/*
| Code released into public domain, no attribution required
*/


//-------------------------------------------------------------------------------
// Commandline options and defaults



#if OPTION_ENABLE_TCP
extern BOOL gOptionUseTCP;
#else
#define gOptionUseTCP FALSE
#endif
extern uint16_t gOptionPort;
extern uint8_t gOptionBindAddr[4];

extern void cmdline_usage(const char* appName);
extern BOOL cmdline_parser(int argc, char* argv[]);
