#pragma once
#define __PERSISTENCY_H__

/*----------------------------------------------------------------------------
| File:
|   persistency.h
|
| Description:
|   XCPlite internal header file for persistency.c
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "xcpLite.h" // for tXcpCalSegIndex

#ifdef __cplusplus
extern "C" {
#endif

#if defined(XCP_ENABLE_CALSEG_LIST) && defined(XCP_ENABLE_DAQ_EVENT_LIST)

bool XcpBinWrite(const char *filename);
bool XcpBinLoad(const char *filename, const char *epk);
bool XcpBinFreezeCalSeg(tXcpCalSegIndex calseg);

#endif // XCP_ENABLE_CALSEG_LIST || XCP_ENABLE_DAQ_EVENT_LIST

#ifdef __cplusplus
} // extern "C"
#endif
