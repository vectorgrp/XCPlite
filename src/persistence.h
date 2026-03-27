#pragma once
#define __PERSISTENCE_H__

/*----------------------------------------------------------------------------
| File:
|   persistence.h
|
| Description:
|   XCPlite internal header file for persistence.c
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcplite.h"    // for tXcpCalSegIndex

#ifdef OPTION_ENABLE_PERSISTENCE

#ifdef __cplusplus
extern "C" {
#endif

// Create the binary file with the current default pages
bool XcpBinWrite(const char *epk);

// Load the binary file and create calibration segment marked as preloaded
bool XcpBinLoad(void);

// Delete the binary file
void XcpBinDelete(void);

// Freeze current working page data of the specified calibration segment in the binary file
bool XcpBinFreezeCalSeg(tXcpCalSegIndex calseg);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // OPTION_ENABLE_PERSISTENCE
