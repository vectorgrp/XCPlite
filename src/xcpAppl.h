#pragma once

/* xcpAppl.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */


//-------------------------------------------------------------------------------
// Debug prints

#ifdef XCP_ENABLE_DEBUG_PRINTS
extern uint32_t gDebugLevel;
#define XCP_DBG_LEVEL gDebugLevel

#define XCP_DBG_PRINTF_ERROR(format, ...) printf(format, __VA_ARGS__)
#define XCP_DBG_PRINTF(level, format, ...) if (XCP_DBG_LEVEL>=level) printf(format, __VA_ARGS__)
#define XCP_DBG_PRINTF1(format, ...) if (XCP_DBG_LEVEL>=1) printf(format, __VA_ARGS__)
#define XCP_DBG_PRINTF2(format, ...) if (XCP_DBG_LEVEL>=2) printf(format, __VA_ARGS__)
#define XCP_DBG_PRINTF3(format, ...) if (XCP_DBG_LEVEL>=3) printf(format, __VA_ARGS__)
#define XCP_DBG_PRINTF4(format, ...) if (XCP_DBG_LEVEL>=4) printf(format, __VA_ARGS__)

#define XCP_DBG_PRINT_ERROR(s) printf(s)
#define XCP_DBG_PRINT(level, s) if (XCP_DBG_LEVEL>=level) printf(s)
#define XCP_DBG_PRINT1(s) if (XCP_DBG_LEVEL>=1) printf(s)
#define XCP_DBG_PRINT2(s) if (XCP_DBG_LEVEL>=2) printf(s)
#define XCP_DBG_PRINT3(s) if (XCP_DBG_LEVEL>=3) printf(s)
#define XCP_DBG_PRINT4(s) if (XCP_DBG_LEVEL>=4) printf(s)

#else

#define XCP_DBG_PRINTF_ERROR(format, ...)
#define XCP_DBG_PRINTF(level, format, ...)
#define XCP_DBG_PRINTF1(format, ...)
#define XCP_DBG_PRINTF2(format, ...)
#define XCP_DBG_PRINTF3(format, ...)
#define XCP_DBG_PRINTF4(format, ...)

#define XCP_DBG_PRINT_ERROR(format, ...)
#define XCP_DBG_PRINT(level, format, ...)
#define XCP_DBG_PRINT1(format, ...)
#define XCP_DBG_PRINT2(format, ...)
#define XCP_DBG_PRINT3(format, ...)
#define XCP_DBG_PRINT4(format, ...)

#endif

