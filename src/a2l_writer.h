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
#define A2L_MODE_EVENT_GROUPS 0x01     // Create a root group for all events assuming there is a subgroup for each event with the event name
#define A2L_MODE_EVENT_CONVERSION 0x02 // Create a enum conversion with all event ids
#define A2L_MODE_PREFIX_SYMBOLS 0x04   // Prefix eventnames and calibration segment names with project name (XcpGetProjectName())
#define A2L_MODE_INCLUDE_AML_FILE 0x08 // Include AML file (e.g., XCP_104.aml) with /include directive, instead of embedding the AML file content into the A2L file
bool A2lWriter(const char *a2l_filename, uint8_t a2l_mode, uint16_t include_count, const char **include_files, const uint8_t *addr, uint16_t port, bool useTCP);
