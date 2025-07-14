#pragma once
#define __PERSISTENCY_H__

#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "xcpLite.h" // for tXcpCalSegIndex

#ifdef __cplusplus
extern "C" {
#endif

bool XcpBinWrite(const char *filename);
bool XcpBinLoad(const char *filename, const char *epk);
bool XcpBinFreezeCalSeg(tXcpCalSegIndex calseg);

#ifdef __cplusplus
} // extern "C"
#endif
