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

#include "xcpLite.h" // for tXcpCalSegIndex

#ifdef OPTION_CAL_PERSISTENCE

#ifdef __cplusplus
extern "C" {
#endif

bool XcpBinWrite(uint8_t page);
bool XcpBinLoad(void);
void XcpBinDelete(void);
bool XcpBinFreezeCalSeg(tXcpCalSegIndex calseg);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // OPTION_CAL_PERSISTENCE
