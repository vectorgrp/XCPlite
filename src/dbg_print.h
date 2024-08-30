#pragma once

/* dbg_print.h */
/*
| Code released into public domain, no attribution required
*/


//-------------------------------------------------------------------------------
// Debug print

#if OPTION_ENABLE_DBG_PRINTS

extern unsigned int gDebugLevel;
#define DBG_LEVEL gDebugLevel

/*
1 - Error
2 - Warn
3 - Info
4 - Trace
5 - Debug 
*/

#define DBG_PRINTF(level, format, ...) if (DBG_LEVEL>=level) printf(format, __VA_ARGS__)
#define DBG_PRINTF_ERROR(format, ...) if (DBG_LEVEL>=1) printf(format, __VA_ARGS__)
#define DBG_PRINTF_WARNING(format, ...) if (DBG_LEVEL>=2) printf(format, __VA_ARGS__)
#define DBG_PRINTF3(format, ...) if (DBG_LEVEL>=3) printf(format, __VA_ARGS__)
#define DBG_PRINTF4(format, ...) if (DBG_LEVEL>=4) printf(format, __VA_ARGS__)
#define DBG_PRINTF5(format, ...) if (DBG_LEVEL>=5) printf(format, __VA_ARGS__)

#define DBG_PRINT(level, format) if (DBG_LEVEL>=level) printf(format)
#define DBG_PRINT_ERROR(format) if (DBG_LEVEL>=1) printf(format)
#define DBG_PRINT_WARNING(format) if (DBG_LEVEL>=2) printf(format)
#define DBG_PRINT3(format) if (DBG_LEVEL>=3) printf(format)
#define DBG_PRINT4(format) if (DBG_LEVEL>=4) printf(format)
#define DBG_PRINT5(format) if (DBG_LEVEL>=5) printf(format)

#else

#undef DBG_LEVEL

#define DBG_PRINTF(level, s, ...) 
#define DBG_PRINTF_ERROR(s, ...) // printf(s,__VA_ARGS__)
#define DBG_PRINTF_WARNING(s, ...) 
#define DBG_PRINTF3(s, ...) 
#define DBG_PRINTF4(s, ...) 
#define DBG_PRINTF5(s, ...) 

#define DBG_PRINT(level, s, ...) 
#define DBG_PRINT_ERROR(s, ...) // printf(s,__VA_ARGS__)
#define DBG_PRINT_WARNING(s, ...) 
#define DBG_PRINT3(s, ...) 
#define DBG_PRINT4(s, ...) 
#define DBG_PRINT5(s, ...) 

#endif

