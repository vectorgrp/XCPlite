#pragma once
#define __A2L_WRITER_H__

/*----------------------------------------------------------------------------
| File:
|   a2l_writer.h
|
| Description:
|   XCPlite internal header file for A2L writer
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

// Write the main A2L file skeleton, with options below and a list of partial A2L files with measurements, characteristics, and typedefs to include
bool A2lWriter(const char *a2l_filename, uint8_t a2l_mode, uint16_t include_count, const char **include_files, const uint8_t *addr, uint16_t port, bool useTCP);
